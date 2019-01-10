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


int main(int argc, char **argv){

    //indicates there is POSSIBILITY for case2 (cannot guarantee) 
    int case2 = 0;   

    
    //check the arguments
    if(argc != 4){
        printf("Usage: ext2_cp <image file name> <path to source file> <path to dest>\n");
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


    //copy the contents of the arguments
    char source[strlen(argv[2])+1];
    strncpy(source, argv[2], strlen(argv[2]));
    source[strlen(argv[2])] = '\0';
    char dest[strlen(argv[3])+1];
    strncpy(dest, argv[3], strlen(argv[3]));
    dest[strlen(argv[3])] = '\0';
  

    //open file for read
    FILE *fp = fopen(source, "r");
    if(fp == NULL){
        perror("fopen: ");
        exit(-1);
    }	


    //get rid of trailing slashes of dest and source
    int tail = strlen(dest)-1;
    while(tail >= 0 && dest[tail] == '/'){
        tail--;
    }
    dest[tail+1] = '\0';
    tail = strlen(source)-1;
    while(tail >= 0 && source[tail] == '/'){
        tail--;
    }
    source[tail+1] = '\0';


    //extract filename
    tail = strlen(source)-1;
    int len_filename = 0;
    while(tail >= 0 && source[tail] != '/'){
        tail--;
        len_filename++;
    }
    char filename[len_filename+1];
    strncpy(filename, &(source[tail+1]), len_filename);
    filename[len_filename] = '\0';


    //find the inode number of the directory in which we should do the copy
    //case1: we are given a directory as "dest"
    //case2: a file name is concatenated after a valid path and given as "dest"


    //preparation for case2
    int length_of_last = 0;
    char dest_without_last[EXT2_NAME_LEN+1];
    char alternative_filename[EXT2_NAME_LEN+1];
    if(compute_level(dest) >= 1){
	    tail = strlen(dest)-1;
	    while(tail >= 0 && dest[tail] != '/'){
		tail--;
		length_of_last++;
	    }
	    strncpy(dest_without_last, dest, tail);
	    dest_without_last[tail] = '\0';
	    strncpy(alternative_filename, &(dest[tail+1]), length_of_last);
	    alternative_filename[length_of_last] = '\0';

        case2 = 1;
    }

       
    //get inode_num of the dir to store the new file
    int dir_inode_num;
    if(cd_revised(dest,'d') > 0){  //case1
        dir_inode_num = cd_revised(dest,'d');
        case2 = 0;   
    }else if(case2 && cd_revised(dest_without_last,'d') > 0){ //case2
        dir_inode_num = cd_revised(dest_without_last,'d');
    }else{ //invalid path
        return ENOENT;
    }
    
    if(case2){  
        if(search_in_inode(alternative_filename, strlen(alternative_filename), inode_table[dir_inode_num-1], 'f') > 0){

            return EEXIST;
        }
    }else{
        if(search_in_inode(filename, strlen(filename), inode_table[dir_inode_num-1], 'f') > 0){

            return EEXIST;
        }
    }

 
    //get inode_num of the new file
    int inode_num = allocate_inode(); //inode_num-1 is the index in inode_table
    if(inode_num == -1){
        return ENOSPC;
    }

    struct ext2_inode *cp_inode = (struct ext2_inode *)(&(inode_table[inode_num-1]));
    cp_inode->i_mode = EXT2_S_IFREG;
    cp_inode->i_links_count = 1;


    //add entry of the new file to the dir it belongs to 
    struct ext2_dir_entry *entry_cp = malloc(sizeof(struct ext2_dir_entry));
    entry_cp->inode = inode_num;
    //entry_cp.name_len = name_length_cp;
    entry_cp->file_type = EXT2_FT_REG_FILE;  
    if(case2){
        strncpy(entry_cp->name, alternative_filename, strlen(alternative_filename));
        entry_cp->name_len = strlen(alternative_filename);
    }else{
        strncpy(entry_cp->name, filename, strlen(filename));
        entry_cp->name_len = strlen(filename);
    }
    entry_cp->rec_len = calculate_reclen(entry_cp); 
    if(add_entry(dir_inode_num, entry_cp) < 0){
        return ENOSPC;
    }

    
    //start copying data
    int finish_copying = 0;
    char buf[EXT2_BLOCK_SIZE+1];
    int have_read;
    int block_index_in_inode = 0;
    int indirect = 0;
    int indirect_block_index;
    unsigned int *indirect_block;
    while(!finish_copying){
        if(indirect == 0 && block_index_in_inode == 12){
            indirect = 1;
            indirect_block_index = allocate_dblock();
            if(indirect_block_index == -1){
                return ENOSPC;
            }

            cp_inode->i_block[12] = indirect_block_index;
	    cp_inode->i_blocks += 2;
            indirect_block = (unsigned int *)(disk + EXT2_BLOCK_SIZE*indirect_block_index);
            block_index_in_inode = 0;
        }
        have_read = fread(buf, 1, EXT2_BLOCK_SIZE, fp);
        if(have_read > 0){

            buf[have_read] = '\0';
            int data_block_index = allocate_dblock();
            if(data_block_index == -1){
                return ENOSPC;
            }

            if(indirect){
                indirect_block[block_index_in_inode] = data_block_index;
            }else{
                cp_inode->i_block[block_index_in_inode] = data_block_index;
            }
            char *copy_dest = (char *)(disk + EXT2_BLOCK_SIZE*data_block_index);
            strncpy(copy_dest, buf, strlen(buf));  
            cp_inode->i_size += have_read;
            cp_inode->i_blocks += 2;

            block_index_in_inode += 1;

        }else{
            finish_copying = 1;
        }
    }

    return 0;
}


















