//
//  misc.c
//  libhooker
//
//  Created by CoolStar on 2/24/20.
//  Copyright © 2020 CoolStar. All rights reserved.
//

#include "libhooker.h"
#include "funcMacros.h"

LIBHOOKER_EXPORT const char *LHVersion(void){
    return "libhooker 1.7.0. Copyright (C) 2022, CoolStar. All Rights Reserved.";
}

LIBHOOKER_EXPORT const char *LHStrError(enum LIBHOOKER_ERR err){
    switch (err) {
        case LIBHOOKER_OK:
            return "No Error";
            break;
        case LIBHOOKER_ERR_SELECTOR_NOT_FOUND:
            return "ObjC Selector Not Found";
        case LIBHOOKER_ERR_SHORT_FUNC:
            return "Function too short";
        case LIBHOOKER_ERR_BAD_INSN_AT_START:
            return "Bad instruction at function start";
        case LIBHOOKER_ERR_VM:
            return "Virtual Memory Error";
        case LIBHOOKER_ERR_NO_SYMBOL:
            return "No symbol was specified for hooking";
        default:
            return "Unknown Error";
            break;
    }
}
