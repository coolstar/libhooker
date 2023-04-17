#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <libhooker.h>
#include <dlfcn.h>

void (*origTestFunctionForFuncHook)(void);

void testFunctionForFuncHook(){
    printf("This is a test function\n");
    printf("Ideally, we should be able to still call into this\n");
}

void replacementFunctionForHook(){
    printf("This is a replacement function\n");
    printf("Now let's try calling the original function...\n");
    origTestFunctionForFuncHook();
}

void (*origTestFunctionForFuncHook2)(void);
void replacementFunctionForHook2(){
    printf("This is second replacement function\n");
    printf("Now let's try calling the original function (should be the first replacement)...\n");
    origTestFunctionForFuncHook2();
}

void testFunctionHook(){
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

void replacementBadFunction(void *ptr, void *ptr2){
    printf("Hooked short function successfully!\n");
    origBadFunction(ptr, ptr2);
}

#if defined(__clang__)
__attribute__((naked))
int badFunction(int arg1, int arg2){
    __asm__ volatile (
        "cbz x0, #0x8\n"
        "add x0, x1, x0\n"
        "nop\n"
        "ret"
    );
}
#else
asm("\
    _badFunction:\n\
        cbz x0, #0x8\n\
        cbz x0, #0x8\n\
        add x0, x1, x0\n\
        nop\n\
        ret\
");
int badFunction(int, int) asm("_badFunction");
#endif

void testBadFunctionHook(){
    printf("Testing Bad Function hooking!\n");

    printf("Calling with test1 & test2\n");
    printf("Output: %d\n", badFunction(3, 2));
    printf("Calling with just test2\n");
    printf("Output: %d\n", badFunction(0, 2));

    struct LHFunctionHook hooks[] = {
        {&badFunction, &replacementBadFunction, &origBadFunction}
    };

    printf("\n\nHooking\n\n");
    LHHookFunctions(hooks, 1);

    printf("Calling with test1 & test2\n");
    printf("Output: %d\n", badFunction(3, 2));
    printf("Calling with just test2\n");
    printf("Output: %d\n", badFunction(0, 2));
}

unsigned int (*origSleep)(unsigned int);

unsigned int newSleep(unsigned int seconds){
    printf("Caught attempt to sleep %d seconds!\n", seconds);
    return origSleep(0);
}

void testSystemHook(){
    printf("Hooking system function!\n");
//    void *dynamicSleep = dlsym(RTLD_NEXT, "sleep");
    printf("Func: %p\n", &sleep);
    struct LHFunctionHook hooks[] = {
        {&sleep, &newSleep, &origSleep}
    };
    LHHookFunctions(hooks, 1);

    sleep(5000);
}

int main(){
	printf("Hello World!\n");
	testFunctionHook();
	testBadFunctionHook();
	testSystemHook();
	return 0;
}
