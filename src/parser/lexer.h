#ifndef CLEAN_LEXER_H
#define CLEAN_LEXER_H

#include <stddef.h>

typedef enum {
    TOK_EOF, TOK_IDENT, TOK_INT, TOK_STR, TOK_CHAR,
    TOK_FN, TOK_LET, TOK_MUT, TOK_VAR, TOK_IF, TOK_ELIF, TOK_ELSE,
    TOK_WHILE, TOK_FOR, TOK_IN, TOK_RETURN, TOK_BREAK, TOK_CONTINUE,
    TOK_MATCH, TOK_STRUCT, TOK_ENUM, TOK_TRAIT, TOK_IMPL,
    TOK_USE, TOK_PUB, TOK_UNSAFE, TOK_EXTERN,
    TOK_TRUE, TOK_FALSE, TOK_UNLESS, TOK_EFFECT,
    TOK_MOVE, TOK_REF, TOK_MUT_REF, TOK_AS, TOK_SELF,
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_EQ, TOK_EQEQ, TOK_NE, TOK_LT, TOK_LE, TOK_GT, TOK_GE,
    TOK_AND, TOK_OR, TOK_NOT,
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACK, TOK_RBRACK,
    TOK_DOT, TOK_DOTDOT, TOK_DOTQUESTION, TOK_QUESTION, TOK_COMMA, TOK_COLON, TOK_ARROW,
    TOK_PLUSEQ, TOK_MINUSEQ, TOK_STAREQ, TOK_SLASHEQ,
    TOK_PIPE,
    TOK_BITOR, TOK_BITXOR, TOK_BITAND, TOK_BITNOT, TOK_SHL, TOK_SHR,
    TOK_STARSTAR, TOK_FLOAT, TOK_AMPERSAND,
    TOK_INDENT, TOK_DEDENT, TOK_NEWLINE,
} TokenType;

typedef struct {
    TokenType type;
    char *text;
    long long int_val;
    double float_val;
    size_t line;
    size_t col;
    size_t len;
} Token;

typedef struct {
    const char *source;
    size_t pos;
    size_t line;
    size_t col;
    int indent_stack[64];
    int indent_sp;
    int pending_indent;
    Token peek;
    int has_peek;
} Lexer;

void lexer_init(Lexer *l, const char *source);
Token lexer_next(Lexer *l);
Token lexer_peek(Lexer *l);
void lexer_free_token(Token *t);
void lexer_free(Lexer *l);

#endif
