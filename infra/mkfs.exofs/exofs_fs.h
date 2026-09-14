#pragma once

#include "offsetof.h"

#include <linux/types.h>
#include <stdint.h>

#define EXOFS_IDATA		5

#define EXOFS_SUPER_ID	0x10000	/* object ID for on-disk superblock */
#define EXOFS_ROOT_ID	0x10001	/* object ID for root directory */
#define EXOFS_OBJ_OFF	EXOFS_ROOT_ID	/* offset for objects */
#define EXOFS_SUPER_MAGIC	0x5DF5

#define NVME_ATTR_KEY_HIGH (1ULL << 31)

static inline uint64_t exofs_nvme_metadata(uint64_t key)
{
    return key | NVME_ATTR_KEY_HIGH;
}

static inline uint64_t exofs_nvme_data(uint64_t key)
{
    return key;
}

/****************************************************************************
 * superblock-related things
 ****************************************************************************/
#define EXOFS_SUPER_MAGIC	0x5DF5

/*
 * The file system control block - stored in object EXOFS_SUPER_ID's data.
 * This is where the in-memory superblock is stored on disk.
 */
enum {EXOFS_FSCB_VER = 2};
struct exofs_fscb {
    __le16  s_magic;	/* Magic signature */
	__le16  s_newfs;	/* Non-zero if this is a new fs */
    __le32	s_version;	/* == EXOFS_FSCB_VER */
	__le64  s_nextid;	/* Only used after mkfs */
	__le64  s_numfiles;	/* Only used after mkfs */
} __attribute__((packed));

/*
 * The file control block - stored in an object's attributes.  This is where
 * the in-memory inode is stored on disk.
 */
struct exofs_fcb {
	__le64  i_size;			/* Size of the file */
	__le16  i_mode;         	/* File mode */
	__le16  i_links_count;  	/* Links count */
	__le32  i_uid;          	/* Owner Uid */
	__le32  i_gid;          	/* Group Id */
	__le64  i_atime;        	/* Access time */
	__le64  i_ctime;        	/* Creation time */
	__le64  i_mtime;        	/* Modification time */
	__le32  i_flags;        	/* File flags (unused for now)*/
	__le32  i_generation;   	/* File version (for NFS) */
	__le32  i_data[EXOFS_IDATA];	/* Short symlink names and device #s */
};

#define EXOFS_NAME_LEN	255

/*
 * The on-disk directory entry
 */
struct exofs_dir_entry {
	__le64		inode_no;		/* inode number           */
	__le16		rec_len;		/* directory entry length */
	uint8_t		name_len;		/* name length            */
	uint8_t		file_type;		/* umm...file type        */
	char		name[EXOFS_NAME_LEN];	/* file name              */
};

#define EXOFS_DIR_PAD			4
#define EXOFS_DIR_ROUND			(EXOFS_DIR_PAD - 1)
#define EXOFS_DIR_REC_LEN(name_len) \
	(((name_len) + offsetof(struct exofs_dir_entry, name)  + \
	  EXOFS_DIR_ROUND) & ~EXOFS_DIR_ROUND)

#define EXOFS_FT_DIR		2
