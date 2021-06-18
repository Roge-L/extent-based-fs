/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Alexey Khrabrov, Karen Reid
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2019 Karen Reid
 */

/**
 * CSC369 Assignment 1 - a1fs types, constants, and data structures header file.
 */

#pragma once

#include <assert.h>
#include <stdint.h>
#include <limits.h>
#include <sys/stat.h>


/**
 * a1fs block size in bytes. You are not allowed to change this value.
 *
 * The block size is the unit of space allocation. Each file (and directory)
 * must occupy an integral number of blocks. Each of the file systems metadata
 * partitions, e.g. superblock, inode/block bitmaps, inode table (but not an
 * individual inode) must also occupy an integral number of blocks.
 */
#define A1FS_BLOCK_SIZE 4096

/** Block number (block pointer) type. */
typedef uint32_t a1fs_blk_t;

/** Inode number type. */
typedef int32_t a1fs_ino_t;

#define INODE_PADDING 256-(sizeof(mode_t) + 3*sizeof(uint32_t) + sizeof(uint64_t) + 4*sizeof(struct timespec) + 13*sizeof(a1fs_extent) + 2*sizeof(uint32_t))

/** Magic value that can be used to identify an a1fs image. */
#define A1FS_MAGIC 0xC5C369A1C5C369A1ul

/** a1fs superblock. */
typedef struct a1fs_superblock {
	/** Must match A1FS_MAGIC. */
	uint64_t magic;
	/** File system size in bytes. */
	uint64_t size;

	uint8_t   sb_first_data_block;  /* Index of first data block */
	uint8_t	  sb_first_empty_db;	/* Index of first empty data block */
	uint8_t	  sb_total_data_blocks; /* Total data block counts */
	uint8_t   sb_block_bitmap;      /* Index of blocks bitmap block */
	uint8_t   sb_inode_bitmap;      /* Index of inodes bitmap block */
	uint8_t   sb_inode_table;       /* Index of inodes table block */
	int64_t   sb_free_blocks_count; /* Free blocks count */
	int64_t   sb_free_inodes_count; /* Free inodes count */
	int64_t   sb_inodes_count;		/* Total inodes count */
	int64_t   sb_used_dirs_count;   /* Directories count */

} a1fs_superblock;

// Superblock must fit into a single block
static_assert(sizeof(a1fs_superblock) <= A1FS_BLOCK_SIZE,
              "superblock is too large");


/** Extent - a contiguous range of blocks. */
typedef struct a1fs_extent {
	/** Starting block of the extent. */
	a1fs_blk_t start;
	/** Number of blocks in the extent. */
	int32_t count;
	/** Number of bytes currently in use by this extent*/
	int32_t size;

} a1fs_extent;


/** a1fs inode. */
typedef struct a1fs_inode {
	mode_t 			  mode;
	uint32_t 		  links;
	uint64_t 		  size;						/* Number of bytes used */
	struct timespec   i_mtime;					/* Last modified time */
    a1fs_extent       i_extent[12];  			/* Pointers to extents */  
	int32_t 		  last_used_extent;			/* Index of Last Used Extent */
	int32_t		  	  last_used_indirect;
	uint32_t 		  num_entries;				/* Number of entries if directory */
	uint8_t           i_pad[64];	  			/* Padding */

} a1fs_inode;

// A single block must fit an integral number of inodes
static_assert(A1FS_BLOCK_SIZE % sizeof(a1fs_inode) == 0, "invalid inode size");


/** Maximum file name (path component) length. Includes the null terminator. */
#define A1FS_NAME_MAX 252

/** Maximum file path length. Includes the null terminator. */
#define A1FS_PATH_MAX PATH_MAX

/** Fixed size directory entry structure. */
typedef struct a1fs_dentry {
	/** Inode number. */
	a1fs_ino_t ino;
	/** File name. A null-terminated string. */
	char name[A1FS_NAME_MAX];

} a1fs_dentry;

static_assert(sizeof(a1fs_dentry) == 256, "invalid dentry size");
