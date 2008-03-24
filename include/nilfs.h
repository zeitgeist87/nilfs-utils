/*
 * nilfs.h - NILFS header file.
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

#ifndef NILFS_H
#define NILFS_H

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <endian.h>
#include <byteswap.h>

/* FIX ME */
#ifndef __bitwise
typedef __u16	__le16;
typedef __u32	__le32;
typedef __u64	__le64;
typedef __u16	__be16;
typedef __u32	__be32;
typedef __u64	__be64;
#endif

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define le16_to_cpu(x)	((__u16)(x))
#define le32_to_cpu(x)	((__u32)(x))
#define le64_to_cpu(x)	((__u64)(x))
#define cpu_to_le16(x)	((__u16)(x))
#define cpu_to_le32(x)	((__u32)(x))
#define cpu_to_le64(x)	((__u64)(x))
#define be16_to_cpu(x)	bswap_16(x)
#define be32_to_cpu(x)	bswap_32(x)
#define be64_to_cpu(x)	bswap_64(x)
#define cpu_to_be16(x)	bswap_16(x)
#define cpu_to_be32(x)	bswap_32(x)
#define cpu_to_be64(x)	bswap_64(x)
#elif __BYTE_ORDER == __BIG_ENDIAN
#define le16_to_cpu(x)	bswap_16(x)
#define le32_to_cpu(x)	bswap_32(x)
#define le64_to_cpu(x)	bswap_64(x)
#define cpu_to_le16(x)	bswap_16(x)
#define cpu_to_le32(x)	bswap_32(x)
#define cpu_to_le64(x)	bswap_64(x)
#define be16_to_cpu(x)	((__u16)(x))
#define be32_to_cpu(x)	((__u32)(x))
#define be64_to_cpu(x)	((__u64)(x))
#define cpu_to_be16(x)	((__u16)(x))
#define cpu_to_be32(x)	((__u32)(x))
#define cpu_to_be64(x)	((__u64)(x))
#else
#error "unknown endian"
#endif	/* __BYTE_ORDER */

/* XXX: nilfs_fs.h requires cpu_to_le32 and le32_to_cpu */
#include "nilfs_fs.h"

/* XXX: sector_t is not defined in user land */
typedef __u64 sector_t;	// XXX: __u64 ??
typedef sector_t nilfs_sector_t;
typedef sector_t nilfs_blkoff_t;
typedef __u64 nilfs_segnum_t;
typedef __u64 nilfs_cno_t;

#define NILFS_IOC	".nilfs"
#define NILFS_FSTYPE	"nilfs2"

#define NILFS_CNO_MIN	((nilfs_cno_t)1)
#define NILFS_CNO_MAX	(~(nilfs_cno_t)0)


/**
 * struct nilfs - nilfs object
 * @n_sb: superblock
 * @n_dev: device file
 * @n_ioc: ioctl file
 * @n_devfd: file descriptor of device file
 * @n_iocfd: file descriptor of ioctl file
 * @n_opts: options
 */
struct nilfs {
	struct nilfs_super_block n_sb;
	char *n_dev;
	//char *n_mnt;
	char *n_ioc;
	int n_devfd;
	int n_iocfd;
	int n_opts;
};

#define NILFS_OPEN_RAW		0x01
#define NILFS_OPEN_RDONLY	0x02
#define NILFS_OPEN_WRONLY	0x04
#define NILFS_OPEN_RDWR		0x08

#define NILFS_OPT_MMAP	0x01


struct nilfs *nilfs_open(const char *, int);
void nilfs_close(struct nilfs *);

const char *nilfs_get_dev(const struct nilfs *);

void nilfs_opt_clear_mmap(struct nilfs *);
int nilfs_opt_set_mmap(struct nilfs *);
int nilfs_opt_test_mmap(struct nilfs *);


struct nilfs_super_block *nilfs_get_sb(struct nilfs *);

inline static int nilfs_lock(struct nilfs *nilfs, int cmd, int type,
			     off_t start, int whence, off_t len)
{
	struct flock flock;

	flock.l_type = type;
	flock.l_start = start;
	flock.l_whence = whence;
	flock.l_len = len;
	return fcntl(nilfs->n_iocfd, cmd, &flock);
}

#define NILFS_LOCK_FNS(name, type)					\
inline static int nilfs_lock_##name(struct nilfs *nilfs)		\
{									\
	return nilfs_lock(nilfs, F_SETLKW, type, 0, SEEK_SET, 1);	\
}									\
inline static int nilfs_unlock_##name(struct nilfs *nilfs)		\
{									\
	return nilfs_lock(nilfs, F_SETLK, F_UNLCK, 0, SEEK_SET, 1);	\
}

NILFS_LOCK_FNS(read, F_RDLCK)
NILFS_LOCK_FNS(write, F_WRLCK)


/**
 * struct nilfs_psegment - partial segment iterator
 * @p_segnum: segment number
 * @p_blocknr: block number of partial segment
 * @p_segblocknr: block number of segment
 * @p_nblocks: number of blocks in segment
 * @p_nblocks_per_segment: number of blocks per segment
 * @p_blksize: block size
 * @p_seed: CRC seed
 */
struct nilfs_psegment {
	struct nilfs_segment_summary *p_segsum;
	sector_t p_blocknr;

	sector_t p_segblocknr;
	size_t p_nblocks;
	sector_t p_nblocks_per_segment;
	size_t p_blksize;
	__u32 p_seed;
};

/**
 * struct nilfs_file - file iterator
 * @f_finfo: file information
 * @f_blocknr: block number
 * @f_offset: byte offset from the begining of segment
 * @f_index: index
 * @f_psegment: partial segment
 */
struct nilfs_file {
	struct nilfs_finfo *f_finfo;
	sector_t f_blocknr;

	unsigned long f_offset;
	int f_index;
	const struct nilfs_psegment *f_psegment;
};

/**
 * struct nilfs_block - block iterator
 * @b_binfo: block information
 * @b_blocknr: block number
 * @b_offset: byte offset from the begining of segment
 * @b_index: index
 * @b_dsize: size of data block information
 * @b_nsize: size of node block information
 * @b_file: file
 */
struct nilfs_block {
	void *b_binfo;
	sector_t b_blocknr;

	unsigned long b_offset;
	int b_index;
	size_t b_dsize;
	size_t b_nsize;
	const struct nilfs_file *b_file;
};

/* virtual block number and block offset */
#define NILFS_BINFO_DATA_SIZE		(sizeof(__le64) + sizeof(__le64))
/* virtual block number */
#define NILFS_BINFO_NODE_SIZE		sizeof(__le64)
/* block offset */
#define NILFS_BINFO_DAT_DATA_SIZE	sizeof(__le64)
/* block offset and level */
#define NILFS_BINFO_DAT_NODE_SIZE	(sizeof(__le64) + sizeof(__le64))


/* partial segment iterator */
void nilfs_psegment_init(struct nilfs_psegment *, nilfs_segnum_t,
			 void *, size_t, const struct nilfs *);
int nilfs_psegment_is_end(const struct nilfs_psegment *);
void nilfs_psegment_next(struct nilfs_psegment *);

#define nilfs_psegment_for_each(pseg, segnum, seg, nblocks, nilfs)	\
	for (nilfs_psegment_init(pseg, segnum, seg, nblocks, nilfs);	\
	     !nilfs_psegment_is_end(pseg);				\
	     nilfs_psegment_next(pseg))

__u32 crc32_le(__u32, unsigned char const *, size_t);
#define nilfs_crc32(seed, data, length)  crc32_le(seed, data, length)

/* file iterator */
void nilfs_file_init(struct nilfs_file *, const struct nilfs_psegment *);
int nilfs_file_is_end(const struct nilfs_file *);
void nilfs_file_next(struct nilfs_file *);

/* XXX: should be defined in nilfs_fs.h */
inline static int nilfs_file_is_super(const struct nilfs_file *file)
{
	__u64 ino;

	ino = le64_to_cpu(file->f_finfo->fi_ino);
	return ino == NILFS_DAT_INO;
	//return (ino == NILFS_DAT_INO) || (ino == NILFS_DAT_CHECKPOINT) || (ino == NILFS_DAT_SUFILE);
}

#define nilfs_file_for_each(file, pseg)		\
	for (nilfs_file_init(file, pseg);	\
	     !nilfs_file_is_end(file);		\
	     nilfs_file_next(file))

/* block iterator */
void nilfs_block_init(struct nilfs_block *, const struct nilfs_file *);
int nilfs_block_is_end(const struct nilfs_block *);
void nilfs_block_next(struct nilfs_block *);

inline static int nilfs_block_is_data(const struct nilfs_block *blk)
{
	return blk->b_index < le32_to_cpu(blk->b_file->f_finfo->fi_ndatablk);
}

inline static int nilfs_block_is_node(const struct nilfs_block *blk)
{
	return blk->b_index >= le32_to_cpu(blk->b_file->f_finfo->fi_ndatablk);
}

#define nilfs_block_for_each(blk, file)		\
	for (nilfs_block_init(blk, file);	\
	     !nilfs_block_is_end(blk);		\
	     nilfs_block_next(blk))

#define NILFS_SB_BLOCK_SIZE_SHIFT	10

ssize_t nilfs_get_segment(struct nilfs *, unsigned long, void **);
int nilfs_put_segment(struct nilfs *, void *);
size_t nilfs_get_block_size(struct nilfs *);


int nilfs_change_cpmode(const struct nilfs *, nilfs_cno_t, int);
ssize_t nilfs_get_cpinfo(const struct nilfs *, nilfs_cno_t, int,
			 struct nilfs_cpinfo *, size_t);
int nilfs_delete_checkpoint(const struct nilfs *, nilfs_cno_t);
int nilfs_get_cpstat(const struct nilfs *, struct nilfs_cpstat *);
ssize_t nilfs_get_suinfo(const struct nilfs *, nilfs_segnum_t,
			 struct nilfs_suinfo *, size_t);
int nilfs_get_sustat(const struct nilfs *, struct nilfs_sustat *);
ssize_t nilfs_get_vinfo(const struct nilfs *, struct nilfs_vinfo *, size_t);
ssize_t nilfs_get_bdescs(const struct nilfs *, struct nilfs_bdesc *, size_t);
int nilfs_clean_segments(const struct nilfs *,
			 struct nilfs_vdesc *, size_t,
			 struct nilfs_period *, size_t,
			 nilfs_sector_t *, size_t,
			 struct nilfs_bdesc *, size_t,
			 nilfs_segnum_t *, size_t);
int nilfs_timedwait(const struct nilfs *, int, struct timespec *);
int nilfs_sync(const struct nilfs *, nilfs_cno_t *);

#endif	/* NILFS_H */

/* Local Variables:		*/
/* eval: (c-set-style "linux")	*/
/* End:				*/