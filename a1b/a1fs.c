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
 * CSC369 Assignment 1 - a1fs driver implementation.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

// Using 2.9.x FUSE API
#define FUSE_USE_VERSION 29
#include <fuse.h>

#include "a1fs.h"
#include "a1fs_helper.h"
#include "fs_ctx.h"
#include "options.h"
#include "map.h"
#include "util.h"

//NOTE: All path arguments are absolute paths within the a1fs file system and
// start with a '/' that corresponds to the a1fs root directory.
//
// For example, if a1fs is mounted at "~/my_csc369_repo/a1b/mnt/", the path to a
// file at "~/my_csc369_repo/a1b/mnt/dir/file" (as seen by the OS) will be
// passed to FUSE callbacks as "/dir/file".
//
// Paths to directories (except for the root directory - "/") do not end in a
// trailing '/'. For example, "~/my_csc369_repo/a1b/mnt/dir/" will be passed to
// FUSE callbacks as "/dir".


/**
 * Initialize the file system.
 *
 * Called when the file system is mounted. NOTE: we are not using the FUSE
 * init() callback since it doesn't support returning errors. This function must
 * be called explicitly before fuse_main().
 *
 * @param fs    file system context to initialize.
 * @param opts  command line options.
 * @return      true on success; false on failure.
 */
static bool a1fs_init(fs_ctx *fs, a1fs_opts *opts)
{
	// Nothing to initialize if only printing help
	if (opts->help) return true;

	size_t size;
	void *image = map_file(opts->img_path, A1FS_BLOCK_SIZE, &size);
	if (!image) return false;

	return fs_ctx_init(fs, image, size);
}

/**
 * Cleanup the file system.
 *
 * Called when the file system is unmounted. Must cleanup all the resources
 * created in a1fs_init().
 */
static void a1fs_destroy(void *ctx)
{
	fs_ctx *fs = (fs_ctx*)ctx;
	if (fs->image) {
		munmap(fs->image, fs->size);
		fs_ctx_destroy(fs);
	}
}

/** Get file system context. */
static fs_ctx *get_fs(void)
{
	return (fs_ctx*)fuse_get_context()->private_data;
}


/**
 * Get file system statistics.
 *
 * Implements the statvfs() system call. See "man 2 statvfs" for details.
 * The f_bfree and f_bavail fields should be set to the same value.
 * The f_ffree and f_favail fields should be set to the same value.
 * The following fields can be ignored: f_fsid, f_flag.
 * All remaining fields are required.
 *
 * Errors: none
 *
 * @param path  path to any file in the file system. Can be ignored.
 * @param st    pointer to the struct statvfs that receives the result.
 * @return      0 on success; -errno on error.
 */
static int a1fs_statfs(const char *path, struct statvfs *st)
{
	// Can be ignored
	(void)path;

	// Fetch the current file system context
	fs_ctx *fs = get_fs();

	// Fill the first <sizeof(*st)> bytes with constant byte 0
	// of the memory pointed to by <st>
	if (memset(st, 0, sizeof(*st)) == NULL) {
		return -errno;
	}

	// Populate the statvfs struct <st>
	st->f_bsize   = A1FS_BLOCK_SIZE;					/* Filesystem block size */
	st->f_frsize  = A1FS_BLOCK_SIZE;					/* Fragment size */
	st->f_blocks = fs->sb->size/ A1FS_BLOCK_SIZE;		/* Size of fs in f_frsize units */
	st->f_bfree = fs->sb->sb_free_blocks_count;			/* Number of free blocks */
	st->f_bavail = fs->sb->sb_free_blocks_count;		/* Number of free blocks for unprivileged users */
	st->f_files = fs->sb->sb_inodes_count;				/* Number of inodes */
	st->f_ffree = fs->sb->sb_free_inodes_count;			/* Number of free inodes */
	st->f_favail = fs->sb->sb_free_inodes_count;		/* Number of free inodes for unprivileged users */
	st->f_namemax = A1FS_NAME_MAX;						/* Maximum filename length */

	return 0;
}

/**
 * Get file or directory attributes.
 *
 * Implements the lstat() system call. See "man 2 lstat" for details.
 * The following fields can be ignored: st_dev, st_ino, st_uid, st_gid, st_rdev,
 *                                      st_blksize, st_atim, st_ctim.
 * All remaining fields are required. : st_mode, st_nlink, st_size, st_blocks, st_mtime
 *
 * NOTE: the st_blocks field is measured in 512-byte units (disk sectors);
 *       it should include any metadata blocks that are allocated to the 
 *       inode.
 *
 * NOTE2: the st_mode field must be set correctly for files and directories.
 *
 * Errors:
 *   ENAMETOOLONG  the path or one of its components is too long.
 *   ENOENT        a component of the path does not exist.
 *   ENOTDIR       a component of the path prefix is not a directory.
 *
 * @param path  path to a file or directory.
 * @param st    pointer to the struct stat that receives the result.
 * @return      0 on success; -errno on error;
 */
static int a1fs_getattr(const char *path, struct stat *st)
{
	// Return ENAMETOOLONG if path name is too long
	if (strlen(path) >= A1FS_PATH_MAX) return -ENAMETOOLONG;

	// Return ENOTDIR if path is not absolute
	if(path[0] != '/') {  
        fprintf(stderr, "Not an absolute path\n");  
        return -ENOTDIR;
    }

	// Fetch the current file system context
	fs_ctx *fs = get_fs();

	// Fill the first <sizeof(*st)> bytes with constant byte 0
	// of the memory pointed to by <st>
	if (memset(st, 0, sizeof(*st)) == NULL) {
		return -errno;
	}

	// Get the inode number of the given path
	int curr_inode = get_inode_num(fs, path, 0);
	if (curr_inode < 0) {
		return -errno;
	}

	// Fill in the required fields based on inode information
	st->st_mode = fs->itable[curr_inode].mode;			/* File type and mode */
	st->st_nlink = fs->itable[curr_inode].links;		/* Number of hard links */
	st->st_size = fs->itable[curr_inode].size;			/* Total size, in bytes */
	st->st_blocks = fs->itable[curr_inode].size/512 
		+ sizeof(fs->itable[curr_inode]);				/* Number of 512B blocks allocated */
	st->st_mtim = fs->itable[curr_inode].i_mtime;		/* Time of last modification */
	
	return 0;
}


 /**
 * Read a directory.
 *
 * Implements the readdir() system call. Should call filler(buf, name, NULL, 0)
 * for each directory entry. See fuse.h in libfuse source code for details.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a filler() call failed).
 *
 * @param path    path to the directory.
 * @param buf     buffer that receives the result.
 * @param filler  function that needs to be called for each directory entry.
 *                Pass 0 as offset (4th argument). 3rd argument can be NULL.
 * @param offset  unused.
 * @param fi      unused.
 * @return        0 on success; -errno on error.
 */
 
static int a1fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
	(void)offset;// unused
	(void)fi;// unused

	// Fetch the current file system context
	fs_ctx *fs = get_fs();

	// Return ENOTDIR if path is not absolute
	if(path[0] != '/') {  
        fprintf(stderr, "Not an absolute path\n");  
        return -ENOTDIR;
    }

	// Get the inode number of the given path
	int curr_inode = get_inode_num(fs, path, 0);
	if (curr_inode < 0) {
		return -errno;
	}


	// Find the path's directory entry and call the filler function on all of its named children;
	// TODO: account for the case that we begin using the single indirect block
	// Loop through every used extent in the corresponding directory's inode
	for (int i = 0; i <= (int)fs->itable[curr_inode].last_used_extent; i++){

		// Loop through every block in the current extent
		int start = fs->itable[curr_inode].i_extent[i].start;
		int length = fs->itable[curr_inode].i_extent[i].count;
		for( int j = start; j < start + length; j++) {

			// Loop through every directory entry in the current block
			for(int k = 0; k < A1FS_BLOCK_SIZE; k += sizeof(a1fs_dentry)){

				// Call filler function if a non-empty entry is found
				// TODO: Verify that empty directory entries have initially null-terminated names
				if( ((a1fs_dentry *)(fs->image + A1FS_BLOCK_SIZE*j + k))->name[0] != '\0'){
					filler(buf, ((a1fs_dentry *)(fs->image + A1FS_BLOCK_SIZE*j + k))->name, NULL, 0);
				}
			}
		}
	}
	
	filler(buf, "." , NULL, 0);
	filler(buf, "..", NULL, 0);
	return 0;
}





/**
 * Create a directory.
 *
 * Implements the mkdir() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the directory to create.
 * @param mode  file mode bits.
 * @return      0 on success; -errno on error.
 */
static int a1fs_mkdir(const char *path, mode_t mode)
{
	mode = mode | S_IFDIR;
	fs_ctx *fs = get_fs();

	// The new directory's inode index
	int newdir_inode_index;

	// Find first available inode bit in inode bitmap
	newdir_inode_index = get_available_bit(fs->sb, fs->inode_bits, 0, -1);
	if (newdir_inode_index == -1) {
		fprintf(stderr, "a1fs_mkdir: could not find empty inode for new directory\n");
		return -ENOSPC;
	}

	// Set corresponding inode bit in inode bitmap
	if (set_bits(fs->sb, fs->inode_bits, newdir_inode_index, 0, -1) < 0) {
		fprintf(stderr, "a1fs_mkdir: could not set inode bit for new directory\n");
		return -errno;
	}

	// Create corresponding inode in inode table
	create_inode(fs->itable, newdir_inode_index, mode);

	// Get name of the new directory
	char *new_dir_name = strchr(path, '/') + 1;

	// Get the parent's inode number
	int parent_inode_num;
	if (new_dir_name == NULL) {
		parent_inode_num = 0;
	} else {
		// Get the inode number of the parent directory
		// Note that this should return 0 if the path is similar to '/dir'
		parent_inode_num = get_inode_num(fs, path, 1);
		if (parent_inode_num < 0) {
			fprintf(stderr, "a1fs_mkdir: parent inode number could not be retrieved\n");
			return -ENOENT;
		}
	}


	// Add directory entry to the parent directory
	if (add_dentry(fs, parent_inode_num, newdir_inode_index, new_dir_name) < 0) {
		fprintf(stderr, "a1fs_mkdir: failed to add directory entry to parent inode\n");
		return -errno;
	}

	return 0;
}

/**
 * Remove a directory.
 *
 * Implements the rmdir() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOTEMPTY  the directory is not empty.
 *
 * @param path  path to the directory to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_rmdir(const char *path)
{
	fs_ctx *fs = get_fs();

	// The inode index of the directory to be removed
	int ino_to_rm = get_inode_num(fs, path, 0);

	// Check if directory has contents
	if (fs->itable[ino_to_rm].num_entries > 0) {
		return -ENOTEMPTY;
	}

	// Reset all meta data of the inode
	struct timespec curr_time;
    clock_gettime(CLOCK_REALTIME, &curr_time);
	fs->itable[ino_to_rm].i_mtime = curr_time;
	fs->itable[ino_to_rm].links = 0;
	fs->itable[ino_to_rm].size = 0;
	fs->itable[ino_to_rm].last_used_extent = -1;
	fs->itable[ino_to_rm].last_used_indirect = -1;
	fs->itable[ino_to_rm].num_entries = 0;

	// Flip corresponding inode bit in inode bitmap
	if (check_bit_usage(fs->inode_bits, ino_to_rm)) {
		if (set_bits(fs->sb, fs->inode_bits, ino_to_rm, 0, -1) < 0) {
			fprintf(stderr, "a1fs_rmdir: could not flip inode bit\n");
			return -errno;
		}
	} else {
		fprintf(stderr, "a1fs_rmdir: supposedly existing inode does not exist per inode bitmap\n");
		return -errno;
	}

	// Get name of the directory to be removed
	char *dir_to_rm_name = strchr(path, '/') + 1;

	// Get the parent's inode number
	int parent_inode_num;
	if (dir_to_rm_name == NULL) {
		parent_inode_num = 0;
	} else {
		// Get the inode number of the parent directory
		// Note that this should return 0 if the path is similar to '/dir'
		parent_inode_num = get_inode_num(fs, path, 1);
		if (parent_inode_num < 0) {
			fprintf(stderr, "a1fs_rmdir: parent inode number could not be retrieved\n");
			return -errno;
		}
	}

	// Reset the directory entry of the directory to be removed
	int first_data_block = fs->sb->sb_first_data_block;
	// Loop through every used extent in the corresponding directory's inode. 
	for (int i = 0; i <= (int)fs->itable[parent_inode_num].last_used_extent; i++){
		// Loop through every block in the current extent
		int start = fs->itable[parent_inode_num].i_extent[i].start;
		int length = fs->itable[parent_inode_num].i_extent[i].count;
		for( int j = start; j < start + length; j++) {
			// Loop through every directory entry in the current block
			for(int k = 0; k < A1FS_BLOCK_SIZE; k += sizeof(a1fs_dentry)){
				if( ((a1fs_dentry *)(fs->image + A1FS_BLOCK_SIZE*(j+first_data_block) + k))->ino == ino_to_rm){
					((a1fs_dentry *)(fs->image + A1FS_BLOCK_SIZE*(j+first_data_block) + k))->ino = -1;
					((a1fs_dentry *)(fs->image + A1FS_BLOCK_SIZE*(j+first_data_block) + k))->name[0] = '\0';

					// Update metadata
					struct timespec curr_time;
					clock_gettime(CLOCK_REALTIME, &curr_time);
					fs->itable[parent_inode_num].i_mtime = curr_time;
					fs->itable[parent_inode_num].num_entries -= 1;
					fs->itable[parent_inode_num].links -= 1;
					fs->sb->sb_used_dirs_count -= 1;

					return 0;
				}
			}
		}	
	}

	return -1;
}

/**
 * Create a file.
 *
 * Implements the open()/creat() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to create.
 * @param mode  file mode bits.
 * @param fi    unused.
 * @return      0 on success; -errno on error.
 */
static int a1fs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	(void)fi;// unused
	assert(S_ISREG(mode));
	fs_ctx *fs = get_fs();

	// The new file's inode index
	int new_inode_index;

	// Find first available inode bit in inode bitmap
	new_inode_index = get_available_bit(fs->sb, fs->inode_bits, 0, -1);
	if (new_inode_index == -1) {
		fprintf(stderr, "a1fs_create: could not find empty inode for new file\n");
		return -ENOSPC;
	}

	// Set corresponding inode bit in inode bitmap
	if (set_bits(fs->sb, fs->inode_bits, new_inode_index, 0, -1) < 0) {
		fprintf(stderr, "a1fs_create: could not set inode bit for new file\n");
		return -errno;
	}

	// Create corresponding inode in inode table
	create_inode(fs->itable, new_inode_index, mode);

	// Get name of the file to be created
	char *new_file_name = strchr(path, '/') + 1;

	// Get the parent's inode number
	int parent_inode_num;
	if (new_file_name == NULL) {
		parent_inode_num = 0;
	} else {
		// Get the inode number of the parent directory
		// Note that this should return 0 if the path is similar to '/dir'
		parent_inode_num = get_inode_num(fs, path, 1);
		if (parent_inode_num < 0) {
			fprintf(stderr, "a1fs_rmdir: parent inode number could not be retrieved\n");
			return -errno;
		}
	}

	// Add directory entry to the parent directory
	if (add_dentry(fs, parent_inode_num, new_inode_index, new_file_name) < 0) {
		fprintf(stderr, "a1fs_mkdir: failed to add directory entry to parent inode\n");
		return -errno;
	}

	return 0;
}

/**
 * Remove a file.
 *
 * Implements the unlink() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path  path to the file to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_unlink(const char *path)
{
	fs_ctx *fs = get_fs();

	// The inode index of the file to be removed
	int ino_to_rm = get_inode_num(fs, path, 0);
	
	// Reset all of the data bits in the data bitmap if the file to be removed used any
	if (fs->itable[ino_to_rm].size != 0 || fs->itable[ino_to_rm].last_used_extent != -1) {
		for (int i = 0; i <= (int)fs->itable[ino_to_rm].last_used_extent; i++) {
			// Flip corresponding data bit(s) in data bitmap
			if (check_bit_usage(fs->block_bits, s->itable[ino_to_rm].i_extent[i].start)) {
				if (set_bits(fs->sb, fs->block_bits, fs->itable[ino_to_rm].i_extent[i].start, 1, fs->itable[ino_to_rm].i_extent[i].count); < 0) {
					fprintf(stderr, "a1fs_unlink: could not flip data bit(s)\n");
					return -errno;
				}
			} else {
				fprintf(stderr, "a1fs_unlink: supposedly existing data bit does not exist per data bitmap\n");
				return -errno;
			}
		}
	}

	// Reset all meta data of the inode
	struct timespec curr_time;
    clock_gettime(CLOCK_REALTIME, &curr_time);
	fs->itable[ino_to_rm].i_mtime = curr_time;
	fs->itable[ino_to_rm].links = 0;
	fs->itable[ino_to_rm].size = 0;
	fs->itable[ino_to_rm].last_used_extent = -1;
	fs->itable[ino_to_rm].last_used_indirect = -1;
	fs->itable[ino_to_rm].num_entries = 0;

	// Flip corresponding inode bit in inode bitmap
	if (check_bit_usage(fs->inode_bits, ino_to_rm)) {
		if (set_bits(fs->sb, fs->inode_bits, ino_to_rm, 0, -1) < 0) {
			fprintf(stderr, "a1fs_unlink: could not flip inode bit\n");
			return -errno;
		}
	} else {
		fprintf(stderr, "a1fs_unlink: supposedly existing inode does not exist per inode bitmap\n");
		return -errno;
	}

	// Get name of the file to be removed
	char *file_to_rm_name = strchr(path, '/') + 1;

	// Get the parent's inode number
	int parent_inode_num;
	if (file_to_rm_name == NULL) {
		parent_inode_num = 0;
	} else {
		// Get the inode number of the parent directory
		// Note that this should return 0 if the path is similar to '/dir'
		parent_inode_num = get_inode_num(fs, path, 1);
		if (parent_inode_num < 0) {
			fprintf(stderr, "a1fs_unlink: parent inode number could not be retrieved\n");
			return -errno;
		}
	}

	// Reset the directory entry of the directory to be removed
	int first_data_block = fs->sb->sb_first_data_block;
	// Loop through every used extent in the corresponding directory's inode. 
	for (int i = 0; i <= (int)fs->itable[parent_inode_num].last_used_extent; i++){
		// Loop through every block in the current extent
		int start = fs->itable[parent_inode_num].i_extent[i].start;
		int length = fs->itable[parent_inode_num].i_extent[i].count;
		for( int j = start; j < start + length; j++) {
			// Loop through every directory entry in the current block
			for(int k = 0; k < A1FS_BLOCK_SIZE; k += sizeof(a1fs_dentry)){
				if( ((a1fs_dentry *)(fs->image + A1FS_BLOCK_SIZE*(j+first_data_block) + k))->ino == ino_to_rm){
					((a1fs_dentry *)(fs->image + A1FS_BLOCK_SIZE*(j+first_data_block) + k))->ino = -1;
					((a1fs_dentry *)(fs->image + A1FS_BLOCK_SIZE*(j+first_data_block) + k))->name[0] = '\0';

					// Update metadata
					struct timespec curr_time;
					clock_gettime(CLOCK_REALTIME, &curr_time);
					fs->itable[parent_inode_num].i_mtime = curr_time;
					fs->itable[parent_inode_num].num_entries -= 1;
					fs->itable[parent_inode_num].links -= 1;

					return 0;
				}
			}
		}	
	}

	return -1;


}


/**
 * Change the modification time of a file or directory.
 *
 * Implements the utimensat() system call. See "man 2 utimensat" for details.
 *
 * NOTE: You only need to implement the setting of modification time (mtime).
 *       Timestamp modifications are not recursive. 
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists.
 *
 * Errors: none
 *
 * @param path   path to the file or directory.
 * @param times  timestamps array. See "man 2 utimensat" for details.
 * @return       0 on success; -errno on failure.
 */
static int a1fs_utimens(const char *path, const struct timespec times[2])
{
	fs_ctx *fs = get_fs();

	//TODO: update the modification timestamp (mtime) in the inode for given
	// path with either the time passed as argument or the current time,
	// according to the utimensat man page
	(void)path;
	(void)times;
	(void)fs;
	return -ENOSYS;
}

/**
 * Change the size of a file.
 *
 * Implements the truncate() system call. Supports both extending and shrinking.
 * If the file is extended, the new uninitialized range at the end must be
 * filled with zeros.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to set the size.
 * @param size  new file size in bytes.
 * @return      0 on success; -errno on error.
 */
static int a1fs_truncate(const char *path, off_t size)
{
	fs_ctx *fs = get_fs();

	//TODO: set new file size, possibly "zeroing out" the uninitialized range

	// how do we do this? 
	//first we should grab file size, then compare with the size we want
	// if size we want is bigger than file size, then we will need to determine how big of an extent
	// to give and allocate it as such
	// if size we want is smaller than current size, then we will need to go through and remove 
	// extents as required
	// every step we shall update total size of file, until we have reached the destination (0)

	// get the inode index of the file we want, and grab its size
	int cur_inode = get_inode_num(fs, path, 0);
	int cur_size = fs->itable[cur_inode].size;
	int firstdata = fs->sb->sb_first_data_block*A1FS_BLOCK_SIZE;
	//if wanted size is same as target size, return no changes (still success)
	if (cur_size == size){
		return 0;
	}
	// if the target size is greater than our current size...
	int diff = size - cur_size;
	if (diff > 0){
		// then we want to 
		//	a. fill up our last extent to the max
		// 	b. make another extent if there are leftovers
		int last_extent = fs->itable[cur_inode].last_used_extent;
		

		// if the last extent isn't filled up...
		if ((int)fs->itable[cur_inode].i_extent[last_extent].size != (int)fs->itable[cur_inode].i_extent[last_extent].count * A1FS_BLOCK_SIZE) {
			for (int i = 0; i < (int)fs->itable[cur_inode].i_extent[last_extent].size; i++){
				//set all remaining in extent to zero 
				// SUSPICIOUS CODE
				int offset = A1FS_BLOCK_SIZE*(fs->sb->sb_first_data_block) + (int)fs->itable[cur_inode].i_extent[last_extent].start*A1FS_BLOCK_SIZE;
				unsigned char* bit = (unsigned char*)(fs->image + offset);
				bit[i/8] = ~(1 << (i%8));
				//increment remaining difference each time, as well as the size of the extent
				diff--;
				fs->itable[cur_inode].i_extent[last_extent].size = fs->itable[cur_inode].i_extent[last_extent].size + 1;
			}
		} 
		//once that's done, we know that we must check for leftovers, if leftover is still greater than zero, figure
		// out how big of an extent we need and allocate, else do nothing
		if (diff >0) {
			//this hopefully yields a whole number, but will be how many blocks we need
			int check = diff / (int)A1FS_BLOCK_SIZE;
			// then we've found how big of an extent we need, so we call our allocation algorithm
			if (allocate_data_blks(fs, cur_inode, fs->sb->sb_first_data_block, check, 0) < 0) {
				return -1;
			}
			// when allocation is complete, increment the index of last extent in inode
			fs->itable[cur_inode].last_used_extent += 1;
			last_extent ++;
			//then loop through our newly created extent blocks and set all to zero
			//for each block in the last extent...
			for (int i = 0; i < (int)fs->itable[cur_inode].i_extent[last_extent].count; i++){
				//for each bit in the block
				for (int j = 0; j < A1FS_BLOCK_SIZE; j++){
					//set each increment to zero
					// SUSPICIOUS don't need to?
					// clear bit
					int offset = A1FS_BLOCK_SIZE*((int)fs->sb->sb_first_data_block) + i*A1FS_BLOCK_SIZE;
					unsigned char* bit = (unsigned char*)(fs->image + offset);
					bit[j/8] = ~(1 << (j%8));
					diff--;
					fs->itable[cur_inode].i_extent[last_extent].size = fs->itable[cur_inode].i_extent[last_extent].size + 1;
				}
			}
			
		}
	}else {
		//its smaller than we expected, so we need to chop off some extents so it'll fit...
		// flip diff to positive so its easier to deal with
		diff = diff * -1;
		// we want to go through the extents, and begin removing until diff is satisfied
		// first check if we need to deal with indirect block
		if (fs->itable[cur_inode].last_used_extent == 12){
			//since we need to deal with the indirect, lets loop through all of its elements backwards
			for(int i = fs->itable[cur_inode].last_used_indirect*sizeof(a1fs_extent); i > 0; i -= sizeof(a1fs_extent) ){
				int start = fs->itable[cur_inode].i_extent[12].start;
				a1fs_extent* cur_extent = (a1fs_extent*)(fs->image + start*A1FS_BLOCK_SIZE +firstdata + i);
				// if the remaining difference is less than the size...
				if (diff < (int)cur_extent->size){
					//since it is less, then we can just shave if off from the extent...
					//first check how many blocks there are 
					int blocks = diff/A1FS_BLOCK_SIZE;
					int bits = diff % A1FS_BLOCK_SIZE;
					fs->itable[cur_inode].size -= diff;
					//this should be able to take diff to 0, ut not a negative number... hopefully
					diff -= cur_extent->size;
					cur_extent->count -= blocks;
					cur_extent->size -= bits;
					//change diff value to zero and break out;
					break;
				}else {
					//otherwise, its greater than the current extent, so delete the current extent and increment
					fs->itable[cur_inode].size -= cur_extent->size;
					cur_extent->start = -1;
					diff -= cur_extent->size;
					cur_extent->size = 0;
					cur_extent->count = -1; 
				}
			}
		}
		//get running index of last used extent, at the end of previous operation, diff is not guaranteed to be zero, so we 
		// keep going
		int index = fs->itable[cur_inode].last_used_extent;
		while (diff!= 0){
			// 
			a1fs_extent* cur_extent = (a1fs_extent*)(&fs->itable[cur_inode].i_extent[index]);
			// if diff is less than current extent, then we chop off some of curr extent and then stop
			if ((int)cur_extent->size > diff){
				int blocks = diff/A1FS_BLOCK_SIZE;
				int bits = diff % A1FS_BLOCK_SIZE;
				fs->itable[cur_inode].size -= diff;
				//this should be able to take diff to 0, ut not a negative number... hopefully
				diff -= cur_extent->size;
				cur_extent->count -= blocks;
				cur_extent->size -= bits;
				break;
			}else{
				//diff is either greater or equal to the following extent, so we delete it
				fs->itable[cur_inode].size -= cur_extent->size;
				cur_extent->start = -1;
				diff -= cur_extent->size;
				cur_extent->size = 0;
				cur_extent->count = -1; 
			}
		}

	}
	// the algorithm is thus complete
	return 0;
}


/**
 * Read data from a file.
 *
 * Implements the pread() system call. Must return exactly the number of bytes
 * requested except on EOF (end of file). Reads from file ranges that have not
 * been written to must return ranges filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path    path to the file to read from.
 * @param buf     pointer to the buffer that receives the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to read from.
 * @param fi      unused.
 * @return        number of bytes read on success; 0 if offset is beyond EOF;
 *                -errno on error.
 */
static int a1fs_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	//TODO: read data from the file at given offset into the buffer
	(void)path;
	(void)buf;
	(void)size;
	(void)offset;
	(void)fs;
	return -ENOSYS;
}

/**
 * Write data to a file.
 *
 * Implements the pwrite() system call. Must return exactly the number of bytes
 * requested except on error. If the offset is beyond EOF (end of file), the
 * file must be extended. If the write creates a "hole" of uninitialized data,
 * the new uninitialized range must filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *   ENOSPC  too many extents (a1fs only needs to support 512 extents per file)
 *
 * @param path    path to the file to write to.
 * @param buf     pointer to the buffer containing the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to write to.
 * @param fi      unused.
 * @return        number of bytes written on success; -errno on error.
 */
static int a1fs_write(const char *path, const char *buf, size_t size,
                      off_t offset, struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	//TODO: write data from the buffer into the file at given offset, possibly
	// "zeroing out" the uninitialized range
	(void)path;
	(void)buf;
	(void)size;
	(void)offset;
	(void)fs;
	return -ENOSYS;
}


static struct fuse_operations a1fs_ops = {
	.destroy  = a1fs_destroy,
	.statfs   = a1fs_statfs,
	.getattr  = a1fs_getattr,
	.readdir  = a1fs_readdir,
	.mkdir    = a1fs_mkdir,
	.rmdir    = a1fs_rmdir,
	.create   = a1fs_create,
	.unlink   = a1fs_unlink,
	.utimens  = a1fs_utimens,
	.truncate = a1fs_truncate,
	.read     = a1fs_read,
	.write    = a1fs_write,
};

int main(int argc, char *argv[])
{
	a1fs_opts opts = {0};// defaults are all 0
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	if (!a1fs_opt_parse(&args, &opts)) return 1;

	fs_ctx fs = {0};
	if (!a1fs_init(&fs, &opts)) {
		fprintf(stderr, "Failed to mount the file system\n");
		return 1;
	}

	return fuse_main(args.argc, args.argv, &a1fs_ops, &fs);
}
