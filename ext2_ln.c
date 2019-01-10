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
    //check arguments
    if( (argc != 4 && argc != 5) || (argc == 5 && strcmp(argv[2], "-s") != 0)){
        printf("Usage: ./etx2_ln <image file name> <source path> <dest path> OR\n");
        printf(" ./etx2_ln <image file name> -s <source path> <dest path>\n");
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

  
    if(argc == 4){ //hard link
        char dest_hard[strlen(argv[3])+1];
        strncpy(dest_hard, argv[3], strlen(argv[3]));
        dest_hard[strlen(argv[3])] = '\0';
        char source_hard[strlen(argv[2])+1];
        strncpy(source_hard, argv[2], strlen(argv[2]));
        source_hard[strlen(argv[2])] = '\0';
        int linkname_hard_length = 0;
        int tail = strlen(dest_hard)-1;
        while(dest_hard[tail] == '/'){
            tail--;
        }
        while(dest_hard[tail] != '/'){
            linkname_hard_length++;
            tail--;
        }
        char linkname_hard[linkname_hard_length+1];
        strncpy(linkname_hard, &(dest_hard[tail+1]), linkname_hard_length);
        linkname_hard[linkname_hard_length] = '\0';
        dest_hard[tail] = '\0';

        //check if the source given is a dir
        if(cd_revised(source_hard, 'd') > 0){
            return EISDIR;
        }

        int inode_num = cd_revised(source_hard, 'f');
        int dir_inode_num = cd_revised(dest_hard, 'd');
        if(inode_num < 0 || dir_inode_num < 0){
            return ENOENT;
        }

        if(exists_repetitive_dir_entry(dir_inode_num, linkname_hard, 'f')){
            return EEXIST;
        }

        //add an entry to inode numbered dir_inode_num
        struct ext2_dir_entry *hard_link = malloc(sizeof(struct ext2_dir_entry));
        hard_link->inode = inode_num;
        hard_link->name_len = strlen(linkname_hard);
        hard_link->file_type = EXT2_FT_REG_FILE;
        hard_link->rec_len = calculate_reclen(hard_link);
        strncpy(hard_link->name, linkname_hard, strlen(linkname_hard));
        if(add_entry(dir_inode_num, hard_link) < 0){
            return ENOSPC;
        }
        free(hard_link);

        //increase i_links_count in source by 1
        inode_table[inode_num-1].i_links_count += 1;
      

    }else{  //symbolic link 
        char dest_soft[strlen(argv[4])+1];
        strncpy(dest_soft, argv[4], strlen(argv[4]));
        dest_soft[strlen(argv[4])] = '\0';
        char source_soft[strlen(argv[3])+1];
        strncpy(source_soft, argv[3], strlen(argv[3]));
        source_soft[strlen(argv[3])] = '\0';
        int linkname_soft_length = 0;
        int tail = strlen(dest_soft)-1;
        while(dest_soft[tail] == '/'){
            tail--;
        }
        while(dest_soft[tail] != '/'){
            linkname_soft_length++;
            tail--;
        }
        char linkname_soft[linkname_soft_length+1];
        strncpy(linkname_soft, &(dest_soft[tail+1]), linkname_soft_length);
        linkname_soft[linkname_soft_length] = '\0';
        char dest_without_linkname[tail+1];
        strncpy(dest_without_linkname, dest_soft, tail);
        dest_without_linkname[tail] = '\0';

        //check if source path is valid
        if(cd_revised(source_soft, 'd') < 0 && cd_revised(source_soft, 'f') < 0 && cd_revised(source_soft, 'l') < 0){
            return ENOENT;       
        }

        //check if link name exists: use search_in_inode
        int directory_inode_number_soft = cd_revised(dest_without_linkname, 'd');
        if(directory_inode_number_soft < 0){
            return ENOENT;
        }
        if(search_in_inode(linkname_soft, strlen(linkname_soft), inode_table[directory_inode_number_soft-1], 'l') > 0){
            return EEXIST;
        }

        //create inode
        int soft_link_inode_num = allocate_inode();
        if(soft_link_inode_num < 0){
            return ENOSPC;
        }
        struct ext2_inode *soft_link_inode = (struct ext2_inode *)(&(inode_table[soft_link_inode_num-1]));
        soft_link_inode->i_mode = EXT2_S_IFLNK;
        soft_link_inode->i_links_count = 1;
 
        //create and insert dir entry
        struct ext2_dir_entry *soft_link = malloc(sizeof(struct ext2_dir_entry));
        soft_link->inode = soft_link_inode_num;
        soft_link->file_type = EXT2_FT_SYMLINK;
        soft_link->name_len = strlen(linkname_soft);
        soft_link->rec_len = calculate_reclen(soft_link);
        strncpy(soft_link->name, linkname_soft, strlen(linkname_soft));
        if(add_entry(directory_inode_number_soft, soft_link) < 0){
            return ENOSPC;
        }

        //copy path of source file as data
        int block_index = allocate_dblock();
        if(block_index < 0){
            return ENOSPC;
        }       
        soft_link_inode->i_block[0] = block_index;
        char *copy_dest = (char *)(disk + EXT2_BLOCK_SIZE*block_index);
        strncpy(copy_dest, source_soft, strlen(source_soft));
        copy_dest[strlen(source_soft)] = '\0';
        soft_link_inode->i_size += strlen(source_soft);
        soft_link_inode->i_blocks += 2;
    }




    return 0;
}



