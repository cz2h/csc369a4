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


/* Need to check if all dirs on path exists. */
int check_path(char* path){
    /* Modify the origin input. */
    if(path[0] == '.' && path[1] == '/'){
        path = &path[2];
    } else if(path[0] == '/'){
        path = &path[1];
    }
    int length = strlen(path);
    char path_to_check[length + 1];
    memset(path_to_check, '\0', sizeof(path_to_check));
    int level, res;
    strncpy(path_to_check, path, length);
    
    /* Remove the trailing back slashes*/
    int finished = 0;
    length = length - 1;
    while(! finished){
        if(path_to_check[length] == '/'){
            path_to_check[length] = '\0';
            length -= 1;
        } else {
            finished = 1;
        }
    }
    
    length = strlen(path_to_check) - 1;
    
    /* Retrive the last directory name to be created. */
    /* Special case when want to create a dir at the root.*/
    finished = 0;
    while( !finished && length >= 1){
        if(path_to_check[length] == '/'){
            length -= 1;
            finished = 1;
        } else {
            length -= 1;
        }
    }
    /* If length is 0, want to return the inode number of the root.*/
    if(length == 0){
        return 2;
    }
    level = compute_level(path_to_check) - 1;
    res = cd(&(path_to_check[0]), gd->bg_inode_table, level, 'd');
    return res;
}

/* A function to check if there are available blocks to allocate new dir. */
int check_space(){
    if(sb->s_free_blocks_count == 0 || sb->s_free_inodes_count == 0){
        return -1;
    }   
    return 0;
}

/* A function checks if there already exists a file in here. */
int check_duplicate(char* path){
    if(path[0] == '.' && path[1] == '/'){
        path = &path[2];
    } else if (path[0] == '/'){
        path = &path[1];
    }
    int length = strlen(path);
    int level, res;
    char path_to_check[length];
    memset(path_to_check, '\0', sizeof(path_to_check));
    strncpy(path_to_check, path, length);

    /* Remove the trailing back slashes*/
    int finished = 0;
    length = length - 1;
    while(! finished && length >= 1){
        if(path_to_check[length] == '/'){
            path_to_check[length] = '\0';
            length -= 1;
        } else {
            finished = 1;
        }
    }
    
    level = compute_level(path_to_check);
    res = cd(&(path_to_check[0]), gd->bg_inode_table, level, 'd');
    return res;
}

/* A helper function, given a path, return the name of the last not null string.*/
void get_name(char** path){
    int finished = 0;
    int length = strlen(*path) - 1;
    
    /* Retrive the following '/' */
    while(! finished){
        if((*path)[length] == '/'){
            (*path)[length] = '\0';
            length -= 1;
        } else {
            finished = 1;
        }
    }
    
    /* Retrive the last directory name to be created. */
    finished = 0;
    while( !finished && length >= 0){
        if((*path)[length] == '/'){
            (*path) = &(*path)[length + 1];
            finished = 1;
        } else {
            length -= 1;
        }
    }
}


/* Given a parent node, allocate new inode, data block, and modify the original disk. */
int allocate_directory(int parent, char* path){
    /* First allocate block. */
    /* Allocate a new data block. */
    get_name(&path);
    int new_inode = allocate_inode();
    int new_db = allocate_dblock();
    int i = 1;
    struct ext2_inode* parent_inode = &(inode_table[parent]);
    while(i < parent_inode->i_blocks/2){
        i += 1;
    }
    /* The dir_entry for the parent. */
    struct ext2_dir_entry* parent_dblock = (struct ext2_dir_entry*)(disk + EXT2_BLOCK_SIZE * parent_inode->i_block[i - 1]);
    
    /* The newly allocated blocks. */
    struct ext2_inode* cur_inode = &(inode_table[new_inode - 1]);
    struct ext2_dir_entry* new_block = (struct ext2_dir_entry*)(disk + EXT2_BLOCK_SIZE * (new_db));
    // Zero out the data block when initialized.
    memset(new_block, '0', EXT2_BLOCK_SIZE);
    /* 1. Update new inode. */
    cur_inode->i_mode = EXT2_S_IFDIR;   
    (cur_inode->i_block)[0] = new_db;
    cur_inode->i_blocks = 2;
    cur_inode->i_size = 1024;
    cur_inode->i_links_count = 2;
    
    /* 2. Update parent data block. */
    struct ext2_dir_entry* entry_dir = malloc(sizeof(struct ext2_dir_entry));
    entry_dir->inode = new_inode;
    entry_dir->file_type = EXT2_FT_DIR;
    entry_dir->name_len = strlen(path);
    strncpy(entry_dir->name, path, strlen(path));
    // Want to make the name size of four.
    int namelen = strlen(path);
    while(namelen % 4 != 0){
        namelen += 1;
        entry_dir->name[namelen] = '\0';
    }
    entry_dir->rec_len = calculate_reclen(entry_dir);
    if(add_entry(parent + 1, entry_dir) < 0){
        int new_parent_dblock = allocate_dblock();
        parent_inode->i_blocks += 2;
        parent_inode->i_block[i] = new_parent_dblock;
        parent_dblock = (struct ext2_dir_entry*)(disk + EXT2_BLOCK_SIZE * parent_inode->i_block[i]);
        strncpy((char* )parent_dblock, (char*)entry_dir, calculate_reclen(entry_dir));
        parent_dblock->rec_len = 1024;
    }
    
    /* 3. Update the new data block. */
    new_block -> inode = new_inode;
    new_block -> rec_len = 12;
    new_block -> name_len = 1;
    new_block -> file_type = EXT2_FT_DIR;
    strncpy(new_block->name, ".\0\0\0", 4);
    
    struct ext2_dir_entry *parent_ent = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * new_db + 12);
    parent_ent -> inode = parent + 1;
    parent_ent -> rec_len = 1012;
    parent_ent -> name_len = 2;
    parent_ent -> file_type = EXT2_FT_DIR;
    strncpy(parent_ent->name, "..\0\0", 4);
    
    /* 4. Update the number of links in parent. */
    parent_inode->i_links_count += 1;
    
    /* 5. Update the directory count. */
    gd->bg_used_dirs_count += 1;
    return 0;
}

/* A helper function that checks whether the input is valid. */
int check_syntax(char* path){
    if(path[0] == '.' && path[1] == '/'){
        path = &path[2];
    } else if(path[0] == '/'){
        path = &path[1];
    }
    int length = strlen(path);
    char path_to_check[length + 1];
    memset(path_to_check, '\0', sizeof(path_to_check));
    strncpy(path_to_check, path, length);
    if(strlen(path_to_check) == 0){
        return -1;
    }
    length = strlen(path_to_check);
    /* Then check if the name is too long. */
    int finished = 0;
    length = length - 1;
    while(! finished){
        if(path_to_check[length] == '/'){
            path_to_check[length] = '\0';
            length -= 1;
        } else {
            finished = 1;
        }
    }

    length = strlen(path_to_check) - 1;
    finished = 0;
    while( !finished && length >= 1){
        if(path_to_check[length] == '/'){
            path_to_check[length] = '\0';
            length -= 1;
            finished = 1;
        } else {
            length -= 1;
        }
    }
    if(strlen(path_to_check) - length + 1 > EXT2_NAME_LEN){
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    int parent;
    if(argc != 3){
        printf("Usage:<disk> <absolute path to the new directory>\n");
        exit(1);
    }
    char* path = argv[2];
    init_disk(argc, argv);
    /* Check if input is valid. */
    if(check_syntax(path) < 0){
        return -EINVAL;
    }
    
    /* Call cd to check if path is available. */
    parent = check_path(path);
    if(parent == -1){
        return -ENOENT;
    }

    /* Check if all nodes are being used */
    if(check_space() == -1){
        return -ENOSPC;
    }
    
    if(check_duplicate(path) != -1){
        return -EEXIST;
    }
    
    if(allocate_directory(parent - 1, path) < 0){
        exit(1);
    }
    
    
    return 0;
}
