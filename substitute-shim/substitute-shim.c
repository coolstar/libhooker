#include "libhooker_panic.h"
#include "libhooker.h"
#include <objc/runtime.h>
#include "libblackjack.h"

const char *substitute_strerror(int err){
	return LHStrError(err);
}

int substitute_hook_functions(const struct LHFunctionHook *hooks, size_t nhooks, void *hook_records, int options){
	LHHookFunctions(hooks, nhooks);
	return LIBHOOKER_OK;
}

void *substitute_open_image(char *filename){
	return LHOpenImage(filename);
}

void substitute_close_image(void *handle){
	LHCloseImage(handle);
}

int substitute_find_private_syms(void *handle, const char **names, void **syms, size_t nsyms){
    if (LHFindSymbols(handle, names, syms, nsyms)){
        return nsyms;
    }
    return 0;
}

int substitute_interpose_imports(void *handle, void *hooks, size_t hooks_cnt, void *hook_records, int options){
	libhooker_panic("This library does not implement interposing. Use fishhook for this.");
}

int substitute_hook_objc_message(Class class, SEL selector, void *replacement, void *old_ptr, bool *created_imp_ptr){
	return LBHookMessage(class, selector, replacement, old_ptr);
}

void substitute_free_created_imp(IMP imp){
	//This can be NOP'd without any issues
}
