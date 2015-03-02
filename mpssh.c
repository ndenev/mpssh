/*-
 * Copyright (c) 2005-2013 Nikolay Denev <ndenev@gmail.com>
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

const char Ver[] = "1.4-dev";

/* global vars */
struct procslot *ps = NULL;

char *cmd         = NULL;
char *user        = NULL;
char *fname       = NULL;
char *outdir      = NULL;
char *label       = NULL;
char *script      = NULL;
char *base_script = NULL;

int children       = 0;
int maxchld        = 0;
int blind          = 0;
int done           = 0;
int delay          = 10;
int hostcount      = 0;
int pslots         = 0;
int user_len_max   = 0;
int host_len_max   = 0;
int print_exit     = 0;
int local_command  = 0;
int ssh_hkey_check = 1;
int ssh_quiet      = 0;
int ssh_conn_tmout = 30;
int verbose        = 0;
int no_err         = 0;
int no_out         = 0;

sigset_t sigmask;
sigset_t osigmask;

/* function declarations */
struct host     *host_readlist(char *);
void             host_free(struct host *);
struct procslot *pslot_add(struct procslot *, int, struct host *);
struct procslot *pslot_del(struct procslot *);
struct procslot *pslot_bypid(struct procslot *, int);
void             pslot_printbuf(struct procslot *, int);
int              pslot_readbuf(struct procslot *, int);

/*
 * child reaping routine. it is installed as signal
 * hanler for the SIG_CHLD signal.
 */
void
reap_child()
{
    int pid;
    int ret;

    while ((pid = waitpid(-1, &ret, WNOHANG)) > 0) {
        done++;
        ps = pslot_bypid(ps, pid);
        ps->pid = 0;

        if (WIFEXITED(ret))
            ps->ret = WEXITSTATUS(ret);
        else
            ps->ret = 255;

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
    char *ssh_argv[17];
    int   sap;

    char *lcmd;
    int   len_u;
    char *user_arg;
    /* enough for -p65535\0 */
    char  port_arg[8];
    char  tmo_arg[32];

    ps->pid = 0;
    sap = 0;

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
#ifdef TESTING
    ssh_argv[sap++] = "/bin/echo";
#endif

    ssh_argv[sap++] = SSHPATH;

    ssh_argv[sap++] = "-oNumberOfPasswordPrompts=0";

    if (ssh_quiet)
        ssh_argv[sap++] = "-q";

    /* space for -l and \0 */
    len_u = strlen(ps->hst->user) + 3;
    user_arg = calloc(1, len_u);
    if (user_arg == NULL) {
        exit(1);
    }
    snprintf(user_arg, len_u, "-l%s", ps->hst->user);
    ssh_argv[sap++] = user_arg;

    if (ps->hst->port != NON_DEFINED_PORT) {
        snprintf(port_arg, sizeof(port_arg), "-p%d", ps->hst->port);
        ssh_argv[sap++] = port_arg;
    }

    if (ssh_hkey_check)
        ssh_argv[sap++] = "-oStrictHostKeyChecking=yes";
    else
        ssh_argv[sap++] = "-oStrictHostKeyChecking=no";

    snprintf(tmo_arg,sizeof(tmo_arg), "-oConnectTimeout=%d",
            ssh_conn_tmout);
    ssh_argv[sap++] = tmo_arg;


    if (local_command) {
        snprintf(port_arg, sizeof(port_arg), "-P%d", (ps->hst->port
            != NON_DEFINED_PORT ? ps->hst->port : DEFAULT_PORT));
        ssh_argv[sap++] = "-oPermitLocalCommand=yes";
        lcmd = calloc(1, 2048);
        snprintf(lcmd, 2048, "-oLocalCommand=%s %s %s -p %s %s@%s:%s",
            SCPPATH,
            (ssh_quiet) ? "-q" : "",
            port_arg,
            script,
            ps->hst->user,
            ps->hst->host,
            base_script);
        ssh_argv[sap++] = lcmd;
    }

    ssh_argv[sap++] = ps->hst->host;

    if (local_command) {
        char *remexec;
        remexec = calloc(1, strlen(base_script)+3);
        snprintf(remexec, strlen(base_script)+3, "./%s", base_script);
        ssh_argv[sap++] = remexec;
    } else {
        ssh_argv[sap++] = cmd;
    }

    ssh_argv[sap++] = NULL;

#ifdef TESTING
    execv("/bin/echo", ssh_argv);
#else
    execv(SSHPATH, ssh_argv);
#endif

    perr("failed to exec the ssh binary");
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
        "  -b, --blind         enable blind mode (no remote output)\n"
        "  -d, --delay         delay between each ssh fork (default %d msec)\n"
        "  -e, --exit          print the remote command return code\n"
        "  -E, --no-err        suppress stderr output\n"
        "  -f, --file=FILE     file with the list of hosts or - for stdin\n"
        "  -h, --help          this screen\n"
        "  -l, --label=LABEL   connect only to hosts under label LABEL\n"
        "  -o, --outdir=DIR    save the remote output in this directory\n"
        "  -O, --no-out        suppress stdout output\n"
        "  -p, --procs=NPROC   number of parallel ssh processes (default %d)\n"
        "  -q, --quiet         run ssh with -q\n"
        "  -r, --script        copy local script to remote host and execute it\n"
        "  -s, --nokeychk      disable ssh strict host key check\n"
        "  -t, --conntmout     ssh connect timeout (default %d sec)\n"
        "  -u, --user=USER     ssh login as this username\n"
        "  -v, --verbose       be more verbose (i.e. show usernames used)\n"
        "  -V, --version       show program version\n"
        "\n", delay, DEFCHLD, ssh_conn_tmout);
    } else {
        printf("\n   *** %s\n\n", msg);
    }

    exit(0);
}

void
parse_opts(int *argc, char ***argv)
{
    int opt;
    struct stat scstat;

    static struct option longopts[] = {
        { "blind",     no_argument,        NULL,        'b' },
        { "exit",      no_argument,        NULL,        'e' },
        { "file",      required_argument,  NULL,        'f' },
        { "help",      no_argument,        NULL,        'h' },
        { "label",     required_argument,  NULL,        'l' },
        { "outdir",    required_argument,  NULL,        'o' },
        { "procs",     required_argument,  NULL,        'p' },
        { "quiet",     no_argument,        NULL,        'q' },
        { "script",    required_argument,  NULL,        'r' },
        { "nokeychk",  no_argument,        NULL,        's' },
        { "no-err",    no_argument,        NULL,        'E' },
        { "no-out",    no_argument,        NULL,        'O' },
        { "conntmout", required_argument,  NULL,        't' },
        { "user",      required_argument,  NULL,        'u' },
        { "verbose",   no_argument,        NULL,        'v' },
        { "version",   no_argument,        NULL,        'V' },
        { NULL,        0,                  NULL,        0},
    };

    while ((opt = getopt_long(*argc, *argv,
                "bd:eEf:hl:o:Op:qr:u:t:svV", longopts, NULL)) != -1) {
        switch (opt) {
            case 'b':
                blind = 1;
                break;
            case 'd':
                delay = (int)strtol(optarg,(char **)NULL,10);
                if (delay == 0 && errno == EINVAL)
                    usage("invalid delay value");
                if (delay < 0) usage("delay can't be negative");
                break;
            case 'e':
                print_exit = 1;
                break;
            case 'E':
                no_err = 1;
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
            case 'O':
                no_out = 1;
                break;
            case 'p':
                maxchld = (int)strtol(optarg,(char **)NULL,10);
                if (maxchld < 0) usage("bad numproc");
                if (maxchld > MAXCHLD) maxchld = MAXCHLD;
                break;
            case 'q':
                ssh_quiet = 1;
                break;
            case 'r':
                local_command = 1;
                script = optarg;
                if (stat(script, &scstat) < 0) {
                    usage("can't stat script file");
                }
                if (!(S_ISREG(scstat.st_mode) && scstat.st_mode & 0111)) {
                    usage("script file is not executable");
                }
                base_script = basename(script);
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
                if (user && strlen(user) > MAXUSER)
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

    if (local_command) {
        if(*argc)
            usage("can't use remote command when executing local script");
        return;
    }

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
 * Routine to handle stdout and stderr
 * output file creation and opening
 * when output to file mode is enabled.
 */
int
setupoutdirfiles(struct procslot *p)
{
    int i;

    /*
     * alloc enough space for the string consisting
     * of a directoryname, slash, username, @ sign,
     * hostname, a dot and a three letter file
     * extension (out/err) and the terminating null
     */
    i  = strlen(outdir);
    i += strlen(p->hst->user);
    i += strlen(p->hst->host);
    i += 7;

    /* setup the stdout output file */
    p->outf[0].name = calloc(1, i);
    if (!p->outf[0].name) {
        perr("unable to malloc memory for filename\n");
        return(1);
    }
    sprintf(p->outf[0].name, "%s/%s@%s.out",
        outdir, p->hst->user, p->hst->host);
    p->outf[0].fh = fopen(p->outf[0].name, "w");
    if (!p->outf[0].fh) {
        perr("unable to open : %s\n", p->outf[0].name);
        return(1);
    }

    /* setup the stderr output file */
    p->outf[1].name = calloc(1, i);
    if (!p->outf[1].name) {
        perr("unable to malloc memory for filename\n");
        return(1);
    }
    sprintf(p->outf[1].name, "%s/%s@%s.err",
        outdir, p->hst->user, p->hst->host);

    p->outf[1].fh = fopen(p->outf[1].name, "w");
    if (!p->outf[1].fh) {
        perr("unable to open : %s\n", p->outf[1].name);
        return(1);
    }
    return(0);
}

/*
 * Main routine
 */
int
main(int argc, char *argv[])
{
    struct host *hst, *tofree;
    int    i;
    int    pid;
    int    tty;
    fd_set readfds;
    int    children_fds;
    struct timeval *timeout;
    struct timeval  notimeout;
    struct passwd  *pw;

    parse_opts(&argc, &argv);

    if (!user) {
        pw = getpwuid(getuid());
        user = pw->pw_name;
    }

    hst = host_readlist(fname);

    if (hst == NULL) {
        perr("host list file empty, "
            "does not exist or no valid entries\n");
        exit(1);
    }

    tofree = hst;

    /* Console Printf if we are running on tty */
    tty = isatty(fileno(stdout));
#define tty_printf(...) if (tty) fprintf(stdout, __VA_ARGS__)

    tty_printf( "MPSSH - Mass Parallel Ssh Ver.%s\n"
        "(c)2005-2013 Nikolay Denev <ndenev@gmail.com>\n\n"
        "  [*] read (%d) hosts from the list\n",
        Ver, hostcount);

    if (local_command) {
        tty_printf( "  [*] uploading and executing the script \"%s\" as user \"%s\"\n",
            script, user);
    } else {
        tty_printf( "  [*] executing \"%s\" as user \"%s\"\n", cmd, user);
    }

    if (label)
        tty_printf("  [*] only on hosts labeled \"%s\"\n", label);

    if (!ssh_hkey_check)
        tty_printf("  [*] strict host key check disabled\n");

    if (blind)
        tty_printf("  [*] blind mode enabled\n");

    if (verbose)
        tty_printf("  [*] verbose mode enabled\n");

    if (outdir) {
        if (!access(outdir, R_OK | W_OK | X_OK)) {
           tty_printf("  [*] using output directory : %s\n", outdir);
        } else {
            tty_printf("  [*] creating output directory : %s\n", outdir);
            if (mkdir(outdir, 0755)) {
                perr("\n *** can't create output dir : ");
                perror(outdir);
                exit(1);
            }
        }
    }
    tty_printf("  [*] spawning %d parallel ssh sessions\n\n",
            maxchld);
    fflush(NULL);

    /* install the signal handler for SIGCHLD */
    signal(SIGCHLD, reap_child);

    if (outdir)
        umask(022);

    while (hst || children) {
        BLOCK_SIGCHLD;
        if (hst && (children < maxchld)) {
            ps = pslot_add(ps, 0, hst);
            if (outdir)
                setupoutdirfiles(ps);
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
    tty_printf("\n  Done. %d hosts processed.\n", done);

    host_free(tofree);

    return(0);
}
