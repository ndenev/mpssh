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
#include "host.h"
#include "pslot.h"

const char Ver[] = "HEAD";

/* global vars */
struct	procslot *ps	= NULL;
char	*cmd		= NULL;
char	*user		= NULL;
char	*fname		= NULL;
char	*outdir		= NULL;
char	*label		= NULL;
int	 children	= 0;
int	 maxchld	= 0;
int	 blind		= 0;
int	 done		= 0;
int	 delay		= 0;
int	 hostcount	= 0;
int	 pslots		= 0;
int	 user_len_max	= 0;
int	 host_len_max	= 0;
int	 print_exit	= 0;
int	 ssh_hkey_check	= 1;
int	 ssh_conn_tmout = 30;
int	 verbose        = 0;
sigset_t	sigmask;
sigset_t	osigmask;

/* function declarations */
struct	host		*host_readlist(char *);
void			host_free(struct host *);
struct	procslot	*pslot_add(struct procslot *, int, struct host *);
struct	procslot	*pslot_del(struct procslot *);
struct	procslot	*pslot_bypid(struct procslot *, int);
void			pslot_printbuf(struct procslot *, int);
int			pslot_readbuf(struct procslot *, int);

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
		ps = pslot_bypid(ps, pid);
		ps->pid = 0;
		ps->ret = WEXITSTATUS(ret);
		while (pslot_readbuf(ps, OUT))
			pslot_printbuf(ps, OUT);
		while (pslot_readbuf(ps, ERR))
			pslot_printbuf(ps, ERR);
		/*
		 * make sure that we print some output in verbose mode
		 * even if there is no data in the buffer
		 */
		pslot_printbuf(ps, OUT);
		ps = pslot_del(ps);
		/* decrement this last, its used in pslot_bypid */
		children--;
	}
	return;
}

void
child()
{
	int	len_u;
	char	*user_arg;
	/* enough for -p65535\0 */
	char	port_arg[8];
	/* enough for -oStrictHostKeyChecking=yes\0 */
	char	hkc_arg[28];
	char	tmo_arg[32];

	ps->pid = 0;

	/* close stdin of the child, so it won't accept input */
	close(0);

	/* close the parent end of the pipes */
	close(ps->io.out[0]);
	close(ps->io.err[0]);

	if (dup2(ps->io.out[1], 1) == -1)
		perr("stdout dup fail %s\n",
			 strerror(errno));

	if (dup2(ps->io.err[1], 2) == -1)
		perr("stderr dup fail %s\n",
			 strerror(errno));

	/* space for -l and \0 */
	len_u = strlen(ps->hst->user) + 3;
	user_arg = calloc(1, len_u);
	if (user_arg == NULL) {
		exit(1);
	}

	snprintf(user_arg, len_u, "-l%s", ps->hst->user);
	snprintf(port_arg, sizeof(port_arg), "-p%d", ps->hst->port);
	snprintf(hkc_arg,sizeof(hkc_arg), "-oStrictHostKeyChecking=%s",
			ssh_hkey_check?"yes":"no");
	snprintf(tmo_arg,sizeof(tmo_arg), "-oConnectTimeout=%d",
			ssh_conn_tmout);

	execl(SSHPATH, "ssh", "-q", hkc_arg, tmo_arg, user_arg, port_arg,
		ps->hst->host, cmd, NULL);
	perr("exec failed : %s -q %s %s %s %s %s %s\n",
		SSHPATH, hkc_arg, tmo_arg, user_arg,
		port_arg, ps->hst->host, cmd);
	exit(1);
}

/*
 * print program version and exit
 */
void
show_ver()
{
	printf("mpssh-%s\n", Ver);
	exit(0);
}

/*
 * routine displaing the usage, and various error messages
 * supplied from the main() routine.
 */
void
usage(char *msg)
{
	if (!msg) {
	    printf("\n Usage: mpssh [-u username] [-p numprocs] [-f hostlist]\n"
		"              [-e] [-b] [-o /some/dir] [-s] [-v] <command>\n\n"
		"  -b, --blind       Enable blind mode (no remote output)\n"
		"  -d, --delay       Delay between weach ssh fork in milisecs\n"
		"  -e, --exit        Print the remote command return code\n"
		"  -f, --file=FILE   Name of the file with the list of hosts\n"
		"  -h, --help        This screen\n"
		"  -l, --label=LABEL Connect only to hosts under label LABEL\n"
		"  -o, --outdir=DIR  Save the remote output in this directory\n"
		"  -p, --procs=NPROC Number of parallel ssh processes\n"
		"  -s, --nokeychk    Disable ssh strict host key check\n"
		"  -t, --conntmout   Ssh connect timeout (default 30sec)\n"
		"  -u, --user=USER   Ssh login as this username\n"
		"  -v, --verbose     Be more verbose and show progress\n"
		"  -V, --version     Show program version\n"
		"\n");
	} else {
		printf("\n   *** %s\n\n", msg);
	}

	exit(0);
}

void
parse_opts(int *argc, char ***argv)
{
	int opt;

	static struct option longopts[] = {
		{ "blind",	no_argument,		NULL,		'b' },
		{ "exit",	no_argument,		NULL,		'e' },
		{ "file",	required_argument,	NULL,		'f' },
		{ "help",	no_argument,		NULL,		'h' },
		{ "label",	required_argument,	NULL,		'l' },
		{ "outdir",	required_argument,	NULL,		'o' },
		{ "procs",	required_argument,	NULL,		'p' },
		{ "nokeychk",	no_argument,		NULL,		's' },
		{ "conntmout",	required_argument,	NULL,		't' },
		{ "user",	required_argument,	NULL,		'u' },
		{ "verbose",	no_argument,		NULL,		'v' },
		{ "version",	no_argument,		NULL,		'V' },
		{ NULL,		0,			NULL,		0},
	};

	while ((opt = getopt_long(*argc, *argv,
				"bd:ef:hl:o:p:u:t:svV", longopts, NULL)) != -1) {
		switch (opt) {
			case 'b':
				blind = 1;
				if (print_exit)
					usage("-b is not compatible with -e");
				break;
			case 'd':
				delay = (int)strtol(optarg,(char **)NULL,10);
				if (delay == 0 && errno == EINVAL)
					usage("invalid delay value");
				if (delay < 0) usage("delay can't be negative");
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
			case 'l':
				label = optarg;
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
				ssh_hkey_check = 0;
				break;
			case 't':
				ssh_conn_tmout = (int)strtol(optarg,(char **)NULL,10);
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
		usage("command missing, use -h for help");

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
	struct	host	*hst, *tofree;
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

	hst = host_readlist(fname);

	if (hst == NULL) {
		perr("host list file empty, "
			"does not exist or no valid entries\n");
		exit(1);
	}

	tofree = hst;

	printf( "MPSSH - Mass Parallel Ssh Ver.%s\n"
		"(c)2005-2012 Nikolay Denev <ndenev@gmail.com>\n\n"
		"  [*] read (%d) hosts from the list\n"
		"  [*] executing \"%s\" as user \"%s\"\n",
		Ver, hostcount, cmd, user);

	if (label)
		printf("  [*] only on hosts labeled \"%s\"\n", label);

	if (!ssh_hkey_check)
		printf("  [*] strict host key check disabled\n");

	if (blind)
		printf("  [*] blind mode enabled\n");

	if (verbose)
		printf("  [*] verbose mode enabled\n");

	if (outdir) {
		if (!access(outdir, R_OK | W_OK | X_OK)) {
			printf("  [*] using output directory : %s\n", outdir);
		} else {
			printf("  [*] creating output directory : %s\n", outdir);
			if (mkdir(outdir, 0755)) {
				perr("\n *** can't create output dir : ");
				perror(outdir);
				exit(1);
			}
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
			ps = pslot_add(ps, 0, hst);
			/* output to file mode */
			if (outdir) {
				umask(022);
				/*
				 * alloc enough space for the string consisting
				 * of a directoryname, slash, username, @ sign,
				 * hostname, a dot and a three letter file
				 * extension (out/err) and the terminating null
				 */
				i  = strlen(outdir);
				i += strlen(ps->hst->user);
				i += strlen(ps->hst->host);
				i += 7;
				/* setup the stdout output file */
				ps->outf[0].name = calloc(1, i);
				if (!ps->outf[0].name) {
					perr("unable to malloc "
						"memory for filename\n");
					exit(1);
				}
				sprintf(ps->outf[0].name,
					"%s/%s@%s.out",
					outdir,
					ps->hst->user,
					ps->hst->host);
				ps->outf[0].fh = fopen(ps->outf[0].name, "w");
				if (!ps->outf[0].fh) {
					perr("unable to open : "
						"%s\n", ps->outf[0].name);
					exit(1);
				}
				/* setup the stderr output file */
				ps->outf[1].name = calloc(1, i);
				if (!ps->outf[1].name) {
					perr("unable to malloc "
						"memory for filename\n");
					exit(1);
				}
				sprintf(ps->outf[1].name,
					"%s/%s@%s.err",
					outdir,
					ps->hst->user,
					ps->hst->host);
				ps->outf[1].fh = fopen(ps->outf[1].name, "w");
				if (!ps->outf[1].fh) {
					perr("unable to open : %s\n",
						ps->outf[1].name);
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
				perr("unable to fork: %s\n",
					strerror(errno));
				break;
			default:
				/* parent */
				ps->pid = pid;
				/* close the child's end of the pipes */
				close(ps->io.out[1]);
				close(ps->io.err[1]);
				children++;
				break;
			}
			/* delay between each sshd fork */
			if (delay)
				usleep(delay * 1000);

			hst = hst->next;
		}
		FD_ZERO(&readfds);
		children_fds = children;
		for (i=0; i <= children_fds; i++) {
			FD_SET(ps->io.out[0], &readfds);
			FD_SET(ps->io.err[0], &readfds);
			if (ps->next)
				ps = ps->next;
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
			if (ps) {
				for (i=0; i <= children_fds; i++) {
					if (FD_ISSET(ps->io.out[0], &readfds)) {
						while (pslot_readbuf(ps, OUT))
							pslot_printbuf(ps, OUT);
					}
					if (FD_ISSET(ps->io.err[0], &readfds)) {
						while (pslot_readbuf(ps, ERR))
							pslot_printbuf(ps, ERR);
					}
					ps = ps->next;
				}
			}
			UNBLOCK_SIGCHLD;
		}
	}
	printf("\n  Done. %d hosts processed.\n", done);

	host_free(tofree);

	return(0);
}
