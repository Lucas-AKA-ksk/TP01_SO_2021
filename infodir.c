// Necessário -lrt e -lpthread

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <dirent.h>
#include <time.h>
#include "infodir.h"

int main(int argc, char const *argv[])
{
    if (argc==2)
    {
        /* Caso queira testar uma de cada vez,
         só comentar uma das chamadas aqui */
        infodir_IPC(argv[1]);

        infoDir_MT(argv[1]);
    }
    else
    {
        perror("Expected a argument, got none.");
        return EXIT_FAILURE;
    }
    return 0;
}

/* Versão de infodir implementando IPC(Inter Process Communication) através de Shared Memory */
void infodir_IPC(const char* root_dir)
{
    time_t start, end;
    struct tm start_info, end_info, *tmp_time;
    double exec_time; // 
    
    /* Marcação do início */
    time(&start);
    tmp_time = localtime(&start);
    start_info = *tmp_time;
    
    int fd;
    dirInfo *infoMap; // Ponteiro para a área de memória compartilhada;
    int forkCount = 0; // Contagem de processos
    DIR *pDir; // Ponteiro para o diretório raiz(root_dir)
    struct dirent *pDirent;// Ponteiro para uma entrada (arquivo ou subdir) do diretório raiz
    
    /* abre/cria uma area de memoria compartilhada */
    fd = shm_open ("/sharedmem", O_RDWR|O_CREAT, S_IRUSR|S_IWUSR);
    if(fd==-1){
        perror("shm_open() failed");
        exit(EXIT_FAILURE);
    }

    /* ajusta o tamanho da area compartilhada para sizeof (dirInfo) */
    if(ftruncate(fd,sizeof(dirInfo))==-1){
        perror("ftruncate() failed.");
        exit(EXIT_FAILURE);
    }

    /* mapeia a area no espaco de enderecamento deste processo */
    infoMap = mmap (NULL,sizeof(dirInfo), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if(infoMap==MAP_FAILED){
        perror("mmap() failed");
        exit(EXIT_FAILURE);
    }
    infoMap->files = infoMap->subdirectories = infoMap->sumOfBytes = 0;

    /* Abertura do diretório */
    pDir = opendir(root_dir);
    if(pDir==NULL){
        perror("opendir() failed");
        exit(EXIT_FAILURE);
    }
    else
    {
        /* Leitura da entries do diretório (max. 3 processos) */
        while ((pDirent = readdir(pDir))!=NULL)
        {
            /* Ignora hardlinks */
            if(!strcmp(pDirent->d_name,".")||!strcmp(pDirent->d_name,".."))
                continue;

            /* Se o conteúdo lido for um subdiretório,
             chama subDirFork() para criar um processo 
             para verificar os conteúdos do mesmo */
            else if(pDirent->d_type==DT_DIR)
            {
                subDirFork(root_dir,pDirent->d_name,infoMap);
                infoMap->subdirectories+=1;
                forkCount++;
            }
            /* Se o conteúdo lido for um arquivo, chama forkFileStats() e incrementa a quantidade de files*/
            else if (pDirent->d_type == DT_REG)
            {
                forkFileStats(root_dir,pDirent->d_name,infoMap);
                infoMap->files+=1;
                forkCount++;
            }
            /* comment */
            if(forkCount==3)
                break;
        }
        /* Chama o quarto Processo para verificar o restante dos diretórios. */
        if (forkCount >=3 )
            subDirExtraFork(root_dir,pDir,infoMap);
        closedir(pDir);
    }
    
    /* marcação do fim da execução*/
    time(&end);
    tmp_time = localtime(&end);
    end_info = *tmp_time;

    /* Calculo do tempo de execução */
    exec_time = end - start;

    /* Relatório */
    printf("\n-Method: Inter Process Communication:\n\n\tDirectory: %s\n",root_dir);
    printf("\n-Contents of Directory:\n\n\tNum. of Directories: %ld\n\tNum. of Files: %ld\n\tTotal size of directory: %ld bytes\n",infoMap->subdirectories,infoMap->files,infoMap->sumOfBytes);
    printf("\n-Execution time (IPC):\n\n\tStart: %d:%d:%d\n\tEnd: %d:%d:%d\n\tExecution time: %.2f seconds\n",start_info.tm_hour,start_info.tm_min,start_info.tm_sec,end_info.tm_hour,end_info.tm_min,end_info.tm_sec, exec_time);
    
    munmap(infoMap,sizeof(dirInfo));
    close(fd);
}

/* cria um processo que abre o subdiretório encontrado
 em root_dir e lê suas entradas, caso uma entrada seja um subdiretório,
 os conteúdos do mesmo serão verificados através de uma 
 chamada de subdirRecursive()*/
void subDirFork(const char* root_dir, const char* subdir, dirInfo* infoMap)
{
    pid_t pid_subd = fork();
        
    if(pid_subd<0)
    {
        perror("fork() failed.");
        exit(EXIT_FAILURE);
    }
    /* Processo Filho */
    else if(pid_subd==0)
    {
        subDirRecursive(root_dir,subdir,infoMap);
        exit(EXIT_SUCCESS);
    }
    /* Processo Pai */
    else
        waitpid(pid_subd,NULL,0);
}

/* Cria o processo responsável por ler as entradas do
 diretório raiz caso já tenham sido criados mais de 3 procesos */
void subDirExtraFork(const char* root_dir, DIR *pDir, dirInfo* infoMap)
{
    struct dirent *pDirent;

    pid_t pid_extDirs = fork();
    if(pid_extDirs<0)
    {
        perror("fork() failed.");
            exit(EXIT_FAILURE);
    }
    /* Processo Filho */
    else if (pid_extDirs == 0)
    {
        /* Leitura dos conteúdos do subdiretório (subdiretórios deste subdiretório serão verificados recursivamente)*/
        while ((pDirent = readdir(pDir))!=NULL)
        {
            /* ignora hardlinks */
            if(!strcmp(pDirent->d_name,".")||!strcmp(pDirent->d_name,".."))
                continue;

            /* Caso o elemento seja um Diretório(DT_DIR) */
            else if(pDirent->d_type==DT_DIR)
            {
                subDirRecursive(root_dir,pDirent->d_name,infoMap);
                infoMap->subdirectories+=1;
            }
            /* Caso o elemento seja um arquivo(DT_REG) */
            else if (pDirent->d_type == DT_REG)
            {
                getFileStats(root_dir,pDirent->d_name,infoMap);
                infoMap->files+=1;
            }  
        }
        exit(EXIT_SUCCESS);
    }
    /* Processo Pai */
    else
        waitpid(pid_extDirs,NULL,0);
}

/* Abre um diretório subdir,
 lê as entradas do mesmo e caso a entrada seja um subdiretório,
 chama a sí mesma recursivamente */
void subDirRecursive(const char* cur_dir, const char* subdir, dirInfo* infoMap)
{
    DIR *subd_ptr; // Ponteiro para o subdiretório a ser aberto
    struct dirent *pSubdEnt; // Ponteiro para uma entrada do subdiretório
    
    /* Fullpath é alocado dinamicamente 
     para armazenar o Path completo do diretório */
    char* fullPath;
    if((fullPath = malloc(strlen(cur_dir)+strlen(subdir)+2))==NULL)
    {
        perror("\nmalloc() failed.");
        exit(EXIT_FAILURE);
    }
    else
    {
        /* Formata o Path do subdiretório e armazena em fullPath */
        snprintf(fullPath,strlen(cur_dir)+strlen(subdir)+2,"%s/%s",cur_dir,subdir);
        /* Abertura do subdiretório. */
        subd_ptr = opendir(fullPath);
            if(subd_ptr==NULL)
        {
            perror("opendir() failed.");
            exit(EXIT_FAILURE);
        }
        else
        {
            /* Leitura de entries do subdiretório (subdiretórios deste subdiretório são verificados recursivamente) */
            while((pSubdEnt = readdir(subd_ptr))!=NULL)
            {
                
                /* Ignora hardlinks */
                if(!strcmp(pSubdEnt->d_name,".")||!strcmp(pSubdEnt->d_name,".."))
                    continue;

                else if(pSubdEnt->d_type == DT_DIR)
                {
                    subDirRecursive(fullPath,pSubdEnt->d_name,infoMap);
                    infoMap->subdirectories+=1;
                }
                else if(pSubdEnt->d_type == DT_REG)
                {
                    getFileStats(fullPath,pSubdEnt->d_name,infoMap);
                    infoMap->files+=1;
                }   
            }
            closedir(subd_ptr);
        }
        free(fullPath);
    }
}

/* Obtem o tamanho do arquivo "filename" e soma o com o campo sumOfBytes */
void getFileStats(const char* cur_dir, const char* filename, dirInfo* infoMap)
{
    struct stat st; // struct de <sys/stat.h> que armazena dados de um arquivo, através da chamada de stat().
    char *fullPath;
    if((fullPath = malloc(strlen(cur_dir)+strlen(filename)+2))==NULL)
    {
        perror("\nmalloc() failed.");
        exit(EXIT_FAILURE);
    }
    else
    {
        snprintf(fullPath,strlen(cur_dir)+strlen(filename)+2,"%s/%s",cur_dir,filename);
        
        if(stat(fullPath,&st)!=0)
        {
            perror("stat() failed.");
            exit(EXIT_FAILURE);
        }
        else 
            infoMap->sumOfBytes += st.st_size;
        free(fullPath);
    }
}

/* Cria um processo que chama getFileStats */
void forkFileStats(const char* cur_dir, const char* filename, dirInfo* infoMap)
{
    pid_t stats_pid = fork();
    if (stats_pid<0)
    {
        perror("fork() failed.");
        exit(EXIT_FAILURE);
    }
    else if (stats_pid == 0)
    {
        getFileStats(cur_dir,filename,infoMap);
        exit(EXIT_SUCCESS);
    }
    else
        waitpid(stats_pid,NULL,0);
}

/* Versão Multithreading de Infodir */
void infoDir_MT(const char* root_dir)
{
    time_t start, end;
    struct tm start_info, end_info, *tmp_time;
    double exec_time; // 
    
    /* Marcação do início */
    time(&start);
    tmp_time = localtime(&start);
    start_info = *tmp_time;

    int threadCount = 0; // Contagem de threads
    pthread_t T_subdir,T_subdir_extra, T_stats;
    
    /* Declaração e inicialização da struct tArgs, que contém os argumentos que as threads podem receber */ 
    ThreadArgs tArgs;
    tArgs.info.files = tArgs.info.subdirectories = tArgs.info.sumOfBytes = 0;
    if((tArgs.cur_dir = malloc(strlen(root_dir)+1))==NULL)
    {
        perror("malloc() failed");
        exit(EXIT_FAILURE);
    }
    strcpy(tArgs.cur_dir,root_dir);
    
    /* Abertura do diretório */
    tArgs.pDir = opendir(root_dir);
    if(tArgs.pDir==NULL){
        perror("opendir() failed");
        exit(EXIT_FAILURE);
    }
    else
    {
        /* Leitura da entries do diretório (max. 3 threads) */
        while ((tArgs.pDirent = readdir(tArgs.pDir))!=NULL)
        {
            /* Ignora hardlinks */
            if(!strcmp(tArgs.pDirent->d_name,".")||!strcmp(tArgs.pDirent->d_name,".."))
                continue;

            /* Se o conteúdo lido for um subdiretório,
             cria uma thread subDirThread() para criar uma thread 
             para verificar os conteúdos do mesmo */
            else if(tArgs.pDirent->d_type==DT_DIR)
            {
                pthread_create(&T_subdir,NULL,subdirThread,&tArgs);
                pthread_join(T_subdir,NULL);
                tArgs.info.subdirectories+=1;
                threadCount++;
            }
            /* Se o conteúdo lido for um arquivo,
             cria uma thread threadFileStats() e 
             incrementa a quantidade de files*/
            else if (tArgs.pDirent->d_type == DT_REG)
            {
                pthread_create(&T_stats,NULL,threadFileStats,&tArgs);
                pthread_join(T_stats,NULL);
                tArgs.info.files+=1;
                threadCount++;
            }
            /* Interrompe o loop após 3 Threads */
            if(threadCount==3)
                break;
        }
        /* Chama a quarta Thread para verificar o restante dos diretórios. */
        if (threadCount >=3 )
        {
            pthread_create(&T_subdir_extra,NULL,subdirThreadExtra,&tArgs);
            pthread_join(T_subdir_extra,NULL);
        }
        closedir(tArgs.pDir);
    }
    
    /* marcação do fim da execução*/
    time(&end);
    tmp_time = localtime(&end);
    end_info = *tmp_time;

    /* Calculo do tempo de execução */
    exec_time = end - start;

    /* Relatório */
    printf("\n-Method: Multithreading:\n\n\tDirectory: %s\n",root_dir);
    printf("\n-Contents of Directory:\n\n\tNum. of Directories: %ld\n\tNum. of Files: %ld\n\tTotal size of directory: %ld bytes\n",tArgs.info.subdirectories,tArgs.info.files,tArgs.info.sumOfBytes);
    printf("\n-Execution time (MT):\n\n\tStart: %d:%d:%d\n\tEnd: %d:%d:%d\n\tExecution time: %.2f seconds\n",start_info.tm_hour,start_info.tm_min,start_info.tm_sec,end_info.tm_hour,end_info.tm_min,end_info.tm_sec, exec_time);
    
    free(tArgs.cur_dir);
}

void *subdirThread(void *arg)
{
    ThreadArgs *true_args = (ThreadArgs*)arg;
    subDirRecursive(true_args->cur_dir,true_args->pDirent->d_name,&true_args->info);
    pthread_exit(0);
}

void *subdirThreadExtra(void *arg)
{
    ThreadArgs *true_args = (ThreadArgs*)arg;

    /* Leitura dos conteúdos do subdiretório (subdiretórios deste subdiretório serão verificados recursivamente)*/
    while ((true_args->pDirent = readdir(true_args->pDir))!=NULL)
    {
        /* ignora hardlinks */
        if(!strcmp(true_args->pDirent->d_name,".")||!strcmp(true_args->pDirent->d_name,".."))
            continue;

        /* Caso o elemento seja um Diretório(DT_DIR) */
        else if(true_args->pDirent->d_type==DT_DIR)
        {
            subDirRecursive(true_args->cur_dir,true_args->pDirent->d_name,&true_args->info);
            true_args->info.subdirectories+=1;
        }
        /* Caso o elemento seja um arquivo(DT_REG) */
        else if (true_args->pDirent->d_type == DT_REG)
        {
            getFileStats(true_args->cur_dir,true_args->pDirent->d_name,&true_args->info);
            true_args->info.files+=1;
        }  
    }
    pthread_exit(0);
}

void *threadFileStats(void *arg)
{
    ThreadArgs *true_args = (ThreadArgs*)arg;
    getFileStats(true_args->cur_dir,true_args->pDirent->d_name,&true_args->info);
    pthread_exit(0);
}