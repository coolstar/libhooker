//
//  dyldSyms.h
//  libhooker
//
//  Created by CoolStar on 8/17/19.
//  Copyright © 2019 CoolStar. All rights reserved.
//
#include <stdint.h>
#ifndef LIBHOOKER_PRIVATE
#define LIBHOOKER_PRIVATE
#endif
#include "libhooker.h"

#ifndef dyldSyms_h
#define dyldSyms_h

#if DEBUG
const struct dyld_all_image_infos *dyld_get_all_image_infos(void);
const struct dyld_cache_header *getDyldCacheHeader(void);
void loadSharedCache(void);
void inspectDyld(void);
#endif

struct libhooker_image {
    const void *imageHeader;
    uintptr_t slide;
    void *dyldHandle;
};

struct dyld_symbol_cache_info {
    uint64_t localSymbolsOffset;
    uint64_t localSymbolsSize;
};

struct libhooker_image *LHOpenImage(const char *path);
void LHCloseImage(struct libhooker_image *libhookerImage);

bool LHFindSymbols(struct libhooker_image *libhookerImage,
                  const char **symbolNames,
                  void **searchSyms,
                  size_t searchSymCount);

#if LIBHOOKER_NEXT
const char *LHAddrExecutable(uintptr_t memAddr);
#endif

#endif /* dyldSyms_h */
