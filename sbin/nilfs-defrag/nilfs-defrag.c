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
	{"verbose", no_argument, NULL, 'v'},
	{"version", no_argument, NULL, 'V'},
	{NULL, 0, NULL, 0}
};
#define NILFS_CLEAN_USAGE						\
	"Usage: %s [options] [file]\n"				\
	"  -h, --help\t\tdisplay this help and exit\n"			\
	"  -v, --verbose\t\tverbose mode\n"				\
	"  -V, --version\t\tdisplay version and exit\n"
#else
#define NILFS_CLEAN_USAGE						\
	"Usage: %s [-v] [-V] [file]\n"
#endif	/* _GNU_SOURCE */

static int option_verbose = 0;

#define MIN_BLOCKS_PER_FILE 5
#define MAX_EXTENTS_PER_SEGMENT 3

static int nilfs_defrag_mark_blocks_dirty(int fd, __u64 offset, __u64 length) {
	if (option_verbose)
		printf("DEFRAG: %llu %llu\n", offset, length);
	__u64 range[2];
	range[0] = offset;
	range[1] = length;
	return ioctl(fd, NILFS_IOCTL_MARK_EXTENT_DIRTY, &range);
}

static int nilfs_defrag_get_bshift(size_t value){
	int count = 0;
	while(value>1){
		value >>= 1;
		count++;
	}
	return count;
}

static int nilfs_defrag_do_run(struct nilfs *nilfs, int fd, off_t size) {
	__u32 j, blocks_per_seg = nilfs_get_blocks_per_segment(nilfs);
	size_t bsize = nilfs_get_block_size(nilfs);
	int ret, bshift = nilfs_defrag_get_bshift(bsize);
	off_t i, bcount = (size + bsize - 1) >> bshift;
	struct fiemap *fiemap;

	if (bcount < MIN_BLOCKS_PER_FILE)
		return 0;

	if ((fiemap = malloc(sizeof(struct fiemap) + sizeof(struct fiemap_extent) * blocks_per_seg / 2)) == NULL ) {
		fprintf(stderr, _("Error: not enough memory\n"));
		return -1;
	}
	memset(fiemap, 0, sizeof(struct fiemap));
	fiemap->fm_extent_count = blocks_per_seg / 2;

	for (i = 0; i < bcount;) {
		fiemap->fm_start = i << bshift;
		fiemap->fm_length = blocks_per_seg << bshift;
		fiemap->fm_mapped_extents = 0;

		if ((ret = ioctl(fd, FS_IOC_FIEMAP, fiemap)) < 0) {
			perror(_("Error: ioctl failed"));
			goto out;
		}

		if (fiemap->fm_mapped_extents > MAX_EXTENTS_PER_SEGMENT) {
			for (j = 0; j < fiemap->fm_mapped_extents; ++j) {
				if (!(fiemap->fm_flags & FIEMAP_EXTENT_DELALLOC)) {
					ret = nilfs_defrag_mark_blocks_dirty(fd,
							fiemap->fm_extents[j].fe_logical >> bshift,
							fiemap->fm_extents[j].fe_length >> bshift);
					if (ret < 0){
						perror(_("Error: ioctl failed"));
						goto out;
					}
				}
			}
		}

		i = (fiemap->fm_extents[fiemap->fm_mapped_extents - 1].fe_logical
				+ fiemap->fm_extents[fiemap->fm_mapped_extents - 1].fe_length)
				>> bshift;
	}
  out:
	free(fiemap);
	return ret;
}

static int nilfs_defrag_check_clean_segs(struct nilfs *nilfs, size_t size){
	struct nilfs_sustat sustat;
	__u32 blocks_per_seg = nilfs_get_blocks_per_segment(nilfs);
	size_t bsize = nilfs_get_block_size(nilfs);
	off_t bcount = (size + bsize - 1) / bsize;

	if (nilfs_get_sustat(nilfs, &sustat) < 0) {
		fprintf(stderr, _("Error: cannot get sustat\n"));
		return -1;
	}

	if (sustat.ss_ncleansegs < bcount / blocks_per_seg)
		return -1;

	return 0;
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

static struct nilfs *nilfs_defrag_find_mount(const char *filename,
		struct stat *file_st) {
	FILE *fp;
	char line[LINE_MAX], *mntent[NMNTFLDS], maxmatch[LINE_MAX],
			maxmatch_type[LINE_MAX], canonical[PATH_MAX + 2];
	size_t maxmatch_len = 0, len, n;
	struct nilfs *ret, *tmp;
	struct stat dev_st;

	if (!myrealpath(filename, canonical, sizeof(canonical)))
		return NULL ;

	filename = canonical;

	fp = fopen(_PATH_PROC_MOUNTS, "r");
	if (fp == NULL )
		return NULL ;

	ret = NULL;

	while (fgets(line, sizeof(line), fp) != NULL ) {
		n = tokenize(line, mntent, NMNTFLDS);
		assert(n == NMNTFLDS);

		len = strlen(mntent[MNTFLD_DIR]);
		if (len > maxmatch_len && !strncmp(mntent[MNTFLD_DIR], filename, len)) {
			strcpy(maxmatch, mntent[MNTFLD_DIR]);
			strcpy(maxmatch_type, mntent[MNTFLD_TYPE]);
			maxmatch_len = len;
		}
	}

	if (maxmatch_len && !strcmp(maxmatch_type, NILFS_FSTYPE)) {
		tmp = nilfs_open(NULL, maxmatch, NILFS_OPEN_RAW | NILFS_OPEN_RDONLY);

		if (tmp && !fstat(tmp->n_devfd, &dev_st)
				&& dev_st.st_rdev == file_st->st_dev)
			ret = tmp;
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
			fprintf(stderr, NILFS_CLEAN_USAGE, progname);
			return NULL;
		case 'v':
			option_verbose = 1;
			break;
		case 'V':
			fprintf(stderr, "%s version %s\n", progname, PACKAGE_VERSION);
			return NULL;
		default:
			fprintf(stderr, _("Error: invalid option -- %c\n"), optopt);
			return NULL;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, NILFS_CLEAN_USAGE, progname);
		return NULL;
	}

	return argv[optind];
}


int main(int argc, char *argv[])
{
	struct nilfs *nilfs;
	struct stat st;
	char *filename;
	int fd, ret = -1;

	if((filename = parse_options(argc, argv)) == NULL)
		return EXIT_SUCCESS;

	if ((fd = open(filename, O_RDONLY)) < 0 || fstat(fd, &st)) {
		fprintf(stderr, _("Error: Cannot find %s: %s\n"), filename,
			strerror(errno));
		return EXIT_FAILURE;
	}

	if (!S_ISREG(st.st_mode)){
		fprintf(stderr, _("Error: Not a regular file: %s\n"), filename);
		goto out;
	}

	if ((nilfs = nilfs_defrag_find_mount(filename, &st)) == NULL){
		fprintf(stderr, _("Error: Cannot find corresponding nilfs volume for %s\n"), filename);
		goto out;
	}

	if ((ret = nilfs_defrag_check_clean_segs(nilfs, st.st_size)) < 0){
		fprintf(stderr, _("Error: Not enough clean segments available. Please run cleaner first.\n"));
		goto out;
	}

	ret = nilfs_defrag_do_run(nilfs, fd, st.st_size);

  out:
	close(fd);
	return (ret < 0) ? EXIT_FAILURE : EXIT_SUCCESS;;
}
