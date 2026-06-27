#include "../lir/lir.h"
#include "../mir/mir.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

/* physical register names for assembly emission */
static const char *reg_name(int r) {
    static const char *names[] = {
        "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
        "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
    };
    if (r >= 0 && r < 24) return names[r];
    return NULL;
}

/* 8-bit register name for SETcc / MOVZX */
static const char *reg_name_8(int r) {
    static const char *names8[] = {
        "al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil",
        "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b",
    };
    if (r >= 0 && r < 16) return names8[r];
    return "al";
}

static const char *cc_name(int cc) {
    switch (cc) {
    case CC_E:  return "e";
    case CC_NE: return "ne";
    case CC_L:  return "l";
    case CC_LE: return "le";
    case CC_G:  return "g";
    case CC_GE: return "ge";
    case CC_B:  return "b";
    case CC_BE: return "be";
    case CC_A:  return "a";
    case CC_AE: return "ae";
    default: return "e";
    }
}

static void emit_asm(FILE *fp, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);
}

void emit_lir_function(FILE *fp, LirFn *lir, const char *fn_name, int num_spills, int is_pub) {
    /* function prologue */
    if (is_pub) {
        emit_asm(fp, "\n.globl %s\n", fn_name);
    }
    emit_asm(fp, ".type %s, @function\n", fn_name);
    emit_asm(fp, "%s:\n", fn_name);
    emit_asm(fp, "  push rbp\n");
    emit_asm(fp, "  mov rbp, rsp\n");
    if (num_spills > 0)
        emit_asm(fp, "  sub rsp, %d\n", num_spills * 8);

    /* emit instructions */
    for (int i = 0; i < lir->count; i++) {
        LirInst *inst = &lir->insts[i];

        switch (inst->opcode) {
        case LIR_LABEL:
            emit_asm(fp, ".L_lir_%lld:\n", inst->imm);
            break;

        case LIR_MOV: {
            const char *dst = reg_name(inst->dst);
            const char *src = reg_name(inst->src1);
            if (dst && src) {
                if (inst->dst >= LREG_XMM0 || inst->src1 >= LREG_XMM0)
                    emit_asm(fp, "  movaps %s, %s\n", dst, src);
                else
                    emit_asm(fp, "  mov %s, %s\n", dst, src);
            }
            break;
        }

        case LIR_MOV_IMM:
            emit_asm(fp, "  mov %s, %lld\n", reg_name(inst->dst), inst->imm);
            break;

        case LIR_LOAD: {
            const char *dst = reg_name(inst->dst);
            const char *src = reg_name(inst->src1);
            if (dst && src) {
                if (inst->is_float)
                    emit_asm(fp, "  movsd %s, qword ptr [%s]\n", dst, src);
                else
                    emit_asm(fp, "  mov %s, qword ptr [%s]\n", dst, src);
            }
            break;
        }

        case LIR_STORE: {
            const char *dst = reg_name(inst->dst);
            const char *src = reg_name(inst->src1);
            if (dst && src) {
                if (inst->is_float)
                    emit_asm(fp, "  movsd qword ptr [%s], %s\n", dst, src);
                else
                    emit_asm(fp, "  mov qword ptr [%s], %s\n", dst, src);
            }
            break;
        }

        case LIR_LEA:
            /* lea: address of a stack variable */
            emit_asm(fp, "  lea %s, [rbp - %d]\n", reg_name(inst->dst), (inst->src1 + 1) * 8);
            break;

        case LIR_ADD:
            emit_asm(fp, "  add %s, %s\n", reg_name(inst->dst), reg_name(inst->src1));
            break;

        case LIR_SUB:
            emit_asm(fp, "  sub %s, %s\n", reg_name(inst->dst), reg_name(inst->src1));
            break;

        case LIR_IMUL:
            emit_asm(fp, "  imul %s, %s\n", reg_name(inst->dst), reg_name(inst->src1));
            break;

        case LIR_NEG:
            emit_asm(fp, "  neg %s\n", reg_name(inst->dst));
            break;

        case LIR_NOT:
            emit_asm(fp, "  not %s\n", reg_name(inst->dst));
            break;

        case LIR_AND:
            emit_asm(fp, "  and %s, %s\n", reg_name(inst->dst), reg_name(inst->src1));
            break;

        case LIR_OR:
            emit_asm(fp, "  or %s, %s\n", reg_name(inst->dst), reg_name(inst->src1));
            break;

        case LIR_XOR:
            emit_asm(fp, "  xor %s, %s\n", reg_name(inst->dst), reg_name(inst->src1));
            break;

        case LIR_SHL:
            emit_asm(fp, "  mov %s, %s\n", reg_name(inst->dst), reg_name(inst->src1));
            emit_asm(fp, "  shl %s, cl\n", reg_name(inst->dst));
            break;

        case LIR_SHR:
            emit_asm(fp, "  mov %s, %s\n", reg_name(inst->dst), reg_name(inst->src1));
            emit_asm(fp, "  shr %s, cl\n", reg_name(inst->dst));
            break;

        case LIR_SAR:
            emit_asm(fp, "  mov %s, %s\n", reg_name(inst->dst), reg_name(inst->src1));
            emit_asm(fp, "  sar %s, cl\n", reg_name(inst->dst));
            break;

        case LIR_CMP:
            if (inst->src1 >= 0 && inst->src2 >= 0)
                emit_asm(fp, "  cmp %s, %s\n", reg_name(inst->dst), reg_name(inst->src1));
            else if (inst->dst >= 0)
                emit_asm(fp, "  cmp %s, 0\n", reg_name(inst->dst));
            break;

        case LIR_SETcc:
            emit_asm(fp, "  set%s %s\n", cc_name(inst->cc), reg_name_8(inst->dst));
            emit_asm(fp, "  movzx %s, %s\n", reg_name(inst->dst), reg_name_8(inst->dst));
            break;

        case LIR_Jcc: {
            const char *cc = cc_name(inst->cc);
            emit_asm(fp, "  j%s .L_lir_%d\n", cc, inst->label_id);
            break;
        }

        case LIR_JMP:
            emit_asm(fp, "  jmp .L_lir_%d\n", inst->label_id);
            break;

        case LIR_CALL: {
            /* set up args (in rdi, rsi, rdx, rcx, r8, r9) */
            int arg_regs[] = {LREG_RDI, LREG_RSI, LREG_RDX, LREG_RCX, LREG_R8, LREG_R9};
            for (int k = 0; k < inst->num_args && k < 6; k++) {
                const char *r = reg_name(inst->args[k]);
                const char *arg_r = reg_name(arg_regs[k]);
                if (r && arg_r && r != arg_r)
                    emit_asm(fp, "  mov %s, %s\n", arg_r, r);
            }
            if (strcmp(inst->callee, "malloc") == 0)
                emit_asm(fp, "  call malloc@plt\n");
            else if (strcmp(inst->callee, "free") == 0)
                emit_asm(fp, "  call free@plt\n");
            else if (strchr(inst->callee, '@'))
                emit_asm(fp, "  call %s\n", inst->callee);
            else if (strcmp(inst->callee, "print_int") == 0)
                emit_asm(fp, "  call print_int\n");
            else if (strcmp(inst->callee, "print_float") == 0)
                emit_asm(fp, "  call print_float\n");
            else if (strcmp(inst->callee, "print_str") == 0)
                emit_asm(fp, "  call print_str\n");
            else if (strcmp(inst->callee, "sleep") == 0)
                emit_asm(fp, "  call sleep\n");
            else if (strcmp(inst->callee, "read_int") == 0)
                emit_asm(fp, "  call read_int\n");
            else if (strcmp(inst->callee, "input") == 0)
                emit_asm(fp, "  call input\n");
            else if (strcmp(inst->callee, "strlen") == 0)
                emit_asm(fp, "  call strlen\n");
            else if (strcmp(inst->callee, "time_ms") == 0)
                emit_asm(fp, "  call time_ms\n");
            else if (strcmp(inst->callee, "calc_expr") == 0)
                emit_asm(fp, "  call calc_expr\n");
            else if (strcmp(inst->callee, "inspect") == 0)
                emit_asm(fp, "  call inspect\n");
            else if (strcmp(inst->callee, "assert") == 0)
                emit_asm(fp, "  call assert\n");
            else if (strcmp(inst->callee, "clear_screen") == 0)
                emit_asm(fp, "  call clear_screen\n");
            else if (strcmp(inst->callee, "reset_attr") == 0)
                emit_asm(fp, "  call reset_attr\n");
            else if (strcmp(inst->callee, "set_fg") == 0)
                emit_asm(fp, "  call set_fg\n");
            else if (strcmp(inst->callee, "set_bg") == 0)
                emit_asm(fp, "  call set_bg\n");
            else if (strcmp(inst->callee, "hide_cursor") == 0)
                emit_asm(fp, "  call hide_cursor\n");
            else if (strcmp(inst->callee, "show_cursor") == 0)
                emit_asm(fp, "  call show_cursor\n");
            else if (strcmp(inst->callee, "get_frame_ptr") == 0)
                emit_asm(fp, "  call get_frame_ptr\n");
            else
                emit_asm(fp, "  call %s\n", inst->callee);
            break;
        }

        case LIR_RET:
            if (inst->dst >= 0) {
                const char *r = reg_name(inst->dst);
                if (r) emit_asm(fp, "  mov rax, %s\n", r);
            }
            emit_asm(fp, "  mov rsp, rbp\n");
            emit_asm(fp, "  pop rbp\n");
            emit_asm(fp, "  ret\n");
            break;

        case LIR_DIV: {
            /* unsigned div: put src1 in rax, xor rdx, div src2 */
            const char *s1 = reg_name(inst->src1);
            const char *s2 = reg_name(inst->src2);
            const char *d = reg_name(inst->dst);
            if (s1) emit_asm(fp, "  mov rax, %s\n", s1);
            emit_asm(fp, "  xor rdx, rdx\n");
            if (s2) emit_asm(fp, "  div %s\n", s2);
            if (inst->imm == 1) /* remainder */
                emit_asm(fp, "  mov %s, rdx\n", d ? d : "rax");
            else
                emit_asm(fp, "  mov %s, rax\n", d ? d : "rax");
            break;
        }

        case LIR_IDIV: {
            /* signed div */
            const char *s1 = reg_name(inst->src1);
            const char *s2 = reg_name(inst->src2);
            const char *d = reg_name(inst->dst);
            if (s1) emit_asm(fp, "  mov rax, %s\n", s1);
            emit_asm(fp, "  cqo\n");
            if (s2) emit_asm(fp, "  idiv %s\n", s2);
            if (inst->imm == 1) /* remainder */
                emit_asm(fp, "  mov %s, rdx\n", d ? d : "rax");
            else
                emit_asm(fp, "  mov %s, rax\n", d ? d : "rax");
            break;
        }

        case LIR_CQTO:
            emit_asm(fp, "  cqo\n"); /* sign-extend rax to rdx:rax */
            break;

        case LIR_MOVSX:
            emit_asm(fp, "  movsx %s, %s\n", reg_name(inst->dst), reg_name(inst->src1));
            break;

        case LIR_MOVZX:
            emit_asm(fp, "  movzx %s, %s\n", reg_name(inst->dst), reg_name(inst->src1));
            break;

        case LIR_MOVSS:
            emit_asm(fp, "  movss %s, %s\n", reg_name(inst->dst), reg_name(inst->src1));
            break;

        case LIR_ADDSD:
            emit_asm(fp, "  addsd %s, %s\n", reg_name(inst->dst), reg_name(inst->src1));
            break;

        case LIR_SUBSD:
            emit_asm(fp, "  subsd %s, %s\n", reg_name(inst->dst), reg_name(inst->src1));
            break;

        case LIR_MULSD:
            emit_asm(fp, "  mulsd %s, %s\n", reg_name(inst->dst), reg_name(inst->src1));
            break;

        case LIR_DIVSD:
            emit_asm(fp, "  divsd %s, %s\n", reg_name(inst->dst), reg_name(inst->src1));
            break;

        case LIR_MOVQ:
            emit_asm(fp, "  movq %s, %s\n", reg_name(inst->dst), reg_name(inst->src1));
            break;

        case LIR_PUSH:
            emit_asm(fp, "  push %s\n", reg_name(inst->dst));
            break;

        case LIR_POP:
            emit_asm(fp, "  pop %s\n", reg_name(inst->dst));
            break;

        default:
            break;
        }
    }

    /* function epilogue (fallback if no RET was emitted) */
    emit_asm(fp, "  mov rsp, rbp\n");
    emit_asm(fp, "  pop rbp\n");
    emit_asm(fp, "  ret\n");
    emit_asm(fp, ".L_%s_end:\n", fn_name);
}
