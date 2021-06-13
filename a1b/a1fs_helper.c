#include "a1fs_helper.h"

int get_inode_num(fs_ctx *fs_context, const char* path, int tog) {
	// Create copy of the path string before mutation  
	char temp_path[strlen(path) + 1];
	strncpy(temp_path, path, strlen(path));
	temp_path[strlen(path)] = '\0';

	// Initialize the first token and the corresponding inode 
	char *token = strtok(temp_path, "/");
	int curr_inode = 0;
	int prev = 0;
	// <token> is NULL -- which is 0 -- if no tokens found  
	while(token) {

		// Keep track of parent inode
		if (curr_inode != -1){
			prev = curr_inode;
		}

		// Update <curr_inode> to be inode of the file or   
        // directory of the current directory, or   
        // possibly -1.
		curr_inode = inode_lookup(fs_context, prev, token);

		// Return ENOENT when path component is invalid
		if (curr_inode == -1) {
			return -1;
		}

		// Next token
		token = strtok(NULL, "/");
	}

	if (tog == 1) {
		return prev;
	}
	return curr_inode;
}

int inode_lookup(fs_ctx *fs_context, int par_inode, char* token){

	fs_context->itable[par_inode].last_used_extent += 1;

	// Loop through every used extent in the corresponding directory's inode
	for (int i = 0; i <= (int)fs_context->itable[par_inode].last_used_extent; i++){

		// Loop through every block in the current extent
		int start = fs_context->itable[par_inode].i_extent[i].start;
		int length = fs_context->itable[par_inode].i_extent[i].count;
		for( int j = start; j < start + length; j++) {

			// Loop through every directory entry in the current block
			for(int k = 0; k < A1FS_BLOCK_SIZE; k += sizeof(a1fs_dentry)){

				// Return inode number if file or directory name is found
				if( strcmp( ((a1fs_dentry *)(fs_context->image + A1FS_BLOCK_SIZE*j + k))->name, token) == 0) {
					return ((a1fs_dentry *)(fs_context->image + A1FS_BLOCK_SIZE*j + k))->ino;
				}	
			}
		}
	}
	return -1;
}

int set_db_extent(a1fs_inode *inode, int extent_index, int extent_start, int extent_count, int new) {
	if (new) {
		inode->i_extent[extent_index].start = extent_start;
		inode->i_extent[extent_index].count = extent_count;
		inode->i_extent[extent_index].size = extent_count;
	} else {
		inode->i_extent[extent_index].start = extent_start;
		inode->i_extent[extent_index].count += extent_count;
		inode->i_extent[extent_index].size += extent_count;
	}

	struct timespec curr_time;
	clock_gettime(CLOCK_REALTIME, &curr_time);
	inode->i_mtime = curr_time;

	return extent_start;
}

int set_dirb_extent(fs_ctx *fs_context, a1fs_inode *inode, int extent_index, int extent_start, int extent_count, int new) {
	if (new) {
		inode->i_extent[extent_index].start = extent_start;
		inode->i_extent[extent_index].count = extent_count;
		inode->i_extent[extent_index].size = extent_count;
	} else {
		inode->i_extent[extent_index].start = extent_start;
		inode->i_extent[extent_index].count += extent_count;
		inode->i_extent[extent_index].size += extent_count;
	}

	for (int i = 0; i < A1FS_BLOCK_SIZE; i += sizeof(a1fs_dentry)) {
		a1fs_dentry *dentry = (a1fs_dentry *)(fs_context->image + fs_context->sb->sb_first_data_block + extent_start);
		dentry->ino = -1;
	}

	struct timespec curr_time;
	clock_gettime(CLOCK_REALTIME, &curr_time);
	inode->i_mtime = curr_time;

	return extent_start;
}

// Precondition: <data_index> must point to a large enough and first-fit data block
// Returns the index of the initialized data block(s)
int allocate_data_blks(fs_ctx *fs_context, int inode_index, int data_index, int extent_size, int db_type) {

	// Walkthrough of the implementation
	// 1. The inode has no allocated data blocks -- so we create the first extent
	//        a. ALLOCATING DATA BLOCKS
	//		      - get inode table
	//			  - get inode
	//			  - init an extent in inode's i_extent with start <data_index> (already large enough and first-fit)
	//			  - update metadata
	//			  - return extent.start
	//		  b. ALLOCATING DIRECTORY BLOCK
	//			  - get inode table
	//			  - get inode
	//			  - init an extent in inode's i_extent with start <data_index> (already large enough and first-fit)
	//			  - init dentry structs in block at <data_index>
	//			      - set inos to -1 basically
	//			  - return extent.start
	// 2. The inode has allocated data blocks
	//		  - Can we add to the end of the last extent?
	//		      A. yes
			//        a. ALLOCATING DATA BLOCKS
			//		      - get inode table
			//			  - get inode
			//			  - record new data_index
			//			  - increase count of last extent
			//			  - update metadata
			//			  - return new data_index
			//		  b. ALLOCATING DIRECTORY BLOCK
			//			  - get inode table
			//			  - get inode
			//			  - record new data_index
			//			  - increase count of last extent
			//			  - init dentry structs in block at new data_index
			//			      - set inos to -1 basically
			//			  - return new data_index
	//			  B. no
			//        a. ALLOCATING DATA BLOCKS
			//		      - get inode table
			//			  - get inode
			//			  - init an extent in inode's i_extent with start <data_index> (already large enough and first-fit)
			//			  - update metadata
			//			  - return extent.start
			//		  b. ALLOCATING DIRECTORY BLOCK
			//			  - get inode table
			//			  - get inode
			//			  - init an extent in inode's i_extent with start <data_index> (already large enough and first-fit)
			//			  - init dentry structs in block at <data_index>
			//			      - set inos to -1 basically
			//			  - return extent.start

	if ((db_type != 0) || (db_type != 1)) {
		return -1;
	}
	a1fs_inode *inode_table = fs_context->itable;
	a1fs_inode *inode = &(inode_table[inode_index]);

	// Case that the inode has no allocated data blocks -- so we create the first extent
	if (inode_table[inode_index].last_used_extent == -1) {

		// ALLOCATING DATA BLOCKS
		if (db_type == 0) {
			(*inode).last_used_extent = 0;
			return set_db_extent(inode, 0, data_index, extent_size, 1);

		// ALLOCATING DIRECTORY BLOCK
		} else {
			(*inode).last_used_extent = 0;
			return set_dirb_extent(fs_context, inode, 0, data_index, extent_size, 1);
		}



	// Case that the inode has previously allocated data blocks
	} else {
		// Can we add to the end of the last extent?
		int large_enough = 1;
		int index_of_last_used_extent = fs_context->itable[inode_index].last_used_extent;
		a1fs_extent last_used_extent = fs_context->itable[inode_index].i_extent[index_of_last_used_extent];

		// This is the index of the data block right after the last extent
		int index_db_after_last_used_extent = last_used_extent.start + last_used_extent.count;

		// Check the <extent_size> blocks after the last extent
		for (int i = index_db_after_last_used_extent; i < extent_size + index_db_after_last_used_extent; i++) {
			// Get specific byte containing the data block
			int byte = i / 8;

			// Get number of bits to left-shift
			int bit = i % 8;

			// Indicate if space after last extent is not large enough or continue with the loop
			if (fs_context->block_bits[byte] & (1 << bit)) {
				large_enough = 0;
				break;
			} else {
				continue;
			}
		}



		// Case that yes, we can add to the end of the last extent
		if (large_enough) {

			// ALLOCATING DATA BLOCKS
			if (db_type == 0) {
				return set_db_extent(inode, index_of_last_used_extent, index_db_after_last_used_extent, extent_size, 0);
				
			// ALLOCATING DIRECTORY BLOCK
			} else {
				return set_dirb_extent(fs_context, inode, index_of_last_used_extent, index_db_after_last_used_extent, extent_size, 0);
			}

		// Case that no, we cannot add to the end of the last extent
		} else {
			// ALLOCATING DATA BLOCKS
			if (db_type == 0) {
				int next_i_extent_index = index_of_last_used_extent + 1;
				(*inode).last_used_extent = next_i_extent_index;
				return set_db_extent(inode, next_i_extent_index, data_index, extent_size, 1);

			// ALLOCATING DIRECTORY BLOCK
			} else {
				int next_i_extent_index = index_of_last_used_extent + 1;
				(*inode).last_used_extent = next_i_extent_index;
				return set_dirb_extent(fs_context, inode, next_i_extent_index, data_index, extent_size, 1);
			}
		}
	}
	return 0;
}

int make_dentry_block(fs_ctx *fs_context, int directory_inode_num) {

	// Create a single block extent to house directory entries if no
	// currently existing extents present. We will initialize entire block
	// to be full of direntries that are empty with (ino value = -1)

	// Get the index of the first-fit data bit
	// Note that this disregards tacking on to the last used extent of the
	// corresponding inode; just find the first-fit.
	int available_data_blk = get_available_bit(fs_context->sb, fs_context->block_bits, 1, 1);
	if (available_data_blk < 0) {
		return -1;
	}

	// Allocate the data block
	available_data_blk = allocate_data_blks(fs_context, directory_inode_num, available_data_blk, 1, 1);
	if (available_data_blk < 0) {
		return -1;
	}

	// Set the data bit in the data bitmap
	if (set_bits(fs_context->sb, fs_context->block_bits, available_data_blk, 1, -1) < 0) {
		return -1;
	}

	return 0;
}

int add_dentry(fs_ctx *fs_context, int directory_inode_num, int dentry_inode_num, char* name) {

	// Create and initialize a directory entry block in the directory inode if none exist
	if (!(fs_context->itable[directory_inode_num].last_used_extent != -1)) {
		if (make_dentry_block(fs_context, directory_inode_num) < 0) {
			return -1;
		}
	}

	// Create and initialize a directory entry block in the directory inode if all of them are taken
	int first_data_block = fs_context->sb->sb_first_data_block;
	int full = 0;
	// Loop through every used extent in the corresponding directory's inode. 
	for (int i = 0; i <= (int)fs_context->itable[directory_inode_num].last_used_extent; i++){
		// Loop through every block in the current extent
		if (full == 1) {
			break;
		}
		int start = fs_context->itable[directory_inode_num].i_extent[i].start;
		int length = fs_context->itable[directory_inode_num].i_extent[i].count;
		for( int j = start; j < start + length; j++) {
			if (full == 1) {
				break;
			}
			// Loop through every directory entry in the current block
			for(int k = 0; k < A1FS_BLOCK_SIZE; k += sizeof(a1fs_dentry)){
				if( ((a1fs_dentry *)(fs_context->image + A1FS_BLOCK_SIZE*(j+first_data_block) + k))->ino != -1){
					full = 1;
					if (make_dentry_block(fs_context, directory_inode_num) < 0) {
						return -1;
					}
					break;
				}
			}
		}	
	}

	// Find and use the first empty directory entry
	// Loop through every used extent in the corresponding directory's inode. 
	for (int i = 0; i <= (int)fs_context->itable[directory_inode_num].last_used_extent; i++){
		// Loop through every block in the current extent
		int start = fs_context->itable[directory_inode_num].i_extent[i].start;
		int length = fs_context->itable[directory_inode_num].i_extent[i].count;
		for( int j = start; j < start + length; j++) {
			// Loop through every directory entry in the current block
			for(int k = 0; k < A1FS_BLOCK_SIZE; k += sizeof(a1fs_dentry)){
				// Use the first empty directory entry
				if( ((a1fs_dentry *)(fs_context->image + A1FS_BLOCK_SIZE*(j+first_data_block) + k))->ino == -1){
					// Copy <name> into the directory entry's name member
					strncpy(((a1fs_dentry *)(fs_context->image + A1FS_BLOCK_SIZE*(j+first_data_block)  + k))->name, name, strlen(name));
					((a1fs_dentry *)(fs_context->image + A1FS_BLOCK_SIZE*(j+first_data_block)  + k))->name[strlen(name)] = '\0';
					// Set the directory entry's inode member
					((a1fs_dentry *)(fs_context->image + A1FS_BLOCK_SIZE*(j+first_data_block)  + k))->ino = (a1fs_ino_t)dentry_inode_num;

					// Update inode metadata
					fs_context->itable[directory_inode_num].links += 1;
					fs_context->itable[directory_inode_num].size += 1;
					struct timespec curr_time;
					clock_gettime(CLOCK_REALTIME, &curr_time);
					fs_context->itable[directory_inode_num].i_mtime = curr_time;
					fs_context->itable[directory_inode_num].num_entries += 1;
					
					// Update superblock metadata
					fs_context->sb->sb_used_dirs_count += 1;

					return 0;
				}
			}
		}	
	}
	
	return -1;
}