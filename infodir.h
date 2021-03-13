#ifndef INFODIR_H_INCLUDED
#define INFODIR_H_INCLUDED

#include <dirent.h>

typedef struct
{
    long int files, subdirectories, sumOfBytes;
}dirInfo;

typedef struct
{
    char * cur_dir;
    DIR *pDir;
    struct dirent *pDirent;
    dirInfo info;
}ThreadArgs;


void infodir_IPC(const char* root_dir);
void subDirFork(const char* cur_dir, const char* subdir, dirInfo* infoMap);
void subDirExtraFork(const char* root_dir, DIR *pDir, dirInfo* infoMap);
void subDirRecursive(const char* cur_dir, const char* subdir, dirInfo* infoMap);
void getFileStats(const char* cur_dir, const char* filename, dirInfo* infoMap);
void forkFileStats(const char* cur_dir, const char* filename, dirInfo* infoMap);

void infoDir_MT(const char* root_dir);
void *subdirThread(void *arg);
void *subdirThreadExtra(void *arg);
void *threadFileStats(void *arg);

#endif //INFODIR_H_INCLUDED