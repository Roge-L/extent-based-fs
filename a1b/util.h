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
 * CSC369 Assignment 1 - Miscellaneous utility functions.
 */

#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <errno.h>

#include "a1fs.h"
#include "fs_ctx.h"


/** Check if x is a power of 2. */
static inline bool is_powerof2(size_t x)
{
	return (x & (x - 1)) == 0;
}

/** Check if x is a multiple of alignment (which must be a power of 2). */
static inline bool is_aligned(size_t x, size_t alignment)
{
	assert(is_powerof2(alignment));
	return (x & (alignment - 1)) == 0;
}

/** Align x up to a multiple of alignment (which must be a power of 2). */
static inline size_t align_up(size_t x, size_t alignment)
{
	assert(is_powerof2(alignment));
	return (x + alignment - 1) & (~alignment + 1);
}

/** Check if bit is in use given its index and the bitmap */
static inline bool check_bit_usage(unsigned char* bitmap, int index)
{
	// Get specific byte containing the bit
	int byte = index / 8;

	// Get number of bits to left-shift
	int bit = index % 8;

	// Check if bit in use
	if (bitmap[byte] & (1 << bit)) {
		return true;
	} else {
		return false;
	}
}
//CONSIDERATION: do we pass in ** bitmap or just *bitmap?
/** Return index of the first available bit */
static inline int get_available_bit(a1fs_superblock *sb, unsigned char *bitmap, int bm_type, int extent_size)
{

	// Find available inode
	if (bm_type == 0) {
		// Return -1 if no free inodes
		if (sb->sb_free_inodes_count == 0) {
			return -1;
		}

		// Loop until first available inode is found
		// and return the corresponding index
		for (int i = 0; i < sb->sb_inodes_count; i++) {
			if (!check_bit_usage(bitmap, i)) {
				return i;
			}
		}


	// Find available data block
	} else if (bm_type == 1) {
		// Calculate the maximum possible index for data
		int total_blocks = (sb->size)/A1FS_BLOCK_SIZE;
		int max_index = total_blocks - 1;

		// Eventually becomes the index of the large-enough and first-fit data bit
		int avail_db_index = sb->sb_first_empty_db;

		// The current data block being observed
		int i = avail_db_index;

		// The current number of contiguous and free data blocks
		int count = 0;
		while ((count != extent_size) && (i < max_index)) {

			// Reset count if bit is occupied and increment count if bit is free
			if (check_bit_usage(bitmap, i)) {
				count = 0;
				i += 1;
				avail_db_index = i;
			} else {
				i += 1;
				count += 1;
			}
		}

		if (i == max_index && extent_size != 1) {
			// TODO: break up the extent into smaller size
			return -ENOENT;
		} else {
			return avail_db_index;
		}
	}


	return -1;
}

/** Set the bits in the bitmap; 0 for inode bitmap, 1 for data bitmap */
static inline int set_bits(a1fs_superblock *sb, unsigned char *bitmap, int index, int bm_type, int extent_size) {

	if (bm_type == 0) {
		// Get specific byte containing the inode
		int byte = index / 8;

		// Get number of bits to left-shift
		int bit = index % 8;

		// Set corresponding inode bit
		// https://www.codesdope.com/blog/article/set-toggle-and-clear-a-bit-in-c/
		bitmap[byte] ^= (1 << bit);

		// Update superblock
		sb->sb_free_inodes_count -= 1;

		return 0;
	} else if (bm_type == 1) {
		for (int i = index; i < extent_size + index; i++) {
			// Get specific byte containing the data block
			int byte = i / 8;

			// Get number of bits to right-shift
			int bit = i % 8;

			// Set corresponding data bit(s)
			// https://www.codesdope.com/blog/article/set-toggle-and-clear-a-bit-in-c/
			bitmap[byte] ^= (1 << bit);

			// Update superblock
			sb->sb_free_blocks_count -= 1;
		}

		return 0;
	}

    return -1;
}

/** Create an inode in the inode table */
static inline bool create_inode(a1fs_inode* itable, int index, mode_t mode) {
	// Set up the inode
    itable[index].mode = mode;
	itable[index].links = 0;
    itable[index].size = 0;
    struct timespec curr_time;
    clock_gettime(CLOCK_REALTIME, &curr_time);
	itable[index].i_mtime = curr_time;
    itable[index].last_used_extent = -1;
	itable[index].num_entries = 0;

	return true;
}
