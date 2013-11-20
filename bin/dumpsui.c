/*
 * nilfs-defrag.c - defragment files on a nilfs2 volume
 *
 * Licensed under GPLv2: the complete text of the GNU General Public License
 * can be found in COPYING file of the nilfs-utils package.
 *
 * Copyright (C) 2013
 * Written by Andreas Rohner <andreas.rohner@gmx.net>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_SYS_STRING_H */

#include <stdio.h>
#include "nls.h"
#include "nilfs.h"

#ifdef _GNU_SOURCE
#include <getopt.h>
const static struct option long_option[] = {
	{"help", no_argument, NULL, 'h'},
	{"version", no_argument, NULL, 'V'},
	{NULL, 0, NULL, 0}
};
#define NILFS_DUMP_SUI_USAGE						\
	"Usage: %s [options] [dev]\n"				\
	"  -h, --help\t\tdisplay this help and exit\n"			\
	"  -V, --version\t\tdisplay version and exit\n"
#else
#define NILFS_DUMP_SUI_USAGE						\
	"Usage: %s [-V] [dev]\n"
#endif	/* _GNU_SOURCE */

#define NILFS_CLEANERD_NSUINFO	512

static int nilfs_dump_sui_do_run(struct nilfs *nilfs) {
	struct nilfs_suinfo si[NILFS_CLEANERD_NSUINFO];
	struct nilfs_sustat sustat;
	__u64 segnum;
	size_t count;
	ssize_t n;
	int i, ret;

	if (nilfs_get_sustat(nilfs, &sustat) < 0) {
		fprintf(stderr, "cannot get segment usage stat");
		return -1;
	}

	/* sui_lastdec may not be set by nilfs_get_suinfo*/
	memset(si, 0, sizeof(si));

	for (segnum = 0; segnum < sustat.ss_nsegs; segnum += n) {
		count = (sustat.ss_nsegs - segnum < NILFS_CLEANERD_NSUINFO) ?
			sustat.ss_nsegs - segnum : NILFS_CLEANERD_NSUINFO;
		if ((n = nilfs_get_suinfo(nilfs, segnum, si, count)) < 0) {
			ret = n;
			goto out;
		}
		for (i = 0; i < n; i++) {
			printf("%llu %lu %llu\n", si[i].sui_lastmod, (unsigned long)si[i].sui_nblocks, si[i].sui_lastdec);
		}
	}
	ret = 0;

  out:
    return ret;
}


static int parse_options(int argc, char *argv[], char **dev, char **progname){
	int c;

	if ((*progname = strrchr(argv[0], '/')) != NULL)
		(*progname)++;
	else
		*progname = argv[0];


#ifdef _GNU_SOURCE
	while ((c = getopt_long(argc, argv, "hvV",
				long_option, NULL)) >= 0) {
#else
	while ((c = getopt(argc, argv, "hvV")) >= 0) {
#endif
		switch (c) {
		case 'h':
			fprintf(stderr, NILFS_DUMP_SUI_USAGE, *progname);
			return 1;
		case 'V':
			fprintf(stderr, "%s version %s\n", *progname, PACKAGE_VERSION);
			return 1;
		default:
			fprintf(stderr, _("Error: invalid option -- %c\n"), optopt);
			return -1;
		}
	}

	if (optind < argc)
		*dev = argv[optind];

	return 0;
}


int main(int argc, char *argv[])
{
	struct nilfs *nilfs;
	char *dev = NULL, *progname;
	int ret = -1;

	if((ret = parse_options(argc, argv, &dev, &progname)) != 0)
		goto out;

	if ((nilfs = nilfs_open(dev, NULL, NILFS_OPEN_RDONLY)) == NULL) {
		fprintf(stderr, "%s: %s: cannot open NILFS\n", progname, dev);
		return EXIT_FAILURE;
	}

	ret = nilfs_dump_sui_do_run(nilfs);

	nilfs_close(nilfs);
  out:
	return (ret < 0) ? EXIT_FAILURE : EXIT_SUCCESS;;
}
