//
//  funcHook.c
//  libhooker
//
//  Created by CoolStar on 2/24/20.
//  Copyright © 2020 CoolStar. All rights reserved.
//

#include "funcHook.h"
#include "../as-aarch64/as-aarch64.h"
#include "../disas-aarch64/disas-aarch64.h"
#include "funcMacros.h"
#include "../mem/writeMem.h"
#include "../mem/shadowMem.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <ptrauth.h>

#if DEBUG
#define PRINT_TRAMP 0
#else
#define PRINT_TRAMP 0
#endif

int LHHookFunction(void *symbol, void *hook, void **old, uint8_t jmp_reg){
    if (!symbol){
        return LIBHOOKER_ERR_NO_SYMBOL;
    }
    
    struct disasm_reg_t *disasm_reg;
    int disasm_entries;
    LHGetDisasmRegistry(&disasm_reg, &disasm_entries);
    
    uint8_t disasmSize = 3;
    uint8_t trampSize = 3;
#define disasmEnd (disasmSize - 1)
#define finalSize (disasmSize + 1)
    
    struct disasm_reg_t ret_entry = disasm_reg[0]; //first is always the ret instruction
    uint32_t *start_op = (uint32_t *)ptrauth_strip(symbol, ptrauth_key_asia);
    uint32_t *shadow_start_op = (uint32_t *)LHReadShadowMem((void *)start_op, disasmSize * sizeof(uint32_t));
    uint32_t *end_op = start_op + disasmEnd;
    if (!old){
        disasmSize -= 1;
        end_op = start_op + disasmEnd;
    }
    
    uint32_t **final_ops = malloc(finalSize * sizeof(uint32_t *));
    int *final_sizes = malloc(finalSize * sizeof(int));
    uint64_t *pcs = malloc(disasmSize * sizeof(uint64_t));
    rehandle_opcode *rewrite_handles = malloc(disasmSize * sizeof(rehandle_opcode));
    uint64_t *target_adrs = malloc(disasmSize * sizeof(uint64_t));
    
    bzero(final_ops, finalSize * sizeof(uint32_t *));
    bzero(final_sizes, finalSize * sizeof(int));
    bzero(pcs, disasmSize * sizeof(uint64_t));
    bzero(rewrite_handles, disasmSize * sizeof(void *));
    bzero(target_adrs, disasmSize * sizeof(uint64_t));
    
    bool needsRehandle = false;
    bool exitsAtTrampEnd = false;
    
    int i = 0;
    for (uint32_t *op = end_op; op >= start_op; op--){
        uint32_t instr = *((op - start_op) + shadow_start_op);
        debugPrint("pc: %p, op: 0x%x\n", (void *)op, instr);
        if (ret_entry.is_op(instr) || is_bExit(instr)){
            if (op != end_op){
                libhooker_dbgpanic("unexpected ret instruction! function too short?\n");
                
                free(shadow_start_op);
                free(final_ops);
                free(final_sizes);
                free(pcs);
                free(rewrite_handles);
                free(target_adrs);
                return LIBHOOKER_ERR_SHORT_FUNC;
            } else {
                exitsAtTrampEnd = true;
            }
        }
        
        if (old){
            for (uint32_t j = 0; j < disasm_entries; j++){
                if (disasm_reg[j].is_op(instr)){
                    void *resulting_ops = disasm_reg[j].handle_op((uint64_t)op, instr, jmp_reg, &target_adrs[disasmEnd - i]);
                    final_ops[disasmEnd - i] = resulting_ops;
                    final_sizes[disasmEnd - i] = disasm_reg[j].tramp_size;
                    pcs[disasmEnd - i] = (uint64_t)op;
                    rewrite_handles[disasmEnd - i] = disasm_reg[j].rehandle_op;
                    if (target_adrs[disasmEnd - i] >= (uint64_t)start_op && target_adrs[disasmEnd - i] <= (uint64_t)end_op){
                        needsRehandle = true;
                        debugPrint("Needs to rehandle instructions!\n");
                    }
                    break;
                }
            }
        }
        i++;
    }
    free(shadow_start_op);
    
    if (old){
        uint64_t targetAddr = (uint64_t)(end_op + 1);
        debugPrint("Target address: %p\n", (void *)targetAddr);
        if (exitsAtTrampEnd){
            uint32_t *nop_ptr = malloc(sizeof(uint32_t));;
            *nop_ptr = assemble_nop();
            final_ops[disasmSize] = nop_ptr;
            final_sizes[disasmSize] = 0;
        } else {
            final_ops[disasmSize] = assemble_jmp(targetAddr, false, jmp_reg);
            final_sizes[disasmSize] = JMPSIZ * sizeof(uint32_t);
        }
        
        int final_size = 0;
        for (int i = 0; i < finalSize; i++){
            final_size += final_sizes[i];
        }
        debugPrint("Tramp size: %ld\n", final_size / sizeof(uint32_t));
        uint32_t *tramp = malloc(final_size);
        
        uint32_t *trampptr = tramp;
        for (int i = 0; i < finalSize; i++){
            memcpy(trampptr, final_ops[i], final_sizes[i]);
            trampptr += (final_sizes[i] / sizeof(uint32_t));
        }
        
#if PRINT_TRAMP
        for (int i = 0; i < final_size/sizeof(uint32_t); i++){
            debugPrint("Tramp: 0x%x\n", tramp[i]);
        }
#endif
        
        int execSuccess = LHExecMemory(old, tramp, final_size);
        if (needsRehandle){
            LHMarkMemoryWriteable(*old);
            for (int i = 0; i < disasmSize; i++){
                if (target_adrs[i] > (uint64_t)start_op && target_adrs[i] <= (uint64_t)end_op){
                    debugPrint("Needs rewrite: PC: %p, target: %p\n", (void *)pcs[i], (void *)target_adrs[i]);
                    if (!rewrite_handles[i]){
                        libhooker_dbgpanic("Unable to find rewrite handler for opcode!\n");
                    }
                    
                    uint64_t offset = target_adrs[i] - pcs[i];
                    uint64_t pcOff = pcs[i] - (uint64_t)start_op;
                    for (int j = 0; j <= i; j++){
                        offset -= sizeof(uint32_t);
                        offset += final_sizes[j];
                    }
                    uint64_t newTarget = (uint64_t)*old + pcOff + offset;
                    debugPrint("New target: %p\n", (void *)newTarget);
                    
                    uint32_t opcode = *((uint32_t *)pcs[i]);
                    
                    uint32_t *new_ops = rewrite_handles[i](opcode, jmp_reg, newTarget);
                    uint64_t newPC = (uint64_t)*old + pcOff;
                    memcpy((void *)newPC, new_ops, final_sizes[i]);
                    free(new_ops);
                }
            }
            LHMarkMemoryExecutable(*old);
        }
        *old = ptrauth_sign_unauthenticated(*old, 0, ptrauth_key_asia);
        debugPrint("Writing old ptr: %p\n", (void *)*old);
        free(tramp);
        
        for (int i = 0; i < finalSize; i++){
            free(final_ops[i]);
        }
        free(final_ops);
        free(final_sizes);
        free(pcs);
        free(rewrite_handles);
        free(target_adrs);
        
        if (!execSuccess)
            return LIBHOOKER_ERR_VM;
    } else {
        for (int i = 0; i < finalSize; i++){
            free(final_ops[i]);
        }
        free(final_ops);
        free(final_sizes);
        free(pcs);
        free(rewrite_handles);
        free(target_adrs);
    }
    
    uint64_t target_op = (uint64_t)ptrauth_strip(hook, ptrauth_key_asia);
    uint64_t targetop_page = target_op & ~0xfff;
#if __LP64__
    uint64_t startop_page = (uint64_t)start_op & ~0xfff;
    
    bool useMMapWorkaround = false;
    uint32_t movk_inst = assemble_nop();
    if ((startop_page >> 33) != (targetop_page >> 33)){
        debugPrint("WTF??? (Startop Page: %p, TargetOp Page: %p)\n", (void *)startop_page, (void *)targetop_page);
        debugPrint("Attempting workaround...\n");
        void *temp_tramp = mmap((void *)startop_page, 5 * sizeof(uint32_t), PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, 0, 0);
        if (!temp_tramp){
            libhooker_panic("Unable to map trampoline\n");
            return LIBHOOKER_ERR_VM;
        }
        uint64_t new_op = (uint64_t)temp_tramp;
        uint64_t new_page = new_op & ~0xfff;
        debugPrint("Got intermediate trampoline: %p\n", (void *)temp_tramp);
        if ((startop_page >> 33) != ((uint64_t)temp_tramp >> 33)){
            debugPrint("AGAIN??? Wtf are you doing to me?\n");
            if ((startop_page >> 48) == ((uint64_t)temp_tramp >> 48) && new_op == new_page){
                debugPrint("OK there may be hope. Doing 2nd workaround.\n");
                uint64_t bits = (new_op >> 32) & 0xffff;
                movk_inst = assemble_mov(1, MOVK_OP, 32, jmp_reg, bits);
                
                new_op &= 0xffffffff;
                new_op |= ((startop_page >> 32) & 0xffff) << 32;
                new_page = new_op;
                debugPrint("Temp page: %p\n", (void *)new_page);
                useMMapWorkaround = true;
            } else {
                libhooker_panic("mmap failed!\n");
                return LIBHOOKER_ERR_VM;
            }
        }
        uint32_t *temp_jmp = assemble_jmp(target_op, false, jmp_reg);
        memcpy(temp_tramp, temp_jmp, 5 * sizeof(uint32_t));
        LHMarkMemoryExecutable(temp_tramp);
        free(temp_jmp);
        target_op = new_op;
        targetop_page = new_page;
    }
#endif
    
    uint32_t adrp_inst = assemble_adrp((uint64_t)start_op, jmp_reg, targetop_page);
    uint32_t add_inst = assemble_add(jmp_reg, jmp_reg, (uint32_t)(target_op - targetop_page));
    uint32_t *replacement_instrs = malloc(trampSize * sizeof(uint32_t));
    for (int i = 0; i < trampSize; i++){
        replacement_instrs[i] = assemble_nop();
    }
    replacement_instrs[0] = adrp_inst;
#if __LP64__
    if (!useMMapWorkaround)
        replacement_instrs[1] = add_inst;
    else
        replacement_instrs[1] = movk_inst;
#else
    replacement_instrs[1] = add_inst;
#endif
    replacement_instrs[2] = assemble_br(BR_OP, jmp_reg, 0, PAC_NONE);
    
    #if PRINT_TRAMP
    for (int i = 0; i < trampSize; i++){
        debugPrint("Repl Inst: 0x%x\n", replacement_instrs[i]);
    }
    #endif
    if (!LHWriteMemoryInternal(start_op, replacement_instrs, trampSize * sizeof(uint32_t))){
        free(replacement_instrs);
        return LIBHOOKER_ERR_VM;
    }
    free(replacement_instrs);
    return LIBHOOKER_OK;
}


LIBHOOKER_EXPORT int LHHookFunctions(const struct LHFunctionHook *hooks, int count){
    int successHook = 0;
    errno = 0;
    for (int i = 0; i < count; i++){
#if __APPLE__
        uint8_t jmpreg = 16;
#else
        uint8_t jmpreg = 18; //risky but ld.so uses x16 and x17
#endif
#if TARGET_OS_IOS && __LP64__
        uint8_t legacyOptions = ((uintptr_t)hooks[i].options) & 0xf;
        if (legacyOptions & LHOptionsSetJumpReg){
            struct LHFunctionHookOld *oldhook = (struct LHFunctionHookOld *)&hooks[i];
            jmpreg = oldhook->jmp_reg;
        } else
#endif
        if (hooks[i].options){
            const struct LHFunctionHookOptions *options = hooks[i].options;
            if (options->options & LHOptionsSetJumpReg){
                jmpreg = options->jmp_reg;
            }
        }
        int funcRet = LHHookFunction(hooks[i].function, hooks[i].replacement, hooks[i].oldptr, jmpreg);
        if (funcRet == LIBHOOKER_OK)
            successHook++;
        else
            errno = funcRet;
    }
    if (!LHCommitShadowPages()){
        debugPrint("Unable to commit shadow pages... Unknown how many succeeded\n");
        return 0;
    }
    return successHook;
}
