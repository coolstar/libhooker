//
//  dyld4Find.c
//  dyld4Find
//
//  Created by CoolStar on 9/10/21.
//  Copyright © 2021 CoolStar. All rights reserved.
//

#include "dyld4Find.h"
#include "funcMacros.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

struct AdrInst {
    bool isPage;
    uint8_t reg;
    uint64_t target;
};

struct LdrImmInst {
    uint8_t sz;
    uint8_t rn;
    uint8_t rt;
    int64_t offset;
};

privateFunc bool is_adr_dyld(uint32_t opcode){
    return ((opcode >> 24) & 0x1f) == 0b10000;
}

privateFunc bool is_ldrimm(int32_t opcode){
    return ((opcode >> 22) & 0xff) == 0b11100001
        || ((opcode >> 22) & 0xff) == 0b11100101;
}

privateFunc int64_t signExtend64_dyld(uint64_t immediate, uint8_t offset){
    int64_t result = immediate;
    uint64_t sign_bit = (immediate >> offset) & 0x1;
    for (int i = offset+1; i < 64; i++){
        result |= (sign_bit << i);
    }
    return result;
}

privateFunc struct AdrInst* unpackAdr(uint32_t opcode, uint64_t pc){
    if (!is_adr_dyld(opcode)){
        return NULL;
    }
    
    uint8_t reg = (opcode & 0x1f);
    bool isPage = (opcode >> 31) == 1;
    
    uint64_t immHi = (opcode >> 5) & 0x7ffff;
    uint64_t immLo = (opcode >> 29) & 0x3;
    
    uint64_t imm = (immHi << 2) | immLo;
    if (isPage){
        imm = imm << 12;
    }
    
    uint64_t offset = signExtend64_dyld(imm, isPage ? 32 : 20);
    
    uint64_t target = isPage ? (pc & ~0xfff) + offset : pc + offset;
    
    struct AdrInst* inst = malloc(sizeof(struct AdrInst));
    inst->isPage = isPage;
    inst->reg = reg;
    inst->target = target;
    return inst;
}

privateFunc struct LdrImmInst* unpackLdrImm(uint32_t opcode){
    if (!is_ldrimm(opcode)){
        return NULL;
    }
    
    bool isUnsigned = ((opcode >> 24) & 0x1) == 1;
    
    uint8_t rawSz = (opcode >> 30) & 0x3;
    uint8_t sz;
    if (rawSz == 0b00) {
        sz = 8;
    } else if (rawSz == 0b01) {
        sz = 16;
    } else if (rawSz == 0b10) {
        sz = 32;
    } else {
        sz = 64;
    }
    
    uint8_t rn = (opcode >> 5) & 0x1f;
    uint8_t rt = opcode & 0x1f;
    uint64_t imm = (opcode >> 10) & 0xfff;
    
    int64_t offset;
    if (isUnsigned) {
        imm = imm << rawSz;
        offset = *((int64_t *)&imm);
    } else {
        offset = signExtend64_dyld(imm, 9);
    }
    
    struct LdrImmInst* inst = malloc(sizeof(struct LdrImmInst));
    inst->sz = sz;
    inst->rn = rn;
    inst->rt = rt;
    inst->offset = offset;
    return inst;
}

void *LHFindDyld4QuickAndDirty(void *dlclose){
    uint32_t *rawOp = (uint32_t *)dlclose;
    for (int i = 0; i < 10; i++) {
        struct AdrInst *adr = unpackAdr(rawOp[i], (uint64_t)&rawOp[i]);
        struct LdrImmInst *ldr = unpackLdrImm(rawOp[i+1]);
        
        if (adr && ldr){
            if (adr->reg == ldr->rn){               
                uint64_t ptr = adr->target + ldr->offset;
                
                free(adr);
                adr = NULL;
                
                free(ldr);
                ldr = NULL;
                return (void *)ptr;
            }
        }
        
        if (adr){
            free(adr);
            adr = NULL;
        }
        if (ldr){
            free(ldr);
            ldr = NULL;
        }
    }
    return NULL;
}
