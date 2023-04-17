//
//  writeMem.c
//  libhooker
//
//  Created by CoolStar on 3/8/20.
//  Copyright © 2020 CoolStar. All rights reserved.
//

#include <stdint.h>
#include <stdbool.h>
#include "funcMacros.h"
#include "writeMem.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

size_t PAGE_SIZE;
size_t PAGE_MASK;

void __attribute__((constructor))initPageSizes(){
	PAGE_SIZE = sysconf(_SC_PAGESIZE);
	PAGE_MASK = PAGE_SIZE - 1;
}

bool LHMarkMemoryWriteable(void *data){
    uintptr_t page_base = (uintptr_t)(data) & ~PAGE_MASK;
    guardRetFalseDbgPanic(mprotect((void *)page_base, PAGE_SIZE, PROT_READ | PROT_WRITE), "unable to mark as writeable");
    return true;
}

bool LHMarkMemoryExecutable(void *data){
    uintptr_t page_base = (uintptr_t)(data) & ~PAGE_MASK;
    guardRetFalseDbgPanic(mprotect((void *)page_base, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC), "unable to mark as executable");
    return true;
}

LIBHOOKER_EXPORT bool LHExecMemory(void **page, void *data, size_t size){
    *page = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, 0, 0);
    guardRetFalse(*page == NULL);
    memcpy(*page, data, size);
    return LHMarkMemoryExecutable(*page);
}

bool LHCommitMemory(uintptr_t page, const void *data, size_t size) {
    uintptr_t page_base = (uintptr_t)(page) & ~PAGE_MASK;
    guardRetFalseDbgPanic(page_base != page, "Page is not aligned!");
    guardRetFalseDbgPanic(size > PAGE_SIZE, "Size is larger than page size!");

    mprotect((void *)page, size, PROT_READ | PROT_WRITE | PROT_EXEC);
    memcpy((void *)page, data, size);

    //Hack to clear instruction cache
    int COUNT = sysconf (_SC_LEVEL1_ICACHE_LINESIZE);
    uint32_t **tempAddrs = malloc(sizeof(uint32_t *) * COUNT);
    size_t tempSize = PAGE_SIZE;
    for (int i = 0; i < COUNT; i++){
        uint32_t *temp = (uint32_t *)mmap(NULL, tempSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, 0, 0);
        usleep(0);
        for (int j = 0; j < tempSize/sizeof(uint32_t); j++){
            temp[j] = 0;
        }
        __clear_cache(temp, ((uint8_t *)temp) + tempSize);
        tempAddrs[i] = temp;
    }
    for (int i = 0; i < COUNT; i++){
        munmap(tempAddrs[i], tempSize);
    }
    free(tempAddrs);

    return true;
}
