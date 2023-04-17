#include <stdio.h>
#include <sys/utsname.h>
#include "unittest.c"

int main(){
    struct utsname kern;
    uname(&kern);
    
    printf("kern: %s\n", kern.version);
    
    dlopen("/System/Library/Frameworks/UIKit.framework/UIKit", RTLD_NOW);
    
    printCSOps();
    testDyld();
    testMemoryHook();
    testFunctionHook();
    testBadFunctionHook();
    testSystemHook();
    testLHHook();
    return 0;
}
