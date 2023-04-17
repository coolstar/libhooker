#include "funcMacros.h"
#include <dlfcn.h>
#include <stdbool.h>
#include <stddef.h>

LIBHOOKER_EXPORT void *LHOpenImage(const char *path){
	return dlopen(path, RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
}

LIBHOOKER_EXPORT void LHCloseImage(void *image){
	dlclose(image);
}

LIBHOOKER_EXPORT bool LHFindSymbols(void *image,
					const char **symbolNames,
					void **searchSyms,
					size_t searchSymCount){
	int foundCount = 0;
	for (int i = 0; i < searchSymCount; i++){
		searchSyms[i] = dlsym(image, symbolNames[i]);
		if (searchSyms[i])
			foundCount++;
	}
	return foundCount == searchSymCount;
}
