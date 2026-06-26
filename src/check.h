#ifndef CLEAN_CHECK_H
#define CLEAN_CHECK_H

#include "ast.h"
#include "diag.h"

typedef enum {
    TYPE_UNKNOWN, TYPE_I64, TYPE_BOOL, TYPE_STR, TYPE_FLOAT, TYPE_VOID, TYPE_CHAR
} ValType;

const char *valtype_str(ValType t);
ValType infer_node_type(Node *n);
int valtype_size(ValType t);

void check_program(Node *prog, Diag *diag, const char *source);

#endif
