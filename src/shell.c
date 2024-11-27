#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

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

// FAT32 Directory Entry structure
typedef struct __attribute__((packed))
{
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
} DirectoryEntry;

// Function to print BPB information
void print_bpb_info(BPB *bpb, FILE *fp) {
    unsigned int totalClusters = bpb->BPB_TotSec32 - bpb->BPB_RsvdSecCnt - (bpb->BPB_NumFATs * bpb->BPB_FATSz32);
    printf("Root cluster position (in cluster #): %u\n", bpb->BPB_RootClus);
    printf("Bytes per sector: %u\n", bpb->BPB_BytesPerSec);
    printf("Sectors per cluster: %u\n", bpb->BPB_SecsPerClus);
    printf("Total clusters in data region: %u\n", totalClusters);
    printf("Number of entries in one FAT: %u\n", ((bpb->BPB_FATSz32 * bpb->BPB_BytesPerSec) / 4));
    printf("Size of image (in bytes): %u\n", (bpb->BPB_TotSec32 * bpb->BPB_BytesPerSec));
}

// Cleanup function
void cleanup_and_exit(BPB *bpb, FILE *fp) {
    if (fp != NULL) {
        fclose(fp);
    }
    exit(0);
}

// Helper to trim and convert a FAT32 name to a string
void fat32_name_to_string(char *dest, unsigned char *src) {
    strncpy(dest, (char *)src, 11);
    dest[11] = '\0';
    for (int i = 10; i >= 0 && isspace(dest[i]); i--) {
        dest[i] = '\0';
    }
}

// Function to change directory
void change_directory(char *dirname, char *current_path, FILE *fp, BPB *bpb) {
    // Stub for directory lookup and validation
    if (strcmp(dirname, ".") == 0) {
        return;
    } else if (strcmp(dirname, "..") == 0) {
        char *last_slash = strrchr(current_path, '/');
        if (last_slash != NULL && last_slash != current_path) {
            *last_slash = '\0';
        } else {
            strcpy(current_path, "/");
        }
        return;
    }

    // Check if the directory exists (Stub - expand this based on FAT32 parsing)
    printf("Attempting to change directory to '%s'\n", dirname);
    strcat(current_path, "/");
    strcat(current_path, dirname);
}

// Function to list directory entries
void list_directory(FILE *fp, BPB *bpb) {
    unsigned int rootDirSector = bpb->BPB_RsvdSecCnt + (bpb->BPB_NumFATs * bpb->BPB_FATSz32);
    unsigned int rootDirOffset = rootDirSector * bpb->BPB_BytesPerSec;

    fseek(fp, rootDirOffset, SEEK_SET);

    DirectoryEntry entry;
    char name[12];
    while (fread(&entry, sizeof(DirectoryEntry), 1, fp) == 1) {
        if (entry.DIR_Name[0] == 0x00) break; // End of directory
        if (entry.DIR_Name[0] == 0xE5) continue; // Deleted entry

        fat32_name_to_string(name, entry.DIR_Name);
        printf("%s\n", name);
    }
}

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

    char *image_name = strrchr(argv[1], '/');
    image_name = image_name ? image_name + 1 : argv[1];
    char current_path[256] = "/";
    char input[256];

    // Shell interface
    while (1) {
        printf("%s%s> ", image_name, current_path);
        if (fgets(input, sizeof(input), stdin) == NULL) {
            perror("Error reading input");
            cleanup_and_exit(&bpb, fp);
        }

        // Remove trailing newline character
        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "exit") == 0) {
            cleanup_and_exit(&bpb, fp);
        } else if (strcmp(input, "info") == 0) {
            print_bpb_info(&bpb, fp);
        } else if (strcmp(input, "ls") == 0) {
            list_directory(fp, &bpb);
        } else if (strncmp(input, "cd ", 3) == 0) {
            change_directory(input + 3, current_path, fp, &bpb);
        } else {
            printf("Invalid command\n");
        }
    }

    return 0;
}
