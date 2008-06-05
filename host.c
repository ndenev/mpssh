/*-
 * Copyright (c) 2005-2007 Niki Denev <ndenev@icdsoft.com>,<ndenev@gmail.com>
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
host*
host_new(char *name)
{
	host *hst;
	if (!(hst = malloc(sizeof(host)))) {
		fprintf(stderr, "%s\n", strerror(errno));
		exit(1);
	}
	strncpy(hst->name, name, sizeof(hst->name));
	hst->next = NULL;
	return(hst);
}

/*
 * routine for adding elements in the existing hostlist
 * linked list.
 */
host*
host_add(host *hst, char *name)
{
	if (!hst) return(host_new(name));
	hst->next = host_add(hst->next, name);
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
	host	*head;
	host	*hst;
	char	 line[MAXNAME];

	if (!fname)
		return(NULL);
	hstlist = fopen(fname, "r");
	if (!hstlist)
		return(NULL);
	head = host_new("");
	hst = head;
	while (fgets(line, sizeof(line), hstlist)) {
		if (sscanf(line, "%[A-Za-z0-9-.]", line) != 1)
			continue;
		hst = host_add(hst, line);
		/* keep track of the longest line */
		if (strlen(line) > host_len_max)
			host_len_max = strlen(line); 
		hostcount++;
	}
	fclose(hstlist);
	hst = head->next;
	free(head);
	if (maxchld > hostcount) maxchld = hostcount;
	return(hst);
}
