//
//  writeMemQueue.cpp
//  libhooker
//
//  Created by CoolStar on 9/4/20.
//  Copyright © 2020 CoolStar. All rights reserved.
//

#include <cstdint>
#include <cstdlib>
#include <map>
#include <vector>
#include <cstring>
#include <pthread.h>
#include "shadowMem.h"

#if __APPLE__
#ifdef THEOS
#include "mach/mach_vm.h"
#include <malloc/_malloc.h>
#else
#include "mach_vm.h"
#endif
#endif

extern "C" bool LHCommitMemory(uintptr_t destination, const void *data, size_t size);

class memEntry {
public:
    void *buf;
    size_t sz;
    
    memEntry(){
        buf = nullptr;
    }
    
    ~memEntry(){
        if (buf != nullptr){
            free(buf);
        }
        buf = nullptr;
    }
};
static std::map<uintptr_t, memEntry *> *shadowPages = NULL;
static pthread_mutex_t shadowPagesLock = PTHREAD_MUTEX_INITIALIZER;

static void LHInitializeShadowPagesOnce(){
    if (!shadowPages)
        shadowPages = new std::map<uintptr_t, memEntry *>;
}

extern "C" void *LHGetShadowPage(uintptr_t page){
    pthread_mutex_lock(&shadowPagesLock);
    LHInitializeShadowPagesOnce();
    if (shadowPages->find(page) == shadowPages->end()){
        memEntry *entry = new memEntry;
        entry->buf = malloc(PAGE_SIZE);
        entry->sz = PAGE_SIZE;
        
        memcpy(entry->buf, (void *)page, PAGE_SIZE);
        shadowPages->insert(std::pair<uintptr_t, memEntry *>(page, entry));
    }
    pthread_mutex_unlock(&shadowPagesLock);
    return (*shadowPages)[page]->buf;
}

extern "C" bool LHCommitShadowPages(){
    pthread_mutex_lock(&shadowPagesLock);
    LHInitializeShadowPagesOnce();
    bool success = true;
    for (auto const &x : *shadowPages){
        success &= LHCommitMemory(x.first, x.second->buf, x.second->sz);
        delete x.second;
    }
    shadowPages->clear();
    pthread_mutex_unlock(&shadowPagesLock);
    return success;
}

extern "C" void LHCopyMemPage(void *dst, uintptr_t page, uintptr_t off, size_t size){
    pthread_mutex_lock(&shadowPagesLock);
    LHInitializeShadowPagesOnce();
    if (shadowPages->find(page) == shadowPages->end()){
        memcpy(dst, (void *)(page + off), size);
    } else {
        memcpy(dst, (void *)((uintptr_t)(*shadowPages)[page]->buf + off), size);
    }
    pthread_mutex_unlock(&shadowPagesLock);
}
