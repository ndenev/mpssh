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

#define LINEBUF 1024    /* max output line len */

#ifdef LUA
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#endif

/* stdout/err structure for struct procslot */
struct
stdio_pipe {
    int out[2];
    int err[2];
};

/* stdout/stderr output filenames and filehandles */
struct
out_files {
    char *name;
    FILE *fh;
};

/* process slot structure */
struct
procslot {
    int     pid;
    struct  host *hst;
    char    out_buf[LINEBUF];
    char    err_buf[LINEBUF];
    struct  out_files outf[2];
    int     used;
    int     ret;
    struct  stdio_pipe io;
    struct  procslot *prev;
    struct  procslot *next;
#ifdef LUA
    lua_State *L;
#endif
};

/* global process slot var */
extern struct procslot *ps;

/* other global vars */
extern int pslots;
