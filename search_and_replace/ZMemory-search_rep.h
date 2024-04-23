#ifndef _ANDROIDMEMDEBUG_H_
#define _ANDROIDMEMDEBUG_H_
#include <stdio.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <iostream>
#include "AndroidMemDebug.cpp"

enum{
	DWORD,
	FLOAT,
	BYTE,
	WORD,
	QWORD,
	XOR,
	DOUBLE,
};

enum{
	Mem_Auto,
	Mem_A,
	Mem_Ca,
	Mem_Cd,
	Mem_Cb,
	Mem_Jh,
	Mem_J,
	Mem_S,
	Mem_V,
	Mem_Xa,
	Mem_Xs,
	Mem_As,
	Mem_B,
	Mem_O,
};

struct MemPage
{
	long start;
	long end;
	char flags[8];
	char name[128];
	void *buf = NULL;
};

struct AddressData
{
	long *addrs = NULL;
	int count = 0;
};

size_t judgSize(int type){
	switch (type)
	{
	case DWORD:
	case FLOAT:
	case XOR:
		return 4;
	case BYTE:
		return sizeof(char);
	case WORD:
		return sizeof(short);
	case QWORD:
		return sizeof(long);
	case DOUBLE:
		return sizeof(double);
	}
	return 4;
}


int memContrast(char *str){
	if (strlen(str) == 0)
		return Mem_A;
	if (strstr(str, "/dev/ashmem/") != NULL)
		return Mem_As;
	if (strstr(str, "/system/fonts/") != NULL)
		return Mem_B;
	if (strstr(str, "/data/app/") != NULL)
		return Mem_Xa;
	if (strstr(str, "/system/framework/") != NULL)
		return Mem_Xs;
	if (strcmp(str, "[anon:libc_malloc]") == 0)
		return Mem_Ca;
	if (strstr(str, ":bss") != NULL)
		return Mem_Cb;
	if (strstr(str, "/data/data/") != NULL)
		return Mem_Cd;
	if (strstr(str, "[anon:dalvik") != NULL)
		return Mem_J;
	if (strcmp(str, "[stack]") == 0)
		return Mem_S;
	if (strcmp(str, "/dev/kgsl-3d0") == 0)
		return Mem_V;
	return Mem_O;
}



class MemoryDebug{
  private:
	pid_t pid = 0;
  public:
    int setPackageName(const char* name);
    long getModuleBase(const char* name,int index = 1);
	long getBssModuleBase(const char *name);
	size_t preadv(long address, void *buffer, size_t size);
	size_t pwritev(long address, void *buffer, size_t size);
	template < class T > AddressData search(T value, int type, int mem, bool debug = false);
	template < class T > int edit(T value,long address,int type,bool debug = false);
	int ReadDword(long address);
	long ReadDword64(long address);
	float ReadFloat(long address);
	long ReadLong(long address);
};


#endif

int MemoryDebug::setPackageName(const char* name){
	int id = -1;
	DIR *dir;
	FILE *fp;
	char filename[32];
	char cmdline[256];
	struct dirent *entry;
	dir = opendir("/proc");
	while ((entry = readdir(dir)) != NULL)
	{
		id = atoi(entry->d_name);
		if (id != 0)
		{
			sprintf(filename, "/proc/%d/cmdline", id);
			fp = fopen(filename, "r");
			if (fp)
			{
				fgets(cmdline, sizeof(cmdline), fp);
				fclose(fp);
				if (strcmp(name, cmdline) == 0)
				{
					pid = id;
					return id;
				}
			}
		}
	}
	closedir(dir);
	return -1;
}

long MemoryDebug::getModuleBase(const char* name,int index)
{
	int i = 0;
	long start = 0,end = 0;
    char line[1024] = {0};
    char fname[128];
	sprintf(fname, "/proc/%d/maps", pid);
    FILE *p = fopen(fname, "r");
    if (p)
    {
        while (fgets(line, sizeof(line), p))
        {
            if (strstr(line, name) != NULL)
            {
                i++;
                if(i==index){
                    sscanf(line, "%lx-%lx", &start,&end);
                    break;
                }
            }
        }
        fclose(p);
    }
    return start;
}

long MemoryDebug::getBssModuleBase(const char *name)
{
	FILE *fp;
	int cnt = 0;
	long start;
	char tmp[256];
	fp = NULL;
	char line[1024];
	char fname[128];
	sprintf(fname, "/proc/%d/maps", pid);
	fp = fopen(fname, "r");
	while (!feof(fp))
	{
		fgets(tmp, 256, fp);
		if (cnt == 1)
		{
			if (strstr(tmp, "[anon:.bss]") != NULL)
			{
				sscanf(tmp, "%lx-%*lx", &start);
				break;
			}
			else
			{
				cnt = 0;
			}
		}
		if (strstr(tmp, name) != NULL)
		{
			cnt = 1;
		}
	}
	return start;
}



size_t MemoryDebug::pwritev(long address, void *buffer, size_t size){
	struct iovec iov_WriteBuffer, iov_WriteOffset;
	iov_WriteBuffer.iov_base = buffer;
	iov_WriteBuffer.iov_len = size;
	iov_WriteOffset.iov_base = (void *)address;
	iov_WriteOffset.iov_len = size;
	return syscall(SYS_process_vm_writev, pid, &iov_WriteBuffer, 1, &iov_WriteOffset, 1, 0);
}


size_t MemoryDebug::preadv(long address, void *buffer, size_t size){
	struct iovec iov_ReadBuffer, iov_ReadOffset;
	iov_ReadBuffer.iov_base = buffer;
	iov_ReadBuffer.iov_len = size;
	iov_ReadOffset.iov_base = (void *)address;
	iov_ReadOffset.iov_len = size;
	return syscall(SYS_process_vm_readv, pid, &iov_ReadBuffer, 1, &iov_ReadOffset, 1, 0);
}

template < class T > AddressData MemoryDebug::search(T value, int type, int mem, bool debug){
	size_t size = judgSize(type);
	MemPage *mp = NULL;
	AddressData ad;
	long * tmp, *ret = NULL;
	int count = 0;
	char filename[32];
	char line[1024];
	snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);
	FILE *fp = fopen(filename, "r");
	if (fp != NULL)
	{
		tmp = (long*)calloc(1024000,sizeof(long));
		while (fgets(line, sizeof(line), fp))
		{
			mp = (MemPage *) calloc(1, sizeof(MemPage));
			sscanf(line, "%p-%p %s %*p %*p:%*p %*p   %[^\n]%s", &mp->start, &mp->end,mp->flags, mp->name);
			if ((memContrast(mp->name) == mem || mem == Mem_Auto)
				&& strstr(mp->flags, "r") != NULL)
			{
				mp->buf = (void *)malloc(mp->end - mp->start);
				preadv(mp->start, mp->buf, mp->end - mp->start);
				for (int i = 0; i < (mp->end - mp->start) / size; i++)
				{
					if((type == XOR ? (*(int *) (mp->buf + i * size) ^ mp->start + i * size)
						 : *(T *) (mp->buf + i * size)) == value)
					{
						*(tmp + count) = mp->start + i * size;
						count++;
						if (debug)
						{
							std::cout
								<< "index:" << count
								<< "    value:" << (type == XOR?*(int *) (mp->buf + i * size) ^ (mp->start + i * size):*(T *) (mp->buf + i * size));
							printf("    address:%p\n", mp->start + i * size);
							
						}
					}
				}

				free(mp->buf);
			}
		}
		fclose(fp);
	}
	if(debug)
	printf(" End of the search , common %d Bar result \n", count);
	ret = (long*)calloc(count,sizeof(long));
	memcpy(ret,tmp,count*(sizeof(long)));
	free(tmp);
	ad.addrs = ret;
	ad.count = count;
	free(ret);
	return ad;
}

template < class T > int MemoryDebug::edit(T value,long address,int type,bool debug){
	if(-1 == pwritev(address,&value,judgSize(type)))
	{
		if(debug)
		printf(" Modification failed -> addr:%p\n",address);
		return -1;
	}else
	{
		if(debug)
		printf(" Modification successful -> addr:%p\n",address);
		return 1;
	}
	return -1;
}

long MemoryDebug::ReadDword64(long address)
{
	long local_ptr = 0;
	preadv(address, &local_ptr, 4);
	return local_ptr;
}

int MemoryDebug::ReadDword(long address)
{
	int local_value = 0;
	preadv(address, &local_value, 4);
	return local_value;
}

float MemoryDebug::ReadFloat(long address)
{
	float local_value = 0;
	preadv(address, &local_value, 4);
	return local_value;
}

long MemoryDebug::ReadLong(long address)
{
	long local_value = 0;
	preadv(address, &local_value, 8);
	return local_value;
}

void getRoot(char **argv)
{
	char shellml[64];
	sprintf(shellml, "su -c %s", *argv);
	if (getuid() != 0)
	{
		system(shellml);
		exit(1);
	}
}
