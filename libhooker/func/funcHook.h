//
//  funcHook.h
//  libhooker
//
//  Created by CoolStar on 2/24/20.
//  Copyright © 2020 CoolStar. All rights reserved.
//

#ifndef funcHook_h
#define funcHook_h

#ifndef LIBHOOKER_PRIVATE
#define LIBHOOKER_PRIVATE
#endif
#include "libhooker.h"

struct LHFunctionHookOld {
    void *function;
    void *replacement;
    void *oldptr;
    enum LHOptions options;
    int jmp_reg;
};

#endif /* funcHook_h */
