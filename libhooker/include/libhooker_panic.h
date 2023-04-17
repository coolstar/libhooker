//
//  libhooker_panic.h
//  libhooker
//
//  Created by CoolStar on 2/9/20.
//  Copyright © 2020 CoolStar. All rights reserved.
//

#if __APPLE__
#include <os/log.h>
#else
#include <stdio.h>
#endif
#include <stdlib.h>

#ifndef libhooker_panic_h
#define libhooker_panic_h

#if __APPLE__
#define libhooker_log(...) os_log_error(OS_LOG_DEFAULT, "libhooker: "__VA_ARGS__)
#define libhooker_panic(...) do { os_log_error(OS_LOG_DEFAULT, "libhooker: "__VA_ARGS__); \
    abort(); } while (false)
#else
#define libhooker_log(...) fprintf(stderr, "libhooker: "__VA_ARGS__)
#define libhooker_panic(...) do { fprintf(stderr, "libhooker: "__VA_ARGS__); \
abort(); } while (false)
#endif

#if DEBUG
#define libhooker_dbgpanic libhooker_panic
#else
#define libhooker_dbgpanic(...)
#endif

#endif /* libhooker_panic_h */
