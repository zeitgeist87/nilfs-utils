/*
 * chcp.c - NILFS command of changing checkpoint mode.
 *
 * Copyright (C) 2007 Nippon Telegraph and Telephone Corporation.
 *
 * This file is part of NILFS.
 *
 * NILFS is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * NILFS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with NILFS; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Koji Sato <koji@osrg.net>.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if HAVE_UNISTD_H
#include <unistd.h>
#endif	/* HAVE_UNISTD_H */

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_STRING_H */

#if HAVE_LIMITS_H
#include <limits.h>
#endif	/* HAVE_LIMITS_H */

#include <errno.h>
#include "nilfs.h"


#define CHCP_MODE_CP	"cp"
#define CHCP_MODE_SS	"ss"
#define CHCP_BASE	10

#ifdef _GNU_SOURCE
#include <getopt.h>
const static struct option long_option[] = {
	{"help", no_argument, NULL, 'h'},
	{NULL, 0, NULL, 0}
};

#define CHCP_USAGE	\
	"Usage: %s [OPTION]... " CHCP_MODE_CP "|" CHCP_MODE_SS" [DEVICE] CNO...\n"	\
	"  -h, --help\t\tdisplay this help and exit\n"
#else	/* !_GNU_SOURCE */
#define CHCP_USAGE	\
	"Usage: %s [option]... " CHCP_MODE_CP "|" CHCP_MODE_SS " [device] cno...\n"
#endif	/* _GNU_SOURCE */


int main(int argc, char *argv[])
{
	struct nilfs *nilfs;
	nilfs_cno_t cno;
	char *dev, *modestr, *progname, *endptr;
	int c, mode, status;
#ifdef _GNU_SOURCE
	int option_index;
#endif	/* _GNU_SOURCE */

	opterr = 0;

	if ((progname = strrchr(argv[0], '/')) == NULL)
		progname = argv[0];
	else
		progname++;

#ifdef _GNU_SOURCE
	while ((c = getopt_long(argc, argv, "fh",
				long_option, &option_index)) >= 0) {
#else	/* !_GNU_SOURCE */
	while ((c = getopt(argc, argv, "fh")) >= 0) {
#endif	/* _GNU_SOURCE */

		switch (c) {
		case 'h':
			fprintf(stderr, CHCP_USAGE, progname);
			exit(0);
		default:
			fprintf(stderr, "%s: invalid option -- %c\n",
				progname, optopt);
			exit(1);
		}
	}

	if (optind > argc - 2) {
		fprintf(stderr, "%s: too few arguments\n", progname);
		exit(1);
	} else if (optind == argc - 2) {
		modestr = argv[optind++];
		dev = NULL;
	} else {
		modestr = argv[optind++];
		strtoul(argv[optind], &endptr, CHCP_BASE);
		if (*endptr == '\0')
			dev = NULL;
		else
			dev = argv[optind++];
	}

	if (strcmp(modestr, CHCP_MODE_CP) == 0)
		mode = NILFS_CHECKPOINT;
	else if (strcmp(modestr, CHCP_MODE_SS) == 0)
		mode = NILFS_SNAPSHOT;
	else {
		fprintf(stderr, "%s: %s: invalid checkpoint mode\n",
			progname, modestr);
		exit(1);
	}

	if ((nilfs = nilfs_open(dev, NILFS_OPEN_RDWR)) == NULL) {
		fprintf(stderr, "%s: %s: cannot open NILFS\n", progname, dev);
		exit(1);
	}

	status = 0;

	if (nilfs_lock_write(nilfs) < 0) {
		fprintf(stderr, "%s: cannot lock NILFS\n", progname);
		status = 1;
		goto out;
	}

	for (; optind < argc; optind++) {
		cno = strtoul(argv[optind], &endptr, CHCP_BASE);
		if (*endptr != '\0') {
			fprintf(stderr, "%s: %s: invalid checkpoint number\n",
				progname, argv[optind]);
			status = 1;
			continue;
		} else if ((cno == ULONG_MAX) && (errno == ERANGE)) {
			fprintf(stderr, "%s: %s: %s\n",
				progname, argv[optind],	strerror(errno));
			status = 1;
			continue;
		}

		if (nilfs_change_cpmode(nilfs, cno, mode) < 0) {
			if (errno == ENOENT)
				fprintf(stderr,	"%s: %llu: no checkpoint\n",
					progname, (unsigned long long)cno);
			else
				fprintf(stderr, "%s: %s\n",
					progname, strerror(errno));
			status = 1;
			continue;
		}
	}

	nilfs_unlock_write(nilfs);

 out:
	nilfs_close(nilfs);
	exit(status);
}

/* Local Variables:		*/
/* eval: (c-set-style "linux")	*/
/* End:				*/