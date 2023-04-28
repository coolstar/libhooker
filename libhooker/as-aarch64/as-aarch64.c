//
//  as-aarch64.c
//  libhooker
//
//  Created by CoolStar on 2/22/20.
//  Copyright © 2020 CoolStar. All rights reserved.
//

#include "as-aarch64.h"
#include "funcMacros.h"
#include <stdio.h>

uint32_t assemble_mov(uint8_t sf, uint8_t op, uint8_t shift, uint8_t reg, uint16_t immediate){
    guardRetFalseDbgPanic(reg >= 31, "Invalid register");
    guardRetFalseDbgPanic(sf != (sf & 0x1), "Invalid sf field");
    guardRetFalseDbgPanic(op != (op & 0x3) || op == 1, "Invalid op");
    
    guardRetFalseDbgPanic((shift % 16) != 0, "Invalid shift");
    shift /= 16;
    guardRetFalseDbgPanic(shift > (1 + sf * 2), "Invalid shift");
    
    uint32_t instruction = 0;
    instruction |= ((uint32_t)sf << 31);
    instruction |= (op << 29);
    instruction |= (0b100101 << 23);
    instruction |= (shift << 21);
    instruction |= (immediate << 5);
    instruction |= reg;
    
    asmDbgPrint("[asm] Assembled instruction: 0x%x\n", instruction);
    return instruction;
}

uint32_t assemble_br(uint8_t op, uint8_t reg, uint8_t reg_pacmod, uint8_t pactype){
    guardRetFalseDbgPanic(reg >= 31, "Invalid register");
#if __arm64e__
    guardRetFalseDbgPanic(reg_pacmod >= 31, "Invalid register");
    guardRetFalseDbgPanic(pactype > 6 || pactype == 1, "Invalid PAC type");
#endif
    
    uint8_t z = 0;
    uint8_t usePac = 0;
    uint8_t m = 0;
#if __arm64e__
    if (pactype){
        usePac = 1;
        
        z = (~pactype & 0x1);
        m = (pactype & 0x4);
        
        if (z == 0){
            reg_pacmod = 0x1f;
        }
    } else {
        reg_pacmod = 0x00;
    }
#endif
    
    uint32_t instruction = 0;
    instruction |= ((uint32_t)0b1101011 << 25);
    instruction |= (z << 24);
    instruction |= (op << 21);
    instruction |= (0b11111 << 16);
    instruction |= (usePac << 11);
    instruction |= (m << 10);
    instruction |= (reg << 5);
    instruction |= reg_pacmod;
    
    asmDbgPrint("[asm] Assembled instruction: 0x%x\n", instruction);
    return instruction;
}

uint32_t *assemble_jmp(uint64_t targetAddr, bool link, uint8_t jmp_reg){
    uint32_t first_movz = assemble_mov(1, MOVZ_OP, 0, jmp_reg, targetAddr & 0xffff);
    uint32_t second_movk = assemble_mov(1, MOVK_OP, 16, jmp_reg, (targetAddr >> 16) & 0xffff);
#if __LP64__
    uint32_t third_movk = assemble_mov(1, MOVK_OP, 32, jmp_reg, (targetAddr >> 32) & 0xffff);
    uint32_t fourth_movk = assemble_mov(1, MOVK_OP, 48, jmp_reg, (targetAddr >> 48) & 0xffff);
#endif
    uint32_t br = assemble_br(link ? BLR_OP : BR_OP, jmp_reg, 0, PAC_NONE);
    
    uint32_t *instrs = malloc(sizeof(uint32_t) * JMPSIZ);
    instrs[0] = first_movz;
    instrs[1] = second_movk;
#if __LP64__
    instrs[2] = third_movk;
    instrs[3] = fourth_movk;
    instrs[4] = br;
#else
    instrs[2] = br;
#endif
    return instrs;
}

uint32_t assemble_cbz(uint8_t sf, uint8_t op, uint8_t reg, uint32_t offset){
    guardRetFalseDbgPanic(reg >= 31, "Invalid register");
    guardRetFalseDbgPanic(sf != (sf & 0x1), "Invalid sf field");
    guardRetFalseDbgPanic(op != (op & 0x1), "Invalid op");
    
    guardRetFalseDbgPanic((offset % 4) != 0, "Invalid offset");
    offset /= 4;
    guardRetFalseDbgPanic(offset != (offset & 0x3ffff), "Invalid offset");
    
    uint32_t instruction = 0;
    instruction |= (sf << 31);
    instruction |= (0b011010 << 25);
    instruction |= (op << 24);
    instruction |= (offset << 5);
    instruction |= reg;
    
    asmDbgPrint("[asm] Assembled instruction: 0x%x\n", instruction);
    
    return instruction;
}

uint32_t assemble_tbz(uint8_t bit, uint8_t op, uint8_t reg, uint32_t offset){
    guardRetFalseDbgPanic(reg >= 31, "Invalid register");
    guardRetFalseDbgPanic(op != (op & 0x1), "Invalid op");
    
    guardRetFalseDbgPanic((offset % 4) != 0, "Invalid offset");
    offset /= 4;
    guardRetFalseDbgPanic(offset != (offset & 0x3fff), "Invalid offset");
    
    uint8_t sf = (bit >> 5) & 0x1; //use w* registers if testing bit below 32
    bit &= 0x1f;
    
    uint32_t instruction = 0;
    instruction |= (sf << 31);
    instruction |= (0b011011 << 25);
    instruction |= (op << 24);
    instruction |= (bit << 19);
    instruction |= (offset << 5);
    instruction |= reg;
    
    asmDbgPrint("[asm] Assembled instruction: 0x%x\n", instruction);
    
    return instruction;
}

uint32_t assemble_b(uint64_t offset){
    guardRetFalseDbgPanic((offset % 4) != 0, "Invalid offset");
    offset /= 4;
    guardRetFalseDbgPanic(offset != (offset & 0x3ffffff), "Invalid offset");
    
    uint32_t instruction = 0;
    instruction |= (0b000101 << 26);
    instruction |= offset;
    
    asmDbgPrint("[asm] Assembled instruction: 0x%x\n", instruction);
    return instruction;
}

uint32_t assemble_bcond(uint8_t cond, uint32_t offset){
    guardRetFalseDbgPanic(cond != (cond & 0xf), "Invalid condition");
    
    guardRetFalseDbgPanic((offset % 4) != 0, "Invalid offset");
    offset /= 4;
    guardRetFalseDbgPanic(offset != (offset & 0x7ffff), "Invalid offset");
    
    uint32_t instruction = 0;
    instruction |= (0b0101010 << 25);
    instruction |= (offset << 5);
    instruction |= cond;
    
    asmDbgPrint("[asm] Assembled instruction: 0x%x\n", instruction);
    return instruction;
}

uint32_t assemble_ldr(uint8_t sf, uint8_t op, uint8_t reg_dst, uint8_t reg_src){
    guardRetFalseDbgPanic(sf != (sf & 0x1), "Invalid sf field");
    guardRetFalseDbgPanic(reg_dst >= 31, "Invalid dest register");
    guardRetFalseDbgPanic(reg_src >= 31, "Invalid src register");
    guardRetFalseDbgPanic(op != LDR_OP && op != LDRSW_OP, "Invalid OP");
    
    uint32_t instruction = 0;
    instruction |= (1 << 31);
    instruction |= (sf << 30);
    instruction |= (0b111 << 27);
    instruction |= (op << 22);
    instruction |= (1 << 21);
    instruction |= (31 << 16); //Rm
    instruction |= (0b011 << 13); //option
    instruction |= (0b10 << 10);
    instruction |= (reg_src << 5);
    instruction |= reg_dst;
    
    asmDbgPrint("[asm] Assembled instruction: 0x%x\n", instruction);
    return instruction;
}

uint32_t assemble_ldri(uint8_t sf, uint32_t offset, uint8_t reg_dst){
    guardRetFalseDbgPanic(sf != (sf & 0x1), "Invalid sf field");
    guardRetFalseDbgPanic(sf != 1, "Only 64 bit supported by this assembler");
    guardRetFalseDbgPanic(offset % 4 != 0, "Offset must be aligned");
    guardRetFalseDbgPanic((offset / 4) != ((offset / 4) & 0x7FFFF), "Offset/4 must fit within 19 bits");
    guardRetFalseDbgPanic(reg_dst >= 31, "Invalid dest register");
    
    uint32_t instruction = 0;
    instruction |= (0 << 31);
    instruction |= (sf << 30);
    instruction |= (0b011 << 27);
    instruction |= ((offset / 4) << 5);
    instruction |= reg_dst;
    
    asmDbgPrint("[asm] Assembled instruction: 0x%x\n", instruction);
    return instruction;
}

uint32_t assemble_nop(void){
    uint32_t instruction = 0xd503201f; //we can hardcode this one
    asmDbgPrint("[asm] Assembled instruction: 0x%x\n", instruction);
    return instruction;
}

//load / store pair
uint32_t assemble_regp(uint8_t sf, uint8_t pop, int32_t offset, uint8_t reg1, uint8_t reg2){
    guardRetFalseDbgPanic(sf != (sf & 0x1), "Invalid sf field");
    guardRetFalseDbgPanic(reg1 >= 31, "Invalid dest register");
    guardRetFalseDbgPanic(reg2 >= 31, "Invalid src register");
    
    guardRetFalseDbgPanic((offset % (sf ? 8 : 4)) != 0, "Invalid offset");
    offset /= (sf ? 8 : 4);
    
    uint32_t imm = (offset & 0x7f);
    uint32_t instruction = 0;
    instruction |= (sf << 31);
    instruction |= ((pop ? 0b1010001 : 0b1010011) << 23);
    instruction |= (pop << 22);
    instruction |= (imm << 15);
    instruction |= (reg2 << 10);
    instruction |= (31 << 5);
    instruction |= reg1;
    
    asmDbgPrint("[asm] Assembled instruction: 0x%x\n", instruction);
    return instruction;
}

//assemble adrp
uint32_t assemble_adrp(uint64_t pc, uint8_t reg, uint64_t target_page){
    guardRetFalseDbgPanic(target_page != (target_page & ~0xfff), "Target must be a page");
    guardRetFalseDbgPanic(reg >= 31, "Invalid dest register");
    
    uint64_t pc_page = (pc & ~0xfff);
    int64_t offset = target_page - pc_page;
    
    uint8_t op = 1; //0 = adr, 1 = adrp
    
    uint64_t imm = offset >> 12;
    uint32_t immlo = (uint32_t)((imm & 0x3) << 29);
    uint32_t immhi = (uint32_t)(((imm >> 2) & 0x7ffff) << 5);
    
    uint32_t opcode = 0;
    opcode |= ((uint32_t)op << 31);
    opcode |= immlo;
    opcode |= (0b10000 << 24);
    opcode |= immhi;
    opcode |= reg;
    return opcode;
}

//assemble add
uint32_t assemble_add(uint8_t dst, uint8_t src, uint32_t imm12){
    guardRetFalseDbgPanic(imm12 != (imm12 & 0xfff), "Immediate addition too big");
    guardRetFalseDbgPanic(dst >= 31, "Invalid dest register");
    guardRetFalseDbgPanic(src >= 31, "Invalid src register");
    
    uint32_t opcode = 0;
    opcode |= (UINT32_C(1) << 31);
    opcode |= (0b10001) << 24;
    opcode |= (imm12 << 10);
    opcode |= (src << 5);
    opcode |= dst;
    return opcode;
}
