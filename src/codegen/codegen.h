#ifndef CLEAN_CODEGEN_H
#define CLEAN_CODEGEN_H

#include "../ast.h"
#include "../diag.h"

int codegen_compile(Node *prog, const char *source_file, const char *output_file, Diag *diag, const char *rt_path);
int codegen_run(Node *prog, const char *source_file, Diag *diag, const char *rt_path, int prog_argc, char **prog_argv);

#endif
