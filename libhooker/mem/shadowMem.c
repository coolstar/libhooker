//
//  shadowMem.c
//  libhooker-test
//
//  Created by CoolStar on 9/4/20.
//  Copyright © 2020 CoolStar. All rights reserved.
//

#include "funcMacros.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "shadowMem.h"
#define LIBHOOKER_PRIVATE 1
#include "libhooker.h"

extern void LHCopyMemPage(void *dst, uintptr_t page, uintptr_t off, size_t size);

void *LHReadShadowMem(void *ptr, size_t size){
    void *outBuf = malloc(size);
    
    uintptr_t page_base;
    uintptr_t offset;
    
    size_t totalBytes = 0;
    while (totalBytes < size){
        page_base = (uintptr_t)(ptr) & ~PAGE_MASK;
        offset = (uintptr_t)(ptr) - page_base;
        
        size_t bufSiz = PAGE_SIZE - offset;
        if (size - totalBytes < bufSiz){
            bufSiz = size - totalBytes;
        }
        LHCopyMemPage(outBuf + totalBytes, page_base, offset, bufSiz);
        totalBytes += bufSiz;
        ptr += bufSiz;
    }
    return outBuf;
}

bool LHWriteMemoryInternal(void *destination, const void *data, size_t size){
    uintptr_t page_base = (uintptr_t)(destination) & ~PAGE_MASK;
    size_t offset = (uintptr_t)(destination) - page_base;
    if (offset + size > PAGE_SIZE){
        bool success = true;
        size_t totalBytes = 0;
        void *ptr = destination;
        while (totalBytes < size){
            page_base = (uintptr_t)(ptr) & ~PAGE_MASK;
            offset = (uintptr_t)(ptr) - page_base;
            
            size_t bufSiz = PAGE_SIZE - offset;
            if (size - totalBytes < bufSiz){
                bufSiz = size - totalBytes;
            }
            if (!LHWriteMemoryInternal(ptr, data + totalBytes, bufSiz))
                success = false;
            totalBytes += bufSiz;
            ptr += bufSiz;
        }
        return success;
    }
    
    void *shadowPage = LHGetShadowPage(page_base);
    memcpy(shadowPage + offset, data, size);
    return true;
}

LIBHOOKER_EXPORT int LHPatchMemory(const struct LHMemoryPatch *patches, int count){
    int successCount = 0;
    for (int i = 0; i < count; i++){
        
        if (patches[i].destination == NULL || patches[i].data == NULL || patches[i].size < 1) {
            continue;
        }

        if (LHWriteMemoryInternal(patches[i].destination, patches[i].data, patches[i].size))
            successCount++;
    }
    if (!LHCommitShadowPages())
        successCount = 0;
    return successCount;
}

LIBHOOKER_EXPORT bool LHWriteMemory(void *destination, const void *data, size_t size) {
    struct LHMemoryPatch patch[] = {
        {destination, data, size}
    };
    return LHPatchMemory(patch, 1) == 1;
}
