//
//  funcMacros.h
//  libhooker
//
//  Created by CoolStar on 8/17/19.
//  Copyright © 2019 CoolStar. All rights reserved.
//

#include "libhooker_panic.h"

#ifndef funcMacros_h
#define funcMacros_h

#if __APPLE__
    #define LIBHOOKER_PRESERVECODESIGN 0
#else
    #define LIBHOOKER_PRESERVECODESIGN 0 //Linux does not have strict codesigning
#endif

#if DEBUG
	#define privateFunc
	#if __APPLE__
		#define debugPrint(...) os_log_error(OS_LOG_DEFAULT, __VA_ARGS__)
	#else
		#define debugPrint(...) fprintf(stderr, __VA_ARGS__)
	#endif
#else
	#define privateFunc static
	#define debugPrint(...)
#endif

#define LIBHOOKER_EXPORT __attribute__ ((visibility ("default")))

#define signPtr(x) x != NULL ? ptrauth_sign_unauthenticated(x, ptrauth_key_asia, 0) : NULL

#define guardRet(x) if (x){return;}
#define guardRetPanic(x, ...) if (x){libhooker_panic(__VA_ARGS__);return;}
#define guardRetDbgPanic(x, ...) if (x){libhooker_dbgpanic(__VA_ARGS__);return;}

#define guardRetFalse(x) if (x){return false;}
#define guardRetFalsePanic(x, ...) if (x){libhooker_panic(__VA_ARGS__);return false;}
#define guardRetFalseDbgPanic(x, ...) if (x){libhooker_dbgpanic(__VA_ARGS__);return false;}

#define guardRetNull(x) if (x){return NULL;}
#define guardRetNullPanic(x, ...) if (x){libhooker_panic(__VA_ARGS__);return NULL;}
#define guardRetNullDbgPanic(x, ...) if (x){libhooker_dbgpanic(__VA_ARGS__);return NULL;}
#endif /* funcMacros_h */
