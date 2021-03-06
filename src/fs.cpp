#include "fs.h"
#include "disk.h"

#include <iostream>
#include <cstdlib>
#include <errno.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <cmath>
#include <cstring>

const int FS_MAGIC           = 0xf0f03410;
const int INODES_PER_BLOCK   = 128;
const int POINTERS_PER_INODE = 5;
const int POINTERS_PER_BLOCK = 1024;

// const int MOUNT_TRAIT = true;
// const int DEBUG_TRAIT = false;

#define MOUNT_TRAIT false
#define DEBUG_TRAIT false
#define FORMAT_TRAIT false
#define DELETE_TRAIT false
#define READ_TRAIT false
#define WRITE_TRAIT false
#define GETSIZE_TRAIT false
#define DEFRAG_TRAIT false

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
	Debug<FORMAT_TRAIT>::msg("fs_format: ### BEGIN ###");
	if(MOUNTED){
		std::cout << "[ERROR] can't format, already mounted!" << std::endl;
		return 0;
	}

	union fs_block block;
	for(int i = 0; i < DISK_BLOCK_SIZE; i++){ 	//criando um bloco vazio, para utilizar no block.data
		block.data[i] = 0;
	}

	for(int i = 0 ; i < disk_size(); i++) { //limpando o disco
		disk_write(i,block.data);
	}

	Debug<FORMAT_TRAIT>::msg("fs_format: disk cleaned");
	//criando o superbloco
	block.super.magic = FS_MAGIC;
	block.super.nblocks = disk_size();
	block.super.ninodeblocks = std::ceil(block.super.nblocks/10.0);
	block.super.ninodes = block.super.ninodeblocks * INODES_PER_BLOCK;
	disk_write(0,block.data);
	Debug<FORMAT_TRAIT>::msg("fs_format: ### END ###");
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

			std::cout << "\tsize: " << inode.inode[i%INODES_PER_BLOCK].size <<  " bytes";

			bool have_direct = false;

			for(int j = 0 ; j < POINTERS_PER_INODE; j++){
				if(inode.inode[i%INODES_PER_BLOCK].direct[j] != 0){
					if(!have_direct){std::cout << std::endl << "\tdirect blocks: "; have_direct = true;}
					std::cout << inode.inode[i%INODES_PER_BLOCK].direct[j] << " ";
				}
			}

			if(inode.inode[i%INODES_PER_BLOCK].indirect != 0){
				std::cout << std::endl << "\tindirect block: " << inode.inode[i%INODES_PER_BLOCK].indirect << std::endl;

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
				data_bitmap[inode.inode[i%INODES_PER_BLOCK].indirect] = 1;
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
	Debug<MOUNT_TRAIT>::msg("fs_mount: FILLING DATA BITMAP WITH INODES AND SUPER BLOCKS");
	data_bitmap[0] = 1;
	for(int i = 0; i < block.super.ninodeblocks; i++)
		data_bitmap[i+1] = 1;

	MOUNTED = true;

	#if MOUNT_TRAIT == 1
		for(auto i : data_bitmap)
			std::cout << i;
		std::cout << " --> data_bitmap" << std::endl;
		for(auto i : inode_bitmap)
			std::cout << i;
		std::cout << " --> inode_bitmap" << std::endl;
	#endif

	Debug<MOUNT_TRAIT>::msg("fs_mount: ### END ###");
	return 1;
}

int fs_create()
{
	if(!MOUNTED) {
		std::cout << "[ERROR] please mount first!" << std::endl;
		return 0;
	}
	for(int i = 1; i < inode_bitmap.size(); i++) { //começa em 1 pq o inode 0 eh invalido
		if(inode_bitmap[i] == 0) {
			union fs_block inode;
			disk_read(i / INODES_PER_BLOCK + 1,inode.data);	// le o bloco inteiro de inodo onde o inodo ta, pq
			int inode_index = i % INODES_PER_BLOCK;			// o disk_write apenas escreve em 1 bloco de 4KB, e nao
			inode.inode[inode_index].isvalid = 1;	        // em apenas 1 inodo
			inode.inode[inode_index].size = 0;
			for(int j = 0; j < POINTERS_PER_INODE; j++)
				inode.inode[inode_index].direct[j] = 0;
			inode.inode[inode_index].indirect = 0;
			inode_bitmap[i] = 1;							// atualiza o bitmap
			disk_write(i/INODES_PER_BLOCK + 1, inode.data); // escreve o bloco inteiro com o inodo atualizado
			return i;
		}
	}
	std::cout << "[ERROR] no available inode" << std::endl;
	return 0;
}

int fs_delete( int inumber )
{

	Debug<DELETE_TRAIT>::msg("fs_delete: ### BEGIN ###");
	if(!MOUNTED) {
		std::cout << "[ERROR] please mount first!" << std::endl;
		return 0;
	}
	Debug<DELETE_TRAIT>::msg("fs_delete: checking inumber value");
	union fs_block block;
	disk_read(0, block.data);
	if(inumber == 0 || inumber > block.super.ninodes){
		std::cout << "[ERROR] inumber out of bounds!" << std::endl;
		return 0;
	}
	if(inode_bitmap[inumber] == 0){
		std::cout << "[ERROR] inode is not valid!" << std::endl;
		return 0;
	}
	union fs_block inode;
	union fs_block data;
	for(int i = 0; i < DISK_BLOCK_SIZE; i++){ 	//criando um bloco vazio, para utilizar no block.data
		data.data[i] = 0;
	}

	disk_read(inumber/INODES_PER_BLOCK + 1, inode.data);
	int inode_index = inumber % INODES_PER_BLOCK;

	inode.inode[inode_index].isvalid = 0;		//comecando a deletar e liberar os blocos
	inode.inode[inode_index].size = 0;
	Debug<DELETE_TRAIT>::msg("fs_delete: cleaning direct pointers");
	for(int i = 0; i < POINTERS_PER_INODE; i++){	//limpando os ponteiros diretos e seus blocos
		if(inode.inode[inode_index].direct[i] != 0){
			Debug<DELETE_TRAIT>::msg("fs_delete: found direct block " + std::to_string(inode.inode[inode_index].direct[i]) + " at direct pointer " + std::to_string(i));
			disk_write(inode.inode[inode_index].direct[i], data.data);
			data_bitmap[inode.inode[inode_index].direct[i]] = 0;
			inode.inode[inode_index].direct[i] = 0;
		}
	}

	if(inode.inode[inode_index].indirect != 0){
		Debug<DELETE_TRAIT>::msg("fs_delete: inode have indirect block at " + std::to_string(inode.inode[inode_index].indirect));
		union fs_block indirect;
		disk_read(inode.inode[inode_index].indirect, indirect.data);
		for(int i = 0 ; i < POINTERS_PER_BLOCK; i++) {
			if (indirect.pointers[i] != 0) {
				Debug<DELETE_TRAIT>::msg("fs_delete: found indirect block " + std::to_string(indirect.pointers[i]));
				disk_write(indirect.pointers[i], data.data);
				data_bitmap[indirect.pointers[i]] = 0;
				indirect.pointers[i] = 0;
			}
		}
		disk_write(inode.inode[inode_index].indirect, data.data);
		data_bitmap[inode.inode[inode_index].indirect] = 0;
		inode.inode[inode_index].indirect = 0;
	}

	inode_bitmap[inumber] = 0;
	disk_write(inumber/INODES_PER_BLOCK + 1, inode.data);
	Debug<DELETE_TRAIT>::msg("fs_delete: ### END ###");
	return 1;
}

int fs_getsize( int inumber )
{

	Debug<GETSIZE_TRAIT>::msg("fs_getsize: ### BEGIN ###");

 	if(!MOUNTED) {
 		std::cout << "[ERROR] please mount first!" << std::endl;
 		return -1;
 	}

	union fs_block block;
	disk_read(0,block.data);

	int n_blocks = 0;

	if(inumber < block.super.ninodes && inode_bitmap[inumber] != 0){

		union fs_block inode;
		disk_read(inumber/INODES_PER_BLOCK + 1, inode.data);
		for(int i = 0;i < POINTERS_PER_INODE; i++){
			if(inode.inode[inumber%INODES_PER_BLOCK].direct[i] != 0)
				n_blocks++;
		}
		if(inode.inode[inumber%INODES_PER_BLOCK].indirect != 0){
			union fs_block indirect;
			disk_read(inode.inode[inumber%INODES_PER_BLOCK].indirect, indirect.data);
			for(int i = 0; i < POINTERS_PER_BLOCK; i++){
				if(indirect.pointers[i] != 0)
					n_blocks++;
			}
		}
		return n_blocks * 4096;
	}

	Debug<GETSIZE_TRAIT>::msg("fs_getsize: ### END ###");

 	return -1;
}

int fs_read( int inumber, char *data, int length, int offset )
{
	Debug<READ_TRAIT>::msg("fs_read: ### BEGIN ###");
	if(!MOUNTED) {
		std::cout << "[ERROR] please mount first!" << std::endl;
		return 0;
	}
	Debug<READ_TRAIT>::msg("fs_read: checking inumber value");
	union fs_block block;
	disk_read(0, block.data);
	if(inumber == 0 || inumber > block.super.ninodes){
		std::cout << "[ERROR] inumber out of bounds!" << std::endl;
		return 0;
	}
	if(inode_bitmap[inumber] == 0){
		std::cout << "[ERROR] inode is not valid!" << std::endl;
		return 0;
	}

	Debug<READ_TRAIT>::msg("fs_read: begin reading data: \n\tinumber = " + std::to_string(inumber) + "\n\tlength = " + std::to_string(length) + "\n\toffset = " + std::to_string(offset));

	union fs_block inode, data_block;

	disk_read(inumber/INODES_PER_BLOCK + 1, inode.data);
	int inode_index = inumber % INODES_PER_BLOCK;

	int begin_block = offset / DISK_BLOCK_SIZE;
	int begin_byte = offset % DISK_BLOCK_SIZE;

	Debug<READ_TRAIT>::msg("fs_read: begin block = " + std::to_string(begin_block));
	Debug<READ_TRAIT>::msg("fs_read: begin byte = " + std::to_string(begin_byte));
	int length_read, cursor = 0, size_left = inode.inode[inode_index].size - offset;
	Debug<READ_TRAIT>::msg("fs_read: reading from directs");
	//comecando a leitura dos blocos diretos
	for(int i = begin_block; i < POINTERS_PER_INODE; i++) {
		length_read = length - begin_byte;
		if (length_read > DISK_BLOCK_SIZE){
			length_read = DISK_BLOCK_SIZE - begin_byte;
		}

		if(length_read > size_left){
			Debug<READ_TRAIT>::msg("fs_read: trying to read " + std::to_string(length_read) + " but size_left is " + std::to_string(size_left) + " bytes!");
			length_read = size_left;
		}
		length -= length_read;
		size_left -= length_read;
		Debug<READ_TRAIT>::msg("fs_read: reading " + std::to_string(length_read) + " bytes, remaining " + std::to_string(length) + " bytes!");
		#if READ_TRAIT == 1
			if(length < 0){
				std::cout << std::endl;
				Debug<READ_TRAIT>::msg("fs_read: length < 0");
			}
			if(size_left < 0){
				std::cout << std::endl;
				Debug<READ_TRAIT>::msg("fs_read: size_left < 0");
			}
		#endif

		if(inode.inode[inode_index].direct[i] == 0) break;
		disk_read(inode.inode[inode_index].direct[i],data_block.data);
		std::memcpy(&data[cursor],&data_block.data[begin_byte],length_read);
		begin_byte = 0;
		cursor += length_read;
		begin_block++;
		if(length == 0 || size_left == 0) break;
	}
	Debug<READ_TRAIT>::msg("fs_read: finished reading from directs");
	//comecando a leitura dos blocos diretos, caso haja

	if(length > 0 && inode.inode[inode_index].indirect != 0) {
		Debug<READ_TRAIT>::msg("fs_read: reading from indirects");
		union fs_block indirect;
		disk_read(inode.inode[inode_index].indirect, indirect.data);
		for(int i = begin_block - POINTERS_PER_INODE; i < POINTERS_PER_BLOCK; i++) {
			length_read = length - begin_byte;
			if (length_read > DISK_BLOCK_SIZE){
				length_read = DISK_BLOCK_SIZE - begin_byte;
			}
			if(length_read > size_left){
				Debug<READ_TRAIT>::msg("fs_read: trying to read " + std::to_string(length_read) + " but size_left is " + std::to_string(size_left) + " bytes!");
				length_read = size_left;
			}
			length -= length_read;
			size_left -= length_read;
			Debug<READ_TRAIT>::msg("fs_read: reading " + std::to_string(length_read) + " bytes, remaining " + std::to_string(length) + " bytes!");
			#if READ_TRAIT == 1
				if(length < 0){
					std::cout << std::endl;
					Debug<READ_TRAIT>::msg("fs_read: length < 0");
				}
				if(size_left < 0){
					std::cout << std::endl;
					Debug<READ_TRAIT>::msg("fs_read: size_left < 0");
				}
			#endif

			if(indirect.pointers[i] == 0) break;
			disk_read(indirect.pointers[i],data_block.data);
			std::memcpy(&data[cursor],&data_block.data[begin_byte],length_read);
			begin_byte = 0;
			cursor += length_read;
			if(length == 0 || size_left == 0) break;
		}
		Debug<READ_TRAIT>::msg("fs_read: finished reading from indirects");

	}

	Debug<READ_TRAIT>::msg("fs_read: ### END ###");
	return cursor;
}

int search_freeblock(){
	for(int i = 0; i < data_bitmap.size(); i++){
		if(data_bitmap[i] == 0) {
			data_bitmap[i] = 1;
			return i;
		}
	}
	return -1;
}

void update_size(int inumber,int offset, int length){
	union fs_block inode;
	disk_read(inumber/INODES_PER_BLOCK + 1, inode.data);
	int inode_index = inumber % INODES_PER_BLOCK;

	int sizelimit_block = inode.inode[inode_index].size / DISK_BLOCK_SIZE;
	int sizelimit_byte = inode.inode[inode_index].size % DISK_BLOCK_SIZE;

	int end_block = (offset + length) / DISK_BLOCK_SIZE;
	int end_byte = (offset + length) % DISK_BLOCK_SIZE;

	if(sizelimit_block == end_block){
		if(sizelimit_byte < end_byte){
			inode.inode[inode_index].size += end_byte - sizelimit_byte;
			Debug<WRITE_TRAIT>::msg("update_size: equal block inserting = " + std::to_string(end_byte - sizelimit_byte));
			disk_write(inumber/INODES_PER_BLOCK + 1, inode.data);
		}
	} else if(sizelimit_block < end_block){
		inode.inode[inode_index].size += (end_block - sizelimit_block) * DISK_BLOCK_SIZE + (end_byte - sizelimit_byte);
		Debug<WRITE_TRAIT>::msg("update_size: end block greater inserting = " + std::to_string((end_block - sizelimit_block) * DISK_BLOCK_SIZE + (end_byte - sizelimit_byte)));
		disk_write(inumber/INODES_PER_BLOCK + 1, inode.data);
	}
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	Debug<WRITE_TRAIT>::msg("fs_write: ### BEGIN ###");
	if(!MOUNTED) {
		std::cout << "[ERROR] please mount first!" << std::endl;
		return -1;
	}
	if(data == NULL){
		std::cout << "[ERROR] invalid buffer" << std::endl;
	}
	Debug<WRITE_TRAIT>::msg("fs_write: checking inumber value");
	union fs_block block;
	disk_read(0, block.data);
	if(inumber == 0 || inumber > block.super.ninodes){
		std::cout << "[ERROR] inumber out of bounds!" << std::endl;
		return -1;
	}
	if(inode_bitmap[inumber] == 0){
		std::cout << "[ERROR] inode is not valid!" << std::endl;
		return -1;
	}

	Debug<WRITE_TRAIT>::msg("fs_write: begin writing data: \n\tinumber = " + std::to_string(inumber) + "\n\tlength = " + std::to_string(length) + "\n\toffset = " + std::to_string(offset));

	union fs_block inode, data_block;

	disk_read(inumber/INODES_PER_BLOCK + 1, inode.data);
	int inode_index = inumber % INODES_PER_BLOCK;

	int begin_block = offset / DISK_BLOCK_SIZE;
	int begin_byte = offset % DISK_BLOCK_SIZE;

	Debug<WRITE_TRAIT>::msg("fs_write: begin block = " + std::to_string(begin_block));
	Debug<WRITE_TRAIT>::msg("fs_write: begin byte = " + std::to_string(begin_byte));
	int length_write, cursor = 0;
	Debug<WRITE_TRAIT>::msg("fs_write: writing in directs");

	for(int i = begin_block; i < POINTERS_PER_INODE; i++) {
		if(inode.inode[inode_index].direct[i] == 0){
			int free_block = search_freeblock();
			if(free_block == -1) {
				std::cout << "[ERROR] there is no free space anymore" << std::endl;
				update_size(inumber,offset, cursor);
				update_size(inumber,offset, cursor);
				return cursor;
			}
			inode.inode[inode_index].direct[i] = free_block;
			disk_write(inumber/INODES_PER_BLOCK + 1, inode.data);
		}
		length_write = length - begin_byte;
		if (length_write > DISK_BLOCK_SIZE){
			length_write = DISK_BLOCK_SIZE - begin_byte;
		}

		length -= length_write;
		Debug<WRITE_TRAIT>::msg("fs_write: writing " + std::to_string(length_write) + " bytes,");
		#if WRITE_TRAIT == 1
			if(length < 0){
				std::cout << std::endl;
				Debug<WRITE_TRAIT>::msg("fs_write: length < 0");
			}
		#endif
		begin_byte = 0;
		for(int i = 0; i < DISK_BLOCK_SIZE; i++){
			data_block.data[i] = 0;
		}

		std::memcpy(&data_block.data[begin_byte],&data[cursor],length_write);
		disk_write(inode.inode[inode_index].direct[i],data_block.data);
		cursor += length_write;
		begin_block++;
		if(length == 0) break;
	}
	int freespace = 0;
	for(auto i:data_bitmap)
		if(i == 1)
			freespace++;
	if(length > 0) {
		Debug<WRITE_TRAIT>::msg("fs_write: writing in indirects");
		union fs_block indirect;
		if(inode.inode[inode_index].indirect == 0) {
			int free_block = search_freeblock();
			if(free_block == -1 || freespace < 1){
				std::cout << "[ERROR] there is no free space anymore" << std::endl;
				update_size(inumber,offset, cursor);
				return cursor;
			}
			inode.inode[inode_index].indirect = free_block;
			disk_write(inumber/INODES_PER_BLOCK + 1, inode.data);
		}
		disk_read(inode.inode[inode_index].indirect, indirect.data);
		for(int i = begin_block - POINTERS_PER_INODE; i < POINTERS_PER_BLOCK; i++) {
			if(indirect.pointers[i] == 0){
				int free_block = search_freeblock();
				if(free_block == -1) {
					std::cout << "[ERROR] there is no free space anymore" << std::endl;
					update_size(inumber,offset, cursor);
					return cursor;
				}
				indirect.pointers[i] = free_block;
				disk_write(inode.inode[inode_index].indirect, indirect.data);
			}
			length_write = length - begin_byte;
			if (length_write > DISK_BLOCK_SIZE){
				length_write = DISK_BLOCK_SIZE - begin_byte;
			}

			length -= length_write;
			Debug<WRITE_TRAIT>::msg("fs_write: writing " + std::to_string(length_write) + " bytes,");
			#if WRITE_TRAIT == 1
				if(length < 0){
					std::cout << std::endl;
					Debug<WRITE_TRAIT>::msg("fs_write: length < 0");
				}
			#endif
			begin_byte = 0;
			for(int i = 0; i < DISK_BLOCK_SIZE; i++){
				data_block.data[i] = 0;
			}

			std::memcpy(&data_block.data[begin_byte],&data[cursor],length_write);
			disk_write(indirect.pointers[i],data_block.data);
			cursor += length_write;
			begin_block++;
			if(length == 0) break;
		}
	}

	Debug<WRITE_TRAIT>::msg("fs_write: ### END ###");
	update_size(inumber,offset, cursor);
	return cursor;
}

int aux_findid (int iblock){ //Erro nessa funcao

	union fs_block block;
	union fs_block inode;

	disk_read(0,block.data);

	Debug<DEFRAG_TRAIT>::msg("aux_findid: enter in funciton, search inode for append block " + std::to_string(iblock));

	for (int i = 0; i < block.super.ninodes; i++) {
		if(inode_bitmap[i] == 1) {
			disk_read(i/INODES_PER_BLOCK + 1, inode.data);

			Debug<DEFRAG_TRAIT>::msg("aux_findid: enter in block " + std::to_string(i/INODES_PER_BLOCK + 1) + " inode " + std::to_string(i));

			for(int j = 0 ; j < POINTERS_PER_INODE; j++){
				if(inode.inode[i%INODES_PER_BLOCK].direct[j] == iblock){
					Debug<DEFRAG_TRAIT>::msg("aux_findid: find append " + std::to_string(iblock) + " in inode " + std::to_string(i%INODES_PER_BLOCK) + " per direct pointer");
					return i;
				}
			}
			if(inode.inode[i%INODES_PER_BLOCK].indirect != 0){
				if(inode.inode[i%INODES_PER_BLOCK].indirect == iblock){
					return i;
				}
				union fs_block indirect;
				disk_read(inode.inode[i%INODES_PER_BLOCK].indirect, indirect.data);
				for(int j = 0; j < POINTERS_PER_BLOCK; j++){
					if(indirect.pointers[j] == iblock){
						Debug<DEFRAG_TRAIT>::msg("aux_findid: find append" + std::to_string(iblock) + " in inode " + std::to_string(i%INODES_PER_BLOCK) + " per indirect pointer " + std::to_string(j));
						return i;
					}
				}
			}
		}
	}

	return -1;
}

int fs_defrag (){

	Debug<DEFRAG_TRAIT>::msg("fs_defrag: ### BEGIN ###");

	if(!MOUNTED) {
		std::cout << "[ERROR] please mount first!" << std::endl;
		return 0;
	}

	Debug<DEFRAG_TRAIT>::msg("fs_defrag: begin defrag, initializing ready disk");

	union fs_block block;

	disk_read(0,block.data);

	if(block.super.magic != FS_MAGIC){
		std::cout << "[ERROR] magic number is invalid" << std::endl;
		return 0;
	}

	int pos = block.super.ninodeblocks + 1;

	int var_aux = 0;
	union fs_block inode;
	union fs_block indirect;
	union fs_block data;
	union fs_block aux;
	int inodo_change;

	for (int i = 0; i < block.super.ninodes; i++) {

		Debug<DEFRAG_TRAIT>::msg("fs_defrag: verific inode " + std::to_string(i));

		if(inode_bitmap[i] == 1) {

			Debug<DEFRAG_TRAIT>::msg("fs_defrag: inode " + std::to_string(i) + " is used");

			disk_read(i/INODES_PER_BLOCK + 1, inode.data);

			for(int j = 0 ; j < POINTERS_PER_INODE; j++){
				if(inode.inode[i%INODES_PER_BLOCK].direct[j] == pos){
					Debug<DEFRAG_TRAIT>::msg("fs_defrag: inode " + std::to_string(i) + " direct pointer " + std::to_string(j) + " already ordered");

					pos++;
					if(pos == block.super.nblocks){
						return 1;
					}
				}
				else if(inode.inode[i%INODES_PER_BLOCK].direct[j] != 0){

					Debug<DEFRAG_TRAIT>::msg("fs_defrag: inode " + std::to_string(i) + " direct pointer " + std::to_string(j) + " ordering");

					if(data_bitmap[pos] ==  1){

						disk_read(pos, aux.data);
						disk_read(inode.inode[i%INODES_PER_BLOCK].direct[j], data.data);
						disk_write(pos,data.data);
						disk_write(inode.inode[i%INODES_PER_BLOCK].direct[j],aux.data);

						inodo_change = aux_findid(pos);

						disk_read(inodo_change/INODES_PER_BLOCK + 1,aux.data);
						var_aux = 0;
						for(int k = 0;k < POINTERS_PER_INODE; k++){
							if(aux.inode[inodo_change%INODES_PER_BLOCK].direct[k] == pos){
								if(inodo_change/INODES_PER_BLOCK != i/INODES_PER_BLOCK){
									aux.inode[inodo_change%INODES_PER_BLOCK].direct[k] = inode.inode[i%INODES_PER_BLOCK].direct[j];
									disk_write(inodo_change/INODES_PER_BLOCK + 1,aux.data);
								}
								else{
									inode.inode[inodo_change%INODES_PER_BLOCK].direct[k] = inode.inode[i%INODES_PER_BLOCK].direct[j];
								}
								Debug<DEFRAG_TRAIT>::msg("fs_defrag: inode " + std::to_string(i) + " direct pointer " + std::to_string(j) + " change data with inode " + std::to_string(inodo_change) + " direct pointer " + std::to_string(k));
								if(DEFRAG_TRAIT){
									disk_write(i/INODES_PER_BLOCK + 1, inode.data);
									fs_debug();
								}
								var_aux = 1; //if pos refereced for direct pointer
								break;
							}
						}
						if(var_aux == 0){ //if pos refereced for indirect pointer
							if(aux.inode[inodo_change%INODES_PER_BLOCK].indirect == pos){
								if(inodo_change/INODES_PER_BLOCK != i/INODES_PER_BLOCK){
									aux.inode[inodo_change%INODES_PER_BLOCK].indirect = inode.inode[i%INODES_PER_BLOCK].direct[j];
									disk_write(inodo_change/INODES_PER_BLOCK + 1,aux.data);
								}else{
									inode.inode[inodo_change%INODES_PER_BLOCK].indirect = inode.inode[i%INODES_PER_BLOCK].direct[j];
								}
								if(DEFRAG_TRAIT){
									disk_write(i/INODES_PER_BLOCK + 1, inode.data);
									fs_debug();
								}
								Debug<DEFRAG_TRAIT>::msg("fs_defrag: inode " + std::to_string(i) + " direct pointer " + std::to_string(j) + " change data with inode " + std::to_string(inodo_change) + " indirect pointer");
							}
							else{
								var_aux = aux.inode[inodo_change%INODES_PER_BLOCK].indirect;
								disk_read(var_aux, aux.data);
								for(int k = 0;k < POINTERS_PER_BLOCK;k++){
									if(aux.pointers[k] == pos){
										aux.pointers[k] = inode.inode[i%INODES_PER_BLOCK].direct[j];
										disk_write(var_aux, aux.data);
										Debug<DEFRAG_TRAIT>::msg("fs_defrag: inode " + std::to_string(i) + " direct pointer " + std::to_string(j) + " change data with inode " + std::to_string(inodo_change) + " indirect pointer " + std::to_string(k));
										break;
									}
								}
							}
						}
					}
					else{
						Debug<DEFRAG_TRAIT>::msg("fs_defrag: inode " + std::to_string(i) + " direct pointer " + std::to_string(j) + " change data for pos " + std::to_string(pos));

						disk_read(inode.inode[i%INODES_PER_BLOCK].direct[j], data.data);
						disk_write(pos,data.data);
						data_bitmap[inode.inode[i%INODES_PER_BLOCK].direct[j]] = 0;
						data_bitmap[pos] = 1;
					}
					inode.inode[i%INODES_PER_BLOCK].direct[j] = pos;
					Debug<DEFRAG_TRAIT>::msg("fs_defrag: inode " + std::to_string(i) + " direct pointer changed finished");
					pos++;
					if(pos == block.super.nblocks){
						return 1;
					}
				}
				disk_write(i/INODES_PER_BLOCK + 1, inode.data);
			}
			if(inode.inode[i%INODES_PER_BLOCK].indirect == pos){
				Debug<DEFRAG_TRAIT>::msg("fs_defrag: inode " + std::to_string(i) + " indirect pointer " + std::to_string(inode.inode[i%INODES_PER_BLOCK].indirect) + " already ordened");
				pos++;
				if(pos == block.super.nblocks){
					return 1;
				}
			}
			else if(inode.inode[i%INODES_PER_BLOCK].indirect != 0){
				if(data_bitmap[pos] == 1){
					disk_read(pos, aux.data);
					disk_read(inode.inode[i%INODES_PER_BLOCK].indirect, data.data);
					disk_write(pos,data.data);
					disk_write(inode.inode[i%INODES_PER_BLOCK].indirect,aux.data);

					inodo_change = aux_findid(pos);

					Debug<DEFRAG_TRAIT>::msg("fs_defrag: inode " + std::to_string(i) + " indirect pointer change data with inode " + std::to_string(inodo_change));

					disk_read(inodo_change/INODES_PER_BLOCK + 1,aux.data);
					var_aux = 0;
					for(int k = 0;k < POINTERS_PER_INODE; k++){
						if(aux.inode[inodo_change%INODES_PER_BLOCK].direct[k] == pos){
							if(inodo_change/INODES_PER_BLOCK != i/INODES_PER_BLOCK){
								aux.inode[inodo_change%INODES_PER_BLOCK].direct[k] = inode.inode[i%INODES_PER_BLOCK].indirect;
								disk_write(inodo_change/INODES_PER_BLOCK + 1,aux.data);
							}else{
								inode.inode[inodo_change%INODES_PER_BLOCK].direct[k] = inode.inode[i%INODES_PER_BLOCK].indirect;
							}
							Debug<DEFRAG_TRAIT>::msg("fs_defrag: inode " + std::to_string(i) + " indirect pointer change data with inode " + std::to_string(inodo_change) + " direct pointer " + std::to_string(k));
							if(DEFRAG_TRAIT){
								disk_write(i/INODES_PER_BLOCK + 1, inode.data);
								fs_debug();
							}
							var_aux = 1; //if pos refereced for direct pointer
							break;
						}
					}
					if(var_aux == 0){ //if pos refereced for indirect pointer
						if(aux.inode[inodo_change%INODES_PER_BLOCK].indirect == pos){
							if(inodo_change/INODES_PER_BLOCK != i/INODES_PER_BLOCK){
								aux.inode[inodo_change%INODES_PER_BLOCK].indirect = inode.inode[i%INODES_PER_BLOCK].indirect;
								disk_write(inodo_change/INODES_PER_BLOCK + 1,aux.data);
							}
							else{
								inode.inode[inodo_change%INODES_PER_BLOCK].indirect = inode.inode[i%INODES_PER_BLOCK].indirect;
							}
							if(DEFRAG_TRAIT){
								disk_write(i/INODES_PER_BLOCK + 1, inode.data);
								fs_debug();
							}
							Debug<DEFRAG_TRAIT>::msg("fs_defrag: inode " + std::to_string(i) + " indirect pointer change data with inode " + std::to_string(inodo_change) + " indirect pointer");
						}
						else{
							var_aux = aux.inode[inodo_change%INODES_PER_BLOCK].indirect;
							disk_read(var_aux, aux.data);
							for(int k = 0;k < POINTERS_PER_BLOCK;k++){
								if(aux.pointers[k] == pos){
									aux.pointers[k] = inode.inode[i%INODES_PER_BLOCK].indirect;
									disk_write(var_aux, aux.data);
									Debug<DEFRAG_TRAIT>::msg("fs_defrag: inode " + std::to_string(i) + " indirect pointer change data with inode " + std::to_string(inodo_change) + " indirect pointer " + std::to_string(k));
									break;
								}
							}
						}
					}
				}
				else{
					Debug<DEFRAG_TRAIT>::msg("fs_defrag: inode " + std::to_string(i) + " indirect pointer change data for pos " + std::to_string(pos));

					disk_read(inode.inode[i%INODES_PER_BLOCK].indirect, data.data);
					disk_write(pos,data.data);
					data_bitmap[inode.inode[i%INODES_PER_BLOCK].indirect] = 0;
					data_bitmap[pos] = 1;
				}
				inode.inode[i%INODES_PER_BLOCK].indirect = pos;
				Debug<DEFRAG_TRAIT>::msg("fs_defrag: inode " + std::to_string(i) + " indirect pointer changed finished");
				pos++;
				if(pos == block.super.nblocks){
					return 1;
				}
			}
			if(inode.inode[i%INODES_PER_BLOCK].indirect != 0){
				disk_read(inode.inode[i%INODES_PER_BLOCK].indirect, indirect.data);
				for(int j = 0; j < POINTERS_PER_BLOCK; j++){
					//Debug<DEFRAG_TRAIT>::msg("fs_defrag: inode " + std::to_string(i) + " indirect pointer " + std::to_string(j) + " ordering");
					if(indirect.pointers[j] == pos){
						Debug<DEFRAG_TRAIT>::msg("fs_defrag: inode " + std::to_string(i) + " indirect pointer " + std::to_string(indirect.pointers[j]) + " already ordere");
						pos++;
						if(pos == block.super.nblocks){
							return 1;
						}
					}
					else if(indirect.pointers[j] != 0){
						if(data_bitmap[pos] ==  1){

							disk_read(pos, aux.data);
							disk_read(indirect.pointers[j], data.data);
							disk_write(pos,data.data);
							disk_write(indirect.pointers[j],aux.data);

							inodo_change = aux_findid(pos);

							Debug<DEFRAG_TRAIT>::msg("fs_defrag: inode " + std::to_string(i) + " indirect pointer " + std::to_string(indirect.pointers[j]) + " change data with inode " + std::to_string(inodo_change));

							disk_read(inodo_change/INODES_PER_BLOCK + 1,aux.data);
							var_aux = 0;
							for(int k = 0;k < POINTERS_PER_INODE; k++){
								if(aux.inode[inodo_change%INODES_PER_BLOCK].direct[k] == pos){
									if(inodo_change/INODES_PER_BLOCK != i/INODES_PER_BLOCK){
										aux.inode[inodo_change%INODES_PER_BLOCK].direct[k] = indirect.pointers[j];
										disk_write(inodo_change/INODES_PER_BLOCK + 1,aux.data);
									}
									else{
										inode.inode[inodo_change%INODES_PER_BLOCK].direct[k] = indirect.pointers[j];
									}
									Debug<DEFRAG_TRAIT>::msg("fs_defrag: inode " + std::to_string(i) + " indirect pointer " + std::to_string(indirect.pointers[j]) + " change data with inode " + std::to_string(inodo_change) + " direct pointer " + std::to_string(k));
									var_aux = 1; //if pos refereced for direct pointer
									if(DEFRAG_TRAIT){
										disk_write(i/INODES_PER_BLOCK + 1, inode.data);
										fs_debug();
									}
									break;
								}
							}
							if(var_aux == 0){ //if pos refereced for indirect pointer
								if(aux.inode[inodo_change%INODES_PER_BLOCK].indirect == pos){
									if(inodo_change/INODES_PER_BLOCK != i/INODES_PER_BLOCK){
										aux.inode[inodo_change%INODES_PER_BLOCK].indirect = indirect.pointers[j];
										disk_write(inodo_change/INODES_PER_BLOCK + 1,aux.data);
									}
									else{
										inode.inode[inodo_change%INODES_PER_BLOCK].indirect = indirect.pointers[j];
									}
									Debug<DEFRAG_TRAIT>::msg("fs_defrag: inode " + std::to_string(i) + " indirect pointer " + std::to_string(j) + " change data with inode " + std::to_string(inodo_change) + " indirect pointer");
								}
								else{
									var_aux = aux.inode[inodo_change%INODES_PER_BLOCK].indirect;
									disk_read(var_aux, aux.data);
									for(int k = 0;k < POINTERS_PER_BLOCK;k++){
										if(aux.pointers[k] == pos){
											aux.pointers[k] = indirect.pointers[j];
											disk_write(var_aux, aux.data);
											Debug<DEFRAG_TRAIT>::msg("fs_defrag: inode " + std::to_string(i) + " indirect pointer " + std::to_string(indirect.pointers[j]) + " change data with inode " + std::to_string(inodo_change) + " indirect pointer " + std::to_string(k));
											if(DEFRAG_TRAIT){
												disk_write(i/INODES_PER_BLOCK + 1, inode.data);
												fs_debug();
											}
											break;
										}
									}
								}
							}
						}
						else{
							disk_read(indirect.pointers[j], data.data);
							disk_write(pos,data.data);
							data_bitmap[indirect.pointers[j]] = 0;
							data_bitmap[pos] = 1;
						}
						indirect.pointers[j] = pos;
						Debug<DEFRAG_TRAIT>::msg("fs_defrag: inode " + std::to_string(i) + " indirect pointer changed finished");
						pos++;
						if(pos == block.super.nblocks){
							return 1;
						}
					disk_write(inode.inode[i%INODES_PER_BLOCK].indirect, indirect.data);
					}
				}
			}
			disk_write(i/INODES_PER_BLOCK + 1, inode.data);
		}
	}

	Debug<DEFRAG_TRAIT>::msg("fs_defrag: ##### END #####");

	return 1;
}
