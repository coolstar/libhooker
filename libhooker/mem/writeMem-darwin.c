//
//  writeMem.c
//  libhooker
//
//  Created by CoolStar on 2/12/20.
//  Copyright © 2020 CoolStar. All rights reserved.
//

#include "funcMacros.h"
#include "writeMem.h"
#ifdef THEOS
#include "mach/mach_vm.h"
#else
#include "mach_vm.h"
#endif
#include <mach/mach.h>
#include <mach/vm_types.h>
#include <mach/vm_prot.h>
#include <sys/mman.h>
#include <stdio.h>
#include <ptrauth.h>
#include "shadowMem.h"
#include "../dyld/dyldSyms.h"

privateFunc void get_protection(task_port_t task, vm_address_t page, vm_prot_t *prot_p, vm_inherit_t *inherit_p) {
  vm_size_t size = 0;
  vm_address_t address = page;
  memory_object_name_t object;
  mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
  vm_region_basic_info_data_64_t info;
  guardRetDbgPanic(vm_region_64(
                                     task, &address, &size,
                                     VM_REGION_BASIC_INFO_64, (vm_region_info_64_t)&info,
                                     &count, &object) != KERN_SUCCESS,
                        "unable to get vm_region info!");
    *prot_p = info.protection & (VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE);
    *inherit_p = info.inheritance;
}

union remap_msg {
#pragma pack(4)
    struct {
        mach_msg_header_t Head;
        mach_msg_body_t msgh_body;
        mach_msg_port_descriptor_t src_task;
        
        NDR_record_t NDR;
        mach_vm_address_t target_address;
        mach_vm_size_t size;
        mach_vm_offset_t mask;
        int flags;
        mach_vm_address_t src_address;
        boolean_t copy;
        vm_inherit_t inherit;
    } Request;
    struct {
        mach_msg_header_t Head;
        NDR_record_t NDR;
        
        mach_vm_address_t target_address;
        vm_prot_t cur_protection;
        vm_prot_t max_protection;
        mach_msg_trailer_t trailer;
    } Reply;
#pragma pack()
};

#define USE_TRAMPOLINE 1 //Enable to support libhooker hooking itself

privateFunc kern_return_t LHmach_vm_protect(vm_map_t target_task,
                                            mach_vm_address_t address,
                                            mach_vm_size_t size,
                                            boolean_t set_maximum,
                                            vm_prot_t new_protection);
privateFunc kern_return_t LHCommitRemapMachMem(vm_address_t *target_address,
                                               vm_prot_t *cur_protection,
                                               vm_prot_t *max_protection,
                                               union remap_msg *msg);

#if USE_TRAMPOLINE
privateFunc void *LHTrampolineFunc(void *func, void *arg1, void *arg2, void *arg3, void *arg4, void *arg5, void *arg6){
    func = ptrauth_auth_data(func, ptrauth_key_asia, 0);
    
    vm_address_t page_base = (vm_address_t)(func) & ~PAGE_MASK;
    vm_offset_t off = (vm_offset_t)(func) - page_base;
    
    mach_port_t task = mach_task_self();
    // vm_prots
    vm_prot_t cur_protection;
    vm_prot_t max_protection;
    vm_address_t dest_page = 0;
    guardRetNullDbgPanic(vm_remap(task, &dest_page, PAGE_SIZE, 0,
                                   VM_FLAGS_ANYWHERE, task, page_base,
                                   FALSE, &cur_protection, &max_protection,
                                   VM_INHERIT_NONE), "unable to remap page");
    guardRetNullDbgPanic(vm_protect(task, dest_page, PAGE_SIZE, 0, VM_PROT_READ | VM_PROT_EXECUTE), "unable to set protections");
    
    void *newFunc = (void *)(dest_page + off);
    void *(*newFuncPtr)(void *, void *, void *, void *, void *, void *) = ptrauth_sign_unauthenticated(newFunc, ptrauth_key_asia, 0);
    
    newFuncPtr(arg1, arg2, arg3, arg4, arg5, arg6);
    
    guardRetNullDbgPanic(vm_deallocate(task, dest_page, PAGE_SIZE), "unable to unmap page");
    
    return false;
}

struct LHFinalizeParameters {
    mach_port_t task;
    vm_address_t target;
    size_t size;
    
    vm_prot_t cur_protection;
    vm_prot_t max_protection;
    union remap_msg *msg;
    
    kern_return_t vm_protect_ret;
    kern_return_t vm_remap_ret;
};

__attribute__((aligned(0x4000))) //If we're using trampoline, align this to a page. Ensure LHmach_msg, LHmach_vm_protect, and LHCommitRemapMachMem are immediately after
privateFunc void *LHFinalizeMemoryInternal(struct LHFinalizeParameters *finalizeParameters){
    kern_return_t vm_protect_ret = LHmach_vm_protect(finalizeParameters->task,
                                         finalizeParameters->target,
                                         finalizeParameters->size,
                                         0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY); // Workaround for iOS 14.3+
    finalizeParameters->vm_protect_ret = vm_protect_ret;
    if (vm_protect_ret){
        return NULL;
    }
    
    
    kern_return_t ret = LHCommitRemapMachMem(&finalizeParameters->target,
                                             &finalizeParameters->cur_protection, &finalizeParameters->max_protection,
                                             finalizeParameters->msg);
    finalizeParameters->vm_remap_ret = ret;
    return NULL;
}
#endif

__attribute__((naked))
privateFunc kern_return_t LHmach_msg(mach_msg_header_t *msg,
                                     mach_msg_option_t option,
                                     mach_msg_size_t send_size,
                                     mach_msg_size_t rcv_size,
                                     mach_port_name_t rcv_name,
                                     mach_msg_timeout_t timeout,
                                     mach_port_name_t notify){
    __asm__ volatile (
                      "movn x16, #30\n"
                      "svc #0x80\n"
                      "ret\n"
                      );
}


__attribute__((naked))
privateFunc kern_return_t LHmach_vm_protect(vm_map_t target_task,
                                            mach_vm_address_t address,
                                            mach_vm_size_t size,
                                            boolean_t set_maximum,
                                            vm_prot_t new_protection){
    __asm__ volatile (
                      "movn x16, #0xd\n"
                      "svc #0x80\n"
                      "ret\n"
                      );
}

privateFunc kern_return_t LHCommitRemapMachMem(vm_address_t *target_address,
                           vm_prot_t *cur_protection,
                           vm_prot_t *max_protection,
                           union remap_msg *msg){
    mach_msg_return_t ret = LHmach_msg(&msg->Request.Head,
                                     MACH_SEND_MSG | MACH_RCV_MSG,
                                     (mach_msg_size_t)sizeof(union remap_msg),
                                     (mach_msg_size_t)sizeof(union remap_msg),
                                     msg->Request.Head.msgh_local_port,
                                     MACH_MSG_TIMEOUT_NONE,
                                     MACH_PORT_NULL);
    if (ret != MACH_MSG_SUCCESS){
        return ret;
    }
    
    *target_address = (vm_address_t)msg->Reply.target_address;
    *cur_protection = msg->Reply.cur_protection;
    *max_protection = msg->Reply.max_protection;
    return KERN_SUCCESS;
}

privateFunc void LHPrepareRemapMachMem(vm_map_t task,
                                           vm_address_t *target_address,
                                           vm_size_t size,
                                           vm_address_t mask,
                                           int flags,
                                           vm_map_t src_task,
                                           vm_address_t src_address,
                                           boolean_t copy,
                                           vm_inherit_t inheritance,
                                           mach_port_t reply_port,
                                           union remap_msg *msg){
    msg->Request.msgh_body.msgh_descriptor_count = 1;
    msg->Request.src_task.name = src_task;
    msg->Request.src_task.disposition = MACH_MSG_TYPE_COPY_SEND;
    msg->Request.src_task.type = MACH_MSG_PORT_DESCRIPTOR;
    
    msg->Request.NDR = NDR_record;
    
    msg->Request.target_address = *target_address;
    msg->Request.size = size;
    msg->Request.mask = mask;
    msg->Request.flags = flags;
    msg->Request.src_address = src_address;
    msg->Request.copy = copy;
    msg->Request.inherit = inheritance;
    
    msg->Request.Head.msgh_bits = MACH_MSGH_BITS_COMPLEX | MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE);
    msg->Request.Head.msgh_remote_port = task;
    msg->Request.Head.msgh_local_port = reply_port;
    msg->Request.Head.msgh_id = 4813;
}

bool LHMarkMemoryWriteable(void *data){
    mach_port_t our_task = mach_task_self();
    
    vm_address_t page_base = (vm_address_t)(data) & ~PAGE_MASK;
    guardRetFalseDbgPanic(vm_protect(our_task, page_base, PAGE_SIZE, FALSE, VM_PROT_READ | VM_PROT_WRITE), "unable to mark as writeable");
    return true;
}

bool LHMarkMemoryExecutable(void *data){
    mach_port_t our_task = mach_task_self();
    
    vm_address_t page_base = (vm_address_t)(data) & ~PAGE_MASK;
    guardRetFalseDbgPanic(vm_protect(our_task, page_base, PAGE_SIZE, FALSE, VM_PROT_READ | VM_PROT_EXECUTE), "unable to mark as executable");
    return true;
}

LIBHOOKER_EXPORT bool LHExecMemory(void **page, void *data, size_t size){
#if LIBHOOKER_PRESERVECODESIGN
    libhooker_log("LHExecMemory failed! libhooker is running in secure mode.\n");
    return false;
#else
    void *newPage = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, 0, 0);
    *page = newPage;
    guardRetFalse(*page == NULL);
    memcpy(*page, data, size);
    
    bool ret = true;
    size_t sz = size;
    while (sz > PAGE_SIZE) {
        sz -= PAGE_SIZE;
        newPage += PAGE_SIZE;
        ret &= LHMarkMemoryExecutable(newPage);
    }
    ret &= LHMarkMemoryExecutable(newPage);
    
    return ret;
#endif
}

bool LHCommitMemory(uintptr_t page, const void *data, size_t size){
#if LIBHOOKER_PRESERVECODESIGN
    libhooker_log("LHCommitMemory failed! libhooker is running in secure mode.\n");
    return false;
#else
    vm_address_t page_base = (vm_address_t)(page) & ~PAGE_MASK;
    guardRetFalseDbgPanic(page_base != page, "Page is not aligned!");
    guardRetFalseDbgPanic(size > PAGE_SIZE, "Size is larger than page size!");

    mach_port_t task = mach_task_self();
    // vm_prots
    vm_address_t dest_page = 0;
    vm_prot_t protections;
    vm_inherit_t inheritances;
    get_protection(task, page_base, &protections, &inheritances);
    dest_page = (vm_address_t)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, 0, 0);
    guardRetFalseDbgPanic(dest_page == 0, "Unable to map new page");
    // Perform patches
    memcpy((void *)dest_page, data, size);
    // Set new page back to RX
    guardRetFalseDbgPanic(vm_protect(task, dest_page, PAGE_SIZE,
                            FALSE, VM_PROT_READ | VM_PROT_EXECUTE), "Unable to set protections");
    
    mach_port_t reply_port = mig_get_reply_port(); //get the port here before we trample library functions
    
    union remap_msg *msg = malloc(sizeof(union remap_msg));
    bzero(msg, sizeof(union remap_msg)); //allocate memory before we trample library function
    LHPrepareRemapMachMem(task, &page_base, size,
                          0, VM_FLAGS_OVERWRITE | VM_FLAGS_FIXED,
                          task, dest_page,
                          FALSE,
                          inheritances,
                          reply_port,
                          msg); //prepare before we trample library functions
    
#if !USE_TRAMPOLINE
    //Workaround for iOS 14.3+
    guardRetFalseDbgPanic(LHmach_vm_protect(task, page_base, size, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY));
    
    vm_prot_t cur_protection, max_protection;
    guardRetFalseDbgPanic(LHCommitRemapMachMem(&page_base, &cur_protection, &max_protection, msg), "unable to remap target memory");
#else
    struct LHFinalizeParameters *finalizeParams = malloc(sizeof(struct LHFinalizeParameters));
    bzero(finalizeParams, sizeof(struct LHFinalizeParameters));
    
    finalizeParams->task = task;
    finalizeParams->target = page_base;
    finalizeParams->size = size;
    
    finalizeParams->msg = msg;
    
    finalizeParams->vm_protect_ret = 0xff;
    finalizeParams->vm_remap_ret = 0xff;
    
    //Start Danger Zone
    LHTrampolineFunc(&LHFinalizeMemoryInternal, finalizeParams, 0, 0, 0, 0, 0);
    
    guardRetFalseDbgPanic(finalizeParams->vm_protect_ret, "unable to set memory protections")
    guardRetFalseDbgPanic(finalizeParams->vm_remap_ret, "Unable to remap page");
    
    free(finalizeParams);
#endif
    
    //End Danger Zone
    
    guardRetFalseDbgPanic(munmap((void *)dest_page, size), "unable to unmap temporary page");
    free(msg);
    return true;
#endif
}
