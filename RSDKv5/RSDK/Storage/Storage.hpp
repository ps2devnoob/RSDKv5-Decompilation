#ifndef STORAGE_H
#define STORAGE_H

namespace RSDK
{
#define STORAGE_ENTRY_COUNT (0x1000)

// dataset type identifiers for different storage categories
enum StorageDataSets {
    DATASET_STG = 0, // stage data
    DATASET_MUS = 1, // music data
    DATASET_SFX = 2, // sound effects data
    DATASET_STR = 3, // string data
    DATASET_TMP = 4, // temporary data
    DATASET_MAX,     // total number of datasets
};

// manages memory allocation for a single dataset
struct DataStorage {
    uint32 *memoryTable;                             // pointer to memory table (legacy)
    uint32 usedStorage;                              // current memory usage in units
    uint32 storageLimit;                             // maximum allowed memory in units
    uint32 **dataEntries[STORAGE_ENTRY_COUNT];       // user pointers to allocated data
    uint32 *storageEntries[STORAGE_ENTRY_COUNT];     // internal pointers to allocated blocks
    uint32 entrySizes[STORAGE_ENTRY_COUNT];          // size of each entry in units
    uint32 entryCount;                               // number of active entries
    uint32 clearCount;                               // number of garbage collections performed
};

// dynamic array container template
template <typename T> class List
{
    T *entries   = NULL; // array of entries
    int32 count  = 0;    // current number of elements
    int32 length = 0;    // allocated capacity

public:
    List()
    {
        entries = NULL;
        count   = 0;
    }
    ~List()
    {
        if (entries) {
            free(entries);
            entries = NULL;
        }
    }
    
    // adds a new entry to the list and returns pointer to it
    T *Append()
    {
        // expand capacity if needed
        if (count == length) {
            length += 32;
            size_t len         = sizeof(T) * length;
            T *entries_realloc = (T *)realloc(entries, len);

            if (entries_realloc) {
                entries = entries_realloc;
            }
        }

        // initialize new entry
        T *entry = &entries[count];
        memset(entry, 0, sizeof(T));
        count++;
        return entry;
    }
    
    // removes entry at specified index
    void Remove(uint32 index)
    {
        // shift remaining entries down
        for (int32 i = index; i < count; i++) {
            if (i + 1 < count) {
                entries[i] = entries[i + 1];
            }
            else { 
                count--;
            }
        }

        // shrink capacity if significantly underused
        if (count < length - 32) {
            length -= 32;
            size_t len         = sizeof(T) * length;
            T *entries_realloc = (T *)realloc(entries, len);

            if (entries_realloc)
                entries = entries_realloc;
        }
    }

    // returns pointer to entry at index
    inline T *At(int32 index) { return &entries[index]; }

    // removes all entries, optionally deallocating memory
    inline void Clear(bool32 dealloc = false)
    {
        for (int32 i = count - 1; i >= 0; i--) {
            Remove(i);
        }

        if (entries && dealloc) {
            free(entries);
            entries = NULL;
        }
    }

    // returns current number of entries
    inline int32 Count() { return count; }
};

// garbage collection state variables
extern uint32 gcFrameCounter;
extern bool32 gcEnabled;

// global storage datasets
extern DataStorage dataStorage[DATASET_MAX];

// initializes all storage datasets
bool32 InitStorage();

// releases all storage and frees memory
void ReleaseStorage();

// attempts to expand storage limit for a dataset
bool32 ExpandStorage(StorageDataSets dataSet, uint32 requiredSize);

// enables or disables automatic garbage collection
void SetGCEnabled(bool32 enabled);

// updates automatic garbage collection (call every frame)
void UpdateStorageGC();

// forcefully clears all entries in a dataset
void EmergencyStorageCleanup(StorageDataSets set);

// prints current storage status (debug only)
void PrintStorageStatus();

// allocates memory from a dataset
void AllocateStorage(void **dataPtr, uint32 size, StorageDataSets dataSet, bool32 clear);

// defragments and garbage collects a dataset
void DefragmentAndGarbageCollectStorage(StorageDataSets set);

// removes and frees a storage entry
void RemoveStorageEntry(void **dataPtr);

// creates a copy reference to existing storage
void CopyStorage(uint32 **src, uint32 **dst);

// performs garbage collection on a dataset
void GarbageCollectStorage(StorageDataSets dataSet);

#if RETRO_REV0U
#include "Legacy/UserStorageLegacy.hpp"
#endif

} 

#endif