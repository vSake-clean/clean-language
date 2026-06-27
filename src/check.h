#ifndef CLEAN_CHECK_H
#define CLEAN_CHECK_H

#include "ast.h"
#include "diag.h"

typedef enum {
    TYPE_UNKNOWN, TYPE_I8, TYPE_I16, TYPE_I32, TYPE_I64,
    TYPE_U8, TYPE_U16, TYPE_U32, TYPE_U64,
    TYPE_I128, TYPE_U128,
    TYPE_F32, TYPE_FLOAT,
    TYPE_BOOL, TYPE_CHAR, TYPE_STR, TYPE_VOID,
    TYPE_USIZE, TYPE_ISIZE,
} ValType;

const char *valtype_str(ValType t);
ValType infer_node_type(Node *n);
int valtype_size(ValType t);
ValType get_type_for_name(const char *name);

void check_program(Node *prog, Diag *diag, const char *source);

#endif
