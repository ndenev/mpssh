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
#include "host.h"

/*
 * routine for allocating a new host element in the
 * linked list containing the hosts read from the file.
 * it is used internally by host_add().
 */
static host*
host_new(char *login, char *name, char *port)
{
	static host *hst;

	if (!(hst = calloc(1, sizeof(host)))) goto fail;

	if (login) {
		if (!(hst->user = calloc(1, strlen(login)+1))) goto fail;
		strncpy(hst->user, login, strlen(login));
	} else {
		hst->user = NULL;
	}

	if (name) {
		if (!(hst->name = calloc(1, strlen(name)+1))) goto fail;
		strncpy(hst->name, name, strlen(name));
	} else {
		hst->name = NULL;
	}

	if (port) {
		if (!(hst->port = calloc(1, strlen(port)+1))) goto fail;
		strncpy(hst->port, port, strlen(port));
	} else {
		hst->port = NULL;
	}

	hst->next = NULL;

	return(hst);
fail:
	fprintf(stderr, "%s\n", strerror(errno));
	return(NULL);
}

/*
 * routine for adding elements in the existing hostlist
 * linked list.
 */
static host*
host_add(host *hst, char *login, char *name, char *port)
{
	if (hst == NULL)
		return(host_new(login, name, port));
	
	hst->next = host_add(hst->next, login, name, port);
	return(hst->next);
}

/*
 * routine that reads the host from a file and puts them
 * in the hostlist linked list using the above two routines
 */
host*
host_readlist(char *fname)
{
	FILE    *hstlist;
	host	*hst;
	host	*hst_head;
	char	line[MAXNAME*3];
	int	i;
	int	linelen;
	char	*login;
	char	*name;
	char	*port;

	if (fname == NULL)
		return(NULL);

	hstlist = fopen(fname, "r");

	if (hstlist == NULL)
		return(NULL);

	hst_head = hst = host_add(NULL, NULL, NULL, NULL);

	while (fgets(line, sizeof(line), hstlist)) {
		if (sscanf(line, "%[A-Za-z0-9-.@:]", line) != 1)
			continue;

		linelen = strlen(line);

		login = NULL;
		name = line;
		port = NULL;

		for (i=0; i < linelen; i++) {
			switch (line[i]) {
				case '@':
					if (port)
						continue;
					if (login)
						continue;
					line[i] = '\0';
					login = line;
					name = &line[i+1];
					break;

				case ':': 
					if (port)
						continue;
					line[i] = '\0';
					port = &line[i+1];
					errno = 0;
					(int)strtol(port, (char **)NULL, 10);
					if (errno)
						port = NULL;
					break;

				default:
					break;
			}
		}

		hst = host_add(hst, login?login:user, name, port?port:"22");

		if (hst == NULL)
			return(NULL);

		/* keep track of the longest line */
		if (strlen(name) > host_len_max)
			host_len_max = strlen(name); 

		hostcount++;
	}
	fclose(hstlist);
	if (maxchld > hostcount) maxchld = hostcount;
	hst = hst_head->next;
	free(hst_head);	
	return(hst);
}
