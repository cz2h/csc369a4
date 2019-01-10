#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>

/* This is a helper function that helps with assignment 4. */
int search_in_db(char* target_name, int dir_block, char type);
int search_in_inode(char* file_name, int length, struct ext2_inode inode, char type);
int cd(char* path, int i_node_start, int level, char type);
int cd_revised(char* path, char type);
int find_new_block(char* type);
int allocate_inode();
int allocate_dblock();
int compute_level(char *path);
int init_disk(int argc, char** argv);
unsigned short calculate_reclen(struct ext2_dir_entry *entry);
void increase_free_inodes();
void increase_free_blocks();
int sen_in_inode(char* file_name, int length, struct ext2_inode inode, char type);
struct ext2_dir_entry* sen_in_db(char* target_name, int dir_block, char type);
int free_map(char type, int block_num);
int exists_repetitive_dir_entry(int dir_inode_num, char *name, char type);
int add_entry(int dir_inode_num, struct ext2_dir_entry *entry_to_add);
int is_root(char *path);
int num_free_inodes();
int num_free_dblocks();


extern unsigned char *disk;
extern unsigned char *block_bit_map;
extern unsigned char *inode_bit_map;
extern struct ext2_inode *inode_table;
extern int table_size;
extern int dblock_size;
extern int table_start;
extern struct ext2_super_block *sb;
extern struct ext2_group_desc *gd;


/* Count number of free data blocks from bitmap. */
int num_free_dblocks(){
    int res = 0;
    int byte_index, bit_index;
    for(int i = 0; i < dblock_size; i++){
        byte_index = (i)/8;
        bit_index = (i)%8;
        if((block_bit_map[byte_index] & 1<<bit_index) == 0){
            res += 1;
        }
    }
    return res;
}

/* Count number of free inode blocks from bitmap.*/
int num_free_inodes(){
    int res = 0;
    int byte_index, bit_index;
    for(int i = 0; i < table_size; i++){
        byte_index = (i)/8;
        bit_index = (i)%8;
        if((inode_bit_map[byte_index] & 1<<bit_index) == 0){
            res += 1;
        }
    }
    return res;
}

/* Given a dir_block, search for a entry having name target_name. */
int search_in_db(char* target_name, int dir_block, char type){
	int read_count = 0;
	/* While not read the whole data block. */
	while(read_count < EXT2_BLOCK_SIZE){
		struct ext2_dir_entry* cur_entry= (struct ext2_dir_entry*)(disk + EXT2_BLOCK_SIZE * dir_block + read_count);
		char cur_name[cur_entry->name_len + 1];
		memset(cur_name, '0', cur_entry->name_len + 1);
		char cur_type;
		strncpy(cur_name, cur_entry->name, cur_entry->name_len);
		cur_name[cur_entry->name_len] = '\0';
		if(cur_entry->file_type == EXT2_FT_REG_FILE){   
			cur_type = 'f';
		} else if (cur_entry->file_type == EXT2_FT_DIR){
		        cur_type = 'd';
		} else if (cur_entry->file_type == EXT2_FT_SYMLINK){
			cur_type = 'l';
		} else {
			cur_type = '0';
		}
		if(!strcmp(&cur_name[0], target_name) && (type == cur_type)){
			return cur_entry->inode;
		}
		read_count += cur_entry->rec_len;
	}
	/* If not found in current dir block. */
	return -1;
}

/* Given a type and block number, free the correspond bitmap. */
int free_map(char type, int block_num){
        unsigned char* map;
	int bit_index, byte_index;
	if(type == 'd'){
	    map = block_bit_map;
	} else {
	    map = inode_bit_map;
	}
	
	byte_index = (block_num - 1)/8;
        bit_index = (block_num - 1)%8;
        map[byte_index] &= ~(1 << (bit_index));
    return 0;
}

/* Helper function that returns a pointer to the entry with same name and type 
 * in a given dir_block.
 */
struct ext2_dir_entry* sen_in_db(char* target_name, int dir_block, char type){
	int read_count = 0;
	/* While not read the whole data block. */
	while(read_count < EXT2_BLOCK_SIZE){
		struct ext2_dir_entry* cur_entry= (struct ext2_dir_entry*)(disk + EXT2_BLOCK_SIZE * dir_block + read_count);
		char cur_name[cur_entry->name_len + 1];
		memset(cur_name, '0', cur_entry->name_len + 1);
		char cur_type;
		strncpy(cur_name, cur_entry->name, cur_entry->name_len);
		cur_name[cur_entry->name_len] = '\0';
		if(cur_entry->file_type == EXT2_FT_REG_FILE){
			cur_type = 'f';
		} else if (cur_entry->file_type == EXT2_FT_DIR){
		    cur_type = 'd';
		} else if (cur_entry->file_type == EXT2_FT_SYMLINK){
			cur_type = 'l';
		} else {
			cur_type = '0';
		}
		if(!strcmp(&cur_name[0], target_name) && (type == cur_type)){
			/* This line is provided for rm dir usage. To tell how much block is raad
			 * to ensure not reading the entry from next data block.
			 */
			cur_entry->name_len = read_count;
			return cur_entry;
		}
		read_count += cur_entry->rec_len;
	}
	/* If not found in current node. */
	return NULL;
}

/*
 * Given a file name, return the correspond 
 */
int sen_in_inode(char* file_name, int length, struct ext2_inode inode, char type){
    int i, j, res;

	char target_name[length + 2];
	memset(target_name, '\0', length + 2);
	/* length is the index of when the filename ends. */
	strncpy(target_name, file_name, length + 1);
	
	
	/* Search for direct blocks. */
	for(i = 0; i < 12; i++){
		res = search_in_db(target_name, inode.i_block[i], type);
	    if(res > 0){
		    return inode.i_block[i];
	    } else if (inode.i_block[i + 1] == 0){
			return -1;
		}
	}
	/* Search for indirect pointers. */
	if(res < 0){
		int *indirect_block = (int *)(disk + (inode.i_block)[12] * EXT2_BLOCK_SIZE);
		for(i = 0; i < 256; i++){
			if(indirect_block[i] == 0){
				return -1;
			}
			res = search_in_db(target_name, indirect_block[i], type);
		    if(res > 0){
			    return inode.i_block[i];
		    }
		}
	}
	
	/* Search for indirect pointers. */
        if(res < 0){
		int *double_indirect = (int *)(disk + (inode.i_block)[13] * EXT2_BLOCK_SIZE);
		for(i = 0; i < 256; i++){
			int *indirect_block = (int *)(disk + (double_indirect[i]) * EXT2_BLOCK_SIZE);
			for(j = 0; j < 256; j++){
				if(indirect_block[i] == 0){
				    return -1;
			    }
			    res = search_in_db(target_name, indirect_block[i], type);
		        if(res > 0){
			        return inode.i_block[i];
		        }
			}
		}
	}
	
	/* Search for triple indirect pointers. */
	if(res < 0){
		int *tri_indirect = (int *)(disk + ((inode.i_block)[14] * EXT2_BLOCK_SIZE));
		for(i = 0; i < 256; i++){
			int *db_indirect = (int *)(disk + (tri_indirect[i]) * EXT2_BLOCK_SIZE);
			    for(j = 0; j < 256; j++){
					int *indirect_block = (int *)(disk + db_indirect[j] * EXT2_BLOCK_SIZE);
					if(indirect_block[i] == 0){
				        return -1;
			        }
			        res = search_in_db(target_name, indirect_block[i], type);
		            if(res > 0){
			            return inode.i_block[i];
		            }
			    }
		}
	}
	
	/* Go over all but still not find. */
	return -1;
}

/*
 * Given a file name, return the correspond 
 */
int search_in_inode(char* file_name, int length, struct ext2_inode inode, char type){
	int i, j, res;
	char target_name[length + 1]; 
	memset(target_name, '\0', strlen(target_name));
	strncpy(target_name, file_name, length);
	
	
	/* Search for direct blocks. */
	for(i = 0; i < 12; i++){
	    res = search_in_db(target_name, inode.i_block[i], type);
	    if(res > 0){
	            return res;
	    } else if (inode.i_block[i + 1] == 0){
		    return -1;
	    }
	}
	/* Search for indirect pointers. */
	if(res < 0){
		int *indirect_block = (int *)(disk + (inode.i_block)[12] * EXT2_BLOCK_SIZE);
		for(i = 0; i < 256; i++){
			if(indirect_block[i] == 0){
				return -1;
			}
			res = search_in_db(target_name, indirect_block[i], type);
		
		        if(res > 0){
			        return res;
		        }
		}
	}
	
	/* Search for double indirect pointers. */
        if(res < 0){
		int *double_indirect = (int *)(disk + (inode.i_block)[13] * EXT2_BLOCK_SIZE);
		for(i = 0; i < 256; i++){
			int *indirect_block = (int *)(disk + (double_indirect[i]) * EXT2_BLOCK_SIZE);
			for(j = 0; j < 256; j++){
				if(indirect_block[i] == 0){
				    return -1;
			        }
			        res = search_in_db(target_name, indirect_block[i], type);
		
		                if(res > 0){
			                return res;
		                }
			}
		}
	}
	
	/* Search for triple indirect pointers. */
	if(res < 0){
		int *tri_indirect = (int *)(disk + ((inode.i_block)[14] * EXT2_BLOCK_SIZE));
		for(i = 0; i < 256; i++){
			int *db_indirect = (int *)(disk + (tri_indirect[i]) * EXT2_BLOCK_SIZE);
			    for(j = 0; j < 256; j++){
					int *indirect_block = (int *)(disk + db_indirect[j] * EXT2_BLOCK_SIZE);
					if(indirect_block[i] == 0){
				                return -1;
			                }
			                res = search_in_db(target_name, indirect_block[i], type);
		
		                        if(res > 0){
			                        return res;
		                        }
			    }
		}
	}
	
	/* Go over all but still not find. */
	return -1;
}

/*
 * A helper file for changing directory.
 * If such directory exists, return the inode number of the last directory.
 */
int cd(char* path, int i_node_start, int level, char type){
	/* The root inode of all. */
	int end = 0;
	char* target_dir = path;
	int cur_level = 0;
	int i, byte_index, bit_index, target_inode;
	while( end < strlen(target_dir) && (path)[end + 1] != '/'){
		end += 1;
	}
	if(level == 1 || level == 0){
		return  search_in_inode(target_dir, end + 1, inode_table[1], type);
	}
	target_inode = search_in_inode(target_dir, end + 1, inode_table[1], 'd');
	target_dir = &target_dir[end + 2];
	if(target_inode > 0){
		cur_level += 1;
		i = target_inode;
	} else {
		i = 11;   
	}
	while( end < strlen(target_dir) - 1 && (path)[end + 1] != '/'){
		end += 1;
	}
	while(cur_level < level && i < table_size){
                byte_index = (i - 1)/8;
		bit_index = (i - 1)%8;
		if(inode_bit_map[byte_index] & 1<<bit_index){
		        if(S_ISDIR(inode_table[i - 1].i_mode)){  
				if(cur_level == level -1){
			                target_inode = search_in_inode(target_dir, end + 1, inode_table[i - 1], type);
		                } else {
					/* Still need to go over some internal subdirectories. */
					target_inode = search_in_inode(target_dir, end + 1, inode_table[i - 1], 'd');
			        }
			        /* Case when some directory is missing. */
		                if(target_inode < 0){
				        i += 1;
			        } else {
				        /* Then find the next subdirectory. */
     		                        cur_level += 1;
	        	                target_dir = &target_dir[end + 2];
		                        end = 0;
				        if(cur_level < level){
    	                                         while( end < strlen(target_dir) && (path)[end + 1] != '/'){
		                                          end += 1;
	                                         }
		                        }
		                        i = target_inode;
			       }
			} else {
				i += 1;
			}
	        } else {
		        i += 1;
		}
		
	}
	/* Case does not find all the subdirectories. */
	if(level != cur_level){
		return -1;
	}
	return target_inode;
}

/*
 * return 1 if given path refers to root
 */
int is_root(char *path){
    if(strlen(path) == 0){
        return 1;
    }
    int head = 0;
    int count = 0;
    while(head < strlen(path)){
        if(head == 0){
            if(path[head] == '.' || path[head] == '/'){
                count += 1;
            }else{
                break;
            }
        }else{
            if(path[head] == '/'){
                count += 1;     
            }else{
                break;
            }
        }
        head++;
    }
    if(count == strlen(path)){
        return 1;
    }else{
        return 0;
    }
}


/*
 * return inode number on success, -1 if not found (path is invalid)
 * if type == 'f', then search for file;
 * if type == 'd', then search for directory;
 */
int cd_revised(char* path, char type){
        /*if(strlen(path) == 0 || compute_level(path) == 0){
            return -ENOENT;
        }*/
        if(is_root(path)){
            return EXT2_ROOT_INO;
        }        
	/* The root inode of all. */
	int head = 0; //keep track of where current dir starts (used to process subdirs)
        int length = 1; //length of current dir
	int cur_level = 0; //how many levels have we processed?
        int total_level = compute_level(path); 
        char *target_dir = path; 
        int target_inode = EXT2_ROOT_INO;
        while(cur_level < total_level){
                while(head < strlen(path) && (path)[head] == '/'){
                    head++;
                }
                target_dir = &(path[head]);

	       	while( head < strlen(path) - 1 && (path)[head + 1] != '/'){
			head += 1;
		        length++;
		}
                if(cur_level == total_level - 1){
		    target_inode = search_in_inode(target_dir, length, inode_table[target_inode-1], type);
                }else{
                    target_inode = search_in_inode(target_dir, length, inode_table[target_inode-1], 'd');
                }
		if(target_inode > 0){
			cur_level += 1;
		} else {
			return -ENOENT;
		}
		head += 1;
		length = 1;
        }
        return target_inode;
}

/*
 *return 1 if exists repetitive (same name and same type) dir entry in dir with inode number dir_inode_num 
 */
int exists_repetitive_dir_entry(int dir_inode_num, char *name, char type){
    struct ext2_inode *dir_inode = (struct ext2_inode *)(&(inode_table[dir_inode_num-1]));
    int i = 0;
    int dir_block_index;
    int found = 0;
    int res;
    while(dir_inode->i_block[i] != 0 && i <= 11){
        dir_block_index = dir_inode->i_block[i];
        res = search_in_db(name, dir_block_index, type);
        if(res > 0){
            found = 1;
            break;
        }
        i++;
    }
    if(found){
        return 1;
    }else{
        return 0;
    }
}



/*
 *add a directory entry to a directory inode, return 0 on success, return -ENOSPC on failure
 */
int add_entry(int dir_inode_num, struct ext2_dir_entry *entry_to_add){
    struct ext2_inode *dir_inode = (struct ext2_inode *)(&(inode_table[dir_inode_num-1]));    
    int block_index = -1;
    int i_block_index;
    for(i_block_index = 0; i_block_index < 12; i_block_index++){
        if(dir_inode->i_block[i_block_index+1] == 0){
            block_index = dir_inode->i_block[i_block_index];
            break;
        }
    }
    if(block_index == -1){
        return -ENOSPC;
    }   
    struct ext2_dir_entry *cur_entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE*block_index);
    int cur_rec = 0;
    int entry_inserted = 0;
    while(!entry_inserted){
	    while(cur_rec + cur_entry->rec_len < EXT2_BLOCK_SIZE){   //each dir at least has 2 entries: "." and ".."
		cur_rec += cur_entry->rec_len;
		cur_entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE*block_index + cur_rec);   
	    }
            if((EXT2_BLOCK_SIZE - cur_rec) - calculate_reclen(cur_entry) < entry_to_add->rec_len){
                if(i_block_index >= 11){
                    return -ENOSPC;
                }else{
                    block_index = allocate_dblock(); 
                    if(block_index == -1){
                        return -ENOSPC;  
                    }
                    i_block_index += 1;
                    dir_inode->i_block[i_block_index] = block_index;
                    cur_rec = 0;
                    cur_entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE*block_index + cur_rec);
                    cur_entry->inode = entry_to_add->inode;
                    cur_entry->name_len = entry_to_add->name_len;
                    cur_entry->file_type = entry_to_add->file_type;
                    cur_entry->rec_len = EXT2_BLOCK_SIZE - cur_rec;
                    strncpy(cur_entry->name, entry_to_add->name, cur_entry->name_len);
                    entry_inserted = 1;
                }                
            }else{
                    cur_entry->rec_len = calculate_reclen(cur_entry);
                    cur_rec += cur_entry->rec_len;
                    cur_entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE*block_index + cur_rec);
                    cur_entry->inode = entry_to_add->inode;
                    cur_entry->name_len = entry_to_add->name_len;
                    cur_entry->file_type = entry_to_add->file_type;
                    cur_entry->rec_len = EXT2_BLOCK_SIZE - cur_rec;
                    strncpy(cur_entry->name, entry_to_add->name, cur_entry->name_len);
                    entry_inserted = 1;
            }
    }
    return 0;
}

/* Return the index of a free block. (return 12 if block 13 is free)*/
int find_new_block(char* type){ 
        int found = 0;
	unsigned char* map;
	int range, i, bit_index, byte_index;
	if(*type == 'i'){
		map = inode_bit_map;
		range = table_size;
		i = 12;
	} else if(*type == 'd'){
		map = block_bit_map;
		range = dblock_size;
		i = 9;
	} else {
		printf("Usage: i/d\n");
		exit(-1);
	}
	while(i < range){
	        byte_index = (i - 1)/8;
		bit_index = (i - 1)%8;
		if(map[byte_index] & 1<<bit_index){
			i += 1;
		} else {
			map[byte_index] |= (1<<bit_index);
                        found = 1;
			break;
		}
	}
	
	/* Return the index of the free block. */
	if(found){
                return i;
        }else{
                return -1;
        }
}

/* Find a useable inode block, and initialize it. 
 * return the number of alloctaed block.
 */
int allocate_inode(){
	char *type = "i";
	/* Note new_block is the block number, not the index. */
	int new_block = find_new_block(type);
	if(new_block == -1){
            return -1;
        }
	/* Set all the values to 0. */
	memset(&inode_table[new_block - 1], 0, sizeof(struct ext2_inode));
	gd->bg_free_inodes_count -= 1;  
        sb->s_free_inodes_count -= 1; 
	return new_block;
}

/* Find a useable data block and initialize it.
 * return the number of allocated block(not the index.)
 */
int allocate_dblock(){
	char *type = "d";
	int new_block = find_new_block(type);
	if(new_block == -1){
            return -1;
        }
	/* Set all the values to 0. */
	memset(disk + EXT2_BLOCK_SIZE * new_block, 0, EXT2_BLOCK_SIZE);
	gd->bg_free_blocks_count -= 1;  
        sb->s_free_blocks_count -= 1;  
	return new_block;
}

/* Increase number of free blocks. */
void increase_free_blocks(){
    sb->s_free_blocks_count += 1;
	gd->bg_free_blocks_count += 1;	
}

void increase_free_inodes(){
    sb->s_free_inodes_count += 1;
	gd->bg_free_inodes_count += 1;
}

/* Given a path, compute the number of levels of the given path.
 * note that path should be in this form a/b (retrive the pre , back trailing '/'s).
 */
int compute_level(char *path){     
	int head = 0;
	int res = 0;
        int new_level_flag = 1;
        int stop = 0;
	while(head < strlen(path)){		
		while(path[head] == '/'){
                        new_level_flag = 1;
                        head += 1; 
                        if(head >= strlen(path)){
                                stop = 1;
                        }
                }
                if(stop){
                        break;
                }
	        if(new_level_flag){
                        res += 1;
                        new_level_flag = 0;
                }
                head ++;
        }
	return res;
}

/* Initialize global variables. */
int init_disk(int argc, char** argv){
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
    return 0;
	
}

/*Calculate the rec_len of a struct dir_entry (rec_len is always a multiple of 4)*/
unsigned short calculate_reclen(struct ext2_dir_entry *entry){
    unsigned short res = 0;
    res += sizeof(unsigned int);
    res += sizeof(unsigned short);
    res += sizeof(unsigned char);
    res += sizeof(unsigned char);
    res += entry->name_len;
    if(res%4 == 0){
        return res;
    }else{
        return 4*(res/4 + 1);
    }
}





