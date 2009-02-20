/*-
 * Copyright (c) 2005-2009 Niki Denev <ndenev@gmail.com>
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
procslt*
pslot_new(int pid, host *hst)
{
	procslt *pslot_tmp;
	pslot_tmp = calloc(1, sizeof(procslt));
	if (pslot_tmp == NULL) {
		fprintf(stderr, "%s\n",
			strerror(errno));
		exit(1);
	}
	pslot_tmp->pid = pid;
	pslot_tmp->hst = hst;
	pslot_tmp->outf = NULL;
	pslot_tmp->outfn = NULL;
	pslot_tmp->used = 0;
	pipe(pslot_tmp->io.out);
	pipe(pslot_tmp->io.err);
	return(pslot_tmp);
}


/*
 * routine for adding new process slot in the
 * circular doubly linked list of proccess slots
 */
procslt*
pslot_add(procslt *pslot, int pid, host *hst)
{
	procslt *pslot_tmp;
	if (!pslot) { /* first entry special case */
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
 * XXX: we do not delete the last process slot!
 */
procslt*
pslot_del(procslt *pslot)
{
	procslt *pslot_todel;
	if (!pslot) return(NULL);
	/* this is the last element */
	if (pslot == pslot->next) {
		close(pslot->io.out[0]);
		close(pslot->io.err[0]);
		free(pslot);
		pslots++;
		return(NULL);
	}
	pslot_todel = pslot;
	pslot->prev->next = pslot_todel->next;
	pslot->next->prev = pslot_todel->prev;
	pslot = pslot_todel->next;
	close(pslot_todel->io.out[0]);
	close(pslot_todel->io.err[0]);
	if (pslot_todel->outf)
		fclose(pslot_todel->outf);
	if (pslot_todel->outfn)
		free(pslot_todel->outfn);	
	free(pslot_todel);
	pslots--;
	return(pslot);
}

/*
 * routine used by the child reaper routine installed as
 * signal handler for SIGCHLD.
 * it iterates thru the process slots and finds the slot
 * with the pid that we have supplied as argument.
 */
procslt*
pslot_bypid(procslt *pslot, int pid)
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
pslot_readbuf(procslt *pslot, int outfd)
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
pslot_printbuf(procslt *pslot, int outfd)
{
	int	i;
	char	*bufp;
	FILE	*stream;
	char	progress[9];
	float	percent;
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
		/* ugly hack to make sure that the last server show progress of 100% */
		if ((done == hostcount-1) && (children == 1)) {
			percent = 100.00;
		} else {
			percent = (float)done/(float)hostcount*100;
		}
		snprintf(progress, sizeof(progress), "[%5.1f%%]", percent);
	} else {
	       	progress[0] = '\0';
	}

	if (strlen(bufp)) {
		if (outdir) {
			/* print to file */
			fprintf(pslot->outf, "%s %s\n", stream_pfx[0], bufp);
			fflush(pslot->outf);
			pslot->used++;
		}
		if (!blind) {
			/* print to console */
			fprintf(stream, "%-*s %s%s %s\n", host_len_max, pslot->hst->name,
				progress, stream_pfx[isatty(fileno(stream))+1], bufp);
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
			printf("%-*s %s%s %d\n", host_len_max, pslot->hst->name,
				progress, pfx_ret[isatty(fileno(stdout))?(pslot->ret?2:1):0], pslot->ret);
		} else if (!pslot->used && !outdir && verbose) {
			printf("%-*s %s\n", host_len_max, pslot->hst->name, progress);
		}
	}

	if (outdir && blind && verbose && !pslot->pid && (outfd == OUT) && (!strlen(bufp))) {
		if (done > 1) for (i=1; i<host_len_max + 10; i++) printf("\b");
		printf("%-*s %s", host_len_max, pslot->hst->name, progress);
		fflush(stdout);
	}

}
