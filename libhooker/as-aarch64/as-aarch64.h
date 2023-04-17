//
//  as-aarch64.h
//  libhooker
//
//  Created by CoolStar on 2/22/20.
//  Copyright © 2020 CoolStar. All rights reserved.
//
#include <stdint.h>
#include <stdbool.h>

#ifndef as_aarch64_h
#define as_aarch64_h

#if DEBUG
#define ASMDBG 0
#else
#define ASMDBG 0
#endif

#if ASMDBG
#define asmDbgPrint debugPrint
#else
#define asmDbgPrint(...)
#endif

#if __LP64__
#define JMPSIZ 5
#else
#define JMPSIZ 3
#endif

enum COND_CODES {
    EQ = 0,
    NE, CS, CC, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE, AL
};

enum PAC_TYPE { //ARMv8.3
    PAC_NONE = 0, //BR / BLR
    PAC_AA = 2, //BRAA / BLRAA
    PAC_AAZ = 3, //BRAAZ / BLRAAZ
    PAC_AB = 4, //BRAB / BLRAB
    PAC_ABZ = 5 //BRABZ / BLRABZ
};

enum MOV_OPS {
    MOVN_OP = 0,
    MOVZ_OP = 2,
    MOVK_OP = 3
};

enum LDR_OPS {
    LDR_OP = 1,
    LDRSW_OP = 2
};

enum BR_OP {
    BR_OP = 0,
    BLR_OP = 1
};

enum CBZ_OPS {
    CBZ_OP = 0,
    CBNZ_OP = 1
};

enum TBZ_OPS {
    TBZ_OP = 0,
    TBNZ_OP = 1
};

uint32_t assemble_mov(uint8_t sf, uint8_t op, uint8_t shift, uint8_t reg, uint16_t immediate);
uint32_t assemble_br(uint8_t op, uint8_t reg, uint8_t reg_pacmod, uint8_t pactype);
uint32_t *assemble_jmp(uint64_t targetAddr, bool link, uint8_t jmp_reg);

uint32_t assemble_cbz(uint8_t sf, uint8_t op, uint8_t reg, uint32_t offset);
uint32_t assemble_tbz(uint8_t bit, uint8_t op, uint8_t reg, uint32_t offset);
uint32_t assemble_b(uint64_t offset);
uint32_t assemble_bcond(uint8_t cond, uint32_t offset);
uint32_t assemble_ldr(uint8_t sf, uint8_t op, uint8_t reg_dst, uint8_t reg_src);
uint32_t assemble_ldri(uint8_t sf, uint32_t offset, uint8_t reg_dst);

uint32_t assemble_nop(void);

uint32_t assemble_regp(uint8_t sf, uint8_t pop, int32_t offset, uint8_t reg1, uint8_t reg2);

uint32_t assemble_adrp(uint64_t pc, uint8_t reg, uint64_t target_page);

uint32_t assemble_add(uint8_t reg1, uint8_t reg2, uint32_t imm12);
#endif /* as_aarch64_h */
