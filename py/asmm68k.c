/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 Yuichi Nakamura
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

#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "py/mpconfig.h"
#include "py/misc.h"

#if MICROPY_DEBUG_VERBOSE // print debugging info
#define DEBUG_PRINT (1)
#define DEBUG_printf DEBUG_printf
#else // don't print debugging info
#define DEBUG_printf(...)
#endif

// wrapper around everything in this file
#if MICROPY_EMIT_M68K || MICROPY_EMIT_INLINE_M68K

#include "py/mpstate.h"
#include "py/asmm68k.h"

static inline byte *asm_m68k_get_cur_to_write_bytes(asm_m68k_t *as, int n) {
    return mp_asm_base_get_cur_to_write_bytes(&as->base, n);
}

void asm_m68k_entry(asm_m68k_t *as, int num_locals) {
    DEBUG_printf("locals=%d\n", num_locals);
    assert(num_locals >= 0 && num_locals <= (0x8000 / 4));
    as->stack_adjust = num_locals * 4;
    asm_m68k_op16(as, 0x4e56);  //  link    a6,#0
    asm_m68k_op16(as, -as->stack_adjust);
    asm_m68k_op16(as, 0x48e7);  //  movem.l d2-d7/a2-a5,-(sp)
    asm_m68k_op16(as, 0x3f3c);
}

void asm_m68k_exit(asm_m68k_t *as) {
    asm_m68k_op16(as, 0x4cdf);  //  movem.l (sp)+,d2-d7/a2-a5
    asm_m68k_op16(as, 0x3cfc);
    asm_m68k_op16(as, 0x4e5e);  //  unlk    a6
    asm_m68k_op16(as, 0x4e75);  //  rts
}

STATIC mp_uint_t get_label_dest(asm_m68k_t *as, mp_uint_t label) {
    assert(label < as->base.max_num_labels);
    return as->base.label_offsets[label];
}

void asm_m68k_op16(asm_m68k_t *as, uint op) {
    byte *c = asm_m68k_get_cur_to_write_bytes(as, 2);
    if (c != NULL) {
        // big endian
        c[0] = op >> 8;
        c[1] = op;
        DEBUG_printf("emit: %04x\n", op & 0xffff);
    }
}

void asm_m68k_op32(asm_m68k_t *as, uint32_t op) {
    byte *c = asm_m68k_get_cur_to_write_bytes(as, 4);
    if (c != NULL) {
        // big endian
        c[0] = op >> 24;
        c[1] = op >> 16;
        c[2] = op >> 8;
        c[3] = op;
        DEBUG_printf("emit: %08x\n", op);
    }
}

void asm_m68k_mov_arg_to_r32(asm_m68k_t *as, int src_arg_num, int rd) {
    DEBUG_printf("ASM_MOV_ARG([%d]->r%d)\n", src_arg_num, rd);
    asm_m68k_op_move(as, 0x2000, rd, ASM_M68K_FP_DSP);  // move.l xxxx(fp),rd
    asm_m68k_op16(as, (src_arg_num + 2) * 4);
}

void asm_m68k_mov_args_to_r32(asm_m68k_t *as, int src_arg_num, uint reglist) {
    DEBUG_printf("ASM_MOV_ARGS([%d]->{0x%04x})\n", src_arg_num, reglist);
    asm_m68k_op_ea(as, 0x4cc0, ASM_M68K_FP_DSP);        // movem.l xxxx(fp),<reglist>
    asm_m68k_op16(as, reglist);
    asm_m68k_op16(as, (src_arg_num + 2) * 4);
}

void asm_m68k_cmp_reg_reg_setcc(asm_m68k_t *as, uint rd, uint rs, uint cond, uint rr) {
    DEBUG_printf("ASM_CMP_REG_REG(%d r%d r%d -> r%d)\n", cond, rd, rs, rr);
    asm_m68k_op_reg_imm8(as, 0x7000, rr, 0);    // moveq.l #0,rr
    asm_m68k_op_regea(as, 0xb080, rd, rs);      // cmp.l rs,rd
    asm_m68k_op_cc(as, 0x50c0, cond, rr);       // scc rr
}

STATIC void asm_m68k_jump_cond(asm_m68k_t *as, uint label, uint op) {
    mp_uint_t dest = get_label_dest(as, label);
    mp_int_t rel = dest - as->base.code_offset;
    if ((dest != (mp_uint_t)-1) && (rel <= 0) && ((rel - 2) >= -0x80)) {
        asm_m68k_op16(as, op | ((rel - 2) & 0xff)); // bcc.s <label>
    } else {
        assert((dest == (mp_uint_t)-1) || ((rel - 2) < 0x7fff && (rel - 2) >= -0x8000));
        asm_m68k_op16(as, op);                      // bcc.w <label>
        asm_m68k_op16(as, rel - 2);
    }
}

void asm_m68k_jump(asm_m68k_t *as, uint label) {
    DEBUG_printf("ASM_JUMP(%d)\n", label);
    asm_m68k_jump_cond(as, label, 0x6000);          // bra <label>
}

void asm_m68k_jmp_if_reg_zero(asm_m68k_t *as, uint reg, uint label, uint bool_test) {
    DEBUG_printf("ASM_JUMP_IF_REG_ZERO(r%d, %d, %d)\n", reg, label, bool_test);
    asm_m68k_op_ea(as, 0x4a80, reg);     // tst.l rn
    asm_m68k_jump_cond(as, label, 0x6700);          // beq <label>
}

void asm_m68k_jmp_if_reg_nonzero(asm_m68k_t *as, uint reg, uint label, uint bool_test) {
    DEBUG_printf("ASM_JUMP_IF_REG_NONZERO(r%d, %d, %d)\n", reg, label, bool_test);
    asm_m68k_op_ea(as, 0x4a80, reg);                // tst.l rn
    asm_m68k_jump_cond(as, label, 0x6600);          // bne <label>
}

void asm_m68k_jmp_if_reg_eq(asm_m68k_t *as, uint reg1, uint reg2, uint label) {
    DEBUG_printf("ASM_JUMP_IF_REG_EQ(r%d, r%d, %d)\n", reg1, reg2, label);
    asm_m68k_op_regea(as, 0xb080, reg1, reg2);      // cmp.l reg2,reg1
    asm_m68k_jump_cond(as, label, 0x6700);          // beq <label>
}

void asm_m68k_jmp_reg(asm_m68k_t *as, uint reg) {
    DEBUG_printf("ASM_JUMP_REG(r%d)\n", reg);
    asm_m68k_op_move(as, 0x2000, ASM_M68K_REG_AT, reg);   // movea.l reg,a0
    asm_m68k_op_ea(as, 0x4ec0, ASM_M68K_AT_IND);      // jmp (a0)
}

void asm_m68k_call_ind(asm_m68k_t *as, uint idx, uint n_args) {
    DEBUG_printf("ASM_CALL_IND(%d %d)\n", idx, n_args);
    switch (n_args) {
        case 1:
            asm_m68k_op16(as, 0x2f00);      // move.l d0,-(sp)
            break;
        case 2:
            asm_m68k_op32(as, 0x48e7c000);  // movem.l d0-d1,-(sp)
            break;
        case 3:
            asm_m68k_op32(as, 0x48e7e000);  // movem.l d0-d2,-(sp)
            break;
        case 4:
            asm_m68k_op32(as, 0x48e7f000);  // movem.l d0-d3,-(sp)
            break;
    }
    asm_m68k_op_move(as, 0x2000, ASM_M68K_REG_AT, ASM_M68K_DSP(ASM_M68K_REG_FUN_TABLE)); // movea.l idx*4(a5),a0
    asm_m68k_op16(as, idx * 4);
    asm_m68k_op_ea(as, 0x4e80, ASM_M68K_AT_IND);    // jsr (a0)
    if (n_args > 0 && n_args <= 2) {
        asm_m68k_op_regea(as, 0x5080, n_args * 4, ASM_M68K_REG_SP); // addq.l #xx,sp
    } else if (n_args > 2) {
        asm_m68k_op_regea(as, 0x41c0, ASM_M68K_REG_SP, ASM_M68K_DSP(ASM_M68K_REG_SP));  // lea.l xxxx(sp),sp
        asm_m68k_op16(as, n_args * 4);
    }
}

void asm_m68k_mov_reg_pcrel(asm_m68k_t *as, uint rd, uint label) {
    DEBUG_printf("ASM_MOV_REG_PCREL(r%d<-rel(%d))\n", rd, label);
    mp_uint_t dest = get_label_dest(as, label);
    mp_int_t rel = dest - as->base.code_offset -2;
    asm_m68k_op_regea(as, 0x41c0, ASM_M68K_REG_AT, ASM_M68K_PCDSP); // lea.l <label>(pc),a0
    asm_m68k_op16(as, rel);
    asm_m68k_op_move(as, 0x2000, rd, ASM_M68K_REG_AT);              // move.l a0,rd
}

void asm_m68k_mov_reg_imm(asm_m68k_t *as, uint rd, int imm) {
    DEBUG_printf("ASM_MOV_REG_IMM(r%d<-#0x%x)\n", rd, imm);
    if (imm <= 0x7f && imm >= -0x80) {
        asm_m68k_op_reg_imm8(as, 0x7000, rd, imm);      // moveq.l #xx,rd
    } else {
        asm_m68k_op_move(as, 0x2000, rd, ASM_M68K_IMM); // move.l #xxxx,rd
        asm_m68k_op32(as, imm);
    }
}

void asm_m68k_mov_local_reg(asm_m68k_t *as, int local_num, uint rs) {
    DEBUG_printf("ASM_MOV_LOCAL_REG([sp+%d]<-r%d)\n", local_num, rs);
    asm_m68k_op_move(as, 0x2000, ASM_M68K_FP_DSP, rs);  // move.l rs,xxxx(fp)
    asm_m68k_op16(as, (local_num * 4) - as->stack_adjust);
}

void asm_m68k_mov_reg_local(asm_m68k_t *as, uint rd, int local_num) {
    DEBUG_printf("ASM_MOV_REG_LOCAL(r%d<-[sp+%d])\n", rd, local_num);
    asm_m68k_op_move(as, 0x2000, rd, ASM_M68K_FP_DSP);  // move.l xxxx(fp),rd
    asm_m68k_op16(as, (local_num * 4) - as->stack_adjust);
}

void asm_m68k_mov_reg_local_addr(asm_m68k_t *as, uint rd, int local_num) {
    DEBUG_printf("ASM_MOV_REG_LOCAL_ADDR(r%d<-sp+%d)\n", rd, local_num);

    asm_m68k_op_regea(as, 0x41c0, ASM_M68K_REG_AT, ASM_M68K_FP_DSP);    // lea.l xxxx(fp),a0
    asm_m68k_op16(as, (local_num * 4) - as->stack_adjust);
    asm_m68k_op_move(as, 0x2000, rd, ASM_M68K_REG_AT);                  // move.l a0,rd
}

void asm_m68k_mov_reg_reg(asm_m68k_t *as, uint rd, uint rs) {
    DEBUG_printf("ASM_MOV_REG_REG(r%d<-r%d)\n", rd, rs);
    asm_m68k_op_move(as, 0x2000, rd, rs);               // move.l rs,rd
}

void asm_m68k_lsl_reg_reg(asm_m68k_t *as, uint rd, uint rshift) {
    DEBUG_printf("ASM_LSL_REG_REG(r%d<-r%d)\n", rd, rshift);
    asm_m68k_op_regea(as, 0xe1a8, rshift, rd);          // lsl.l rshift,rd
}
void asm_m68k_lsr_reg_reg(asm_m68k_t *as, uint rd, uint rshift) {
    DEBUG_printf("ASM_LSR_REG_REG(r%d<-r%d)\n", rd, rshift);
    asm_m68k_op_regea(as, 0xe0a8, rshift, rd);          // lsr.l rshift,rd
}
void asm_m68k_asr_reg_reg(asm_m68k_t *as, uint rd, uint rshift) {
    DEBUG_printf("ASM_ASR_REG_REG(%d<-%d)\n", rd, rshift);
    asm_m68k_op_regea(as, 0xe0a0, rshift, rd);          // asr.l rshift,rd
}
void asm_m68k_or_reg_reg(asm_m68k_t *as, uint rd, uint rs) {
    DEBUG_printf("ASM_OR_REG_REG(r%d<-r%d)\n", rd, rs);
    asm_m68k_op_regea(as, 0x8080, rd, rs);              // or.l rs,rd
}
void asm_m68k_xor_reg_reg(asm_m68k_t *as, uint rd, uint rs) {
    DEBUG_printf("ASM_XOR_REG_REG(r%d<-r%d)\n", rd, rs);
    asm_m68k_op_regea(as, 0xb180, rs, rd);              // eor.l rs,rd
}
void asm_m68k_and_reg_reg(asm_m68k_t *as, uint rd, uint rs) {
    DEBUG_printf("ASM_AND_REG_REG(r%d<-r%d)\n", rd, rs);
    asm_m68k_op_regea(as, 0xc080, rd, rs);              // and.l rs,rd
}
void asm_m68k_add_reg_reg(asm_m68k_t *as, uint rd, uint rs) {
    DEBUG_printf("ASM_ADD_REG_REG(r%d<-r%d)\n", rd, rs);
    asm_m68k_op_regea(as, 0xd080, rd, rs);              // add.l rs,rd
}
void asm_m68k_sub_reg_reg(asm_m68k_t *as, uint rd, uint rs) {
    DEBUG_printf("ASM_SUB_REG_REG(r%d<-r%d)\n", rd, rs);
    asm_m68k_op_regea(as, 0x9080, rd, rs);              // sub.l rs,rd
}

#if !MICROPY_SMALL_INT_MUL_HELPER
void asm_m68k_mul_reg_reg(asm_m68k_t *as, uint rd, uint rs) {
    DEBUG_printf("ASM_MUL_REG_REG(r%d<-r%d)\n", rd, rs);
    asm_m68k_op16(as, 0x518f);                      // subq.l #8,sp
    asm_m68k_op16(as, 0x2e80 | (rd));               // move.l rd,(sp)
    asm_m68k_op16(as, 0x2f40 | (rs));               // move.l rs,4(sp)
    asm_m68k_op16(as, 0x0004);
    asm_m68k_op16(as, 0xc0ef | (rd << 9));          // mulu.w 4(sp),rd
    asm_m68k_op16(as, 0x0004);
    asm_m68k_op16(as, 0xc0d7 | (rs << 9));          // mulu.w (sp),rs
    asm_m68k_op16(as, 0xd040 | (rd << 9) | (rs));   // add.w rs,rd
    asm_m68k_op16(as, 0x4840 | (rd));               // swap rd
    asm_m68k_op16(as, 0x4240 | (rd));               // clr.w rd
    asm_m68k_op16(as, 0x302f | (rs << 9));          // move.w 2(sp),rs
    asm_m68k_op16(as, 0x0002);
    asm_m68k_op16(as, 0xc0ef | (rs << 9));          // mulu.w 6(sp),rs
    asm_m68k_op16(as, 0x0006);
    asm_m68k_op16(as, 0xd080 | (rd << 9) | (rs));   // add.l rs,rd
    asm_m68k_op16(as, 0x508f);                      // addq.l #8,sp
}
#endif

void asm_m68k_ld_reg_reg(asm_m68k_t *as, uint rd, uint rbase) {
    DEBUG_printf("ASM_LOAD_REG_REG(r%d<-[r%d])\n", rd, rbase);
    if (ASM_M68K_IS_AREG(rbase)) {
        asm_m68k_op_move(as, 0x2000, rd, ASM_M68K_IND(rbase));  // move.l (rbase),rd
    } else {
        asm_m68k_op_move(as, 0x2000, ASM_M68K_REG_AT, rbase);   // movea.l rbase,a0
        asm_m68k_op_move(as, 0x2000, rd, ASM_M68K_AT_IND);      // move.l (a0),rd
    }
}

void asm_m68k_ld_reg_reg_ofst(asm_m68k_t *as, uint rd, uint rbase, uint word_offset) {
    if (word_offset == 0) {
        asm_m68k_ld_reg_reg(as, rd, rbase);
        return;
    }
    DEBUG_printf("ASM_LOAD_REG_REG_OFFSET(r%d<-[r%d+%d])\n", rd, rbase, word_offset);
    if (ASM_M68K_IS_AREG(rbase)) {
        asm_m68k_op_move(as, 0x2000, rd, ASM_M68K_DSP(rbase));  // move.l xxxx(rbase),rd
    } else {
        asm_m68k_op_move(as, 0x2000, ASM_M68K_REG_AT, rbase);   // movea.l rbase,a0
        asm_m68k_op_move(as, 0x2000, rd, ASM_M68K_AT_DSP);      // move.l xxxx(a0),rd
    }
    asm_m68k_op16(as, word_offset * 4);
}

void asm_m68k_ld8_reg_reg(asm_m68k_t *as, uint rd, uint rbase) {
    DEBUG_printf("ASM_LOAD8_REG_REG(r%d<-[r%d])\n", rd, rbase);
    asm_m68k_op_move(as, 0x2000, ASM_M68K_REG_AT, rbase);   // movea.l rbase,a0
    asm_m68k_op_move(as, 0x1000, rd, ASM_M68K_AT_IND);      // move.b (a0),rd
    asm_m68k_op_ea(as, 0x4880, rd);                         // ext.w rd
    asm_m68k_op_ea(as, 0x48c0, rd);                         // ext.l rd
}

void asm_m68k_ld16_reg_reg(asm_m68k_t *as, uint rd, uint rbase) {
    DEBUG_printf("ASM_LOAD16_REG_REG(r%d<-[r%d])\n", rd, rbase);
    asm_m68k_op_move(as, 0x2000, ASM_M68K_REG_AT, rbase);   // movea.l rbase,a0
    asm_m68k_op_move(as, 0x3000, rd, ASM_M68K_AT_IND);      // move.w (a0),rd
    asm_m68k_op_ea(as, 0x48c0, rd);                         // ext.l rd
}

void asm_m68k_ld16_reg_reg_ofst(asm_m68k_t *as, uint rd, uint rbase, uint uint16_offset) {
    DEBUG_printf("ASM_LOAD16_REG_REG_OFFSET(r%d<-[r%d+%d])\n", rd, rbase, uint16_offset);
    asm_m68k_op_move(as, 0x2000, ASM_M68K_REG_AT, rbase);   // movea.l rbase,a0
    asm_m68k_op_move(as, 0x3000, rd, ASM_M68K_AT_DSP);      // move.w xxxx(a0),rd
    asm_m68k_op16(as, uint16_offset * 2);
    asm_m68k_op_ea(as, 0x48c0, rd);                         // ext.l rd
}

void asm_m68k_ld32_reg_reg(asm_m68k_t *as, uint rd, uint rbase) {
    DEBUG_printf("ASM_LOAD32_REG_REG(r%d<-[r%d])\n", rd, rbase);
    asm_m68k_op_move(as, 0x2000, ASM_M68K_REG_AT, rbase);   // movea.l rbase,a0
    asm_m68k_op_move(as, 0x2000, rd, ASM_M68K_AT_IND);      // move.l (a0),rd
}


void asm_m68k_st_reg_reg(asm_m68k_t *as, uint rs, uint rbase) {
    DEBUG_printf("ASM_STORE_REG_REG(r%d->[r%d])\n", rs, rbase);
    asm_m68k_op_move(as, 0x2000, ASM_M68K_REG_AT, rbase);   // movea.l rbase,a0
    asm_m68k_op_move(as, 0x2000, ASM_M68K_AT_IND, rs);      // move.l rs,(a0)
}

void asm_m68k_st_reg_reg_ofst(asm_m68k_t *as, uint rs, uint rbase, uint word_offset) {
    DEBUG_printf("ASM_STORE_REG_REG_OFFSET(r%d->[r%d+%d])\n", rs, rbase, word_offset);
    asm_m68k_op_move(as, 0x2000, ASM_M68K_REG_AT, rbase);   // movea.l rbase,a0
    asm_m68k_op_move(as, 0x2000, ASM_M68K_AT_DSP, rs);      // move.l rs,xxxx(a0)
    asm_m68k_op16(as, word_offset * 4);
}

void asm_m68k_st8_reg_reg(asm_m68k_t *as, uint rs, uint rbase) {
    DEBUG_printf("ASM_STORE8_REG_REG(r%d->[r%d])\n", rs, rbase);
    asm_m68k_op_move(as, 0x2000, ASM_M68K_REG_AT, rbase);   // movea.l rbase,a0
    asm_m68k_op_move(as, 0x1000, ASM_M68K_AT_IND, rs);      // move.b rs,(a0)
}

void asm_m68k_st16_reg_reg(asm_m68k_t *as, uint rs, uint rbase) {
    DEBUG_printf("ASM_STORE16_REG_REG(r%d->[r%d])\n", rs, rbase);
    asm_m68k_op_move(as, 0x2000, ASM_M68K_REG_AT, rbase);   // movea.l rbase,a0
    asm_m68k_op_move(as, 0x3000, ASM_M68K_AT_IND, rs);      // move.w rs,(a0)
}

void asm_m68k_st32_reg_reg(asm_m68k_t *as, uint rs, uint rbase) {
    DEBUG_printf("ASM_STORE32_REG_REG(r%d->[r%d])\n", rs, rbase);
    asm_m68k_op_move(as, 0x2000, ASM_M68K_REG_AT, rbase);   // movea.l rbase,a0
    asm_m68k_op_move(as, 0x2000, ASM_M68K_AT_IND, rs);      // move.l rs,(a0)
}

#endif // MICROPY_EMIT_M68K || MICROPY_EMIT_INLINE_M68K
