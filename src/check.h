#ifndef CLEAN_CHECK_H
#define CLEAN_CHECK_H

#include "ast.h"
#include "diag.h"

void check_program(Node *prog, Diag *diag, const char *source);

#endif
