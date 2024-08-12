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

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

#include "py/emit.h"
#include "py/asmm68k.h"

#if MICROPY_EMIT_INLINE_M68K

typedef enum {
// define rules with a compile function
#define DEF_RULE(rule, comp, kind, ...) PN_##rule,
#define DEF_RULE_NC(rule, kind, ...)
    #include "py/grammar.h"
#undef DEF_RULE
#undef DEF_RULE_NC
    PN_const_object, // special node for a constant, generic Python object
// define rules without a compile function
#define DEF_RULE(rule, comp, kind, ...)
#define DEF_RULE_NC(rule, kind, ...) PN_##rule,
    #include "py/grammar.h"
#undef DEF_RULE
#undef DEF_RULE_NC
} pn_kind_t;

struct _emit_inline_asm_t {
    asm_m68k_t as;
    uint16_t pass;
    mp_obj_t *error_slot;
    mp_uint_t max_num_labels;
    qstr *label_lookup;
};

STATIC void emit_inline_m68k_error_msg(emit_inline_asm_t *emit, mp_rom_error_text_t msg) {
    if (*emit->error_slot == MP_OBJ_NULL) {
        *emit->error_slot = mp_obj_new_exception_msg(&mp_type_SyntaxError, msg);
    }
}

STATIC void emit_inline_m68k_error_exc(emit_inline_asm_t *emit, mp_obj_t exc) {
    if (*emit->error_slot == MP_OBJ_NULL) {
        *emit->error_slot = exc;
    }
}

emit_inline_asm_t *emit_inline_m68k_new(mp_uint_t max_num_labels) {
    emit_inline_asm_t *emit = m_new_obj(emit_inline_asm_t);
    memset(&emit->as, 0, sizeof(emit->as));
    emit->as.base.endian = true;            /* big endian */
    mp_asm_base_init(&emit->as.base, max_num_labels);
    emit->max_num_labels = max_num_labels;
    emit->label_lookup = m_new(qstr, max_num_labels);
    return emit;
}

void emit_inline_m68k_free(emit_inline_asm_t *emit) {
    m_del(qstr, emit->label_lookup, emit->max_num_labels);
    mp_asm_base_deinit(&emit->as.base, false);
    m_del_obj(emit_inline_asm_t, emit);
}

STATIC void emit_inline_m68k_start_pass(emit_inline_asm_t *emit, pass_kind_t pass, mp_obj_t *error_slot) {
    emit->pass = pass;
    emit->error_slot = error_slot;
    if (emit->pass == MP_PASS_CODE_SIZE) {
        memset(emit->label_lookup, 0, emit->max_num_labels * sizeof(qstr));
    }
    mp_asm_base_start_pass(&emit->as.base, pass == MP_PASS_EMIT ? MP_ASM_PASS_EMIT : MP_ASM_PASS_COMPUTE);
    asm_m68k_entry(&emit->as, 0);
}

STATIC void emit_inline_m68k_end_pass(emit_inline_asm_t *emit, mp_uint_t type_sig) {
    asm_m68k_exit(&emit->as);
    asm_m68k_end_pass(&emit->as);
}

STATIC mp_uint_t emit_inline_m68k_count_params(emit_inline_asm_t *emit, mp_uint_t n_params, mp_parse_node_t *pn_params) {
    return n_params;
}

STATIC bool emit_inline_m68k_label(emit_inline_asm_t *emit, mp_uint_t label_num, qstr label_id) {
    assert(label_num < emit->max_num_labels);
    if (emit->pass == MP_PASS_CODE_SIZE) {
        // check for duplicate label on first pass
        for (uint i = 0; i < emit->max_num_labels; i++) {
            if (emit->label_lookup[i] == label_id) {
                return false;
            }
        }
    }
    emit->label_lookup[label_num] = label_id;
    mp_asm_base_label_assign(&emit->as.base, label_num);
    return true;
}

/****************************************************************************/

typedef struct _reg_name_t { byte reg;
                             qstr name; }
reg_name_t;
STATIC const reg_name_t reg_name_table[] = {
    {0,  MP_QSTR_d0},
    {1,  MP_QSTR_d1},
    {2,  MP_QSTR_d2},
    {3,  MP_QSTR_d3},
    {4,  MP_QSTR_d4},
    {5,  MP_QSTR_d5},
    {6,  MP_QSTR_d6},
    {7,  MP_QSTR_d7},

    {8,  MP_QSTR_a0},
    {9,  MP_QSTR_a1},
    {10, MP_QSTR_a2},
    {11, MP_QSTR_a3},
    {12, MP_QSTR_a4},
    {13, MP_QSTR_a5},
    {14, MP_QSTR_a6},
    {15, MP_QSTR_a7},
    {14, MP_QSTR_fp},
    {15, MP_QSTR_sp},

    {16, MP_QSTR_pc},

    {17, MP_QSTR_ccr},
    {18, MP_QSTR_sr},
    {19, MP_QSTR_usp},
};

STATIC mp_uint_t get_reg(mp_parse_node_t pn) {
    for (mp_uint_t i = 0; i < MP_ARRAY_SIZE(reg_name_table); i++) {
        qstr qst = MP_PARSE_NODE_LEAF_ARG(pn);
        if (qst == reg_name_table[i].name) {
            return reg_name_table[i].reg;
        }
    }
    return -1;
}

typedef struct _cc_name_t { byte cc;
                            const char *name; }
cc_name_t;
STATIC const cc_name_t cc_name_table[] = {
    { 0x0, "t" },
    { 0x1, "f" },
    { 0x2, "hi" },
    { 0x3, "ls" },
    { 0x4, "cc" },
    { 0x5, "cs" },
    { 0x4, "hs" },
    { 0x5, "lo" },
    { 0x6, "ne" },
    { 0x7, "eq" },
    { 0x6, "nz" },
    { 0x7, "ze" },
    { 0x8, "vc" },
    { 0x9, "vs" },
    { 0xa, "pl" },
    { 0xb, "mi" },
    { 0xc, "ge" },
    { 0xd, "lt" },
    { 0xe, "gt" },
    { 0xf, "le" },
};

STATIC mp_uint_t get_cc(const char **strp) {
    for (mp_uint_t i = 0; i < MP_ARRAY_SIZE(cc_name_table); i++) {
        const char *cc = cc_name_table[i].name;
        int len = strlen(cc);
        if (strncmp(*strp, cc, len) == 0) {
            *strp += len;
            return cc_name_table[i].cc;
        }
    }
    return -1;
}

STATIC const char *get_arg_str(mp_parse_node_t pn) {
    if (MP_PARSE_NODE_IS_ID(pn)) {
        qstr qst = MP_PARSE_NODE_LEAF_ARG(pn);
        return qstr_str(qst);
    } else {
        return "";
    }
}

STATIC int get_arg_label(emit_inline_asm_t *emit, const char *op, mp_parse_node_t pn) {
    if (!MP_PARSE_NODE_IS_ID(pn)) {
        emit_inline_m68k_error_exc(emit, mp_obj_new_exception_msg_varg(&mp_type_SyntaxError, MP_ERROR_TEXT("'%s' expects a label"), op));
        return 0;
    }
    qstr label_qstr = MP_PARSE_NODE_LEAF_ARG(pn);
    for (uint i = 0; i < emit->max_num_labels; i++) {
        if (emit->label_lookup[i] == label_qstr) {
            return i;
        }
    }
    // only need to have the labels on the last pass
    if (emit->pass == MP_PASS_EMIT) {
        emit_inline_m68k_error_exc(emit, mp_obj_new_exception_msg_varg(&mp_type_SyntaxError, MP_ERROR_TEXT("label '%q' not defined"), label_qstr));
    }
    return 0;
}

STATIC void check_value_range(emit_inline_asm_t *emit, uint32_t data, int32_t min, int32_t max) {
    if (!((int32_t)data >= min && (int32_t)data <= max)) {
        emit_inline_m68k_error_msg(emit, MP_ERROR_TEXT("illegal value range"));
    }
}

STATIC void check_byte_range(emit_inline_asm_t *emit, uint32_t data) {
    check_value_range(emit, data, -0x80, 0xff);
}

STATIC void check_word_range(emit_inline_asm_t *emit, uint32_t data) {
    check_value_range(emit, data, -0x8000, 0xffff);
}

STATIC void check_sbyte_range(emit_inline_asm_t *emit, uint32_t data) {
    check_value_range(emit, data, -0x80, 0x7f);
}

STATIC void check_sword_range(emit_inline_asm_t *emit, uint32_t data) {
    check_value_range(emit, data, -0x8000, 0x7fff);
}

/****************************************************************************/

/* Effective address elements */
typedef struct _ea_elem_t {
    uint16_t flag;      /* Element bit flag */
    uint8_t  reg;       /* Register number (Dn/An/Special regs.) */
    uint8_t  areg;      /* Address register number */
    uint8_t  size;      /* Data size */
    uint32_t value;     /* Immediate value / label number / register list */
} ea_elem_t;

/* ea_elem flag bits */
#define EL_REG          (1 << 0)    /* reg is used */
#define EL_AREG         (1 << 1)    /* areg is used */
#define EL_SIZE         (1 << 2)    /* size is used */
#define EL_VALUE        (1 << 3)    /* value is used (disp. or imm.)*/
#define EL_POSTINC      (1 << 4)    /* post-increment : (An)+ */
#define EL_PREDEC       (1 << 5)    /* pre-decrement  : -(An) */
#define EL_INDIRECT     (1 << 6)    /* indirect mode is used : (An) etc. */
#define EL_LABEL        (1 << 7)    /* value is label number */
#define EL_SPREG        (1 << 8)    /* special register (CCR/SR/USP) */
#define EL_REGLIST      (1 << 9)    /* value is register list */

int get_reglist(emit_inline_asm_t *emit, mp_parse_node_t pn, ea_elem_t *ea) {
    int i;
    int r = -1;
    if (MP_PARSE_NODE_IS_NULL(pn)) {
        return -1;
    } else if (MP_PARSE_NODE_IS_ID(pn)) {
        if ((r = get_reg(pn)) < 16) {       /* Dn or An */
            if (ea->areg) {                 /* Rn-Rm */
                if (ea->reg >= r) {
                    return -1;              /* bad register order */
                }
                for (i = ea->reg; i <= r; i++) {
                    ea->value |= (1 << i);
                }
                ea->areg = 0;
                return 0;
            }
            ea->reg = r;                    /* Rn */
            ea->value |= (1 << r);
            return 0;
        } else {
            return -1;
        }
    } else if (MP_PARSE_NODE_IS_TOKEN(pn)) {
        if (MP_PARSE_NODE_IS_TOKEN_KIND(pn, MP_TOKEN_OP_MINUS)) {
            ea->areg = 1;                   /* Rn-Rm */
            return 0;
        } else if (MP_PARSE_NODE_IS_TOKEN_KIND(pn, MP_TOKEN_OP_SLASH)) {
            return 0;                       /* '/' (just ignored) */
        }
        return -1;
    } else if (MP_PARSE_NODE_IS_STRUCT(pn)) {
        mp_parse_node_struct_t *pns = (mp_parse_node_struct_t *)pn;
        int n = MP_PARSE_NODE_STRUCT_NUM_NODES(pns);
        for (i = 0; i < n; i++) {
            r = get_reglist(emit, pns->nodes[i], ea);
            if (r < 0) {
                break;
            }
        }
        return r;
    }
    return -1;
}

/* Get effective address elements from mp_parse_node_t */
int get_ea_elem(emit_inline_asm_t *emit, mp_parse_node_t pn, ea_elem_t *ea) {
    int i;
    int r = -1;
    if (MP_PARSE_NODE_IS_NULL(pn)) {
        return -1;
    } else if (MP_PARSE_NODE_IS_SMALL_INT(pn)) {        /* immediate value */
        if (!(ea->flag & EL_VALUE)) {
            ea->value = MP_PARSE_NODE_LEAF_SMALL_INT(pn);
            ea->flag |= EL_VALUE;
            return 0;
        }
    } else if (MP_PARSE_NODE_IS_ID(pn)) {
        if ((r = get_reg(pn)) < 0) {                    /* not a register */
            const char *reg_str = get_arg_str(pn);
            ea->flag |= EL_LABEL;
            ea->value = get_arg_label(emit, reg_str, pn);
            return 0;
        } else if (r < 8) {                     /* Dn */
            if (!(ea->flag & EL_REG)) {
                ea->reg = r;
                ea->flag |= EL_REG;
                return 0;
            }
        } else if (r < 16) {                    /* An */
            if (!(ea->flag & EL_AREG)) {
                ea->areg = r - 8;
                ea->flag |= EL_AREG;
                return 0;
            } else if (!(ea->flag & EL_REG)) {
                ea->reg = r;
                ea->flag |= EL_REG;
                return 0;
            }
        } else if (r == 16) {                   /* PC */
            if (!(ea->flag & EL_AREG)) {
                ea->areg = 8;
                ea->flag |= EL_AREG;
                return 0;
            }
        } else {                                /* CCR/SR/USP */
            if (!(ea->flag & EL_REG)) {
                ea->reg = r;
                ea->flag |= EL_SPREG;
                return 0;
            }
        }
    } else if (MP_PARSE_NODE_IS_STRUCT(pn)) {
        mp_parse_node_struct_t *pns = (mp_parse_node_struct_t *)pn;
        int n = MP_PARSE_NODE_STRUCT_NUM_NODES(pns);
        if (!(ea->flag & EL_VALUE)) {
            mp_obj_t o;
            if (mp_parse_node_get_int_maybe(pn, &o)) {  /* immediate value */
                ea->value = mp_obj_get_int_truncated(o);
                ea->flag |= EL_VALUE;
                return 0;
            }
        }
        if (MP_PARSE_NODE_IS_STRUCT_KIND(pn, PN_atom_brace)) {  /* { ... } */
            if (ea->flag & EL_REGLIST) {
                return -1;
            }
            ea->flag |= EL_REGLIST;                 /* get register list */
            ea->value = 0;
            ea->areg = 0;
            for (i = 0; i < n; i++) {
                r = get_reglist(emit, pns->nodes[i], ea);
                if (r < 0) {
                    break;
                }
            }
            return r;
        }
        if (MP_PARSE_NODE_IS_STRUCT_KIND(pn, PN_trailer_period)) {  /* .XXX  */
            if (n == 1) {
                const char *reg_str = get_arg_str(pns->nodes[0]);
                if (reg_str != NULL) {
                    if (!(ea->flag & EL_SIZE)) {                    /* .w/.l */
                        if (strcmp(reg_str, "w") == 0 ||
                            strcmp(reg_str, "l") == 0) {
                            ea->size = (strcmp(reg_str, "l") == 0);
                            ea->flag |= EL_SIZE;
                            if (ea->flag & EL_AREG && !(ea->flag & EL_REG)) {
                                ea->flag |= EL_REG;
                                ea->flag &= ~EL_AREG;
                                ea->reg = ea->areg + 8;
                            }
                            return 0;
                        }
                    }
                    if (!(ea->flag & (EL_POSTINC|EL_PREDEC))) {     /* .inc/.dec */
                        if (strcmp(reg_str, "inc") == 0) {
                            ea->flag |= EL_POSTINC;
                            return 0;
                        } else if (strcmp(reg_str, "dec") == 0) {
                            ea->flag |= EL_PREDEC;
                            return 0;
                        }
                    }
                }
            }
        } else {
            if (MP_PARSE_NODE_IS_STRUCT_KIND(pn, PN_atom_bracket) ||    /* [ ... ] */
                MP_PARSE_NODE_IS_STRUCT_KIND(pn, PN_trailer_bracket)) {
                if (!(ea->flag & EL_INDIRECT)) {
                    ea->flag |= EL_INDIRECT;
                } else {
                    return -1;
                }
            }
            for (i = 0; i < n; i++) {
                r = get_ea_elem(emit, pns->nodes[i], ea);
                if (r < 0) {
                    break;
                }
            }
            return r;
        }
    }
    return -1;
}

/* Operand type */
#define OT_DREG         (1 << 0)    /* Data register direct */
#define OT_AREG         (1 << 1)    /* Address register direct */
#define OT_AIND         (1 << 2)    /* Address register indirect */
#define OT_AIINC        (1 << 3)    /* Address register indirect w/ post-increment */
#define OT_AIDEC        (1 << 4)    /* Address register indirect w/ pre-decrement */
#define OT_AIDSP        (1 << 5)    /* Address register indirect w/ displacement */
#define OT_AIIDX        (1 << 6)    /* Address register indirect w/ index */
#define OT_PIDSP        (1 << 7)    /* PC indirect w/ displacement */
#define OT_PIIDX        (1 << 8)    /* PC indirect w/ index */
#define OT_ABS          (1 << 9)    /* Absolute short/long */
#define OT_IMM          (1 << 10)   /* Immediate */
#define OT_SPREG        (1 << 11)   /* CCR/SR/USP */
#define OT_REGLIST      (1 << 12)   /* register list */
#define OT_LABEL        (1 << 13)   /* label */

#define OT_EA           (OT_DREG|OT_AREG|OT_AIND|OT_AIINC|OT_AIDEC|OT_AIDSP|OT_AIIDX|OT_PIDSP|OT_PIIDX|OT_ABS|OT_IMM)
#define OT_EAALT        (OT_DREG|OT_AREG|OT_AIND|OT_AIINC|OT_AIDEC|OT_AIDSP|OT_AIIDX|0       |0       |OT_ABS|0     )
#define OT_EADATA       (OT_DREG|0      |OT_AIND|OT_AIINC|OT_AIDEC|OT_AIDSP|OT_AIIDX|OT_PIDSP|OT_PIIDX|OT_ABS|OT_IMM)
#define OT_EADAT2       (OT_DREG|0      |OT_AIND|OT_AIINC|OT_AIDEC|OT_AIDSP|OT_AIIDX|OT_PIDSP|OT_PIIDX|OT_ABS|0     )
#define OT_EADALT       (OT_DREG|0      |OT_AIND|OT_AIINC|OT_AIDEC|OT_AIDSP|OT_AIIDX|0       |0       |OT_ABS|0     )
#define OT_EAMALT       (0      |0      |OT_AIND|OT_AIINC|OT_AIDEC|OT_AIDSP|OT_AIIDX|0       |0       |OT_ABS|0     )
#define OT_EACTL        (0      |0      |OT_AIND|0       |0       |OT_AIDSP|OT_AIIDX|OT_PIDSP|OT_PIIDX|OT_ABS|0     )
#define OT_EACALT       (0      |0      |OT_AIND|0       |0       |OT_AIDSP|OT_AIIDX|0       |0       |OT_ABS|0     )
#define OT_ANY          0xffff

/* Operand data type */
#define OR_NODATA       0       /* no data */
#define OR_WORD         1       /* word data */
#define OR_LONG         2       /* long word data */
#define OR_SIZE         3       /* depends on the opcode */

typedef struct _operand_t {
    uint16_t type;      /* Operand type */
    uint16_t ea;        /* Effective address field */
    byte reg;           /* Register no. */
    uint32_t data;      /* Disp./Imm./reglist/... */
} operand_t;

/* Get one operand */
int get_operand(emit_inline_asm_t *emit, mp_parse_node_t pn, operand_t *o, uint16_t mask) {
    ea_elem_t eas = { 0 };
    int res = -1;
    mp_uint_t dest;
    mp_int_t rel;

    /* Get effective address elements */    
    if (get_ea_elem(emit, pn, &eas) < 0) {
        return -1;
    }

    /* Get addressing mode from elements */
    switch (eas.flag) {
    case EL_REG:                            /* Rn */
        if (eas.reg < 8) {
            o->type = OT_DREG;              /* Data register direct */
            o->ea = (0x0 << 3) | eas.reg;
            o->reg = eas.reg;
            res = OR_NODATA;
        }
        break;
    case EL_AREG:                           /* An */
        if (eas.areg < 8) {
            o->type = OT_AREG;              /* Address register direct */
            o->ea = (0x1 << 3) | eas.areg;
            o->reg = eas.areg;
            res = OR_NODATA;
        }
        break;
    case EL_AREG|EL_INDIRECT:               /* (An) */
        if (eas.areg < 8) {
            o->type = OT_AIND;              /* Address register indirect */
            o->ea = (0x2 << 3) | eas.areg;
            o->reg = eas.areg;
            res = OR_NODATA;
        }
        break;
    case EL_AREG|EL_INDIRECT|EL_POSTINC:    /* (An)+ */
        if (eas.areg < 8) {
            o->type = OT_AIINC;             /* Address register indirect w/ post-increment */
            o->ea = (0x3 << 3) | eas.areg;
            o->reg = eas.areg;
            res = OR_NODATA;
        }
        break;
    case EL_AREG|EL_INDIRECT|EL_PREDEC:     /* -(An) */
        if (eas.areg < 8) {
            o->type = OT_AIDEC;             /* Address register indirect w/ pre-decrement */
            o->ea = (0x4 << 3) | eas.areg;
            o->reg = eas.areg;
            res = OR_NODATA;
        }
        break;
    case EL_AREG|EL_INDIRECT|EL_VALUE:
        if (eas.areg < 8) {                 /* (d16,An) */
            o->type = OT_AIDSP;             /* Address register indirect w/ displacement */
            o->ea = (0x5 << 3) | eas.areg;    
            o->reg = eas.areg;
        } else if (eas.areg == 8) {         /* (d16,PC) */
            o->type = OT_PIDSP;             /* PC indirect w/ displacement */
            o->ea = (0x7 << 3) | 0x2;     
        }
        o->data = eas.value;
        check_sword_range(emit, o->data);
        res = OR_WORD;
        break;
    case EL_AREG|EL_INDIRECT|EL_LABEL:      /* (label,PC) */
        if (eas.areg == 8) {                /* (d16,PC) */
            o->type = OT_PIDSP;             /* PC indirect w/ displacement */
            o->ea = (0x7 << 3) | 0x2;     
            dest = emit->as.base.label_offsets[eas.value];
            rel = dest - emit->as.base.code_offset;
            o->data = rel - 2;
            check_sword_range(emit, o->data);
            res = OR_WORD;
        }
        break;
    case EL_AREG|EL_REG|EL_INDIRECT|EL_SIZE:
    case EL_AREG|EL_REG|EL_INDIRECT|EL_VALUE|EL_SIZE:
        if (eas.areg < 8) {                 /* (d8,An,Rn.size) */
            o->type = OT_AIIDX;             /* Address register indirect w/ index */
            o->ea = (0x6 << 3) | eas.areg;    
            o->reg = eas.areg;
        } else if (eas.areg == 8) {         /* (d8,PC,Rn.size) */
            o->type = OT_PIIDX;             /* PC indirect w/ index */
            o->ea = (0x7 << 3) | 0x3;
        }
        check_sbyte_range(emit, eas.value);
        o->data = (eas.value & 0xff) | (eas.size << 11) | (eas.reg << 12);
        res = OR_WORD;
        break;
    case EL_AREG|EL_REG|EL_INDIRECT|EL_LABEL|EL_SIZE:
        if (eas.areg == 8) {
            o->type = OT_PIIDX;             /* PC indirect w/ index */
            o->ea = (0x7 << 3) | 0x3;
            dest = emit->as.base.label_offsets[eas.value];
            rel = dest - emit->as.base.code_offset;
            o->data = rel - 2;
            check_sbyte_range(emit, o->data);
            o->data = (o->data & 0xff) | (eas.size << 11) | (eas.reg << 12);
            res = OR_WORD;
        }
        break;
    case EL_INDIRECT|EL_VALUE|EL_SIZE:      /* (xxxx).size */
        if (eas.size == 0) {
            check_sword_range(emit, eas.value);
        }
        o->type = OT_ABS;                   /* Absolute short/long */
        o->ea = (0x7 << 3) | (eas.size ? 0x1 : 0x0);  
        o->data = eas.value;
        res = OR_WORD + eas.size;
        break;
    case EL_INDIRECT|EL_VALUE:              /* (xxxx) */
        o->type = OT_ABS;                   /* Absolute long */
        o->ea = (0x7 << 3) | 0x1;
        o->data = eas.value;
        res = OR_LONG;
        break;
    case EL_VALUE:                          /* #xxxx */
        o->type = OT_IMM;                   /* Immediate data */
        o->ea = (0x7 << 3) | 0x4;
        o->data = eas.value;
        res = OR_SIZE;
        break;
    case EL_LABEL:                          /* label */
        o->type = OT_LABEL;
        o->data = emit->as.base.label_offsets[eas.value];
        res = OR_LONG;
        break;
    case EL_SPREG:                          /* CCR/SR/USP */
        o->type = OT_SPREG;
        o->reg = eas.reg;
        res = OR_NODATA;
        break;
    case EL_REGLIST:                        /* register list */
        o->type = OT_REGLIST;
        o->data = eas.value;
        res = OR_NODATA;
        break;
    }

    if (!(o->type & mask)) {
        return -1;          /* Unavailable addressing mode */
    }

    return res;
}

/****************************************************************************/

/* Opcode size */
#define B       (1 << 0)
#define W       (1 << 1)
#define L       (1 << 2)
#define S       (1 << 3)
#define CC      (1 << 4)    /* with condition */

enum m68k_instr_type {
    IN_MOVE,
    IN_MOVEA,
    IN_MOVEQ,
    IN_ADDQ,
    IN_ADD,
    IN_EOR,
    IN_ADDI,
    IN_ADDA,
    IN_LEA,
    IN_EA,
    IN_BRA,
    IN_DBRA,
    IN_SCC,
    IN_CLR,
    IN_ROTSFT,
    IN_BCHG,
    IN_MULDIV,
    IN_ADDX,
    IN_REG,
    IN_LINK,
    IN_EXG,
    IN_EXT,
    IN_CMPM,
    IN_MOVEM,
    IN_MOVEP,
    IN_TRAP,
    IN_STOP,
    IN_NOOPR,
};

typedef struct _m68k_instr_table_t {
    const char *opcode;             /* opcode */
    uint16_t size;                  /* acceptable size */
    uint16_t instr;                 /* instruction code */
    enum m68k_instr_type type;      /* operand type */
    uint16_t oprtype1;              /* acceptable operand 1 */
    uint16_t oprtype2;              /* acceptable operand 2 */
} m68k_instr_table_t;
m68k_instr_table_t inst_table[] = {
    { "move",     L|W|B,  0x0000,   IN_MOVE,    OT_EA|OT_SPREG, OT_EAALT|OT_SPREG },
    { "movea",    L|W,    0x2040,   IN_MOVEA,   OT_EA,          OT_AREG },

    { "moveq",    L,      0x7000,   IN_MOVEQ,   OT_IMM,         OT_DREG },
    { "subq",     L|W|B,  0x5100,   IN_ADDQ,    OT_IMM,         OT_EAALT },
    { "addq",     L|W|B,  0x5000,   IN_ADDQ,    OT_IMM,         OT_EAALT },

    { "sub",      L|W|B,  0x9000,   IN_ADD,     OT_EA,          OT_EADALT },
    { "add",      L|W|B,  0xd000,   IN_ADD,     OT_EA,          OT_EADALT },
    { "cmp",      L|W|B,  0xb000,   IN_ADD,     OT_EA,          OT_DREG },
    { "or",       L|W|B,  0x8000,   IN_ADD,     OT_EADATA,      OT_EADALT },
    { "and",      L|W|B,  0xc000,   IN_ADD,     OT_EADATA,      OT_EADALT },
    { "eor",      L|W|B,  0xb100,   IN_EOR,     OT_DREG,        OT_EADALT },

    { "subi",     L|W|B,  0x0400,   IN_ADDI,    OT_IMM,         OT_EADALT },
    { "addi",     L|W|B,  0x0600,   IN_ADDI,    OT_IMM,         OT_EADALT },
    { "cmpi",     L|W|B,  0x0c00,   IN_ADDI,    OT_IMM,         OT_EADALT },
    { "ori",      L|W|B,  0x0000,   IN_ADDI,    OT_IMM,         OT_EADALT|OT_SPREG },
    { "andi",     L|W|B,  0x0200,   IN_ADDI,    OT_IMM,         OT_EADALT|OT_SPREG },
    { "eori",     L|W|B,  0x0a00,   IN_ADDI,    OT_IMM,         OT_EADALT|OT_SPREG },

    { "suba",     L|W,    0x90c0,   IN_ADDA,    OT_EA,          OT_AREG },
    { "adda",     L|W,    0xd0c0,   IN_ADDA,    OT_EA,          OT_AREG },
    { "cmpa",     L|W,    0xb0c0,   IN_ADDA,    OT_EA,          OT_AREG },

    { "lea",      L,      0x41c0,   IN_LEA,     OT_EACTL,       OT_AREG },
    { "pea",      L,      0x4840,   IN_EA,      OT_EACTL,       0 },
    { "jsr",      0,      0x4e80,   IN_EA,      OT_EACTL,       0 },
    { "jmp",      0,      0x4ec0,   IN_EA,      OT_EACTL,       0 },

    { "bra",      W|S,    0x6000,   IN_BRA,     OT_LABEL,       0 },
    { "bsr",      W|S,    0x6100,   IN_BRA,     OT_LABEL,       0 },
    { "b",        CC|W|S, 0x6000,   IN_BRA,     OT_LABEL,       0 },
    { "dbra",     W|S,    0x51c8,   IN_DBRA,    OT_DREG,        OT_LABEL },
    { "db",       CC|W|S, 0x50c8,   IN_DBRA,    OT_DREG,        OT_LABEL },
    { "s",        CC|B,   0x50c0,   IN_SCC,     OT_EADALT,      0 },

    { "clr",      L|W|B,  0x4200,   IN_CLR,     OT_EADALT,      0 },
    { "neg",      L|W|B,  0x4400,   IN_CLR,     OT_EADALT,      0 },
    { "negx",     L|W|B,  0x4000,   IN_CLR,     OT_EADALT,      0 },
    { "not",      L|W|B,  0x4600,   IN_CLR,     OT_EADALT,      0 },
    { "tst",      L|W|B,  0x4a00,   IN_CLR,     OT_EADALT,      0 },

    { "asr",      L|W|B,  0xe000,   IN_ROTSFT,  OT_DREG|OT_IMM|OT_EAMALT, OT_ANY },
    { "asl",      L|W|B,  0xe100,   IN_ROTSFT,  OT_DREG|OT_IMM|OT_EAMALT, OT_ANY },
    { "lsr",      L|W|B,  0xe008,   IN_ROTSFT,  OT_DREG|OT_IMM|OT_EAMALT, OT_ANY },
    { "lsl",      L|W|B,  0xe108,   IN_ROTSFT,  OT_DREG|OT_IMM|OT_EAMALT, OT_ANY },
    { "roxr",     L|W|B,  0xe010,   IN_ROTSFT,  OT_DREG|OT_IMM|OT_EAMALT, OT_ANY },
    { "roxl",     L|W|B,  0xe110,   IN_ROTSFT,  OT_DREG|OT_IMM|OT_EAMALT, OT_ANY },
    { "ror",      L|W|B,  0xe018,   IN_ROTSFT,  OT_DREG|OT_IMM|OT_EAMALT, OT_ANY },
    { "rol",      L|W|B,  0xe118,   IN_ROTSFT,  OT_DREG|OT_IMM|OT_EAMALT, OT_ANY },

    { "bchg",     L|B,    0x0040,   IN_BCHG,    OT_DREG|OT_IMM, OT_EADALT },
    { "bclr",     L|B,    0x0080,   IN_BCHG,    OT_DREG|OT_IMM, OT_EADALT },
    { "bset",     L|B,    0x00c0,   IN_BCHG,    OT_DREG|OT_IMM, OT_EADALT },
    { "btst",     L|B,    0x0000,   IN_BCHG,    OT_DREG|OT_IMM, OT_EADAT2 },

    { "mulu",     W,      0xc0c0,   IN_MULDIV,  OT_EADATA,      OT_DREG },
    { "muls",     W,      0xc1c0,   IN_MULDIV,  OT_EADATA,      OT_DREG },
    { "divu",     W,      0x80c0,   IN_MULDIV,  OT_EADATA,      OT_DREG },
    { "divs",     W,      0x81c0,   IN_MULDIV,  OT_EADATA,      OT_DREG },
    { "chk",      W,      0x4180,   IN_MULDIV,  OT_EADATA,      OT_DREG },

    { "subx",     L|W|B,  0x9100,   IN_ADDX,    OT_DREG|OT_AIDEC, OT_DREG|OT_AIDEC },
    { "addx",     L|W|B,  0xd100,   IN_ADDX,    OT_DREG|OT_AIDEC, OT_DREG|OT_AIDEC },
    { "sbcd",     B,      0x8100,   IN_ADDX,    OT_DREG|OT_AIDEC, OT_DREG|OT_AIDEC },
    { "abcd",     B,      0xc100,   IN_ADDX,    OT_DREG|OT_AIDEC, OT_DREG|OT_AIDEC },
    { "nbcd",     B,      0x4800,   IN_SCC,     OT_EADALT,      0 },
    { "tas",      B,      0x4ac0,   IN_SCC,     OT_EADALT,      0 },

    { "swap",     W,      0x4840,   IN_REG,     OT_DREG,        0 },
    { "unlk",     0,      0x4e58,   IN_REG,     OT_AREG,        0 },
    { "link",     W,      0x4e50,   IN_LINK,    OT_AREG,        OT_IMM },
    { "exg",      L,      0xc100,   IN_EXG,     OT_DREG|OT_AREG, OT_DREG|OT_AREG },
    { "ext",      L|W,    0x4800,   IN_EXT,     OT_DREG,        0 },
    { "cmpm",     L|W|B,  0xb108,   IN_CMPM,    OT_AIINC,       OT_AIINC },
    { "movem",    L|W,    0x4880,   IN_MOVEM,   OT_EACTL|OT_AIINC|OT_REGLIST, OT_EACALT|OT_AIDEC|OT_REGLIST },
    { "movep",    W,      0x0108,   IN_MOVEP,   OT_DREG|OT_AIDSP, OT_DREG|OT_AIDSP },

    { "trap",     0,      0x4e40,   IN_TRAP,    OT_IMM,         0 },
    { "stop",     0,      0x4e72,   IN_STOP,    OT_IMM,         0 },

    { "illegal",  0,      0x4afc,   IN_NOOPR,   0,              0 },
    { "reset",    0,      0x4e70,   IN_NOOPR,   0,              0 },
    { "nop",      0,      0x4e71,   IN_NOOPR,   0,              0 },
    { "rte",      0,      0x4e73,   IN_NOOPR,   0,              0 },
    { "rts",      0,      0x4e75,   IN_NOOPR,   0,              0 },
    { "trapv",    0,      0x4e76,   IN_NOOPR,   0,              0 },
    { "rtr",      0,      0x4e77,   IN_NOOPR,   0,              0 },
};

STATIC int emit_inline_m68k_data(emit_inline_asm_t *emit, int size, int opr, uint32_t data) {
    if (opr == OR_SIZE) {
        if (size == 0) {
            check_byte_range(emit, data);
        } else if (size == 1) {
            check_word_range(emit, data);
        }
    }
    if (opr == OR_WORD || (opr == OR_SIZE && size < 2)) {
        asm_m68k_op16(&emit->as, data);
    } else if (opr == OR_LONG || (opr == OR_SIZE && size == 2)) {
        asm_m68k_op32(&emit->as, data);
    }
    return 0;
}

STATIC void emit_inline_m68k_op(emit_inline_asm_t *emit, qstr op, mp_uint_t n_args, mp_parse_node_t *pn_args) {
    size_t op_len;
    const char *op_str = (const char *)qstr_data(op, &op_len);
    unsigned int i;
    const m68k_instr_table_t *inst;
    int size = -1;
    int defsize = -1;
    int cc = 0;

    inst = inst_table;
    for (i = 0; i < MP_ARRAY_SIZE(inst_table); i++, inst++) {
        int l = strlen(inst->opcode);
        if (strncmp(op_str, inst->opcode, l) == 0) {
            const char *str = op_str + l;
            if (inst->size & CC) {          /* opcode with condition code */
                if ((cc = get_cc(&str)) < 0) {
                    continue;
                }
            }
            if (*str != '\0') {             /* opcode with size */
                static const char *sz = "bwls";
                char *s = strchr(sz, *str);
                if (s == NULL || *(str + 1) != '\0') {
                    continue;
                }
                size = s - sz;
                defsize = size;
                if (!((1 << size) & inst->size)) {
                    continue;
                }
            } else {                        /* default size */
                size = (inst->size & L) ? 2 : ((inst->size & W) ? 1 : 0);
            }
            break;
        }
    }
    if (i == MP_ARRAY_SIZE(inst_table)) {
        goto unknown_op;
    }

    int r1 = -1, r2 = -1;
    operand_t o1, o2;
    mp_int_t rel;

    if (n_args > 0) {                       /* 1st operand */
        if (inst->oprtype1 == 0) {
            goto bad_operand;
        }
        r1 = get_operand(emit, pn_args[0], &o1, inst->oprtype1);
        if (r1 < 0) {
            goto bad_operand;
        }
    }
    if (n_args > 1) {                       /* 2nd operand */
        if (inst->oprtype2 == 0) {
            goto bad_operand;
        }
        r2 = get_operand(emit, pn_args[1], &o2, inst->oprtype2);
        if (r2 < 0) {
            goto bad_operand;
        }
    }

    if ((o1.type == OT_AREG || o2.type == OT_AREG) && defsize == 0) {
        goto bad_operand;           /* address register byte access */
    }

    switch (inst->type) {
    case IN_MOVE:
        if (o1.type == OT_SPREG) {
            if (o1.reg == 19 && o2.type == OT_AREG) {   /* move USP,An */
                if (size != 2) {
                    goto unknown_op;
                }
                asm_m68k_op16(&emit->as, 0x4e68 | o2.reg);
                return;
            }
            if (defsize > 0 && defsize != 1) {  /* move SR,<ea> */
                goto unknown_op;
            }
            if (o1.reg != 18 || !(o2.type & OT_EADALT)) {
                goto bad_operand;
            }
            asm_m68k_op16(&emit->as, 0x40c0 | o2.ea);
            emit_inline_m68k_data(emit, size, r2, o2.data);
            return;
        }
        if (o2.type == OT_SPREG) {
            if (o2.reg == 19 && o1.type == OT_AREG) {   /* move An,USP */
                if (size != 2) {
                    goto unknown_op;
                }
                asm_m68k_op16(&emit->as, 0x4e60 | o1.reg);
                return;
            }
            if (defsize > 0 && defsize != 1) {  /* move <ea>,CCR/SR */
                goto unknown_op;
            }
            if (!(o2.type & OT_EADATA)) {
                goto bad_operand;
            }
            asm_m68k_op16(&emit->as,
                          ((o2.reg == 18) ? 0x46c0 : 0x44c0) | o1.ea);
            emit_inline_m68k_data(emit, size, r1, o1.data);
            return;
        }

        static const byte mb[3] = {1, 3, 2};

        asm_m68k_op16(&emit->as,
                      inst->instr | (mb[size] << 12) |
                      ((o2.ea & 0x07) << 9) | ((o2.ea & 0x38) << (6 - 3)) | o1.ea);
        emit_inline_m68k_data(emit, size, r1, o1.data);
        emit_inline_m68k_data(emit, size, r2, o2.data);
        return;

    case IN_MOVEA:          /* move <ea>,Rn */
        asm_m68k_op16(&emit->as,
                      inst->instr | (size << 12) | (o2.reg << 9) | o1.ea);
        emit_inline_m68k_data(emit, size, r1, o1.data);
        return;

    case IN_MOVEQ:          /* moveq #xx,Dn */
        check_byte_range(emit, o1.data);
        asm_m68k_op16(&emit->as,
                      inst->instr | (o2.reg << 9) | (o1.data & 0xff));
        return;

    case IN_ADDQ:           /* OP #xx,<ea> */
        check_value_range(emit, o1.data, 1, 8);
        asm_m68k_op16(&emit->as,
                      inst->instr | (o1.data & 7) << 9 | (size << 6) | o2.ea);
        emit_inline_m68k_data(emit, size, r2, o2.data);
        return;

    case IN_ADD:            /* OP <ea>,Dn / Dn,<ea> */
        if (o2.type == OT_DREG) {   /* OP <ea>,Dn */
            asm_m68k_op16(&emit->as,
                          inst->instr | (o2.reg << 9) | (size << 6) | o1.ea);
            emit_inline_m68k_data(emit, size, r1, o1.data);
        } else if (o1.type != OT_DREG) {
            goto bad_operand;
        }

        /* fall through */
    case IN_EOR:            /* OP Dn,<ea> */
        asm_m68k_op16(&emit->as,
                      inst->instr | (o1.reg << 9) | (1 << 8) | (size << 6) | o2.ea);
        emit_inline_m68k_data(emit, size, r2, o2.data);
        return;

    case IN_ADDI:           /* OP.s #imm,<ea> */
        if (o2.type == OT_SPREG) {         /* ori/andi/eori to ccr/sr */
            if (o2.reg == 17 || o2.reg == 18) {     /* CCR/SR */
                asm_m68k_op16(&emit->as,
                              inst->instr | ((o2.reg - 17) << 6) | 0x3c);
                emit_inline_m68k_data(emit, 1, OR_SIZE, o1.data);
                return;
            } else {
                goto bad_operand;
            }
        }
        asm_m68k_op16(&emit->as,
                      inst->instr | (size << 6) | o2.ea);
        emit_inline_m68k_data(emit, size, OR_SIZE, o1.data);
        emit_inline_m68k_data(emit, size, r2, o2.data);
        return;

    case IN_ADDA:           /* OP <ea>,An */
        asm_m68k_op16(&emit->as,
                      inst->instr | (o2.reg << 9) | (size << 7) | o1.ea);
        emit_inline_m68k_data(emit, size, r1, o1.data);
        return;

    case IN_LEA:            /* lea <ea>,Dn */
        asm_m68k_op16(&emit->as,
                      inst->instr | (o2.reg << 9) | o1.ea);
        emit_inline_m68k_data(emit, size, r1, o1.data);
        return; 
    case IN_EA:             /* OP <ea> */
        asm_m68k_op16(&emit->as,
                      inst->instr | o1.ea);
        emit_inline_m68k_data(emit, size, r1, o1.data);
        return;

    case IN_BRA:            /* bra <label> */
        rel = (o1.data - emit->as.base.code_offset) - 2;
        if (cc == 1) {
            goto unknown_op;    /* bf is not allowed */
        }
        if (size == 1) {    /* bra.w */
            check_sword_range(emit, rel);
            asm_m68k_op16(&emit->as, inst->instr | (cc << 8));
            asm_m68k_op16(&emit->as, rel);
        } else {            /* bra.s */
            check_sbyte_range(emit, rel);
            asm_m68k_op16(&emit->as, 0x6000 | (cc << 8) | (rel & 0xff));
        }
        return;

    case IN_DBRA:           /* dbra Dn,<label> */
        rel = o2.data - emit->as.base.code_offset - 2;
        check_sword_range(emit, rel);
        asm_m68k_op16(&emit->as, inst->instr | (cc << 8) | o1.ea);
        asm_m68k_op16(&emit->as, rel);
        return;

    case IN_SCC:            /* OP <label> */
        asm_m68k_op16(&emit->as, inst->instr | (cc << 8) | o1.ea);
        emit_inline_m68k_data(emit, size, r1, o1.data);
        return;

    case IN_CLR:            /* OP <ea>*/
        asm_m68k_op16(&emit->as,
                      inst->instr | (size << 6) | o1.ea);
        emit_inline_m68k_data(emit, size, r1, o1.data);
        return;

    case IN_ROTSFT:         /* OP <ea> / Dx,Dy / #n,Dy */
        if (n_args == 1) {              /* <ea> */
            if (defsize > 0 && defsize != 1) {
                goto unknown_op;
            }
            if (!(o1.type & OT_EAMALT)) {
                goto bad_operand;
            }
            asm_m68k_op16(&emit->as, inst->instr | 0xc0 | o1.ea);
            emit_inline_m68k_data(emit, size, r1, o1.data);
        } else {
            if (o2.type != OT_DREG) {
                goto bad_operand;
            }
            if (o1.type == OT_DREG) {           /* Dx,Dy */
                asm_m68k_op16(&emit->as,
                              inst->instr | (o1.reg << 9) | (size << 6) |
                              (1 << 5) | o2.reg);
            } else if (o1.type == OT_IMM) {     /* #n,Dy*/
                check_value_range(emit, o1.data, 1, 8);
                asm_m68k_op16(&emit->as,
                              inst->instr | (o1.data & 7) << 9 | (size << 6) |
                              o2.reg);
            } else {
                goto bad_operand;
            }
        }
        return;

    case IN_BCHG:           /* OP Dx/#n,<ea> */
        if (o1.type == OT_DREG) {       /* Dx,<ea> */
            if (defsize > 0 && defsize != 2) {
                goto unknown_op;
            }
            asm_m68k_op16(&emit->as,
                          inst->instr | (o1.reg << 9) | (1 << 8) | o2.ea);
            emit_inline_m68k_data(emit, size, r2, o2.data);
        } else {                        /* #n,<ea> */ 
            if (defsize > 0 && defsize != 0) {
                goto unknown_op;
            }
            asm_m68k_op16(&emit->as, inst->instr | (1 << 11) | o2.ea);
            asm_m68k_op16(&emit->as, o1.data & 0xff);
            emit_inline_m68k_data(emit, size, r2, o2.data);
        }
        return;

    case IN_MULDIV:         /* OP <ea>,Dn */
        asm_m68k_op16(&emit->as,
                      inst->instr | (o2.reg << 9) | (size << 7) | o1.ea);
        emit_inline_m68k_data(emit, size, r1, o1.data);
        return;

    case IN_ADDX:           /* OP Dn,Dn / -(An),-(An) */
        if (o1.type != o2.type) {
            goto bad_operand;
        }
        asm_m68k_op16(&emit->as,
                      inst->instr | (o2.reg << 9) | (size << 6) |
                      ((o1.type == OT_DREG ? 0 : 1) << 3) | o1.reg);
        return;

    case IN_REG:            /* OP Rn */
        asm_m68k_op16(&emit->as, inst->instr | o1.reg);
        return;

    case IN_LINK:           /* link An,#imm */
        asm_m68k_op16(&emit->as, inst->instr | o1.reg);
        emit_inline_m68k_data(emit, size, r2, o2.data);
        return;

    case IN_EXG:            /* exg Rn,Rn */
        if (o1.type == o2.type) {           /* Dx,Dy or Ax,Ay */
            asm_m68k_op16(&emit->as,
                          inst->instr | (o1.reg << 9) |
                                        (0x08 << 3) | (o2.ea & 0x0f));
        } else if (o1.type == OT_DREG) {    /* Dx,Ay */
            asm_m68k_op16(&emit->as,
                          inst->instr | (o1.reg << 9) |
                                        (0x11 << 3) | o2.reg);
        } else {                            /* Ax,Dy */
            asm_m68k_op16(&emit->as,
                          inst->instr | (o2.reg << 9) |
                                        (0x11 << 3) | o1.reg);
        }
        return;

    case IN_EXT:            /* ext Rn */
        asm_m68k_op16(&emit->as,
                      inst->instr | (((size == 1) ? 0x2 : 0x3) << 6)
                                  | o1.reg);
        return;

    case IN_CMPM:           /* cmpm (An)+,(An)+ */
        asm_m68k_op16(&emit->as,
                      inst->instr | (o2.reg << 9) | (size << 6) | o1.reg);
        return;

    case IN_MOVEM:
        if (o1.type == OT_REGLIST) {        /* <reglist>,<ea> */
            if (o2.type == OT_REGLIST) {
                goto bad_operand;
            }
            if (o2.type == OT_AIDEC) {      /* -(An) */
                int i;
                uint32_t rev = 0;
                for (i = 0; i < 16; i++) {
                    if (o1.data & (1 << i)) {
                        rev |= (1 << (15 - i));
                    }
                }
                o1.data = rev;
            }
            asm_m68k_op16(&emit->as,
                          inst->instr | (((size == 1) ? 0 : 1) << 6) | o2.ea);
            asm_m68k_op16(&emit->as, o1.data);
            emit_inline_m68k_data(emit, size, r2, o2.data);
        } else if (o2.type == OT_REGLIST) { /* <ea>,<reglist> */
            asm_m68k_op16(&emit->as,
                          inst->instr | (1 << 10) | (((size == 1) ? 0 : 1) << 6) | o1.ea);
            emit_inline_m68k_data(emit, size, r1, o1.data);
            asm_m68k_op16(&emit->as, o2.data);
        } else {
            goto bad_operand;
        }
        return;

    case IN_MOVEP:
        if (o1.type == OT_DREG) {       /* Dx,d16(Ay) */
            asm_m68k_op16(&emit->as,
                          inst->instr | (o1.reg << 9) |
                          ((5 + size) << 6) | o2.reg);
            emit_inline_m68k_data(emit, size, r2, o2.data);
        } else {                        /* d16(Ay),Dx */
            asm_m68k_op16(&emit->as,
                          inst->instr | (o2.reg << 9) |
                          ((3 + size) << 6) | o1.reg);
            emit_inline_m68k_data(emit, size, r1, o1.data);
        }
        return;

    case IN_TRAP:           /* trap #n */
        check_value_range(emit, o1.data, 0, 15);
        asm_m68k_op16(&emit->as, inst->instr | (o1.data & 15));
        return;

    case IN_STOP:           /* stop #n */
        asm_m68k_op16(&emit->as, inst->instr);
        emit_inline_m68k_data(emit, 1, OR_SIZE, o1.data);
        return;

    case IN_NOOPR:          /* no operand */
        asm_m68k_op16(&emit->as, inst->instr);
        return;

    default:
        goto unknown_op;
    }

    return;

unknown_op:
    emit_inline_m68k_error_exc(emit, mp_obj_new_exception_msg_varg(&mp_type_SyntaxError, MP_ERROR_TEXT("unsupported M68K instruction '%s' with %d arguments"), op_str, n_args));
    return;

bad_operand:
    emit_inline_m68k_error_exc(emit, mp_obj_new_exception_msg_varg(&mp_type_SyntaxError, MP_ERROR_TEXT("bad M68K instruction '%s' operand"), op_str));
    return;
}

const emit_inline_asm_method_table_t emit_inline_m68k_method_table = {
    #if MICROPY_DYNAMIC_COMPILER
    emit_inline_m68k_new,
    emit_inline_m68k_free,
    #endif

    emit_inline_m68k_start_pass,
    emit_inline_m68k_end_pass,
    emit_inline_m68k_count_params,
    emit_inline_m68k_label,
    emit_inline_m68k_op,
};

#endif // MICROPY_EMIT_INLINE_M68K
