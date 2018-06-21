#include "fs.h"
#include "disk.h"

#include <iostream>
#include <cstdlib>
#include <errno.h>
#include <unistd.h>
#include <string>
#include <vector>

const int FS_MAGIC           = 0xf0f03410;
const int INODES_PER_BLOCK   = 128;
const int POINTERS_PER_INODE = 5;
const int POINTERS_PER_BLOCK = 1024;

const int MOUNT_TRAIT = false;
const int DEBUG_TRAIT = false;

bool MOUNTED = false;

std::vector<int> data_bitmap;
std::vector<int> inode_bitmap;

//DEBUG CLASS
template<bool T>
struct Debug{inline static void msg(std::string s){std::cout << "[DEBUG] " << s << std::endl;}};
template<>
struct Debug<false>{inline static void msg(std::string s){}};

struct fs_superblock {
	int magic;
	int nblocks;
	int ninodeblocks;
	int ninodes;
};

struct fs_inode {
	int isvalid;
	int size;
	int direct[POINTERS_PER_INODE];
	int indirect;
};

union fs_block {
	struct fs_superblock super;
	struct fs_inode inode[INODES_PER_BLOCK];
	int pointers[POINTERS_PER_BLOCK];
	char data[DISK_BLOCK_SIZE];
};

int fs_format()
{
	return 1;
}

void fs_debug()
{
	Debug<DEBUG_TRAIT>::msg("fs_debug: ### BEGIN ###");

	if(!MOUNTED) {
		std::cout << "[ERROR] please mount first!" << std::endl;
		return;
	}

	union fs_block block;

	disk_read(0,block.data);

	if(block.super.magic != FS_MAGIC){
		std::cout << "magic number is invalid" << std::endl;
		return;
	}

	std::cout << "magic number is valid!" << std::endl;
	std::cout << "superblock:" << std::endl;
	std::cout << "\t" << block.super.nblocks << " blocks" << std::endl;
	std::cout << "\t" << block.super.ninodeblocks << " inode blocks" << std::endl;
	std::cout << "\t" << block.super.ninodes << " inodes" << std::endl;

	union fs_block inode;

	for (int i = 0; i < block.super.ninodes; i++) {
		if(inode_bitmap[i] == 1) {
			std::cout << "inode " << i << ":" << std::endl;
			disk_read(i/INODES_PER_BLOCK + 1, inode.data);

			std::cout << "\tsize: " << inode.inode[i%INODES_PER_BLOCK].size <<  " bytes" << std::endl;

			bool have_direct = false;

			for(int j = 0 ; j < POINTERS_PER_INODE; j++){
				if(inode.inode[i%INODES_PER_BLOCK].direct[j] != 0){
					if(!have_direct){std::cout << "\tdirect blocks: "; have_direct = true;}
					std::cout << inode.inode[i%INODES_PER_BLOCK].direct[j] << " ";
				} 
			}

			std::cout << std::endl;
			if(inode.inode[i%INODES_PER_BLOCK].indirect != 0){
				std::cout << "\tindirect block: " << inode.inode[i%INODES_PER_BLOCK].indirect << std::endl;

				union fs_block indirect;
				disk_read(inode.inode[i%INODES_PER_BLOCK].indirect, indirect.data);
				std::cout << "\tindirect data blocks: ";
				for(int j = 0; j < POINTERS_PER_BLOCK; j++){
					if(indirect.pointers[j] != 0){
						std::cout << indirect.pointers[j] << " ";
					}
				}
			}
			std::cout << std::endl;
		}
	}

	Debug<DEBUG_TRAIT>::msg("fs_debug: ### END ###");


}

int fs_mount()
{
	Debug<MOUNT_TRAIT>::msg("fs_mount: ### BEGIN ###");
	union fs_block block;

	disk_read(0,block.data);

	data_bitmap.resize(block.super.nblocks, 0);
	inode_bitmap.resize(block.super.ninodes, 0);

	if(block.super.magic != FS_MAGIC){			
		Debug<MOUNT_TRAIT>::msg("fs_mount: magic number invalid");			 
		return 0;
	}
		
	Debug<MOUNT_TRAIT>::msg("fs_mount: magic number valid");
	
	Debug<MOUNT_TRAIT>::msg("fs_mount: CONSTRUCTING INODE BITMAP");

	union fs_block inode;
	for(int i = 0 ; i < block.super.ninodeblocks ; i++){
		disk_read(i+1, inode.data);
		for(int j = 0; j < INODES_PER_BLOCK; j++){
			if(inode.inode[j].isvalid == 1){
				inode_bitmap[i*INODES_PER_BLOCK + j] = 1;				
				Debug<MOUNT_TRAIT>::msg("fs_mount: inode " + std::to_string(i*INODES_PER_BLOCK + j) + " valid!");				
				} 
			else
				inode_bitmap[i*INODES_PER_BLOCK + j] = 0;
		}
	}

	Debug<MOUNT_TRAIT>::msg("fs_mount: CONSTRUCTING DATA BITMAP");

	for (int i = 0; i < block.super.ninodes; i++) {
		if(inode_bitmap[i] == 1) {
			disk_read(i/INODES_PER_BLOCK + 1, inode.data);
			for(int j = 0 ; j < POINTERS_PER_INODE; j++){
				if(inode.inode[i%INODES_PER_BLOCK].direct[j] != 0){
					data_bitmap[inode.inode[i%INODES_PER_BLOCK].direct[j]] = 1;
					Debug<MOUNT_TRAIT>::msg("fs_mount: inode " + std::to_string(i) + " has direct " + std::to_string(inode.inode[i%INODES_PER_BLOCK].direct[j]) + " being used!");
				} 
			}
			if(inode.inode[i%INODES_PER_BLOCK].indirect != 0){
				Debug<MOUNT_TRAIT>::msg("fs_mount: inode " + std::to_string(i) + " indirect block point to " + std::to_string(inode.inode[i%INODES_PER_BLOCK].indirect) + " block!");
				union fs_block indirect;
				disk_read(inode.inode[i%INODES_PER_BLOCK].indirect, indirect.data);
				for(int j = 0; j < POINTERS_PER_BLOCK; j++){
					if(indirect.pointers[j] != 0){
						data_bitmap[indirect.pointers[j]] = 1;
						Debug<MOUNT_TRAIT>::msg("fs_mount: 	indirect " + std::to_string(indirect.pointers[j]) + " block being pointed!");
					}
				}
			}
		}
	}
	MOUNTED = true;
	Debug<MOUNT_TRAIT>::msg("fs_mount: ### END ###");
	return 1;
}

int fs_create()
{
	return 0;
}

int fs_delete( int inumber )
{
	return 0;
}

int fs_getsize( int inumber )
{
	return -1;
}

int fs_read( int inumber, char *data, int length, int offset )
{
	return 0;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	return 0;
}