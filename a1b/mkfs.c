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
 * CSC369 Assignment 1 - a1fs formatting tool.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "a1fs.h"
#include "map.h"
#include "fs_ctx.h"
#include "util.h"

struct a1fs_superblock* sb;
unsigned char* inode_bits;
unsigned char* block_bits;
struct a1fs_inode* itable;

/** Command line options. */
typedef struct mkfs_opts {
	/** File system image file path. */
	const char *img_path;
	/** Number of inodes. */
	size_t n_inodes;

	/** Print help and exit. */
	bool help;
	/** Overwrite existing file system. */
	bool force;
	/** Zero out image contents. */
	bool zero;

} mkfs_opts;

static const char *help_str = "\
Usage: %s options image\n\
\n\
Format the image file into a1fs file system. The file must exist and\n\
its size must be a multiple of a1fs block size - %zu bytes.\n\
\n\
Options:\n\
    -i num  number of inodes; required argument\n\
    -h      print help and exit\n\
    -f      force format - overwrite existing a1fs file system\n\
    -z      zero out image contents\n\
";

static void print_help(FILE *f, const char *progname)
{
	fprintf(f, help_str, progname, A1FS_BLOCK_SIZE);
}


static bool parse_args(int argc, char *argv[], mkfs_opts *opts)
{
	char o;
	while ((o = getopt(argc, argv, "i:hfvz")) != -1) {
		switch (o) {
			case 'i': opts->n_inodes = strtoul(optarg, NULL, 10); break;

			case 'h': opts->help  = true; return true;// skip other arguments
			case 'f': opts->force = true; break;
			case 'z': opts->zero  = true; break;

			case '?': return false;
			default : assert(false);
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing image path\n");
		return false;
	}
	opts->img_path = argv[optind];

	if (opts->n_inodes == 0) {
		fprintf(stderr, "Missing or invalid number of inodes\n");
		return false;
	}
	return true;
}


/** Determine if the image has already been formatted into a1fs. */
static bool a1fs_is_present(void *image)
{
	// Check if the image already has an initialized superblock
	if (((a1fs_superblock *)(image))->magic == A1FS_MAGIC) {
		return true;
	}

	return false;
}


/**
 * Format the image into a1fs.
 *
 * NOTE: Must update mtime of the root directory.
 *
 * @param image  pointer to the start of the image.
 * @param size   image size in bytes.
 * @param opts   command line options.
 * @return       true on success;
 *               false on error, e.g. options are invalid for given image size.
 */
static bool mkfs(void *image, size_t size, mkfs_opts *opts)
{
	// Check if file system has already been formatted
	if (a1fs_is_present(image)) {
		fprintf(stderr, "mkfs: file system has already been formatted into a1fs\n");
		return false;
	}

	int blocks_remaining = size / A1FS_BLOCK_SIZE;
	int num_ibm_blocks;
	int num_iblocks;
	int num_dbm_blocks;
	bool num_dblocks_optimized = false;

	int root_inode_index;

	// Decrement blocks remaining and check space
	blocks_remaining -= 1;
	if (blocks_remaining <= 0) {
		fprintf(stderr, "mkfs: insufficient blocks to initialize superblock\n");
		return false;
	}

	// Initialize the superblock and its inode metadata
	sb = (a1fs_superblock *)(image);
	sb->sb_free_inodes_count = opts->n_inodes;
	sb->sb_inodes_count = opts->n_inodes;

	// Calculate number of inode bitmap blocks
	num_ibm_blocks = opts->n_inodes / (A1FS_BLOCK_SIZE * 8) + (opts->n_inodes % (A1FS_BLOCK_SIZE * 8) != 0);

	// Decrement blocks remaining and check space
	blocks_remaining -= num_ibm_blocks;
	if (blocks_remaining <= 0) {
		fprintf(stderr, "mkfs: insufficient blocks to initialize inode bitmap\n");
		return false;
	}

	// Initialize inode bitmap block(s)
	sb->sb_inode_bitmap = 1;
	inode_bits = (unsigned char *)(image + A1FS_BLOCK_SIZE * sb->sb_inode_bitmap);



	// Calculate number of inode blocks
	num_iblocks = opts->n_inodes / sizeof(struct a1fs_inode) + (opts->n_inodes % sizeof(struct a1fs_inode) != 0);

	// Decrement blocks remaining and check space
	blocks_remaining -= num_iblocks;
	if (blocks_remaining <= 0) {
		fprintf(stderr, "mkfs: insufficient blocks to initialize inode table\n");
		return false;
	}



	// Calculate the optimal number of data bitmap blocks
	num_dbm_blocks = 1;
	while (!num_dblocks_optimized) {
		if (!(num_dbm_blocks >= ((blocks_remaining - num_dbm_blocks) / (A1FS_BLOCK_SIZE * 8)) + ((blocks_remaining - num_dbm_blocks) / (A1FS_BLOCK_SIZE * 8) != 0))) {
			num_dbm_blocks += 1;
		} else {
			num_dblocks_optimized = true;
		}
	}

	// Decrement blocks remaining and check space
	blocks_remaining -= num_dbm_blocks;
	if (blocks_remaining <= 0) {
		fprintf(stderr, "mkfs: insufficient blocks to initialize data bitmap\n");
		return false;
	}

	// Initialize data bitmap block(s)
	sb->sb_block_bitmap = 1 + num_ibm_blocks;
	block_bits = (unsigned char *)(image + A1FS_BLOCK_SIZE * sb->sb_block_bitmap);



	// Initialize inode table block(s)
	sb->sb_inode_table = 1 + num_ibm_blocks + num_dbm_blocks;
	itable = (a1fs_inode *)(image + A1FS_BLOCK_SIZE * sb->sb_inode_table);
	

	// Initialize data region
	sb->sb_first_data_block = 1 + num_ibm_blocks + num_dbm_blocks + num_iblocks;

	// Find first available inode bit in inode bitmap
	root_inode_index = get_available_bit(sb, inode_bits, 0, -1);
	if (root_inode_index == -1) {
		fprintf(stderr, "mkfs: could not find empty inode for root directory\n");
		return false;
	}

	// Set corresponding inode bit in inode bitmap
	if (set_bits(sb, inode_bits, root_inode_index, 0, -1, 1) < 0) {
		printf("%d\n", root_inode_index);
		fprintf(stderr, "mkfs: could not set inode bit for root directory\n");
		return false;
	}

	// Create corresponding inode in inode table
	if (!create_inode(itable, root_inode_index, S_IFDIR | 0777)) {
		fprintf(stderr, "mkfs: could not create inode for root directory\n");
		return false;
	}

	// Update root inode metadata
	// time_t s;
    struct timespec curr_time;
    clock_gettime(CLOCK_REALTIME, &curr_time);
	itable[root_inode_index].i_mtime = curr_time;
	itable[root_inode_index].last_used_extent = -1;
	itable[root_inode_index].links = 2;
	itable[root_inode_index].num_entries = 0;
	itable[root_inode_index].size = sizeof(itable[root_inode_index]);


	// Fill in the leftover members of the superblock
	sb->magic = A1FS_MAGIC;
	sb->size = size;
	sb->sb_total_data_blocks = size / A1FS_BLOCK_SIZE - sb->sb_first_data_block;
	sb->sb_free_blocks_count = blocks_remaining;
	sb->sb_used_dirs_count = 1;

	return true;
}


int main(int argc, char *argv[])
{
	mkfs_opts opts = {0};// defaults are all 0
	if (!parse_args(argc, argv, &opts)) {
		// Invalid arguments, print help to stderr
		print_help(stderr, argv[0]);
		return 1;
	}
	if (opts.help) {
		// Help requested, print it to stdout
		print_help(stdout, argv[0]);
		return 0;
	}

	// Map image file into memory
	size_t size;
	void *image = map_file(opts.img_path, A1FS_BLOCK_SIZE, &size);
	if (image == NULL) return 1;

	// Check if overwriting existing file system
	int ret = 1;
	if (!opts.force && a1fs_is_present(image)) {
		fprintf(stderr, "Image already contains a1fs; use -f to overwrite\n");
		goto end;
	}

	if (opts.zero) memset(image, 0, size);
	if (!mkfs(image, size, &opts)) {
		fprintf(stderr, "Failed to format the image\n");
		goto end;
	}

	ret = 0;
end:
	munmap(image, size);
	return ret;
}
