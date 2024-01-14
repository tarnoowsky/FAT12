#ifndef TEST_FILE_READER_H
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <errno.h>

#define BLOCK_SIZE 512
#define CORRECT_SIGNATURE 0xAA55
#define FAT_DIR_ENTRY_SIZE 32

struct disk_t {
    FILE *filePointer;
    uint32_t blockCounter;
};

void setBlockCounter(struct disk_t *disk);

struct disk_t *disk_open_from_file(const char *volume_file_name);

int disk_read(struct disk_t *pdisk, int32_t first_sector, void *buffer, int32_t sectors_to_read);

int disk_close(struct disk_t *pdisk);

struct superblock_t{
    uint8_t jump_code[3];
    char oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t size_of_reserved_area;
    uint8_t fats_count;
    uint16_t root_dir_capacity;
    uint16_t sectors_count;
    uint8_t media_type;
    uint16_t size_of_fat;
    uint16_t sectors_per_track;
    uint16_t heads_count;
    uint32_t sectors_before_partition_count;
    uint32_t sectors_in_filesystem_count;
    uint8_t drive_number;
    uint8_t not_used_1;
    uint8_t boot_signature;
    uint32_t volume_serial_number;
    char volume_label[11];
    char type_level[8];
    uint8_t not_used_2[448];
    uint16_t signature;
}__attribute__((packed));

struct volume_t {
    struct superblock_t *superblock;
    struct disk_t *disk;
};

struct superblock_t* allocateAndReadSuperBlock(struct disk_t* pdisk, uint32_t first_sector);

struct volume_t *fat_open(struct disk_t *pdisk, uint32_t first_sector);

int fat_close(struct volume_t *pvolume);

struct clusters_chain_t {
    uint16_t *clusters;
    size_t size;
};

struct clusters_chain_t *initClusterChain(size_t size);

void freeCluster(struct clusters_chain_t *chain);

struct clusters_chain_t *getChainFat12(const void * const buffer, size_t size, uint16_t first_cluster);

struct full_dir_entry_t {
    char name[11];
    uint8_t file_attributes;
    uint8_t reserved;
    uint8_t file_creation_time;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t access_date;
    uint16_t high_order_address_of_first_cluster;
    uint16_t modified_time;
    uint16_t modified_date;
    uint16_t low_order_address_of_first_cluster;
    uint32_t file_size;
} __attribute__(( packed ));

struct file_t {
    struct volume_t *fileVolume;
    struct full_dir_entry_t *fatEntry;
    struct clusters_chain_t *fatChain;
    size_t filePosition;
};
void formatName(const char *src, char *dest);

int validateFileName(char *currentName, const char*fileName);

struct file_t *file_open(struct volume_t *pvolume, const char *file_name);

size_t min(size_t firstElement, size_t secondElement);

size_t calculateClusterStartByte(const struct file_t *stream, size_t clusterIndex, size_t clusterOffset);

size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream);

int32_t file_seek(struct file_t *stream, int32_t offset, int whence);

int file_close(struct file_t *stream);

struct dir_entry_t {
    char name[11];
    uint32_t size;
    uint8_t is_archived;
    uint8_t is_readonly;
    uint8_t is_system;
    uint8_t is_hidden;
    uint8_t is_directory;
};

struct dir_t{
    struct full_dir_entry_t *dirEntry;
    struct volume_t *volume;
    size_t dirPosition;
};

struct dir_t *dir_open(struct volume_t *pvolume, const char *dir_path);

void setEntryInformationStructure(struct full_dir_entry_t *srcEntry, struct dir_entry_t *destEntry);

int dir_read(struct dir_t *pdir, struct dir_entry_t *pentry);

int dir_close(struct dir_t *pdir);

#define TEST_FILE_READER_H

#endif //TEST_FILE_READER_H
