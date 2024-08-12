/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) Yuichi Nakamura
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef MICROPY_INCLUDED_PY_ASMM68K_H
#define MICROPY_INCLUDED_PY_ASMM68K_H

#include <assert.h>
#include "py/misc.h"
#include "py/asmbase.h"
#include "py/persistentcode.h"

#define ASM_M68K_REG_D0     (0)
#define ASM_M68K_REG_D1     (1)
#define ASM_M68K_REG_D2     (2)
#define ASM_M68K_REG_D3     (3)
#define ASM_M68K_REG_D4     (4)
#define ASM_M68K_REG_D5     (5)
#define ASM_M68K_REG_D6     (6)
#define ASM_M68K_REG_D7     (7)
#define ASM_M68K_REG_A0     (8)
#define ASM_M68K_REG_A1     (9)
#define ASM_M68K_REG_A2     (10)
#define ASM_M68K_REG_A3     (11)
#define ASM_M68K_REG_A4     (12)
#define ASM_M68K_REG_A5     (13)
#define ASM_M68K_REG_A6     (14)
#define ASM_M68K_REG_A7     (15)
#define ASM_M68K_REG_FP     (ASM_M68K_REG_A6)
#define ASM_M68K_REG_SP     (ASM_M68K_REG_A7)

#define ASM_M68K_CC_T   (0x0)
#define ASM_M68K_CC_F   (0x1)
#define ASM_M68K_CC_HI  (0x2)
#define ASM_M68K_CC_LS  (0x3)
#define ASM_M68K_CC_CC  (0x4)
#define ASM_M68K_CC_CS  (0x5)
#define ASM_M68K_CC_NE  (0x6)
#define ASM_M68K_CC_EQ  (0x7)
#define ASM_M68K_CC_VC  (0x8)
#define ASM_M68K_CC_VS  (0x9)
#define ASM_M68K_CC_PL  (0xa)
#define ASM_M68K_CC_MI  (0xb)
#define ASM_M68K_CC_GE  (0xc)
#define ASM_M68K_CC_LT  (0xd)
#define ASM_M68K_CC_GT  (0xe)
#define ASM_M68K_CC_LE  (0xf)

// m68k passes values on the stack, but the emitter is register based, so we need
// to define registers that can temporarily hold the function arguments.  They
// need to be defined here so that asm_m68k_call_ind can push them onto the stack
// before the call.
#define ASM_M68K_REG_ARG_1 ASM_M68K_REG_D0
#define ASM_M68K_REG_ARG_2 ASM_M68K_REG_D1
#define ASM_M68K_REG_ARG_3 ASM_M68K_REG_D2
#define ASM_M68K_REG_ARG_4 ASM_M68K_REG_D3

typedef struct _asm_m68k_t {
    mp_asm_base_t base;
    uint stack_adjust;
} asm_m68k_t;

static inline void asm_m68k_end_pass(asm_m68k_t *as) {
    (void)as;
}

void asm_m68k_entry(asm_m68k_t *as, int num_locals);
void asm_m68k_exit(asm_m68k_t *as);

void asm_m68k_op16(asm_m68k_t *as, uint op);
void asm_m68k_op32(asm_m68k_t *as, uint32_t op);

#define ASM_M68K_REGEA_ENCODE(op, reg, ea) \
    ((op) | (((reg) & 7) << 9) | (ea))
#define ASM_M68K_EA_ENCODE(op, ea)      ASM_M68K_REGEA_ENCODE((op), 0, (ea))
#define ASM_M68K_REG_ENCODE(op, reg)    ASM_M68K_REGEA_ENCODE((op), (reg), 0)
#define ASM_M68K_CC_ENCODE(op, cc, ea) \
    ((op) | ((cc) << 8) | (ea))
#define ASM_M68K_MOVE_ENCODE(op, dest, src) \
    ((op) | (((dest) & 7) << 9) | (((dest) & 0x38) << (6 - 3)) | (src))

static inline void asm_m68k_op_ea(asm_m68k_t *as, uint op, uint ea) {
    asm_m68k_op16(as, ASM_M68K_EA_ENCODE(op, ea));
}

static inline void asm_m68k_op_reg_imm8(asm_m68k_t *as, uint op, uint reg, uint imm) {
    asm_m68k_op16(as, ASM_M68K_REG_ENCODE(op, reg) | (imm & 0xff));
}

static inline void asm_m68k_op_regea(asm_m68k_t *as, uint op, uint reg, uint ea) {
    asm_m68k_op16(as, ASM_M68K_REGEA_ENCODE(op, reg, ea));
}

static inline void asm_m68k_op_cc(asm_m68k_t *as, uint op, uint cc, uint ea) {
    asm_m68k_op16(as, ASM_M68K_CC_ENCODE(op, cc, ea));
}

static inline void asm_m68k_op_move(asm_m68k_t *as, uint op, uint dest, uint src) {
    asm_m68k_op16(as, ASM_M68K_MOVE_ENCODE(op, dest, src));
}

void asm_m68k_mov_arg_to_r32(asm_m68k_t *as, int src_arg_num, int rd);
void asm_m68k_mov_args_to_r32(asm_m68k_t *as, int src_arg_num, uint reglist);
void asm_m68k_cmp_reg_reg_setcc(asm_m68k_t *as, uint rd, uint rs, uint cond, uint rr);

void asm_m68k_jump(asm_m68k_t *as, uint label);
void asm_m68k_jmp_if_reg_zero(asm_m68k_t *as, uint reg, uint label, uint bool_test);
void asm_m68k_jmp_if_reg_nonzero(asm_m68k_t *as, uint reg, uint label, uint bool_test);
void asm_m68k_jmp_if_reg_eq(asm_m68k_t *as, uint reg1, uint reg2, uint label);
void asm_m68k_jmp_reg(asm_m68k_t *as, uint reg);
void asm_m68k_call_ind(asm_m68k_t *as, uint idx, uint n_args);
void asm_m68k_mov_reg_pcrel(asm_m68k_t *as, uint rd, uint label);

void asm_m68k_mov_reg_imm(asm_m68k_t *as, uint rd, int imm);
void asm_m68k_mov_local_reg(asm_m68k_t *as, int local_num, uint rs);
void asm_m68k_mov_reg_local(asm_m68k_t *as, uint rd, int local_num);
void asm_m68k_mov_reg_local_addr(asm_m68k_t *as, uint rd, int local_num);
void asm_m68k_mov_reg_reg(asm_m68k_t *as, uint rd, uint rs);

void asm_m68k_lsl_reg_reg(asm_m68k_t *as, uint rd, uint rshift);
void asm_m68k_lsr_reg_reg(asm_m68k_t *as, uint rd, uint rshift);
void asm_m68k_asr_reg_reg(asm_m68k_t *as, uint rd, uint rshift);
void asm_m68k_or_reg_reg(asm_m68k_t *as, uint rd, uint rs);
void asm_m68k_xor_reg_reg(asm_m68k_t *as, uint rd, uint rs);
void asm_m68k_and_reg_reg(asm_m68k_t *as, uint rd, uint rs);
void asm_m68k_add_reg_reg(asm_m68k_t *as, uint rd, uint rs);
void asm_m68k_sub_reg_reg(asm_m68k_t *as, uint rd, uint rs);
void asm_m68k_mul_reg_reg(asm_m68k_t *as, uint rd, uint rs);

void asm_m68k_ld_reg_reg(asm_m68k_t *as, uint rd, uint rbase);
void asm_m68k_ld_reg_reg_ofst(asm_m68k_t *as, uint rd, uint rbase, uint word_offset);
void asm_m68k_ld8_reg_reg(asm_m68k_t *as, uint rd, uint rbase);
void asm_m68k_ld16_reg_reg(asm_m68k_t *as, uint rd, uint rbase);
void asm_m68k_ld16_reg_reg_ofst(asm_m68k_t *as, uint rd, uint rbase, uint uint16_offset);
void asm_m68k_ld32_reg_reg(asm_m68k_t *as, uint rd, uint rbase);

void asm_m68k_st_reg_reg(asm_m68k_t *as, uint rs, uint rbase);
void asm_m68k_st_reg_reg_ofst(asm_m68k_t *as, uint rs, uint rbase, uint word_offset);
void asm_m68k_st8_reg_reg(asm_m68k_t *as, uint rs, uint rbase);
void asm_m68k_st16_reg_reg(asm_m68k_t *as, uint rs, uint rbase);
void asm_m68k_st32_reg_reg(asm_m68k_t *as, uint rs, uint rbase);

// Temporary address register for register indirect addressing
#define ASM_M68K_REG_AT         ASM_M68K_REG_A0

#define ASM_M68K_IND(areg)      (((areg) & 7) | 0x10)
#define ASM_M68K_PRD(areg)      (((areg) & 7) | 0x20)
#define ASM_M68K_DSP(areg)      (((areg) & 7) | 0x28)
#define ASM_M68K_PCDSP          (0x3a)
#define ASM_M68K_IMM            (0x3c)

#define ASM_M68K_AT_IND         ASM_M68K_IND(ASM_M68K_REG_AT)
#define ASM_M68K_AT_DSP         ASM_M68K_DSP(ASM_M68K_REG_AT)
#define ASM_M68K_FP_DSP         ASM_M68K_DSP(ASM_M68K_REG_FP)
#define ASM_M68K_SP_PRD         ASM_M68K_PRD(ASM_M68K_REG_SP)
#define ASM_M68K_SP_DSP         ASM_M68K_DSP(ASM_M68K_REG_SP)
#define ASM_M68K_IS_AREG(reg)   ((reg) >= 8)

#define ASM_M68K_REG_FUN_TABLE  ASM_M68K_REG_A5

#if GENERIC_ASM_API

// The following macros provide a (mostly) arch-independent API to
// generate native code, and are used by the native emitter.

#define ASM_WORD_SIZE (4)

#define REG_RET     ASM_M68K_REG_D0
#define REG_ARG_1   ASM_M68K_REG_ARG_1
#define REG_ARG_2   ASM_M68K_REG_ARG_2
#define REG_ARG_3   ASM_M68K_REG_ARG_3
#define REG_ARG_4   ASM_M68K_REG_ARG_4

#define REG_TEMP0   ASM_M68K_REG_D0
#define REG_TEMP1   ASM_M68K_REG_D1
#define REG_TEMP2   ASM_M68K_REG_D2

#define REG_LOCAL_1     ASM_M68K_REG_D4
#define REG_LOCAL_2     ASM_M68K_REG_D5
#define REG_LOCAL_3     ASM_M68K_REG_D6
#define REG_LOCAL_NUM (3)

#define REG_FUN_TABLE ASM_M68K_REG_FUN_TABLE

#define ASM_T               asm_m68k_t
#define ASM_END_PASS        asm_m68k_end_pass
#define ASM_ENTRY           asm_m68k_entry
#define ASM_EXIT            asm_m68k_exit

#define ASM_JUMP(as, label) asm_m68k_jump((as), (label))
#define ASM_JUMP_IF_REG_ZERO(as, reg, label, bool_test) asm_m68k_jmp_if_reg_zero((as), (reg), (label), (bool_test))
#define ASM_JUMP_IF_REG_NONZERO(as, reg, label, bool_test) asm_m68k_jmp_if_reg_nonzero((as), (reg), (label), (bool_test))
#define ASM_JUMP_IF_REG_EQ(as, reg1, reg2, label) asm_m68k_jmp_if_reg_eq((as), (reg1), (reg2), (label))
#define ASM_JUMP_REG(as, reg) asm_m68k_jmp_reg((as),(reg))
#define ASM_CALL_IND(as, idx) asm_m68k_call_ind((as), (idx), mp_f_n_args[idx])
#define ASM_CALL_IND_N(as, idx, n_args) asm_m68k_call_ind((as), (idx), (n_args))

#define ASM_MOV_LOCAL_REG(as, local_num, reg_src) asm_m68k_mov_local_reg((as), (local_num), (reg_src))
#define ASM_MOV_REG_IMM(as, reg_dest, imm) asm_m68k_mov_reg_imm((as), (reg_dest), (imm))
#define ASM_MOV_REG_IMM_FIX_U16(as, reg_dest, imm) asm_m68k_mov_reg_imm((as), (reg_dest), (imm))
#define ASM_MOV_REG_IMM_FIX_WORD(as, reg_dest, imm) asm_m68k_mov_reg_imm((as), (reg_dest), (imm))
#define ASM_MOV_REG_LOCAL(as, reg_dest, local_num) asm_m68k_mov_reg_local((as), (reg_dest), (local_num))
#define ASM_MOV_REG_REG(as, reg_dest, reg_src) asm_m68k_mov_reg_reg((as), (reg_dest), (reg_src))
#define ASM_MOV_REG_LOCAL_ADDR(as, reg_dest, local_num) asm_m68k_mov_reg_local_addr((as), (reg_dest), (local_num))
#define ASM_MOV_REG_PCREL(as, reg_dest, label) asm_m68k_mov_reg_pcrel((as), (reg_dest), (label))

#define ASM_LSL_REG_REG(as, reg_dest, reg_shift) asm_m68k_lsl_reg_reg((as), (reg_dest), (reg_shift))
#define ASM_LSR_REG_REG(as, reg_dest, reg_shift) asm_m68k_lsr_reg_reg((as), (reg_dest), (reg_shift))
#define ASM_ASR_REG_REG(as, reg_dest, reg_shift) asm_m68k_asr_reg_reg((as), (reg_dest), (reg_shift))
#define ASM_OR_REG_REG(as, reg_dest, reg_src) asm_m68k_or_reg_reg((as), (reg_dest), (reg_src))
#define ASM_XOR_REG_REG(as, reg_dest, reg_src) asm_m68k_xor_reg_reg((as), (reg_dest), (reg_src))
#define ASM_AND_REG_REG(as, reg_dest, reg_src) asm_m68k_and_reg_reg((as), (reg_dest), (reg_src))
#define ASM_ADD_REG_REG(as, reg_dest, reg_src) asm_m68k_add_reg_reg((as), (reg_dest), (reg_src))
#define ASM_SUB_REG_REG(as, reg_dest, reg_src) asm_m68k_sub_reg_reg((as), (reg_dest), (reg_src))
#if !MICROPY_SMALL_INT_MUL_HELPER
#define ASM_MUL_REG_REG(as, reg_dest, reg_src) asm_m68k_mul_reg_reg((as), (reg_dest), (reg_src))
#endif

#define ASM_LOAD_REG_REG(as, reg_dest, reg_base) asm_m68k_ld_reg_reg((as), (reg_dest), (reg_base))
#define ASM_LOAD_REG_REG_OFFSET(as, reg_dest, reg_base, offset) asm_m68k_ld_reg_reg_ofst((as), (reg_dest), (reg_base), (offset))
#define ASM_LOAD8_REG_REG(as, reg_dest, reg_base) asm_m68k_ld8_reg_reg((as), (reg_dest), (reg_base))
#define ASM_LOAD16_REG_REG(as, reg_dest, reg_base) asm_m68k_ld16_reg_reg((as), (reg_dest), (reg_base))
#define ASM_LOAD16_REG_REG_OFFSET(as, reg_dest, reg_base, offset) asm_m68k_ld16_reg_reg_ofst((as), (reg_dest), (reg_base), (offset))
#define ASM_LOAD32_REG_REG(as, reg_dest, reg_base) asm_m68k_ld32_reg_reg((as), (reg_dest), (reg_base))

#define ASM_STORE_REG_REG(as, reg_src, reg_base) asm_m68k_st_reg_reg((as), (reg_src), (reg_base))
#define ASM_STORE_REG_REG_OFFSET(as, reg_src, reg_base, offset) asm_m68k_st_reg_reg_ofst((as), (reg_src), (reg_base), (offset))
#define ASM_STORE8_REG_REG(as, reg_src, reg_base) asm_m68k_st8_reg_reg((as), (reg_src), (reg_base))
#define ASM_STORE16_REG_REG(as, reg_src, reg_base) asm_m68k_st16_reg_reg((as), (reg_src), (reg_base))
#define ASM_STORE32_REG_REG(as, reg_src, reg_base) asm_m68k_st32_reg_reg((as), (reg_src), (reg_base))

#endif // GENERIC_ASM_API

#endif // MICROPY_INCLUDED_PY_ASMM68K_H
