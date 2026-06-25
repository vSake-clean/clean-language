#ifndef CLEAN_PARSER_H
#define CLEAN_PARSER_H

#include "../ast.h"
#include "../diag.h"
#include "lexer.h"

#define MAX_PSTRUCTS 32
#define MAX_PFIELDS 32
#define MAX_PENUMS 32
#define MAX_VARIANTS 16
#define MAX_TRAITS 32
#define MAX_METHODS 16
#define MAX_TYPE_PARAMS 8

typedef struct {
    char name[64];
    char fields[MAX_PFIELDS][64];
    int field_count;
} PStructDef;

typedef struct {
    char name[64];
    char variants[MAX_VARIANTS][64];
    int payloads[MAX_VARIANTS];
    int variant_count;
    int total_size;
} PEnumDef;

typedef struct {
    char name[64];
    char methods[MAX_METHODS][64];
    int method_count;
} PTraitDef;

typedef struct {
    char names[MAX_TYPE_PARAMS][64];
    int count;
} TypeParamList;

typedef struct {
    Lexer lexer;
    int error_count;
    const char *filename;
    Diag *diag;
    PStructDef structs[MAX_PSTRUCTS];
    int struct_count;
    PEnumDef enums[MAX_PENUMS];
    int enum_count;
    TypeParamList current_type_params;
} Parser;

void parser_init(Parser *p, const char *filename, const char *source, Diag *diag);
Node *parser_parse(Parser *p);
void parser_free(Parser *p);

#endif
