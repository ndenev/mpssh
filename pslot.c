/*-
 * Copyright (c) 2005-2012 Nikolay Denev <ndenev@gmail.com>
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

#include "mpssh.h"
#include "pslot.h"
#include "host.h"

/*
 * process slot initialization routine
 */
struct procslot*
pslot_new(int pid, struct host *hst)
{
	struct procslot *pslot_tmp;

	pslot_tmp = calloc(1, sizeof(struct procslot));
	if (pslot_tmp == NULL) {
		perr("%s\n", strerror(errno));
		exit(1);
	}
	pslot_tmp->pid = pid;
	pslot_tmp->hst = hst;
	pslot_tmp->outf[0].name	= NULL;
	pslot_tmp->outf[0].fh	= NULL;
	pslot_tmp->outf[1].name	= NULL;
	pslot_tmp->outf[1].fh	= NULL;
	pslot_tmp->used = 0;
	pipe(pslot_tmp->io.out);
	pipe(pslot_tmp->io.err);
	fcntl(pslot_tmp->io.out[0], F_SETFL, O_NONBLOCK);
	fcntl(pslot_tmp->io.err[0], F_SETFL, O_NONBLOCK);
	return(pslot_tmp);
}


/*
 * routine for adding new process slot in the
 * circular doubly linked list of proccess slots
 */
struct procslot*
pslot_add(struct procslot *pslot, int pid, struct host *hst)
{
	struct procslot *pslot_tmp;

	if (!pslot) {
		/* first entry special case */
		pslot = pslot_new(pid, hst);
		pslot->prev = pslot;
		pslot->next = pslot;
		pslot_tmp = pslot;
	} else {
		pslot_tmp = pslot_new(pid, hst);
		pslot_tmp->next = pslot->next;
		pslot_tmp->prev = pslot;
		pslot->next->prev = pslot_tmp;
		pslot->next = pslot_tmp;
	}
	pslots++;
	return(pslot_tmp);
}

/*
 * routine for deleting process slot from the ring list
 * it adjusts the links of the neighbor pslots and free()'s
 * the slot, closing it's piped descriptors
 */
struct procslot*
pslot_del(struct procslot *pslot)
{
	struct procslot *pslot_todel;
	int    is_last = 0;

	if (!pslot) return(NULL);

	if (pslot == pslot->next)
		is_last = 1;

	/* these shouldn't do anything if we are the last element */
	pslot_todel = pslot;
	pslot->prev->next = pslot_todel->next;
	pslot->next->prev = pslot_todel->prev;
	pslot = pslot_todel->next;
	close(pslot_todel->io.out[0]);
	close(pslot_todel->io.err[0]);

	/*
	 * close the stdout and stderr filehandles,
	 * unlink the output file if we have not written anything to it,
	 * and finally free the memory containing the filename
	 */
	if (pslot_todel->outf[0].fh) {
		if (ftell(pslot_todel->outf[0].fh) == 0)
			unlink(pslot_todel->outf[0].name);
		fclose(pslot_todel->outf[0].fh);
		free(pslot_todel->outf[0].name);
	}
	if (pslot_todel->outf[1].fh) {
		if (ftell(pslot_todel->outf[1].fh) == 0)
			unlink(pslot_todel->outf[1].name);
		fclose(pslot_todel->outf[1].fh);
		free(pslot_todel->outf[1].name);
	}

	free(pslot_todel);

	if (is_last) {
		pslots++;
		return(NULL);
	}

	pslots--;
	return(pslot);
}

/*
 * routine used by the child reaper routine installed as
 * signal handler for SIGCHLD.
 * it iterates thru the process slots and finds the slot
 * with the pid that we have supplied as argument.
 */
struct procslot*
pslot_bypid(struct procslot *pslot, int pid)
{
	int i;

	for (i=0; i <= children; i++) {
		if (pslot->pid == pid)
			return(pslot);
		pslot = pslot->next;
	}
	return(NULL);
}

int
pslot_readbuf(struct procslot *pslot, int outfd)
{
	int i,fd;
	char buf;
	char *bufp;

	switch (outfd) {
		case OUT:
			fd = pslot->io.out[0];
			bufp = pslot->out_buf;
			break;
		case ERR:
			fd = pslot->io.err[0];
			bufp = pslot->err_buf;
			break;
		default:
			return 0;
	}

	for (;;) {
		i = read(fd, &buf, sizeof(buf));
		if (i == 0) return 0;
		if (i < 0) {
			if (errno == EINTR) continue;
			return 0;
		}
		if (buf == '\n') return 1;
		strncat(bufp, &buf, 1);
		if (strlen(bufp) >= (LINEBUF-1)) return 1;
	}

}

void
pslot_printbuf(struct procslot *pslot, int outfd)
{
	int	i;
	char	*bufp;
	FILE	*stream;
	char	progress[9];
	char	**stream_pfx;
	char	*pfx_out[] = { "OUT:", "->", "\033[1;32m->\033[0;39m", NULL };
	char	*pfx_err[] = { "ERR:", "=>", "\033[1;31m=>\033[0;39m", NULL };
	char	*pfx_ret[] = { "=:", "\033[1;32m=:\033[0;39m", "\033[1;31m=:\033[0;39m", NULL };

	switch (outfd) {
	case OUT:
		bufp = pslot->out_buf;
		stream_pfx = pfx_out;
		stream = stdout;
		break;
	case ERR:
		bufp = pslot->err_buf;
		stream_pfx = pfx_err;
		stream = stderr;
		break;
	default:
		return;
	}

	if (verbose) {
		//snprintf(progress, sizeof(progress), "[%d]", 100 / ( hostcount - done ) );
		progress[0] = '\0';
	} else {
		progress[0] = '\0';
	}

	if (strlen(bufp)) {
		if (outdir) {
			/* print to file */
			fprintf(pslot->outf[outfd - 1].fh, "%s\n", bufp);
			fflush(pslot->outf[outfd - 1].fh);
			pslot->used++;
		}
		if (!blind) {
			/* print to console */
			fprintf(stream, "[%*s @ %*s] %s%s %s\n",
				user_len_max, pslot->hst->user,
				host_len_max, pslot->hst->host,
				progress,
				stream_pfx[isatty(fileno(stream))+1],
				bufp);
			fflush(stream);
			pslot->used++;
		}
		memset(bufp, 0, LINEBUF);
	/*
	 * the child is dead and we are going to print exit code if reqested
	 * so, make sure that we print it only when we are called for OUT fd,
	 * because we want it printed only once
	 */
	} else if (!pslot->pid && (outfd == OUT)) {
		if (print_exit) {
			/*
			 * print exit code prefix "=:", bw if we are not on a tty, 
			 * green if return code is zero and red if differs from zero
			 * pfx_ret[isatty(fileno(stdout))?(pslot->ret?2:1):0]
			 */
			printf("[%*s @ %*s] %s%s %d\n",
				user_len_max, pslot->hst->user,
				host_len_max, pslot->hst->host,
				progress,
				pfx_ret[isatty(fileno(stdout))?(pslot->ret?2:1):0],
				pslot->ret);
			fflush(stdout);
		} else if (!pslot->used && !outdir && verbose) {
			printf("[%*s @ %*s] %s\n",
				user_len_max, pslot->hst->user,
				host_len_max, pslot->hst->host,
				progress);
		}
	}

	if (outdir && blind && verbose && !pslot->pid && (outfd == OUT) && (!strlen(bufp))) {
		if (done > 1) for (i=1; i<host_len_max + 10; i++) printf("\b");
		printf("%-*s %s", host_len_max, pslot->hst->host, progress);
		fflush(stdout);
	}

}
