#include "a1fs_helper.h"

int get_inode_num(fs_ctx *fs_context, const char* path, int tog) {
	// Create copy of the path string before mutation  
	char temp_path[strlen(path) + 1];
	strncpy(temp_path, path, strlen(path));
	temp_path[strlen(path)] = '\0';

	// Initialize the first token and the corresponding inode 
	char *token = strtok(temp_path, "/");
	int curr_inode = 0;
	int par_inode = 0;
	// <token> is NULL -- which is 0 -- if no tokens found  
	while(token) {

		// Keep track of parent inode
		if (curr_inode != -1){
			par_inode = curr_inode;
		}

		// Update <curr_inode> to be inode of the file or   
        // directory of the current directory, or   
        // possibly -1.
		curr_inode = inode_lookup(fs_context, par_inode, token);

		// Next token
		token = strtok(NULL, "/");
	}

	if (tog == 1) {
		return par_inode;
	}
	return curr_inode;
}

int inode_lookup(fs_ctx *fs_context, int par_inode, char* token){

	// Parent inode does not exist
	if (par_inode == -1) {
		return -1;
	}

	// TODO: indirect case
	// Loop through every used extent in the corresponding directory's inode
	for (int i = 0; i <= (int)fs_context->itable[par_inode].last_used_extent; i++){

		// Loop through every block in the current extent
		int start = fs_context->itable[par_inode].i_extent[i].start;
		int length = fs_context->itable[par_inode].i_extent[i].count;
		for( int j = start; j < start + length; j++) {

			// Loop through every directory entry in the current block
			for(int k = 0; k < A1FS_BLOCK_SIZE; k += sizeof(a1fs_dentry)){

				// Return inode number if file or directory name is found
				if( strcmp( ((a1fs_dentry *)(fs_context->image + (A1FS_BLOCK_SIZE * j) + (A1FS_BLOCK_SIZE * fs_context->sb->sb_first_data_block) + k))->name, token) == 0) {
					return ((a1fs_dentry *)(fs_context->image + (A1FS_BLOCK_SIZE * j) + (A1FS_BLOCK_SIZE * fs_context->sb->sb_first_data_block) + k))->ino;
				}	
			}
		}
	}
	return -1;
}

// Disclosure: last ditch effort to implement indirection involved
int attachable(fs_ctx *fs_context, int inode_index, int extent_count, int *index_of_last_used_extent, int *index_db_after_last_used_extent) {
	// Can we add to the end of the last extent?
	int large_enough = 0;

	// Case of indirect block
	if (fs_context->itable[inode_index].last_used_indirect != -1) {
		// The relative byte at which represents the current indirect extent
		int indirect_extent_index_in_bytes = fs_context->itable[inode_index].last_used_indirect * A1FS_BLOCK_SIZE;

		// Index of the indirect block relative to the first data block
		int indirect_fdb_index = fs_context->itable[inode_index].i_extent[12].start;

		// The current indirect extent
		a1fs_extent * cur_indirect_extent = (a1fs_extent *) fs_context->image
							+ fs_context->sb->sb_first_data_block * A1FS_BLOCK_SIZE
							+ indirect_fdb_index * A1FS_BLOCK_SIZE
							+ indirect_extent_index_in_bytes;

		// This is the index of the data block right after the last extent
		*index_db_after_last_used_extent = cur_indirect_extent->start + cur_indirect_extent->count;

		// Check the <extent_count> blocks after the last extent
		for (int i = *index_db_after_last_used_extent; i < extent_count + (*index_db_after_last_used_extent); i++) {
			// Get specific byte containing the data block
			int byte = i / 8;

			// Get number of bits to left-shift
			int bit = i % 8;

			// Indicate if space after last extent is not large enough or continue with the loop
			if (fs_context->block_bits[byte] & (1 << bit)) {
				large_enough = 1;
				break;
			} else {
				continue;
			}
		}
		// Indicate that indirect block was used
		*index_of_last_used_extent = -1;

		return large_enough;
	}

	// Case of non-indirect block
	*index_of_last_used_extent = fs_context->itable[inode_index].last_used_extent;
	a1fs_extent last_used_extent = fs_context->itable[inode_index].i_extent[*index_of_last_used_extent];

	// This is the index of the data block right after the last extent
	*index_db_after_last_used_extent = last_used_extent.start + last_used_extent.count;

	// Check the <extent_count> blocks after the last extent
	for (int i = *index_db_after_last_used_extent; i < extent_count + (*index_db_after_last_used_extent); i++) {
		// Get specific byte containing the data block
		int byte = i / 8;

		// Get number of bits to left-shift
		int bit = i % 8;

		// Indicate if space after last extent is not large enough or continue with the loop
		if (fs_context->block_bits[byte] & (1 << bit)) {
			large_enough = 1;
			break;
		} else {
			continue;
		}
	}
	return large_enough;
}

int set_db_extent(a1fs_inode *inode, int extent_index, int extent_start, int extent_count, int new) {
	// TODO: indirect case
	if (new) {
		inode->i_extent[extent_index].start = extent_start;
		inode->i_extent[extent_index].count = extent_count;
		inode->i_extent[extent_index].size = 0;
	} else {
		inode->i_extent[extent_index].count += extent_count;
	}

	struct timespec curr_time;
	clock_gettime(CLOCK_REALTIME, &curr_time);
	inode->i_mtime = curr_time;

	return extent_start;
}

int set_dirb_extent(fs_ctx *fs_context, a1fs_inode *inode, int extent_index, int extent_start, int extent_count, int new) {
	// TODO: indirect case
	if (new) {
		inode->i_extent[extent_index].start = extent_start;
		inode->i_extent[extent_index].count = extent_count;
		inode->i_extent[extent_index].size = 0;
	} else {
		inode->i_extent[extent_index].count += extent_count;
	}
	for (int i = 0; i < A1FS_BLOCK_SIZE; i += sizeof(a1fs_dentry)) {
		a1fs_dentry *dentry = (a1fs_dentry *)(fs_context->image 
											+ (A1FS_BLOCK_SIZE * fs_context->sb->sb_first_data_block) 
											+ (A1FS_BLOCK_SIZE * inode->i_extent[extent_index].start) 
											+ i);
		dentry->ino = (a1fs_ino_t)-1;
	}

	struct timespec curr_time;
	clock_gettime(CLOCK_REALTIME, &curr_time);
	inode->i_mtime = curr_time;

	return extent_start;
}

// Disclosure: last ditch effort to implement indirection involved
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

	if ((db_type != 0) && (db_type != 1)) {
		return -1;
	}
	a1fs_inode *inode_table = fs_context->itable;
	a1fs_inode *inode = &(inode_table[inode_index]);

	// Case that the inode has no allocated data blocks -- so we create the first extent
	if (inode->last_used_extent == -1) {

		// ALLOCATING DATA BLOCKS
		if (db_type == 0) {
			(*inode).last_used_extent = 0;
			fprintf(stderr, "\nallocate_data_blks: empty, new, data block; ino %d, num_blks %d, extent_index %d, db_index %d\n", inode_index, extent_size, 0, data_index);
			return set_db_extent(inode, 0, data_index, extent_size, 1);

		// ALLOCATING DIRECTORY BLOCK
		} else {
			(*inode).last_used_extent = 0;
			fprintf(stderr, "\nallocate_data_blks: empty, new, dir block; ino %d, num_blks %d, extent_index %d, db_index %d\n", inode_index, extent_size, 0, data_index);
			return set_dirb_extent(fs_context, inode, 0, data_index, extent_size, 1);
		}



	// Case that the inode has previously allocated data blocks
	} else {
		int index_of_last_used_extent;
		int index_db_after_last_used_extent;
		
		// Case that yes, we can add to the end of the last extent
		if (attachable(fs_context, inode_index, extent_size, &index_of_last_used_extent, &index_db_after_last_used_extent)) {

			// ALLOCATING DATA BLOCKS
			if (db_type == 0) {
				fprintf(stderr, "\nallocate_data_blks: non-empty, addon, and data block; ino %d, num_blks %d, extent_index %d, db_index %d\n", inode_index, extent_size, index_of_last_used_extent, index_db_after_last_used_extent);
				return set_db_extent(inode, index_of_last_used_extent, index_db_after_last_used_extent, extent_size, 0);
				
			// ALLOCATING DIRECTORY BLOCK
			} else {
				fprintf(stderr, "\nallocate_data_blks: non-empty, addon, and dir block; ino %d, num_blks %d, extent_index %d, db_index %d\n", inode_index, extent_size, index_of_last_used_extent, index_db_after_last_used_extent);
				return set_dirb_extent(fs_context, inode, index_of_last_used_extent, index_db_after_last_used_extent, extent_size, 0);
			}

		// Case that no, we cannot add to the end of the last extent
		} else {
			// ALLOCATING DATA BLOCKS
			if (db_type == 0) {
				int next_i_extent_index = index_of_last_used_extent + 1;
				(*inode).last_used_extent = next_i_extent_index;
				fprintf(stderr, "\nallocate_data_blks: non-empty, new, and data block; ino %d, num_blks %d, extent_index %d, db_index %d\n", inode_index, extent_size, next_i_extent_index, data_index);
				return set_db_extent(inode, next_i_extent_index, data_index, extent_size, 1);

			// ALLOCATING DIRECTORY BLOCK
			} else {
				int next_i_extent_index = index_of_last_used_extent + 1;
				(*inode).last_used_extent = next_i_extent_index;
				fprintf(stderr, "\nallocate_data_blks: non-empty, new, and dir block; ino %d, num_blks %d, extent_index %d, db_index %d\n", inode_index, extent_size, next_i_extent_index, data_index);
				return set_dirb_extent(fs_context, inode, next_i_extent_index, data_index, extent_size, 1);
			}
		}
	}
	return -1;
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
		fprintf(stderr, "a1fs_helper: make_dentry_block: get_available_bit failed\n");
		return -1;
	}

	// Allocate the data block
	available_data_blk = allocate_data_blks(fs_context, directory_inode_num, available_data_blk, 1, 1);
	if (available_data_blk < 0) {
		fprintf(stderr, "a1fs_helper: make_dentry_block: allocate_data_blks failed\n");
		return -1;
	}

	// Set the data bit in the data bitmap
	if (set_bits(fs_context->sb, fs_context->block_bits, available_data_blk, 1, 1, 1) < 0) {
		fprintf(stderr, "a1fs_helper: make_dentry_block: set_bits failed\n");
		return -1;
	}

	return 0;
}

int add_dentry(fs_ctx *fs_context, int directory_inode_num, int dentry_inode_num, char* name) {

	// Create and initialize a directory entry block in the directory inode if none exist
	if (fs_context->itable[directory_inode_num].last_used_extent == -1) {
		if (make_dentry_block(fs_context, directory_inode_num) < 0) {
			return -1;
		}
	}

	// Check if all directory entries in the directory block are taken
	int first_data_block = fs_context->sb->sb_first_data_block;
	int full = 1;
	// Loop through every used extent in the corresponding directory's inode. 
	for (int i = 0; i <= (int)fs_context->itable[directory_inode_num].last_used_extent; i++){
		// Loop through every block in the current extent
		if (full == 0) {
			break;
		}
		int start = fs_context->itable[directory_inode_num].i_extent[i].start;
		int length = fs_context->itable[directory_inode_num].i_extent[i].count;
		for( int j = start; j < start + length; j++) {
			if (full == 0) {
				break;
			}
			// Loop through every directory entry in the current block
			for(int k = 0; k < A1FS_BLOCK_SIZE; k += sizeof(a1fs_dentry)){
				if( ((a1fs_dentry *)(fs_context->image + (A1FS_BLOCK_SIZE * j) + (A1FS_BLOCK_SIZE * first_data_block) + k))->ino == -1){
					full = 0;
					break;
				}
			}
		}	
	}
	// Create a new directory entry block if all directory entires in existing blocks are taken
	if (full == 1) {
		if (make_dentry_block(fs_context, directory_inode_num) < 0) {
			return -1;
		}
	}

	// TODO: indirect case
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
				if( ((a1fs_dentry *)(fs_context->image + (A1FS_BLOCK_SIZE * j) + (A1FS_BLOCK_SIZE * first_data_block) + k))->ino == -1){
					// Copy <name> into the directory entry's name member
					strncpy(((a1fs_dentry *)(fs_context->image + (A1FS_BLOCK_SIZE * j) + (A1FS_BLOCK_SIZE * first_data_block) + k))->name, name, strlen(name));
					((a1fs_dentry *)(fs_context->image + (A1FS_BLOCK_SIZE * j) + (A1FS_BLOCK_SIZE * first_data_block) + k))->name[strlen(name)] = '\0';
					// Set the directory entry's inode member
					((a1fs_dentry *)(fs_context->image + (A1FS_BLOCK_SIZE * j) + (A1FS_BLOCK_SIZE * first_data_block) + k))->ino = (a1fs_ino_t)dentry_inode_num;

					// Update inode metadata
					fs_context->itable[directory_inode_num].links = 2;
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

int make_data_blocks(fs_ctx *fs_context, int inode_index, int num_blocks) {
	
	// Get the index of the first-fit data bit
	int available_data_blk = get_available_bit(fs_context->sb, fs_context->block_bits, 1, num_blocks);

	// Base case
	if (available_data_blk > -1) {
		// Allocate the data block
		available_data_blk = allocate_data_blks(fs_context, inode_index, available_data_blk, num_blocks, 0);
		if (available_data_blk < 0) {
			fprintf(stderr, "a1fs_helper: make_data_blocks: allocate_data_blks failed\n");
			return -1;
		}

		// Set the data bit in the data bitmap
		if (set_bits(fs_context->sb, fs_context->block_bits, available_data_blk, 1, num_blocks, 1) < 0) {
			fprintf(stderr, "a1fs_helper: make_data_blocks: set_bits failed\n");
			return -1;
		}

		return num_blocks;

	// Recursion
	} else {
		int total_created = 0;
		int blocks_created = 0;
		do
		{
			int blocks_created = make_data_blocks(fs_context, inode_index, num_blocks - 1);
			if (blocks_created != -1) {
				total_created += blocks_created;
			} else {
				return -1;
			}
		} while (num_blocks - blocks_created > 0);
		return total_created;
	}

	return -1;
}

int truncate_helper(fs_ctx *fs, int cur_inode, off_t size) {
	// Num blocks is the floored number of blocks of the incoming size
	int num_blocks = size / A1FS_BLOCK_SIZE;
	int leftover_bytes = size - (num_blocks * A1FS_BLOCK_SIZE);
	
	fprintf(stderr, "\ntruncate_helper: num_blocks = %d \n", num_blocks);
	fprintf(stderr, "\ntruncate_helper: leftover_bytes = %d \n", leftover_bytes);

	// Make the corresponding data blocks
	//     - a non-zero leftover_bytes indicates the need for an extra block
	if (leftover_bytes != 0) {
		if ((num_blocks + 1) != make_data_blocks(fs, cur_inode, (num_blocks + 1))) {
			fprintf(stderr, "a1fs_truncate: make_data_blocks failed\n");
			return -1;
		}
	} else {
		if (num_blocks != make_data_blocks(fs, cur_inode, num_blocks)) {
			fprintf(stderr, "a1fs_truncate: make_data_blocks failed\n");
			return -1;
		}
	}

	// Update inode metadata
	struct timespec curr_time;
	clock_gettime(CLOCK_REALTIME, &curr_time);
	fs->itable[cur_inode].i_mtime = curr_time;

	// The number of blocks to zero out
	int blocks_left_to_zero;
	if (leftover_bytes != 0) {
		blocks_left_to_zero = num_blocks + 1;	
	} else {
		blocks_left_to_zero = num_blocks;
	}


	// TODO: indirect case
	// Number of blocks needed for zeroeing
	int blocks_needed = blocks_left_to_zero;
	// Find the index of the extent that holds the first block to zero
	int extent_first_db_to_zero;

	// Find the starting block index at which we begin zeroeing such that the last <blocks_left_to_zero> blocks are zeroed
	int first_db_to_zero;
	for (int i = fs->itable[cur_inode].last_used_extent; i >= 0; i--) {
		if (fs->itable[cur_inode].i_extent[i].count >= blocks_needed) {
			extent_first_db_to_zero = i;
			first_db_to_zero = (fs->itable[cur_inode].i_extent[i].start + fs->itable[cur_inode].i_extent[i].count) - blocks_needed;
			break;
		} else {
			blocks_needed -= fs->itable[cur_inode].i_extent[i].count;
		}
	}

	fprintf(stderr, "\ntruncate_helper: extent_first_db_to_zero = %d \n", extent_first_db_to_zero);
	fprintf(stderr, "\ntruncate_helper: first_db_to_zero = %d \n", first_db_to_zero);

	// TODO: indirect case
	// Loop through the extents of which contain the blocks to be zeroed
	for (int i = extent_first_db_to_zero; i <= (int)fs->itable[cur_inode].last_used_extent; i++){

		// Break out of loop if corresponding blocks are all zeroed out
		if (blocks_left_to_zero == 0) {
			break;
		}

		// First extent may have existing blocks already, therefore it starts with a different index
		if (i == extent_first_db_to_zero) {

			if (zero_out_blocks(fs, cur_inode, i, 
									first_db_to_zero, 
									fs->itable[cur_inode].i_extent[i].count, 
									&blocks_left_to_zero, 
									leftover_bytes) < 0) {
				fprintf(stderr, "\ntruncate_helper: zero_out_blocks failed for initial data extent\n");
				return -1;
			}

			// Update the extent size
			fs->itable[cur_inode].i_extent[i].size += leftover_bytes;
		} else {

			if (zero_out_blocks(fs, cur_inode, i, 
								fs->itable[cur_inode].i_extent[i].start, 
								fs->itable[cur_inode].i_extent[i].count, 
								&blocks_left_to_zero, 
								leftover_bytes) < 0) {
			fprintf(stderr, "\ntruncate_helper: zero_out_blocks failed for subsequent data extents\n");
			return -1;
			}

			// Update the extent size
			fs->itable[cur_inode].i_extent[i].size += leftover_bytes;
		}
	}

	// Update inode modification time
	clock_gettime(CLOCK_REALTIME, &curr_time);
	fs->itable[cur_inode].i_mtime = curr_time;

	return 0;
}

int zero_out_blocks(fs_ctx *fs, int cur_inode, int extent_index, int start, int length, int *blocks_left_to_zero, int leftover_bytes) {
	
	// TODO: indirect case
	// Loop through every block in the current extent
	// fprintf(stderr, "\nzero_out_blocks: starting at block index %d \n", start);
	// fprintf(stderr, "\nzero_out_blocks: going through %d blocks \n", length);
	for( int j = start; j < start + length; j++) {
		
		// Get the corresponding data block address
		unsigned char * db_addr = (unsigned char *)(fs->image + (A1FS_BLOCK_SIZE * j) + (A1FS_BLOCK_SIZE * fs->sb->sb_first_data_block));

		// Only zero out <leftover_bytes> if we are at the leftover block
		// and if there exists leftover bytes
		if (*blocks_left_to_zero == 1 && leftover_bytes != 0) {
			if (memset(db_addr, '\0', leftover_bytes) == NULL) {
				fprintf(stderr, "zero_out_blocks: memset failed for leftover_bytes\n");
				return -1;
			}
			
			// fprintf(stderr, "\nzero_out_blocks: just zeroed out %d bytes at %d on db index (rel. to 1st db) %d\n", leftover_bytes, j + fs->sb->sb_first_data_block, j);

			// Update the size of the current extent
			fs->itable[cur_inode].i_extent[extent_index].size += leftover_bytes;

			*blocks_left_to_zero -= 1;
			break;
		}

		// Zero out the current block
		if (memset(db_addr, '\0', A1FS_BLOCK_SIZE) == NULL) {
			fprintf(stderr, "zero_out_blocks: memset failed for full block of bytes\n");
			return -1;
		}

		// fprintf(stderr, "\nzero_out_blocks: just zeroed out a block at %d on db index (rel. to 1st db) %d\n", j + fs->sb->sb_first_data_block, j);

		// Update the size of the current extent
		fs->itable[cur_inode].i_extent[extent_index].size -= A1FS_BLOCK_SIZE;

		*blocks_left_to_zero -= 1;
	}	
	return 0;
}

int find_offset_extent(fs_ctx *fs, int inode_num, int *cur_extent_index, int *cur_indirect_extent_index, int *remainingoffset) {
	
	(void) cur_indirect_extent_index;
	
	if (*remainingoffset == 0) {
		return 0;
	}

	while(*remainingoffset > 0){

		// // Case that we must check the indirect extent
		// if (*cur_extent_index == 12) {
		// 	*cur_indirect_extent_index += 1;
			
		// 	// The relative byte at which represents the current indirect extent
		// 	int indirect_extent_index_in_bytes = 0;
		// 	while (*remainingoffset > 0) {

		// 		// Index of the indirect block relative to the first data block
		// 		int indirect_fdb_index = fs->itable[inode_num].i_extent[12].start;

		// 		// The current indirect extent
		// 		a1fs_extent * cur_indirect_extent = (a1fs_extent *) fs->image
		// 							+ fs->sb->sb_first_data_block * A1FS_BLOCK_SIZE
		// 							+ indirect_fdb_index * A1FS_BLOCK_SIZE
		// 							+ indirect_extent_index_in_bytes;

		// 		// Decrement <remainingoffset> and start checking the next indirect extent
		// 		if (cur_indirect_extent->size < *remainingoffset){
		// 			remainingoffset -= cur_indirect_extent->size;

		// 			cur_indirect_extent_index++;
		// 			indirect_extent_index_in_bytes += sizeof(a1fs_extent);

		// 		// Break when the indirect extent containing the offset byte is found
		// 		} else{
		// 			return 0;
		// 		}
		// 	}
		// 	return -1;
		// }

		// Decrement <remainingoffset> and start checking the next extent
		if (fs->itable[inode_num].i_extent[*cur_extent_index].size < *remainingoffset){
			remainingoffset -= fs->itable[inode_num].i_extent[*cur_extent_index].size;
			
			*cur_extent_index += 1;
		// Break when the extent containing the offset byte is found
		}else{
			return 0;
		}
	}
	return -1;
}
