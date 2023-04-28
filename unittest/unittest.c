#import <dlfcn.h>
#import <ptrauth.h>
#import <unistd.h>
#import <objc/runtime.h>
#import <mach-o/dyld.h>
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

#define ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("%s:%d: %s: Test failed: %s\n", \
                   __FILE__, __LINE__, __func__, message); \
            return -1; \
        } \
    } while (0)

#define RUN_TEST(test) do { \
        printf("Running test: %s\n", #test); \
        if (test() == KERN_SUCCESS) printf("  PASSED\n"); \
    } while (0)

#define    CS_OPS_STATUS        0    /* return status */
int csops(pid_t pid, unsigned int  ops, void * useraddr, size_t usersize);

void printCSOps(void){
    uint32_t ops;
    int retval = csops(getpid(), CS_OPS_STATUS, &ops, sizeof(uint32_t));
    printf("retval: %d, ops: 0x%x\n", retval, ops);
}

kern_return_t test_LHOpenImage(void) {
    
    void *expectedHandle = dlopen("/usr/lib/swift/libswiftDemangle.dylib", RTLD_NOW);
    ASSERT(expectedHandle, "dlopen returned NULL");
    
    const struct mach_header *expectedHeader = NULL;
    uintptr_t expectedSlide = -1;
    for (int i = 0; i < _dyld_image_count(); i++) {
        if (strstr(_dyld_get_image_name(i), "libswiftDemangle.dylib")) {
            expectedHeader = _dyld_get_image_header(i);
            expectedSlide = _dyld_get_image_vmaddr_slide(i);
            break;
        }
    }
    ASSERT(expectedHeader, "Could not find mach_header with _dyld_get_image_header()");
    
    struct libhooker_image *lhHandle = LHOpenImage("/usr/lib/swift/libswiftDemangle.dylib");
    ASSERT(lhHandle, "LHOpenImage() returned NULL");
    
    int cond1 = lhHandle->dyldHandle == expectedHandle;
    int cond2 = lhHandle->imageHeader == expectedHeader;
    int cond3 = lhHandle->slide == expectedSlide;
    
    LHCloseImage(lhHandle);

    ASSERT(cond1, "LH's dyldHandle doesn't match dlopen's");
    ASSERT(cond2, "LH's imageHeader doesn't match _dyld_get_image_header()");
    ASSERT(cond3, "LH's slide doesn't match _dyld_get_image_vmaddr_slide");
    
    return KERN_SUCCESS;
}

kern_return_t test_LHFindSymbols__single_symbol_and_invoke(void) {
    
    const char *symbol_names[1] = {
        "_swift_demangle_getDemangledName"
    };
    void *symbol_pointers[1];
    
    struct libhooker_image *lhHandle = LHOpenImage("/usr/lib/swift/libswiftDemangle.dylib");
    ASSERT(lhHandle, "LHOpenImage() returned NULL");
    
    int success = LHFindSymbols(lhHandle, symbol_names, symbol_pointers, 1);
    
    ASSERT(success, "LHFindSymbols() failed");
    ASSERT(symbol_pointers[0], "LHFindSymbols failed to find _swift_demangle_getDemangledName");

    // Make sure returned symbols are callable
    char demangled[65];
    ((size_t (*)(const char *, char *, size_t))symbol_pointers[0])("_TMaC17find_the_treasure15YD_Secret_Class", demangled, 65);
    ASSERT(strcmp(demangled, "type metadata accessor for find_the_treasure.YD_Secret_Class") == 0, "Invoked function has unexpected return value");
    
    LHCloseImage(lhHandle);
    return KERN_SUCCESS;
}

kern_return_t test_LHFindSymbols__single_symbol(void) {
    
    const char *symbol_names[1] = {
        "_kCTUIFontTextStyleBody"
    };
    void *symbol_pointers[1];
    
    struct libhooker_image *lhHandle = LHOpenImage("/System/Library/Frameworks/CoreText.framework/CoreText");
    ASSERT(lhHandle, "LHOpenImage() returned NULL");
    
    int success = LHFindSymbols(lhHandle, symbol_names, symbol_pointers, 1);
    LHCloseImage(lhHandle);
    
    ASSERT(success, "LHFindSymbols() failed");
    ASSERT(symbol_pointers[0], "LHFindSymbols failed to find _CTFontLogSuboptimalRequest");
    
    return KERN_SUCCESS;
}

kern_return_t test_LHFindSymbols__multiple_symbols(void) {
    
    const char *symbol_names[7] = {
        "_abort",
        "__libkernel_memmove",
        "__libkernel_strlen",
        "__libkernel_bzero",
        "__libkernel_memset",
        "__libkernel_strcpy",
        "__libkernel_strlcpy"
    };
    void *symbol_pointers[7];
    
    struct libhooker_image *lhHandle = LHOpenImage("/usr/lib/system/libsystem_kernel.dylib");
    ASSERT(lhHandle, "LHOpenImage() returned NULL");
    
    int success = LHFindSymbols(lhHandle, symbol_names, symbol_pointers, 7);
    LHCloseImage(lhHandle);
    
    ASSERT(success, "LHFindSymbols() failed");
    
    for (int i = 0; i < 6; i++) {
        ASSERT(symbol_pointers[i] != NULL, "LHFindSymbols failed to find one of the provided symbol names");
    }
    
    return KERN_SUCCESS;
}

kern_return_t test_getDyldCacheHeader(void) {
    ASSERT(getDyldCacheHeader(), "getDyldCacheHeader() returned NULL");
    return KERN_SUCCESS;
}

__attribute__((aligned(0x4000)))
int _test_LHPatchMemory_single_patch_target(void) { return -10; }
kern_return_t test_LHPatchMemory_single_patch(void) {
    
    int expectedOriginalValue = -10;
    int expectedReplacementValue = 5;
    
    char replacementInstrs[] = {
        0xa0, 0x00, 0x80, 0xd2, //mov x0, #5
        0xc0, 0x03, 0x5f, 0xd6  //ret
    };
    
    ASSERT(expectedOriginalValue == _test_LHPatchMemory_single_patch_target(), "Function didn't have expected retval before memory patch");
    
    struct LHMemoryPatch patch = {
        ptrauth_strip(&_test_LHPatchMemory_single_patch_target, ptrauth_key_asia), replacementInstrs, 8 * sizeof(char)
    };
    
    int successfulPatches = LHPatchMemory(&patch, 1);

    ASSERT(successfulPatches == 1, "LHPatchMemory() failed");
    ASSERT(expectedReplacementValue == _test_LHPatchMemory_single_patch_target(), "Function didn't have expected retval after memory patch");
    
    return KERN_SUCCESS;
}

__attribute__((aligned(0x4000)))
int _test_LHPatchMemory_multiple_patches_target(void) { return 45; }
kern_return_t test_LHPatchMemory_multiple_patches(void) {
    
    int expectedOriginalValue_1 = 45;
    int expectedReplacementValue_1 = 10;
    int expectedReplacementValue_2 = 3;

    char replacementInstrs_1[] = {
        0x40, 0x01, 0x80, 0xd2, // mov x0, #10
        0xc0, 0x03, 0x5f, 0xd6  // ret
    };
    
    char replacementInstrs_2[] = {
        0x60, 0x00, 0x80, 0xd2, // mov x0, #3
        0x01, 0xcf, 0x8a, 0x52, // movz x1, #0x5678
        0x81, 0x46, 0xa2, 0x72, // movk w1, #0x1234, lsl 16
        0x41, 0x00, 0x00, 0xb9, // str x1, [x2]
        0xc0, 0x03, 0x5f, 0xd6  // ret
    };
    
    ASSERT(expectedOriginalValue_1 == _test_LHPatchMemory_multiple_patches_target(), "Function didn't have expected retval before memory patch");
    
    struct LHMemoryPatch patches[2] = {
        {ptrauth_strip(&_test_LHPatchMemory_multiple_patches_target, ptrauth_key_asia), replacementInstrs_1, 8 * sizeof(char)},
        {csops, replacementInstrs_2, 20 * sizeof(char)}
    };
    
    int successfulPatches = LHPatchMemory(patches, 2);

    ASSERT(successfulPatches == 2, "LHPatchMemory() failed");
    ASSERT(expectedReplacementValue_1 == _test_LHPatchMemory_multiple_patches_target(), "Function didn't have expected retval after memory patch");
    
    uint32_t ops;
    int replacementValue_2 = csops(getpid(), CS_OPS_STATUS, &ops, sizeof(uint32_t));
    ASSERT(replacementValue_2 == expectedReplacementValue_2, "Function didn't have expected retval after memory patch");
    ASSERT(ops == 0x12345678, "_csops ops didn't have expected value after memory patch");
    
    return KERN_SUCCESS;
}

__attribute__((aligned(0x4000)))
int (*_test_LHFunctionHook__single_function_target_orig)(void);
int _test_LHFunctionHook__single_function_target(void) { getpid(); return 1; }
int _test_LHFunctionHook__single_function_replacement(void) { getpid(); return 2; }
kern_return_t test_LHFunctionHook__single_function(void) {
    
    int expectedOriginalValue = _test_LHFunctionHook__single_function_target();
    int expectedReplacementValue = _test_LHFunctionHook__single_function_replacement();
    ASSERT(expectedOriginalValue != expectedReplacementValue, "The target and replacement test functions have the same return value");
    
    struct LHFunctionHook hooks[] = {
        {&_test_LHFunctionHook__single_function_target, &_test_LHFunctionHook__single_function_replacement, &_test_LHFunctionHook__single_function_target_orig}
    };
    int successfulHooks = LHHookFunctions(hooks, 1);
    ASSERT(successfulHooks == 1, "LHHookFunctions failed");
    ASSERT(_test_LHFunctionHook__single_function_target_orig != NULL, "Original function ptr is null");
    
    ASSERT(_test_LHFunctionHook__single_function_target() == expectedReplacementValue, "Function didn't return the expected value");
    ASSERT(_test_LHFunctionHook__single_function_target_orig() == expectedOriginalValue, "The orig fp didn't return the expected value");
    
    return KERN_SUCCESS;
}

__attribute__((aligned(0x4000)))
int (*_test_LHFunctionHook__double_hook_target1_orig)(void);
int _test_LHFunctionHook__double_hook_target(void) { getpid(); return 3; }
int _test_LHFunctionHook__double_hook_replacement1(void) { getpid(); return 4; }
int _test_LHFunctionHook__double_hook_replacement2(void) { getpid(); return 5; }
kern_return_t test_LHFunctionHook__double_hook(void) {
    
    int expectedOriginalValue = _test_LHFunctionHook__double_hook_target();
    int expectedReplacementValue1 = _test_LHFunctionHook__double_hook_replacement1();
    int expectedReplacementValue2 = _test_LHFunctionHook__double_hook_replacement2();
    ASSERT(expectedOriginalValue != expectedReplacementValue1 && expectedReplacementValue1 != expectedReplacementValue2 && expectedOriginalValue != expectedReplacementValue2, "Test functions have the same return value");
    
    struct LHFunctionHook hooks[] = {
        {&_test_LHFunctionHook__double_hook_target, &_test_LHFunctionHook__double_hook_replacement1, &_test_LHFunctionHook__double_hook_target1_orig}
    };
    
    int successfulHooks = LHHookFunctions(hooks, 1);
    ASSERT(successfulHooks == 1, "LHHookFunctions failed on first hook");
    ASSERT(_test_LHFunctionHook__double_hook_target1_orig != NULL, "Original function ptr is null after first hook");
    
    ASSERT(_test_LHFunctionHook__double_hook_target() == expectedReplacementValue1, "Function didn't return the expected value after first hook");
    ASSERT(_test_LHFunctionHook__double_hook_target1_orig() == expectedOriginalValue, "The orig fp didn't return the expected value after the first hook");
    
    struct LHFunctionHook hooks2[] = {
        {&_test_LHFunctionHook__double_hook_target, &_test_LHFunctionHook__double_hook_replacement2, &_test_LHFunctionHook__double_hook_target1_orig}
    };
    
    successfulHooks = LHHookFunctions(hooks2, 1);
    ASSERT(successfulHooks == 1, "LHHookFunctions failed on second hook");
    ASSERT(_test_LHFunctionHook__double_hook_target1_orig != NULL, "Original function ptr is null after second hook");
    
    ASSERT(_test_LHFunctionHook__double_hook_target() == expectedReplacementValue2, "Function didn't return the expected value after second hook");
    ASSERT(_test_LHFunctionHook__double_hook_target1_orig() == expectedReplacementValue1, "The orig fp didn't return the expected value after the second hook");
    
    return KERN_SUCCESS;
}

void tests(void) {

    RUN_TEST(test_LHOpenImage);
    RUN_TEST(test_LHFindSymbols__single_symbol_and_invoke);
    RUN_TEST(test_LHFindSymbols__single_symbol);
    RUN_TEST(test_LHFindSymbols__multiple_symbols);
    RUN_TEST(test_getDyldCacheHeader);

    RUN_TEST(test_LHPatchMemory_single_patch);
    RUN_TEST(test_LHPatchMemory_multiple_patches);

    RUN_TEST(test_LHFunctionHook__single_function);
    RUN_TEST(test_LHFunctionHook__double_hook);
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
__unused static void testLHHook(void){
    printf("Initial Error: %s\n", LHStrError(-30));
    struct LHFunctionHook hooks[] = {
        {&LHStrError, &newLHStrError, &origLHStrError}
    };
    LHHookFunctions(hooks, 1);
    
    printf("New Error: %s\n", LHStrError(-30));
}
