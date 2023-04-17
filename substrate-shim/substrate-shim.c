#include <objc/runtime.h>
#include "libhooker.h"
#include "libblackjack.h"
#include <os/log.h>
#include <stdlib.h>
#include <mach-o/dyld.h>

void *MSGetImageByName(const char *filename) {
    return LHOpenImage(filename);
}

void *MSFindSymbol(void *image, const char *name) {
    if (!image){
        os_log_error(OS_LOG_DEFAULT, "MSFindSymbol: NULL image specified. The fuck did you pass to me? You should be ashamed of yourself.\n");
        os_log_error(OS_LOG_DEFAULT, "MSFindSymbol: I'm going to do this to preserve compatibility with Substrate, but go fuck yourself.\n");
        os_log_error(OS_LOG_DEFAULT, "MSFindSymbol: Also btw. Stop using this Substrate compatibility shim while you fix your crap.\n");

        for (uint32_t i = 0; i < _dyld_image_count(); i++){
            const char *path = _dyld_get_image_name(i);
            void *im = LHOpenImage(path);
            if (!im)
                continue;
            void *r = MSFindSymbol(im, name);
            LHCloseImage(im);
            if (r)
                return r;
        }
        return NULL;
    }
    void *ptr = NULL;
	if (LHFindSymbols(image, &name, &ptr, 1)){
        return ptr;
    }
    return ptr;
}

void MSHookFunction(void *symbol, void *replace, void **result) {
	if (symbol != NULL){
        struct LHFunctionHook hooks[] = {
            {symbol, replace, result}
        };
        LHHookFunctions(hooks, 1);
	}
}

void MSHookMessageEx(Class _class, SEL sel, IMP imp, IMP *result) {
    LBHookMessage(_class, sel, imp, result);
}

// i don't think anyone uses this function anymore, but it's here for completeness
void MSHookClassPair(Class _class, Class hook, Class old) {
    unsigned int n_methods = 0;
    Method *hooks = class_copyMethodList(hook, &n_methods);
    
    for (unsigned int i = 0; i < n_methods; ++i) {
        SEL selector = method_getName(hooks[i]);
        const char *what = method_getTypeEncoding(hooks[i]);
        
        Method old_mptr = class_getInstanceMethod(old, selector);
        Method cls_mptr = class_getInstanceMethod(_class, selector);
        
        if (cls_mptr) {
            class_addMethod(old, selector, method_getImplementation(hooks[i]), what);
            method_exchangeImplementations(cls_mptr, old_mptr);
        } else {
            class_addMethod(_class, selector, method_getImplementation(hooks[i]), what);
        }
    }
    
    free(hooks);
}

void MSHookMemory(void *target, const void *data, size_t size){
    struct LHMemoryPatch patch[] = {
        {target, data, size}
    };
    LHPatchMemory(patch, 1);
}
