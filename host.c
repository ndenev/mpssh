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

/*
 * routine for allocating a new host element in the
 * linked list containing the hosts read from the file.
 * it is used internally by host_add().
 */
static struct host*
host_new(char *user, char *host, uint16_t port)
{
    static struct host *hst;

    if (!(hst = calloc(1, sizeof(struct host)))) goto fail;

    if (user) {
        hst->user = calloc(1, strlen(user)+1);
        if (hst->user == NULL)
            goto fail;
        strncpy(hst->user, user, strlen(user));
    } else {
        hst->user = NULL;
    }

    if (host) {
        hst->host = calloc(1, strlen(host)+1);
        if (hst->host == NULL)
            goto fail;
        strncpy(hst->host, host, strlen(host));
    } else {
        hst->host = NULL;
    }

    hst->port = port;

    hst->next = NULL;

    return(hst);
fail:
    perr("Can't alloc mem in %s\n", __func__);

    if (hst != NULL) {
        if (hst->user != NULL)
            free(hst->user);
        if (hst->host != NULL)
            free(hst->host);
        free(hst);
    }

    exit(1);
}

/*
 * routine for adding elements in the existing hostlist
 * linked list.
 */
static struct host*
host_add(struct host *hst, char *user, char *host, uint16_t port)
{
    if (hst == NULL)
        return(host_new(user, host, port));

    hst->next = host_add(hst->next, user, host, port);
    return(hst->next);
}

static FILE*
host_openfile(char *fname)
{
    int   fnamelen;
    char *home;
    FILE *hstlist;

    if (fname == NULL) {
        home = getenv("HOME");
        if (!home) {
            perr("Can't get HOME env var in %s\n", __func__);
            return NULL;
        }

        fnamelen = strlen(home) + strlen("/"HSTLIST) + 1;

        fname = calloc(1, fnamelen);

        if (!fname) {
            perr("Can't alloc mem in %s\n", __func__);
            return NULL;
        }

        sprintf(fname, "%s/"HSTLIST, home);

    } else if (strcmp(fname, "-") == 0) {

        fname = calloc(1, strlen("stdin")+1);

        if (!fname) {
            perr("Can't alloc mem in %s\n", __func__);
            return NULL;
        }

        sprintf(fname, "stdin");

        hstlist = stdin;

        if (verbose)
            fprintf(stdout, "Reading hosts from : stdin\n");

        goto out;
    }

    if (verbose)
        fprintf(stdout, "Reading hosts from : %s\n", fname);

    hstlist = fopen(fname, "r");

    if (!hstlist)
        perr("Can't open file: %s (%s) in %s\n",
            fname, strerror(errno), __func__);
out:
    return hstlist;
}

/*
 * routine that reads the host from a file and puts them
 * in the hostlist linked list using the above two routines
 */
struct host*
host_readlist(char *fname)
{
    FILE   *hstlist;
    struct  host *hst;
    struct  host *hst_it;
    struct  host *hst_head;
    char    line[MAXNAME*3];
    int     i;
    int     linelen;
    u_long  port;
    char   *login = NULL;
    char   *hostname = NULL;
    char   *llabel = NULL;

    hstlist = host_openfile(fname);

    if (hstlist == NULL)
        exit(1);

    hst_head = hst = host_add(NULL, NULL, NULL, 0);

    if (hst_head == NULL) {
        perr("Unable to add host structure head in %s\n", __func__);
        exit(1);
    }

    while (fgets(line, sizeof(line), hstlist)) {

        if (sscanf(line, "%[A-Za-z0-9-.@:%]", line) != 1)
            continue;

        linelen = strlen(line);

        /* label support */
        if (line[0] == '%') {
            if (llabel)
                free(llabel);
            llabel = calloc(1, linelen + 1);
            if (llabel == NULL) {
                perr("Can't alloc mem in %s\n", __func__);
                exit(1);
            }
            strncpy(llabel, &line[1], linelen);
            continue;
        }

        hostname = line;
        login = NULL;
        port = NON_DEFINED_PORT;

        /* XXX: This is ugly */
        for (i=0; i < linelen; i++) {
            switch (line[i]) {
                case '@':
                    if (login)
                        break;
                    line[i] = '\0';
                    if (strlen(line))
                        login = line;
                    else
                        break;
                    hostname = &line[i+1];
                    break;
                case ':':
                    line[i] = '\0';
                    port = strtol(&line[i+1],
                        (char **)NULL, 10);
                    break;
                default:
                    break;
            }
        }

        if ((hostname == NULL) || (strlen(hostname) == 0))
            continue;

        errno = 0;

        if (!login)
            login = user;

        /* check if labels match */
        if (label && llabel) {
            if (strcmp(llabel, label))
                continue;
        }

        /* check for duplicate host */
        hst_it = hst_head;
        do
        {
            if ((hst_it->host != NULL && !strcmp(hst_it->host, hostname))
                    && (hst_it->user != NULL && !strcmp(hst_it->user, login))
                    && (hst_it->port == (uint16_t)port))
            {
                break;
            }
        }
        while ((hst_it = hst_it->next));

        /* throw out duplicate host */
        if (hst_it)
            continue;

        /* add the host record */
        hst = host_add(hst, login, hostname, (uint16_t)port);

        if (hst == NULL) {
            perr("Unable to add member to "
                "the host struct in %s\n", __func__);
            exit(1);
        }

        /* keep track of the longest username */
        if (login && strlen(login) > user_len_max)
            user_len_max = strlen(login);

        /* keep track of the longest hostname */
        if (strlen(hostname) > host_len_max)
            host_len_max = strlen(hostname);

        hostcount++;
    }

    if (llabel)
        free(llabel);

    fclose(hstlist);

    if (maxchld > hostcount)
        maxchld = hostcount;

    hst = hst_head->next;

    free(hst_head);

    return(hst);
}

void
host_free(struct host *hst)
{
    struct host *next = NULL;

    if (hst == NULL)
        return;

    next = hst;

    while (next != NULL) {
        hst = hst->next;

        if (next->host != NULL)
            free(next->host);
        if (next->user != NULL)
            free(next->user);

        free(next);
        next = hst;
    }
}
