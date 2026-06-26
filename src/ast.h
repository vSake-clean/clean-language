#ifndef CLEAN_AST_H
#define CLEAN_AST_H

#include <stddef.h>

typedef enum {
    NODE_PROGRAM,
    NODE_FN_DECL,
    NODE_EXTERN_DECL,
    NODE_BLOCK,
    NODE_LET,
    NODE_ASSIGN,
    NODE_IF,
    NODE_WHILE,
    NODE_RETURN,
    NODE_EXPR_STMT,
    NODE_INT,
    NODE_FLOAT,
    NODE_STR,
    NODE_BOOL,
    NODE_IDENT,
    NODE_BINARY,
    NODE_UNARY,
    NODE_CALL,
    NODE_INDEX,
    NODE_BREAK,
    NODE_CONTINUE,
    NODE_COMPREHENSION,
    NODE_STRUCT_DECL,
    NODE_STRUCT_LITERAL,
    NODE_MATCH,
    NODE_MATCH_ARM,
    NODE_ENUM_DECL,
    NODE_ENUM_LITERAL,
    NODE_BORROW,
    NODE_MUT_BORROW,
    NODE_DEREF,
    NODE_TRAIT_DECL,
    NODE_IMPL_BLOCK,
} NodeType;

typedef struct Node {
    NodeType type;
    struct Node *next;
    size_t src_line;
    size_t src_col;
    union {
        struct { char *name; struct Node *params; struct Node *ret_type; struct Node *body; int effect; int pub; } fn;
        struct { char *name; struct Node *params; struct Node *ret_type; int pub; } ext;
        struct { struct Node *stmts; } block;
        struct { char *name; struct Node *type; struct Node *init; int mut; } let;
        struct { struct Node *lhs; struct Node *rhs; } assign;
        struct { struct Node *cond; struct Node *then; struct Node *otherwise; } if_stmt;
        struct { struct Node *cond; struct Node *body; } while_stmt;
        struct { struct Node *val; } ret;
        struct { struct Node *expr; } expr_stmt;
        long long int_val;
        double float_val;
        char *str_val;
        int bool_val;
        char *ident;
        struct { struct Node *left; struct Node *right; int op; } binary;
        struct { struct Node *operand; int op; } unary;
        struct { struct Node *callee; struct Node *args; } call;
        struct { struct Node *obj; struct Node *index; } index_expr;
        struct { struct Node *map; char *var; struct Node *iter; struct Node *iter_end; struct Node *filter; } comp;
        struct { char *name; struct Node *fields; } struct_decl;
        struct { char *name; struct Node *args; } struct_literal;
        struct { struct Node *expr; struct Node *arms; } match;
        struct { char *variant; struct Node *payload; struct Node *guard; struct Node *body; } match_arm;
        struct { char *name; struct Node *type_params; struct Node *variants; } enum_decl;
        struct { char *enum_name; char *variant; struct Node *payload; } enum_literal;
        struct { struct Node *operand; int mut; } borrow;
        struct { char *name; struct Node *type_params; struct Node *methods; } trait_decl;
        struct { char *name; struct Node *for_type; struct Node *methods; } impl_block;
    };
} Node;

Node *node_new(NodeType type);
void node_free(Node *n);

#endif
