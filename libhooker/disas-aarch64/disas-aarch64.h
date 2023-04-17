//
//  disas-aarch64.h
//  libhooker
//
//  Created by CoolStar on 2/21/20.
//  Copyright © 2020 CoolStar. All rights reserved.
//
#include <stdint.h>
#include <stdbool.h>

#ifndef disas_aarch64_h
#define disas_aarch64_h

#if DEBUG
#define DASMDBG 0
#else
#define DASMDBG 0
#endif

#if DASMDBG
#define dasmDbgPrint debugPrint
#else
#define dasmDbgPrint(...)
#endif

typedef bool (*is_opcode)(uint32_t);
typedef uint32_t *(*handle_opcode)(uint64_t, uint32_t, uint8_t, uint64_t *);
typedef uint32_t *(*rehandle_opcode)(uint32_t, uint8_t, uint64_t);

typedef struct disasm_reg_t {
    is_opcode is_op;
    handle_opcode handle_op;
    rehandle_opcode rehandle_op;
    int tramp_size;
} disasm_reg;

void LHGetDisasmRegistry(struct disasm_reg_t **disasm_reg, int *size);

bool is_bExit(uint32_t opcode);
#if DEBUG
uint32_t *handle_cbz(uint64_t pc, uint32_t op, uint8_t jmp_reg, uint64_t *target_adr);
uint32_t *handle_tbz(uint64_t pc, uint32_t opcode, uint8_t jmp_reg, uint64_t *target_adr);
uint32_t *handle_b(uint64_t pc, uint32_t opcode, uint8_t jmp_reg, uint64_t *target_adr);
uint32_t *handle_bCond(uint64_t pc, uint32_t opcode, uint8_t jmp_reg, uint64_t *target_adr);
uint32_t *handle_ldr(uint64_t pc, uint32_t opcode, uint8_t jmp_reg, uint64_t *target_adr);
uint32_t *handle_prfm(uint64_t pc, uint32_t opcode, uint8_t jmp_reg, uint64_t *target_adr);
#endif

#endif /* disas_aarch64_h */
