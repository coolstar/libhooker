//
//  main.m
//  libhooker-test
//
//  Created by CoolStar on 8/17/19.
//  Copyright © 2019 CoolStar. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "AppDelegate.h"
#import <sys/utsname.h>
#import "../unittest/unittest.c"

static void (*origObjcFunction)(AppDelegate *, SEL);

static void testObjcReplacement(AppDelegate *self, SEL _cmd){
    origObjcFunction(self, _cmd);
    
    NSLog(@"%@",@"Well we hooked it successfully!");
}

void testObjcHook(void){
    LBHookMessage([AppDelegate2 class], @selector(test), (IMP)&testObjcReplacement, (IMP *)&origObjcFunction);
}

/*void testReverseEngineering(){
    uint64_t lrAddr = 0;
    asm("mov %0, x30"
        : "=r"(lrAddr)
        : "0"(lrAddr));
    printf("Executable Name: %s (should be self)\n", LHAddrExecutable(lrAddr));
    
    printf("Executable Name: %s (should be libsystem)\n", LHAddrExecutable((void *)dlsym(RTLD_DEFAULT, "uname")));
}*/

extern char **environ;

int main(int argc, char * argv[]) {
    @autoreleasepool {
        struct utsname kern;
        uname(&kern);
        
        printf("kern: %s\n", kern.version);
        
        if (ptrauth_sign_unauthenticated((void *)0x12345, ptrauth_key_asda, 0) == (void *)0x12345){
            printf("Running without PAC\n");
        } else {
            printf("Running with PAC\n");
        }
        
        printCSOps();
        testDyld();
        testMemoryHook();
        testFunctionHook();
        testBadFunctionHook();
        testSystemHook();
        //testLHHook();
        testObjcHook();
        //testReverseEngineering();
        
        /*uint32_t opcode = *((uint32_t *)&CFBundleCopyResourceURL);
        free(handle_cbz((uint64_t)&CFBundleCopyResourceURL, opcode));
        free(handle_tbz((uint64_t)&CFBundleCopyResourceURL, 0x361001e0));
        free(handle_bCond((uint64_t)&CFBundleCopyResourceURL, 0x540001eb));
        free(handle_ldr((uint64_t)&CFBundleCopyResourceURL, 0x581fffea));
        free(handle_ldr((uint64_t)&CFBundleCopyResourceURL, 0x981fffea));*/
        
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate2 class]));
    }
}
