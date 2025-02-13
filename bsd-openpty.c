/* This file was taken from portable openssh 3.8p1, as permitted by
 * its license.
 *
 * It has been somewhat modified by Scott Gifford <sgifford@suspectclass.com>
 * Any bugs here are my responsibility, not Damien Miller's or the
 * OpenSSH team.
 */

/*
 * Please note: this implementation of openpty() is far from complete.
 * it is just enough for our needs.
 */

/*
 * Copyright (c) 2004 Damien Miller <djm@mindrot.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Author: Tatu Ylonen <ylo@cs.hut.fi>
 * Copyright (c) 1995 Tatu Ylonen <ylo@cs.hut.fi>, Espoo, Finland
 *                    All rights reserved
 * Allocating a pseudo-terminal, and making it the controlling tty.
 *
 * As far as I am concerned, the code I have written for this software
 * can be used freely for any purpose.  Any derived versions of this
 * software must be clearly marked as such, and if the derived work is
 * incompatible with the protocol description in the RFC file, it must be
 * called by a name other than "ssh" or "Secure Shell".
 */

#include "config.h"
#include "bsd-openpty.h"

#include <sys/types.h>
/* This XOPEN hack seems to be required for Linux. --sg */
#define  __USE_XOPEN 1
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>

#ifdef HAVE_UTIL_H
# include <util.h>
#endif /* HAVE_UTIL_H */

#ifdef HAVE_PTY_H
# include <pty.h>
#endif
#if defined(HAVE_DEV_PTMX) && defined(HAVE_SYS_STROPTS_H)
# include <sys/stropts.h>
#endif

#ifndef O_NOCTTY
#define O_NOCTTY 0
#endif

typedef void (*mysig_t)(int);

int
my_openpty(int *amaster, int *aslave, char *name)
{
        /* Re-implement with clearer rules about the size of name:
	 * must be TTYLEN+1 bytes.
	 */
        char *tmpname;

	if (openpty(amaster, aslave, NULL, NULL, NULL) < 0)
		return -1;
	if (!name)
	  	return 0;
	if ((tmpname = ttyname(*aslave)) == NULL)
	  	return -1;
	if (strlen(tmpname) > TTYLEN)
	  	return -1;
	strcpy(name,tmpname);
	return (0);
}


