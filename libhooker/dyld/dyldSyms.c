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
#include <mach-o/loader.h>
#include <mach-o/dyld.h>

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

//dyld methods
static int dyldVersion;

static uintptr_t (*dyld_ImageLoaderMachO_getSlide)(void *);
static const struct mach_header *(*dyld_ImageLoaderMachO_machHeader)(void *);
/*MegaDylib methods*/
static uintptr_t (*dyld_ImageLoaderMegaDylib_getSlide)(void*);
static void *(*dyld_ImageLoaderMegaDylib_getIndexedMachHeader)(void*, unsigned index);
static void *(*dyld_ImageLoaderMegaDylib_isCacheHandle)(void*proxy, void* handle, unsigned* index, uint8_t* flags);
static void **dyld_sAllCacheImagesProxy;
/*dyld3 methods */
static uintptr_t (*dyld3_MachOLoaded_getSlide)(const void *);
/*dyld4 methods */
static void *(*dyld4_Loader_loadAddress_RuntimeState)(const void *, const void *);
static void *dyld4_RuntimeState;
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

privateFunc bool openDyldCache(const char *dir, const char *arch, bool isDyld4, const struct dyld_cache_header *hdr){
    dyldSharedCacheLocalSymEntry = NULL;
    
    char path[PATH_MAX];
    bzero(path, PATH_MAX);
    
    char *extension = isDyld4 ? ".symbols" : "";
    
    guardRetFalse(snprintf(path, PATH_MAX, "%s%s%s%s", dir, DYLD_SHARED_CACHE_BASE_NAME, arch, extension) >= sizeof(path));
    
    int fd = open(path, O_RDONLY);
    guardRetFalse(fd < 0);
    
    __block bool success = false;
    
    defer ^{
        if (!success)
            close(fd);
    };
    
    struct dyld_cache_header fsHdr;
    guardRetFalse(read(fd, &fsHdr, sizeof(struct dyld_cache_header)) != sizeof(struct dyld_cache_header));
    
    if (isDyld4){
        guardRetFalse(memcmp(fsHdr.uuid, hdr->symbolFileUUID, sizeof(hdr->symbolFileUUID)))
    } else {
        guardRetFalse(memcmp(fsHdr.uuid, hdr->uuid, sizeof(fsHdr.uuid)));
    }
    
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
    
    if (isDyld4){
        if (read(fd, entry, entrySize) != entrySize){
            free(entry);
            return false;
        }
    } else {
        //Convert v1 entries to v2;
        
        size_t legacyEntrySize = fsSymbolInfo->entriesCount * sizeof(struct dyld_cache_local_symbols_entry);
        struct dyld_cache_local_symbols_entry *legacyEntry = malloc(legacyEntrySize);
        guardRetFalse(!legacyEntry);
        
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
        const struct dyld_cache_header *hdr = dyldSharedCacheHdr;
        if (memcmp(hdr->magic, "dyld_v1 ", 8))
            return;
        
        const char *arch = hdr->magic + 8;
        while (*arch == ' ')
            arch++;
        
        dyldSharedCacheFd = -1;
        dyldSymbolCacheInfo = NULL;
        
        if (hdr->localSymbolsSize >= sizeof(struct dyld_cache_local_symbols_info)){
#if TARGET_OS_IPHONE
            openDyldCache(IPHONE_DYLD_SHARED_CACHE_DIR, arch, false, hdr);
#else
            openDyldCache(MACOSX_DYLD_SHARED_CACHE_DIR, arch, false, hdr);
#endif
        } else if (hdr->mappingOffset >= offsetof(struct dyld_cache_header, symbolFileUUID) && hdr->subCacheArrayCount > 0) {
#if TARGET_OS_IPHONE
#define DYLD_SHARED_CACHE_DIR IPHONE_DYLD_SHARED_CACHE_DIR
#else
#define DYLD_SHARED_CACHE_DIR MACOSX_MRM_DYLD_SHARED_CACHE_DIR
#endif
            openDyldCache(DYLD_SHARED_CACHE_DIR, arch, true, hdr);
        }
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
    if (inSharedCache(hdr, &entry)){
        if (getSymsFromSharedCache(entry, slide, symbolNames, searchSyms, searchSymCount))
            return true;
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

privateFunc void inspectDyld(void){
    static dispatch_once_t onceTokenInspectDyld;
    dispatch_once(&onceTokenInspectDyld, ^{
        const struct dyld_all_image_infos *imageInfos = dyldGetAllImageInfos();
        const void *dyldHdr = imageInfos->dyldImageLoadAddress;
        
        uintptr_t dyldSlide = -1;
        dyldVersion = 0;
        
        if (dyldVersion == 0) {
            //dyld2 & dyld3
            
            const char *dyldNames[6] = {
                "__ZNK16ImageLoaderMachO8getSlideEv",
                "__ZNK16ImageLoaderMachO10machHeaderEv",
                "__ZN4dyldL20sAllCacheImagesProxyE",
                "__ZN20ImageLoaderMegaDylib13isCacheHandleEPvPjPh",
                "__ZNK20ImageLoaderMegaDylib8getSlideEv",
                "__ZNK20ImageLoaderMegaDylib20getIndexedMachHeaderEj"
            };
            void *dyldSyms[6];
            
            findSymbols(dyldHdr, dyldSlide, dyldNames, dyldSyms, 6);
            if (dyldSyms[0] && dyldSyms[1]) {
                dyldVersion = 2;
                
                dyld_ImageLoaderMachO_getSlide = signPtr(dyldSyms[0]);
                dyld_ImageLoaderMachO_machHeader = signPtr(dyldSyms[1]);
                dyld_sAllCacheImagesProxy = dyldSyms[2];
                dyld_ImageLoaderMegaDylib_isCacheHandle = signPtr(dyldSyms[3]);
                dyld_ImageLoaderMegaDylib_getSlide = signPtr(dyldSyms[4]);
                dyld_ImageLoaderMegaDylib_getIndexedMachHeader = signPtr(dyldSyms[5]);
                
                const void *libdyldHdr = NULL;
                intptr_t libdyldSlide = 0;
                for(uint32_t i = 0; i < _dyld_image_count(); i++) {
                    const char *imName = _dyld_get_image_name(i);
                    if (strcmp(imName, "/usr/lib/system/libdyld.dylib") == 0){
                        libdyldHdr = _dyld_get_image_header(i);
                        libdyldSlide = _dyld_get_image_vmaddr_slide(i);
                    }
                }

                if (libdyldHdr == NULL)
                    return;
                const char *libDyldNames[2] = {
                    "_gUseDyld3",
                    "__ZNK5dyld311MachOLoaded8getSlideEv"
                };
                void *libDyldSyms[2];
                findSymbols(libdyldHdr, libdyldSlide, libDyldNames, libDyldSyms, 2);
                if (libDyldSyms[0]){
                    dyldVersion = *(bool *)(libDyldSyms[0]) ? 3 : 2;
                    
                    dyld3_MachOLoaded_getSlide = signPtr(libDyldSyms[1]);
                }
            }
        }
        
        if (dyldVersion == 0) {
            const char *dyldNames[2] = {
                "__ZNK5dyld46Loader11loadAddressERNS_12RuntimeStateE",
                "__ZNK5dyld311MachOLoaded8getSlideEv"
            };
            void *dyldSyms[2];
            findSymbols(dyldHdr, dyldSlide, dyldNames, dyldSyms, 2);
            
            if (dyldSyms[0] && dyldSyms[1]){
                dyldVersion = 4;
            }
            
            dyld4_Loader_loadAddress_RuntimeState = signPtr(dyldSyms[0]);
            dyld3_MachOLoaded_getSlide = signPtr(dyldSyms[1]);
            
            void *libdyld = dlopen("/usr/lib/system/libdyld.dylib", RTLD_NOW);
            void *dlcloseptr = ptrauth_auth_data(dlsym(libdyld, "dlclose"), ptrauth_key_asia, 0);
            
            //quick and dirty patchfind (to find the dyld4 runtime state)
            dyld4_RuntimeState = *(void **)LHFindDyld4QuickAndDirty(dlcloseptr);
        }
        
        if (dyldVersion < 2) {
            libhooker_panic("unable to determine dyld version!!! this is really bad!");
        }
    });
}

LIBHOOKER_EXPORT struct libhooker_image *LHOpenImage(const char *path){
    inspectDyld();
    
    void *dyldHandle = dlopen(path, RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
    guardRetNull(!dyldHandle);
    
    void *image = NULL;
    switch (dyldVersion) {
        case 4:
        {
            void *strippedHandle = ptrauth_strip(dyldHandle, ptrauth_key_asda);
            void *validHandle = ptrauth_sign_unauthenticated(strippedHandle, ptrauth_key_process_dependent_data, ptrauth_string_discriminator("dlopen"));
            guardRetNull(dyldHandle != validHandle);
            image = (void *)(((uint64_t)strippedHandle) >> 1);
            break;
        }
        case 3:
            image = (void*)((((uintptr_t)dyldHandle) & (-2)) << 5);
            break;
        default:
            image = (void *)(((uintptr_t)dyldHandle) & (-4));
            break;
    }
    
    const void *imageHeader = NULL;
    uintptr_t slide = 0;
    
    unsigned index;
    uint8_t mode;
    if (dyldVersion == 4){
        imageHeader = (const void *)dyld4_Loader_loadAddress_RuntimeState(image, dyld4_RuntimeState);
        if (*(uint32_t *)imageHeader == MH_MAGIC_64){
            slide = dyld3_MachOLoaded_getSlide(imageHeader);
        }
    } else if (dyldVersion == 3){
        if (*(uint32_t *)image == MH_MAGIC_64 && dyld3_MachOLoaded_getSlide != NULL){
            imageHeader = (const void *)image;
            slide = dyld3_MachOLoaded_getSlide(imageHeader);
        }
    } else if (dyld_ImageLoaderMegaDylib_isCacheHandle != NULL && dyld_ImageLoaderMegaDylib_isCacheHandle(*dyld_sAllCacheImagesProxy, image, &index, &mode)){
        if (dyld_ImageLoaderMegaDylib_getSlide == NULL || dyld_ImageLoaderMegaDylib_getIndexedMachHeader == NULL)
        libhooker_panic("couldn't find required dyld methods\n");
        imageHeader = dyld_ImageLoaderMegaDylib_getIndexedMachHeader(*dyld_sAllCacheImagesProxy, index);
        slide = dyld_ImageLoaderMegaDylib_getSlide(*dyld_sAllCacheImagesProxy);
    } else {
       imageHeader = dyld_ImageLoaderMachO_machHeader(image);
       slide = dyld_ImageLoaderMachO_getSlide(image);
   }
    
    struct libhooker_image *libhookerImage = malloc(sizeof(struct libhooker_image));
    guardRetNull(!libhookerImage);
    
    libhookerImage->imageHeader = imageHeader;
    libhookerImage->slide = slide;
    libhookerImage->dyldHandle = dyldHandle;
    
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
