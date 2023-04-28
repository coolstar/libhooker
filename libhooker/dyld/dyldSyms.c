//
//  dyldSyms.c
//  libhooker
//
//  Created by CoolStar on 8/17/19.
//  Copyright © 2019 CoolStar. All rights reserved.
//

#include "dyldSyms.h"
#include "funcMacros.h"
#include "libhooker_panic.h"

#include <TargetConditionals.h>

#include <stdlib.h>
#include <limits.h>

#include <stdio.h>
#include <sys/mman.h>

#include <mach-o/nlist.h>

#include "dyld_cache_format.h"
#include <mach-o/dyld_images.h>

#include <dlfcn.h>
#include <ptrauth.h>
#include <errno.h>

#include <dispatch/dispatch.h>

#include "dyld4Find.h"

static inline void defer_cleanup(void (^*b)(void)) { (*b)(); }
#define defer_merge(a,b) a##b
#define defer_varname(a) defer_merge(defer_scopevar_, a)
#define defer __attribute__((unused, cleanup(defer_cleanup))) void (^defer_varname(__COUNTER__))(void) =

privateFunc struct dyld_cache_header *dyldSharedCacheHdr;
privateFunc struct dyld_cache_local_symbols_info dyldSharedCacheLocalSymInfo;
privateFunc int dyldSharedCacheFd;
privateFunc struct dyld_symbol_cache_info *dyldSymbolCacheInfo;
privateFunc struct dyld_cache_local_symbols_entry_64 *dyldSharedCacheLocalSymEntry;

dispatch_semaphore_t _dyld_debugger_notif_sem;
int _dyld_notification_hook_installed = 0;

#if __LP64__
#define LHnlist nlist_64
#else
#define LHnlist nlist
#endif

privateFunc const struct dyld_all_image_infos *dyldGetAllImageInfos(void){
    struct task_dyld_info dyldInfo;
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
    if (task_info(mach_task_self_, TASK_DYLD_INFO, (task_info_t)&dyldInfo, &count) == KERN_SUCCESS){
        return (struct dyld_all_image_infos *)dyldInfo.all_image_info_addr;
    } else {
        abort();
    }
}

privateFunc const struct dyld_cache_header *getDyldCacheHeader(void){
    struct dyld_cache_header *hdr = dyldSharedCacheHdr;
    if (!hdr){
        const struct dyld_all_image_infos *allImageInfos = dyldGetAllImageInfos();
        
        dyldSharedCacheHdr = (struct dyld_cache_header *)allImageInfos->sharedCacheBaseAddress;
        hdr = dyldSharedCacheHdr;
    }
    return hdr;
}

privateFunc bool openDyldCache(const struct dyld_cache_header *hdr){
    dyldSharedCacheLocalSymEntry = NULL;
    
    // Get the filepath for the primary shared cache. Depending on the environment, this may or may not
    // be the cache that contains local symbol information
    void *_dyld_shared_cache_file_path = dlsym(dlopen(NULL, 0), "dyld_shared_cache_file_path");
    if (_dyld_shared_cache_file_path == NULL) {
        libhooker_panic("Could not find dyld_shared_cache_file_path()");
    }

    const char *cachePath = ((const char * (*)(void))_dyld_shared_cache_file_path)();
    if (cachePath == NULL || strlen(cachePath) < 1) {
        libhooker_panic("Failed to locate shared cache");
    }
    debugPrint("Shared cache location: %s\n", cachePath);
    
    // dyld4 on iOS devices store locals in a subcache located alongside the primary cache.
    // iOS Simulator and macOS use a single prelinked cache (even on dyld4).
    // Use the symbols subcache if it exists, otherwise use the primary cache
    int usesSymbolCache = hdr->mappingOffset >= offsetof(struct dyld_cache_header, symbolFileUUID) && uuid_is_null(hdr->symbolFileUUID) == 0;
    if (usesSymbolCache) {
        const char *symbolsExtension = ".symbols";
        size_t symbolCachePathLen = strlen(cachePath) + strlen(symbolsExtension);
        if (symbolCachePathLen > PATH_MAX) {
            libhooker_panic("Symbols subcache filepath length exceeds PATH_MAX\n");
        }
        
        char symbolCachePath[PATH_MAX];
        sprintf(symbolCachePath, "%s%s", cachePath, symbolsExtension);
        cachePath = symbolCachePath;
    }
    
    int fd = open(cachePath, O_RDONLY);
    if (fd < 0) {
        libhooker_panic("Failed to open the shared cache!");
    }
    
    __block bool success = false;
    
    defer ^{
        if (!success)
            close(fd);
    };
    
    struct dyld_cache_header fsHdr;
    guardRetFalse(read(fd, &fsHdr, sizeof(struct dyld_cache_header)) != sizeof(struct dyld_cache_header));
    
    struct dyld_cache_local_symbols_info *fsSymbolInfo = &dyldSharedCacheLocalSymInfo;
    lseek(fd, fsHdr.localSymbolsOffset, SEEK_SET);
    if (read(fd, fsSymbolInfo, sizeof(struct dyld_cache_local_symbols_info)) != sizeof(struct dyld_cache_local_symbols_info)){
        bzero(fsSymbolInfo, sizeof(struct dyld_cache_local_symbols_info));
        return false;
    }
    
    if (fsSymbolInfo->nlistOffset > fsHdr.localSymbolsSize ||
        fsSymbolInfo->nlistCount > (fsHdr.localSymbolsSize - fsSymbolInfo->nlistOffset)/sizeof(struct LHnlist) ||
        fsSymbolInfo->stringsOffset > fsHdr.localSymbolsSize ||
        fsSymbolInfo->stringsSize > fsHdr.localSymbolsSize - fsSymbolInfo->stringsOffset){
        bzero(fsSymbolInfo, sizeof(struct dyld_cache_local_symbols_info));
        return false;
    }
    
    
    struct dyld_cache_local_symbols_entry_64 *entry;
    size_t entrySize = fsSymbolInfo->entriesCount * sizeof(struct dyld_cache_local_symbols_entry_64);
    entry = malloc(entrySize);
    guardRetFalse(!entry);
    
    lseek(fd, hdr->localSymbolsOffset + fsSymbolInfo->entriesOffset, SEEK_SET);
    
    if (usesSymbolCache){
        if (read(fd, entry, entrySize) != entrySize){
            free(entry);
            return false;
        }
    } else {
        //Convert v1 entries to v2;
        
        size_t legacyEntrySize = fsSymbolInfo->entriesCount * sizeof(struct dyld_cache_local_symbols_entry);
        struct dyld_cache_local_symbols_entry *legacyEntry = malloc(legacyEntrySize);
        if (!legacyEntry) {
            free(entry);
            return false;
        }
        
        if (read(fd, legacyEntry, legacyEntrySize) != legacyEntrySize){
            free(entry);
            free(legacyEntry);
            return false;
        }
        
        for (int i = 0; i < fsSymbolInfo->entriesCount; i++){
            entry[i].dylibOffset = legacyEntry[i].dylibOffset;
            entry[i].nlistStartIndex = legacyEntry[i].nlistStartIndex;
            entry[i].nlistCount = legacyEntry[i].nlistCount;
        }
        
        free(legacyEntry);
        legacyEntry = NULL;
    }
    
    success = true;
    
    dyldSharedCacheFd = fd;
    dyldSharedCacheLocalSymEntry = entry;
    
    dyldSymbolCacheInfo = malloc(sizeof(struct dyld_symbol_cache_info));
    dyldSymbolCacheInfo->localSymbolsOffset = fsHdr.localSymbolsOffset;
    dyldSymbolCacheInfo->localSymbolsSize = fsHdr.localSymbolsSize;
    
    return true;
}

privateFunc void loadSharedCache(void){
    
    static dispatch_once_t onceTokenLoadSharedCache;
    dispatch_once(&onceTokenLoadSharedCache, ^{
        
        dyldSharedCacheFd = -1;
        dyldSymbolCacheInfo = NULL;
        
        // iOS 16.2+ simulator zereos sharedCacheBaseAddress. hdr may be NULL
        const struct dyld_cache_header *hdr = dyldSharedCacheHdr;
        if (hdr == NULL || memcmp(hdr->magic, "dyld_v1 ", 8)) {
            return;
        }

        openDyldCache(hdr);
    });
}

privateFunc void *symToPtr(const struct LHnlist *symbol, uintptr_t slide){
    uintptr_t addr = symbol->n_value + slide;
    return (void *)addr;
}

privateFunc bool parseSymtab(const struct LHnlist *symbols,
                             const char *strings,
                             size_t symbolCount,
                             uintptr_t slide,
                             const char **symbolNames,
                             void **searchSyms,
                             size_t searchSymCount){
    int foundSyms = 0;
    
    for (uint32_t i = 0; i < symbolCount; i++){
        const struct LHnlist *symbol = &symbols[i];
        const char *name = (symbol->n_un.n_strx != 0) ? strings + symbol->n_un.n_strx : "";
        if (symbol->n_sect == NO_SECT || (symbol->n_type & N_SECT) == 0)
            continue;
        for (size_t j = 0; j < searchSymCount; j++){
            if (!searchSyms[j] && !strcmp(name, symbolNames[j])){
                searchSyms[j] = symToPtr(symbol, slide);
                if (++foundSyms == searchSymCount)
                    return true;
            }
        }
    }
    return false;
}

privateFunc bool inSharedCache(const void *hdr, struct dyld_cache_local_symbols_entry_64 *entry){
    const struct dyld_cache_header *cacheHdr = getDyldCacheHeader();
    
    loadSharedCache();
    
    int fd = dyldSharedCacheFd;
    guardRetFalse(fd == -1);
    
    struct dyld_cache_local_symbols_entry_64 localEntry;
    for (uint32_t i = 0; i < dyldSharedCacheLocalSymInfo.entriesCount; i++){
        localEntry = dyldSharedCacheLocalSymEntry[i];
        if (localEntry.dylibOffset == (uintptr_t)hdr - (uintptr_t)cacheHdr){
            *entry = localEntry;
            return true;
        }
    }
    return false;
}

privateFunc bool getSymsFromSharedCache(struct dyld_cache_local_symbols_entry_64 entry,
                                        uintptr_t slide,
                                        const char **symbolNames,
                                        void **searchSyms,
                                        size_t searchSymCount){
    int fd = dyldSharedCacheFd;
    guardRetFalse(fd == -1);
    
    const struct dyld_symbol_cache_info *symbolCacheInfo = dyldSymbolCacheInfo;
    
    guardRetFalse(entry.nlistStartIndex > dyldSharedCacheLocalSymInfo.nlistCount ||
             dyldSharedCacheLocalSymInfo.nlistCount - entry.nlistStartIndex < entry.nlistCount);
    
    int pageMask = getpagesize() - 1;
    int pageOffset = symbolCacheInfo->localSymbolsOffset & pageMask;
    off_t mapOffset = symbolCacheInfo->localSymbolsOffset & ~pageMask;
    size_t mapSize = ((symbolCacheInfo->localSymbolsOffset + symbolCacheInfo->localSymbolsSize + pageMask) & ~pageMask) - mapOffset;
    void *mapping = mmap(NULL, mapSize, PROT_READ, MAP_SHARED, fd, mapOffset);
    guardRetFalse(mapping == MAP_FAILED);
    
    char *symbolData = (char *)mapping + pageOffset;
    
    void *rawSymbols = (void *)(symbolData + dyldSharedCacheLocalSymInfo.nlistOffset);
    const struct LHnlist *symbols = rawSymbols + (entry.nlistStartIndex * sizeof(struct LHnlist));
    const char *strings = symbolData + dyldSharedCacheLocalSymInfo.stringsOffset;
    size_t symbolCount = entry.nlistCount;
    
    bool retVal = parseSymtab(symbols, strings,
                symbolCount, slide,
                symbolNames, searchSyms,
                searchSymCount);
    
    munmap(mapping, mapSize);
    return retVal;
}

privateFunc bool findSymbols(const void *hdr,
                             uintptr_t slide,
                             const char **symbolNames,
                             void **searchSyms,
                             size_t searchSymCount){
    memset(searchSyms, 0, sizeof(void *) * searchSymCount);
    
    struct dyld_cache_local_symbols_entry_64 entry;
    if (inSharedCache(hdr, &entry)) {
        // TODO: don't silenty return a symbol from the local symtable if the provided header is in the cache but the given
        // symbol was not found
        if (getSymsFromSharedCache(entry, slide, symbolNames, searchSyms, searchSymCount)) {
            return true;
        }
    }

#if __LP64__
    const struct mach_header_64 *machHeader = hdr;
    guardRetFalse(machHeader->magic != MH_MAGIC_64);
#else
    const struct mach_header *machHeader = hdr;
    guardRetFalse(machHeader->magic != MH_MAGIC);
#endif
    
    uint32_t ncmds = machHeader->ncmds;
    struct load_command *loadCmd = (void *)(machHeader + 1);
    struct symtab_command *symtabCmd = NULL;
    for (uint32_t i = 0; i < ncmds; i++){
        if (loadCmd->cmd == LC_SYMTAB){
            symtabCmd = (struct symtab_command *)loadCmd;
            break;
        }
        loadCmd = (void *)loadCmd + loadCmd->cmdsize;
    }
    
    guardRetFalse(symtabCmd == NULL);
    
    struct LHnlist *symbols = NULL;
    const char *strings = NULL;
    loadCmd = (void *)(machHeader + 1);
    for (uint32_t i = 0; i < ncmds; i++){
#if __LP64__
        if (loadCmd->cmd == LC_SEGMENT_64){
            struct segment_command_64 *seg = (void *)loadCmd;
#else
        if (loadCmd->cmd == LC_SEGMENT){
            struct segment_command *seg = (void *)loadCmd;
#endif
            if (symtabCmd->symoff - seg->fileoff < seg->filesize)
                symbols = (void *)seg->vmaddr + symtabCmd->symoff - seg->fileoff;
            if (symtabCmd->stroff - seg->fileoff < seg->filesize)
                strings = (void *)seg->vmaddr + symtabCmd->stroff - seg->fileoff;
            if (slide == -1 && !strcmp(seg->segname, "__TEXT")){
                slide = (uintptr_t)hdr - seg->vmaddr + seg->fileoff;
            }
            if (symbols && strings)
                break;
        }
        loadCmd = (void *)loadCmd + loadCmd->cmdsize;
    }
    
    guardRetFalse(symbols == NULL || strings == NULL);
    
    symbols = (void *)symbols + slide;
    strings = (void *)strings + slide;
    
    return parseSymtab(symbols, strings,
                symtabCmd->nsyms, slide,
                symbolNames, searchSyms,
                searchSymCount);
}

privateFunc void new_dyld_debugger_notification(enum dyld_image_mode mode, uint32_t infoCount, const struct dyld_image_info info[]) {
    // The image list is populated and ready to be used
    if (_dyld_debugger_notif_sem) {
        dispatch_semaphore_signal(_dyld_debugger_notif_sem);
    }
}

LIBHOOKER_EXPORT struct libhooker_image *LHOpenImage(const char *path){
    
    // Attempt dlopen() early to ensure the path exists and image is applicable for mapping before going further
    void *imageHandle = dlopen(path, RTLD_NOW);
    if (!imageHandle) {
        libhooker_log("The image at path %s failed to open with error: %s\n", path, dlerror());
        return NULL;
    }
    
    /*
     Locate the mach header and slide for the dlopen() handle to the image.
     The _dyld_* APIs (like _dyld_get_image_header(idx)) are technically not threadsafe on dyld3 and below because the underlaying std::vector used by those functions does not perform locks before index-based accesses
     and they have the potential to be modified during enumeration, though Apples still uses them within dyld without much consideration to this.
     libhooker previously did a lot of work to access private functions on the dlopen handle to bypass those higher level APIs, but the functions are unstable with frequent change between iOS/macOS versions, which is
     a burden to maintain and keep up to date. To avoid both the higher level APIs and the private dyld symbols, image information will get fetched from the dyld_all_image_infos structure.
     
     From mach-o/dyld_images.h:
        
     For a snapshot of what images are currently loaded, the infoArray fields contain a pointer an array of all images. ** If infoArray is NULL, it means it is being modified, come back later. **.
     To be notified of changes, gdb sets a break point on the address pointed to by the notification field. The function it points to is called by dyld with an array of information about what images
     h ave been added (dyld_image_adding) or are about to be removed (dyld_image_removing).
     
     The notification is called after infoArray is updated.  This means that if gdb attaches to a process and infoArray is NULL, gdb can set a break point on notification and let the proccess continue to
     run until the break point.  Then gdb can inspect the full infoArray.
     */
    
    // Given the above, we can assume that as long as infoArray is not NULL, it can be relied on to not change during enumeration. If infoArray is NULL, set a breakpoint on _dyld_debugger_notification.
    // This will fire when the image list is no longer being modified.
    
    const void *imageMachHeaderAddr = NULL;
    uintptr_t slide = 0;
    
    const struct dyld_all_image_infos *imageInfos = dyldGetAllImageInfos();
    if (imageInfos->infoArray == NULL) {
        
        // The image list has not been populated or is currently being modified. Setup to be notified when this is complete
        _dyld_debugger_notif_sem = dispatch_semaphore_create(0);
        
        if (_dyld_notification_hook_installed == 0) {

            const char *dyldNames[1] = {
                "__dyld_debugger_notification"
            };
            void *dyldSyms[1];
            findSymbols(imageInfos->dyldImageLoadAddress, -1, dyldNames, dyldSyms, 1);
            if (dyldSyms[0] == NULL) {
                libhooker_log("imageInfos->infoArray is NULL and __dyld_debugger_notification could not be found.\n");
                return NULL;
            }
            
            void *__dyld_debugger_notification = signPtr(dyldSyms[0]);
            struct LHFunctionHook hooks[] = {
                {__dyld_debugger_notification, &new_dyld_debugger_notification, NULL}
            };
            
            if (LHHookFunctions(hooks, 1) == 1) {
                _dyld_notification_hook_installed = 1;
            }
        }
        
        // Wait a small amount of time to see if dyld notifies of image build completion
        dispatch_semaphore_wait(_dyld_debugger_notif_sem, (dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC))));
    }
    
    // Try to access the image infos list again. If it's still NULL, bail
    if (imageInfos->infoArray == NULL) {
        libhooker_log("imageInfos->infoArray is NULL and __dyld_debugger_notification did not fire in a reasonable amount of time. Bailing\n.");
        return NULL;
    }
    
    // We can trust that infoArray is populated and won't change out from under us
    for (int i = 0; i < imageInfos->infoArrayCount; i++) {
        // Sanity check. Shouldn't be necessary though
        if (imageInfos->infoArray == NULL) {
            break;
        }
        
        // Look for the correct image by filepath. A substring search is performed instead of strcmp because Simulator and iOSOnMac
        // use a path relative to macOS
        // TODO: maybe strcmp on ios
        const struct dyld_image_info info = imageInfos->infoArray[i];
        if (strstr(info.imageFilePath, path)) {
            imageMachHeaderAddr = info.imageLoadAddress;
            break;
        }
    }
    
    if (imageMachHeaderAddr == NULL) {
        libhooker_log("Opening the image at %s yielded a handle but the associated mach_header could not be found.\n", path);
        return NULL;
    }
    
    // Header was located. Find the image's slide by walking load commands
#if __LP64__
    const struct mach_header_64 *machHeader = imageMachHeaderAddr;
    uint32_t headerSize = sizeof(struct mach_header_64);
    guardRetFalse(machHeader->magic != MH_MAGIC_64);
#else
    const struct mach_header *machHeader = imageMachHeaderAddr;
    uint32_t headerSize = sizeof(struct mach_header);
    guardRetFalse(machHeader->magic != MH_MAGIC);
#endif
    
    struct load_command *loadCmd = (struct load_command *)(((char *)machHeader) + headerSize);
    for (uint32_t i = 0; i < machHeader->ncmds; i++){
        if (loadCmd->cmd == LC_SEGMENT_64) {
            
            struct segment_command_64 *seg = (struct segment_command_64 *)loadCmd;
            if (strcmp(seg->segname, "__TEXT") == 0 && seg->filesize != 0) {
                slide = (uintptr_t)imageMachHeaderAddr - seg->vmaddr;
                break;
            }
        }

        loadCmd = (void *)loadCmd + loadCmd->cmdsize;
    }
    
    // Build the libhooker_image struct containing this image's dlopen handle, mach_header, and slide
    struct libhooker_image *libhookerImage = malloc(sizeof(struct libhooker_image));
    guardRetNull(!libhookerImage);
    
    libhookerImage->imageHeader = machHeader;
    libhookerImage->slide = slide;
    libhookerImage->dyldHandle = imageHandle;
    
    return libhookerImage;
}

LIBHOOKER_EXPORT void LHCloseImage(struct libhooker_image *libhookerImage){
    dlclose(libhookerImage->dyldHandle);
    free(libhookerImage);
}

LIBHOOKER_EXPORT bool LHFindSymbols(struct libhooker_image *libhookerImage,
                  const char **symbolNames,
                  void **searchSyms,
                  size_t searchSymCount){
    return findSymbols(libhookerImage->imageHeader, libhookerImage->slide,
                symbolNames, searchSyms, searchSymCount);
}

#if LIBHOOKER_NEXT
LIBHOOKER_EXPORT const char *LHAddrExecutable(uintptr_t memAddr){
    const char *executableName = "";
    
    uint32_t imageCount = _dyld_image_count();
    for (uint32_t i = 0; i < imageCount; i++){
#if __LP64__
        const struct mach_header_64 *machHeader = (const struct mach_header_64 *)_dyld_get_image_header(i);
#else
        const struct mach_header *machHeader = _dyld_get_image_header(i);
#endif
        const struct load_command *loadCmd = (const struct load_command *)(machHeader + 1);
        for (int j = 0; j < machHeader->ncmds; j++){
#if __LP64__
            if (loadCmd->cmd == LC_SEGMENT_64){
                const struct segment_command_64 *segmentCmd = (struct segment_command_64 *)loadCmd;
#else
            if (loadCmd->cmd == LC_SEGMENT){
                const struct segment_command *segmentCmd = (struct segment_command *)loadCmd;
#endif
                
                intptr_t slide = _dyld_get_image_vmaddr_slide(i);
                uintptr_t baseAddr = segmentCmd->vmaddr + slide;
                if (memAddr >= baseAddr && memAddr <= (baseAddr + segmentCmd->vmsize)){
                    executableName = _dyld_get_image_name(i);
                }
            }
            loadCmd = (void *)loadCmd + loadCmd->cmdsize;
        }
    }
    return executableName;
}
#endif
