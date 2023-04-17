#import <dlfcn.h>
#import <ptrauth.h>
#import <unistd.h>
#import <objc/runtime.h>
#import <CoreFoundation/CoreFoundation.h>

#ifdef THEOS
#import "../libhooker/dyld/dyldSyms.h"
#import "../libhooker/mem/writeMem.h"
#import "../libhooker/as-aarch64/as-aarch64.h"
#import "../libhooker/disas-aarch64/disas-aarch64.h"
#import "../libhooker/func/funcHook.h"
#import "../libhooker/include/libhooker.h"
#import "../libblackjack/include/libblackjack.h"
#else
#import "dyldSyms.h"
#import "writeMem.h"
#import "as-aarch64.h"
#import "disas-aarch64.h"
#import "funcHook.h"
#import "libhooker.h"
#import "libblackjack.h"
#endif

#define    CS_OPS_STATUS        0    /* return status */
int csops(pid_t pid, unsigned int  ops, void * useraddr, size_t usersize);

void printCSOps(void){
    uint32_t ops;
    int retval = csops(getpid(), CS_OPS_STATUS, &ops, sizeof(uint32_t));
    printf("retval: %d, ops: 0x%x\n", retval, ops);
}

void testDyld(void){
    const char *swiftNames[1] = {
        "_swift_demangle_getDemangledName"
    };
    void *swiftSyms[1];
    
    dlopen("/usr/lib/swift/libswiftDemangle.dylib", RTLD_NOW);
    
    struct libhooker_image *libswiftDemangle = LHOpenImage("/usr/lib/swift/libswiftDemangle.dylib");
    LHFindSymbols(libswiftDemangle, swiftNames, swiftSyms, 1);
    LHCloseImage(libswiftDemangle);
    
    const char *systemNames[1] = {
        "_posix_spawn"
    };
    void *systemSyms[1];
    
    struct libhooker_image *libsystem = LHOpenImage("/usr/lib/system/libsystem_kernel.dylib");
    LHFindSymbols(libsystem, systemNames, systemSyms, 1);
    LHCloseImage(libsystem);
    
    const char *coreTextNames[1] = {
        "_CTFontSetAltTextStyleSpec"
    };
    void *coreTextSyms[1];
    
    struct libhooker_image *coreText = LHOpenImage("/System/Library/Frameworks/CoreText.framework/CoreText");
    LHFindSymbols(coreText, coreTextNames, coreTextSyms, 1);
    LHCloseImage(coreText);
    
    void *uikitSyms[1];
    uikitSyms[0] = NULL;
    struct libhooker_image *uikitCore = LHOpenImage("/System/Library/PrivateFrameworks/UIKitCore.framework/UIKitCore");
    LHFindSymbols(uikitCore, coreTextNames, uikitSyms, 1);
    LHCloseImage(uikitCore);
    
    printf("libswift: 0x%llx\n", (uint64_t)swiftSyms[0]);
    printf("libsystem: 0x%llx\n", (uint64_t)systemSyms[0]);
    printf("coretext: 0x%llx\n", (uint64_t)coreTextSyms[0]);
    printf("uikit: 0x%llx\n", (uint64_t)uikitSyms[0]);
    
#if DEBUG
    printf("Header: 0x%llx\n", (uint64_t)getDyldCacheHeader());
#endif
}

__attribute__((aligned(0x4000))) //separate page
int victimFunctionForMemoryHook(void){
    printf("Hello World\n");
    return -10;
}

__attribute__((aligned(0x4000))) //separate page
void testMemoryHook(void){
    printf("Calling victim function...\n");
    int ret = victimFunctionForMemoryHook();
    printf("Returned %d before write memory\n", ret);
    
    char replacementInstrs[] = {0xa0, 0x00, 0x80, 0xd2, //mov x0, #5
        0xc0, 0x03, 0x5f, 0xd6}; //ret
    struct LHMemoryPatch patch = {
        ptrauth_strip(&victimFunctionForMemoryHook, ptrauth_key_asia), replacementInstrs, 8 * sizeof(char)
    };
    LHPatchMemory(&patch, 1);
    
    printf("Calling victim function...\n");
    ret = victimFunctionForMemoryHook();
    printf("Returned %d after write memory\n", ret);
    
    printCSOps();
    
    const char *names[1] = {"_csops"};
    void *symbols[1];
    struct libhooker_image *libsystem = LHOpenImage("/usr/lib/system/libsystem_kernel.dylib");
    LHFindSymbols(libsystem, names, symbols, 1);
    LHCloseImage(libsystem);
    
    printf("csops function: %p\n", symbols[0]);
    
    char replacementInstrs_csops[] = {0x60, 0x00, 0x80, 0xd2, //mov x0, #3
        0x01, 0xcf, 0x8a, 0x52, //movz x1, #0x5678
        0x81, 0x46, 0xa2, 0x72, //movk w1, #0x1234, lsl 16
        0x41, 0x00, 0x00, 0xb9, //str x1, [x2]
        0xc0, 0x03, 0x5f, 0xd6}; //ret
    struct LHMemoryPatch patch2 = {
        symbols[0], replacementInstrs_csops, 20 * sizeof(char)
    };
    LHPatchMemory(&patch2, 1);
    
    printCSOps();
}

__attribute__((aligned(0x4000))) //separate page
void (*origTestFunctionForFuncHook)(void);

void testFunctionForFuncHook(void){
    printf("This is a test function\n");
    printf("Ideally, we should be able to still call into this\n");
}

void replacementFunctionForHook(void){
    printf("This is a replacement function\n");
    printf("Now let's try calling the original function...\n");
    origTestFunctionForFuncHook();
}

void (*origTestFunctionForFuncHook2)(void);
void replacementFunctionForHook2(void){
    printf("This is second replacement function\n");
    printf("Now let's try calling the original function (should be the first replacement)...\n");
    origTestFunctionForFuncHook2();
}

__attribute__((aligned(0x4000))) //separate page
void testFunctionHook(void){
    printf("Testing Function hooking!\n");
    
    testFunctionForFuncHook();
    struct LHFunctionHook hooks[] = {
        {&testFunctionForFuncHook, &replacementFunctionForHook, &origTestFunctionForFuncHook}
    };
    LHHookFunctions(hooks, 1);
    testFunctionForFuncHook();
    
    struct LHFunctionHook hooks2[] = {
        {&testFunctionForFuncHook, &replacementFunctionForHook2, &origTestFunctionForFuncHook2}
    };
    LHHookFunctions(hooks2, 1);
    testFunctionForFuncHook();
}

void (*origBadFunction)(void *ptr, void *ptr2);

__attribute__((aligned(0x4000))) //separate page
void replacementBadFunction(void *ptr, void *ptr2){
    printf("Hooked short function successfully!\n");
    origBadFunction(ptr, ptr2);
}

__attribute__((naked))
int badFunction(int arg1, int arg2){
    __asm__ volatile (
        "cbz x0, #0x8\n"
        "add x0, x1, x0\n"
        "nop\n"
        "ret"
    );
}

__attribute__((aligned(0x4000))) //separate page
void testBadFunctionHook(void){
    printf("Testing Bad Function hooking!\n");
    struct LHFunctionHook hooks[] = {
        {&badFunction, &replacementBadFunction, &origBadFunction}
    };
    
    printf("Calling with test1 & test2\n");
    printf("Output: %d\n", badFunction(3, 2));
    printf("Calling with just test2\n");
    printf("Output: %d\n", badFunction(0, 2));
    
    printf("\n\nHooking\n\n");
    LHHookFunctions(hooks, 1);
    
    printf("Calling with test1 & test2\n");
    printf("Output: %d\n", badFunction(3, 2));
    printf("Calling with just test2\n");
    printf("Output: %d\n", badFunction(0, 2));
}

size_t (*origUIAppInitialize)(void);
size_t newUIAppInitialize(void){
    printf("Hooked UIAppInitialize!\n");
    return origUIAppInitialize();
}

CFIndex (*oldCFPreferencesGetAppIntegerValue)(CFStringRef, CFStringRef, Boolean *);
CFIndex newCFPreferencesGetAppIntegerValue(CFStringRef key, CFStringRef applicationID, Boolean *keyExistsAndHasValidFormat){
    printf("Hooked CFPreferencesGetAppIntegerValue\n");
    return oldCFPreferencesGetAppIntegerValue(key, applicationID, keyExistsAndHasValidFormat);
}

static void testSystemHook(void){
    Boolean test;
    CFPreferencesGetAppIntegerValue(CFSTR("canvas_width"), CFSTR("com.apple.iokit.IOMobileGraphicsFamily"), &test);
    
    const char *names[1] = {"_UIApplicationInitialize"};
    void *symbols[1];
    struct libhooker_image *uikitcore = LHOpenImage("/System/Library/PrivateFrameworks/UIKitCore.framework/UIKitCore");
    LHFindSymbols(uikitcore, names, symbols, 1);
    LHCloseImage(uikitcore);
    struct LHFunctionHook hooks[] = {
        {symbols[0], &newUIAppInitialize, &origUIAppInitialize},
        {CFPreferencesGetAppIntegerValue, &newCFPreferencesGetAppIntegerValue, &oldCFPreferencesGetAppIntegerValue}
    };
    LHHookFunctions(hooks, 2);
    
    CFPreferencesGetAppIntegerValue(CFSTR("canvas_width"), CFSTR("com.apple.iokit.IOMobileGraphicsFamily"), &test);
}

__attribute__((aligned(0x4000))) //separate page
const char *(*origLHStrError)(int);
const char *newLHStrError(int err){
    if (err == -30){
        return "Hooked ourselves successfully!";
    }
    return origLHStrError(err);
}

__attribute__((aligned(0x4000))) //separate page
static void testLHHook(void){
    printf("Initial Error: %s\n", LHStrError(-30));
    struct LHFunctionHook hooks[] = {
        {&LHStrError, &newLHStrError, &origLHStrError}
    };
    LHHookFunctions(hooks, 1);
    
    printf("New Error: %s\n", LHStrError(-30));
}
