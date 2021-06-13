#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <error.h>
#include <time.h>
#include <stdint.h>

int main(int argc, char *argv[]) {

    (void)argc;
	(void)argv;



	// unsigned int disk_image[2] = {0x71, 0x10};

	// typedef struct byte {
	// 	int new_val;
	// 	int another_new_val;
	// } byte;

	
	// printf("%d\n", disk_image[0]);

	// byte *sb = (byte *)(disk_image);
	// sb->new_val = 0x00;
	// sb->another_new_val = 0x10;
	// printf("%d\n", disk_image[0]);
	// printf("%d\n", disk_image[1]);

	// return 0;

	/** Block number (block pointer) type. */
	typedef uint32_t a1fs_blk_t;

	/** Extent - a contiguous range of blocks. */
	typedef struct a1fs_extent {
		/** Starting block of the extent. */
		a1fs_blk_t start;
		/** Number of blocks in the extent. */
		a1fs_blk_t count;
		/** Number of bits currently in use by this extent*/
		a1fs_blk_t size;

	} a1fs_extent;

	/** a1fs inode. */
	typedef struct a1fs_inode {
		mode_t 			  mode;
		uint32_t 		  links;
		uint64_t 		  size;
		struct timespec   i_mtime;					/* Last modified time */
		a1fs_extent       i_extent[12];  			/* Pointers to extents */  
		int8_t 		  	  last_used_extent;			/* Index of Last Used Extent */
		uint32_t 		  num_entries;				/* Number of entries if directory */
		uint32_t 		  last_entry_index;			/* Index of last entry if directory */
		uint8_t           i_pad[64];	  			/* Padding */

	} a1fs_inode;

	printf("%ld\n", sizeof(a1fs_inode));
	printf("%ld\n", 256 - sizeof(a1fs_inode));

	return 0;

	
}