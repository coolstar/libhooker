//
//  writeMemQueue.hpp
//  libhooker
//
//  Created by CoolStar on 9/4/20.
//  Copyright © 2020 CoolStar. All rights reserved.
//

#include <stdbool.h>

#ifndef writeMemQueue_hpp
#define writeMemQueue_hpp

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __APPLE__
#include <mach/vm_param.h>
#else
extern size_t PAGE_SIZE;
extern size_t PAGE_MASK;
#endif

void *LHReadShadowMem(void *ptr, size_t size);
void *LHGetShadowPage(uintptr_t page);
bool LHCommitShadowPages(void);

#ifdef __cplusplus
}
#endif

#endif /* writeMemQueue_hpp */
