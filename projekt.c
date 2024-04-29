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

#include <sys/mman.h>

#include <signal.h>

// cos jest nie tak jak sie da prog wiekszy niz 3GB

// Struktura definiujaca liste plikow
typedef struct fileList {
  char fileName[PATH_MAX]; // Nazwa pliku
  off_t fileSize; // Calkowity rozmiar pliku w bajtach
  time_t modDate; // Czas ostatniej modyfikacji pliku
  mode_t permissions; // Uprawnienia dostepu do pliku
  struct fileList *next; // Wskaznik na nastepny element listy
} fileList;

/* Struktura definiujaca konfiguracje programu: Czas spania,
 * flage rekurencji, prog dzielacy pliki male od duzych w bajtach
 */
typedef struct {
  int sleep_time;
  bool recursive;
  off_t threshold;
} config;

// Struktura definiujaca liste podkatalogow
typedef struct subDirList {
  char path[PATH_MAX]; // Sciezka do katalogu
  mode_t permissions;  // Uprawnienia dostepu do katalogu
  struct subDirList *next; // Wskaznik na nastepny element listy
  struct subDirList *previous; // Wskaznik na poprzedni element listy
} subDirList;

config flags = {
    300, false, 1000000000 /* Zdefiniowanie domyslnej konfiguracji programu*/
};                         /* Czas spania: 300 sekund, wylaczona rekurencja, Prog: 1 GB */

// Zmienna boolowska sygnalizujaca czy nastapil sygnal SIGUSR1
volatile bool signal_received; 

// NR_OPEN - Maksymalna liczba otwartych deskryptorow pliku
#define NR_OPEN 1024
#define O_BINARY 0

// Funkcja kopiujaca dla dużych plików (> edge)
int copy_big(const char *source_file, const char *destination_file)
{ 
  int source_fd,dest_fd;
  char *src, *dst;
  struct stat file_info;

  source_fd = open(source_file, O_RDONLY); // Otwarcie pliku zrodlowego w trybie odczytywania
  if(source_fd == -1) // Sprawdzenie czy nie wystapil blad w funkcji open
  {
    syslog(LOG_ERR, "Problem z otworzeniem pliku %s", source_file);
    exit(EXIT_FAILURE);
  }
  if(fstat(source_fd,&file_info) == -1) // Uzyskanie informacji o pliku zrodlowym
  {
    syslog(LOG_ERR, "Problem z uzyskaniem informacji o pliku: %s", source_file);
    exit(EXIT_FAILURE);
  }
  // Otwarcie pliku docelowego w trybie odczytywania i pisania, stworzenie go jesli nie istnieje
  dest_fd = open(destination_file, O_RDWR | O_CREAT, file_info.st_mode); 
  if(dest_fd == -1)
  {
    syslog(LOG_ERR, "Problem z otwarciem pliku docelowego: %s", destination_file);
    exit(EXIT_FAILURE);
  }
  // Obciecie pliku docelowego 
  if(ftruncate(dest_fd, file_info.st_size) == -1)
  {
    syslog(LOG_ERR, "Problem z ustawieniem rozmiaru pliku docelowego: %s", destination_file);
    exit(EXIT_FAILURE);
  }
  // Zmapowanie pliku zrodlowego do pamieci
  src = mmap(NULL, file_info.st_size, PROT_READ, MAP_PRIVATE, source_fd, 0);
  if(src == MAP_FAILED)
  {
    syslog(LOG_ERR, "Problem z mapowaniem pliku zrodlowego: %s", source_file);
    exit(EXIT_FAILURE);
  }
  // Zmapowanie pliku docelowego do pamieci
  dst = mmap(NULL, file_info.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, dest_fd, 0);
  if(dst == MAP_FAILED)
  {
    syslog(LOG_ERR, "Problem z mapowaniem pliku docelowego: %s", destination_file);
    exit(EXIT_FAILURE);
  }
  // Kopiowanie pliku zrodlowego do pliku docelowego
  memcpy(dst, src, file_info.st_size);

  // Usuniecie odwzorowania stron pamieci
  munmap(src, file_info.st_size);
  munmap(dst, file_info.st_size);

  // Zamkniecie deskryptorow plikow
  close(source_fd);
  close(dest_fd);
  return 0;
}

// Funkcja kopiujaca dla malych plikow 
int copy_small(const char *source_file, const char *destination_file) {
  size_t bufferSize = 4096;
  ssize_t bytes_read;
  ssize_t bytes_written;
  char *buffer = malloc(bufferSize);

  // Otworz plik zrodlowy
  int fd_in = open(source_file, O_RDONLY | O_BINARY);

  if (fd_in == -1) {
    syslog(LOG_ERR,"Problem z otwarciem pliku zrodlowego");
    exit(EXIT_FAILURE);
  }

  struct stat file_info; 

  if(fstat(fd_in,&file_info) == -1) // Uzyskanie informacji o pliku zrodlowym
  {
    syslog(LOG_ERR, "Problem z uzyskaniem informacji o pliku: %s", source_file);
    exit(EXIT_FAILURE);
  }

  // Otworz plik docelowy
  int fd_out = open(destination_file, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, file_info.st_mode);

  if (fd_out == -1) {
    syslog(LOG_ERR,"Problem z otwarciem pliku docelowego");
    close(fd_in);
    exit(EXIT_FAILURE);
  }

  // Petla kopiujaca dane
  while ((bytes_read = read(fd_in, buffer, bufferSize)) > 0) {
    bytes_written = write(fd_out, buffer, bytes_read);

    if (bytes_written != bytes_read) {
      syslog(LOG_ERR,"Problem z zapisem do pliku");
      free(buffer);
      close(fd_in);
      close(fd_out);
      exit(EXIT_FAILURE);
    }
  }

  // Zamkniecie plikow
  if (close(fd_in) == -1) {
    syslog(LOG_ERR,"Problem z zamknieciem pliku zrodlowego");
    exit(EXIT_FAILURE);
  }

  if (close(fd_out) == -1) {
    syslog(LOG_ERR,"Problem z zamknieciem pliku docelowego");
    exit(EXIT_FAILURE);
  }
  free(buffer);
  return EXIT_SUCCESS;
}

void copy(char *sourceFilePath, char *targetFilePath) {
  // Przypisanie sciezki do pliku
  const char *source_file = sourceFilePath;
  const char *destination_file = targetFilePath;

  // Sprawdzenie rozmiaru pliku zrodlowego
  struct stat st;
  stat(source_file, &st);

  // Wybor funkcji kopiujacej w zaleznosci od rozmiaru pliku
  if (flags.threshold < st.st_size) {
    copy_big(source_file, destination_file);
  } else {
    copy_small(source_file, destination_file);
  }

}
// Funkcja sprawdzajaca czy plik docelowy jest inny od pliku zrodlowego
bool changedFile(fileList *sourceNode, fileList *targetNode) {
  if (!sourceNode)
    return 0;

  bool changed = true;
  while (targetNode != NULL) {
    if (!strcmp(sourceNode->fileName, targetNode->fileName) &&
        sourceNode->fileSize == targetNode->fileSize &&
        sourceNode->permissions == targetNode->permissions &&
        sourceNode->modDate < targetNode->modDate) {
      changed = false; // Jesli wszystkie warunki zostaly spelnione, oznacza to ze plik sie nie zmienil
      // Wychodzimy z petli i zwracamy false
      break;
    }
    targetNode = targetNode->next;
  }

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
// Funkcja dodajaca odczytany plik do listy plikow
void addToList(fileList **head, char *filename, off_t filesize, time_t moddate,
               mode_t permissions) {
  fileList *node = malloc(sizeof(fileList));
  if (!node) {
    perror("malloc");
    exit(EXIT_FAILURE);
  }
  // Ustawiamy zmienne w strukturze na takie same jakie sa w pliku odczytanym
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
  // Iterujemy na koniec listy i dodajemy element do listy
  while (last->next != NULL) {
    last = last->next;
  }
  last->next = node;
}

fileList *saveFilesToList(char *dirPath) {
  /* Inicjalizujemy odnosnik na pierwszy element w liscie plikow na NULL */
  fileList *head = NULL;
  /* Otwieramy katalog z ktorego odczytujemy pliki */
  DIR *dir = opendir(dirPath);
  if (!dir) {
    syslog(LOG_ERR,"Wystapil blad przy probie odczytania katalogu: %s", dirPath);
    exit(EXIT_FAILURE);
  }
  // Inicjalizacja struktury reprezentujacej jednostke w katalogu
  struct dirent *dirContent;

  while ((dirContent = readdir(dir)) != NULL) { //Odczytujemy cala zawartosc katalogu
    /* pomijamy odnosnik . na katalog obecny i katalog poprzedni .. */
    if (strcmp(dirContent->d_name, ".") == 0 ||
        strcmp(dirContent->d_name, "..") == 0)
      continue;
    char fullPath[PATH_MAX];
    struct stat info; // Inicjalizacja struktury reprezentujacej informacje o jednostce w katalogu
    // Konstruujemy sciezke bezwzgledna do pliku
    snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, dirContent->d_name);
    if (stat(fullPath, &info) == -1) { 
      syslog(LOG_ERR,"Wystapil blad z proba odczytania pliku: %s", fullPath);
      closedir(dir);
      exit(EXIT_FAILURE);
    }

    /* Jezeli jednostka jest plikiem regularnym to dodajemy go do listy */
    if (!S_ISDIR(info.st_mode)) {
      addToList(&head, dirContent->d_name, info.st_size, info.st_mtime,
                info.st_mode);
    }
  }
  closedir(dir);
  return head;
}
// Funkcja sprawdzajaca czy proces moze otworzyc katalog zrodlowy i docelowy
void checkDirs(char *sourceDir, char *targetDir) {
  DIR *srcDir;
  DIR *targDir;
  srcDir = opendir(sourceDir);
  targDir = opendir(targetDir);
  /* sprawdzamy czy mozna otworzyc katalogi */
  if (srcDir == NULL || targDir == NULL) {
    syslog(LOG_ERR,"Wystapil blad z proba otwarcia katalogow");
    exit(EXIT_FAILURE);
  }
  /* zamykamy katalogi, jesli wystapil blad to */
  if (closedir(srcDir) == -1 || closedir(targDir) == -1) {
    syslog(LOG_ERR,"Wystapil blad z proba zamkniecia katalogow");
    exit(EXIT_FAILURE);
  }
}

// Funkcja dodajaca podkatalog na koniec listy podkatalogow
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
// Funkcja odczytujaca podkatalogi z katalogu i dodajaca je do listy podkatalogow
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
      char *fullSourcePath = constructFullPath(sourceDirPath, node->fileName);
      char *fullTargetPath = constructFullPath(targetDirPath, node->fileName);
      syslog(LOG_INFO,"Kopiowanie pliku: %s do: %s\n",fullSourcePath, fullTargetPath);
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
  // Iterujemy na sam koniec listy katalogow docelowych 
  while (trgDirHead->next) {
    trgDirHead = trgDirHead->next;
  }
  // Przechodzimy od konca do poczatku w liscie podkatalogow i sprawdzamy czy taki katalog istnieje w
  // katalogu docelowym
  // Iterowanie w tyl gwarantuje sprawdzenie najpierw katalogow najbardziej zaglebionych
  while (trgDirHead != NULL) {
    /* Konstruujemy sciezke i sprawdzamy czy katalog istnieje jesli nie to go usuwamy */
    char *relativePath = getRelativePath(trgDirPath, trgDirHead->path);
    char *fullPath = constructFullPath(srcDirPath, relativePath);
    if (!directoryExists(fullPath)) {
      fileList *removeList = saveFilesToList(trgDirHead->path);
      while (removeList != NULL) {
        char *removeFilePath = constructFullPath(trgDirHead->path, removeList->fileName);
        unlink(removeFilePath);
        syslog(LOG_INFO, "Usuniecie pliku: %s", removeFilePath);
        fileList *tmp = removeList;
        removeList = removeList->next;
        free(tmp);
        free(removeFilePath);
      }
      rmdir(trgDirHead->path);
      syslog(LOG_INFO,"Usuniecie katalogu: %s", trgDirHead->path);
    }
    free(fullPath);
    free(relativePath);
    trgDirHead = trgDirHead->previous;
  }
}
// Funkcja synchronizujaca rekurencyjnie katalogi
void syncRecursive(char *sourceDirPath, char *targetDirPath) {
  /* Inicjalizujemy liste podkatalogow w katalogu zrodlowym*/
  subDirList *srcDirHead = NULL;
  srcDirHead = getSubDirs(srcDirHead, sourceDirPath);
  /* Inicjalizujemy liste podkatalogow w katalogu docelowym */
  subDirList *targetDirHead = NULL;
  targetDirHead = getSubDirs(targetDirHead, targetDirPath);
  /* Inicjalizujemy pomocnicze wskazniki na pierwszy element listy podkatalogow */
  subDirList *srcDirNode = srcDirHead;
  subDirList *targetDirNode = targetDirHead;
  while (srcDirNode) {
    // Uzyskujemy sciezke wzgledna do katalogu
    char *relativePath = getRelativePath(sourceDirPath, srcDirNode->path);
    char fullTargetPath[2046];
    // Konstruujemy sciezke bezwzgledna do katalogu docelowego
    snprintf(fullTargetPath, sizeof(fullTargetPath), "%s/%s", targetDirPath,
             relativePath);
    // Otwieramy katalog podany w sciezce bezwzglednej
    DIR *dir = opendir(fullTargetPath);
    if (dir != NULL) // Jesli katalog istnieje to synchronizujemy pliki
    {
      closedir(dir);
      syncNonRecursive(srcDirNode->path, fullTargetPath);
    }
    else // Jesli katalog nie istnieje to go tworzymy i synchronizujemy pliki
    {
      syslog(LOG_INFO,"Tworzenie katalogu: %s\n", fullTargetPath);
      closedir(dir);
      struct stat info;
      stat(srcDirNode->path, &info);
      mkdir(fullTargetPath, info.st_mode);
      syncNonRecursive(srcDirNode->path, fullTargetPath);
    }
    free(relativePath);
    srcDirNode = srcDirNode->next;
  }
  // Usuwamy wszystkie katalogi i pliki, ktore nie istnieja w katalogu zrodlowym
  removeRecursive(targetDirHead, targetDirPath, sourceDirPath);
  freeSubDirList(targetDirHead);
  freeSubDirList(srcDirHead);
}

void sigusr_handler(int signum) {
  signal_received = true;
  syslog(LOG_INFO, "Wybudzenie sie demona na skutek sygnalu");
}

// Funkcja zawierajaca nieskonczona petle w ktorej wykonuje sie dzialanie demona
void daemonLoop(char *sourceDir, char *targetDir) {
  // Otwarcie dziennika systemowego
  openlog("demon", LOG_NDELAY, LOG_DAEMON);
  if (flags.recursive == true) {
    syslog(LOG_INFO, "Rozpoczeto dzialanie demona w trybie rekursywnym");
  } else {
    syslog(LOG_INFO, "Rozpoczeto dzialanie demona w trybie nierekursywnym");
  }
  while (true) {
    // Jesli flaga rekurencyjnego dzialania jest ustawiona na false to synchronizujemy nierekurencyjnie
    if (flags.recursive == false) 
    {
      syncNonRecursive(sourceDir, targetDir);
      syslog(LOG_INFO,"Zakonczono nierekurencyjne synchronizowanie katalogow");
    } 
    // Jesli flaga rekurencyjnego dzialania jest ustawiona na true to synchronizujemy rekurencyjnie 
    else 
    {
      syncRecursive(sourceDir, targetDir);
      syslog(LOG_INFO,"Zakonczono rekurencyjne synchronizowanie katalogow");
    }
    syslog(LOG_INFO, "Uspienie demona na czas %d sekund", flags.sleep_time);
    signal(SIGUSR1, sigusr_handler); // Zdefiniowanie odbioru sygnalu SIGUSR1 na obsluzenie funkcji sigusr_handler
    if (signal_received == false) 
    {
      sleep(flags.sleep_time);
    }
    signal_received = false;
    signal(SIGUSR1, SIG_IGN); // Ignorowanie sygnalu w czasie synchronizacji
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
  /* Stworzenie nowej sesji i grupy procesow */
  if (setsid() == -1)
   return -1;
  
  /* Ustawienie katalogu roboczego na katalog glowny */
  if (chdir("/") == -1)
    return -1;
  
  pid_t cpid = getpid();
  printf("Rozpoczeto dzialanie demona \nRekurencja: %s\nKatalog zrodlowy: %s\nKatalog docelowy: %s\nPID: %d\nCzas spania: %d s\nProg: %lu MB\n",
         flags.recursive ? "Tak" : "Nie",sourceDir, targetDir, cpid, flags.sleep_time, flags.threshold/1000000);
  /* Zamkniecie otwartych deskryptorow pliku */
     for (i = 0; i < NR_OPEN; i++)
          close(i);
  
  /* Przeadresowanie deskryptorow plikow 0,1,2 na /dev/null */
     open("/dev/null", O_RDWR);
      dup(0);
      dup(0);
  // Przejscie do petli obslugujacej dzialanie demona
  daemonLoop(sourceDir, targetDir);
  return 1;
}

int main(int argc, char **argv) {
  /* Odczytywanie argumentow programu i ustawianie odpowiedniej konfiguracji */
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
      fprintf(stderr,"Sposob uzycia: %s [flagi] [katalog_zrodlowy] [katalog_docelowy]",
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
  /* Sprawdzamy czy podane katalogi istnieja, czy mamy do nich dostep.. */
  checkDirs(sourceDir, targetDir);
  char sourceDirPath[PATH_MAX];
  char targetDirPath[PATH_MAX];
  realpath(sourceDir, sourceDirPath);
  realpath(targetDir, targetDirPath);
  // Uruchamiamy demona
  runDaemon(sourceDirPath, targetDirPath);
}
