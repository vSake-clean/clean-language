#include "codegen.h"
#include "../ast.h"
#include "../diag.h"
#include "../check.h"
#include "../parser/parser.h"
#include "../runtime/clgui_embed.h"
#include "../mir/mir.h"
#include "../lir/lir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <limits.h>
#include <stdint.h>

#define MAX_VARS 256
#define MAX_STRS 1024
#define MAX_VAR_REGS 3
/* r13, r14, r15 for variables. r12/rbx used as temps by comprehension/struct codegen. */
#define REG_IDX_R13 0
#define REG_IDX_R14 1
#define REG_IDX_R15 2

#define MAX_CG_STRUCTS 32
#define MAX_CG_FIELDS 32
#define MAX_CG_ENUMS 16
#define MAX_CG_VARIANTS 16

typedef struct {
    char name[64];
    char fields[MAX_CG_FIELDS][64];
    int offsets[MAX_CG_FIELDS];
    int field_count;
    int total_size;
} CStructDef;

typedef struct {
    char name[64];
    char variants[MAX_CG_VARIANTS][64];
    int variant_sizes[MAX_CG_VARIANTS];
    int payload_sizes[MAX_CG_VARIANTS][MAX_CG_FIELDS];
    int payload_counts[MAX_CG_VARIANTS];
    int variant_count;
    int total_size;
} CEnumDef;

static const char *var_reg_names[] = {"r13", "r14", "r15"};

typedef struct {
    char names[MAX_VARS][64];
    int offsets[MAX_VARS];
    int regs[MAX_VARS];
    int struct_idx[MAX_VARS];
    int is_float[MAX_VARS];
    int is_heap[MAX_VARS];
    int var_type[MAX_VARS];
    int count;
    int stack_size;
    int reg_count;
    int used_regs;
} SymTab;

static void sym_init(SymTab *s) {
    s->count = 0;
    s->stack_size = 8;
    s->reg_count = 0;
    s->used_regs = 0;
    for (int i = 0; i < MAX_VARS; i++) s->regs[i] = -1;
}

static int sym_find(SymTab *s, const char *name) {
    for (int i = 0; i < s->count; i++)
        if (strcmp(s->names[i], name) == 0) return s->offsets[i];
    return -1;
}

static int sym_find_idx(SymTab *s, const char *name) {
    for (int i = 0; i < s->count; i++)
        if (strcmp(s->names[i], name) == 0) return i;
    return -1;
}

static int sym_add(SymTab *s, const char *name) {
    if (s->count >= MAX_VARS) { fprintf(stderr, "too many variables\n"); exit(1); }
    strncpy(s->names[s->count], name, 63);
    s->names[s->count][63] = 0;
    s->stack_size += 8;
    s->offsets[s->count] = s->stack_size;
    s->regs[s->count] = (s->reg_count < MAX_VAR_REGS) ? s->reg_count : -1;
    if (s->regs[s->count] >= 0) {
        s->used_regs |= (1 << s->regs[s->count]);
        s->reg_count++;
    }
    s->struct_idx[s->count] = -1;
    s->is_float[s->count] = 0;
    s->is_heap[s->count] = 0;
    s->var_type[s->count] = TYPE_I64;
    s->count++;
    return s->offsets[s->count-1];
}

static int sym_add_struct(SymTab *s, const char *name, int st_idx) {
    int off = sym_add(s, name);
    s->struct_idx[s->count-1] = st_idx;
    return off;
}

typedef struct {
    FILE *out;
    SymTab sym;
    int label_seq;
    Diag *diag;
    const char *strs[MAX_STRS];
    int str_cnt;
    int loop_start[64];
    int loop_continue[64];
    int loop_end[64];
    int loop_sp;
    CStructDef structs[MAX_CG_STRUCTS];
    int struct_count;
    CEnumDef enums[MAX_CG_ENUMS];
    int enum_count;
    int gui_mode;
} Codegen;

static int new_label(Codegen *c) { return c->label_seq++; }

static void emit(Codegen *c, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(c->out, fmt, ap);
    va_end(ap);
}

static int get_str_id(Codegen *c, const char *s) {
    for (int i = 0; i < c->str_cnt; i++)
        if (strcmp(c->strs[i], s) == 0) return i;
    if (c->str_cnt >= MAX_STRS) { diag_add(c->diag, 0, SEV_ERROR, 0,0,0, "too many unique string literals"); exit(1); }
    c->strs[c->str_cnt++] = s;
    return c->str_cnt - 1;
}

static void emit_strtab(Codegen *c) {
    if (c->str_cnt == 0) return;
    emit(c, ".section .rodata\n");
    for (int i = 0; i < c->str_cnt; i++) {
        const char *p = c->strs[i];
        size_t len = strlen(p);
        const size_t CHUNK = 4096;
        if (len <= CHUNK) {
            emit(c, ".L_str_%d: .asciz \"", i);
            while (*p) {
                char ch = *p++;
                if (ch == '"') emit(c, "\\\"");
                else if (ch == '\\') emit(c, "\\\\");
                else if (ch == '\n') emit(c, "\\n");
                else if (ch == '\t') emit(c, "\\t");
                else fputc(ch, c->out);
            }
            emit(c, "\"\n");
        } else {
            emit(c, ".L_str_%d:\n", i);
            while (len > 0) {
                size_t n = len > CHUNK ? CHUNK : len;
                fputs("  .ascii \"", c->out);
                for (size_t j = 0; j < n; j++, p++) {
                    char ch = *p;
                    if (ch == '"') fputs("\\\"", c->out);
                    else if (ch == '\\') fputs("\\\\", c->out);
                    else if (ch == '\n') fputs("\\n", c->out);
                    else if (ch == '\t') fputs("\\t", c->out);
                    else fputc(ch, c->out);
                }
                fputs("\"\n", c->out);
                len -= n;
            }
            emit(c, "  .byte 0\n");
        }
    }
    emit(c, ".section .text\n");
}

static void emit_print_int(Codegen *c) {
    int lbl = new_label(c);
    emit(c, "\n# print_int(rdi) -> void\n");
    emit(c, ".globl print_int\n");
    emit(c, ".type print_int, @function\n");
    emit(c, "print_int:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n  sub rsp, 48\n");
    emit(c, "  mov qword ptr [rbp-8], rdi\n");
    emit(c, "  lea rsi, [rbp-40]\n  add rsi, 19\n");
    emit(c, "  mov rax, qword ptr [rbp-8]\n  mov r8, 10\n");
    emit(c, "  cmp rax, 0\n  jge .L_print_pos_%d\n", lbl);
    emit(c, "  neg rax\n  push rax\n");
    emit(c, "  mov byte ptr [rbp-48], '-'\n");
    emit(c, "  mov rdi, 1\n  lea rsi, [rbp-48]\n  mov rdx, 1\n  mov rax, 1\n  syscall\n");
    emit(c, "  pop rax\n");
    emit(c, "  lea rsi, [rbp-40]\n  add rsi, 19\n");
    emit(c, ".L_print_pos_%d:\n", lbl);
    emit(c, "  cmp rax, 0\n");
    emit(c, "  jne .L_print_loop_%d\n", lbl);
    emit(c, "  dec rsi\n  mov byte ptr [rsi], '0'\n");
    emit(c, "  jmp .L_print_done_%d\n", lbl);
    emit(c, ".L_print_loop_%d:\n", lbl);
    emit(c, "  xor rdx, rdx\n  div r8\n  add dl, '0'\n  dec rsi\n");
    emit(c, "  mov byte ptr [rsi], dl\n  cmp rax, 0\n");
    emit(c, "  jne .L_print_loop_%d\n", lbl);
    emit(c, ".L_print_done_%d:\n", lbl);
    emit(c, "  lea r10, [rbp-40]\n  add r10, 19\n");
    emit(c, "  sub r10, rsi\n  mov rdx, r10\n");
    emit(c, "  mov rdi, 1\n  mov rax, 1\n  syscall\n");
    emit(c, "  mov byte ptr [rbp-48], 10\n");
    emit(c, "  mov rdi, 1\n  lea rsi, [rbp-48]\n  mov rdx, 1\n  mov rax, 1\n  syscall\n");
    emit(c, "  mov rsp, rbp\n  pop rbp\n  ret\n");
}

static void emit_print_float(Codegen *c) {
    int L = new_label(c);
    emit(c, "\n# print_float(rdi) -> void (reads ieee754 bits from rdi, uses libc printf)\n");
    emit(c, ".globl print_float\n.type print_float, @function\nprint_float:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n  sub rsp, 16\n");
    emit(c, "  movq xmm0, rdi\n");
    emit(c, "  lea rdi, [rip + .L_float_fmt_%d]\n", L);
    emit(c, "  mov rax, 1\n  call printf\n");
    emit(c, "  mov rsp, rbp\n  pop rbp\n  ret\n");
    emit(c, ".L_float_fmt_%d: .asciz \"%%.6f\\n\"\n", L);
}

static void emit_sleep(Codegen *c) {
    emit(c, "\n# sleep(rdi)\n");
    emit(c, ".globl sleep\n.type sleep, @function\nsleep:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n  sub rsp, 32\n");
    emit(c, "  mov qword ptr [rbp-16], rdi\n  mov qword ptr [rbp-8], 0\n");
    emit(c, "  lea rdi, [rbp-16]\n  xor rsi, rsi\n  mov rax, 35\n  syscall\n");
    emit(c, "  mov rsp, rbp\n  pop rbp\n  ret\n");
}

static void emit_time_ms(Codegen *c) {
    emit(c, "\n# time_ms() -> rax\n");
    emit(c, ".globl time_ms\n.type time_ms, @function\ntime_ms:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n  sub rsp, 32\n");
    emit(c, "  mov rdi, 1\n");
    emit(c, "  lea rsi, [rbp-16]\n");
    emit(c, "  mov rax, 228\n  syscall\n");
    emit(c, "  mov rax, qword ptr [rbp-16]\n");
    emit(c, "  imul rax, 1000\n");
    emit(c, "  mov r8, rax\n");
    emit(c, "  mov rax, qword ptr [rbp-8]\n");
    emit(c, "  mov rcx, 1000000\n  mov rdx, 0\n  div rcx\n");
    emit(c, "  add rax, r8\n");
    emit(c, "  mov rsp, rbp\n  pop rbp\n  ret\n");
}

static void emit_print_str(Codegen *c) {
    emit(c, "\n# print_str(rdi=ptr, rsi=len) -> void\n");
    emit(c, ".globl print_str\n");
    emit(c, ".type print_str, @function\n");
    emit(c, "print_str:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n");
    emit(c, "  mov rdx, rsi\n  mov rsi, rdi\n");
    emit(c, "  mov rdi, 1\n  mov rax, 1\n  syscall\n");
    emit(c, "  mov rsp, rbp\n  pop rbp\n  ret\n");
}

static void emit_read_int(Codegen *c) {
    int L = new_label(c);
    emit(c, "\n# read_int() -> rax (supports negative numbers)\n");
    emit(c, ".globl read_int\n");
    emit(c, ".type read_int, @function\n");
    emit(c, "read_int:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n  sub rsp, 16\n");
    emit(c, "  xor r8, r8\n");
    emit(c, "  xor r9, r9\n");
    emit(c, "  xor rax, rax\n  mov rdi, 0\n");
    emit(c, "  lea rsi, [rbp-1]\n  mov rdx, 1\n  syscall\n");
    emit(c, "  cmp rax, 1\n  jne .L_rd_done_%d\n", L);
    emit(c, "  mov al, byte ptr [rbp-1]\n");
    emit(c, "  cmp al, '-'\n  jne .L_rd_digit_%d\n", L);
    emit(c, "  mov r9, 1\n");
    emit(c, ".L_rd_read_%d:\n", L);
    emit(c, "  xor rax, rax\n  mov rdi, 0\n");
    emit(c, "  lea rsi, [rbp-2]\n  mov rdx, 1\n  syscall\n");
    emit(c, "  cmp rax, 1\n  jne .L_rd_done_%d\n", L);
    emit(c, "  mov al, byte ptr [rbp-2]\n");
    emit(c, "  jmp .L_rd_check_%d\n", L);
    emit(c, ".L_rd_loop_%d:\n", L);
    emit(c, "  xor rax, rax\n  mov rdi, 0\n");
    emit(c, "  lea rsi, [rbp-2]\n  mov rdx, 1\n  syscall\n");
    emit(c, "  cmp rax, 1\n  jne .L_rd_done_%d\n", L);
    emit(c, "  mov al, byte ptr [rbp-2]\n");
    emit(c, ".L_rd_check_%d:\n", L);
    emit(c, "  cmp al, '0'\n  jb .L_rd_done_%d\n", L);
    emit(c, "  cmp al, '9'\n  ja .L_rd_done_%d\n", L);
    emit(c, "  sub al, '0'\n  movzx rcx, al\n");
    emit(c, "  imul r8, r8, 10\n  add r8, rcx\n");
    emit(c, "  jmp .L_rd_loop_%d\n", L);
    emit(c, ".L_rd_digit_%d:\n", L);
    emit(c, "  cmp al, '0'\n  jb .L_rd_done_%d\n", L);
    emit(c, "  cmp al, '9'\n  ja .L_rd_done_%d\n", L);
    emit(c, "  sub al, '0'\n  movzx rcx, al\n");
    emit(c, "  imul r8, r8, 10\n  add r8, rcx\n");
    emit(c, "  jmp .L_rd_loop_%d\n", L);
    emit(c, ".L_rd_done_%d:\n", L);
    emit(c, "  mov rax, r8\n");
    emit(c, "  test r9, r9\n  je .L_rd_pos_%d\n", L);
    emit(c, "  neg rax\n");
    emit(c, ".L_rd_pos_%d:\n", L);
    emit(c, "  mov rsp, rbp\n  pop rbp\n  ret\n");
}

static void emit_input(Codegen *c) {
    int L = new_label(c);
    emit(c, "\n# input(rdi=prompt) -> rax=buffer (null-terminated)\n");
    emit(c, ".globl input\n.type input, @function\ninput:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n  push rbx\n  push r12\n  sub rsp, 16\n");
    emit(c, "  mov qword ptr [rbp-32], rdi\n");
    emit(c, "  mov rdi, qword ptr [rbp-32]\n");
    emit(c, "  xor rax, rax\n  mov rcx, -1\n");
    emit(c, "  repne scasb\n  not rcx\n  dec rcx\n");
    emit(c, "  mov rsi, rcx\n");
    emit(c, "  mov rdi, qword ptr [rbp-32]\n");
    emit(c, "  call print_str\n");
    emit(c, "  lea rbx, [rip + input_buf]\n");
    emit(c, "  xor r12, r12\n");
    emit(c, ".L_input_loop_%d:\n", L);
    emit(c, "  xor rax, rax\n  mov rdi, 0\n");
    emit(c, "  lea rsi, [rbx + r12]\n  mov rdx, 1\n  syscall\n");
    emit(c, "  cmp rax, 1\n  jne .L_input_done_%d\n", L);
    emit(c, "  mov al, byte ptr [rbx + r12]\n");
    emit(c, "  cmp al, 10\n  je .L_input_done_%d\n", L);
    emit(c, "  inc r12\n  cmp r12, 1023\n  jb .L_input_loop_%d\n", L);
    emit(c, ".L_input_done_%d:\n", L);
    emit(c, "  mov byte ptr [rbx + r12], 0\n");
    emit(c, "  mov rax, rbx\n");
    emit(c, "  add rsp, 16\n  pop r12\n  pop rbx\n  pop rbp\n  ret\n");
}

static void emit_strlen(Codegen *c) {
    emit(c, "\n# strlen(rdi=str) -> rax=len\n");
    emit(c, ".globl strlen\n.type strlen, @function\nstrlen:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n");
    emit(c, "  xor rax, rax\n  mov rcx, -1\n");
    emit(c, "  repne scasb\n  not rcx\n  dec rcx\n");
    emit(c, "  mov rax, rcx\n");
    emit(c, "  pop rbp\n  ret\n");
}

static void emit_inspect(Codegen *c) {
    int L = new_label(c);
    emit(c, "\n# inspect(rdi) -> rax (prints \"inspect: N\\n\", returns value)\n");
    emit(c, ".globl inspect\n.type inspect, @function\ninspect:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n  sub rsp, 16\n");
    emit(c, "  mov qword ptr [rbp-8], rdi\n");
    emit(c, "  lea rdi, [rip + .Linsp_str_%d]\n", L);
    emit(c, "  mov rsi, 9\n  call print_str\n");
    emit(c, "  mov rdi, qword ptr [rbp-8]\n  call print_int\n");
    emit(c, "  mov rax, qword ptr [rbp-8]\n");
    emit(c, "  mov rsp, rbp\n  pop rbp\n  ret\n");
    emit(c, ".Linsp_str_%d: .asciz \"inspect: \"\n", L);
}

static void emit_assert(Codegen *c) {
    int L = new_label(c);
    emit(c, "\n# assert(rdi) -> void (aborts if rdi == 0)\n");
    emit(c, ".globl assert\n.type assert, @function\nassert:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n");
    emit(c, "  test rdi, rdi\n  jnz .L_assert_ok_%d\n", L);
    emit(c, "  lea rdi, [rip + .Lassert_str_%d]\n", L);
    emit(c, "  mov rsi, 17\n  call print_str\n");
    emit(c, "  mov rdi, 1\n  mov rax, 60\n  syscall\n");
    emit(c, ".L_assert_ok_%d:\n  mov rsp, rbp\n  pop rbp\n  ret\n", L);
    emit(c, ".Lassert_str_%d: .asciz \"assertion failed\\n\"\n", L);
}

static void emit_clear_screen(Codegen *c) {
    emit(c, "\n# clear_screen() -> void\n");
    emit(c, ".globl clear_screen\n.type clear_screen, @function\nclear_screen:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n");
    emit(c, "  mov rax, 1\n  mov rdi, 1\n");
    emit(c, "  lea rsi, [rip + .L_ansi_cls]\n  mov rdx, 7\n  syscall\n");
    emit(c, "  pop rbp\n  ret\n");
}

static void emit_reset_attr(Codegen *c) {
    emit(c, "\n# reset_attr() -> void\n");
    emit(c, ".globl reset_attr\n.type reset_attr, @function\nreset_attr:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n");
    emit(c, "  mov rax, 1\n  mov rdi, 1\n");
    emit(c, "  lea rsi, [rip + .L_ansi_reset]\n  mov rdx, 4\n  syscall\n");
    emit(c, "  pop rbp\n  ret\n");
}

static void emit_hide_cursor(Codegen *c) {
    emit(c, "\n# hide_cursor() -> void\n");
    emit(c, ".globl hide_cursor\n.type hide_cursor, @function\nhide_cursor:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n");
    emit(c, "  mov rax, 1\n  mov rdi, 1\n");
    emit(c, "  lea rsi, [rip + .L_ansi_hide]\n  mov rdx, 6\n  syscall\n");
    emit(c, "  pop rbp\n  ret\n");
}

static void emit_show_cursor(Codegen *c) {
    emit(c, "\n# show_cursor() -> void\n");
    emit(c, ".globl show_cursor\n.type show_cursor, @function\nshow_cursor:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n");
    emit(c, "  mov rax, 1\n  mov rdi, 1\n");
    emit(c, "  lea rsi, [rip + .L_ansi_show]\n  mov rdx, 6\n  syscall\n");
    emit(c, "  pop rbp\n  ret\n");
}

static void emit_get_frame_ptr(Codegen *c) {
    emit(c, "\n# get_frame_ptr(rdi=base, rsi=idx, rdx=fsize) -> rax=ptr\n");
    emit(c, ".globl get_frame_ptr\n.type get_frame_ptr, @function\nget_frame_ptr:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n");
    emit(c, "  mov rax, rsi\n  mul rdx\n  add rax, rdi\n");
    emit(c, "  pop rbp\n  ret\n");
}

static void emit_set_fg(Codegen *c) {
    int L = new_label(c);
    emit(c, "\n# set_fg(rdi=color) -> void (256-color foreground)\n");
    emit(c, ".globl set_fg\n.type set_fg, @function\nset_fg:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n  push rbx\n  sub rsp, 32\n");
    emit(c, "  mov qword ptr [rbp-8], rdi\n");
    emit(c, "  mov rax, 1\n  mov rdi, 1\n  lea rsi, [rip + .L_ansi_fg]\n");
    emit(c, "  mov rdx, 7\n  syscall\n");
    emit(c, "  mov rax, qword ptr [rbp-8]\n");
    emit(c, "  lea rsi, [rbp-32]\n  add rsi, 19\n");
    emit(c, "  mov rcx, 10\n  cmp rax, 0\n");
    emit(c, "  jne .L_fg_loop_%d\n", L);
    emit(c, "  dec rsi\n  mov byte ptr [rsi], '0'\n");
    emit(c, "  jmp .L_fg_done_%d\n", L);
    emit(c, ".L_fg_loop_%d:\n", L);
    emit(c, "  xor rdx, rdx\n  div rcx\n");
    emit(c, "  add dl, '0'\n  dec rsi\n  mov byte ptr [rsi], dl\n");
    emit(c, "  cmp rax, 0\n  jne .L_fg_loop_%d\n", L);
    emit(c, ".L_fg_done_%d:\n", L);
    emit(c, "  lea rbx, [rbp-32]\n  add rbx, 19\n");
    emit(c, "  sub rbx, rsi\n  mov rdx, rbx\n");
    emit(c, "  mov rdi, 1\n  mov rax, 1\n  syscall\n");
    emit(c, "  mov rax, 1\n  mov rdi, 1\n  lea rsi, [rip + .L_ansi_m]\n");
    emit(c, "  mov rdx, 1\n  syscall\n");
    emit(c, "  mov rsp, rbp\n  pop rbx\n  pop rbp\n  ret\n");
}

static void emit_set_bg(Codegen *c) {
    int L = new_label(c);
    emit(c, "\n# set_bg(rdi=color) -> void (256-color background)\n");
    emit(c, ".globl set_bg\n.type set_bg, @function\nset_bg:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n  push rbx\n  sub rsp, 32\n");
    emit(c, "  mov qword ptr [rbp-8], rdi\n");
    emit(c, "  mov rax, 1\n  mov rdi, 1\n  lea rsi, [rip + .L_ansi_bg]\n");
    emit(c, "  mov rdx, 7\n  syscall\n");
    emit(c, "  mov rax, qword ptr [rbp-8]\n");
    emit(c, "  lea rsi, [rbp-32]\n  add rsi, 19\n");
    emit(c, "  mov rcx, 10\n  cmp rax, 0\n");
    emit(c, "  jne .L_bg_loop_%d\n", L);
    emit(c, "  dec rsi\n  mov byte ptr [rsi], '0'\n");
    emit(c, "  jmp .L_bg_done_%d\n", L);
    emit(c, ".L_bg_loop_%d:\n", L);
    emit(c, "  xor rdx, rdx\n  div rcx\n");
    emit(c, "  add dl, '0'\n  dec rsi\n  mov byte ptr [rsi], dl\n");
    emit(c, "  cmp rax, 0\n  jne .L_bg_loop_%d\n", L);
    emit(c, ".L_bg_done_%d:\n", L);
    emit(c, "  lea rbx, [rbp-32]\n  add rbx, 19\n");
    emit(c, "  sub rbx, rsi\n  mov rdx, rbx\n");
    emit(c, "  mov rdi, 1\n  mov rax, 1\n  syscall\n");
    emit(c, "  mov rax, 1\n  mov rdi, 1\n  lea rsi, [rip + .L_ansi_m]\n");
    emit(c, "  mov rdx, 1\n  syscall\n");
    emit(c, "  mov rsp, rbp\n  pop rbx\n  pop rbp\n  ret\n");
}

static void emit_calc_expr(Codegen *c) {
    int L = new_label(c);
    emit(c, "\n# calc_expr() -> rax\n");
    emit(c, ".globl calc_expr\n");
    emit(c, ".type calc_expr, @function\n");
    emit(c, "calc_expr:\n");
    emit(c, "  push rbp\n  push rbx\n  mov rbp, rsp\n  sub rsp, 4096\n");
    emit(c, "  xor rbx, rbx\n");
    emit(c, ".L_ce_read_%d:\n", L);
    emit(c, "  xor rax, rax\n  mov rdi, 0\n");
    emit(c, "  lea rsi, [rbp-4096]\n  add rsi, rbx\n  mov rdx, 1\n  syscall\n");
    emit(c, "  cmp rax, 1\n  jne .L_ce_eof_%d\n", L);
    emit(c, "  mov al, byte ptr [rbp + rbx - 4096]\n");
    emit(c, "  cmp al, 10\n  je .L_ce_rdone_%d\n", L);
    emit(c, "  inc rbx\n  cmp rbx, 4095\n  jb .L_ce_read_%d\n", L);
    emit(c, ".L_ce_eof_%d:\n", L);
    emit(c, "  test rbx, rbx\n  jz .L_ce_exit_%d\n", L);
    emit(c, "  jmp .L_ce_rdone_%d\n", L);
    emit(c, ".L_ce_rdone_%d:\n", L);
    emit(c, "  mov byte ptr [rbp-4096+rbx], 0\n");
    emit(c, "  test rbx, rbx\n  jz .L_ce_empty_%d\n", L);
    emit(c, "  lea rsi, [rbp-4096]\n");
    emit(c, ".L_ce_skip1_%d:\n", L);
    emit(c, "  mov al, byte ptr [rsi]\n  cmp al, ' '\n");
    emit(c, "  jne .L_ce_got1_%d\n  inc rsi\n  jmp .L_ce_skip1_%d\n", L, L);
    emit(c, ".L_ce_got1_%d:\n", L);
    emit(c, "  xor r8, r8\n");
    emit(c, ".L_ce_num1_%d:\n", L);
    emit(c, "  mov al, byte ptr [rsi]\n  cmp al, '0'\n  jb .L_ce_num1d_%d\n", L);
    emit(c, "  cmp al, '9'\n  ja .L_ce_num1d_%d\n", L);
    emit(c, "  sub al, '0'\n  movzx r9, al\n  imul r8, r8, 10\n  add r8, r9\n");
    emit(c, "  inc rsi\n  jmp .L_ce_num1_%d\n", L);
    emit(c, ".L_ce_num1d_%d:\n", L);
    emit(c, "  xor r9, r9\n");
    emit(c, ".L_ce_main_%d:\n", L);
    emit(c, ".L_ce_skip2_%d:\n", L);
    emit(c, "  mov al, byte ptr [rsi]\n  cmp al, ' '\n");
    emit(c, "  jne .L_ce_gotop_%d\n  inc rsi\n  jmp .L_ce_skip2_%d\n", L, L);
    emit(c, ".L_ce_gotop_%d:\n", L);
    emit(c, "  cmp al, 0\n  je .L_ce_done_%d\n", L);
    emit(c, "  mov r11b, al\n  inc rsi\n");
    emit(c, ".L_ce_skip3_%d:\n", L);
    emit(c, "  mov al, byte ptr [rsi]\n  cmp al, ' '\n");
    emit(c, "  jne .L_ce_gotnum_%d\n  inc rsi\n  jmp .L_ce_skip3_%d\n", L, L);
    emit(c, ".L_ce_gotnum_%d:\n", L);
    emit(c, "  xor r10, r10\n");
    emit(c, ".L_ce_num2_%d:\n", L);
    emit(c, "  mov al, byte ptr [rsi]\n  cmp al, '0'\n  jb .L_ce_num2d_%d\n", L);
    emit(c, "  cmp al, '9'\n  ja .L_ce_num2d_%d\n", L);
    emit(c, "  sub al, '0'\n  movzx rcx, al\n  imul r10, r10, 10\n  add r10, rcx\n");
    emit(c, "  inc rsi\n  jmp .L_ce_num2_%d\n", L);
    emit(c, ".L_ce_num2d_%d:\n", L);
    emit(c, "  cmp r11b, '+'\n  jne .L_ce_cm_%d\n", L);
    emit(c, "  add r9, r8\n  mov r8, r10\n  jmp .L_ce_main_%d\n", L);
    emit(c, ".L_ce_cm_%d:\n", L);
    emit(c, "  cmp r11b, '-'\n  jne .L_ce_cx_%d\n", L);
    emit(c, "  add r9, r8\n  neg r10\n  mov r8, r10\n  jmp .L_ce_main_%d\n", L);
    emit(c, ".L_ce_cx_%d:\n", L);
    emit(c, "  cmp r11b, '*'\n  je .L_ce_mul_%d\n", L);
    emit(c, "  cmp r11b, 'x'\n  je .L_ce_mul_%d\n", L);
    emit(c, "  cmp r11b, 'X'\n  je .L_ce_mul_%d\n", L);
    emit(c, "  cmp r11b, ':'\n  je .L_ce_div_%d\n", L);
    emit(c, "  cmp r11b, '/'\n  jne .L_ce_main_%d\n", L);
    emit(c, ".L_ce_div_%d:\n", L);
    emit(c, "  mov rax, r8\n  cqo\n  idiv r10\n  mov r8, rax\n");
    emit(c, "  jmp .L_ce_main_%d\n", L);
    emit(c, ".L_ce_mul_%d:\n", L);
    emit(c, "  imul r8, r10\n  jmp .L_ce_main_%d\n", L);
    emit(c, ".L_ce_done_%d:\n", L);
    emit(c, "  add r9, r8\n  mov rax, r9\n");
    emit(c, "  jmp .L_ce_ret_%d\n", L);
    emit(c, ".L_ce_empty_%d:\n", L);
    emit(c, "  xor rax, rax\n");
    emit(c, ".L_ce_ret_%d:\n", L);
    emit(c, "  mov rsp, rbp\n  pop rbx\n  pop rbp\n  ret\n");
    emit(c, ".L_ce_exit_%d:\n", L);
    emit(c, "  mov rsp, rbp\n  pop rbx\n  pop rbp\n");
    emit(c, "  mov rax, 60\n  xor rdi, rdi\n  syscall\n");
}

static void emit_string_clone(Codegen *c) {
    emit(c, "\n# string_clone(rdi=src, rsi=len) -> rax=ptr (allocates + copies)\n");
    emit(c, ".globl string_clone\n.type string_clone, @function\nstring_clone:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n  push rbx\n  sub rsp, 32\n");
    emit(c, "  mov qword ptr [rbp-24], rdi\n");  /* src */
    emit(c, "  mov qword ptr [rbp-32], rsi\n");  /* len */
    /* allocate: len + 1 bytes via malloc */
    emit(c, "  mov rdi, qword ptr [rbp-32]\n");
    emit(c, "  inc rdi\n");  /* len+1 */
    emit(c, "  call malloc\n");
    emit(c, "  mov rbx, rax\n");
    /* copy */
    emit(c, "  mov rdi, rbx\n");
    emit(c, "  mov rsi, qword ptr [rbp-24]\n");
    emit(c, "  mov rcx, qword ptr [rbp-32]\n");
    emit(c, "  inc rcx\n");  /* include null terminator */
    emit(c, "  rep movsb\n");
    emit(c, "  mov rax, rbx\n");
    emit(c, "  add rsp, 32\n  pop rbx\n  pop rbp\n  ret\n");
}

static void emit_string_concat(Codegen *c) {
    emit(c, "\n# string_concat(rdi=a, rsi=b) -> rax=ptr\n");
    emit(c, ".globl string_concat\n.type string_concat, @function\nstring_concat:\n");
    emit(c, "  push rbp\n  mov rbp, rsp\n  push rbx\n  push r12\n  push r13\n  sub rsp, 32\n");
    emit(c, "  mov qword ptr [rbp-48], rdi\n");  /* a */
    emit(c, "  mov qword ptr [rbp-56], rsi\n");  /* b */
    /* strlen of a and b */
    emit(c, "  mov rdi, qword ptr [rbp-48]\n");
    emit(c, "  xor rax, rax\n  mov rcx, -1\n  repne scasb\n  not rcx\n  dec rcx\n");
    emit(c, "  mov r12, rcx\n");  /* len_a */
    emit(c, "  mov rdi, qword ptr [rbp-56]\n");
    emit(c, "  xor rax, rax\n  mov rcx, -1\n  repne scasb\n  not rcx\n  dec rcx\n");
    emit(c, "  mov r13, rcx\n");  /* len_b */
    /* allocate: len_a + len_b + 1 via malloc */
    emit(c, "  mov rdi, r12\n");
    emit(c, "  add rdi, r13\n");
    emit(c, "  inc rdi\n");  /* len_a + len_b + 1 */
    emit(c, "  call malloc\n");
    emit(c, "  mov rbx, rax\n");
    /* copy a */
    emit(c, "  mov rdi, rbx\n");
    emit(c, "  mov rsi, qword ptr [rbp-48]\n");
    emit(c, "  mov rcx, r12\n");
    emit(c, "  rep movsb\n");
    /* copy b */
    emit(c, "  mov rsi, qword ptr [rbp-56]\n");
    emit(c, "  mov rcx, r13\n");
    emit(c, "  rep movsb\n");
    /* null terminate */
    emit(c, "  mov byte ptr [rdi], 0\n");
    emit(c, "  mov rax, rbx\n");
    emit(c, "  add rsp, 32\n  pop r13\n  pop r12\n  pop rbx\n  pop rbp\n  ret\n");
    /* __string_concat alias for str + str lowering */
    emit(c, ".globl __string_concat\n.set __string_concat, string_concat\n");
}

static void gen_expr(Codegen *c, Node *n);
static void gen_stmt(Codegen *c, Node *n);
static void emit_fn_return(Codegen *c);
static int find_cg_struct(Codegen *c, const char *name);
static void collect_structs(Codegen *c, Node *prog);

static int find_enum_variant_tag(Codegen *c, const char *variant) {
    for (int i = 0; i < c->enum_count; i++)
        for (int j = 0; j < c->enums[i].variant_count; j++)
            if (strcmp(c->enums[i].variants[j], variant) == 0)
                return j;
    return -1; /* not found */
}

static int find_enum_payload_size(Codegen *c, const char *variant, int field_idx) {
    for (int i = 0; i < c->enum_count; i++)
        for (int j = 0; j < c->enums[i].variant_count; j++)
            if (strcmp(c->enums[i].variants[j], variant) == 0) {
                if (field_idx < c->enums[i].payload_counts[j])
                    return c->enums[i].payload_sizes[j][field_idx];
                return 8;
            }
    return 8;
}

static int expr_is_float(SymTab *s, Node *n) {
    if (!n) return 0;
    switch (n->type) {
    case NODE_FLOAT: return 1;
    case NODE_IDENT: {
        int idx = sym_find_idx(s, n->ident);
        return (idx >= 0) ? s->is_float[idx] : 0;
    }
    case NODE_BINARY:
        return expr_is_float(s, n->binary.left) || expr_is_float(s, n->binary.right);
    case NODE_UNARY:
        return expr_is_float(s, n->unary.operand);
    default: return 0;
    }
}

static int expr_is_unsigned(SymTab *s, Node *n) {
    if (!n) return 0;
    switch (n->type) {
    case NODE_IDENT: {
        int idx = sym_find_idx(s, n->ident);
        if (idx < 0) return 0;
        int vt = s->var_type[idx];
        return vt == TYPE_U8 || vt == TYPE_U16 || vt == TYPE_U32 || vt == TYPE_U64 || vt == TYPE_U128 || vt == TYPE_USIZE;
    }
    case NODE_BINARY:
        return expr_is_unsigned(s, n->binary.left) || expr_is_unsigned(s, n->binary.right);
    case NODE_UNARY:
        return expr_is_unsigned(s, n->unary.operand);
    default: return 0;
    }
}

static int expr_is_f32(SymTab *s, Node *n) {
    if (!n) return 0;
    switch (n->type) {
    case NODE_IDENT: {
        int idx = sym_find_idx(s, n->ident);
        return (idx >= 0) ? (s->var_type[idx] == TYPE_F32) : 0;
    }
    case NODE_BINARY:
        return expr_is_f32(s, n->binary.left) || expr_is_f32(s, n->binary.right);
    case NODE_UNARY:
        return expr_is_f32(s, n->unary.operand);
    default: return 0;
    }
}

static void emit_trunc(Codegen *c, int vt) {
    switch (vt) {
        case TYPE_I8: emit(c, "  movsx rax, al\n"); break;
        case TYPE_U8: emit(c, "  movzx rax, al\n"); break;
        case TYPE_I16: emit(c, "  movsx rax, ax\n"); break;
        case TYPE_U16: emit(c, "  movzx rax, ax\n"); break;
        case TYPE_I32: emit(c, "  movsxd rax, eax\n"); break;
        case TYPE_U32:
        case TYPE_F32: emit(c, "  mov eax, eax\n"); break;
    }
}

static void gen_expr(Codegen *c, Node *n) {
    if (!n) { emit(c, "  xor rax, rax\n"); return; }
    switch (n->type) {
    case NODE_INT: emit(c, "  mov rax, %lld\n", n->int_val); break;
    case NODE_FLOAT: {
        uint64_t bits;
        memcpy(&bits, &n->float_val, 8);
        emit(c, "  mov rax, 0x%llx\n", (unsigned long long)bits);
        break;
    }
    case NODE_BOOL: emit(c, "  mov rax, %d\n", n->bool_val ? 1 : 0); break;
    case NODE_STR: {
        int id = get_str_id(c, n->str_val);
        /* allocate string via string_clone */
        emit(c, "  lea rdi, [rip + .L_str_%d]\n", id);
        emit(c, "  mov rsi, %zu\n", strlen(n->str_val));
        emit(c, "  call string_clone\n");
        break;
    }
    case NODE_IDENT: {
        int off = sym_find(&c->sym, n->ident);
        if (off < 0) { emit(c, "  xor rax, rax\n"); break; }
        int idx = sym_find_idx(&c->sym, n->ident);
        int vt = (idx >= 0) ? c->sym.var_type[idx] : TYPE_I64;
        if (vt == TYPE_F32) {
            emit(c, "  mov eax, dword ptr [rbp - %d]\n", off);
        } else if (idx >= 0 && c->sym.regs[idx] >= 0) {
            emit(c, "  mov rax, %s\n", var_reg_names[c->sym.regs[idx]]);
        } else {
            emit(c, "  mov rax, qword ptr [rbp - %d]\n", off);
        }
        break;
    }
    case NODE_UNARY:
        gen_expr(c, n->unary.operand);
        if (n->unary.op == 0) emit(c, "  test rax, rax\n  setz al\n  movzx rax, al\n");
        else if (n->unary.op == 1) emit(c, "  neg rax\n");
        else if (n->unary.op == 2) emit(c, "  not rax\n");
        /* op 3 = move: no-op, op 4 = cast: no-op */
        break;
    case NODE_BINARY: {
        int op = n->binary.op;
        /* check if either operand is float (literal or variable) */
        int is_float = (n->binary.left && n->binary.right) &&
                        (expr_is_float(&c->sym, n->binary.left) ||
                         expr_is_float(&c->sym, n->binary.right));
        int is_u = expr_is_unsigned(&c->sym, n->binary.left) || expr_is_unsigned(&c->sym, n->binary.right);
        int is_f32 = expr_is_f32(&c->sym, n->binary.left) || expr_is_f32(&c->sym, n->binary.right);
        const char *fsuf = is_f32 ? "ss" : "sd";
        if (is_float && op <= 3) {
            /* float arithmetic: use SSE */
            gen_expr(c, n->binary.left);
            emit(c, "  push rax\n");
            gen_expr(c, n->binary.right);
            emit(c, "  mov rcx, rax\n  pop rdx\n");
            emit(c, "  movq xmm0, rdx\n  movq xmm1, rcx\n");
            if (op == 0) emit(c, "  add%s xmm0, xmm1\n", fsuf);
            else if (op == 1) emit(c, "  sub%s xmm0, xmm1\n", fsuf);
            else if (op == 2) emit(c, "  mul%s xmm0, xmm1\n", fsuf);
            else if (op == 3) emit(c, "  div%s xmm0, xmm1\n", fsuf);
            emit(c, "  movq rax, xmm0\n");
            break;
        }
        /* Optimize: avoid push/pop when left operand is in a register */
        int binop_optimized = 0;
        if (!is_float && op != 3 && op != 4 && op != 11 && op != 12 && op != 18) {
            int lr = -1, rr = -1;
            if (n->binary.left && n->binary.left->type == NODE_IDENT) {
                int idx = sym_find_idx(&c->sym, n->binary.left->ident);
                if (idx >= 0 && c->sym.regs[idx] >= 0) lr = c->sym.regs[idx];
            }
            if (n->binary.right && n->binary.right->type == NODE_IDENT) {
                int idx = sym_find_idx(&c->sym, n->binary.right->ident);
                if (idx >= 0 && c->sym.regs[idx] >= 0) rr = c->sym.regs[idx];
            }
            if (lr >= 0) {
                int rconst = n->binary.right && (n->binary.right->type == NODE_INT || n->binary.right->type == NODE_BOOL);
                if (rconst) {
                    long long v = n->binary.right->type == NODE_INT ? n->binary.right->int_val
                                   : (long long)n->binary.right->bool_val;
                    if (op == 0) {
                        if (v >= 0) emit(c, "  lea rax, [%s + %lld]\n", var_reg_names[lr], v);
                        else emit(c, "  mov rax, %s\n  sub rax, %lld\n", var_reg_names[lr], -v);
                    } else if (op == 1) {
                        emit(c, "  mov rax, %s\n  sub rax, %lld\n", var_reg_names[lr], v);
                    } else if (op == 2) {
                        emit(c, "  mov rax, %s\n  imul rax, %lld\n", var_reg_names[lr], v);
                    } else if (op >= 5 && op <= 10) {
                        const char *ccodes[] = {"sete","setne","setl","setle","setg","setge"};
                        emit(c, "  xor rax, rax\n  cmp %s, %lld\n  %s al\n",
                             var_reg_names[lr], v, ccodes[op-5]);
                    } else if (op == 13) emit(c, "  mov rax, %s\n  or rax, %lld\n", var_reg_names[lr], v);
                    else if (op == 14) emit(c, "  mov rax, %s\n  xor rax, %lld\n", var_reg_names[lr], v);
                    else if (op == 15) emit(c, "  mov rax, %s\n  and rax, %lld\n", var_reg_names[lr], v);
                    else if (op == 16) { emit(c, "  mov rax, %s\n  mov rcx, %lld\n  shl rax, cl\n", var_reg_names[lr], v); }
                    else if (op == 17) { emit(c, "  mov rax, %s\n  mov rcx, %lld\n  %s rax, cl\n", var_reg_names[lr], v, is_u ? "shr" : "sar"); }
                    else goto binop_fallback;
                    binop_optimized = 1;
                } else if (rr >= 0) {
                    if (op == 0) emit(c, "  mov rax, %s\n  add rax, %s\n", var_reg_names[lr], var_reg_names[rr]);
                    else if (op == 1) emit(c, "  mov rax, %s\n  sub rax, %s\n", var_reg_names[lr], var_reg_names[rr]);
                    else if (op == 2) emit(c, "  mov rax, %s\n  imul rax, %s\n", var_reg_names[lr], var_reg_names[rr]);
                    else if (op >= 5 && op <= 10) {
                        const char *ccodes[] = {"sete","setne","setl","setle","setg","setge"};
                        emit(c, "  xor rax, rax\n  cmp %s, %s\n  %s al\n",
                             var_reg_names[lr], var_reg_names[rr], ccodes[op-5]);
                    } else if (op == 13) emit(c, "  mov rax, %s\n  or rax, %s\n", var_reg_names[lr], var_reg_names[rr]);
                    else if (op == 14) emit(c, "  mov rax, %s\n  xor rax, %s\n", var_reg_names[lr], var_reg_names[rr]);
                    else if (op == 15) emit(c, "  mov rax, %s\n  and rax, %s\n", var_reg_names[lr], var_reg_names[rr]);
                    else if (op == 16) { emit(c, "  mov rax, %s\n  mov rcx, %s\n  shl rax, cl\n", var_reg_names[lr], var_reg_names[rr]); }
                    else if (op == 17) { emit(c, "  mov rax, %s\n  mov rcx, %s\n  %s rax, cl\n", var_reg_names[lr], var_reg_names[rr], is_u ? "shr" : "sar"); }
                    else goto binop_fallback;
                    binop_optimized = 1;
                }
            }
        }
        if (!binop_optimized) {
            binop_fallback:
            gen_expr(c, n->binary.left);
            emit(c, "  push rax\n");
            gen_expr(c, n->binary.right);
            emit(c, "  mov rcx, rax\n  pop rdx\n");
            if (op <= 4) {
                if (op == 0) emit(c, "  mov rax, rdx\n  add rax, rcx\n");
                else if (op == 1) emit(c, "  mov rax, rdx\n  sub rax, rcx\n");
                else if (op == 2) {
                    if (is_u) emit(c, "  mov rax, rdx\n  mul rcx\n");
                    else emit(c, "  mov rax, rdx\n  imul rax, rcx\n");
                } else if (op == 3) {
                    if (is_u) emit(c, "  mov rax, rdx\n  xor rdx, rdx\n  div rcx\n");
                    else emit(c, "  mov rax, rdx\n  cqo\n  idiv rcx\n");
                } else if (op == 4) {
                    if (is_u) emit(c, "  mov rax, rdx\n  xor rdx, rdx\n  div rcx\n  mov rax, rdx\n");
                    else emit(c, "  mov rax, rdx\n  cqo\n  idiv rcx\n  mov rax, rdx\n");
                }
            } else if (op <= 10) {
                const char **cc;
                const char *cc_signed[] = {"sete","setne","setl","setle","setg","setge"};
                const char *cc_unsigned[] = {"sete","setne","setb","setbe","seta","setae"};
                cc = is_u ? cc_unsigned : cc_signed;
                emit(c, "  cmp rdx, rcx\n  %s al\n  movzx rax, al\n", cc[op-5]);
            } else if (op == 11) {
                int l = new_label(c);
                emit(c, "  test rdx, rdx\n  je .L_and_false_%d\n", l);
                emit(c, "  test rcx, rcx\n  je .L_and_false_%d\n", l);
                emit(c, "  mov rax, 1\n  jmp .L_and_end_%d\n", l);
                emit(c, ".L_and_false_%d:\n  xor rax, rax\n.L_and_end_%d:\n", l, l);
            } else if (op == 12) {
                int l = new_label(c);
                emit(c, "  test rdx, rdx\n  jne .L_or_true_%d\n", l);
                emit(c, "  test rcx, rcx\n  je .L_or_false_%d\n", l);
                emit(c, ".L_or_true_%d:\n  mov rax, 1\n  jmp .L_or_end_%d\n", l, l);
                emit(c, ".L_or_false_%d:\n  xor rax, rax\n.L_or_end_%d:\n", l, l);
            } else if (op >= 13 && op <= 17) {
                /* bitwise ops */
                if (op == 13) emit(c, "  mov rax, rdx\n  or rax, rcx\n");
                else if (op == 14) emit(c, "  mov rax, rdx\n  xor rax, rcx\n");
                else if (op == 15) emit(c, "  mov rax, rdx\n  and rax, rcx\n");
                else if (op == 16) emit(c, "  mov rax, rdx\n  shl rax, cl\n");
                else if (op == 17) emit(c, "  mov rax, rdx\n  %s rax, cl\n", is_u ? "shr" : "sar");
            } else if (op == 18) {
                int L = new_label(c);
                if (is_float) {
                    /* float power: SSE loop multiply base^exp */
                    emit(c, "  movq xmm0, rdx\n  mov r8, rcx\n");
                    emit(c, "  mov r9, 1\n  cvtsi2sd xmm1, r9\n");
                    emit(c, ".L_fpow_loop_%d:\n", L);
                    emit(c, "  test r8, r8\n  je .L_fpow_done_%d\n", L);
                    emit(c, "  mulsd xmm1, xmm0\n  dec r8\n  jmp .L_fpow_loop_%d\n", L);
                    emit(c, ".L_fpow_done_%d:\n  movq rax, xmm1\n", L);
                } else {
                    /* integer power: loop multiply */
                    emit(c, "  mov rax, rdx\n  mov r8, rcx\n  mov r9, 1\n");
                    emit(c, ".L_pow_loop_%d:\n", L);
                    emit(c, "  test r8, r8\n  je .L_pow_done_%d\n", L);
                    emit(c, "  imul r9, rax\n  dec r8\n  jmp .L_pow_loop_%d\n", L);
                    emit(c, ".L_pow_done_%d:\n  mov rax, r9\n", L);
                }
            }
        }
        break;
    }
    case NODE_CALL: {
        int argc = 0;
        for (Node *a = n->call.args; a; a = a->next) argc++;
        if (argc > 16) { diag_add(c->diag, 0, SEV_ERROR, 0,0,0, "too many arguments in call (max 16)"); exit(1); }
        if (argc > 0) {
            Node *args[16];
            int i = 0;
            for (Node *a = n->call.args; a; a = a->next) args[i++] = a;
            for (int j = i-1; j >= 0; j--) { gen_expr(c, args[j]); emit(c, "  push rax\n"); }
        }
        static const char *arg_regs[] = {"rdi","rsi","rdx","rcx","r8","r9"};
        int n_pop = argc < 6 ? argc : 6;
        for (int i = 0; i < n_pop; i++) emit(c, "  pop %s\n", arg_regs[i]);
        if (n->call.callee->type == NODE_IDENT)
            emit(c, "  call %s\n", n->call.callee->ident);
        else { gen_expr(c, n->call.callee); emit(c, "  call rax\n"); }
        break;
    }
    case NODE_COMPREHENSION: {
        int L = new_label(c);
        int var_off = -1;
        int end_off = -1;
        int idx_off = -1;
        int var_idx = -1;
        if (n->comp.var) {
            var_off = sym_find(&c->sym, n->comp.var);
            if (var_off < 0) var_off = sym_add(&c->sym, n->comp.var);
            var_idx = sym_find_idx(&c->sym, n->comp.var);
            /* reserve slot for end bound after loop variable */
            end_off = c->sym.stack_size + 8;
            c->sym.stack_size += 8;
        }
        if (var_off < 0) { emit(c, "  xor rax, rax\n"); break; }
        emit(c, "  push r12\n  push rbx\n");
        gen_expr(c, n->comp.iter);
        Node *end = n->comp.iter_end;
        if (!end) {
            /* container iteration: iter returns ptr to {count, elem0, elem1, ...} */
            idx_off = c->sym.stack_size + 8;
            c->sym.stack_size += 8;
            emit(c, "  mov rbx, rax\n");
            emit(c, "  mov rax, qword ptr [rbx]\n");
            emit(c, "  mov qword ptr [rbp - %d], rax\n", end_off);
            emit(c, "  mov qword ptr [rbp - %d], 0\n", idx_off);
        } else {
            emit(c, "  mov qword ptr [rbp - %d], rax\n", var_off);
            if (var_idx >= 0 && c->sym.regs[var_idx] >= 0)
                emit(c, "  mov %s, rax\n", var_reg_names[c->sym.regs[var_idx]]);
            gen_expr(c, end);
            emit(c, "  mov qword ptr [rbp - %d], rax\n", end_off);
        }
        emit(c, "  xor r12, r12\n");
        emit(c, ".L_comp_loop_%d:\n", L);
        if (end) {
            emit(c, "  mov rax, qword ptr [rbp - %d]\n", var_off);
        } else {
            emit(c, "  mov rax, qword ptr [rbp - %d]\n", idx_off);
        }
        emit(c, "  mov rcx, qword ptr [rbp - %d]\n", end_off);
        emit(c, "  cmp rax, rcx\n  jge .L_comp_done_%d\n", L);
        if (!end) {
            emit(c, "  mov rax, qword ptr [rbp - %d]\n", idx_off);
            emit(c, "  shl rax, 3\n  add rax, rbx\n");
            emit(c, "  mov rax, qword ptr [rax + 8]\n");
            emit(c, "  mov qword ptr [rbp - %d], rax\n", var_off);
            if (var_idx >= 0 && c->sym.regs[var_idx] >= 0)
                emit(c, "  mov %s, rax\n", var_reg_names[c->sym.regs[var_idx]]);
        }
        if (n->comp.filter) {
            gen_expr(c, n->comp.filter);
            emit(c, "  test rax, rax\n  je .L_comp_skip_%d\n", L);
        }
        gen_expr(c, n->comp.map);
        emit(c, "  mov rdi, rax\n  call print_int\n");
        emit(c, "  inc r12\n");
        if (n->comp.filter) emit(c, ".L_comp_skip_%d:\n", L);
        if (end) {
            emit(c, "  mov rax, qword ptr [rbp - %d]\n", var_off);
        } else {
            emit(c, "  mov rax, qword ptr [rbp - %d]\n", idx_off);
        }
        emit(c, "  inc rax\n");
        if (end) {
            emit(c, "  mov qword ptr [rbp - %d], rax\n", var_off);
            if (var_idx >= 0 && c->sym.regs[var_idx] >= 0)
                emit(c, "  mov %s, rax\n", var_reg_names[c->sym.regs[var_idx]]);
        } else {
            emit(c, "  mov qword ptr [rbp - %d], rax\n", idx_off);
        }
        emit(c, "  jmp .L_comp_loop_%d\n", L);
        emit(c, ".L_comp_done_%d:\n", L);
        if (!end) emit(c, "  xor rbx, rbx\n");
        emit(c, "  mov rax, r12\n  pop rbx\n  pop r12\n");
        /* Reload register variables from stack (comprehension clobbered r12/rbx) */
        for (int ri = 0; ri < c->sym.count; ri++) {
            if (c->sym.regs[ri] >= 0)
                emit(c, "  mov %s, qword ptr [rbp - %d]\n",
                     var_reg_names[c->sym.regs[ri]], c->sym.offsets[ri]);
        }
        break;
    }
    case NODE_STRUCT_LITERAL: {
        int si = find_cg_struct(c, n->struct_literal.name);
        if (si < 0) { emit(c, "  xor rax, rax\n"); break; }
        CStructDef *s = &c->structs[si];
        int argc = 0;
        for (Node *a = n->struct_literal.args; a; a = a->next) argc++;
        if (argc > 16) { diag_add(c->diag, 0, SEV_ERROR, 0,0,0, "too many fields in struct literal (max 16)"); exit(1); }
        Node *args[16];
        int i = 0;
        for (Node *a = n->struct_literal.args; a; a = a->next) args[i++] = a;
        emit(c, "  push rbx\n");
        for (int j = i-1; j >= 0; j--) { gen_expr(c, args[j]); emit(c, "  push rax\n"); }
        emit(c, "  mov rdi, %d\n  call malloc\n", s->total_size > 0 ? s->total_size : 8);
        emit(c, "  mov rbx, rax\n");
        for (int j = 0; j < argc && j < s->field_count; j++) {
            emit(c, "  pop rax\n");
            emit(c, "  mov qword ptr [rbx + %d], rax\n", s->offsets[j]);
        }
        emit(c, "  mov rax, rbx\n");
        emit(c, "  pop rbx\n");
        break;
    }
    case NODE_ENUM_LITERAL: {
        int ei = -1;
        for (int i = 0; i < c->enum_count; i++)
            if (strcmp(c->enums[i].name, n->enum_literal.enum_name) == 0) { ei = i; break; }
        if (ei < 0) { emit(c, "  xor rax, rax\n"); break; }
        CEnumDef *e = &c->enums[ei];
        int tag = -1;
        for (int i = 0; i < e->variant_count; i++)
            if (strcmp(e->variants[i], n->enum_literal.variant) == 0) { tag = i; break; }
        if (tag < 0) { emit(c, "  xor rax, rax\n"); break; }
        /* compute actual payload size + alignment */
        int payload_total = 0;
        int pidx = 0;
        for (Node *p = n->enum_literal.payload; p; p = p->next) {
            int fsize = (pidx < MAX_CG_FIELDS) ? e->payload_sizes[tag][pidx] : 8;
            payload_total += fsize;
            pidx++;
        }
        int alloc_size = payload_total + 8;
        if (alloc_size < 8) alloc_size = 8;
        emit(c, "  push rbx\n");
        emit(c, "  mov rdi, %d\n  call malloc\n", alloc_size);
        emit(c, "  mov rbx, rax\n");
        /* store tag */
        emit(c, "  mov qword ptr [rbx], %d\n", tag);
        /* store payload(s) */
        int poff = 8;
        pidx = 0;
        for (Node *p = n->enum_literal.payload; p; p = p->next) {
            gen_expr(c, p);
            int fsize = (pidx < MAX_CG_FIELDS) ? e->payload_sizes[tag][pidx] : 8;
            if (fsize == 1)
                emit(c, "  mov byte ptr [rbx + %d], al\n", poff);
            else
                emit(c, "  mov qword ptr [rbx + %d], rax\n", poff);
            poff += fsize;
            pidx++;
        }
        emit(c, "  mov rax, rbx\n");
        emit(c, "  pop rbx\n");
        break;
    }
    case NODE_INDEX: {
        gen_expr(c, n->index_expr.obj);
        emit(c, "  push rax\n");
        if (n->index_expr.index->type == NODE_IDENT) {
            const char *field = n->index_expr.index->ident;
            int found = 0;
            for (int si = 0; si < c->struct_count && !found; si++) {
                for (int j = 0; j < c->structs[si].field_count; j++) {
                    if (strcmp(c->structs[si].fields[j], field) == 0) {
                        emit(c, "  pop rax\n");
                        emit(c, "  mov rax, qword ptr [rax + %d]\n", c->structs[si].offsets[j]);
                        found = 1;
                        break;
                    }
                }
            }
            if (!found) {
                emit(c, "  pop rbx\n");
                gen_expr(c, n->index_expr.index);
                emit(c, "  shl rax, 3\n  add rax, rbx\n  mov rax, qword ptr [rax]\n");
            }
        } else {
            emit(c, "  pop rbx\n");
            gen_expr(c, n->index_expr.index);
            emit(c, "  shl rax, 3\n  add rax, rbx\n  mov rax, qword ptr [rax]\n");
        }
        break;
    }
    case NODE_NULLSAFE: {
        int L = new_label(c);
        gen_expr(c, n->index_expr.obj);
        emit(c, "  test rax, rax\n");
        emit(c, "  jz .L_nullsafe_%d\n", L);
        emit(c, "  push rax\n");
        if (n->index_expr.index->type == NODE_IDENT) {
            const char *field = n->index_expr.index->ident;
            int found = 0;
            for (int si = 0; si < c->struct_count && !found; si++) {
                for (int j = 0; j < c->structs[si].field_count; j++) {
                    if (strcmp(c->structs[si].fields[j], field) == 0) {
                        emit(c, "  pop rax\n");
                        emit(c, "  mov rax, qword ptr [rax + %d]\n", c->structs[si].offsets[j]);
                        found = 1;
                        break;
                    }
                }
            }
            if (!found) {
                emit(c, "  pop rax\n");
                emit(c, "  xor rax, rax\n");
            }
        } else {
            emit(c, "  pop rax\n");
            emit(c, "  xor rax, rax\n");
        }
        emit(c, ".L_nullsafe_%d:\n", L);
        break;
    }
    case NODE_BORROW:
    case NODE_MUT_BORROW: {
        if (n->borrow.operand->type == NODE_IDENT) {
            int off = sym_find(&c->sym, n->borrow.operand->ident);
            if (off >= 0) emit(c, "  lea rax, [rbp - %d]\n", off);
            else emit(c, "  xor rax, rax\n");
        } else if (n->borrow.operand->type == NODE_INDEX &&
                   n->borrow.operand->index_expr.index->type == NODE_IDENT) {
            /* &obj.field — compute field address instead of loading */
            gen_expr(c, n->borrow.operand->index_expr.obj);
            const char *field = n->borrow.operand->index_expr.index->ident;
            int found = 0;
            for (int si = 0; si < c->struct_count && !found; si++) {
                for (int j = 0; j < c->structs[si].field_count; j++) {
                    if (strcmp(c->structs[si].fields[j], field) == 0) {
                        emit(c, "  lea rax, [rax + %d]\n", c->structs[si].offsets[j]);
                        found = 1;
                        break;
                    }
                }
            }
            if (!found) {
                gen_expr(c, n->borrow.operand);
            }
        } else {
            gen_expr(c, n->borrow.operand);
        }
        break;
    }
    case NODE_DEREF: {
        gen_expr(c, n->borrow.operand);
        emit(c, "  mov rax, qword ptr [rax]\n");
        break;
    }
        default: emit(c, "  xor rax, rax\n"); break;
    }
}

static void gen_stmt(Codegen *c, Node *n) {
    if (!n) return;
    switch (n->type) {
    case NODE_BLOCK:
        for (Node *s = n->block.stmts; s; s = s->next) gen_stmt(c, s);
        break;
    case NODE_LET: {
        int off;
        if (n->let.init && n->let.init->type == NODE_STRUCT_LITERAL) {
            int si = find_cg_struct(c, n->let.init->struct_literal.name);
            if (si >= 0) off = sym_add_struct(&c->sym, n->let.name, si);
            else off = sym_add(&c->sym, n->let.name);
        } else {
            off = sym_add(&c->sym, n->let.name);
        }
        int idx = sym_find_idx(&c->sym, n->let.name);
        emit(c, "  mov qword ptr [rbp - %d], 0\n", off);
        if (idx >= 0 && c->sym.regs[idx] >= 0)
            emit(c, "  xor %s, %s\n", var_reg_names[c->sym.regs[idx]], var_reg_names[c->sym.regs[idx]]);
        if (n->let.init) {
            gen_expr(c, n->let.init);
            if (idx >= 0) {
                if (n->let.type && n->let.type->type == NODE_IDENT) {
                    ValType vt = get_type_for_name(n->let.type->ident);
                    if (vt != TYPE_UNKNOWN) {
                        c->sym.var_type[idx] = vt;
                        if (vt == TYPE_FLOAT || vt == TYPE_F32) c->sym.is_float[idx] = 1;
                    }
                }
                emit_trunc(c, c->sym.var_type[idx]);
                c->sym.is_float[idx] = c->sym.is_float[idx] || expr_is_float(&c->sym, n->let.init);
                if (c->sym.regs[idx] >= 0)
                    emit(c, "  mov %s, rax\n", var_reg_names[c->sym.regs[idx]]);
                if (n->let.init->type == NODE_STRUCT_LITERAL ||
                    n->let.init->type == NODE_ENUM_LITERAL)
                    c->sym.is_heap[idx] = 1;
            }
            emit(c, "  mov qword ptr [rbp - %d], rax\n", off);
        }
        break;
    }
    case NODE_ASSIGN: {
        if (n->assign.lhs->type != NODE_IDENT) break;
        int idx = sym_find_idx(&c->sym, n->assign.lhs->ident);
        if (idx < 0) break;
        int off = c->sym.offsets[idx];
        /* free old heap value before overwriting */
        if (c->sym.is_heap[idx]) {
            emit(c, "  mov rdi, [rbp - %d]\n", off);
            emit(c, "  test rdi, rdi\n  jz .L_skip_free_assign_%d\n", new_label(c));
            emit(c, "  call free\n");
            emit(c, ".L_skip_free_assign_%d:\n", new_label(c) - 1);
        }
        gen_expr(c, n->assign.rhs);
        emit_trunc(c, c->sym.var_type[idx]);
        if (c->sym.regs[idx] >= 0)
            emit(c, "  mov %s, rax\n", var_reg_names[c->sym.regs[idx]]);
        emit(c, "  mov qword ptr [rbp - %d], rax\n", off);
        /* update heap flag based on RHS type */
        if (n->assign.rhs && (n->assign.rhs->type == NODE_STRUCT_LITERAL ||
                               n->assign.rhs->type == NODE_ENUM_LITERAL))
            c->sym.is_heap[idx] = 1;
        else if (n->assign.rhs && n->assign.rhs->type != NODE_IDENT)
            c->sym.is_heap[idx] = 0;
        /* for NODE_IDENT, preserve old is_heap value (conservative) */
        break;
    }
    case NODE_IF: {
        int lbl_else = new_label(c), lbl_end = new_label(c);
        gen_expr(c, n->if_stmt.cond);
        emit(c, "  test rax, rax\n  je .L_else_%d\n", lbl_else);
        gen_stmt(c, n->if_stmt.then);
        emit(c, "  jmp .L_endif_%d\n", lbl_end);
        emit(c, ".L_else_%d:\n", lbl_else);
        if (n->if_stmt.otherwise) gen_stmt(c, n->if_stmt.otherwise);
        emit(c, ".L_endif_%d:\n", lbl_end);
        break;
    }
    case NODE_WHILE: {
        int lbl_start = new_label(c), lbl_cont = new_label(c), lbl_end = new_label(c);
        if (c->loop_sp >= 64) { diag_add(c->diag, 0, SEV_ERROR, 0, 0, 0, "too many nested loops"); exit(1); }
        c->loop_start[c->loop_sp] = lbl_start;
        c->loop_continue[c->loop_sp] = lbl_cont;
        c->loop_end[c->loop_sp] = lbl_end;
        c->loop_sp++;
        emit(c, ".L_while_%d:\n", lbl_start);
        gen_expr(c, n->while_stmt.cond);
        emit(c, "  test rax, rax\n  je .L_endwhile_%d\n", lbl_end);
        gen_stmt(c, n->while_stmt.body);
        emit(c, ".L_continue_%d:\n", lbl_cont);
        emit(c, "  jmp .L_while_%d\n", lbl_start);
        emit(c, ".L_endwhile_%d:\n", lbl_end);
        c->loop_sp--;
        break;
    }
    case NODE_FOR: {
        int lbl_start = new_label(c), lbl_cont = new_label(c), lbl_end = new_label(c);
        if (c->loop_sp >= 64) { diag_add(c->diag, 0, SEV_ERROR, 0, 0, 0, "too many nested loops"); exit(1); }
        c->loop_start[c->loop_sp] = lbl_start;
        c->loop_continue[c->loop_sp] = lbl_cont;
        c->loop_end[c->loop_sp] = lbl_end;
        c->loop_sp++;
        /* initialize var = iter */
        gen_expr(c, n->for_stmt.iter);
        int idx = sym_find_idx(&c->sym, n->for_stmt.var);
        int off = (idx >= 0) ? c->sym.offsets[idx] : 0;
        int vreg = (idx >= 0) ? c->sym.regs[idx] : -1;
        if (vreg >= 0)
            emit(c, "  mov %s, rax\n", var_reg_names[vreg]);
        emit(c, "  mov qword ptr [rbp - %d], rax\n", off);
        emit(c, ".L_while_%d:\n", lbl_start);
        /* condition: var < iter_end */
        gen_expr(c, n->for_stmt.iter_end);
        emit(c, "  push rax\n");
        if (vreg >= 0)
            emit(c, "  push %s\n", var_reg_names[vreg]);
        else
            emit(c, "  push qword ptr [rbp - %d]\n", off);
        emit(c, "  pop rax\n  pop rcx\n");
        emit(c, "  cmp rax, rcx\n");
        emit(c, "  jge .L_endwhile_%d\n", lbl_end);
        /* body */
        gen_stmt(c, n->for_stmt.body);
        /* increment */
        emit(c, ".L_continue_%d:\n", lbl_cont);
        if (vreg >= 0)
            emit(c, "  add %s, 1\n", var_reg_names[vreg]);
        else
            emit(c, "  add qword ptr [rbp - %d], 1\n", off);
        emit(c, "  jmp .L_while_%d\n", lbl_start);
        emit(c, ".L_endwhile_%d:\n", lbl_end);
        c->loop_sp--;
        break;
    }
    case NODE_RETURN:
        if (n->ret.val) gen_expr(c, n->ret.val);
        else emit(c, "  xor rax, rax\n");
        emit_fn_return(c);
        break;
    case NODE_BREAK:
        if (c->loop_sp > 0)
            emit(c, "  jmp .L_endwhile_%d\n", c->loop_end[c->loop_sp-1]);
        break;
    case NODE_CONTINUE:
        if (c->loop_sp > 0)
            emit(c, "  jmp .L_continue_%d\n", c->loop_continue[c->loop_sp-1]);
        break;
    case NODE_EXPR_STMT:
        gen_expr(c, n->expr_stmt.expr);
        break;
    case NODE_MATCH: {
        gen_expr(c, n->match.expr);
        emit(c, "  push rax\n");  /* save matched value */
        int end_label = new_label(c);
        Node *arm = n->match.arms;
        while (arm) {
            int next_arm = new_label(c);
            if (arm->match_arm.variant && strcmp(arm->match_arm.variant, "_") == 0) {
                /* wildcard: always matches */
                emit(c, "  add rsp, 8\n");  /* pop matched value */
                gen_stmt(c, arm->match_arm.body);
                emit(c, "  jmp .L_match_end_%d\n", end_label);
            } else if (arm->match_arm.variant && arm->match_arm.payload) {
                /* variant with payload: check tag */
                int tag = find_enum_variant_tag(c, arm->match_arm.variant);
                if (tag < 0) { char buf[256]; snprintf(buf, sizeof(buf), "unknown enum variant '%s' in match", arm->match_arm.variant); diag_add(c->diag, 2009, SEV_ERROR, 0, 0, 0, buf); exit(1); }
                emit(c, "  mov rax, qword ptr [rsp]\n");  /* enum ptr */
                emit(c, "  mov rax, qword ptr [rax]\n");  /* tag */
                emit(c, "  cmp rax, %d\n", tag);
                emit(c, "  jne .L_match_arm_%d\n", next_arm);
                /* bind payload: extract at offset 8 */
                int fsize = find_enum_payload_size(c, arm->match_arm.variant, 0);
                emit(c, "  mov rax, qword ptr [rsp]\n");
                if (fsize == 1)
                    emit(c, "  movzx rax, byte ptr [rax + 8]\n");
                else
                    emit(c, "  mov rax, qword ptr [rax + 8]\n");
                if (arm->match_arm.payload->type == NODE_IDENT) {
                    /* use pre-counted slot (count_locals already called sym_add) */
                    int pidx = sym_find_idx(&c->sym, arm->match_arm.payload->ident);
                    if (pidx < 0) {
                        sym_add(&c->sym, arm->match_arm.payload->ident);
                        pidx = sym_find_idx(&c->sym, arm->match_arm.payload->ident);
                    }
                    int poff = c->sym.offsets[pidx];
                    if (c->sym.regs[pidx] >= 0)
                        emit(c, "  mov %s, rax\n", var_reg_names[c->sym.regs[pidx]]);
                    emit(c, "  mov qword ptr [rbp - %d], rax\n", poff);
                }
                /* check guard */
                if (arm->match_arm.guard) {
                    gen_expr(c, arm->match_arm.guard);
                    emit(c, "  test rax, rax\n  je .L_match_arm_%d\n", next_arm);
                }
                emit(c, "  add rsp, 8\n");
                gen_stmt(c, arm->match_arm.body);
                emit(c, "  jmp .L_match_end_%d\n", end_label);
            } else if (arm->match_arm.variant) {
                /* no payload — check tag */
                int tag = find_enum_variant_tag(c, arm->match_arm.variant);
                if (tag < 0) { char buf[256]; snprintf(buf, sizeof(buf), "unknown enum variant '%s' in match", arm->match_arm.variant); diag_add(c->diag, 2009, SEV_ERROR, 0, 0, 0, buf); exit(1); }
                emit(c, "  mov rax, qword ptr [rsp]\n");
                emit(c, "  mov rax, qword ptr [rax]\n");  /* tag */
                emit(c, "  cmp rax, %d\n", tag);
                emit(c, "  jne .L_match_arm_%d\n", next_arm);
                /* check guard */
                if (arm->match_arm.guard) {
                    gen_expr(c, arm->match_arm.guard);
                    emit(c, "  test rax, rax\n  je .L_match_arm_%d\n", next_arm);
                }
                emit(c, "  add rsp, 8\n");
                gen_stmt(c, arm->match_arm.body);
                emit(c, "  jmp .L_match_end_%d\n", end_label);
            } else {
                /* fallback: no variant at all */
                emit(c, "  add rsp, 8\n");
                gen_stmt(c, arm->match_arm.body);
                emit(c, "  jmp .L_match_end_%d\n", end_label);
            }
            emit(c, ".L_match_arm_%d:\n", next_arm);
            arm = arm->next;
        }
        /* no arm matched: fallthrough */
        emit(c, "  add rsp, 8\n");
        emit(c, ".L_match_end_%d:\n", end_label);
        break;
    }
    default: break;
    }
}

static void count_locals(SymTab *s, Node *n);

static void count_locals(SymTab *s, Node *n) {
    if (!n) return;
    switch (n->type) {
    case NODE_LET:
        if (n->let.name) sym_add(s, n->let.name);
        if (n->let.init) count_locals(s, n->let.init);
        break;
    case NODE_BLOCK:
        for (Node *c = n->block.stmts; c; c = c->next) count_locals(s, c);
        break;
    case NODE_IF:
        count_locals(s, n->if_stmt.cond);
        count_locals(s, n->if_stmt.then);
        if (n->if_stmt.otherwise) count_locals(s, n->if_stmt.otherwise);
        break;
    case NODE_WHILE:
        count_locals(s, n->while_stmt.cond);
        count_locals(s, n->while_stmt.body);
        break;
    case NODE_FOR:
        count_locals(s, n->for_stmt.body);
        break;
    case NODE_ASSIGN:
        if (n->assign.rhs) count_locals(s, n->assign.rhs);
        break;
    case NODE_RETURN:
        if (n->ret.val) count_locals(s, n->ret.val);
        break;
    case NODE_EXPR_STMT:
        if (n->expr_stmt.expr) count_locals(s, n->expr_stmt.expr);
        break;
    case NODE_BREAK:
    case NODE_CONTINUE:
        break;
    case NODE_COMPREHENSION:
        if (n->comp.var) { sym_add(s, n->comp.var); }
        count_locals(s, n->comp.map);
        count_locals(s, n->comp.iter);
        if (n->comp.iter_end) count_locals(s, n->comp.iter_end);
        if (n->comp.filter) count_locals(s, n->comp.filter);
        break;
    case NODE_CALL:
        for (Node *a = n->call.args; a; a = a->next) count_locals(s, a);
        break;
    case NODE_STRUCT_LITERAL:
        for (Node *a = n->struct_literal.args; a; a = a->next) count_locals(s, a);
        break;
    case NODE_ENUM_LITERAL:
        for (Node *p = n->enum_literal.payload; p; p = p->next)
            count_locals(s, p);
        break;
    case NODE_INDEX:
    case NODE_NULLSAFE:
        count_locals(s, n->index_expr.obj);
        count_locals(s, n->index_expr.index);
        break;
    case NODE_BINARY:
        count_locals(s, n->binary.left);
        count_locals(s, n->binary.right);
        break;
    case NODE_UNARY:
        if (n->unary.operand && n->unary.op <= 4) count_locals(s, n->unary.operand);
        break;
    case NODE_BORROW:
    case NODE_MUT_BORROW:
    case NODE_DEREF:
        if (n->borrow.operand) count_locals(s, n->borrow.operand);
        break;
    case NODE_MATCH:
        count_locals(s, n->match.expr);
        for (Node *arm = n->match.arms; arm; arm = arm->next) {
            /* pre-allocate payload variable slot */
            if (arm->match_arm.payload && arm->match_arm.payload->type == NODE_IDENT)
                sym_add(s, arm->match_arm.payload->ident);
            if (arm->match_arm.payload) count_locals(s, arm->match_arm.payload);
            if (arm->match_arm.guard) count_locals(s, arm->match_arm.guard);
            count_locals(s, arm->match_arm.body);
        }
        break;
    case NODE_MATCH_ARM:
        if (n->match_arm.payload && n->match_arm.payload->type == NODE_IDENT)
            sym_add(s, n->match_arm.payload->ident);
        if (n->match_arm.payload) count_locals(s, n->match_arm.payload);
        if (n->match_arm.guard) count_locals(s, n->match_arm.guard);
        count_locals(s, n->match_arm.body);
        break;
    default: break;
    }
}

static int find_cg_struct(Codegen *c, const char *name) {
    for (int i = 0; i < c->struct_count; i++)
        if (strcmp(c->structs[i].name, name) == 0) return i;
    return -1;
}

static void collect_structs(Codegen *c, Node *prog) {
    for (Node *item = prog->next; item; item = item->next) {
        if (item->type == NODE_STRUCT_DECL) {
            if (c->struct_count >= MAX_CG_STRUCTS) continue;
            CStructDef *s = &c->structs[c->struct_count++];
            strncpy(s->name, item->struct_decl.name, 63); s->name[63] = 0;
            s->field_count = 0;
            s->total_size = 0;
            for (Node *f = item->struct_decl.fields; f; f = f->next) {
                if (f->type != NODE_LET || !f->let.name) continue;
                if (s->field_count >= MAX_CG_FIELDS) continue;
                strncpy(s->fields[s->field_count], f->let.name, 63);
                s->fields[s->field_count][63] = 0;
                s->offsets[s->field_count] = s->total_size;
                s->total_size += 8;
                s->field_count++;
            }
        }
    }
}

static void collect_enums(Codegen *c, Node *prog) {
    for (Node *item = prog->next; item; item = item->next) {
        if (item->type == NODE_ENUM_DECL) {
            if (c->enum_count >= MAX_CG_ENUMS) continue;
            CEnumDef *e = &c->enums[c->enum_count++];
            strncpy(e->name, item->enum_decl.name, 63); e->name[63] = 0;
            e->variant_count = 0;
            e->total_size = 8;
            for (Node *v = item->enum_decl.variants; v; v = v->next) {
                if (v->type != NODE_MATCH_ARM || !v->match_arm.variant) continue;
                if (e->variant_count >= MAX_CG_VARIANTS) continue;
                int idx = e->variant_count;
                strncpy(e->variants[idx], v->match_arm.variant, 63);
                e->variants[idx][63] = 0;
                memset(e->payload_sizes[idx], 0, sizeof(e->payload_sizes[idx]));
                int payload_size = 0;
                int pcount = 0;
                for (Node *pld = v->match_arm.payload; pld; pld = pld->next) {
                    int fsize = 8;
                    if (pld->type == NODE_IDENT && strcmp(pld->ident, "bool") == 0) fsize = 1;
                    payload_size += fsize;
                    if (pcount < MAX_CG_FIELDS) e->payload_sizes[idx][pcount] = fsize;
                    pcount++;
                }
                e->payload_counts[idx] = pcount;
                e->variant_sizes[idx] = payload_size + 8;
                if (e->variant_sizes[idx] > e->total_size) e->total_size = e->variant_sizes[idx];
                e->variant_count++;
            }
        }
    }
}

static void emit_fn_return(Codegen *c) {
    emit(c, "  mov rsp, rbp\n  pop rbp\n");
    for (int i = MAX_VAR_REGS - 1; i >= 0; i--)
        if (c->sym.used_regs & (1 << i))
            emit(c, "  pop %s\n", var_reg_names[i]);
    emit(c, "  ret\n");
}

static void gen_fn(Codegen *c, Node *n) {
    sym_init(&c->sym);
    c->loop_sp = 0;
    const char *name = n->fn.name;
    if (c->gui_mode && strcmp(name, "main") == 0) name = "clean_main";
    /* Add params to determine register usage before prologue */
    for (Node *p = n->fn.params; p; p = p->next) {
        if (p->type == NODE_LET && p->let.name) {
            sym_add(&c->sym, p->let.name);
        }
    }
    SymTab tmp;
    memcpy(&tmp, &c->sym, sizeof(tmp));
    count_locals(&c->sym, n->fn.body);
    int frame = c->sym.stack_size;
    if (frame > 0) frame = (frame + 15) & ~15;
    int saved_used_regs = c->sym.used_regs;
    memcpy(&c->sym, &tmp, sizeof(tmp));
    /* Emit function header + prologue */
    emit(c, "\n.section .text\n");
    if (n->fn.pub || strcmp(n->fn.name, "main") == 0)
        emit(c, ".globl %s\n", name);
    emit(c, ".type %s, @function\n%s:\n", name, name);
    /* Push callee-saved regs before push rbp so they sit above rbp */
    for (int i = 0; i < MAX_VAR_REGS; i++)
        if (saved_used_regs & (1 << i))
            emit(c, "  push %s\n", var_reg_names[i]);
    emit(c, "  push rbp\n  mov rbp, rsp\n");
    if (frame > 0) emit(c, "  sub rsp, %d\n", frame);
    /* Store params from argument registers */
    static const char *arg_regs[] = {"rdi","rsi","rdx","rcx","r8","r9"};
    int reg_idx = 0;
    for (Node *p = n->fn.params; p; p = p->next) {
        if (p->type == NODE_LET && p->let.name) {
            int off = sym_find(&c->sym, p->let.name);
            int idx = sym_find_idx(&c->sym, p->let.name);
            if (reg_idx < 6) {
                if (idx >= 0 && c->sym.regs[idx] >= 0)
                    emit(c, "  mov %s, %s\n", var_reg_names[c->sym.regs[idx]], arg_regs[reg_idx]);
                emit(c, "  mov qword ptr [rbp - %d], %s\n", off, arg_regs[reg_idx]);
            }
            reg_idx++;
        }
    }
    gen_stmt(c, n->fn.body);
    /* free heap-allocated local variables at scope exit */
    for (int i = 0; i < c->sym.count; i++) {
        if (c->sym.is_heap[i]) {
            int off = c->sym.offsets[i];
            emit(c, "  mov rdi, [rbp - %d]\n", off);
            emit(c, "  test rdi, rdi\n  jz .L_skip_free_%d\n", i);
            emit(c, "  call free\n");
            emit(c, ".L_skip_free_%d:\n", i);
        }
    }
    emit(c, "  xor rax, rax\n");
    emit_fn_return(c);
}

/* Try MIR→LIR→regalloc→asm pipeline for a function. Returns 1 if MIR path was used, 0 if fallback needed.
   Currently disabled — MIR while-loop lowering is incomplete (generates sequential code without jumps). */
static int codegen_mir_fn(Codegen *c, Node *n) {
    (void)c; (void)n;
    return 0;
}

static int has_gui_externs(Node *prog) {
    for (Node *item = prog; item; item = item->next) {
        if (item->type == NODE_EXTERN_DECL && item->ext.name &&
            strncmp(item->ext.name, "clgui_", 6) == 0)
            return 1;
    }
    return 0;
}

static const char *find_rt_file(const char *rt_path, const char *name, pid_t pid) {
    static char path[4096];
    if (rt_path) {
        snprintf(path, sizeof(path), "%s/%s", rt_path, name);
        if (access(path, F_OK) == 0) return path;
    }
    const char *home = getenv("HOME");
    if (home) {
        snprintf(path, sizeof(path), "%s/.local/bin/%s", home, name);
        if (access(path, F_OK) == 0) return path;
    }
    snprintf(path, sizeof(path), "./%s", name);
    if (access(path, F_OK) == 0) return path;
    snprintf(path, sizeof(path), "src/runtime/%s", name);
    if (access(path, F_OK) == 0) return path;
    snprintf(path, sizeof(path), "../src/runtime/%s", name);
    if (access(path, F_OK) == 0) return path;
    snprintf(path, sizeof(path), "/tmp/%s", name);
    if (access(path, F_OK) == 0) return path;
    snprintf(path, sizeof(path), "/tmp/clean_%d_clgui.c", pid);
    FILE *f = fopen(path, "w");
    if (!f) return NULL;
    fputs(clgui_embedded, f);
    fclose(f);
    return path;
}

int codegen_compile(Node *prog, const char *source_file, const char *output_file, Diag *diag, const char *rt_path) {
    (void)source_file;
    int gui = has_gui_externs(prog);
    char asm_path[1024], obj_path[1024];
    snprintf(asm_path, sizeof(asm_path), "%s.s", output_file);
    snprintf(obj_path, sizeof(obj_path), "%s.o", output_file);

    Codegen cg;
    cg.out = fopen(asm_path, "w");
    if (!cg.out) { perror("asm"); return 1; }
    cg.label_seq = 0;
    cg.diag = diag;
    cg.str_cnt = 0;
    cg.loop_sp = 0;
    cg.struct_count = 0;
    cg.enum_count = 0;
    cg.gui_mode = gui;

    emit(&cg, ".intel_syntax noprefix\n");

    if (gui) {
        emit(&cg, ".section .rodata\n");
        emit(&cg, ".section .text\n");
    }

    if (!gui) {
        emit_print_int(&cg);
        emit_print_float(&cg);
        emit_sleep(&cg);
        emit_read_int(&cg);
        emit_print_str(&cg);
        emit_calc_expr(&cg);
        emit_time_ms(&cg);
        emit_input(&cg);
        emit_strlen(&cg);
        emit_inspect(&cg);
        emit_assert(&cg);
        emit_clear_screen(&cg);
        emit_reset_attr(&cg);
        emit_set_fg(&cg);
        emit_set_bg(&cg);
        emit_hide_cursor(&cg);
        emit_show_cursor(&cg);
        emit_get_frame_ptr(&cg);
        emit_string_clone(&cg);
        emit_string_concat(&cg);
    }

    collect_structs(&cg, prog);
    collect_enums(&cg, prog);
    for (Node *item = prog->next; item; item = item->next) {
        if (item->type == NODE_FN_DECL) {
            if (!codegen_mir_fn(&cg, item))
                gen_fn(&cg, item);
        }
        else if (item->type == NODE_EXTERN_DECL) {
            if (item->ext.pub) emit(&cg, ".globl %s\n", item->ext.name);
        }
        else if (item->type == NODE_IMPL_BLOCK)
            for (Node *m = item->impl_block.methods; m; m = m->next)
                if (m->type == NODE_FN_DECL) {
                    if (!codegen_mir_fn(&cg, m))
                        gen_fn(&cg, m);
                }
    }

    emit_strtab(&cg);
    emit(&cg, ".L_ansi_cls: .byte 0x1b, '[', '2', 'J', 0x1b, '[', 'H'\n");
    emit(&cg, ".L_ansi_reset: .byte 0x1b, '[', '0', 'm'\n");
    emit(&cg, ".L_ansi_fg: .byte 0x1b, '[', '3', '8', ';', '5', ';'\n");
    emit(&cg, ".L_ansi_bg: .byte 0x1b, '[', '4', '8', ';', '5', ';'\n");
    emit(&cg, ".L_ansi_m: .byte 'm'\n");
    emit(&cg, ".L_ansi_hide: .byte 0x1b, '[', '?', '2', '5', 'l'\n");
    emit(&cg, ".L_ansi_show: .byte 0x1b, '[', '?', '2', '5', 'h'\n");
    emit(&cg, "\n.section .bss\n.align 8\ninput_buf:\n  .space 1024\n");

    fclose(cg.out);

    char cmd[8192];
    snprintf(cmd, sizeof(cmd), "as -o %s %s", obj_path, asm_path);
    if (system(cmd) != 0) {
        diag_add(diag, 3000, SEV_ERROR, 0, 0, 0, "assembly failed");
        unlink(asm_path); return 1;
    }

    if (gui) {
        const char *clgui_path = find_rt_file(rt_path, "clgui.c", getpid());
        if (!clgui_path) {
            diag_add(diag, 3002, SEV_ERROR, 0, 0, 0, "clgui.c not found");
            unlink(asm_path); unlink(obj_path); return 1;
        }
        char gui_obj[1024];
        snprintf(gui_obj, sizeof(gui_obj), "%s_clgui.o", output_file);
        snprintf(cmd, sizeof(cmd), "cc -c -o %s %s", gui_obj, clgui_path);
        if (system(cmd) != 0) {
            diag_add(diag, 3003, SEV_ERROR, 0, 0, 0, "clgui.c compilation failed");
            unlink(asm_path); unlink(obj_path); unlink(gui_obj); return 1;
        }
        snprintf(cmd, sizeof(cmd), "cc -o %s %s %s -lX11 -lm", output_file, obj_path, gui_obj);
        if (system(cmd) != 0) {
            diag_add(diag, 3001, SEV_ERROR, 0, 0, 0, "linking failed (X11 required)");
            unlink(asm_path); unlink(obj_path); unlink(gui_obj); return 1;
        }
        unlink(gui_obj);
    } else {
        snprintf(cmd, sizeof(cmd), "cc -o %s %s -lc", output_file, obj_path);
        if (system(cmd) != 0) {
            diag_add(diag, 3001, SEV_ERROR, 0, 0, 0, "linking failed");
            unlink(asm_path); unlink(obj_path); return 1;
        }
    }

    unlink(asm_path);
    unlink(obj_path);
    return 0;
}

int codegen_run(Node *prog, const char *source_file, Diag *diag, const char *rt_path, int prog_argc, char **prog_argv) {
    char output[1024];
    snprintf(output, sizeof(output), "/tmp/clean_%d", getpid());
    if (codegen_compile(prog, source_file, output, diag, rt_path) != 0) return 1;
    chmod(output, 0755);

    int total = 1 + prog_argc + 1;
    char **args = (char **)malloc(total * sizeof(char *));
    if (!args) return 1;
    args[0] = output;
    for (int i = 0; i < prog_argc; i++) args[1 + i] = prog_argv[i];
    args[total - 1] = NULL;

    int gui = has_gui_externs(prog);
    int status;
    pid_t pid = fork();
    if (pid == 0) {
        if (gui) {
            pid_t pid2 = fork();
            if (pid2 == 0) {
                execvp(output, args);
                _exit(127);
            }
            _exit(0);
        } else {
            execvp(output, args);
            _exit(127);
        }
    }
    free(args);
    if (pid > 0) waitpid(pid, &status, 0);
    if (!gui) {
        unlink(output);
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }
    usleep(50000);
    unlink(output);
    return 0;
}
