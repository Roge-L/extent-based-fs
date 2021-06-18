#pragma once

#include "a1fs.h"
#include "fs_ctx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <error.h>
#include <time.h>
#include "util.h"

/** 
 * Return the inode number of the given path or -1 if not found.
 * 
 * @param fs_context  pointer to the file system context
 * @param path		  path to file or directory
 * @param tog         function toggle, 1 to find parent and 0 for default
 * @return 			  non-negative number on success, negative number otherwise
*/
int get_inode_num(fs_ctx *fs_context, const char* path, int tog);

/** 
*  Return inode number of file or directory under the directory 
*  given by the inode number and the name <token>  
*  or -1 if not found. 
*
*  @param fs_context  pointer to the file system context
*  @param par_inode  the inode number of the parent directory
*  @param token		  the name of the corresponding directory
*/ 
int inode_lookup(fs_ctx *fs_context, int par_inode, char* token);

/** 
 * Initialize newly created or added-on data block(s)
 * 
 * @param inode             pointer to the inode corresponding to the data block(s)
 * @param extent_index      the index to the corresponding extent in the inode
 * @param extent_start      the index of the new data block (only if it is new)
 * @param extent_count      the number of blocks to be initialized/added to the extent
 * @param new               1 for new data block extent, 0 for add-on to existing extent
*/
int set_db_extent(a1fs_inode *inode, int extent_index, int extent_start, int extent_count, int new);

/** 
 * Initialize a newly created or added-on directory block.
 * 
 * ==== Preconditions ====
 * - The directory entry block is not full
 * 
 * @param fs_context        pointer to the file system context
 * @param inode             pointer to the inode corresponding to the directory block
 * @param extent_index      the index to the corresponding extent in the inode
 * @param extent_start      the index of the new directory block (only if it is new)
 * @param extent_count      the number of blocks to be initialized/added to the extent
 * @param new               1 for new directory block extent, 0 for add-on to existing directory block
*/
int set_dirb_extent(fs_ctx *fs_context, a1fs_inode *inode, int extent_index, int extent_start, int extent_count, int new);

/** 
 * Allocate data blocks.
 * 
 * ===== Preconditions =====
 * - <data_index> must point to a large enough and first-fit data block
 * 
 * @param fs_context        pointer to the file system context
 * @param inode_index		the inode number of the inode that is requesting the data block allocation
 * @param data_index        starting index of the extent in the data bitmap
 * @param extent_size       number of blocks in the extent
 * @return                  the index of the initialized data block(s), -1 on failure
 */
int allocate_data_blks(fs_ctx *fs_context, int inode_index, int data_index, int extent_size, int db_type);

/**
 * Create a directory entry block.
 * 
 * @param fs_context            pointer to the file system context
 * @param directory_inode_num   the inode number of the directory that is requesting the directory block
 * @return                      0 on success, -1 otherwise
 */
int make_dentry_block(fs_ctx *fs_context, int directory_inode_num);

/** 
 * Add directory entry to a given directory.
 * 
 * @param fs_context  			pointer to the file system context
 * @param directory_inode_num	inode number of the directory
 * @param dentry_inode_num		inode number of the directory entry
 * @param name					null-terminated file name of the directory entry
 * @return 						0 on success, -1 otherwise
*/
int add_dentry(fs_ctx *fs_context, int directory_inode_num, int dentry_inode_num, char* name);

/** 
 * Create <num_blocks> data blocks possibly across multiple extents.
 * 
 * @param fs_context            pointer to the file system context
 * @param inode_index           inode number of the inode to make data blocks for
 * @param num_blocks            the number of blocks to create
 * @return                      the total number of blocks successfully created, -1 otherwise
*/
int make_data_blocks(fs_ctx *fs_context, int inode_index, int num_blocks);

/** 
 * Handle general extension cases of truncate where the file is empty
 * or when the very last data block of a file is filled.
 * 
 * @param fs                    pointer to the file system context
 * @param cur_inode             inode number of the inode to truncate
 * @param size                  the size to truncate in bytes
 * @return                      0 on success, -1 otherwise
*/
int truncate_helper(fs_ctx *fs, int cur_inode, off_t size);

/** 
 * Zero out data blocks of some given extent.
 */
int zero_out_blocks(fs_ctx *fs, int cur_inode, int extent_index, int start, int length, int *blocks_left_to_zero, int leftover_bytes);

/**
 * Finds the extent that contains the corresponding offset byte specified
 * by a read or a write.
 */
int find_offset_extent(fs_ctx *fs, int inode_num, int *cur_extent_index, int *cur_indirect_extent_index, int *remainingoffset);