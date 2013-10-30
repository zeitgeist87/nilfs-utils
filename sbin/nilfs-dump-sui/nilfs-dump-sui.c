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

#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif	/* HAVE_STDLIB_H */

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif	/* HAVE_SYS_TYPES_H */

#if HAVE_LINUX_TYPES_H
#include <linux/types.h>
#endif	/* HAVE_LINUX_TYPES_H */

#if HAVE_UNISTD_H
#include <unistd.h>
#endif	/* HAVE_UNISTD_H */

#if HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif	/* HAVE_SYS_IOCTL_H */

#if HAVE_LIMITS_H
#include <limits.h>
#endif	/* HAVE_LIMITS_H */

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_SYS_STRING_H */

#if HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif	/* HAVE_LINUX_TYPES_H */

#if HAVE_LINUX_FIEMAP_H
#include <linux/fiemap.h>
#endif	/* HAVE_LINUX_FIEMAP_H */

#include <sys/stat.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include "nls.h"
#include "nilfs.h"
#include "pathnames.h"
#include "realpath.h"

#ifdef _GNU_SOURCE
#include <getopt.h>
const static struct option long_option[] = {
	{"help", no_argument, NULL, 'h'},
	{"version", no_argument, NULL, 'V'},
	{NULL, 0, NULL, 0}
};
#define NILFS_DUMP_SUI_USAGE						\
	"Usage: %s [options] [mountpoint]\n"				\
	"  -h, --help\t\tdisplay this help and exit\n"			\
	"  -V, --version\t\tdisplay version and exit\n"
#else
#define NILFS_DUMP_SUI_USAGE						\
	"Usage: %s [-V] [mountpoint]\n"
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

	for (segnum = 0; segnum < sustat.ss_nsegs; segnum += n) {
		count = (sustat.ss_nsegs - segnum < NILFS_CLEANERD_NSUINFO) ?
			sustat.ss_nsegs - segnum : NILFS_CLEANERD_NSUINFO;
		if ((n = nilfs_get_suinfo(nilfs, segnum, si, count)) < 0) {
			ret = n;
			goto out;
		}
		for (i = 0; i < n; i++) {
			printf("%llu %lu\n", si[i].sui_lastmod, (unsigned long)si[i].sui_nblocks);
		}
	}
	ret = 0;

  out:
    return ret;
}

static inline int iseol(int c)
{
	return (c == '\n' || c == '\0');
}

static size_t tokenize(char *line, char **tokens, size_t ntoks)
{
	char *p;
	size_t n;

	p = line;
	for (n = 0; n < ntoks; n++) {
		while (isspace(*p))
			p++;
		if (iseol(*p))
			break;
		tokens[n] = p++;
		while (!isspace(*p) && !iseol(*p))
			p++;
		if (isspace(*p))
			*p++ = '\0';
		else
			*p = '\0';
	}
	return n;
}

#ifndef LINE_MAX
#define LINE_MAX	2048
#endif	/* LINE_MAX */
#define NMNTFLDS	6
#define MNTFLD_FS	0
#define MNTFLD_DIR	1
#define MNTFLD_TYPE	2
#define MNTFLD_OPTS	3
#define MNTFLD_FREQ	4
#define MNTFLD_PASSNO	5

static struct nilfs *nilfs_dump_sui_find_mount(const char *mountpoint) {
	FILE *fp;
	char line[LINE_MAX], *mntent[NMNTFLDS], maxmatch[LINE_MAX],
			maxmatch_type[LINE_MAX], canonical[PATH_MAX + 2];
	size_t maxmatch_len = 0, len, n;
	struct nilfs *ret;

	if (!myrealpath(mountpoint, canonical, sizeof(canonical)))
		return NULL ;

	mountpoint = canonical;

	fp = fopen(_PATH_PROC_MOUNTS, "r");
	if (fp == NULL )
		return NULL ;

	ret = NULL;

	while (fgets(line, sizeof(line), fp) != NULL ) {
		n = tokenize(line, mntent, NMNTFLDS);
		assert(n == NMNTFLDS);

		len = strlen(mntent[MNTFLD_DIR]);
		if (len > maxmatch_len && !strncmp(mntent[MNTFLD_DIR], mountpoint, len)) {
			strcpy(maxmatch, mntent[MNTFLD_DIR]);
			strcpy(maxmatch_type, mntent[MNTFLD_TYPE]);
			maxmatch_len = len;
		}
	}

	if (maxmatch_len && !strcmp(maxmatch_type, NILFS_FSTYPE)) {
		ret = nilfs_open(NULL, maxmatch, NILFS_OPEN_RAW | NILFS_OPEN_RDONLY);
	}

	fclose(fp);
	return ret;
}

static char *parse_options(int argc, char *argv[]){
	char *progname;
	int c;

	if ((progname = strrchr(argv[0], '/')) != NULL)
		progname++;
	else
		progname = argv[0];


#ifdef _GNU_SOURCE
	while ((c = getopt_long(argc, argv, "hvV",
				long_option, NULL)) >= 0) {
#else
	while ((c = getopt(argc, argv, "hvV")) >= 0) {
#endif
		switch (c) {
		case 'h':
			fprintf(stderr, NILFS_DUMP_SUI_USAGE, progname);
			return NULL;
		case 'V':
			fprintf(stderr, "%s version %s\n", progname, PACKAGE_VERSION);
			return NULL;
		default:
			fprintf(stderr, _("Error: invalid option -- %c\n"), optopt);
			return NULL;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, NILFS_DUMP_SUI_USAGE, progname);
		return NULL;
	}

	return argv[optind];
}


int main(int argc, char *argv[])
{
	struct nilfs *nilfs;
	char *mountpoint;
	int ret = -1;

	if((mountpoint = parse_options(argc, argv)) == NULL)
		return EXIT_SUCCESS;


	if ((nilfs = nilfs_dump_sui_find_mount(mountpoint)) == NULL){
		fprintf(stderr, _("Error: Cannot find corresponding nilfs volume for %s\n"), mountpoint);
		goto out;
	}

	ret = nilfs_dump_sui_do_run(nilfs);

  out:
	nilfs_close(nilfs);
	return (ret < 0) ? EXIT_FAILURE : EXIT_SUCCESS;;
}
