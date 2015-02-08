/*-
 * Copyright (c) 2005-2015 Nikolay Denev <ndenev@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 * derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <libgen.h>

#define LUA

#ifndef SSHPATH
#define SSHPATH    "/usr/bin/ssh"
#endif

#ifndef SCPPATH
#define SCPPATH "/usr/bin/scp"
#endif

/* Default hosts filename, relative to users homedir */
#define HSTLIST  ".mpssh/hosts"
#define MAXCMD   1024                /* max command len */
#define MAXUSER    30                /* max username len */
#define MAXCHLD  1024                /* max child procs */
#define DEFCHLD   100                /* default child procs */
#define OUT         1
#define ERR         2
#define MAXFD    1024                /* max filedesc number */

/* block/unblck SIGCHLD macros. */
#define BLOCK_SIGCHLD                           \
    sigemptyset(&sigmask);                      \
    sigaddset(&sigmask, SIGCHLD);               \
    sigprocmask(SIG_BLOCK, &sigmask, &osigmask)

#define UNBLOCK_SIGCHLD                         \
    sigprocmask(SIG_SETMASK, &osigmask, NULL)

#define perr(...) fprintf(stderr, __VA_ARGS__)

/* some global vars */
extern int maxchld;
extern const char Rev[];
extern int user_len_max;
extern int host_len_max;
extern int children;
extern int verbose;
extern int done;
extern int print_exit;
extern int hostcount;
extern int blind;
extern char *outdir;
extern char *user;
extern char *label;
extern int no_err;
extern int no_out;
