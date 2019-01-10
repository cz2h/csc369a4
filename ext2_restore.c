#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "ext2.h"
#include "errno.h"
#include "ext2_helper_modified.h"

int search_gap(int dblock_num, int cur_rec, char *filename);
//int search_gap_bottom(int dblock_num, int current_reclen, char *filename);
int recoverable(int inode_num);
int data_block_occupied(int block_index);
void restore_entry(int dblock_num, int reclen_before_gap, int gap_reclen);
void restore_data_block(int block_index);
void restore_inode_full(int inode_num);



/*disk components*/
unsigned char *disk;
unsigned char *block_bit_map;
unsigned char *inode_bit_map;
struct ext2_inode *inode_table;
int table_size;
int dblock_size;
int table_start;
struct ext2_super_block *sb;
struct ext2_group_desc *gd;


/*
 *@cur_rec: cur_rec of suspect entry in dblock (suspicious because its rec_len is larger than it needs)
 *search gap: return offset of the gap from beginning of current dblock if found, return -1 if not found under given entry
 */
int search_gap(int dblock_num, int cur_rec, char *filename){
    struct ext2_dir_entry *cur_entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE*dblock_num + cur_rec); 
    int reclen_count = calculate_reclen(cur_entry);
    struct ext2_dir_entry *gap = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE*dblock_num + cur_rec + reclen_count);
    while(reclen_count < cur_entry->rec_len){
        if(gap->inode == 0){
            break;
        }
        char gap_name[gap->name_len+1];
        strncpy(gap_name, gap->name, gap->name_len);
        gap_name[gap->name_len] = '\0';
        if(strcmp(gap_name, filename) == 0){
            return (cur_rec + reclen_count);              
        }
        reclen_count += gap->rec_len;
        gap = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE*dblock_num + cur_rec + reclen_count);
    }
    return -1;
}

/*
 *similar to search_gap, except now we are searching the gaps at the bottom of a dir block (further discussion needed here!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!)
 */
int search_gap_bottom(int dblock_num, int current_reclen, char *filename){
    struct ext2_dir_entry *cur_entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE*dblock_num + current_reclen); 
    int cur_rec = current_reclen + calculate_reclen(cur_entry);
    struct ext2_dir_entry *gap = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE*dblock_num + cur_rec); 
    while(cur_rec < EXT2_BLOCK_SIZE){
        if(gap->inode == 0){
            break;
        }
        char gap_name[gap->name_len+1];
        strncpy(gap_name, gap->name, gap->name_len);
        gap_name[gap->name_len] = '\0';
        if(strcmp(gap_name, filename) == 0){
            return cur_rec;
        }        
       
        cur_rec += gap->rec_len;
        if(cur_rec >= EXT2_BLOCK_SIZE){
            break;
        } 
        gap = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE*dblock_num + cur_rec); 
    }
    return -1;
}

void restore_entry(int dblock_num, int reclen_before_gap, int gap_reclen){
    struct ext2_dir_entry *entry_before_gap = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE*dblock_num + reclen_before_gap);
    int total_reclen = entry_before_gap->rec_len;
    struct ext2_dir_entry *gap = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE*dblock_num + gap_reclen);
    entry_before_gap->rec_len = gap_reclen - reclen_before_gap;
    gap->rec_len = total_reclen - entry_before_gap->rec_len;    
}

/*
 *return 1 if corresponding inode, as well as all data blocks in its i_block, are recoverable, meaning that they have not been re-allocated, retrun 0 otherwise
 */
int recoverable(int inode_num){
    int inode_index = inode_num - 1;
    if(inode_bit_map[inode_index/8] & (1<<(inode_index%8))){
        return 0;
    }
    struct ext2_inode inode_to_restore = inode_table[inode_num-1];
    int index = 0;
    int block_index;    
    while(inode_to_restore.i_block[index] != 0 && index < 12){
        block_index = inode_to_restore.i_block[index];
        if(data_block_occupied(block_index)){
            return 0;
        }
        index++;
    }
    if(index == 12){
        if(inode_to_restore.i_block[12] != 0){ 
            block_index = inode_to_restore.i_block[12];
            if(data_block_occupied(block_index)){
                return 0;
            }
            unsigned int *indirect_block = (unsigned int *)(disk + EXT2_BLOCK_SIZE*block_index);
            int indirect_index = 0;
            while(indirect_block[indirect_index] != 0){
                int index_of_data_block_in_indirect_block = indirect_block[indirect_index];
                if(data_block_occupied(index_of_data_block_in_indirect_block)){
                    return 0;
                }
                indirect_index++;
            }
        }
 
    }
    return 1;
}

/*
 * return 1 if block with index "block_index" is occupied, return 0 otherwise
 */
int data_block_occupied(int block_index){
    int index_in_block_bitmap = block_index-1;
    int byte_index = index_in_block_bitmap/8;
    int bit_index = index_in_block_bitmap%8;
    return (block_bit_map[byte_index] & (1<<bit_index));
}

/*
 * restore datablock by marking corresponding position in block bitmap as 1
 */
void restore_data_block(int block_index){
    int index_in_block_bitmap = block_index-1;
    int byte_index = index_in_block_bitmap/8;
    int bit_index = index_in_block_bitmap%8;
    block_bit_map[byte_index] |= (1<<bit_index);
    gd->bg_free_blocks_count -= 1;
    sb->s_free_blocks_count -= 1;
}

/*
 * restore inode, including restoring all data blocks in its i_block; 
 */
void restore_inode_full(int inode_num){
    int inode_index = inode_num - 1;
    inode_bit_map[inode_index/8] |= (1<<(inode_index%8));
    gd->bg_free_inodes_count -= 1;
    sb->s_free_inodes_count -= 1;

    struct ext2_inode *inode_to_restore = (struct ext2_inode *)(&(inode_table[inode_num-1]));
    inode_to_restore -> i_dtime = 0;
    inode_to_restore -> i_links_count += 1;
    int index = 0;
    int block_index;    
    while(inode_to_restore->i_block[index] != 0 && index < 12){
        block_index = inode_to_restore->i_block[index];
        restore_data_block(block_index);
        index++;
    }
    if(index == 12){
        if(inode_to_restore->i_block[12] != 0){ 
            block_index = inode_to_restore->i_block[12];
            restore_data_block(block_index);
            unsigned int *indirect_block = (unsigned int *)(disk + EXT2_BLOCK_SIZE*block_index);
            int indirect_index = 0;
            while(indirect_block[indirect_index] != 0){
                int index_of_data_block_in_indirect_block = indirect_block[indirect_index];
                restore_data_block(index_of_data_block_in_indirect_block);
                indirect_index++;
            }
        }
 
    }
}


int main(int argc, char **argv){
    //validate arguments
    if(argc != 3){
        printf("Usage: ext2_restore <image file name> <path to file>\n");
        exit(100);
    }

    //initialize disk
    int fd = open(argv[1], O_RDWR);
    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    gd = (struct ext2_group_desc *)(disk + EXT2_BLOCK_SIZE * 2);
    sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
    inode_bit_map = (unsigned char *)(disk + EXT2_BLOCK_SIZE*gd->bg_inode_bitmap);
    block_bit_map = (unsigned char *)(disk + EXT2_BLOCK_SIZE*gd->bg_block_bitmap);
    inode_table = (struct ext2_inode *)(disk + EXT2_BLOCK_SIZE*gd->bg_inode_table);
    table_start = gd->bg_inode_table;
    table_size = sb->s_inodes_count;
    dblock_size = sb->s_blocks_count;

    //copy args and separate into dir_path and filename
    char file_path[strlen(argv[2])+1];
    strncpy(file_path, argv[2], strlen(argv[2]));
    file_path[strlen(argv[2])] = '\0';

    int tail = strlen(file_path)-1;
    int filename_length = 0;
    while(tail >= 0 && file_path[tail] == '/'){
        tail--;
    }
    while(tail >= 0 && file_path[tail] != '/'){
        filename_length++;
        tail--;
    }
    char filename[filename_length+1];
    strncpy(filename, &(file_path[tail+1]), filename_length);
    filename[filename_length] = '\0';
    char dir_path[tail+1];
    strncpy(dir_path, file_path, tail);
    dir_path[tail] = '\0';

    //validate dir path
    int dir_inode_num = cd_revised(dir_path, 'd');    
    if(dir_inode_num < 0){
        return ENOENT;
    }
    
    //--------------------------------------------------
    //start searching for gaps under the directory path
    //--------------------------------------------------
    struct ext2_inode *dir_inode = (struct ext2_inode *)(&(inode_table[dir_inode_num-1]));
    int index = 0;
    int dblock_num;
    struct ext2_dir_entry *cur_entry;
    int cur_rec = 0;
    int gap_rec; //the offset of matching gap, from beginning of a certain dir block! 
    int gap_inode_num;
    struct ext2_dir_entry *gap; 
    char type;   
    while(index <= 11 && dir_inode->i_block[index] != 0){
        dblock_num = dir_inode->i_block[index];
        cur_entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE*dblock_num);
        while(cur_rec + cur_entry->rec_len < EXT2_BLOCK_SIZE){
            if(cur_entry->rec_len > calculate_reclen(cur_entry)){
                gap_rec = search_gap(dblock_num, cur_rec, filename);
                if(gap_rec > 0){
                    gap = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE*dblock_num + gap_rec);
                    if(gap->file_type == EXT2_FT_REG_FILE){
                        type = 'f';
                    }else if(gap->file_type == EXT2_FT_SYMLINK){
                        type = 'l';
                    }else if(gap->file_type == EXT2_FT_DIR){
                        return EISDIR;
                    }else{
                        return ENOENT;
                    }
                    if(cd_revised(file_path, type) > 0){
                        return EEXIST;
                    }
                    gap_inode_num = gap->inode;
                    if(recoverable(gap_inode_num)){
                        restore_entry(dblock_num, cur_rec, gap_rec);
                        restore_inode_full(gap_inode_num);
                        return 0;
                    }else{
                        return ENOENT; 
                    }
                }
            }
            cur_rec += cur_entry->rec_len;
            cur_entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE*dblock_num + cur_rec);
        }
        //we are now at the last entry of the current directory block
        gap_rec = search_gap_bottom(dblock_num, cur_rec, filename);
        if(gap_rec > 0){
            gap = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE*dblock_num + gap_rec);
            if(gap->file_type == EXT2_FT_REG_FILE){
                type = 'f';
            }else if(gap->file_type == EXT2_FT_SYMLINK){
                type = 'l';
            }else if(gap->file_type == EXT2_FT_DIR){
                return EISDIR;
            }else{
                return ENOENT;
            }
            if(cd_revised(file_path, type) > 0){
                return EEXIST;
            }
            gap_inode_num = gap->inode;
            if(recoverable(gap_inode_num)){
                restore_entry(dblock_num, cur_rec, gap_rec);
                restore_inode_full(gap_inode_num);
                return 0;
            }else{
                return ENOENT; 
            }
        }
 
        index++;
        cur_rec = 0; 
    }
    return ENOENT;
}
