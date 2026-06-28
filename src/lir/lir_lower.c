#include "lir.h"
#include "../mir/mir.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define LI(opc,dstn,sr1,sr2,immv) \
    do { LirInst _li; memset(&_li,0,sizeof(_li)); _li.opcode=(opc); _li.dst=(dstn); _li.src1=(sr1); _li.src2=(sr2); _li.imm=(immv); lir_add(l, &_li); } while(0)

#define LI2(opc,dstn,sr1,sr2,immv,ccc,flt) \
    do { LirInst _li; memset(&_li,0,sizeof(_li)); _li.opcode=(opc); _li.dst=(dstn); _li.src1=(sr1); _li.src2=(sr2); _li.imm=(immv); _li.cc=(ccc); _li.is_float=(flt); lir_add(l, &_li); } while(0)

static void lir_add(LirFn *l, LirInst *inst) {
    if (l->count >= MAX_LIR_INSTS) return;
    l->insts[l->count++] = *inst;
}

LirFn *lir_lower(MirFn *mir) {
    LirFn *l = calloc(1, sizeof(LirFn));
    if (!l) return NULL;

    int *block_labels = malloc(mir->block_count * sizeof(int));
    if (!block_labels) { free(l); return NULL; }

    for (int i = 0; i < mir->block_count; i++)
        block_labels[i] = l->num_labels++;

    int pos = 0;
    for (int bi = 0; bi < mir->block_count; bi++) {
        MirBlock *b = &mir->blocks[bi];

        /* emit block label */
        LI(LIR_LABEL, 0, 0, 0, block_labels[bi]);

        /* emit instructions */
        for (int j = 0; j < b->inst_count; j++) {
            MirInst *mi = &mir->insts[pos++];

            switch (mi->opcode) {
            case MIR_CONST:
                LI(LIR_MOV_IMM, mi->dst, 0, 0, mi->imm);
                break;

            case MIR_COPY:
                LI(LIR_MOV, mi->dst, mi->src1, 0, 0);
                break;

            case MIR_LOAD: {
                LirInst li;
                memset(&li, 0, sizeof(li));
                li.opcode = LIR_LOAD; li.dst = mi->dst; li.src1 = mi->src1;
                li.is_float = mi->float_result;
                lir_add(l, &li);
                break;
            }

            case MIR_STORE: {
                LirInst li;
                memset(&li, 0, sizeof(li));
                li.opcode = LIR_STORE; li.dst = mi->src1; li.src1 = mi->src2;
                li.is_float = mi->float_result;
                lir_add(l, &li);
                break;
            }

            case MIR_LEA:
                LI(LIR_LEA, mi->dst, mi->src1, 0, 0);
                break;

            case MIR_UNARY: {
                switch (mi->op) {
                case 0: LI(LIR_NOT, mi->dst, mi->src1, 0, 0); break;
                case 1: LI(LIR_NEG, mi->dst, mi->src1, 0, 0); break;
                case 2: LI(LIR_NOT, mi->dst, mi->src1, 0, 0); break;
                case 3: LI(LIR_MOV, mi->dst, mi->src1, 0, 0); break;
                case 4: LI(LIR_MOV, mi->dst, mi->src1, 0, 0); break;
                }
                break;
            }

            case MIR_BINARY: {
                int op = mi->op;
                int is_float = mi->float_result;

                if (op >= 5 && op <= 10) {
                    /* comparison */
                    int signed_cc[] = {CC_E, CC_NE, CC_L, CC_LE, CC_G, CC_GE};
                    int unsigned_cc[] = {CC_E, CC_NE, CC_B, CC_BE, CC_A, CC_AE};
                    int cc = mi->is_unsigned ? unsigned_cc[op - 5] : signed_cc[op - 5];
                    LI(LIR_CMP, mi->src1, mi->src2, 0, 0);
                    LI2(LIR_SETcc, mi->dst, 0, 0, 0, cc, 0);
                } else if (op >= 11 && op <= 12) {
                    if (op == 11) {
                        LI(LIR_MOV, mi->dst, mi->src1, 0, 0);
                        LI(LIR_AND, mi->dst, mi->src2, 0, 0);
                    } else {
                        LI(LIR_MOV, mi->dst, mi->src1, 0, 0);
                        LI(LIR_OR, mi->dst, mi->src2, 0, 0);
                    }
                } else if (op == 15) {
                    LI(LIR_MOV, mi->dst, mi->src1, 0, 0);
                    LI(LIR_AND, mi->dst, mi->src2, 0, 0);
                } else if (op == 13) {
                    LI(LIR_MOV, mi->dst, mi->src1, 0, 0);
                    LI(LIR_OR, mi->dst, mi->src2, 0, 0);
                } else if (op == 14) {
                    LI(LIR_MOV, mi->dst, mi->src1, 0, 0);
                    LI(LIR_XOR, mi->dst, mi->src2, 0, 0);
                } else if (op == 16) {
                    LI(LIR_MOV, mi->dst, mi->src1, 0, 0);
                    LI(LIR_SHL, mi->dst, mi->src2, 0, 0);
                } else if (op == 17) {
                    LI(LIR_MOV, mi->dst, mi->src1, 0, 0);
                    LI(mi->is_unsigned ? LIR_SHR : LIR_SAR, mi->dst, mi->src2, 0, 0);
                } else if (op == 18) {
                    LI(LIR_MOV, mi->dst, mi->src1, 0, 0);
                } else {
                    int lir_op;
                    int is_div = 0;
                    switch (op) {
                    case 0: lir_op = LIR_ADD; break;
                    case 1: lir_op = LIR_SUB; break;
                    case 2: lir_op = LIR_IMUL; break;
                    case 3: lir_op = mi->is_unsigned ? LIR_DIV : LIR_IDIV; is_div = 1; break;
                    case 4: lir_op = mi->is_unsigned ? LIR_DIV : LIR_IDIV; is_div = 1; break;
                    default: lir_op = LIR_ADD; break;
                    }
                    if (is_div) {
                        LirInst ci;
                        memset(&ci, 0, sizeof(ci));
                        ci.opcode = lir_op; ci.dst = mi->dst; ci.src1 = mi->src1; ci.src2 = mi->src2;
                        ci.is_unsigned = mi->is_unsigned;
                        ci.imm = (op == 4) ? 1 : 0;
                        lir_add(l, &ci);
                    } else if (is_float) {
                        int flir = LIR_ADDSD;
                        if (lir_op == LIR_SUB) flir = LIR_SUBSD;
                        else if (lir_op == LIR_IMUL) flir = LIR_MULSD;
                        else if (lir_op == LIR_DIV) flir = LIR_DIVSD;
                        /* mov dst, src1; op dst, src2 */
                        LI(LIR_MOV, mi->dst, mi->src1, 0, 0);
                        LI2(flir, mi->dst, mi->src2, 0, 0, 0, 1);
                    } else {
                        /* mov dst, src1; op dst, src2 */
                        LI(LIR_MOV, mi->dst, mi->src1, 0, 0);
                        LI(lir_op, mi->dst, mi->src2, 0, 0);
                    }
                }
                break;
            }

            case MIR_CALL: {
                LirInst ci;
                memset(&ci, 0, sizeof(ci));
                ci.opcode = LIR_CALL; ci.dst = mi->dst;
                strncpy(ci.callee, mi->callee, 63); ci.callee[63] = 0;
                ci.num_args = mi->num_args;
                for (int k = 0; k < mi->num_args; k++) ci.args[k] = mi->args[k];
                ci.is_float = mi->float_result;
                lir_add(l, &ci);
                break;
            }

            case MIR_BRANCH: {
                /* compare condition against 0 and branch */
                LI(LIR_CMP, mi->src1, 0, 0, 0);
                LirInst ji;
                memset(&ji, 0, sizeof(ji));
                ji.opcode = LIR_Jcc;
                ji.cc = CC_NE;
                ji.label_id = b->term_true >= 0 ? block_labels[b->term_true] : -1;
                ji.imm = ji.label_id;
                lir_add(l, &ji);
                /* fall through: jmp to false */
                LirInst uj;
                memset(&uj, 0, sizeof(uj));
                uj.opcode = LIR_JMP;
                uj.label_id = b->term_false >= 0 ? block_labels[b->term_false] : -1;
                uj.imm = uj.label_id;
                lir_add(l, &uj);
                break;
            }

            case MIR_JUMP: {
                LirInst uj;
                memset(&uj, 0, sizeof(uj));
                uj.opcode = LIR_JMP;
                uj.label_id = b->term_target >= 0 ? block_labels[b->term_target] : -1;
                uj.imm = uj.label_id;
                lir_add(l, &uj);
                break;
            }

            case MIR_RET: {
                LirInst ri;
                memset(&ri, 0, sizeof(ri));
                ri.opcode = LIR_RET; ri.dst = mi->dst;
                ri.is_float = mi->float_result;
                lir_add(l, &ri);
                break;
            }

            default:
                break;
            }
        }
    }

    l->num_vregs = mir->num_vregs;
    free(block_labels);
    return l;
}

void lir_print(LirFn *l) {
    printf("LIR: %d insts, %d vregs\n", l->count, l->num_vregs);
    for (int i = 0; i < l->count; i++) {
        LirInst *li = &l->insts[i];
        printf("  %3d: ", i);
        switch (li->opcode) {
        case LIR_LABEL:  printf("LABEL %lld", li->imm); break;
        case LIR_MOV:    printf("v%d = v%d", li->dst, li->src1); break;
        case LIR_MOV_IMM: printf("v%d = %lld", li->dst, li->imm); break;
        case LIR_LOAD:   printf("v%d = *v%d", li->dst, li->src1); break;
        case LIR_STORE:  printf("*v%d = v%d", li->dst, li->src1); break;
        case LIR_LEA:    printf("v%d = &v%d", li->dst, li->src1); break;
        case LIR_ADD:    printf("v%d = v%d + v%d", li->dst, li->src1, li->src2); break;
        case LIR_SUB:    printf("v%d = v%d - v%d", li->dst, li->src1, li->src2); break;
        case LIR_IMUL:   printf("v%d = v%d * v%d", li->dst, li->src1, li->src2); break;
        case LIR_CMP:    printf("cmp v%d, v%d", li->dst, li->src1); break;
        case LIR_SETcc:  printf("setcc(%d) v%d", li->cc, li->dst); break;
        case LIR_Jcc:    printf("jcc(%d) label %d", li->cc, li->label_id); break;
        case LIR_JMP:    printf("jmp label %d", li->label_id); break;
        case LIR_CALL:   printf("call %s", li->callee); break;
        case LIR_RET:    printf("ret v%d", li->dst); break;
        case LIR_NEG:    printf("v%d = -v%d", li->dst, li->src1); break;
        case LIR_NOT:    printf("v%d = ~v%d", li->dst, li->src1); break;
        case LIR_AND:    printf("v%d = v%d & v%d", li->dst, li->src1, li->src2); break;
        case LIR_OR:     printf("v%d = v%d | v%d", li->dst, li->src1, li->src2); break;
        case LIR_XOR:    printf("v%d = v%d ^ v%d", li->dst, li->src1, li->src2); break;
        default:         printf("opcode=%d", li->opcode); break;
        }
        printf("\n");
    }
}
