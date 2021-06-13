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

int set_db_extent(a1fs_inode *inode, int extent_index, int extent_start, int extent_count, int new);

int set_dirb_extent(fs_ctx *fs_context, a1fs_inode *inode, int extent_index, int extent_start, int extent_count, int new);

/** 
 * Allocate data blocks.
 * 
 * @param fs_context        pointer to the file system context
 * @param inode_index		...
 * @param data_index        starting index of the extent in the data bitmap
 * @param extent_size       number of blocks in the extent
 * @return                  0 on success, -1 otherwise
 */
int allocate_data_blks(fs_ctx *fs_context, int inode_index, int data_index, int extent_size, int db_type);

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
