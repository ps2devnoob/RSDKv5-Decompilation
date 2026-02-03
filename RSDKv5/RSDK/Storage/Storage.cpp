#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

#if RETRO_REV0U
#include "Legacy/UserStorageLegacy.cpp"
#endif

#define HEADER(memory, header_value) memory[-HEADER_SIZE + header_value]

enum {
    HEADER_ACTIVE,      // indicates if the memory block is active
    HEADER_SET_ID,      // dataset identifier
    HEADER_DATA_OFFSET, // offset to actual data
    HEADER_DATA_LENGTH, // length of data in bytes
    HEADER_SIZE         // total header size
};

DataStorage RSDK::dataStorage[DATASET_MAX];

bool32 RSDK::InitStorage()
{
    // initialize all storage datasets
    for (int32 s = 0; s < DATASET_MAX; ++s) {
        dataStorage[s].memoryTable = NULL;
        dataStorage[s].usedStorage = 0;
        dataStorage[s].storageLimit = 0;
        dataStorage[s].entryCount = 0;
        dataStorage[s].clearCount = 0;

        // set memory limits for each dataset type
        switch (s) {
            case DATASET_STG:
                dataStorage[s].storageLimit = (12 * 1024 * 1024) / sizeof(uint32);  // 12mb for stage data
                break;
            case DATASET_TMP:
                dataStorage[s].storageLimit = (3 * 1024 * 1024) / sizeof(uint32);    // 3mb for temporary data
                break;
            case DATASET_STR:
                dataStorage[s].storageLimit = (1 * 1024 * 1024) / sizeof(uint32);    // 1mb for string data
                break;
            default:
                dataStorage[s].storageLimit = (4 * 1024 * 1024) / sizeof(uint32);    // 4mb default
        }
        
        // clear all entry arrays
        for (int32 e = 0; e < STORAGE_ENTRY_COUNT; ++e) {
            dataStorage[s].dataEntries[e] = NULL;
            dataStorage[s].storageEntries[e] = NULL;
            dataStorage[s].entrySizes[e] = 0;
        }
    }

    return true;
}

void RSDK::ReleaseStorage()
{
    // release all datasets and free their memory
    for (int32 s = 0; s < DATASET_MAX; ++s) {
        // free all entries in this dataset
        for (int32 e = 0; e < dataStorage[s].entryCount; ++e) {
            if (dataStorage[s].storageEntries[e] != NULL) {
                uint32 *data = (uint32 *)dataStorage[s].storageEntries[e];
                uint32 *header = data - HEADER_SIZE;
                free(header);
                
                // null out the user's pointer if it exists
                if (dataStorage[s].dataEntries[e] != NULL) {
                    *dataStorage[s].dataEntries[e] = NULL;
                }
            }
        }

        // reset dataset state
        dataStorage[s].memoryTable = NULL;
        dataStorage[s].usedStorage = 0;
        dataStorage[s].storageLimit = 0;
        dataStorage[s].entryCount = 0;
        dataStorage[s].clearCount = 0;
        
        // clear all entry arrays
        for (int32 e = 0; e < STORAGE_ENTRY_COUNT; ++e) {
            dataStorage[s].dataEntries[e] = NULL;
            dataStorage[s].storageEntries[e] = NULL;
            dataStorage[s].entrySizes[e] = 0;
        }
    }

#if !RETRO_USE_ORIGINAL_CODE
    // free data pack buffers if not using original code
    for (int32 p = 0; p < dataPackCount; ++p) {
        if (dataPacks[p].fileBuffer)
            free(dataPacks[p].fileBuffer);
        dataPacks[p].fileBuffer = NULL;
    }
#endif
}

void RSDK::AllocateStorage(void **dataPtr, uint32 size, StorageDataSets dataSet, bool32 clear)
{
    uint32 **data = (uint32 **)dataPtr;
    *data = NULL;

    // validate dataset index
    if ((uint32)dataSet >= DATASET_MAX) {
        return;
    }

    // align size to 4-byte boundary
    size = (size + 3) & ~3;
    
    DataStorage *storage = &dataStorage[dataSet];

    // check memory limit
    uint32 totalSize = (HEADER_SIZE * sizeof(uint32)) + size;
    uint32 totalSizeInUnits = totalSize / sizeof(uint32);
    
    // if not enough space, try garbage collection first
    if (storage->usedStorage + totalSizeInUnits > storage->storageLimit) {
        GarbageCollectStorage(dataSet);
        
        // if still not enough space, try expanding storage
        if (storage->usedStorage + totalSizeInUnits > storage->storageLimit) {
            if (!ExpandStorage(dataSet, totalSize)) {
                return;
            }
        }
    }

    // check entry limit
    if (storage->entryCount >= STORAGE_ENTRY_COUNT) {
        GarbageCollectStorage(dataSet);
        if (storage->entryCount >= STORAGE_ENTRY_COUNT) {
            return;
        }
    }

    // allocate memory with header
    uint32 *memory = (uint32 *)malloc(totalSize);
    
    if (memory == NULL) {
        return;
    }

    // setup header information
    memory[HEADER_ACTIVE] = true;
    memory[HEADER_SET_ID] = dataSet;
    memory[HEADER_DATA_OFFSET] = HEADER_SIZE;
    memory[HEADER_DATA_LENGTH] = size;

    // point to data after header
    *data = &memory[HEADER_SIZE];

    // clear memory if requested
    if (clear) {
        memset(*data, 0, size);
    }

    // register entry in storage system
    int32 entryIndex = storage->entryCount;
    storage->dataEntries[entryIndex] = data;
    storage->storageEntries[entryIndex] = *data;  // no cast needed, compatible with uint32*
    storage->entrySizes[entryIndex] = totalSizeInUnits;
    storage->entryCount++;
    storage->usedStorage += totalSizeInUnits;

    // run garbage collection if over 75% capacity
    if ((float)storage->usedStorage / (float)storage->storageLimit > 0.75f) {
        GarbageCollectStorage(dataSet);
    }
}

void RSDK::RemoveStorageEntry(void **dataPtr)
{
    // validate pointers
    if (dataPtr == NULL || *dataPtr == NULL) {
        return;
    }

    // get header and dataset info
    uint32 *data = *(uint32 **)dataPtr;
    uint32 *header = data - HEADER_SIZE;
    uint32 set = header[HEADER_SET_ID];
    
    if (set >= DATASET_MAX) {
        return;
    }

    DataStorage *storage = &dataStorage[set];
    
    // mark as inactive
    header[HEADER_ACTIVE] = false;

    // find entry in storage list
    int32 foundIdx = -1;
    for (int32 e = 0; e < storage->entryCount; ++e) {
        if (storage->storageEntries[e] == data) {
            foundIdx = e;
            break;
        }
    }

    // if found, free and remove from list
    if (foundIdx >= 0) {
        uint32 dataSize = header[HEADER_DATA_LENGTH];
        uint32 totalSize = (HEADER_SIZE * sizeof(uint32)) + dataSize;
        uint32 totalSizeInUnits = totalSize / sizeof(uint32);
        
        storage->usedStorage -= totalSizeInUnits;
        
        free(header);

        // shift remaining entries down
        for (int32 e = foundIdx; e < storage->entryCount - 1; ++e) {
            storage->dataEntries[e] = storage->dataEntries[e + 1];
            storage->storageEntries[e] = storage->storageEntries[e + 1];
            storage->entrySizes[e] = storage->entrySizes[e + 1];
        }
        
        // clear last entry
        storage->entryCount--;
        storage->dataEntries[storage->entryCount] = NULL;
        storage->storageEntries[storage->entryCount] = NULL;
        storage->entrySizes[storage->entryCount] = 0;
    }

    // null out user's pointer
    *dataPtr = NULL;
}

void RSDK::DefragmentAndGarbageCollectStorage(StorageDataSets set)
{
    // currently just calls garbage collection
    GarbageCollectStorage(set);
}

void RSDK::CopyStorage(uint32 **src, uint32 **dst)
{
    // validate destination
    if (dst == NULL || *dst == NULL) {
        return;
    }

    // get header and dataset info
    uint32 *dstPtr = *dst;
    uint32 *header = dstPtr - HEADER_SIZE;
    uint32 set = header[HEADER_SET_ID];
    
    if (set >= DATASET_MAX) {
        return;
    }

    DataStorage *storage = &dataStorage[set];
    
    // copy pointer
    *src = *dst;

    // add as new entry if space available
    if (storage->entryCount < STORAGE_ENTRY_COUNT) {
        storage->dataEntries[storage->entryCount] = src;
        storage->storageEntries[storage->entryCount] = *src;
        storage->entrySizes[storage->entryCount] = (header[HEADER_DATA_LENGTH] + (HEADER_SIZE * sizeof(uint32))) / sizeof(uint32);
        storage->entryCount++;
    }
}

void RSDK::GarbageCollectStorage(StorageDataSets set)
{
    // validate dataset
    if ((uint32)set >= DATASET_MAX) {
        return;
    }

    DataStorage *storage = &dataStorage[set];
    
    int32 validCount = 0;
    uint32 freedMemory = 0;
    
    // temporary arrays to hold valid entries
    uint32 **validDataEntries[STORAGE_ENTRY_COUNT];
    uint32 *validStorageEntries[STORAGE_ENTRY_COUNT];
    uint32 validEntrySizes[STORAGE_ENTRY_COUNT];
    
    // scan all entries and separate valid from invalid
    for (int32 e = 0; e < storage->entryCount; ++e) {
        bool32 isValid = false;
        
        // check if entry is still valid
        if (storage->dataEntries[e] != NULL && storage->storageEntries[e] != NULL) {
            uint32 *userPtr = *storage->dataEntries[e];
            
            // verify pointer still points to our storage and header is valid
            if (userPtr != NULL && userPtr == storage->storageEntries[e]) {
                uint32 *header = userPtr - HEADER_SIZE;
                if (header[HEADER_ACTIVE] && header[HEADER_SET_ID] == set) {
                    isValid = true;
                }
            }
        }
        
        // keep valid entries, free invalid ones
        if (isValid) {
            validDataEntries[validCount] = storage->dataEntries[e];
            validStorageEntries[validCount] = storage->storageEntries[e];
            validEntrySizes[validCount] = storage->entrySizes[e];
            validCount++;
        }
        else {
            // free invalid entry
            if (storage->storageEntries[e] != NULL) {
                uint32 *data = storage->storageEntries[e];
                uint32 *header = data - HEADER_SIZE;
                
                freedMemory += storage->entrySizes[e];
                free(header);
                
                // null out user pointer if it exists
                if (storage->dataEntries[e] != NULL) {
                    *storage->dataEntries[e] = NULL;
                }
            }
        }
    }
    
    // copy valid entries back to main arrays
    for (int32 e = 0; e < validCount; ++e) {
        storage->dataEntries[e] = validDataEntries[e];
        storage->storageEntries[e] = validStorageEntries[e];
        storage->entrySizes[e] = validEntrySizes[e];
    }
    
    // clear remaining entries
    for (int32 e = validCount; e < STORAGE_ENTRY_COUNT; ++e) {
        storage->dataEntries[e] = NULL;
        storage->storageEntries[e] = NULL;
        storage->entrySizes[e] = 0;
    }
    
    // update storage state
    storage->entryCount = validCount;
    storage->usedStorage -= freedMemory;
    storage->clearCount++;
}

void RSDK::EmergencyStorageCleanup(StorageDataSets set)
{
    // validate dataset
    if ((uint32)set >= DATASET_MAX) {
        return;
    }
    
    DataStorage *storage = &dataStorage[set];
    
    // free all entries
    for (int32 e = 0; e < storage->entryCount; ++e) {
        if (storage->storageEntries[e] != NULL) {
            uint32 *data = storage->storageEntries[e];
            uint32 *header = data - HEADER_SIZE;
            free(header);
            
            // null out user pointer
            if (storage->dataEntries[e] != NULL) {
                *storage->dataEntries[e] = NULL;
            }
        }
    }
    
    // reset storage state
    storage->usedStorage = 0;
    storage->entryCount = 0;
    
    // clear all arrays
    for (int32 e = 0; e < STORAGE_ENTRY_COUNT; ++e) {
        storage->dataEntries[e] = NULL;
        storage->storageEntries[e] = NULL;
        storage->entrySizes[e] = 0;
    }
}

bool32 RSDK::ExpandStorage(StorageDataSets dataSet, uint32 requiredSize)
{
    // validate dataset
    if ((uint32)dataSet >= DATASET_MAX) {
        return false;
    }
    
    DataStorage *storage = &dataStorage[dataSet];
    
    // ps2 has a total 32mb ram limit
    uint32 maxLimit = (32 * 1024 * 1024) / sizeof(uint32);
    
    // try to expand by double the required size
    uint32 newLimit = storage->storageLimit + (requiredSize / sizeof(uint32)) * 2;
    
    // cap at maximum limit
    if (newLimit > maxLimit) {
        newLimit = maxLimit;
    }
    
    // check if expansion is possible
    if (newLimit <= storage->storageLimit) {
        return false;
    }
    
    storage->storageLimit = newLimit;
    
    return true;
}

// new functions (maintain compatibility)
void RSDK::SetGCEnabled(bool32 enabled) 
{
    // simple implementation placeholder
}

void RSDK::UpdateStorageGC()
{
    static uint32 frameCounter = 0;
    frameCounter++;
    
    // run garbage collection every second (at 60fps)
    if (frameCounter >= 60) {
        for (int32 s = 0; s < DATASET_MAX; ++s) {
            // only gc if there are many entries
            if (dataStorage[s].entryCount > 10) {
                GarbageCollectStorage((StorageDataSets)s);
            }
        }
        frameCounter = 0;
    }
}

void RSDK::PrintStorageStatus()
{
    // implementation removed
}