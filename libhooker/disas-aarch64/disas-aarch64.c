//
//  disas-aarch64.c
//  libhooker
//
//  Created by CoolStar on 2/21/20.
//  Copyright © 2020 CoolStar. All rights reserved.
//

#include "funcMacros.h"
#include "disas-aarch64.h"
#include "../as-aarch64/as-aarch64.h"
#include <string.h>

privateFunc int16_t signExtend16(uint16_t immediate, uint8_t offset){
    int16_t result = immediate;
    uint16_t sign_bit = (immediate >> offset) & 0x1;
    for (int i = offset+1; i < 16; i++){
        result |= (sign_bit << i);
    }
    return result;
}


privateFunc int32_t signExtend(uint32_t immediate, uint8_t offset){
    int32_t result = immediate;
    uint32_t sign_bit = (immediate >> offset) & 0x1;
    for (int i = offset+1; i < 32; i++){
        result |= (sign_bit << i);
    }
    return result;
}

privateFunc int64_t signExtend64(uint64_t immediate, uint8_t offset){
    int64_t result = immediate;
    uint64_t sign_bit = (immediate >> offset) & 0x1;
    for (int i = offset+1; i < 64; i++){
        result |= (sign_bit << i);
    }
    return result;
}

privateFunc bool is_cbz(uint32_t opcode){
    return ((opcode >> 25) & 0x3f) == 0b011010;
}

// cbz / cbnz
privateFunc uint32_t *rehandle_cbz(uint32_t opcode, uint8_t jmp_reg, uint64_t targetAddr){
    uint8_t sf = (opcode >> 31) & 0x1; //xN or wN register (1 = 64 bit, 0 = 32 bit)
    uint8_t op = (opcode >> 24) & 0x1; //0 = cbz; 1 = cbnz
    uint8_t reg = (opcode & 0x1f); //0 -> 30
    
    uint32_t *opcodes = malloc((JMPSIZ + 1) * sizeof(uint32_t));
    opcodes[0] = assemble_cbz(sf, (~op) & 0x1, reg, 0x18);
    uint32_t *jmp = assemble_jmp(targetAddr, false, jmp_reg);
    memcpy(&opcodes[1], jmp, JMPSIZ * sizeof(uint32_t));
    free(jmp);
    return opcodes;
}

privateFunc uint32_t *handle_cbz(uint64_t pc, uint32_t opcode, uint8_t jmp_reg, uint64_t *target_adr){
    dasmDbgPrint("[disasm] pc: 0x%llx, opcode: 0x%x (cbz/cbnz)\n", pc, opcode);
    
    uint8_t sf = (opcode >> 31) & 0x1; //xN or wN register (1 = 64 bit, 0 = 32 bit)
    uint8_t op = (opcode >> 24) & 0x1; //0 = cbz; 1 = cbnz
    uint8_t reg = (opcode & 0x1f); //0 -> 30
    
    uint32_t imm19 = (opcode >> 5) & 0x7ffff;
    int32_t offset = signExtend(imm19, 17) * 4;
    
    dasmDbgPrint("[disasm] sf: %d, op: %d, reg: %d, jump offset: 0x%x (%d)\n", sf, op, reg, offset, offset);
    
    uint64_t targetAddr = pc + offset;
    dasmDbgPrint("[disasm] target address: 0x%llx\n", targetAddr);
    
    if (target_adr){
        *target_adr = targetAddr;
    }
    
    uint32_t *opcodes = malloc((JMPSIZ + 1) * sizeof(uint32_t));
    opcodes[0] = assemble_cbz(sf, (~op) & 0x1, reg, 0x18);
    uint32_t *jmp = assemble_jmp(targetAddr, false, jmp_reg);
    memcpy(&opcodes[1], jmp, JMPSIZ * sizeof(uint32_t));
    free(jmp);
    return opcodes;
}

privateFunc bool is_tbz(uint32_t opcode){
    return ((opcode >> 25) & 0x3f) == 0b011011;
}

// tbz / tbnz
privateFunc uint32_t *rehandle_tbz(uint32_t opcode, uint8_t jmp_reg, uint64_t targetAddr){
    uint8_t sf = (opcode >> 31) & 0x1; //xN or wN register (1 = 64 bit, 0 = 32 bit)
    uint8_t op = (opcode >> 24) & 0x1; //0 = tbz; 1 = tbnz
    uint8_t reg = (opcode & 0x1f); //0 -> 30
    
    uint16_t b40 = (opcode >> 19) & 0x1f;
    b40 |= (sf << 5);
    
    uint32_t *opcodes = malloc((JMPSIZ + 1) * sizeof(uint32_t));
    opcodes[0] = assemble_tbz(b40, (~op) & 0x1, reg, 0x18);
    uint32_t *jmp = assemble_jmp(targetAddr, false, jmp_reg);
    memcpy(&opcodes[1], jmp, JMPSIZ * sizeof(uint32_t));
    free(jmp);
    return opcodes;
}

privateFunc uint32_t *handle_tbz(uint64_t pc, uint32_t opcode, uint8_t jmp_reg, uint64_t *target_adr){
    dasmDbgPrint("[disasm] pc: 0x%llx, opcode: 0x%x (tbz/tbnz)\n", pc, opcode);
    
    uint8_t sf = (opcode >> 31) & 0x1; //xN or wN register (1 = 64 bit, 0 = 32 bit)
    uint8_t op = (opcode >> 24) & 0x1; //0 = tbz; 1 = tbnz
    uint8_t reg = (opcode & 0x1f); //0 -> 30
    
    uint16_t b40 = (opcode >> 19) & 0x1f;
    b40 |= (sf << 5);
    
    uint16_t imm14 = (opcode >> 5) & 0x3fff;
    int16_t offset = signExtend16(imm14, 12) * 4;
    
    dasmDbgPrint("[disasm] sf: %d, op: %d, reg: %d, bit: %d, jump offset: 0x%x (%d)\n", sf, op, reg, b40, offset, offset);
    
    uint64_t targetAddr = pc + offset;
    dasmDbgPrint("[disasm] target address: 0x%llx\n", targetAddr);
    
    if (target_adr){
        *target_adr = targetAddr;
    }
    
    uint32_t *opcodes = malloc((JMPSIZ + 1) * sizeof(uint32_t));
    opcodes[0] = assemble_tbz(b40, (~op) & 0x1, reg, 0x18);
    uint32_t *jmp = assemble_jmp(targetAddr, false, jmp_reg);
    memcpy(&opcodes[1], jmp, JMPSIZ * sizeof(uint32_t));
    free(jmp);
    return opcodes;
}

privateFunc bool is_b(uint32_t opcode){
    return ((opcode >> 26) & 0x1f) == 0b00101;
}

bool is_bExit(uint32_t opcode){
    uint8_t op = (opcode >> 31) & 0x01; //0 = b; 1 = bl
    return op == 0 && is_b(opcode);
}

privateFunc uint32_t *rehandle_b(uint32_t opcode, uint8_t jmp_reg, uint64_t targetAddr){
    uint8_t op = (opcode >> 31) & 0x01; //0 = b; 1 = bl
    
    uint32_t *jmp = assemble_jmp(targetAddr, op == BLR_OP, jmp_reg);
    return jmp;
}

privateFunc uint32_t *handle_b(uint64_t pc, uint32_t opcode, uint8_t jmp_reg, uint64_t *target_adr){
    dasmDbgPrint("[disasm] pc: 0x%llx, opcode: 0x%x (b)\n", pc, opcode);
    
    uint8_t op = (opcode >> 31) & 0x01; //0 = b; 1 = bl
    
    uint32_t imm26 = opcode & 0x3ffffff;
    int32_t offset = signExtend(imm26, 24) * 4;
    
    dasmDbgPrint("[disasm] op: %d, jump offset: 0x%x (%d)\n", op, offset, offset);
    
    uint64_t targetAddr = pc + offset;
    dasmDbgPrint("[disasm] target address: 0x%llx\n", targetAddr);
    
    if (target_adr){
        *target_adr = targetAddr;
    }
    
    uint32_t *jmp = assemble_jmp(targetAddr, op == BLR_OP, jmp_reg);
    return jmp;
}

privateFunc bool is_bCond(uint32_t opcode){
    return ((opcode >> 25) & 0x7f) == 0b0101010;
}

privateFunc uint32_t *rehandle_bCond(uint32_t opcode, uint8_t jmp_reg, uint64_t targetAddr){
    uint8_t cond = opcode & 0xf;
    
    uint32_t *opcodes = malloc((JMPSIZ + 2) * sizeof(uint32_t));
    opcodes[0] = assemble_bcond(cond, 0x8);
    
    uint32_t b = assemble_b(0x18);
    opcodes[1] = b;
    
    uint32_t *jmp = assemble_jmp(targetAddr, false, jmp_reg);
    memcpy(&opcodes[2], jmp, JMPSIZ * sizeof(uint32_t));
    
    free(jmp);
    return opcodes;
}

privateFunc uint32_t *handle_bCond(uint64_t pc, uint32_t opcode, uint8_t jmp_reg, uint64_t *target_adr){
    dasmDbgPrint("[disasm] pc: 0x%llx, opcode: 0x%x (b.Cond) \n", pc, opcode);
    
    uint32_t imm19 = (opcode >> 5) & 0x7ffff;
    int32_t offset = signExtend(imm19, 17) * 4;
    
    uint8_t cond = opcode & 0xf;
    
    dasmDbgPrint("[disasm] cond: %d, jump offset: 0x%x (%d)\n", cond, offset, offset);
    
    uint64_t targetAddr = pc + offset;
    dasmDbgPrint("[disasm] target address: 0x%llx\n", targetAddr);
    
    if (target_adr){
        *target_adr = targetAddr;
    }
    
    uint32_t *opcodes = malloc((JMPSIZ + 2) * sizeof(uint32_t));
    opcodes[0] = assemble_bcond(cond, 0x8);
    
    uint32_t b = assemble_b(0x18);
    opcodes[1] = b;
    
    uint32_t *jmp = assemble_jmp(targetAddr, false, jmp_reg);
    memcpy(&opcodes[2], jmp, JMPSIZ * sizeof(uint32_t));
    
    free(jmp);
    return opcodes;
}

privateFunc bool is_ldr(uint32_t opcode){
    return ((opcode >> 24) & 0x3f) == 0b011000;
}

//TODO: ldr simd + ldr (offset from reg)
//ldr/ldrsw (literal)
privateFunc uint32_t *rehandle_ldr(uint32_t opcode, uint8_t jmp_reg, uint64_t targetAddr){
    uint8_t sig = (opcode >> 31) & 0x1; //signed
    uint8_t sf = (opcode >> 30) & 0x1; //xN or wN register (1 = 64 bit; 0 = 32 bit)
    uint8_t reg = (opcode & 0x1f); //0 -> 30
    
    uint32_t *opcodes = malloc(JMPSIZ * sizeof(uint32_t));
    opcodes[0] = assemble_mov(1, MOVZ_OP, 0, reg, targetAddr & 0xffff);
    opcodes[1] = assemble_mov(1, MOVK_OP, 16, reg, (targetAddr >> 16) & 0xffff);
#if __LP64__
    opcodes[2] = assemble_mov(1, MOVK_OP, 32, reg, (targetAddr >> 32) & 0xffff);
    opcodes[3] = assemble_mov(1, MOVK_OP, 48, reg, (targetAddr >> 48) & 0xffff);
    opcodes[4] = assemble_ldr(sf, sig ? LDRSW_OP : LDR_OP, reg, reg);
#else
    opcodes[2] = assemble_ldr(sf, sig ? LDRSW_OP : LDR_OP, reg, reg);
#endif
    return opcodes;
}

privateFunc uint32_t *handle_ldr(uint64_t pc, uint32_t opcode, uint8_t jmp_reg, uint64_t *target_adr){
    dasmDbgPrint("[disasm] pc: 0x%llx, opcode: 0x%x (ldr/ldrsw [literal])\n", pc, opcode);
    
    uint8_t sig = (opcode >> 31) & 0x1; //signed
    uint8_t sf = (opcode >> 30) & 0x1; //xN or wN register (1 = 64 bit; 0 = 32 bit)
    uint8_t reg = (opcode & 0x1f); //0 -> 30
    
    uint32_t imm19 = (opcode >> 5) & 0x7ffff;
    int32_t offset = signExtend(imm19, 17) * 4;
    
    dasmDbgPrint("[disasm] sig: %d, sf: %d, reg: %d, offset: 0x%x (%d)\n", sig, sf, reg, offset, offset);
    
    uint64_t targetAddr = pc + offset;
    dasmDbgPrint("[disasm] target address: 0x%llx\n", targetAddr);
    
    if (target_adr){
        *target_adr = targetAddr;
    }
    
    uint32_t *opcodes = malloc(JMPSIZ * sizeof(uint32_t));
    opcodes[0] = assemble_mov(1, MOVZ_OP, 0, reg, targetAddr & 0xffff);
    opcodes[1] = assemble_mov(1, MOVK_OP, 16, reg, (targetAddr >> 16) & 0xffff);
#if __LP64__
    opcodes[2] = assemble_mov(1, MOVK_OP, 32, reg, (targetAddr >> 32) & 0xffff);
    opcodes[3] = assemble_mov(1, MOVK_OP, 48, reg, (targetAddr >> 48) & 0xffff);
    opcodes[4] = assemble_ldr(sf, sig ? LDRSW_OP : LDR_OP, reg, reg);
#else
    opcodes[2] = assemble_ldr(sf, sig ? LDRSW_OP : LDR_OP, reg, reg);
#endif
    return opcodes;
}

privateFunc bool is_ldr_simd_fp(uint32_t opcode){
    return ((opcode >> 24) & 0x3f) == 0b011100;
}

privateFunc uint32_t *handle_ldr_simd_fp(uint64_t pc, uint32_t opcode, uint8_t jmp_reg, uint64_t *target_adr){
    dasmDbgPrint("[disasm] pc: 0x%llx, opcode: 0x%x (ldr [simd/fp])\n", pc, opcode);
    
    libhooker_panic("SIMD/FP LDR not implemented!");
    return NULL;
}

privateFunc bool is_prfm(uint32_t opcode){
    return ((opcode >> 24) & 0xff) == 0b11011000;
}

privateFunc uint32_t *handle_prfm(uint64_t pc, uint32_t opcode, uint8_t jmp_reg, uint64_t *target_adr){
    dasmDbgPrint("[disasm] pc: 0x%llx, opcode: 0x%x (prfm)\n", pc, opcode);
    
    uint32_t *opcodes = malloc(1 * sizeof(uint32_t));
    opcodes[0] = assemble_nop();
    return opcodes;
}

privateFunc bool is_adr(uint32_t opcode){
    return ((opcode >> 24) & 0x1f) == 0b10000;
}

//adr/adrp
privateFunc uint32_t *rehandle_adr(uint32_t opcode, uint8_t jmp_reg, uint64_t targetAddr){
    uint8_t reg = (opcode & 0x1f); //0 -> 30
    
    uint32_t *opcodes = malloc((JMPSIZ - 1) * sizeof(uint32_t));
    opcodes[0] = assemble_mov(1, MOVZ_OP, 0, reg, targetAddr & 0xffff);
    opcodes[1] = assemble_mov(1, MOVK_OP, 16, reg, (targetAddr >> 16) & 0xffff);
#if __LP64__
    opcodes[2] = assemble_mov(1, MOVK_OP, 32, reg, (targetAddr >> 32) & 0xffff);
    opcodes[3] = assemble_mov(1, MOVK_OP, 48, reg, (targetAddr >> 48) & 0xffff);
#endif
    return opcodes;
}

privateFunc uint32_t *handle_adr(uint64_t pc, uint32_t opcode, uint8_t jmp_reg, uint64_t *target_adr){
    dasmDbgPrint("[disasm] pc: 0x%llx, opcode: 0x%x (adr/adrp])\n", pc, opcode);
    uint8_t op = (opcode >> 31) & 0x1; //0 = adr, 1 = adrp
    uint8_t reg = (opcode & 0x1f); //0 -> 30
    
    uint32_t immhi = (opcode >> 5) & 0x7ffff;
    uint8_t immlo = (opcode >> 29) & 0x3;
    
    uint64_t imm = (immhi << 2) | immlo; //21 bits
    if (op == 1){
        imm = imm << 12;
        pc = pc & ~0xfff; //mask bottom 12 bits on pc
    }
    
    int64_t offset = signExtend64(imm, op ? 32 : 20);
    
    dasmDbgPrint("[disasm] op: %d, reg: %d, offset: 0x%llx (%lld)\n", op, reg, offset, offset);
    uint64_t targetAddr = pc + offset;
    dasmDbgPrint("[disasm] target address: 0x%llx\n", targetAddr);
    
    if (target_adr){
        *target_adr = targetAddr;
    }
    
    uint32_t *opcodes = malloc((JMPSIZ - 1) * sizeof(uint32_t));
    opcodes[0] = assemble_mov(1, MOVZ_OP, 0, reg, targetAddr & 0xffff);
    opcodes[1] = assemble_mov(1, MOVK_OP, 16, reg, (targetAddr >> 16) & 0xffff);
#if __LP64__
    opcodes[2] = assemble_mov(1, MOVK_OP, 32, reg, (targetAddr >> 32) & 0xffff);
    opcodes[3] = assemble_mov(1, MOVK_OP, 48, reg, (targetAddr >> 48) & 0xffff);
#endif
    return opcodes;
}

privateFunc bool is_ret(uint32_t opcode) {
    return (((opcode >> 25) & 0x7f) == 0b1101011) && (((opcode >> 21) & 0xf) == 0b10);
}

privateFunc uint32_t *handle_ret(uint64_t pc, uint32_t opcode, uint8_t jmp_reg, uint64_t *target_adr){
    dasmDbgPrint("[disasm] pc: 0x%llx, opcode: 0x%x (ret)\n", pc, opcode);
    
    uint32_t *opcodes = malloc(1 * sizeof(uint32_t));
    opcodes[0] = opcode;
    return opcodes;
}

privateFunc bool is_nullop(uint32_t opcode){
    return true;
}

privateFunc uint32_t *handle_nullop(uint64_t pc, uint32_t opcode, uint8_t jmp_reg, uint64_t *target_adr){
    dasmDbgPrint("[disasm] pc: 0x%llx, opcode: 0x%x (unhandled)\n", pc, opcode);
    
    uint32_t *opcodes = malloc(1 * sizeof(uint32_t));
    opcodes[0] = opcode;
    return opcodes;
}

struct disasm_reg_t disasm_registry[] = {
    {&is_ret, &handle_ret, NULL, 1 * sizeof(uint32_t)}, //first registered entry must be `ret`
    {&is_cbz, &handle_cbz, &rehandle_cbz, (JMPSIZ + 1) * sizeof(uint32_t)},
    {&is_tbz, &handle_tbz, &rehandle_tbz, (JMPSIZ + 1) * sizeof(uint32_t)},
    {&is_b, &handle_b, &rehandle_b, JMPSIZ * sizeof(uint32_t)},
    {&is_bCond, &handle_bCond, &rehandle_bCond, (JMPSIZ + 2) * sizeof(uint32_t)},
    {&is_ldr, &handle_ldr, &rehandle_ldr, JMPSIZ * sizeof(uint32_t)},
    {&is_ldr_simd_fp, &handle_ldr_simd_fp, NULL, 0},
    {&is_prfm, &handle_prfm, NULL, 1 * sizeof(uint32_t)},
    {&is_adr, &handle_adr, &rehandle_adr, (JMPSIZ - 1) * sizeof(uint32_t)},
    {&is_nullop, &handle_nullop, NULL, 1 * sizeof(uint32_t)}
};

void LHGetDisasmRegistry(struct disasm_reg_t **disasm_reg, int *size){
    *disasm_reg = (struct disasm_reg_t *)&disasm_registry;
    *size = sizeof(disasm_registry)/sizeof(struct disasm_reg_t);
}
