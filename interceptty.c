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

#define BUFF_SIZE	256

char buff[BUFF_SIZE];

char ttynam[TTYLEN+1] = "";
int ptyfd = -1;

int fdmax = 0;

char    *backend = NULL,
  *frontend = DEFAULT_FRONTEND,
  timestamp = 0,
  use_eol_ch = 0,
  print_hex = 1,
  print_chrs = 1;
int     created_link = 0;
pid_t child_pid = 0;

FILE *outfile;

/* find & open a pty to be used by the pty-master */

int find_ptyxx (char *ptyname)
{
  int fd, ttyfd;
  
  if (my_openpty(&fd,&ttyfd,ptyname) < 0)
    errorf("Couldn't openpty: %s\n",strerror(errno));

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

/* main program */

/* Run stty on the given file descriptor with the given arguments */
int setup_back_tty(char *backend, int f[2])
{
  int serialfd;
  
  /* Open the serial port */
  serialfd = open(backend, O_RDWR | O_NOCTTY | O_SYNC | O_NOCTTY);
  if (serialfd < 0)
    errorf("error opening backend device '%s': %s\n",backend,strerror(errno));
  
  return f[0]=f[1]=serialfd;
}

/* This can also do front fds */
int setup_backend(int f[2])
{
  switch(backend[0]) {
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
  char *scratch;

  /* Set default options */
  outfile = stdout;

  /* Process the two non-flag options */
  backend = argv[optind];
  if ((argc-optind) == 2)
    frontend = argv[optind+1];
        
  if (strcmp(frontend,"-") == 0)
    frontend = NULL;

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

  printf("testing...\n");


  while (TRUE)
  {
    do
    {
      FD_ZERO (&readset);
      FD_SET (backfd[0], &readset);
      FD_SET (frontfd[0], &readset);
                        
      sel = select(fdmax + 1, &readset, NULL, NULL, NULL);
    }
    while (sel == -1 && errno == EINTR);

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
      else
      {
	/* We should handle this better.  FIX */
        if (write (frontfd[1], buff, n) != n)
	  errorf("Error writing to frontend device: %s\n",strerror(errno));
      }
    }

    if (FD_ISSET(frontfd[0], &readset))
    {
      if ((n = read(frontfd[0], buff, BUFF_SIZE)) == 0)
      {
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
