//
//  machWrapper.c
//  libhooker
//
//  Created by CoolStar on 6/3/20.
//  Copyright © 2020 CoolStar. All rights reserved.
//

#include "machWrapper.h"
#include "funcMacros.h"
#include <mach/mach.h>

#if TARGET_IPHONE
LIBHOOKER_EXPORT char *LHWrapFunction(int argc, char *argv[], void *functionPtr){
    exception_mask_t masks[32];
    mach_msg_type_number_t masksCnt;
    exception_handler_t handlers[32];
    exception_behavior_t behaviors[32];
    thread_state_flavor_t flavors[32];

    thread_get_exception_ports(mach_thread_self(), EXC_MASK_BAD_ACCESS, masks, &masksCnt, handlers, behaviors, flavors);
    
    char *(*function)(int, char **) = functionPtr;
    char *retVal = function(argc, argv);
    
    for (int i = 0; i < masksCnt; i++){
        thread_set_exception_ports(mach_thread_self(), EXC_MASK_BAD_ACCESS, handlers[i], behaviors[i], flavors[i]);
        mach_port_deallocate(mach_task_self(), handlers[i]);
    }
    
    return retVal;
}
#endif
