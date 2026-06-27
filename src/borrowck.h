#ifndef CLEAN_BORROWCK_H
#define CLEAN_BORROWCK_H

#include "ast.h"
#include "diag.h"

void borrowck_program(Node *prog, Diag *diag, const char *source);

#endif
