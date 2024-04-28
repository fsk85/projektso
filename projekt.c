#include <dirent.h>

#include <sys/syslog.h>
#include <syslog.h>

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

#include <signal.h>
/* TODO:
 * zmienic perrory na logi, bo stdout jest zamkniete
 * dodac checki
 * test
 */
typedef struct fileList {
  char fileName[PATH_MAX];
  off_t fileSize;
  time_t modDate;
  mode_t permissions;
  struct fileList *next;
} fileList;

typedef struct {
  int sleep_time;
  bool recursive;
  off_t threshold;
} config;

typedef struct subDirList {
  char path[PATH_MAX];
  mode_t permissions; /* to do wyjebania */
  struct subDirList *next;
  struct subDirList *previous;
} subDirList;

config flags = {
    300, false, 1000000000 /* 1GB */
};

volatile bool signal_received;

#define NR_OPEN 1024
#define O_BINARY 0
/* funkcja ktora sprawdza czy plik z jednego katalogu istnieje i jest taki sam
 * jak w drugim katalogu*/

// Funkcja kopiujaca dla dużych plików (> edge)
int copy_big(const char *source_file, const char *destination_file)
{ 
  int source_fd,dest_fd;
  char *src, *dst;
  struct stat file_info;

  source_fd = open(source_file, O_RDONLY);
  if(source_fd == -1)
  {
    syslog(LOG_ERR, "Problem z otworzeniem pliku %s", source_file);
    exit(EXIT_FAILURE);
  }
  if(fstat(source_fd,&file_info) == -1)
  {
    syslog(LOG_ERR, "Problem z uzyskaniem informacji o pliku: %s", source_file);
    exit(EXIT_FAILURE);
  }
  dest_fd = open(destination_file, O_RDWR | O_CREAT, 0666);
  if(dest_fd == -1)
  {
    syslog(LOG_ERR, "Problem z otwarciem pliku docelowego: %s", destination_file);
    exit(EXIT_FAILURE);
  }
  if(ftruncate(dest_fd, file_info.st_size) == -1)
  {
    syslog(LOG_ERR, "Problem z ustawieniem rozmiaru pliku docelowego: %s", destination_file);
    exit(EXIT_FAILURE);
  }
  src = mmap(NULL, file_info.st_size, PROT_READ, MAP_PRIVATE, source_fd, 0);
  if(src == MAP_FAILED)
  {
    syslog(LOG_ERR, "Problem z mapowaniem pliku zrodlowego: %s", source_file);
    exit(EXIT_FAILURE);
  }
  dst = mmap(NULL, file_info.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, dest_fd, 0);
  if(dst == MAP_FAILED)
  {
    syslog(LOG_ERR, "Problem z mapowaniem pliku docelowego: %s", destination_file);
    exit(EXIT_FAILURE);
  }
  /* kopiowanie pliku */
  memcpy(dst, src, file_info.st_size);

  munmap(src, file_info.st_size);
  munmap(dst, file_info.st_size);

  close(source_fd);
  close(dest_fd);
  return 0;
}

// Kopiowanie dla malych plikow
int copy_small(const char *source_file, const char *destination_file,
               size_t BUF_SIZE) {
  ssize_t bytes_read;
  ssize_t bytes_written;
  char *buffer = malloc(BUF_SIZE);

  // Otworz plik zrodlowy
  int fd_in = open(source_file, O_RDONLY | O_BINARY);

  if (fd_in == -1) {
    perror("Problem pliku zrodlowego!");
    exit(EXIT_FAILURE);
  }

  // Otworz plik docelowy
  int fd_out =
      open(destination_file, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);

  if (fd_out == -1) {
    perror("Problem pliku docelowego!");
    close(fd_in);
    exit(EXIT_FAILURE);
  }

  // Petla kopiujaca dane
  while ((bytes_read = read(fd_in, buffer, BUF_SIZE)) > 0) {
    bytes_written = write(fd_out, buffer, bytes_read);

    if (bytes_written != bytes_read) {
      perror("Problem zapisu!");
      free(buffer);
      close(fd_in);
      close(fd_out);
      exit(EXIT_FAILURE);
    }
  }

  // Zamkniecie plikow
  if (close(fd_in) == -1) {
    perror("Problem z zamknieciem pliku zrodlowego!");
    exit(EXIT_FAILURE);
  }

  if (close(fd_out) == -1) {
    perror("Problem z zamknieciem pliku docelowego!");
    exit(EXIT_FAILURE);
  }
  free(buffer);
  return EXIT_SUCCESS;
}

void copy(char *sourceFilePath, char *targetFilePath) {
  size_t bufferSize = 4096;
  off_t size;
  size_t result;
  // Przypisanie sciezki do pliku
  const char *source_file = sourceFilePath;
  const char *destination_file = targetFilePath;

  // Sprawdzenie rozmiaru pliku zrodlowego
  struct stat st;
  stat(source_file, &st);
  size = st.st_size;

  // Wybor funkcji kopiujacej w zaleznosci od rozmiaru pliku
  if (flags.threshold < size) {
    result = copy_big(source_file, destination_file);
  } else {
    result = copy_small(source_file, destination_file, bufferSize);
  }

  // Sprawdzenie wyniku operacji kopiowania
  if (result == EXIT_FAILURE) {
    perror("Blad kopiowania!");
    exit(EXIT_FAILURE);
  } else {
    printf("Udalo sie skopiowac plik!\n");
  }
}

int changedFile(fileList *sourceNode, fileList *targetNode) {
  if (!sourceNode)
    return 0;
  printf("sprawdzanie: %s | %d | %o\n", sourceNode->fileName,
         sourceNode->fileSize, sourceNode->permissions);
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

  printf("wysiadamy z : %s do: \n", sourceNode->fileName,
         sourceNode->next->fileName);
  return changed;
}

int fileToRemove(fileList *sourceNode, fileList *targetNode) {
  if (!sourceNode)
    return 0;
  int different = 1;
  while (targetNode != NULL) {
    if (!strcmp(sourceNode->fileName, targetNode->fileName)) {
      different = 0;
      break;
    }
    targetNode = targetNode->next;
  }
  return different;
}

void addToList(fileList **head, char *filename, off_t filesize, time_t moddate,
               mode_t permissions) {
  fileList *node = malloc(sizeof(fileList));
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
  fileList *last = *head;
  while (last->next != NULL) {
    last = last->next;
  }
  last->next = node;
}

fileList *saveFilesToList(char *dirPath) {
  /* inicjalizujemy odnosnik na pierwszy element w liscie plikow na NULL */
  fileList *head = NULL;
  /* otwieramy katalog */
  DIR *dir = opendir(dirPath);
  if (!dir) {
    perror("opendir");
    exit(EXIT_FAILURE);
  }
  struct dirent *dirContent;
  while ((dirContent = readdir(dir)) != NULL) {
    /* pomijamy odnosnik . na katalog obecny i katalog poprzedni .. */
    if (strcmp(dirContent->d_name, ".") == 0 ||
        strcmp(dirContent->d_name, "..") == 0)
      continue;
    char fullPath[PATH_MAX];
    struct stat info;
    snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, dirContent->d_name);
    if (stat(fullPath, &info) == -1) {
      perror("stat");
      closedir(dir);
      exit(EXIT_FAILURE);
    }

    /* Jezeli jest plikiem regularnym to dodajemy go do listy */
    if (!S_ISDIR(info.st_mode)) {
      addToList(&head, dirContent->d_name, info.st_size, info.st_mtime,
                info.st_mode);
    }
  }
  closedir(dir);
  return head;
}

void checkDirs(char *sourceDir, char *targetDir) {
  DIR *srcDir;
  DIR *targDir;
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

void appendSubDirList(subDirList **head, char *Path) {
  subDirList *node = malloc(sizeof(subDirList));
  if (!node)
    exit(EXIT_FAILURE);
  strcpy(node->path, Path);
  node->next = NULL;
  node->previous = NULL;
  if (!*head) {
    *head = node;
    return;
  }
  subDirList *tmp = *head;
  while (tmp->next) {
    tmp = tmp->next;
  }
  tmp->next = node;
  node->previous = tmp;
}
/* zmienic zeby katalog sie zamykal przed wywolaniem rekurencyjnym bo wypierdala
 * blad przy wielu plikach w katalogu ze za duzo otwartych plikow jest */
subDirList *getSubDirs(subDirList *head, char *dirPath) {
  DIR *dir;
  dir = opendir(dirPath);
  if (!dir)
    exit(EXIT_FAILURE);

  struct dirent *subDir;
  subDir = readdir(dir);
  appendSubDirList(&head, dirPath);
  while (subDir != NULL) {
    if (subDir->d_type == DT_DIR && strcmp(subDir->d_name, ".") != 0 &&
        strcmp(subDir->d_name, "..") != 0) {
      char subDirPath[PATH_MAX] = {0};
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

char *getRelativePath(const char *basePath, const char *targetPath) {
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

char *constructFullPath(char *dirPath, char *fileName) {
  if (fileName == NULL)
    return dirPath;
  size_t dirLen = strlen(dirPath);
  size_t fileLen = strlen(fileName);
  char *tmp = malloc((dirLen + fileLen + 2) * sizeof(char));
  if (tmp == NULL) {
    exit(EXIT_FAILURE);
  }

  strcpy(tmp, dirPath);
  strcat(tmp, "/");
  strcat(tmp, fileName);
  printf("constructed path: %s\n", tmp);
  return tmp;
}

void freeFileList(fileList *head)
{
  fileList *tmp; 
  while(head != NULL)
  {
    tmp = head;
    head = head->next;
    free(tmp);
  }
}

void freeSubDirList(subDirList *head)
{
  subDirList *tmp; 
  while(head != NULL)
  {
    tmp = head;
    head = head->next;
    free(tmp);
  }
}

void syncNonRecursive(char *sourceDirPath, char *targetDirPath) {
  fileList *srcDirHead = saveFilesToList(sourceDirPath);
  fileList *targetDirHead = saveFilesToList(targetDirPath);
  fileList *node = srcDirHead;
  fileList *nodeTarg = targetDirHead;
  while (node != NULL) {
    /* Sprawdzamy czy plik istnieje w katalogu docelowym i czy
     * zostal zmieniony */
    if (changedFile(node, targetDirHead)) {
      /*Skopiuj plik do katalogu docelowego */
      printf("zmienil sie lub nie istnieje plik w katalogu:  %s o naziwe %s\n",
             targetDirPath, node->fileName);
      char *fullSourcePath = constructFullPath(sourceDirPath, node->fileName);
      char *fullTargetPath = constructFullPath(targetDirPath, node->fileName);
      printf("FULLSOURCEPATH: %s\n", fullSourcePath);
      printf("FULLTARGETPATH: %s\n", fullTargetPath);
      copy(fullSourcePath, fullTargetPath);
      free(fullSourcePath);
      free(fullTargetPath);
    }
    node = node->next;
    printf("WYSIADAMY\n");
  }
  while (nodeTarg) {
    if (fileToRemove(nodeTarg, srcDirHead)) {
      char *fullTargetPath = constructFullPath(targetDirPath, nodeTarg->fileName);
      unlink(fullTargetPath);
      free(fullTargetPath);
    }
    nodeTarg = nodeTarg->next;
  }
  freeFileList(srcDirHead);
  freeFileList(targetDirHead);
}

bool directoryExists(char *dirPath) {
  DIR *dir = opendir(dirPath);
  if (dir == NULL) {
    closedir(dir);
    return false;
  } else {
    closedir(dir);
    return true;
  }
}

void removeRecursive(subDirList *trgDirHead, char *trgDirPath,
                     char *srcDirPath) {
  /* idziemy na sam koniec listy katalogow docelowych i idac do tylu sprawdzamy
   * czy istnieja w liscie katalogow zrodlowych, jesli nie to usuwamy */
  while (trgDirHead->next) {
    trgDirHead = trgDirHead->next;
  }
  while (trgDirHead != NULL) {
    /* konstruujemy sciezke i sprawdzamy czy katalog istnieje jesli nie to go
     * usuwamy */
    char *relativePath = getRelativePath(trgDirPath, trgDirHead->path);
    char *fullPath = constructFullPath(srcDirPath, relativePath);
    if (!directoryExists(fullPath)) {
      fileList *removeList = saveFilesToList(trgDirHead->path);
      while (removeList != NULL) {
        char *removeFilePath = constructFullPath(trgDirHead->path, removeList->fileName);
        unlink(removeFilePath);
        fileList *tmp = removeList;
        removeList = removeList->next;
        free(tmp);
        free(removeFilePath);
      }
      rmdir(trgDirHead->path);
      printf("USUNIETO: %s\n", trgDirHead->path);
    }
    free(fullPath);
    trgDirHead = trgDirHead->previous;
  }
}

void syncRecursive(char *sourceDirPath, char *targetDirPath) {
  /* inicjalizujemy liste podkatalogow w katalogu zrodlowym*/
  printf("Hello \n");
  subDirList *srcDirHead = NULL;
  srcDirHead = getSubDirs(srcDirHead, sourceDirPath);
  /* inicjalizujemy liste podkatalogow w katalogu docelowym */
  subDirList *targetDirHead = NULL;
  targetDirHead = getSubDirs(targetDirHead, targetDirPath);
  printf("hello 2\n");
  /* inicjalizujemy pomocnicze wskazniki na pierwszy element listy
   * podkatalogow */
  subDirList *srcDirNode = srcDirHead;
  subDirList *targetDirNode = targetDirHead;
  while (srcDirNode) {
    char *relativePath = getRelativePath(sourceDirPath, srcDirNode->path);
    printf("RELATIVE: %s\n", relativePath);
    char fullTargetPath[2046];
    snprintf(fullTargetPath, sizeof(fullTargetPath), "%s/%s", targetDirPath,
             relativePath);
    DIR *dir = opendir(fullTargetPath);
    if (dir != NULL) {
      printf("istnieje KATALOG: %s\n", fullTargetPath);
      closedir(dir);
      syncNonRecursive(srcDirNode->path, fullTargetPath);
    } else {
      printf("nie istnieje KATALOG: %s\n", fullTargetPath);
      closedir(dir);
      struct stat info;
      stat(srcDirNode->path, &info);
      mkdir(fullTargetPath, info.st_mode);
      syncNonRecursive(srcDirNode->path, fullTargetPath);
    }
    srcDirNode = srcDirNode->next;
  }
  // Usuwamy wszystkie katalogi i pliki, ktore nie istnieja w katalogu zrodlowym
  removeRecursive(targetDirHead, targetDirPath, sourceDirPath);
  printf("TEST: %s\n", targetDirHead->path);
  printf("TEST: %s\n", srcDirHead->path);
  freeSubDirList(targetDirHead);
  freeSubDirList(srcDirHead);
}

void sigusr_handler(int signum) {
  signal_received = true;
  syslog(LOG_INFO, "Wybudzenie sie demona na skutek sygnalu");
}

void daemonLoop(char *sourceDir, char *targetDir) {
  openlog("demon", LOG_NDELAY, LOG_DAEMON);
  if (flags.recursive == true) {
    syslog(LOG_INFO, "Rozpoczeto dzialanie demona w trybie rekursywnym");
  } else {
    syslog(LOG_INFO, "Rozpoczeto dzialanie demona w trybie nierekursywnym");
  }
  while (true) {
    if (flags.recursive == false) {
      syncNonRecursive(sourceDir, targetDir);
      /* todo: usun pliki ktore istnieja w katalogu docelowym, a nie
       * istnieja w zrodlowym */

    } else {
      syncRecursive(sourceDir, targetDir);
      printf("SKONCZONO SYNC RECURSE\n");
    }
    syslog(LOG_INFO, "Uspienie demona na czas %d sekund", flags.sleep_time);
    signal(SIGUSR1, sigusr_handler);
    if (signal_received == false) {
      sleep(flags.sleep_time);
    }
    signal_received = false;
    signal(SIGUSR1, SIG_IGN);
  }
}

int runDaemon(char *sourceDir, char *targetDir) {
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
         sourceDir, targetDir, cpid, flags.sleep_time, flags.threshold);
  /* Zamkniecie otwartych plikow */
  //   for (i = 0; i < NR_OPEN; i++)
  //        close(i);

  /* Przeadresowanie deskryptorow plikow 0,1,2 na /dev/null */
  //   open("/dev/null", O_RDWR);
  //    dup(0);
  //    dup(0);

  daemonLoop(sourceDir, targetDir);
  return 1;
}

int main(int argc, char **argv) {
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
      flags.threshold = atoi(optarg) * 1000000; // Prog w MB
      break;
    default:
      fprintf(stderr,
              "Sposob uzycia: %s [flagi] [katalog_zrodlowy] "
              "[katalog_docelowy]",
              argv[0]);
      exit(EXIT_FAILURE);
    }
  }

  char *sourceDir = argv[optind];
  char *targetDir = argv[optind + 1];
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
