//
// Simple FIle System
// Student Name : 이충헌
// Student Number : B811225
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* optional */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
/***********/

#include "sfs_types.h"
#include "sfs_func.h"
#include "sfs_disk.h"
#include "sfs.h"

void dump_directory();

/* BIT operation Macros */
/* a=target variable, b=bit number to act upon 0-n */
#define BIT_SET(a,b) ((a) |= (1<<(b)))
#define BIT_CLEAR(a,b) ((a) &= ~(1<<(b)))
#define BIT_FLIP(a,b) ((a) ^= (1<<(b)))
#define BIT_CHECK(a,b) ((a) & (1<<(b)))

static struct sfs_super spb;	// superblock
static struct sfs_dir sd_cwd = { SFS_NOINO }; // current working directory

void error_message(const char *message, const char *path, int error_code) {
	switch (error_code) {
	case -1:
		printf("%s: %s: No such file or directory\n",message, path); return;
	case -2:
		printf("%s: %s: Not a directory\n",message, path); return;
	case -3:
		printf("%s: %s: Directory full\n",message, path); return;
	case -4:
		printf("%s: %s: No block available\n",message, path); return;
	case -5:
		printf("%s: %s: Not a directory\n",message, path); return;
	case -6:
		printf("%s: %s: Already exists\n",message, path); return;
	case -7:
		printf("%s: %s: Directory not empty\n",message, path); return;
	case -8:
		printf("%s: %s: Invalid argument\n",message, path); return;
	case -9:
		printf("%s: %s: Is a directory\n",message, path); return;
	case -10:
		printf("%s: %s: Is not a file\n",message, path); return;
	default:
		printf("unknown error code\n");
		return;
	}
}

void sfs_mount(const char* path)
{
	if( sd_cwd.sfd_ino !=  SFS_NOINO )
	{
		//umount
		disk_close();
		printf("%s, unmounted\n", spb.sp_volname);
		bzero(&spb, sizeof(struct sfs_super));
		sd_cwd.sfd_ino = SFS_NOINO;
	}

	printf("Disk image: %s\n", path);

	disk_open(path);
	disk_read( &spb, SFS_SB_LOCATION ); // super block 읽어들임 

	printf("Superblock magic: %x\n", spb.sp_magic);

	assert( spb.sp_magic == SFS_MAGIC );
	
	printf("Number of blocks: %d\n", spb.sp_nblocks);
	printf("Volume name: %s\n", spb.sp_volname);
	printf("%s, mounted\n", spb.sp_volname);
	
	sd_cwd.sfd_ino = 1;		//init at root
	sd_cwd.sfd_name[0] = '/';
	sd_cwd.sfd_name[1] = '\0';
}

void sfs_umount() {

	if( sd_cwd.sfd_ino !=  SFS_NOINO )
	{
		//umount
		disk_close();
		printf("%s, unmounted\n", spb.sp_volname);
		bzero(&spb, sizeof(struct sfs_super));
		sd_cwd.sfd_ino = SFS_NOINO;
	}
}
int free_block() {
	int i,bit;
	int start = SFS_MAP_LOCATION;
	int end = SFS_MAP_LOCATION + (spb.sp_nblocks / SFS_BLOCKBITS);
	char bitmap[512];
	int free;
	int error_flag = 0;
	for(start; start <=end; start++){
		disk_read(&bitmap,start); 
		for(i=0; i<SFS_BLOCKSIZE; i++){
			for(bit=0; bit<8; bit++){
			    if(bitmap[i] != -1){
				if((bitmap[i] & (1<<bit)) == 0) // 512 block중 1라인 8비트 봐서  비어있으면 가능!
				{
						error_flag = 1;
						bitmap[i] = bitmap[i] | (1<<bit);
						//printf("%d\n",bitmap[i]);
						disk_write(&bitmap,start);
						free = (start-2) * 4096 + 8*i + bit;
						return free;
				}
			}
			}
		}	
	}
	return error_flag;
}
void sfs_touch(const char* path)
{  //이름같은거 찾고, bitmap 1로 해주기 
	int i,j;
	int free_ino;
	struct sfs_inode si,new;
	disk_read(&si, sd_cwd.sfd_ino);
	struct sfs_dir sd[SFS_DENTRYPERBLOCK];
	for(i=0; i<SFS_NDIRECT; i++){
		disk_read(sd,si.sfi_direct[i]);
		for(j=0; j<SFS_DENTRYPERBLOCK; j++){
			if(strcmp(path,sd[j].sfd_name)==0) {
				error_message("touch", path, -6);
				return;
			}
		}
		if(si.sfi_direct[i] != 0){ 
		for(j=0; j<SFS_DENTRYPERBLOCK; j++){
			if(sd[j].sfd_ino == 0) // 빈블락임 bit map에서 블락찾아서 ino 씌워주면 됨 
			{
				free_ino = free_block();
				if(!free_ino) {
					error_message("touch", path, -4);
        			return;
				}
				sd[j].sfd_ino = free_ino;
				strcpy(sd[j].sfd_name,path);
				disk_write(sd,si.sfi_direct[i]);

				si.sfi_size += sizeof(struct sfs_dir);
				disk_write(&si,sd_cwd.sfd_ino);

				bzero(&new, sizeof(struct sfs_inode)); // initalize sfi_direct[] and sfi_indirect
                new.sfi_type = SFS_TYPE_FILE;

                disk_write(&new, free_ino);
                return;
			}
			}
		}	
		}
	error_message("touch", path, -3);
		
}



void sfs_cd(const char* path)
{
	//current dir에서 모든 파일 서치해서 맞으면 current dir을 그 녀석의 ino로 덮어씌움
    //sd_cwd.sfd_ino = 10;
	int i,j;
	int y,z;
	int flag=0;
	struct sfs_inode si,si2;
	struct sfs_inode check;
	disk_read(&si, sd_cwd.sfd_ino ); 
	struct sfs_dir sd[SFS_DENTRYPERBLOCK],sd2[SFS_DENTRYPERBLOCK];
	if(path != NULL) {
		for(i=0; i < SFS_NDIRECT; i++) {
			if(si.sfi_direct[i] == 0) {
				if (i==SFS_NDIRECT-1 && strcmp(sd[j].sfd_name,path)) {error_message("cd",path,-1); return;}
				else continue;}
			disk_read(sd,si.sfi_direct[i]);
			for(j=0; j < SFS_DENTRYPERBLOCK; j++){
				disk_read(&check,sd[j].sfd_ino);
				if(sd[j].sfd_ino == 0) continue;
				if(strcmp(sd[j].sfd_name,path)==0) {
					if(check.sfi_type == SFS_TYPE_FILE) {error_message("cd",path,-5); return;}
					sd_cwd.sfd_ino = sd[j].sfd_ino;
					return ;
				}
			}
		}
	}
	else {
		sd_cwd.sfd_ino = 1;
	}
	
}

	

void sfs_ls(const char* path)
{
	int i,j;
	int y,z;
	int error = 0;
	struct sfs_inode si,si2;
	struct sfs_inode check;
	disk_read(&si, sd_cwd.sfd_ino); 
	struct sfs_dir sd[SFS_DENTRYPERBLOCK],sd2[SFS_DENTRYPERBLOCK];
	if(path != NULL) {
		for(i=0; i < SFS_NDIRECT; i++) {
			disk_read(sd,si.sfi_direct[i]);
			if(si.sfi_direct[i] == 0) continue;
			for(j=0; j < SFS_DENTRYPERBLOCK; j++){
				disk_read(&si2, sd[j].sfd_ino);
				if(sd[j].sfd_ino == 0) continue;
				if(strcmp(sd[j].sfd_name,path)==0) {
					error = 1;
					if(si2.sfi_type == SFS_TYPE_FILE) {printf("%s\n",path); return;}
					for(y=0; y < SFS_NDIRECT; y ++){
						if(si2.sfi_direct[i] == 0) {printf("\n"); return;}
						disk_read(sd2,si2.sfi_direct[y]);
						for(z=0; z < SFS_DENTRYPERBLOCK; z++){
							disk_read(&check,sd2[z].sfd_ino);
							if(sd2[z].sfd_ino == 0) {printf("\n"); return;}
							printf("%s", sd2[z].sfd_name);
							if(check.sfi_type == SFS_TYPE_DIR) printf("/\t");
							else printf("\t");
						}
					}
				}
			}
		}

	}
	else{ 
	error = 1;
	for(i=0; i < SFS_NDIRECT; i++){
		disk_read( sd, si.sfi_direct[i]);
		if(si.sfi_direct[i] == 0) continue;
		for(j=0; j < SFS_DENTRYPERBLOCK; j++){
				disk_read(&check,sd[j].sfd_ino);
				if(sd[j].sfd_ino == 0) continue;
				printf("%s", sd[j].sfd_name);
				if(check.sfi_type == SFS_TYPE_DIR) printf("/\t");
				else printf("\t");
			}
		}
	}
	if(!error) {error_message("ls",path,-1); return;}
	printf("\n");
}

void sfs_mkdir(const char* org_path) 
{  //si를 search 하고 빈 블락에 block을 넣는다고 생각
   //한블락은 512 생각하자
	int i,j;
	int q;
	int free_ino;
	int bit;
	int blocknum1;
	int blocknum2;
	int next_block;
	char bitmap[512];
	struct sfs_inode si,new;
	disk_read(&si, sd_cwd.sfd_ino);
	struct sfs_dir sd[SFS_DENTRYPERBLOCK];
	struct sfs_dir newdir[SFS_DENTRYPERBLOCK];
	for(i=0; i<SFS_NDIRECT; i++){
		disk_read(sd,si.sfi_direct[i]);
		for(j=0; j<SFS_DENTRYPERBLOCK; j++){
			if(strcmp(org_path,sd[j].sfd_name)==0) {
				error_message("mkdir", org_path, -6);
				return;
			}
		}
		if(si.sfi_direct[i] != 0){ 
		for(j=0; j<SFS_DENTRYPERBLOCK; j++){
			if(sd[j].sfd_ino == 0) // 빈블락임 bit map에서 블락찾아서 ino 씌워주면 됨 
			{
				free_ino = free_block();
				if(!free_ino) {
					error_message("mkdir", org_path, -4);
        			return;
				}
				bzero(&new, SFS_BLOCKSIZE);
				strcpy(sd[j].sfd_name,org_path);
				// . .. 구현
				for(q = 0; q < SFS_DENTRYPERBLOCK; q++){
					newdir[q].sfd_ino = 0;
					strcpy(newdir[q].sfd_name,"");
				}
				sd[j].sfd_ino = free_ino;
				disk_write(sd,si.sfi_direct[i]);
				newdir[0].sfd_ino = free_ino; 
				strncpy(newdir[0].sfd_name,".",SFS_NAMELEN);
				newdir[1].sfd_ino = sd_cwd.sfd_ino;
				strncpy(newdir[1].sfd_name,"..",SFS_NAMELEN);
				next_block = free_block();
				new.sfi_direct[0] = next_block;
				new.sfi_size += 2*sizeof(struct sfs_dir);
				new.sfi_type = SFS_TYPE_DIR;

				disk_write(&newdir,next_block);
				disk_write(&new, free_ino);

				si.sfi_size += sizeof(struct sfs_dir);
				disk_write(&si,sd_cwd.sfd_ino);

                return;
			}
		}	
		}
		else{
				//새로 할당
				free_ino = free_block();
				if(!free_ino) {
					error_message("mkdir", org_path, -4);
        			return;
				}
				si.sfi_direct[i] = free_ino;
				si.sfi_size += sizeof(struct sfs_dir);
				disk_write(&si,sd_cwd.sfd_ino);
				if(!free_ino) {
					error_message("mkdir", org_path, -4);
        			return;
				}
				free_ino = free_block();
				bzero(&new, SFS_BLOCKSIZE); 
				sd[j].sfd_ino = free_ino;
				strcpy(sd[j].sfd_name,org_path);

				// . .. 구현
				for(q = 0; q < SFS_DENTRYPERBLOCK; q++){
					newdir[q].sfd_ino = 0;
					strcpy(newdir[q].sfd_name,"");
				}
				disk_write(sd,si.sfi_direct[i]);
				newdir[0].sfd_ino = free_ino; 
				strncpy(newdir[0].sfd_name,".",SFS_NAMELEN);
				newdir[1].sfd_ino = sd_cwd.sfd_ino;
				strncpy(newdir[1].sfd_name,"..",SFS_NAMELEN);
				next_block = free_block();
				new.sfi_direct[0] = next_block; //이거 bitmap에 써야함
				new.sfi_type = SFS_TYPE_DIR;
				disk_write(&newdir,next_block);
				disk_write(&new, free_ino);

                return;
			}
		}
		error_message("mkdir", org_path, -3);
}
void mk_free_block(int rm_block,int type){ // 0  rm   : 1 rmdir
	int bit;
	int i;
	int blocknum1;
	int blocknum2;
	int blocknum3;
	struct sfs_inode free;
	char bitmap[512];
	blocknum1 = 2 + (rm_block / 4096);
	blocknum2 = (rm_block / 8) % 512;
	bit = rm_block % 8;
	
	disk_read(bitmap,blocknum1);
	bitmap[blocknum2] = bitmap[blocknum2] ^ (1 <<bit);
	disk_read(&free,rm_block);
	if(type == 0){
		for(i=0; i<SFS_NDIRECT; i++){
			if(!free.sfi_direct[i]) break;
			else{
					blocknum2 = (free.sfi_direct[i] / 8) % 512;
					bit = free.sfi_direct[i] % 8;
					bitmap[blocknum2] = bitmap[blocknum2] ^ (1 <<bit);
				}
		}
	}	
	else if(type == 1){
				blocknum2 = (free.sfi_direct[0] / 8) % 512;
				bit = free.sfi_direct[0] % 8;
				bitmap[blocknum2] = bitmap[blocknum2] ^ (1 <<bit);
	}
	
	disk_write(&bitmap,blocknum1);

}
void sfs_rmdir(const char* org_path) 
{
	  if (strcmp(org_path, ".") == 0)
        {
            error_message("rmdir", org_path, -8);
            return;
        }
	//rm이랑 똑같이 org_path랑 같은 이름 찾고 dir인지 아닌지 체크 
	//에러상황 빈 DIR이 아닌경우
	int i,j;
	int x,y;
	int rm_block;
	struct sfs_inode si;
	struct sfs_inode check;
	disk_read(&si, sd_cwd.sfd_ino);
	struct sfs_dir sd[SFS_DENTRYPERBLOCK];
	struct sfs_dir empty[SFS_DENTRYPERBLOCK];
	for(i=0; i<SFS_NDIRECT; i++){
		disk_read(sd,si.sfi_direct[i]);
		if(si.sfi_direct[i] == 0) continue;
		for(j=0; j<SFS_DENTRYPERBLOCK; j++){
		if(strcmp(sd[j].sfd_name,org_path) == 0){
				disk_read(&check,sd[j].sfd_ino);
				if(check.sfi_type == SFS_TYPE_FILE) {
					error_message("rmdir", org_path, -5);
                    return;
				}
				else if(check.sfi_type == SFS_TYPE_DIR){
						for(x=0; x<SFS_NDIRECT; x++){
						disk_read(empty,check.sfi_direct[x]);
						if(check.sfi_direct[x] == 0) continue;
						for(y=0; y<SFS_DENTRYPERBLOCK; y++){
							if(empty[j].sfd_ino != 0){
								error_message("rmdir", org_path, -7);
								return ;
							}
						}
					}
				

					si.sfi_size -= sizeof(struct sfs_dir);
					disk_write(&si,sd_cwd.sfd_ino);

					rm_block = sd[j].sfd_ino;
					mk_free_block(rm_block,1);

					sd[j].sfd_ino = 0;
					strcpy(sd[j].sfd_name,"");  
					disk_write(&sd,si.sfi_direct[i]);

					return ;
				}
			} 
		}
	}
	error_message("rmdir", org_path, -1);
}
void sfs_rm(const char* path) 
{
	//path기준으로 search 한다. 찾아내면 
	//bitmap도 바꿔줘야함
	int i,j;
	int rm_block;
	struct sfs_inode si,check;
	disk_read(&si, sd_cwd.sfd_ino);
	struct sfs_dir sd[SFS_DENTRYPERBLOCK];
	for(i=0; i<SFS_NDIRECT; i++){
		disk_read(sd,si.sfi_direct[i]);
		if(si.sfi_direct[i] == 0) break;
		for(j=0; j<SFS_DENTRYPERBLOCK; j++){
			disk_read(&check,sd[j].sfd_ino);
		if(strcmp(sd[j].sfd_name,path) == 0){
					if(sd[j].sfd_ino != 0){
					if(!strcmp(sd[j].sfd_name,".") || !strcmp(sd[j].sfd_name,"..")) {error_message("rm",path,-8); return;}
					if(check.sfi_type == SFS_TYPE_DIR) {error_message("rm", path, -9);
                    	return;}
					rm_block = sd[j].sfd_ino;
					si.sfi_size -= sizeof(struct sfs_dir);
					sd[j].sfd_ino = 0; 

					disk_write(&sd,si.sfi_direct[i]);
					disk_write(&si,sd_cwd.sfd_ino);
					////bit map free 만들기
					mk_free_block(rm_block,0);

					return ;
				}
			} 
		}
	}
	error_message("rm", path, -1);
}

void sfs_mv(const char* src_name, const char* dst_name) 
{
	//src name 찾고 맞으면 sfd name을 dst_name으로
	int i,j;
	struct sfs_inode si;
	disk_read(&si, sd_cwd.sfd_ino);
	struct sfs_dir sd[SFS_DENTRYPERBLOCK];
	for(i=0; i<SFS_NDIRECT; i++){
		disk_read(sd,si.sfi_direct[i]);
		if(si.sfi_direct[i] == 0) break;
		for(j=0; j<SFS_DENTRYPERBLOCK; j++){
			if(!strcmp(sd[j].sfd_name,dst_name)){
				if(sd[j].sfd_ino !=0){
					if(!strcmp(dst_name,".") || !strcmp(dst_name,"..")) {error_message("mv",dst_name,-8); return;}
					if(!strcmp(src_name,".") || !strcmp(src_name,"..")) {error_message("mv",src_name,-8); return;}
					error_message("mv", dst_name, -6);
                	return;
				}
			}
		}
		for(j=0; j<SFS_DENTRYPERBLOCK; j++){
		if(strcmp(sd[j].sfd_name,src_name)==0 && sd[j].sfd_ino != 0){
			if(!strcmp(dst_name,".") || !strcmp(dst_name,"..")) {error_message("mv",dst_name,-8); return;}
			if(!strcmp(src_name,".") || !strcmp(src_name,"..")) {error_message("mv",src_name,-8); return;}
			strncpy(sd[j].sfd_name,dst_name,SFS_NAMELEN);
			disk_write(sd,si.sfi_direct[i]);

			return ;
			} 
		}
	}
	error_message("mv", src_name, -1);
}

void sfs_cpin(const char* local_path, const char* path) 
{
	printf("Not Implemented\n");
}

void sfs_cpout(const char* local_path, const char* path) 
{
	printf("Not Implemented\n");
}

void dump_inode(struct sfs_inode inode) {
	int i;
	struct sfs_dir dir_entry[SFS_DENTRYPERBLOCK];

	printf("size %d type %d direct ", inode.sfi_size, inode.sfi_type);
	for(i=0; i < SFS_NDIRECT; i++) {
		printf(" %d ", inode.sfi_direct[i]);
	}
	printf(" indirect %d",inode.sfi_indirect);
	printf("\n");

	if (inode.sfi_type == SFS_TYPE_DIR) {
		for(i=0; i < SFS_NDIRECT; i++) {
			if (inode.sfi_direct[i] == 0) break;
			disk_read(dir_entry, inode.sfi_direct[i]);
			dump_directory(dir_entry);
		}
	}

}

void dump_directory(struct sfs_dir dir_entry[]) {
	int i;
	struct sfs_inode inode;
	for(i=0; i < SFS_DENTRYPERBLOCK;i++) {
		printf("%d %s\n",dir_entry[i].sfd_ino, dir_entry[i].sfd_name);
		disk_read(&inode,dir_entry[i].sfd_ino);
		if (inode.sfi_type == SFS_TYPE_FILE) {
			printf("\t");
			dump_inode(inode);
		}
	}
}

void sfs_dump() {
	// dump the current directory structure
	struct sfs_inode c_inode;

	disk_read(&c_inode, sd_cwd.sfd_ino);
	printf("cwd inode %d name %s\n",sd_cwd.sfd_ino,sd_cwd.sfd_name);
	dump_inode(c_inode);
	printf("\n");

}

