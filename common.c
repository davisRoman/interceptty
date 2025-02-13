/*
	common.c
*/

/* $Id: common.c,v 7.2 2003/10/01 20:21:09 sgifford Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <stdarg.h>
#include <unistd.h>

#include "common.h"

#define TTY_STORE	16

static struct termios orig_tty_state[TTY_STORE];
static int sttyfds[TTY_STORE];

void errorf (char *fmt, ...)
{
  va_list args;
	
  va_start (args, fmt);
  vfprintf (stderr, fmt, args);
  exit (1);
}

int fdprintf (int fd, char *fmt, ...)
{
  va_list args;
  int r;
  char str[256];

  va_start (args, fmt);
  r = vsprintf(str, fmt, args);
  write (fd, str, r);

  return (r);
}

char *leafname (char *path)
{
  int i = 0, j;
	
  for (j = 0; path[j]; j++)
    if (path[j] == '/')
      i = j + 1;
	
  return (path + i);
}

/* init the stty store array */

void stty_initstore (void)
{
  int i;
	
  for (i = 0; i < TTY_STORE; i++)
    sttyfds[i] = -1;
}

/* restore all altered ttys to their original state */

void stty_orig (void)
{
  int i;
	
  for (i = 0; i < TTY_STORE; i++)
    if (sttyfds[i] != -1)
    {
      tcsetattr (sttyfds[i], TCSAFLUSH, &orig_tty_state[i]);
      sttyfds[i] = -1;
    }
}
