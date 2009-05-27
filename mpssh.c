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

/*
 * mpssh - Mass Parallel Secure Shell. (c) 2005 Niki Denev
 *
 * This program reads list of machines from text file (one hostname at line)
 * and connects to all of the machines by creating upto N parallel ssh
 * sessions. Then it reads the output from each ssh session and prints it on
 * the screen prepending the machine name and the descriptor name (stdout/err).
 * The number of parallel sessions N, the filename of the list of machine
 * and the username to login as are user configurable.
 */

#include "mpssh.h"
#include "host.h"
#include "pslot.h"

const char Ident[] = "$Id$";
const char Ver[] = "HEAD";

/* global vars */
procslt	*pslot_ptr   = NULL;
char	*cmd         = NULL;
char	*user        = NULL;
char	*fname       = NULL;
char   *outdir       = NULL;
int	children     = 0;
int	maxchld      = 0;
int	blind        = 0;
int	done         = 0;
int	hostcount    = 0;
int	pslots       = 0;
int	user_len_max = 0;
int	host_len_max = 0;
int	print_exit   = 0;
int	hkey_check   = 1;
int	verbose      = 0;
sigset_t	sigmask;
sigset_t	osigmask;

/* function declarations */
host	*host_readlist(char *);
procslt	*pslot_add(procslt *, int, host *);
procslt	*pslot_del(procslt *);
procslt	*pslot_bypid(procslt *, int);
void	pslot_printbuf(procslt *, int);
int	pslot_readbuf(procslt *, int);

/*
 * child reaping routine. it is installed as signal
 * hanler for the SIG_CHLD signal.
 */
void
reap_child()
{
	int	 pid;
	int	 ret;

	while ((pid = waitpid(-1, &ret, WNOHANG)) > 0) {
		done++;
		pslot_ptr = pslot_bypid(pslot_ptr, pid);
		pslot_ptr->pid = 0;
		pslot_ptr->ret = WEXITSTATUS(ret);
		while (pslot_readbuf(pslot_ptr, OUT))
			pslot_printbuf(pslot_ptr, OUT);
		while (pslot_readbuf(pslot_ptr, ERR))
			pslot_printbuf(pslot_ptr, ERR);
		/* 
		 * make sure that we print some output in verbose mode
		 * even if there is no data in the buffer
		 */
		pslot_printbuf(pslot_ptr, OUT);
		pslot_ptr = pslot_del(pslot_ptr);
		children--; /* decrement this last, its used in pslot_bypid */
	}
	return;
}

void
child()
{
	int	len_u, len_p;
	char	*user_arg, *port_arg;

	pslot_ptr->pid = 0;

	/* close stdin of the child, so it won't accept input */
	close(0);

	/* close the parent end of the pipes */
	close(pslot_ptr->io.out[0]);
	close(pslot_ptr->io.err[0]);

	if (dup2(pslot_ptr->io.out[1], 1) == -1)
		fprintf(stderr, "stdout dup fail %s\n",
			 strerror(errno));

	if (dup2(pslot_ptr->io.err[1], 2) == -1)
		fprintf(stderr, "stderr dup fail %s\n",
			 strerror(errno));

	len_u = strlen(pslot_ptr->hst->user) + 3; // space for -l and \0
	user_arg = calloc(1, len_u);
	if (user_arg == NULL) {
		exit(1);
	}

	len_p = strlen(pslot_ptr->hst->port) + 3; // space for -p and \0
	port_arg = calloc(1, len_p);
	if (port_arg == NULL) {
		exit(1);
	}

	snprintf(user_arg, len_u, "-l%s", pslot_ptr->hst->user);
	snprintf(port_arg, len_p, "-p%s", pslot_ptr->hst->port);

	execl(SSHPATH, "ssh", SSHOPTS, hkey_check?HKCHK_Y:HKCHK_N,
			user_arg, port_arg, pslot_ptr->hst->name, cmd, NULL);
	fprintf(stderr, "exec failed : %s %s %s %s %s %s %s\n", SSHPATH, SSHOPTS,
			hkey_check?HKCHK_Y:HKCHK_N, user_arg, port_arg, pslot_ptr->hst->name, cmd);
	exit(1);
}

/*
 * print program version and exit
 */
void
show_ver()
{
	printf("mpssh-%s\n", Ver);
	printf("%s\n", Ident);
	exit(0);
}

/*
 * routine displaing the usage, and various error messages
 * supplied from the main() routine.
 */
void
usage(char *msg)
{
	printf( "\n  Usage: mpssh [-u username] [-p numprocs] [-f hostlist]\n"
		"               [-e] [-b] [-o /some/dir] [-s] [-v] <command>\n\n"
		"  -h, --help         this screen\n"
		"  -u, --user=USER    login as this username\n"
		"  -p, --procs=NPROC  number of parallel ssh processes\n"
		"  -f, --file=FILE    host list file name\n"
		"  -e, --retcode      print the return code on exit\n"
		"  -b, --blind        enable blind mode (do not show remote output)\n"
		"  -o, --outdir=DIR   directory to save output files\n"
		"  -s, --nokeychk     disable ssh strict host key check\n"
		"  -v, --verbose      be more verbose and show progress\n"
		"  -V, --version      show program version\n"
		"\n");
	if (msg)
		printf("   *** %s\n\n", msg);

	exit(0);
}

void
parse_opts(int *argc, char ***argv)
{
	int opt;

	static struct option longopts[] = {
		{ "blind",	no_argument,		NULL,		'b' },
		{ "retcode",	no_argument,		NULL,		'e' },
		{ "file",	required_argument,	NULL,		'f' },
		{ "help",	no_argument,		NULL,		'h' },
		{ "outdir",	required_argument,	NULL,		'o' },
		{ "procs",	required_argument,	NULL,		'p' },
		{ "user",	required_argument,	NULL,		'u' },
		{ "nokeychk",	no_argument,		NULL,		's' },
		{ "verbose",	no_argument,		NULL,		'v' },
		{ "version",	no_argument,		NULL,		'V' },	
		{ NULL,		0,			NULL,		0},
	};

	while ((opt = getopt_long(*argc, *argv, "bef:ho:p:u:svV", longopts, NULL)) != -1) {
		switch (opt) {
			case 'b':
				blind = 1;
				if (print_exit)
					usage("-b is not compatible with -e");
				break;
			case 'e':
				print_exit = 1;
				if (blind)
					usage("-b is not compatible with -e");
				break;
			case 'f':
				if (fname)
					usage("one filename allowed");
				fname = optarg;
				break;
			case 'h':
				usage(NULL);
				break;
			case 'o':
				if (outdir)
					usage("one output dir allowed");
				outdir = optarg;
				break;
			case 'p':
				maxchld = (int)strtol(optarg,(char **)NULL,10);
				if (maxchld < 0) usage("bad numproc");
				if (maxchld > MAXCHLD) maxchld = MAXCHLD;
				break;
			case 's':
				hkey_check = 0;
				break;
			case 'u':
				if (user)
					usage("one username allowed");
				user = optarg;
				if (strlen(user) > MAXUSER)
					usage("username too long");
				break;
			case 'v':
				verbose = 1;
				break;
			case 'V':
				show_ver();
				break;
			case '?':
				usage("unrecognized option");
				break;
			default:
			        usage(NULL);
		}
	}
	*argc -= optind;
	*argv += optind;

	if (!maxchld)
		maxchld = DEFCHLD;

	if (*argc > 1)
		usage("too many arguments");
	if (*argc < 1)
		usage("command missing");

	cmd = *argv[0];
	if (strlen(cmd) > MAXCMD)
		usage("command too long");

	return;
}

/*
 * Main routine
 */
int
main(int argc, char *argv[])
{
	host	*hst;
	int	i, pid;
	fd_set	readfds;
	int	children_fds;
	struct	timeval	*timeout;
	struct	timeval notimeout;
	struct	passwd	*pw;

	parse_opts(&argc, &argv);

	if (!user) {
		pw = getpwuid(getuid());
		user = pw->pw_name;
	}

	if (blind && !outdir)
		usage("can't use blind mode without outdir");

	if (!(hst = host_readlist(fname?fname:HSTLIST)))
		usage("problem with file");

	printf( "MPSSH - Mass Parallel Ssh Ver.%s\n"
		"(c)2005-2009 N.Denev <ndenev@gmail.com>\n"
		"%s\n\n"
		"  [*] read (%d) hosts from the list\n"
		"  [*] executing \"%s\" as user \"%s\" on each\n",
		Ver, Ident, hostcount, cmd, user);

	if (!hkey_check)
		printf("  [*] strict host key check disabled\n");
	if (blind)
		printf("  [*] blind mode enabled\n");
	if (verbose)
		printf("  [*] verbose mode enabled\n");
	if (outdir) {
		if (!access(outdir, R_OK | W_OK | X_OK)) {
			printf("  [*] using output directory : %s\n", outdir);
		} else {
			printf("\n *** can't access output dir : %s, aborting\n", outdir);
			exit(1);
		}
	}
	printf("  [*] spawning %d parallel ssh sessions\n\n",
			maxchld);
	fflush(NULL);

	/* install the signal handler for SIGCHLD */
	signal(SIGCHLD, reap_child);

	while (hst || children) {
		BLOCK_SIGCHLD;
		if (hst && (children < maxchld)) {
			pslot_ptr = pslot_add(pslot_ptr, 0, hst);
			/* output to file mode */
			if (outdir) {
				umask(022);
				/*
				 * alloc enough space for the string consisting of a directoryname, slash, filename,
				 * a dot and a three letter file extension (out/err) and of course the terminating null
				 */
				i = strlen(outdir) + strlen(pslot_ptr->hst->name);
				i += 6;
				pslot_ptr->outfn = calloc(1, i);
				if (!pslot_ptr->outfn) {
					fprintf(stderr, "unable to malloc memory for filename\n");
					exit(1);
				}
				sprintf(pslot_ptr->outfn, "%s/%s.out", outdir, pslot_ptr->hst->name);
				pslot_ptr->outf = fopen(pslot_ptr->outfn, "w");
				if (!pslot_ptr->outf) {
					fprintf(stderr, "unable to open : %s\n", pslot_ptr->outfn);
					exit(1);
				}
			} /* /blind or output to file mode */
			switch (pid = fork()) {
			case 0:
				/* child, does not return */
				child();
				break;
			case -1:
				/* error */
				fprintf(stderr, "unable to fork: %s\n",
					strerror(errno));
				break;
			default:
				/* parent */
				pslot_ptr->pid = pid;
				/* close the child's end of the pipes */
				close(pslot_ptr->io.out[1]);
				close(pslot_ptr->io.err[1]);
				children++;
				break;
			}
			hst = hst->next;
		}
		FD_ZERO(&readfds);
		children_fds = children;
		for (i=0; i <= children_fds; i++) {
			FD_SET(pslot_ptr->io.out[0], &readfds);
			FD_SET(pslot_ptr->io.err[0], &readfds);
			pslot_ptr = pslot_ptr->next;
        	}
		if (children == maxchld || !hst) {
			timeout = NULL;
		} else {
			memset(&notimeout, 0, sizeof(struct timeval));
			timeout = &notimeout;
		}
		UNBLOCK_SIGCHLD;
		if (select(MAXFD , &readfds, NULL, NULL, timeout) > 0 ) {
			BLOCK_SIGCHLD;
			if (pslot_ptr) {
				for (i=0; i <= children_fds; i++) {
					if (FD_ISSET(pslot_ptr->io.out[0], &readfds)) {
						while (pslot_readbuf(pslot_ptr, OUT))
							pslot_printbuf(pslot_ptr, OUT);
					}
					if (FD_ISSET(pslot_ptr->io.err[0], &readfds)) {
						while (pslot_readbuf(pslot_ptr, ERR))
							pslot_printbuf(pslot_ptr, ERR);
					}
					pslot_ptr = pslot_ptr->next;
				}
			}
			UNBLOCK_SIGCHLD;
		}
	}
	printf("\n  Done. %d hosts processed.\n", done);

	return(0);
}
