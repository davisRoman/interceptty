/*     interceptty.c
 *
 * This file is an adaptation of ttysnoops.c, from the ttysnoop-0.12d
 * package It was originally written by Carl Declerck, Ulrich
 * Callmeir, Carl Declerck, and Josh Bailey.  They deserve all of the * credit for the clever parts.  I, on the other hand, deserve all of
 * the blame for whatever I broke.  Please do not email the original
 * authors of ttysnoop about any problems with interceptty.
 *
 */

/* $Id: interceptty.c,v 7.12 2004/09/05 23:01:35 gifford Exp $ */

#include "config.h"

#include <sys/types.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <grp.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <netdb.h>
#include <pwd.h>
#include <grp.h>
#include <sys/time.h>
#include <time.h>

#include "bsd-openpty.h"
#include "common.h"

#ifndef O_NOCTTY
#define O_NOCTTY 0
#endif

#define DEFAULT_FRONTEND "/tmp/interceptty"

struct sockaddr_in inet_resolve(const char *sockname);


#define BUFF_SIZE	256

char buff[BUFF_SIZE];

char ttynam[TTYLEN+1] = "";
int ptyfd = -1;

int fdmax = 0;

char    *backend = NULL,
  *frontend = DEFAULT_FRONTEND,
  *settings = NULL,
  timestamp = 0,
  use_eol_ch = 0,
  print_hex = 1,
  print_chrs = 1;
char    eol_ch = 0;
int     created_link = 0;
char    last_pty[TTYLEN] = "",
  last_tty[TTYLEN] = "";
pid_t child_pid = 0;
int please_die_now = 0;
int listenfd = 0;

mode_t frontend_mode = -1;
uid_t frontend_owner = -1;
gid_t frontend_group = -1;

int no_closedown = 0;

FILE *outfile;

/* find & open a pty to be used by the pty-master */

int find_ptyxx (char *ptyname)
{
  int fd, ttyfd;
  
  if (my_openpty(&fd,&ttyfd,ptyname) < 0)
    errorf("Couldn't openpty: %s\n",strerror(errno));

  if (stty_raw(ttyfd) != 0)
    errorf("Couldn't put pty into raw mode: %s\n",strerror(errno));
  /* Throw away the ttyfd.  We'll keep it open because it prevents
   * errors when the client disconnects, but we don't ever plan to
   * read or write any data, so we needn't remember it.
   */

  return fd;
}

/* Create the pty */
int create_pty (int *ptyfd, char *ttynam)
{
  char name[TTYLEN+1];

    *ptyfd = find_ptyxx(name);
    strcpy(ttynam, name);

  if (*ptyfd < 0)
    errorf("can't open pty '%s'\n",name);

  return 1;
}		

/* do a graceful closedown */

void closedown (void)
{	
  if (no_closedown)
    return;
  stty_orig ();
  if (created_link)
    unlink(frontend);
  if (child_pid) 
  {
    kill(child_pid,SIGTERM);
  }
}

/* signal handlers */

void sighup (int sig)
{
  sig = sig;
  closedown ();
}

void sigpipe (int sig)
{
  sig = sig;
	
  signal (SIGPIPE, sigpipe);
}

void sigint(int sig)
{
  sig = sig;
  closedown();
  _exit(1);
}

void sigdeath(int sig)
{
  please_die_now=1;
}

void sigchld(int sig)
{
  child_pid = 0;
  sigdeath(sig);
}

int alldigits(const char *s)
{
  while(isdigit(*s))
    s++;
  return *s == '\0';
}

uid_t find_uid(const char *u)
{
  struct passwd *pw;

  if (alldigits(u))
    return atoi(u);
  
  if (!(pw = getpwnam(u)))
    errorf("Error finding user '%s': %s\n",u,strerror(errno));
  return pw->pw_uid;
}

gid_t find_gid(const char *g)
{
  struct group *gr;

  if (alldigits(g))
    return atoi(g);
  if (!(gr = getgrnam(g)))
    errorf("Error finding group '%s': %s\n",g,strerror(errno));
  return gr->gr_gid;
}

/* main program */

const char *char_repr(unsigned char ch)
{
  enum { RESP_SZ = 6, LOW_CRTL_SZ = 33 };
  static char response[RESP_SZ] = {0};
  /* https://en.wikipedia.org/wiki/Control_character */
  static const char lowCtrlChrs[LOW_CRTL_SZ][RESP_SZ] = {
    "[NUL]", "[SOH]", "[STX]", "[ETX]", "[EOT]", "[ENQ]", "[ACK]", "[BEL]",
    "[BS]",  "[TAB]", "[LF]",  "[VT]",  "[FF]",  "[CR]",  "[SO]",  "[SI]",
    "[DLE]", "[DC1]", "[DC2]", "[DC3]", "[DC4]", "[NAK]", "[SYN]", "[ETB]",
    "[CAN]", "[EM]",  "[SUB]", "[ESC]", "[FS]",  "[GS]",  "[RS]",  "[US]",
    "' '" /* for space */
  };

  if (ch < 32)
    return lowCtrlChrs[ch];
  else if (ch == 127)
    return "[DEL]";

  snprintf(response, RESP_SZ, "%c", ch);
  return response;
}


void dumpbuff(int dir, char *buf, int buflen)
{
  int i;
  int ic;
  int lineChCnt = 0;
  enum { TSTAMP_SZ = 24 };
  char tstamp[TSTAMP_SZ] = { 0 };
  struct timeval timeVal;

  if (timestamp)
    gettimeofday(&timeVal, NULL);
  
  for (i=0;i<buflen;i++)
  {
    /* timestamp stuff */
    if (timestamp && lineChCnt < 1)
    {
      if (tstamp[0] == 0)
      {
        char dtime[9];
        struct tm *now = localtime(&timeVal.tv_sec);
        strftime(dtime, sizeof(dtime), "%H:%M %S", now);
        snprintf(tstamp, sizeof(tstamp), "[%s.%06ld]", dtime, timeVal.tv_usec);
        //snprintf(tstamp, TSTAMP_SZ, "[%s:%s]   ", sec, usec);
      }
      else if (tstamp[0] == '[')
      {
        memset(tstamp, ' ', strnlen(tstamp, TSTAMP_SZ));
      }
      fprintf(outfile, "%s", tstamp);
    }
    /* origin arrows */
    if (lineChCnt < 1)
    {
      if (dir)
      {
        fprintf(outfile, "> ");
        if (lineChCnt < 1)
          fprintf(outfile, "\t");
      }
      else
      {
        fprintf(outfile, "< ");
      }
    }
    /* print value */
    ic=(unsigned char)buf[i];
    if (print_hex)
    {
      /* compact if using lineendings */
      if (use_eol_ch)
        fprintf(outfile, "%02x", ic);
      else
        fprintf(outfile, "0x%02x",ic);
    }
    if (print_chrs)
    {
      if (print_hex)
      {
        if (!use_eol_ch)
          fprintf(outfile, " (%s)", char_repr(buf[i]));
        else
          fprintf(outfile, "(%s)", char_repr(buf[i]));
      } else {
        if (!use_eol_ch)
          fprintf(outfile, " %s", char_repr(buf[i]));
        else
          fprintf(outfile, "%s", char_repr(buf[i]));
      }
    }
    if (use_eol_ch)
    {
      /* should we end line? */
      lineChCnt = (ic == eol_ch) ? 0 : lineChCnt +1;
      /* separator */
      if (i + 1 < buflen && lineChCnt > 0)
        fprintf(outfile, ":");

    }
    if (lineChCnt < 1 || i == (buflen - 1))
      fprintf(outfile, "\n");
  }
  fflush(outfile);
}

/* Run stty on the given file descriptor with the given arguments */
int fstty(int fd, char *stty_args)
{
  int child_status;
  int pid;
  char *stty_cmd;
        
  stty_cmd = malloc(strlen(stty_args)+1+strlen("stty "));
  if (!stty_cmd)
    errorf("Couldn't malloc for stty_cmd: %s\n",strerror(errno));
  strcpy(stty_cmd,"stty ");
  strcat(stty_cmd,stty_args);
                
  if ((pid=fork()) == 0)
  {
    /* Child */
    if (dup2(fd,STDIN_FILENO) < 0)
      errorf("Couldn't dup2(%d,STDIN_FILENO=%d): %s\n",fd,STDIN_FILENO,strerror(errno));
    if (execlp("sh","sh","-c",stty_cmd,NULL) < 0)
      errorf("Couldn't exec stty command: %s\n",strerror(errno));
    /* Should never reach here. */
    exit(-1);
  }
  else if (pid == -1)
  {
    errorf("Couldn't fork: %s\n",strerror(errno));
  }
                
  free(stty_cmd);
  /* Parent */
  if (wait(&child_status) <= 0)
    errorf("Error waiting for forked stty process: '%s'\n",strerror(errno));
  if (!(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0) )
    errorf("stty %s failed\n",stty_args);

  return 0;
}
        

        
struct sockaddr_in inet_resolve(const char *sockname)
{
  struct sockaddr_in sa;
  char *hostname, *netport;
  struct hostent *he;
  
  if (strchr(sockname,':') == NULL)
    errorf("Internet hostname must be @host:port\n");

  if (!(hostname = strdup(sockname)))
    errorf("Couldn't dup string: %s\n",strerror(errno));

  netport = strchr(hostname,':');
  *netport='\0';
  netport++;
  
  sa.sin_family=AF_INET;
  
  if (!(he = gethostbyname(hostname)))
    errorf("Couldn't resolve name '%s': %s.\n",hostname,
	   (h_errno == HOST_NOT_FOUND) ? "Host not found" :
	   ((h_errno == NO_ADDRESS)||(h_errno == NO_DATA)) ? "No data available" :
	   (h_errno == NO_RECOVERY) ? "A non-recoverable name server error occured" :
	   (h_errno == TRY_AGAIN) ? "A temporary error occured." :
	   "An unknown error occured");

  memcpy(&(sa.sin_addr),he->h_addr,he->h_length);
      
#if 0
  if (!(se = getservbyname(netport)))
    errorf("Couldn't resolve port.\n");

  host_port=htons(se->s_port);
#endif
      
  if (!(sa.sin_port = htons(atoi(netport))))
    errorf("Couldn't figure out port number.\n");

  free(hostname);
  
  return sa;
}

int setup_back_tty(char *backend, int f[2])
{
  int serialfd;
  
  /* Open the serial port */
  serialfd = open(backend, O_RDWR | O_NOCTTY | O_SYNC | O_NOCTTY);
  if (serialfd < 0)
    errorf("error opening backend device '%s': %s\n",backend,strerror(errno));
  if (stty_raw(serialfd) != 0)
    errorf("Error putting serial device '%s' in raw mode: %s\n",backend,strerror(errno));
  
  /* Process settings from the -s switch */
  if (settings) {
    fstty(serialfd,settings);
  }

  return f[0]=f[1]=serialfd;
}

int setup_back_program(char *backend, int f[2])
{
  int sock[2];

  if (socketpair(PF_UNIX,SOCK_STREAM,0,sock) != 0)
    errorf("Couldn't create socket: %s\n",strerror(errno));

  /* Now run the program */
  switch (child_pid=fork()) {
    case -1:
      /* Error */
      errorf("Error in fork(): %s\n",strerror(errno));
    case 0:
      /* Child */
      if (close(sock[0]) != 0) {
	errorf("Error in close(sock[0]): %s\n",strerror(errno));
      }

      if (close(STDIN_FILENO) != 0) {
	errorf("Error in close(STDIN_FILENO): %s\n",strerror(errno));
      }
      if (dup2(sock[1],STDIN_FILENO) != STDIN_FILENO) {
	errorf("Error in dup2(sock[1],STDIN_FILENO): %s\n",strerror(errno));
      }

      if (close(STDOUT_FILENO) != 0) {
	errorf("Error in close(STDOUT_FILENO): %s\n",strerror(errno));
      }
      if (dup2(sock[1],STDOUT_FILENO) != STDOUT_FILENO) {
	errorf("Error in dup2(sock[1],STDOUT_FILENO): %s\n",strerror(errno));
      }
      
      if (close(sock[1]) != 0) {
	errorf("Error in close(sock[1]): %s\n",strerror(errno));
      }

      execl("/bin/sh","sh","-c",backend,NULL);
      /* Only returns if there is an error. */
      errorf("exec error: %s\n",strerror(errno));
  }
  /* Parent */
  return f[0]=f[1]=sock[0];
}

/* This can also do front fds */
int setup_back_fds(char *backend, int f[2])
{
  char *fd1_s,*fd2_s;

  if (!(fd1_s = strdup(backend)))
    errorf("Couldn't dup string: %s\n",strerror(errno));

  if ((fd2_s = strchr(fd1_s,',')) != 0) {
    *fd2_s='\0';
    fd2_s++;
  }
  else {
    fd2_s = fd1_s;
  }
  f[1]=atoi(fd2_s);
  return f[0]=atoi(fd1_s);
}


int setup_backend(int f[2])
{
  switch(backend[0]) {
    case '!':
      return setup_back_program(backend+1,f);
    case '=':
      return setup_back_fds(backend+1,f);
    default:
      return setup_back_tty(backend,f);
  }
}

/*************************************
 * Set up pty
 ************************************/
int setup_front_tty(char *frontend, int f[2])
{
  struct stat st;
  int ptyfd;

  /* Open the parent tty */
  create_pty(&ptyfd, ttynam);
  
  /* Now set permissions, owners, etc. */
  if (geteuid() == 0)
  {
    if ((frontend_mode == -1) && !strchr("@!=",backend[0]))
    {
      if (stat(backend, &st) < 0)
	errorf("Couldn't stat backend device '%s': %s\n",backend,strerror(errno));
      frontend_mode = st.st_mode;
      frontend_owner = st.st_uid;
      frontend_group = st.st_gid;
    }
    if (frontend_mode != -1) {
      /* Set up permissions on the pty slave */
      if (stat(ttynam, &st) < 0)
	errorf("Couldn't stat tty '%s': %s\n",ttynam,strerror(errno));

      if (chown(ttynam, frontend_owner, frontend_group) < 0)
	errorf("Couldn't chown backend device to uid=%d, gid=%d: %s\n",st.st_uid,st.st_gid,strerror(errno));
      if (chmod(ttynam, frontend_mode & 07777) < 0)
	errorf("Couldn't set permissions on tty '%s': %s\n",strerror(errno));
    }
  }
  /* Now make the symlink */
  if (frontend)
  { 
    /* Unlink it in case it's there; if it fails, it
     * probably wasn't.
     */
    unlink(frontend);
    if (symlink(ttynam, frontend) < 0)
    {
      errorf("Couldn't symlink '%s' -> '%s': %s\n",frontend,ttynam,strerror(errno));
    }
    created_link = 1;
  }

  return f[0]=f[1]=ptyfd;
}

int setup_frontend(int f[2])
{
  if (frontend)
    switch (frontend[0])
    {
      case '=':
        return setup_back_fds(frontend+1,f);
    }

  return setup_front_tty(frontend,f);
}



extern char *optarg;
extern int optind;

int main (int argc, char *argv[])
{
  fd_set readset;
  int n, sel;
  int c;
  int backfd[2], frontfd[2];
  struct sigaction sigact;
  sigset_t sigmask;
  char *scratch;

  /* Set default options */
  outfile = stdout;

  if (((argc-optind) < 1) || ((argc-optind) > 2)) {
    exit (2);
  }

  /* Process the two non-flag options */
  backend = argv[optind];
  if ((argc-optind) == 2)
    frontend = argv[optind+1];
        
  if (strcmp(frontend,"-") == 0)
    frontend = NULL;

  atexit (closedown);
  /* Do some initialization */
  stty_initstore();

  /* Setup backend */
  if (setup_backend(backfd) < 0)
    errorf ("select failed. errno = %d\n", errno);

  /* Setup frontend */
  if ((setup_frontend(frontfd)) < 0)
    errorf("setup_frontend failed: %s\n",strerror(errno));
  
  /* calc (initial) max file descriptor to use in select() */
  fdmax = max(backfd[0], frontfd[0]);

  /* Set up signal handlers and such */
  sigemptyset(&sigmask);
  memset(&sigact,0,sizeof sigact);
  sigact.sa_handler = sigdeath;
  sigact.sa_mask = sigmask;

  printf("testing...\n");

  sigaction(SIGHUP,&sigact,NULL);
  sigaction(SIGINT,&sigact,NULL);
  sigaction(SIGQUIT,&sigact,NULL);
  sigaction(SIGPIPE,&sigact,NULL);
  sigaction(SIGTERM,&sigact,NULL);

  sigact.sa_handler = sigchld;
  sigaction(SIGCHLD,&sigact,NULL);

  while (!please_die_now)
  {
    do
    {
      FD_ZERO (&readset);
      FD_SET (backfd[0], &readset);
      FD_SET (frontfd[0], &readset);
                        
      sel = select(fdmax + 1, &readset, NULL, NULL, NULL);
    }
    while (sel == -1 && errno == EINTR && !please_die_now);
    if (sel == -1 && errno != EINTR)
      errorf ("select failed. errno = %d\n", errno);
    else if (please_die_now)
      break;

    if (FD_ISSET(backfd[0], &readset))
    {
      if ((n = read(backfd[0], buff, BUFF_SIZE)) == 0)
      {
	/* Serial port has closed.  This doesn't really make sense for
	 * a real serial port, but for sockets and programs, probably
	 * we should just exit.
	 */
	break;
      }
      else if (n < 0)
      {
	if ( (errno != EAGAIN) && (errno != EINTR) )
	{
	  errorf("Error reading from backend device: %s\n",strerror(errno));
	}
	break;
      }
      else
      {
	/* We should handle this better.  FIX */
        if (write (frontfd[1], buff, n) != n)
	  errorf("Error writing to frontend device: %s\n",strerror(errno));
      }
    }

    if (please_die_now)
      break;

    if (FD_ISSET(frontfd[0], &readset))
    {
      if ((n = read(frontfd[0], buff, BUFF_SIZE)) == 0)
      {
        if (listenfd)
        {
          if (close(frontfd[0]) < 0)
            errorf("Couldn't close old frontfd: %s\n",strerror(errno));
          if ((frontfd[0]=frontfd[1]=accept(listenfd,NULL,NULL)) < 0)
            errorf("Couldn't accept new socket connection: %s\n",strerror(errno));
        }
          
      }
      else if (n <= 0)
      {
	if ( (errno == EAGAIN) || (errno == EINTR) )
	{
	  /* No real error */
	}
	else
	{
	  errorf("Error reading from frontend device: %s\n",strerror(errno));
	}
      }
      else
      {
        if (write (backfd[1], buff, n) != n)
	  errorf("Error writing to backend device: %s\n",strerror(errno));
      }
    }

  }
  stty_orig();
  exit(0);
}
