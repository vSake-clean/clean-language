#ifndef CLEAN_PARSER_H
#define CLEAN_PARSER_H

#include "../ast.h"
#include "../diag.h"
#include "lexer.h"

#define MAX_PSTRUCTS 32
#define MAX_PFIELDS 32

typedef struct {
    char name[64];
    char fields[MAX_PFIELDS][64];
    int field_count;
} PStructDef;

typedef struct {
    Lexer lexer;
    int error_count;
    const char *filename;
    Diag *diag;
    PStructDef structs[MAX_PSTRUCTS];
    int struct_count;
} Parser;

void parser_init(Parser *p, const char *filename, const char *source, Diag *diag);
Node *parser_parse(Parser *p);
void parser_free(Parser *p);

#endif
