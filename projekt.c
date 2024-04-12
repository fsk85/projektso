#include <dirent.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* todo:
 * zmienic perrory na logi, bo stdout jest zamkniete
 * dodac checki
 * dodac funkcje copy co wybiera czy copy big czy copy small i dac ja w syncNonRecursive
 */
typedef struct fileList
{
    char fileName[PATH_MAX];
    off_t fileSize;
    time_t modDate; 
    mode_t permissions;
    struct fileList* next;
} fileList;

typedef struct
{
    int sleep_time;
    bool recursive;
    int threshold;
} config;

typedef struct subDirList
{
    char path[PATH_MAX];
    mode_t permissions; /* to do wyjebania */
    struct subDirList* next;
} subDirList;

config flags = { 300, false, 10000 };

#define NR_OPEN 1024

/* funkcja ktora sprawdza czy plik z jednego katalogu istnieje i jest taki sam
 * jak w drugim katalogu*/

int
changedFile(fileList* sourceNode, fileList* targetNode)
{
    if (!sourceNode)
        return 0;
    printf("sprawdzanie: %s | %d | %o\n",
           sourceNode->fileName,
           sourceNode->fileSize,
           sourceNode->permissions);
    int changed = 1;
    while (targetNode != NULL) {
        if (!strcmp(sourceNode->fileName, targetNode->fileName) &&
            sourceNode->fileSize == targetNode->fileSize &&
            sourceNode->permissions == targetNode->permissions &&
            sourceNode->modDate < targetNode->modDate) {
            changed = 0;
            break;
        }
        targetNode = targetNode->next;
    }

        printf("wysiadamy z : %s do: \n",sourceNode->fileName,sourceNode->next->fileName);
    return changed;
}

void
addToList(fileList** head,
          char* filename,
          off_t filesize,
          time_t moddate,
          mode_t permissions)
{
    fileList* node = malloc(sizeof(fileList));
    if (!node) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    strcpy(node->fileName, filename);
    node->fileSize = filesize;
    node->modDate = moddate;
    node->permissions = permissions;
    node->next = NULL;
    if (*head == NULL) {
        *head = node;
        return;
    }
    fileList* last = *head;
    while (last->next != NULL) {
        last = last->next;
    }
    last->next = node;
}

fileList*
saveFilesToList(char* dirPath)
{
    /* inicjalizujemy odnosnik na pierwszy element w liscie plikow na NULL */
    fileList* head = NULL;
    /* otwieramy katalog */
    DIR* dir = opendir(dirPath);
    if (!dir) {
        perror("opendir");
        exit(EXIT_FAILURE);
    }
    struct dirent* dirContent;
    while ((dirContent = readdir(dir)) != NULL) {
        /* pomijamy odnosnik . na katalog obecny i katalog poprzedni .. */
        if (strcmp(dirContent->d_name, ".") == 0 ||
            strcmp(dirContent->d_name, "..") == 0)
            continue;
        char fullPath[PATH_MAX];
        struct stat info;
        snprintf(
          fullPath, sizeof(fullPath), "%s/%s", dirPath, dirContent->d_name);
        if (stat(fullPath, &info) == -1) {
            perror("stat");
            closedir(dir);
            exit(EXIT_FAILURE);
        }

        /* Jezeli jest plikiem regularnym to dodajemy go do listy */
        if (S_ISREG(info.st_mode)) {
            addToList(&head,
                      dirContent->d_name,
                      info.st_size,
                      info.st_mtime,
                      info.st_mode);
        }
    }

    return head;
}

void
checkDirs(char* sourceDir, char* targetDir)
{
    DIR* srcDir;
    DIR* targDir;
    srcDir = opendir(sourceDir);
    targDir = opendir(targetDir);
    /* sprawdzamy czy mozna otworzyc katalogi */
    if (srcDir == NULL || targDir == NULL) {
        perror("opendir");
        exit(EXIT_FAILURE);
    }
    /* zamykamy katalogi, jesli wystapil blad to */
    if (closedir(srcDir) == -1 || closedir(targDir) == -1) {
        perror("closedir");
        exit(EXIT_FAILURE);
    }
}

void
appendSubDirList(subDirList** head, char* Path)
{
    subDirList* node = malloc(sizeof(subDirList));
    if (!node)
        exit(EXIT_FAILURE);
    strcpy(node->path, Path);
    node->next = NULL;
    if (!*head) {
        *head = node;
        return;
    }
    subDirList* tmp = *head;
    while (tmp->next) {
        tmp = tmp->next;
    }
    tmp->next = node;
}

subDirList*
getSubDirs(subDirList* head, char* dirPath)
{
    DIR* dir;
    dir = opendir(dirPath);
    if (!dir)
        exit(EXIT_FAILURE);

    struct dirent* subDir;
    subDir = readdir(dir);
    appendSubDirList(&head, dirPath);
    while (subDir != NULL) {
        if (subDir->d_type == DT_DIR && strcmp(subDir->d_name, ".") != 0 &&
            strcmp(subDir->d_name, "..") != 0) {
            char subDirPath[PATH_MAX] = { 0 };
            strcat(subDirPath, dirPath);
            strcat(subDirPath, "/");
            strcat(subDirPath, subDir->d_name);
            head = getSubDirs(head, subDirPath);
        }
        subDir = readdir(dir);
    }
    closedir(dir);
    printf("getting subdir: %s\n", dirPath);
    return head;
}


char* getRelativePath(const char *basePath, const char *targetPath) {
    size_t baseLen = strlen(basePath);
    size_t targetLen = strlen(targetPath);

    if (strncmp(basePath, targetPath, baseLen) != 0) {
        return NULL;
    }

    const char *relativePath = targetPath + baseLen;

    if (relativePath[0] == '/' || relativePath[0] == '\\') {
        relativePath++;
    }

    return strdup(relativePath);
}

void syncNonRecursive(char *sourceDirPath, char *targetDirPath){
      fileList* srcDirHead = saveFilesToList(sourceDirPath);
      fileList* targetDirHead = saveFilesToList(targetDirPath);
      fileList* node = srcDirHead;
      fileList* nodeTarg = targetDirHead;
        while (node != NULL) {
            /* Sprawdzamy czy plik istnieje w katalogu docelowym i czy
            * zostal zmieniony */
           if (changedFile(node, targetDirHead)) {
               /*Skopiuj plik do katalogu docelowego */
                    printf("zmienil sie lub nie istnieje plik w katalogu:  %s o naziwe %s\n",targetDirPath,
                           node->fileName);
                }
                node = node->next;
                printf("WYSIADAMY\n");
            }
}

void
syncRecursive(char *sourceDirPath, char *targetDirPath)
{
    /* inicjalizujemy liste podkatalogow w katalogu zrodlowym*/
    subDirList* srcDirHead = NULL;
    srcDirHead = getSubDirs(srcDirHead, sourceDirPath);
    /* inicjalizujemy liste podkatalogow w katalogu docelowym */
    subDirList* targetDirHead = NULL;
    targetDirHead = getSubDirs(targetDirHead, targetDirPath);
    /* inicjalizujemy pomocnicze wskazniki na pierwszy element listy
     * podkatalogow */
    subDirList* srcDirNode = srcDirHead;
    subDirList* targetDirNode = targetDirHead;
    
    while(srcDirNode){
    char *relativePath = getRelativePath(sourceDirPath,srcDirNode->path);
    printf("RELATIVE: %s\n",relativePath);
    char fullTargetPath[2046];
    snprintf(fullTargetPath, sizeof(fullTargetPath), "%s/%s", targetDirPath, relativePath);
    DIR *dir = opendir(fullTargetPath);
    if(dir != NULL){
      printf("istnieje KATALOG: %s\n", fullTargetPath);
      closedir(dir);
      syncNonRecursive(srcDirNode->path,fullTargetPath);
    }
    else{
        printf("nie istnieje KATALOG: %s\n", fullTargetPath);
        closedir(dir);
        struct stat info;
        stat(srcDirNode->path,&info);
        mkdir(fullTargetPath,info.st_mode);
        syncNonRecursive(srcDirNode->path,fullTargetPath);
    }
    srcDirNode = srcDirNode -> next;
  }
 
}

void
daemonLoop(char* sourceDir, char* targetDir)
{
    while (true) {
        if (flags.recursive == false) {
            syncNonRecursive(sourceDir,targetDir);
            printf("SKONCZONO SYNC NIE REKURESE\n");
            /* todo: usun pliki ktore istnieja w katalogu docelowym, a nie
             * istnieja w zrodlowym */
            
        } else {
            syncRecursive(sourceDir,targetDir);
            printf("SKONCZONO SYNC RECURSE\n");
        }
        /* uwolnic pamiec */
        sleep(flags.sleep_time);
    }
    /* todo: */
}

int
runDaemon(char* sourceDir, char* targetDir)
{
    pid_t pid;
    int i;

    /* Tworzenie nowego procesu */
    pid = fork();
    if (pid == -1)
        return -1;
    else if (pid != 0)
        exit(EXIT_SUCCESS);
    /*Stworzenie nowej sesji i grupy procesow*/
    if (setsid() == -1)
        return -1;

    /*Ustawienie katalogu roboczego na katalog glowny*/
    if (chdir("/") == -1)
        return -1;

    pid_t cpid = getpid();
    printf("info wstepne: sourceDir: %s targetDir: %s pid: %d t: %d p: %d\n",
           sourceDir,
           targetDir,
           cpid,
           flags.sleep_time,
           flags.threshold);
    /* Zamkniecie otwartych plikow */
//      for (i = 0; i < NR_OPEN; i++)
//       close(i);

    /* Przeadresowanie deskryptorow plikow 0,1,2 na /dev/null */
 //    open("/dev/null",O_RDWR);
 //    dup(0);
  //   dup(0);

    daemonLoop(sourceDir, targetDir);
    return 1;
}

int
main(int argc, char** argv)
{
    /* parsowanie argumentow */
    int opt;
    while ((opt = getopt(argc, argv, "Rp:t:")) != -1) {
        switch (opt) {
            case 't':
                flags.sleep_time = atoi(optarg);
                break;
            case 'R':
                flags.recursive = true;
                break;
            case 'p':
                flags.threshold = atoi(optarg);
                break;
            default:
                fprintf(stderr,
                        "Sposob uzycia: %s [flagi] [katalog_zrodlowy] "
                        "[katalog_docelowy]",
                        argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    char* sourceDir = argv[optind];
    char* targetDir = argv[optind + 1];
    if (argv[optind + 2] != NULL) {
        fprintf(stderr, "Podano niewlasciwa liczbe argumentow!\n");
        exit(EXIT_FAILURE);
    }
    /* sprawdzamy czy podane katalogi istnieja, czy mamy do nich dostep.. */
    checkDirs(sourceDir, targetDir);
    char sourceDirPath[PATH_MAX];
    char targetDirPath[PATH_MAX];
    realpath(sourceDir, sourceDirPath);
    realpath(targetDir, targetDirPath);

    runDaemon(sourceDirPath, targetDirPath);
}
