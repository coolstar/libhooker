//
//  objc.c
//  libblackjack
//
//  Created by CoolStar on 2/24/20.
//  Copyright © 2020 CoolStar. All rights reserved.
//

#include "objc.h"
#include "libhooker_panic.h"
#include "libhooker.h"
#include "funcMacros.h"
#include <objc/runtime.h>
#include <pthread.h>
#include <mach/mach.h>
#include <sys/mman.h>
#include <sys/queue.h>
#include <errno.h>
#include <ptrauth.h>
#include "../../libhooker/as-aarch64/as-aarch64.h"

#define _PAGE_SHIFT 14
#define _PAGE_SIZE (1 << _PAGE_SHIFT)
#define TRAMPOLINE_SIZE 0x40
#define TRAMPOLINES_PER_PAGE (_PAGE_SIZE / TRAMPOLINE_SIZE)

#ifdef __LP64__
#define TRAMPOLINE_PARAMETERS_SIZE 24
#else
#define TRAMPOLINE_PARAMETERS_SIZE 12
#endif

struct tramp_params {
#if __LP64__
    uint64_t func;
    uint64_t arg1;
    uint64_t arg2;
#else
    uint32_t func;
    uint32_t arg1;
    uint32_t arg2;
#endif
};

#if PRESERVE_CODESIGN
extern char objc_tramp[];
#else
void *objc_tramp = NULL;
#endif

static void *get_tramp_page(){
    #if !PRESERVE_CODESIGN
    if (!objc_tramp){
        void *asm_instrs = malloc(_PAGE_SIZE);
        uint32_t *tramp_ptr = asm_instrs;
        #define PARAMSOFF _PAGE_SIZE - (i * TRAMPOLINE_SIZE) + (i * TRAMPOLINE_PARAMETERS_SIZE)
        for (int i = 0; i < TRAMPOLINES_PER_PAGE; i++){
            tramp_ptr[0] = assemble_regp(1, 0, -0x10, 30, 8);
            tramp_ptr[1] = assemble_regp(1, 0, -0x10, 6, 7);
            tramp_ptr[2] = assemble_regp(1, 0, -0x10, 4, 5);
            tramp_ptr[3] = assemble_regp(1, 0, -0x10, 2, 3);
            tramp_ptr[4] = assemble_regp(1, 0, -0x10, 0, 1);
            
#if __LP64__
            //x0 = arg1
            uint32_t offset = PARAMSOFF - (5 * 4) + 0x8;
            tramp_ptr[5] = assemble_ldri(1, offset, 0);
            //x1 = arg2
            tramp_ptr[6] = assemble_ldri(1, PARAMSOFF - (6 * 4) + 0x10, 1);
            //x2 = func
            tramp_ptr[7] = assemble_ldri(1, PARAMSOFF - (7 * 4), 2);
#else
            //x0 = arg1
            tramp_ptr[5] = assemble_ldri(1, PARAMSOFF + 0x4, 0);
            //x1 = arg2
            tramp_ptr[6] = assemble_ldri(1, PARAMSOFF + 0x8, 1);
            //x2 = func
            tramp_ptr[7] = assemble_ldri(1, PARAMSOFF, 2);
#endif
            
#if __arm64e__
            tramp_ptr[8] = assemble_br(BLR_OP, 2, 0, PAC_AAZ);
#else
            tramp_ptr[8] = assemble_br(BLR_OP, 2, 0, PAC_NONE);
#endif
            tramp_ptr[9] = 0xaa0003e9; //mov x9, x0
                
            tramp_ptr[10] = assemble_regp(1, 1, 0x10, 0, 1);
            tramp_ptr[11] = assemble_regp(1, 1, 0x10, 2, 3);
            tramp_ptr[12] = assemble_regp(1, 1, 0x10, 4, 5);
            tramp_ptr[13] = assemble_regp(1, 1, 0x10, 6, 7);
            tramp_ptr[14] = assemble_regp(1, 1, 0x10, 30, 8);
            
#if __arm64e__
            tramp_ptr[15] = assemble_br(BR_OP, 9, 0, PAC_AAZ);
#else
            tramp_ptr[15] = assemble_br(BR_OP, 9, 0, PAC_NONE);
#endif
            tramp_ptr += 16;
        }
        guardRetFalsePanic(!LHExecMemory(&objc_tramp, asm_instrs, _PAGE_SIZE), "Unable to assemble trampoline page");
        free(asm_instrs);
    }
    #endif
    
    mach_port_t our_task = mach_task_self();

    void *new_pages = mmap(NULL, _PAGE_SIZE * 2, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, 0, 0);
    vm_address_t tramp_page = (vm_address_t)new_pages;
    vm_prot_t cur_protection, max_protection;
    guardRetFalseDbgPanic(vm_remap(
            our_task, &tramp_page, _PAGE_SIZE, _PAGE_SIZE - 1,
            VM_FLAGS_OVERWRITE | VM_FLAGS_FIXED, our_task, (vm_address_t)objc_tramp,
            FALSE, &cur_protection, &max_protection, VM_INHERIT_NONE), "unable to remap memory");
    return (void *)tramp_page;
}

static void *make_trampoline(void *func, void *arg1, void *arg2){
    if (PAGE_SIZE > _PAGE_SIZE){
#if __LP64__
        libhooker_panic("Weird PAGE_SIZE %lx\n", PAGE_SIZE);
#else
        libhooker_panic("Weird PAGE_SIZE %x\n", PAGE_SIZE);
#endif
    }

    static pthread_mutex_t tramp_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&tramp_lock);
    
    static void *cur_tramp_page = NULL;
    static int cur_tramp_cnt = 0;

    if (cur_tramp_cnt >= TRAMPOLINES_PER_PAGE || cur_tramp_page == NULL){
        cur_tramp_page = get_tramp_page();
        cur_tramp_cnt = 0;
    }

    void *tramp_page = cur_tramp_page;
    void *tramp = (void *)((uintptr_t)tramp_page + (uintptr_t)(TRAMPOLINE_SIZE * cur_tramp_cnt));

    void *params_page = (void *)((uintptr_t)tramp_page + _PAGE_SIZE);
    struct tramp_params *tramp_params_page = params_page;
    struct tramp_params *params = &tramp_params_page[cur_tramp_cnt];
    
    params->func = (uintptr_t)func;
    params->arg1 = (uintptr_t)arg1;
    params->arg2 = (uintptr_t)arg2;
    cur_tramp_cnt++;

    pthread_mutex_unlock(&tramp_lock);
    return tramp;

}

LIBHOOKER_EXPORT int LBHookMessage(Class class, SEL selector, void *replacement, void *old_ptr){
    Method method = class_getInstanceMethod(class, selector);
    if (method == NULL){
        return LIBHOOKER_ERR_SELECTOR_NOT_FOUND;
    }
    const char *typeEncoding = method_getTypeEncoding(method);
    
    IMP old = class_replaceMethod(class, selector, replacement, typeEncoding);
    if (old){
        if (old_ptr){
            *(IMP *)old_ptr = old;
        }
    } else {
        if (old_ptr){
            Class super = class_getSuperclass(class);
            if (!super){
                libhooker_panic("no superclass but the method didn't exist!\n");
            }
            
            void *temp = make_trampoline(&class_getMethodImplementation, super, selector);
            *(void **)old_ptr = ptrauth_sign_unauthenticated(temp, ptrauth_key_asia, 0);
        }
    }
    
    return LIBHOOKER_OK;
}
