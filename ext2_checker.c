#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include "ext2.h"
#include "ext2_helper_modified.h"

unsigned char *disk;
unsigned char *block_bit_map;
unsigned char *inode_bit_map;
struct ext2_inode *inode_table;
int table_size;
struct ext2_super_block *sb;
struct ext2_group_desc *gd;
int table_start;
int dblock_size;
int fixed_count;


/* Check if a block is allocated on bitmap, it can be either type i(inode) or d(data block). */
int check_map(int block_num, int inode_num, char type){
    unsigned char* map;
    int byte_index, bit_index;
    if(type == 'i'){
        map = inode_bit_map;
    } else if(type == 'd'){
        map = block_bit_map;
    }
    byte_index = (block_num - 1)/8;
    bit_index = (block_num - 1)%8;

    /* Check if the bitmap is marked. */
    if((map[byte_index] & 1<<(bit_index)) == 0){
        map[byte_index] |= 1<<bit_index;
        fixed_count += 1;
        if(type == 'i'){
            printf("Fixed: inode [%d] not marked as in-use\n", block_num);
        } else {
            printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n", 
            block_num, inode_num);
        }
    }
    return 0;
    
}

/*
 * Given a directory inode, check all files and subdirectories in it satisfy all following conditions.
 *  b. Check type the data block of the directory inode.
 *  c. Check if the bit is marked on inode_bit_map.
 *  d. Check if the inode's d_time is set to 0;
 *  e. Check if block bitmap is allocated on bitmap.
 */
int check_dir_entry_type(struct ext2_inode* inode, int inode_number){
    int i = 0;
    int db_num, current_rec;
    struct ext2_dir_entry* cur_entry;
    struct ext2_inode* cur_inode;
    unsigned char inode_mode;
    while(inode->i_block[i] != 0){
        db_num = inode->i_block[i]; 
        /* Check data block is allocated. */
        check_map(db_num, inode_number, 'd');
        current_rec = 0;
        while(current_rec < EXT2_BLOCK_SIZE){
            cur_entry = (struct ext2_dir_entry*)(disk + EXT2_BLOCK_SIZE*db_num + current_rec);
            current_rec += cur_entry->rec_len;
            /* Handle the special case where blocked is occupied but empty(contains nothing).*/
            if(cur_entry->inode == 0){
                break;
            }
            cur_inode = &inode_table[cur_entry->inode - 1];
            /* Check inode is allocated on bitmap. */
            check_map(cur_entry->inode, 0, 'i');
            /* Check if inode's i_dtime is 0*/
            if(cur_inode->i_dtime != 0){
                printf("Fixed: valid inode marked for deletion: [%d]\n", cur_entry->inode);
                fixed_count += 1;
                cur_inode->i_dtime = 0;
            }
            /* Check file type consistent. */
            if(cur_inode->i_mode & EXT2_S_IFREG){
                inode_mode = EXT2_FT_REG_FILE;
            } else if(cur_inode->i_mode & EXT2_S_IFDIR){
                inode_mode = EXT2_FT_DIR;
            } else if(cur_inode->i_mode & EXT2_S_IFLNK){
                inode_mode = EXT2_FT_SYMLINK;
            } else {
                inode_mode = EXT2_FT_UNKNOWN;
            }
            if(inode_mode != cur_entry->file_type){
                /* Print the current inodes's number but not the index. */
                printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", cur_entry->inode);
                /* Trust inode's file type. */
                cur_entry->file_type = inode_mode;
                fixed_count += 1;
            }
        }
        i += 1;
    }
    return 0;
}

int check_iblock(struct ext2_inode* inode, int inode_number){
    int i = 0;
    while(inode->i_block[i] != 0){
        check_map(inode->i_block[i], inode_number, 'd');
        i += 1;
    }
    return 0;
} 
 
 
/* 
 * Loop over all directory inodes to find files, directories, links...
 */
int check_dir_type(){
    /* Check the root. */
    printf("Check each directory block.\n");
    check_dir_entry_type(&inode_table[1], 2);
    
    /* Go throught the inodes, step into data blocks when inode is type d. */
    int byte_index, bit_index;
    for(int index = 11 -1; index < table_size; index++){
        byte_index = index/8;
        bit_index = index%8;
        if(inode_bit_map[byte_index] & 1<<bit_index){
            if(inode_table[index].i_mode & EXT2_S_IFREG){
                check_iblock(&inode_table[index], index + 1);
            } else if (inode_table[index].i_mode & EXT2_S_IFDIR){
                check_dir_entry_type(&inode_table[index], index + 1);
            } else if (inode_table[index].i_mode & EXT2_S_IFLNK){
                check_iblock(&inode_table[index], index + 1);
            }
        }
    }
    return 0;
}

/*
 * Check if the number of free blocks/inodes count is consistent 
 * to the free blocks on bitmap.
 */
int check_free_counts(){
    int actual_free_inodes = num_free_inodes();
    int actual_free_dblocks = num_free_dblocks();
    int d;
    d = actual_free_dblocks - gd->bg_free_blocks_count;
    /* Check and update gd. */
    if(d != 0){
        printf("Fixed: block group's free blocks counter was off by %d\n", d);
        fixed_count += 1;
        gd->bg_free_blocks_count += d;
    }
    
    d = actual_free_inodes - gd->bg_free_inodes_count;
    if(d != 0){
        printf("Fixed: blocks group's free inodes counter was off by %d\n", d);
        fixed_count += 1;
        gd->bg_free_inodes_count += d;
    }
    
    d = actual_free_dblocks - sb->s_free_blocks_count;
    /* Check and update sb. */
    if(d != 0){
        printf("Fixed: superblock's free blocks counter was off by %d\n", d);
        fixed_count += 1;
        sb->s_free_blocks_count += d;
    }
    
    d = actual_free_inodes - sb->s_free_inodes_count ;
    if(d != 0){
        printf("Fixed: superblock's free inodes counter was off by %d\n", d);
        fixed_count += 1;
        sb->s_free_inodes_count += d;
    }
    return 0;
}

/* Main function*/
int main(int argc, char** argv){
    fixed_count = 0;
    if(init_disk(argc, argv) != 0){
        return -EINVAL;
    }
    if(argc != 2){
        printf("Usage: <image name>\n");
        return -EINVAL;
    }
    check_dir_type();
    check_free_counts();
    if(fixed_count == 0){
        printf("No file system inconsistencies repaired!\n");
    } else {
        printf("%d file system inconsistencies repaired!\n", fixed_count);
    }
    return 0;

}
 
