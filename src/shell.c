#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

int main(int argc, char *argv[]){
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
    if (image_name == NULL) {
        image_name = argv[1];
    } else {
        image_name++;
    }
    char current_path[256] = "/";


    // shell interface
    while(1){
        printf("%s%s> ", image_name, current_path);
        if (fgets(input, sizeof(input), stdin) == NULL) {
            perror("Error reading input");
            cleanup_and_exit(&bpb, fp);
        }
        else if (fgets(input, sizeof(input), stdin) == "exit") {;
            free(input);
            free_tokens(tokens);
            break;
        }
        else if (fgets(input, sizeof(input), stdin) == "info") {
            print_bpb_info(&bpb, fp);
        }
        else {
            printf("Invalid command\n");
        }

        free(input);
        free_tokens(tokens);
    }

    fclose(fp);
    return 0;
}
