#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>  // For basename()

/************************************************************************************************/

// BPB structure
typedef struct __attribute__((packed))
{
    unsigned char BS_jmpBoot[3];
    unsigned char BS_OEMName[8];
    unsigned short BPB_BytesPerSec;
    unsigned char BPB_SecsPerClus;
    unsigned short BPB_RsvdSecCnt;
    unsigned char BPB_NumFATs;
    unsigned short BPB_RootEntCnt;
    unsigned short BPB_TotSec16;
    unsigned char BPB_Media;
    unsigned short BPB_FATSz16;
    unsigned short BPB_SecPerTrk;
    unsigned short BPB_NumHeads;
    unsigned int BPB_HiddSec;
    unsigned int BPB_TotSec32;
    unsigned int BPB_FATSz32;
    unsigned short BPB_ExtFlags;
    unsigned short BPB_FSVer;
    unsigned int BPB_RootClus;
    unsigned short BPB_FSInfo;
    unsigned short BPB_BkBootSe;
    unsigned char BPB_Reserved[12];
    unsigned char BS_DrvNum;
    unsigned char BS_Reserved1;
    unsigned char BS_BootSig;
    unsigned int BS_VollD;
    unsigned char BS_VolLab[11];
    unsigned char BS_FilSysType[8];
    unsigned char empty[420];
    unsigned short Signature_word;
} BPB;

// directory entry structure
typedef struct __attribute__((packed)) {
    unsigned char DIR_Name[11];          
    unsigned char DIR_Attr;              
    unsigned char DIR_NTRes;             
    unsigned char DIR_CrtTimeTenth;     
    unsigned short DIR_CrtTime;          
    unsigned short DIR_CrtDate;          
    unsigned short DIR_LstAccDate;       
    unsigned short DIR_FstClusHI;        
    unsigned short DIR_WrtTime;          
    unsigned short DIR_WrtDate;          
    unsigned short DIR_FstClusLO;        
    unsigned int DIR_FileSize;           
} DIR;

typedef struct OpenFile {
    char name[12];       // 8.3 format for FAT32 filenames
    unsigned int offset; // offset for read/write
    char mode[3];        // r, w, rw, wr
    char path[512];      // path to the file
} OpenFile;

/************************************************************************************************/

// array to store open files
#define MAX_OPEN_FILES 10
OpenFile openFiles[MAX_OPEN_FILES];
int openFileCount = 0;

unsigned int currentCluster = 0;  // start at root directory (BPB_RootClus)

/************************************************************************************************/

// Function to print BPB information
void print_bpb_info(BPB *bpb, FILE *fp) {
    // total clusters in data region
    unsigned int totalClusters = bpb->BPB_TotSec32 - bpb->BPB_RsvdSecCnt - (bpb->BPB_NumFATs * bpb->BPB_FATSz32);

    // print info
    printf("Root cluster position (in cluster #): %u\n", bpb->BPB_RootClus);
    printf("Bytes per sector: %u\n", bpb->BPB_BytesPerSec);
    printf("Sectors per cluster: %u\n", bpb->BPB_SecsPerClus);
    printf("Total clusters in data region: %u\n", totalClusters);
    printf("Number of entries in one FAT: %u\n", ((bpb->BPB_FATSz32 * bpb->BPB_BytesPerSec) / 4));
    printf("Size of image (in bytes): %u\n", (bpb->BPB_TotSec32 * bpb->BPB_BytesPerSec));
}

// function to manage and update the cwd path as we move between them
void update_cwd(char *path, const char *dirName) {
    // if we are to move up a directory
    if (strcmp(dirName, "..") == 0) {
        // find the last occurence of '/' in path
        // if a '/' is found and it's not the root '/', modify the path
        char *last = strrchr(path, '/');       
        if (last != NULL && last != path) {
            *last = '\0';  // remove everything after '/'
        } else {
            // don't go past the root
            strcpy(path, "/");
        }
    } else {    // we are going into a valid directory
        if (strcmp(path, "/") != 0) { // if not at root
            strcat(path, "/"); // add a '/' to separate directories
        }
        strcat(path, dirName); // add the directory name to the current path
    }
}

// Function to go to the parent directory (cd ..)
void cd_parent(FILE *fp, BPB *bpb, unsigned int *currentCluster, char *path) {
    if (*currentCluster == 0 || strcmp(path, "/") == 0) {
        // if the current cluster is 0 or the path is root "/", we are already at root
        printf("Already at root directory.\n");
        return;
    }

    unsigned int bytesPerCluster = bpb->BPB_BytesPerSec * bpb->BPB_SecsPerClus;
    unsigned int rootDirSector = bpb->BPB_RsvdSecCnt + (bpb->BPB_NumFATs * bpb->BPB_FATSz32);
    unsigned int dataRegionStart = rootDirSector * bpb->BPB_BytesPerSec;
    unsigned int clusterOffset = dataRegionStart + (*currentCluster - 2) * bytesPerCluster;

    // move file pointer to start of current cluster.
    fseek(fp, clusterOffset, SEEK_SET);
    DIR dirEntry;
    int found = 0; // track if '..' found

    // search for ".." entry
    while (fread(&dirEntry, sizeof(DIR), 1, fp) == 1) {
        // check if the directory entry is '..' by comparing first two characters.
        if (dirEntry.DIR_Name[0] == 0x2E && dirEntry.DIR_Name[1] == 0x2E) {  // ".."
            unsigned int parentCluster = dirEntry.DIR_FstClusLO | (dirEntry.DIR_FstClusHI << 16);

            if (parentCluster == 0) {
                // we are at root directory
                *currentCluster = bpb->BPB_RootClus;
            } else {
                *currentCluster = parentCluster;
            }
            found = 1;
            break;
        }
    }

    if (found) {
        update_cwd(path, "..");
    } else {
        printf("Error: Unable to find parent directory.\n");
    }
}

// function to update cwd for cd command
void change_directory(FILE *fp, BPB *bpb, char *path, char *dirName, unsigned int *currentCluster) {
    if (strcmp(dirName, ".") == 0) {
        // do nothing for "cd ."
        return;
    } else if (strcmp(dirName, "..") == 0) {
        // handle "cd .." case
        cd_parent(fp, bpb, currentCluster, path);
        return;
    }

    DIR dirEntry;
    unsigned int bytesPerCluster = bpb->BPB_BytesPerSec * bpb->BPB_SecsPerClus;
    unsigned int rootDirSector = bpb->BPB_RsvdSecCnt + (bpb->BPB_NumFATs * bpb->BPB_FATSz32);
    unsigned int dataRegionStart = rootDirSector * bpb->BPB_BytesPerSec;
    unsigned int clusterOffset = dataRegionStart + (*currentCluster - 2) * bytesPerCluster;

    int found = 0;  // directory found?

    while (*currentCluster < 0xFFFFFF8) {   // loop through all clusters
        fseek(fp, clusterOffset, SEEK_SET);

        // go through each directory entry in the cluster
        for (unsigned int offset = 0; offset < bytesPerCluster; offset += sizeof(DIR)) {
            fread(&dirEntry, sizeof(DIR), 1, fp);

            // skip unused or deleted directories
            if (dirEntry.DIR_Name[0] == 0x00 || dirEntry.DIR_Name[0] == 0xE5) {
                continue;
            }

            //
            for (int i = 0; i < 11; ++i) {
                if (dirEntry.DIR_Name[i] == ' ') {
                    dirEntry.DIR_Name[i] = '\0';
                    break;
                }
            }

            // check if we found the right directory and it is actually a directory
            if (strcmp((char *)dirEntry.DIR_Name, dirName) == 0) {
                // Check if the entry is a directory
                if (dirEntry.DIR_Attr & 0x10) {
                    *currentCluster = dirEntry.DIR_FstClusLO | (dirEntry.DIR_FstClusHI << 16);
                    found = 1;
                    break;
                } else {
                    printf("Error: '%s' is not a directory.\n", dirName);
                    return;
                }
            }
        }

        if (found) {
            update_cwd(path, dirName);
            return;
        }

        unsigned int fatOffset = bpb->BPB_RsvdSecCnt * bpb->BPB_BytesPerSec + (*currentCluster * 4);
        fseek(fp, fatOffset, SEEK_SET);
        fread(currentCluster, sizeof(unsigned int), 1, fp);
        *currentCluster &= 0x0FFFFFFF;
        clusterOffset = dataRegionStart + (*currentCluster - 2) * bytesPerCluster;
    }

    printf("Error: Directory '%s' not found.\n", dirName);
}

// function to list directory entries in the current working directory
void list_directory(FILE *fp, BPB *bpb, unsigned int currentCluster) {
    DIR dirEntry;
    unsigned int bytesPerCluster = bpb->BPB_BytesPerSec * bpb->BPB_SecsPerClus;
    unsigned int rootDirSector = bpb->BPB_RsvdSecCnt + (bpb->BPB_NumFATs * bpb->BPB_FATSz32);
    unsigned int dataRegionStart = rootDirSector * bpb->BPB_BytesPerSec;
    unsigned int clusterOffset;

    // if we are at the root directory (currentCluster == 0)
    if (currentCluster == 0) {
        clusterOffset = dataRegionStart;
    } else {
        // for normal directories, calculate the offset from the cluster number
        clusterOffset = dataRegionStart + (currentCluster - 2) * bytesPerCluster;
    }

    // loop through each cluster
    while (currentCluster < 0xFFFFFF8) { // 0xFFFFFF8 is an invalid cluster (end of chain)
        fseek(fp, clusterOffset, SEEK_SET);

        // check each directory
        for (unsigned int offset = 0; offset < bytesPerCluster; offset += sizeof(DIR)) {
            fread(&dirEntry, sizeof(DIR), 1, fp);

            // skip empty or deleted entries
            if (dirEntry.DIR_Name[0] == 0x00 || dirEntry.DIR_Name[0] == 0xE5) {
                continue;
            }

            // Null-terminate DIR_Name for proper printing
            for (int i = 0; i < 11; ++i) {
                if (dirEntry.DIR_Name[i] == ' ') {
                    dirEntry.DIR_Name[i] = '\0';
                    break;
                }
            }

            // check if file or directory
            if ((dirEntry.DIR_Attr & 0x10) == 0 && (dirEntry.DIR_Attr & 0x20) == 0) {
                continue;
            }
            printf("%s\n", dirEntry.DIR_Name);  // print
        }

        // Move to the next cluster in the chain
        unsigned int fatOffset = bpb->BPB_RsvdSecCnt * bpb->BPB_BytesPerSec + (currentCluster * 4);
        fseek(fp, fatOffset, SEEK_SET);
        fread(&currentCluster, sizeof(unsigned int), 1, fp);
        currentCluster &= 0x0FFFFFFF; // reset first high bits
        clusterOffset = dataRegionStart + (currentCluster - 2) * bytesPerCluster; // update offset for new cluster
    }
}

// function for open file
void open_file(FILE *fp, BPB *bpb, unsigned int currentCluster, const char *filename, const char *flags, const char *fatImagePath) {
    // check valid flag
    if (strcmp(flags, "-r") != 0 && strcmp(flags, "-w") != 0 &&
        strcmp(flags, "-rw") != 0 && strcmp(flags, "-wr") != 0) {
        printf("Error: Invalid mode '%s'.\n", flags);
        return;
    }

    // check if the file is already open
    for (int i = 0; i < openFileCount; i++) {
        if (strcmp(openFiles[i].name, filename) == 0) {
            printf("Error: File '%s' is already open.\n", filename);
            return;
        }
    }

    // check maximum open file limit
    if (openFileCount >= MAX_OPEN_FILES) {
        printf("Error: Maximum number of open files reached.\n");
        return;
    }

    // search for the file in the current directory
    DIR dirEntry;
    unsigned int bytesPerCluster = bpb->BPB_BytesPerSec * bpb->BPB_SecsPerClus;
    unsigned int rootDirSector = bpb->BPB_RsvdSecCnt + (bpb->BPB_NumFATs * bpb->BPB_FATSz32);
    unsigned int dataRegionStart = rootDirSector * bpb->BPB_BytesPerSec;
    unsigned int clusterOffset;

    // handle root directory differently
    if (currentCluster == 0) {
        clusterOffset = dataRegionStart;
    } else {
        clusterOffset = dataRegionStart + (currentCluster - 2) * bytesPerCluster;
    }

    fseek(fp, clusterOffset, SEEK_SET);

    for (unsigned int offset = 0; offset < bytesPerCluster; offset += sizeof(DIR)) {
        fread(&dirEntry, sizeof(DIR), 1, fp);

        // skip unused, deleted, or long name entries
        if (dirEntry.DIR_Name[0] == 0x00 || dirEntry.DIR_Name[0] == 0xE5 || (dirEntry.DIR_Attr & 0x0F) == 0x0F) {
            continue;
        }

        // Null-terminate and compare filename
        char entryName[12] = {0};
        strncpy(entryName, (char *)dirEntry.DIR_Name, 11);

        // Remove trailing spaces
        for (int i = 10; i >= 0; --i) {
            if (entryName[i] == ' ') {
                entryName[i] = '\0';
            } else {
                break;
            }
        }

        // printf("entryName='%s' vs filename='%s'\n", entryName, filename);

        if (strcmp(entryName, filename) == 0) {
            if (dirEntry.DIR_Attr & 0x10) {
                printf("Error: '%s' is a directory, not a file.\n", filename);
                return;
            }

            snprintf(openFiles[openFileCount].path, sizeof(openFiles[openFileCount].path), "/%s", fatImagePath);

            // Add the file to the open file list
            strcpy(openFiles[openFileCount].name, filename);
            strcpy(openFiles[openFileCount].mode, flags + 1);  // Skip leading '-'
            openFiles[openFileCount].offset = 0;
            openFileCount++;

            printf("File '%s' opened in mode '%s'.\n", filename, flags);
            return;
        }
    }
    printf("Error: File '%s' not found in the current directory.\n", filename);
}

// function for close file
void close_file(const char *filename) {
    int found = 0;

    // Search for the file in the open file list
    for (int i = 0; i < openFileCount; i++) {
        if (strcmp(openFiles[i].name, filename) == 0) {
            found = 1;

            // Shift the remaining files in the array to remove the entry
            for (int j = i; j < openFileCount - 1; j++) {
                openFiles[j] = openFiles[j + 1];
            }

            // Decrease the count of open files
            openFileCount--;

            printf("File '%s' closed successfully.\n", filename);
            break;
        }
    }

    // If file was not found, print an error
    if (!found) {
        printf("Error: File '%s' is not open or does not exist.\n", filename);
    }
}

// function for lsof
void lsof() {
    if (openFileCount == 0) {
        printf("No files are currently opened.\n");
        return;
    }

    // Print header for the open files list
    printf("%-5s %-12s %-5s %-10s %-50s\n", "Index", "Filename", "Mode", "Offset", "Path");

    // Loop through all open files and print their details
    for (int i = 0; i < openFileCount; i++) {
        printf("%-5d %-12s %-5s %-10u %-50s\n", 
               i,                           // Index of the open file
               openFiles[i].name,           // Filename
               openFiles[i].mode,           // Mode
               openFiles[i].offset,         // Offset
               openFiles[i].path
               );
    }
}

// Function to handle lseek [FILENAME] [OFFSET] in a single function
void lseek_file(const char *filename, unsigned int offset, FILE *fp, BPB *bpb, unsigned int currentCluster) {
    // Search for the file in the open files list
    int found = 0;
    unsigned int fileSize = 0;

    for (int i = 0; i < openFileCount; i++) {
        if (strcmp(openFiles[i].name, filename) == 0) {
            found = 1;

            // Get the file size from the directory entry
            unsigned int bytesPerCluster = bpb->BPB_BytesPerSec * bpb->BPB_SecsPerClus;
            unsigned int rootDirSector = bpb->BPB_RsvdSecCnt + (bpb->BPB_NumFATs * bpb->BPB_FATSz32);
            unsigned int dataRegionStart = rootDirSector * bpb->BPB_BytesPerSec;
            unsigned int clusterOffset = (currentCluster == 0) ? dataRegionStart : dataRegionStart + (currentCluster - 2) * bytesPerCluster;

            // Read the directory entry for the given filename
            DIR dirEntry;
            fseek(fp, clusterOffset, SEEK_SET);

            for (unsigned int entryOffset = 0; entryOffset < bytesPerCluster; entryOffset += sizeof(DIR)) {
                fread(&dirEntry, sizeof(DIR), 1, fp);

                // Skip unused, deleted, or long name entries
                if (dirEntry.DIR_Name[0] == 0x00 || dirEntry.DIR_Name[0] == 0xE5 || (dirEntry.DIR_Attr & 0x0F) == 0x0F) {
                    continue;
                }

                // Null-terminate and compare filename
                char entryName[12] = {0};
                strncpy(entryName, (char *)dirEntry.DIR_Name, 11);

                // Remove trailing spaces
                for (int i = 10; i >= 0; --i) {
                    if (entryName[i] == ' ') {
                        entryName[i] = '\0';
                    } else {
                        break;
                    }
                }

                if (strcmp(entryName, filename) == 0) {
                    // File found, retrieve the file size from the directory entry
                    fileSize = dirEntry.DIR_FileSize;
                    break;
                }
            }
            break;
        }
    }

    // If file was not found in the open files list
    if (!found) {
        printf("Error: File '%s' is not open or does not exist.\n", filename);
        return;
    }

    // Check if the offset exceeds the file size
    if (offset > fileSize) {
        printf("Error: Offset exceeds the size of the file '%s'.\n", filename);
        return;
    }

    // Update the offset for the file in the open files list
    for (int i = 0; i < openFileCount; i++) {
        if (strcmp(openFiles[i].name, filename) == 0) {
            openFiles[i].offset = offset;
            printf("Offset of file '%s' set to %u bytes.\n", filename, offset);
            return;
        }
    }
}

// Function for read command
void read_file(const char *filename, unsigned int size) {


}


/************************************************************************************************/

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s [FAT32 ISO file]\n", argv[0]);
        return 1;
    }
    if (access(argv[1], F_OK) == -1) {
        perror("Error");
        return 1;
    }
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) {
        perror("Error opening the image file");
        return 1;
    }

    BPB bpb;
    fseek(fp, 0, SEEK_SET);
    if (fread(&bpb, sizeof(BPB), 1, fp) != 1) {
        perror("Failed to read BPB structure");
        fclose(fp);
        return 1;
    }

    // initial current cluster is the root directory
    unsigned int currentCluster = bpb.BPB_RootClus;

    char *imageName = basename(argv[1]);
    char pathToImage[256] = "/";
    char *input;
    while (1) {
        printf("./%s%s> ", imageName, pathToImage);
        input = get_input();

        tokenlist *tokens = get_tokens(input);
        if (tokens->size > 0) {
            if (strcmp(tokens->items[0], "info") == 0) {
                print_bpb_info(&bpb, fp);
            } else if (strcmp(tokens->items[0], "ls") == 0) {
                list_directory(fp, &bpb, currentCluster);
            } else if (strcmp(tokens->items[0], "cd") == 0) {
                if (tokens->size > 1) {
                    change_directory(fp, &bpb, pathToImage, tokens->items[1], &currentCluster);
                } else {
                    printf("Error: No directory name provided.\n");
                }
            } else if (strcmp(tokens->items[0], "open") == 0) {
                if (tokens->size == 3) {
                    open_file(fp, &bpb, currentCluster, tokens->items[1], tokens->items[2], imageName);
                } else {
                    printf("Error: Usage: open [FILENAME] [FLAGS]\n");
                }
            } else if (strcmp(tokens->items[0], "close") == 0) {
                if (tokens->size > 1) {
                    close_file(tokens->items[1]);
                } else {
                    printf("Error: No filename provided for 'close'.\n");
                }
            } else if (strcmp(tokens->items[0], "lsof") == 0) {
                lsof();
            } else if (strcmp(tokens->items[0], "lseek") == 0) {
                if (tokens->size == 3) {
                    unsigned int offset = strtoul(tokens->items[2], NULL, 10);
                    lseek_file(tokens->items[1], offset, fp, &bpb, currentCluster);
                } else {
                    printf("Error: Usage: lseek [FILENAME] [OFFSET]\n");
                }
            } else if (strcmp(tokens->items[0], "read") == 0) {
                if (tokens->size == 3) {
                    unsigned int size = strtoul(tokens->items[2], NULL, 10); // Convert SIZE from string to unsigned int
                    read_file(tokens->items[1], size);
                } else {
                    printf("Error: Usage: read [FILENAME] [SIZE]\n");
                }
            } else if (strcmp(tokens->items[0], "exit") == 0) {
                free(input);
                free_tokens(tokens);
                break;
            }
        }
        free(input);
        free_tokens(tokens);
    }

    fclose(fp);
    return 0;
}
