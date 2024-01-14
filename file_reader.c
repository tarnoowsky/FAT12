#include "file_reader.h"
#include "tested_declarations.h"
#include "rdebug.h"
#include "tested_declarations.h"
#include "rdebug.h"

void setBlockCounter(struct disk_t *disk) {
    if (disk != NULL) {
        fseek(disk->filePointer, 0, SEEK_END);
        uint32_t currentPosition = ftell(disk->filePointer);
        disk->blockCounter = currentPosition / BLOCK_SIZE;
        fseek(disk->filePointer, 0, SEEK_SET);
    }
}

struct disk_t* disk_open_from_file(const char* volume_file_name) {
    if (volume_file_name == NULL) {
        errno = EFAULT;
        return NULL;
    }
    FILE *fp = fopen(volume_file_name, "rb");
    if(fp == NULL){
        errno = ENOENT;
        return NULL;
    }
    struct disk_t *disk = calloc(1, sizeof(struct disk_t));
    if (disk == NULL) {
        fclose(fp);
        errno = ENOMEM;
        return NULL;
    }
    disk->filePointer = fp;
    setBlockCounter(disk);
    return disk;
}

int disk_read(struct disk_t *pdisk, int32_t first_sector, void *buffer, int32_t sectors_to_read){
    if(pdisk == NULL || buffer == NULL || first_sector < 0 || sectors_to_read < 1){
        errno = EFAULT;
        return -1;
    }
    if(pdisk->blockCounter < (uint32_t)(first_sector + sectors_to_read)){
        errno = ERANGE;
        return -1;
    }
    fseek(pdisk->filePointer, first_sector * BLOCK_SIZE, SEEK_SET);
    int read_sectors = (int)fread(buffer, BLOCK_SIZE, sectors_to_read, pdisk->filePointer);
    return read_sectors;
}

int disk_close(struct disk_t *pdisk){
    if (pdisk == NULL) {
        errno = EFAULT;
        return -1;
    }
    if(pdisk->filePointer != NULL) {
        fclose(pdisk->filePointer);
    }
    free(pdisk);
    return 0;
}

struct superblock_t* allocateAndReadSuperBlock(struct disk_t* pdisk, uint32_t first_sector){
    struct superblock_t *superBlock = malloc(sizeof(struct superblock_t));
    if (superBlock == NULL) {
        return NULL;
    }
    if (disk_read(pdisk, first_sector, superBlock, 1) == -1) {
        free(superBlock);
        return NULL;
    }
    return superBlock;
}

struct volume_t* fat_open(struct disk_t* pdisk, uint32_t first_sector) {
    if (pdisk == NULL) {
        errno = EFAULT;
        return NULL;
    }
    struct superblock_t *superBlock = allocateAndReadSuperBlock(pdisk, first_sector);
    if(superBlock == NULL){
        return NULL;
    }
    struct volume_t *volume = calloc(1, sizeof(struct volume_t));
    if (volume == NULL) {
        free(superBlock);
        errno = ENOMEM;
        return NULL;
    }
    if(superBlock->signature != CORRECT_SIGNATURE){
        free(superBlock);
        free(volume);
        errno = EINVAL;
        return NULL;
    }
    volume->disk = pdisk;
    volume->superblock = superBlock;
    return volume;
}

int fat_close(struct volume_t *pvolume) {
    if (pvolume == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (pvolume->superblock != NULL) {
        free(pvolume->superblock);
    }
    free(pvolume);
    return 0;
}

struct clusters_chain_t *initClusterChain(size_t size) {
    struct clusters_chain_t *result = (struct clusters_chain_t *)malloc(sizeof(struct clusters_chain_t));
    if(result == NULL){
        return NULL;
    }
    uint16_t *newClusters = (uint16_t *)malloc(size * sizeof(uint16_t));
    if(newClusters == NULL){
        free(result);
        return NULL;
    }
    result->clusters = newClusters;
    return result;
}

void freeCluster(struct clusters_chain_t *chain){
    if(chain != NULL){
        if(chain->clusters != NULL){
            free(chain->clusters);
        }
        free(chain);
    }
}

struct clusters_chain_t *getChainFat12(const void * const buffer, size_t size, uint16_t first_cluster) {
    if (buffer == NULL || size < 1) {
        return NULL;
    }
    size_t fatEntries = (size * 8) / 12;
    struct clusters_chain_t *newResult = initClusterChain(fatEntries);
    if (newResult == NULL) {
        return NULL;
    }
    const uint8_t *fat = (const uint8_t *)buffer;
    uint16_t currentCluster = first_cluster;
    uint16_t count = 0;
    while (currentCluster >= 0x002 && currentCluster <= 0xFEF) {
        if (count >= fatEntries) {
            freeCluster(newResult);
            return NULL;
        }
        newResult->clusters[count++] = currentCluster;
        size_t bytePos = (currentCluster * 3) / 2;
        if (currentCluster % 2 == 0) {
            currentCluster = fat[bytePos] + ((fat[bytePos + 1] & 0x0F) << 8);
        } else {
            currentCluster = (fat[bytePos] >> 4) + (fat[bytePos + 1] << 4);
        }
        currentCluster &= 0xFFF;
    }
    newResult->size = count;
    return newResult;
}

void formatName(const char *src, char *dest) {
    int length = 0;
    for (int i = 0; i < 8 && src[i] != ' '; ++i) {
        if (!isalpha((unsigned char)src[i])) {
            *dest = '\0';
            return;
        }
        length++;
    }
    strncpy(dest, src, length);
    dest[length] = '\0';
    if (src[8] != ' ') {
        strcat(dest, ".");
        for (int i = 0; i < 3; ++i) {
            if (src[8 + i] != ' ' && !isalpha((unsigned char)src[8 + i])) {
                *dest = '\0';
                return;
            }
        }
        strncat(dest, src + 8, 3);
    }
}

int validateFileName(char *currentName, const char*fileName){
    if(currentName == NULL || fileName == NULL){
        return 0;
    }
    char formatedName[11];
    formatName(currentName, formatedName);
    if(strcmp(fileName, formatedName) == 0){
        return 1;
    }
    return 0;
}

struct file_t *file_open(struct volume_t *pvolume, const char *file_name) {
    if(pvolume == NULL || file_name == NULL) {
        errno = EFAULT;
        return NULL;
    }
    struct file_t *newFile = malloc(sizeof(struct file_t));
    if(newFile == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    struct full_dir_entry_t *rootDir = malloc(pvolume->superblock->root_dir_capacity * FAT_DIR_ENTRY_SIZE);
    if(rootDir == NULL){
        free(newFile);
        errno = ENOMEM;
        return NULL;
    }
    if(disk_read(pvolume->disk, pvolume->superblock->fats_count * pvolume->superblock->size_of_fat + pvolume->superblock->size_of_reserved_area, rootDir, pvolume->superblock->root_dir_capacity * sizeof(struct full_dir_entry_t) / BLOCK_SIZE) < 0){
        free(newFile);
        free(rootDir);
        errno = ENOMEM;
        return NULL;
    }
    for (int i = 0; i < pvolume->superblock->root_dir_capacity; i++) {
        struct full_dir_entry_t currentRoot = rootDir[i];
        char *currentName = currentRoot.name;
        if(validateFileName(currentName, file_name) == 1){
            if(currentRoot.file_attributes & 0x18){
                free(newFile);
                free(rootDir);
                errno = EISDIR;
                return NULL;
            }
            newFile->fatEntry = malloc(sizeof(struct full_dir_entry_t));
            if(newFile->fatEntry == NULL){
                free(newFile);
                free(rootDir);
                errno = ENOMEM;
                return NULL;
            }
            memcpy(newFile->fatEntry, &currentRoot, sizeof(struct full_dir_entry_t));
            free(rootDir);
            newFile->filePosition = 0;
            uint8_t *tempBuffer = malloc(pvolume->superblock->fats_count * pvolume->superblock->size_of_fat * BLOCK_SIZE);
            if (disk_read(pvolume->disk, pvolume->superblock->size_of_reserved_area, tempBuffer, pvolume->superblock->fats_count * pvolume->superblock->size_of_fat) < 0) {
                free(newFile);
                return NULL;
            }
            newFile->fatChain = getChainFat12(tempBuffer, pvolume->superblock->fats_count * pvolume->superblock->size_of_fat, newFile->fatEntry->low_order_address_of_first_cluster);
            newFile->fileVolume = pvolume;
            free(tempBuffer);
            return newFile;
        }
    }
    free(newFile);
    free(rootDir);
    errno = ENOENT;
    return NULL;
}

size_t min(size_t firstElement, size_t secondElement){
    if(firstElement < secondElement){
        return firstElement;
    }
    return secondElement;
}

size_t calculateClusterStartByte(const struct file_t *stream, size_t clusterIndex, size_t clusterOffset) {
    size_t bytesPerCluster = stream->fileVolume->superblock->sectors_per_cluster * BLOCK_SIZE;
    return ((stream->fileVolume->superblock->fats_count * stream->fileVolume->superblock->size_of_fat +
             stream->fileVolume->superblock->size_of_reserved_area) * BLOCK_SIZE) +
           (bytesPerCluster * (stream->fatChain->clusters[clusterIndex] - 2)) +
           (stream->fileVolume->superblock->root_dir_capacity * sizeof(struct full_dir_entry_t)) + clusterOffset;
}

size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream) {
    if (ptr == NULL || stream == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (size == 0 || nmemb == 0) {
        return 0;
    }
    size_t bytesPerCluster = stream->fileVolume->superblock->sectors_per_cluster * BLOCK_SIZE;
    size_t totalBytesToRead = nmemb * size;
    size_t bytesRead = 0;

    while (stream->filePosition < stream->fatEntry->file_size && bytesRead < totalBytesToRead) {
        size_t clusterIndex = stream->filePosition / bytesPerCluster;
        size_t clusterOffset = stream->filePosition % bytesPerCluster;

        size_t clusterStartByte = calculateClusterStartByte(stream, clusterIndex, clusterOffset);
        size_t bytesInCluster = bytesPerCluster - clusterOffset;
        size_t bytesToRead = min(min(bytesInCluster, totalBytesToRead - bytesRead), stream->fatEntry->file_size - stream->filePosition);

        if (fseek(stream->fileVolume->disk->filePointer, clusterStartByte, SEEK_SET) != 0) {
            errno = ERANGE;
            return -1;
        }
        size_t result = fread((char *) ptr + bytesRead, 1, bytesToRead, stream->fileVolume->disk->filePointer);
        stream->filePosition += result;
        bytesRead += result;
    }

    return bytesRead / size;
}

int32_t file_seek(struct file_t *stream, int32_t offset, int whence){
    if(stream == NULL ){
        errno = EFAULT;
        return -1;
    }
    if(whence == SEEK_SET){
        stream->filePosition = 0 + offset;
    }else if(whence == SEEK_CUR){
        stream->filePosition += offset;
    }else if(whence == SEEK_END){
        stream->filePosition = stream->fatEntry->file_size + offset;
    }
    int32_t newPosition = (int32_t )stream->filePosition;
    return newPosition;
}

int file_close(struct file_t *stream){
    if(stream == NULL){
        errno = EFAULT;
        return -1;
    }
    if(stream->fatEntry != NULL){
        free(stream->fatEntry);
    }
    if(stream->fatChain != NULL){
        freeCluster(stream->fatChain);
    }
    if(stream->fileVolume == NULL){
        fat_close(stream->fileVolume);
    }
    free(stream);
    return 0;
}

struct dir_t *dir_open(struct volume_t *pvolume, const char *dir_path){
    if(pvolume == NULL){
        errno = EFAULT;
        return NULL;
    }
    if(dir_path == NULL || strcmp(dir_path, "\\")){
        errno = ENOENT;
        return NULL;
    }
    struct dir_t *directory = malloc(sizeof(struct dir_entry_t));
    if(directory == NULL){
        errno = ENOMEM;
        return NULL;
    }
    directory->volume = pvolume;
    directory->dirPosition = 0;
    directory->dirEntry = malloc(pvolume->superblock->root_dir_capacity * FAT_DIR_ENTRY_SIZE);
    if (disk_read(pvolume->disk, pvolume->superblock->fats_count * pvolume->superblock->size_of_fat + pvolume->superblock->size_of_reserved_area, directory->dirEntry,
                  pvolume->superblock->root_dir_capacity * sizeof(struct full_dir_entry_t) / BLOCK_SIZE) < 0) {
        free(directory);
        return NULL;
    }
    return directory;
}

void setEntryInformationStructure(struct full_dir_entry_t *srcEntry, struct dir_entry_t *destEntry){
    destEntry->size = srcEntry->file_size;
    destEntry->is_archived = (srcEntry->file_attributes & 0x20);
    destEntry->is_readonly = (srcEntry->file_attributes & 0x01);
    destEntry->is_system = (srcEntry->file_attributes & 0x04);
    destEntry->is_hidden = (srcEntry->file_attributes & 0x02);
    destEntry->is_directory = (srcEntry->file_attributes & 0x10);
}

int dir_read(struct dir_t *pdir, struct dir_entry_t *pentry) {
    if (pdir == NULL || pentry == NULL) {
        errno = EFAULT;
        return -1;
    }
    struct full_dir_entry_t *dirEntries = pdir->dirEntry;
    size_t maxEntries = pdir->volume->superblock->root_dir_capacity;
    while (pdir->dirPosition < maxEntries) {
        struct full_dir_entry_t *currentEntry = &dirEntries[pdir->dirPosition++];
        formatName(currentEntry->name, pentry->name);
        if (pentry->name[0] == '\0') {
            continue;
        }
        setEntryInformationStructure(currentEntry, pentry);
        return 0;
    }
    return 1;
}

int dir_close(struct dir_t *pdir){
    if(pdir == NULL){
        return -1;
    }
    if(pdir->dirEntry != NULL){
        free(pdir->dirEntry);
    }
    free(pdir);
    return -1;
}

