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
#include <errno.h>
#include <sys/mman.h>

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
#define O_BINARY  0
/* funkcja ktora sprawdza czy plik z jednego katalogu istnieje i jest taki sam
 * jak w drugim katalogu*/

// Funkcja kopiujaca dla dużych plików (> edge)
int copy_big(const char *source_file, const char *destination_file, size_t BUF_SIZE)
{
    // Otwórz pliki
    int fd_in = open(source_file, O_RDONLY);
    if (fd_in == -1)
    {
        perror("Problem pliku źródlowego!");
        return EXIT_FAILURE;
    }

    int fd_out = open(destination_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_out == -1)
    {
        perror("Problem pliku docelowego!");
        close(fd_in);
        return EXIT_FAILURE;
    }

    struct stat sb;
    if (fstat(fd_in, &sb) == -1)
    {
        perror("Problem ze statystykami!");
        close(fd_in);
        return EXIT_FAILURE;
    }
    size_t total_size = sb.st_size;
    size_t bytes_copied = 0;
    size_t bytes_written;
    size_t mapping_size;
    void *addr;

    // Petla kopiujaca dane
    while (bytes_copied < total_size)
    {
        if (total_size - bytes_copied < BUF_SIZE)
        {
            mapping_size = total_size - bytes_copied;
        }
        else
        {
            mapping_size = BUF_SIZE;
        }

        // Mapowanie pliku do pamieci
        addr = mmap(NULL, mapping_size, PROT_READ, MAP_SHARED, fd_in, bytes_copied);
        if (addr == MAP_FAILED)
        {
            perror("Problem z mmap'em!");
            close(fd_in);
            close(fd_out);
            return EXIT_FAILURE;
        }

        // Zapis danych do pliku docelowego
        bytes_written = write(fd_out, addr, mapping_size);

        if (bytes_written != mapping_size)
        {
            if (bytes_written == -1 && errno == EINTR)
            {
                munmap(addr, mapping_size);
                continue;
            }
            perror("Problem z zapisem!");
            munmap(addr, mapping_size);
            close(fd_in);
            close(fd_out);
            return EXIT_FAILURE;
        }

        // Zwolnienie zmapowanej pamieci
        if (munmap(addr, mapping_size) == -1)
        {
            perror("Problem z munmap'em!");
            close(fd_in);
            close(fd_out);
            return EXIT_FAILURE;
        }

        bytes_copied += mapping_size;
    }

    // Zamkniecie plików
    if (close(fd_in) == -1)
    {
        perror("Problem z zamknieciem pliku zrodlowego!");
        return EXIT_FAILURE;
    }
    if (close(fd_out) == -1)
    {
        perror("Problem z zamknieciem pliku docelowego!");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

// Kopiowanie dla malych plikow
int copy_small(const char *source_file, const char *destination_file, size_t BUF_SIZE)
{
    ssize_t bytes_read;
    ssize_t bytes_written;
    unsigned char buffer[BUF_SIZE];

    // Otworz plik zrodlowy
    int fd_in = open(source_file, O_RDONLY| O_BINARY);

    if (fd_in == -1)
    {
        perror("Problem pliku zrodlowego!");
        exit(EXIT_FAILURE);
    }

    // Otworz plik docelowy
    int fd_out = open(destination_file, O_WRONLY | O_CREAT | O_TRUNC| O_BINARY, 0644);

    if (fd_out == -1)
    {
        perror("Problem pliku docelowego!");
        close(fd_in);
        exit(EXIT_FAILURE);
    }

    // Petla kopiujaca dane
    while ((bytes_read = read(fd_in, buffer, BUF_SIZE)) > 0)
    {
        bytes_written = write(fd_out, buffer, bytes_read);

        if (bytes_written != bytes_read)
        {
            perror("Problem zapisu!");
            close(fd_in);
            close(fd_out);
            exit(EXIT_FAILURE);
        }
    }

    // Zamkniecie plikow
    if (close(fd_in) == -1)
    {
        perror("Problem z zamknieciem pliku zrodlowego!");
        return EXIT_FAILURE;
    }

    if (close(fd_out) == -1)
    {
        perror("Problem z zamknieciem pliku docelowego!");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void copy(char* sourceDirPath, char* targetDirPath)
{
    size_t buffer;
    size_t size;
    size_t result;
    buffer=4096;
    // Sprawdzenie liczby argumentow
    const char *source_file = sourceDirPath;
    const char *destination_file = targetDirPath;
    

    // Sprawdzenie rozmiaru pliku zrodlowego
    struct stat st;
    stat(source_file, &st);
    size = st.st_size;

    // Wybor funkcji kopiujacej w zaleznosci od rozmiaru pliku
    if (flags.threshold<size)
    {
        result = copy_big(source_file, destination_file,buffer);
    }
    else
    {
        result = copy_small(source_file, destination_file,buffer);
    }

    // Sprawdzenie wyniku operacji kopiowania
    if (result == EXIT_FAILURE)
    {
        perror("Blad kopiowania!");
        return EXIT_FAILURE;
    }
    else
    {
        printf("Udalo sie skopiowac plik!\n");
    }
}




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

char *constructFullPath(char *dirPath, char *fileName){
    size_t dirLen = strlen(dirPath);
    size_t fileLen = strlen(fileName);
    char *tmp = malloc((dirLen+fileLen+2)*sizeof(char));
    if (tmp == NULL) {
        exit(EXIT_FAILURE);
    }

    strcpy(tmp, dirPath);
    strcat(tmp, "/");
    strcat(tmp, fileName);
    printf("constructed path: %s\n", tmp);
    return tmp;
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
                    printf("zmienil sie lub nie istnieje plik w katalogu:  %s o naziwe %s\n",targetDirPath, node->fileName);
                    char *fullSourcePath = constructFullPath(sourceDirPath,node->fileName);
                    char *fullTargetPath = constructFullPath(targetDirPath,node->fileName);
                    printf("FULLSOURCEPATH: %s\n", fullSourcePath);
                    printf("FULLTARGETPATH: %s\n",fullTargetPath);
                    copy(fullSourcePath,fullTargetPath);
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
