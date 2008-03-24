/*
 * umount.nilfs2.c - umount NILFS
 *
 * Copyright (C) 2007 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 * Written by Ryusuke Konishi <ryusuke@osrg.net>
 *         using examples from util-linux-2.12r/{umount,lomount}.c.
 *
 * The following functions are based on util-linux-2.12r/mount.c
 *  - umount_one()
 *  - complain()
 *
 * The following function is extracted from util-linux-2.12r/lomount.c
 *  - del_loop()
 */

#define _LARGEFILE64_SOURCE
#define _XOPEN_SOURCE 600

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <strings.h>
#include <stdarg.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

#include "fstab.h"
#include "paths.h"
#include "sundries.h"
#include "xmalloc.h"
#include "mntent.h"
#include "mount_constants.h"
#include "mount.nilfs2.h"
#include "nls.h"


int verbose = 0;
int mount_quiet = 0;
static int nomtab = 0;

extern char *optarg;
extern int optind;

const char fstype[] = NILFS2_FS_NAME;
char *progname = "umount." NILFS2_FS_NAME;

const char gcpid_opt_fmt[] = PIDOPT_NAME "=%d";
typedef int gcpid_opt_t;

struct umount_options {
	int flags;
	int force;
	int lazy;	/* not supported yet */
	int remount;
	int suid;	/* reserved for non-root user mount/umount
			   (not supported yet) */
};

struct umount_options options = {
	.force = 0,
	.lazy = 0,
	.remount = 0,
	.suid = 0,
};

/*
 * Other routines
 */
static void parse_options(int argc, char *argv[], struct umount_options *opts)
{
	int c;

	while ((c = getopt(argc, argv, "nlfvr")) != EOF) {
		switch (c) {
		case 'n':
			nomtab++;
			break;
		case 'l':
			opts->lazy++;
			break;
		case 'f':
			opts->force++;
			break;
		case 'v':
			verbose++;
			break;
		case 'r':
			opts->remount++;
			break;
		default:
			break;
		}
	}
}

static int umount_one(const char *, const char *, const char *, const char *,
		      struct mntentchn *);

static int umount_dir(const char *arg)
{
	const char *mntdir;
	struct mntentchn *mc;
	int ret = 0;
	
	if (!*arg)
		die(EX_USAGE, _("Cannot umount \"\"\n"));

	mntdir = canonicalize(arg);

	mc = getmntdirbackward(mntdir, NULL);
	if (!mc) {
		error(_("Could not find %s in mtab"), mntdir);
		
		ret = umount_one(arg, mntdir, fstype, arg, NULL);
	} else {
		if (strncmp(mc->m.mnt_type, fstype, strlen(fstype)))
			die(EX_USAGE,
			    _("Different filesystem (%s) mounted on %s"),
			    mc->m.mnt_type, mntdir);

		ret = umount_one(mc->m.mnt_fsname, mc->m.mnt_dir,
				 mc->m.mnt_type, mc->m.mnt_opts, mc);
	}
	return ret;
}

int main(int argc, char *argv[])
{
	struct umount_options *opts = &options;
	int ret = 0; 

	parse_options(argc, argv, opts);

	if (argc > 0) {
		char *cp = strrchr(argv[0], '/');

		progname = (cp ? cp + 1 : argv[0]);
	}

	umask(022);

	if (opts->force)
		error(_("Force option is ignored (only supported for NFS)"));

	if (opts->lazy)
		error(_("Lazy mount not supported - ignored."));

	if (getuid() != geteuid()) {
		opts->suid = 1;
#if 0 /* XXX: normal user mount support */
		if (opts->nomtab || opts->remount)
			die(EX_USAGE, _("only root can do that"));
#else
		die(EX_USAGE,
		    _("%s: umount by non-root user is not supported yet"),
		    progname);
#endif
	}

	argc -= optind;
	argv += optind;

	if (argc < 1)
		die(EX_USAGE, _("No mountpoint specified"));
	else while (argc--)
		ret += umount_dir(*argv++);

	exit(ret);
}

/*
 * Code based on util-linux-2.12r/lomount.c
 */
#define LOOP_SET_FD		0x4C00
#define LOOP_CLR_FD		0x4C01

static int del_loop(const char *device)
{
	int fd;

	if ((fd = open (device, O_RDONLY)) < 0) {
		int errsv = errno;
		error(_("loop: can't delete device %s: %s\n"),
		      device, strerror (errsv));
		return 1;
	}
	if (ioctl (fd, LOOP_CLR_FD, 0) < 0) {
		perror ("ioctl: LOOP_CLR_FD");
		return 1;
	}
	close (fd);
	if (verbose > 1)
		printf(_("del_loop(%s): success\n"), device);
	return 0;
}

/*
 * Code based on util-linux-2.12r/umount.c
 */
/* complain about a failed umount */
static void complain(int err, const char *dev)
{
	switch (err) {
	case ENXIO:
		error(_("%s: %s: invalid block device"), progname, dev);
		break;
	case EINVAL:
		error(_("%s: %s: not mounted"), progname, dev);
		break;
	case EIO:
		error(_("%s: %s: I/O error while unmounting"), progname, dev);
		break;
	case EBUSY:
		/* Let us hope fstab has a line "proc /proc ..."
		   and not "none /proc ..."*/
		error(_("%s: %s: device is busy"), progname, dev);
		break;
	case ENOENT:
		error(_("%s: %s: not found"), progname, dev);
		break;
	case EPERM:
		error(_("%s: %s: must be superuser to umount"), progname, dev);
		break;
	case EACCES:
		error(_("%s: %s: block devices not permitted on fs"), progname,
		      dev);
		break;
	default:
		error(_("%s: %s: %s"), progname, dev, strerror(err));
		break;
	}
}

static inline int read_only_mount_point(const struct mntentchn *mc)
{
	return (find_opt(mc->m.mnt_opts, "ro", NULL) >= 0);
}

static inline pid_t get_mtab_gcpid(const struct mntentchn *mc)
{
	pid_t pid = 0;
	gcpid_opt_t id;

	if (find_opt(mc->m.mnt_opts, gcpid_opt_fmt, &id) >= 0)
			pid = id;
	return pid;
}

static inline char *change_gcpid_opt(const char *opts, pid_t newpid)
{
	char buf[256];
	gcpid_opt_t oldpid;

	buf[0] = '\0';
	snprintf(buf, sizeof(buf), gcpid_opt_fmt, (int)newpid);
	return change_opt(opts, gcpid_opt_fmt, &oldpid, buf);
}

static inline void my_free(const void *ptr)
{
	/* free(NULL) is ignored; the check below is just to be sure */
	if (ptr)
		free((void *)ptr);
}

static void change_mtab_opt(const char *spec, const char *node,
			    const char *type, char *opts)
{
	struct my_mntent mnt;

	mnt.mnt_fsname = canonicalize(spec);
	mnt.mnt_dir = canonicalize(node);
	mnt.mnt_type = xstrdup(type);
	mnt.mnt_freq = 0;
	mnt.mnt_passno = 0;
	/* Above entries are used only when adding new entry */
	mnt.mnt_opts = opts;

	if (!nomtab && mtab_is_writable())
		update_mtab(node, &mnt);

	my_free(mnt.mnt_fsname);
	my_free(mnt.mnt_dir);
	my_free(mnt.mnt_type);
	my_free(mnt.mnt_opts);
}

/* Umount a single device.  Return a status code, so don't exit
   on a non-fatal error.  We lock/unlock around each umount.  */
static int
umount_one(const char *spec, const char *node, const char *type,
	   const char *opts, struct mntentchn *mc)
{
	int umnt_err = 0;
	int res, alive = 0;
	const char *loopdev;
	pid_t pid;

	if (streq (node, "/") || streq (node, "root"))
		nomtab++;

	if (mc) {
		if (!read_only_mount_point(mc) &&
		    (pid = get_mtab_gcpid(mc)) != 0) {
			alive = check_cleanerd(spec, pid);
			stop_cleanerd(spec, pid);
		}
	}

	res = umount(node);
	if (res < 0)
		umnt_err = errno;

	if (res < 0 && (umnt_err == EBUSY)) {
		if (options.remount) {
			/* Umount failed - let us try a remount */
			res = mount(spec, node, NULL,
				    MS_MGC_VAL | MS_REMOUNT | MS_RDONLY, NULL);
			if (res == 0) {
				error(_("%s: %s busy - remounted read-only"),
				      progname, spec);
				change_mtab_opt(spec, node, type,
						xstrdup("ro"));
				return 0;
			} else if (errno != EBUSY) { 	/* hmm ... */
				error(_("%s: could not remount %s read-only"),
				      progname, spec);
			}
		} else if (alive && !check_cleanerd(spec, pid)) {
			if (start_cleanerd(spec, node, &pid) == 0) {
				if (verbose)
					printf(_("%s: restarted %s(pid=%d)\n"),
					       progname, CLEANERD_NAME,
					       (int)pid);
				change_mtab_opt(spec, node, type,
						change_gcpid_opt(opts, pid));
				goto out;
			} else
				error(_("%s: failed to restart %s"),
				      progname, CLEANERD_NAME);
		}
	}

	loopdev = 0;
	if (res >= 0) {
		/* Umount succeeded */
		if (verbose)
			printf(_("%s: %s umounted\n"), progname, spec);

		/* Free any loop devices that we allocated ourselves */
		if (mc) {
			char *optl;

			/* old style mtab line? */
			if (streq(mc->m.mnt_type, "loop")) {
				loopdev = spec;
				goto gotloop;
			}

			/* new style mtab line? */
			optl = mc->m.mnt_opts ? xstrdup(mc->m.mnt_opts) : "";
			for (optl = strtok (optl, ","); optl;
			     optl = strtok (NULL, ",")) {
				if (!strncmp(optl, "loop=", 5)) {
					loopdev = optl+5;
					goto gotloop;
				}
			}
		} else {
			/*
			 * If option "-o loop=spec" occurs in mtab,
			 * note the mount point, and delete mtab line.
			 */
			if ((mc = getmntoptfile (spec)) != NULL)
				node = mc->m.mnt_dir;
		}

#if 0 /* XXX: -d flag is not delivered by the mount program */
		/* Also free loop devices when -d flag is given */
		if (delloop && is_loop_device(spec))
			loopdev = spec;
#endif
	}
 gotloop:
	if (loopdev)
		del_loop(loopdev);

// writemtab:
	if (!nomtab && mtab_is_writable() &&
	    (umnt_err == 0 || umnt_err == EINVAL || umnt_err == ENOENT)) {
		update_mtab(node, NULL);
	}
 out:
	if (res >= 0)
		return 0;
	if (umnt_err)
		complain(umnt_err, node);
	return 1;
}


/* Local Variables:		*/
/* eval: (c-set-style "linux")	*/
/* End:				*/