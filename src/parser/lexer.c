#include "lexer.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void lexer_init(Lexer *l, const char *source) {
    l->source = source;
    l->pos = 0;
    l->line = 1;
    l->col = 1;
    l->indent_sp = 0;
    l->indent_stack[0] = 0;
    l->pending_indent = 0;
    l->has_peek = 0;
}

static Token token_new(Lexer *l, TokenType type) {
    return (Token){ .type = type, .text = NULL, .int_val = 0, .line = l->line, .col = l->col, .len = 1 };
}

static int kw_match(const char *s, TokenType *t) {
    static const char *kw[] = {
        "fn","let","mut","var","if","elif","else","while","for","in",
        "return","break","continue","match","struct","enum","trait","impl",
        "use","pub","unsafe","extern","true","false","unless","effect",
        "move","ref","mut_ref",
        "and","or","not", NULL
    };
    static TokenType kt[] = {
        TOK_FN,TOK_LET,TOK_MUT,TOK_VAR,TOK_IF,TOK_ELIF,TOK_ELSE,TOK_WHILE,TOK_FOR,TOK_IN,
        TOK_RETURN,TOK_BREAK,TOK_CONTINUE,TOK_MATCH,TOK_STRUCT,TOK_ENUM,TOK_TRAIT,TOK_IMPL,
        TOK_USE,TOK_PUB,TOK_UNSAFE,TOK_EXTERN,TOK_TRUE,TOK_FALSE,TOK_UNLESS,TOK_EFFECT,
        TOK_MOVE,TOK_REF,TOK_MUT_REF,
        TOK_AND,TOK_OR,TOK_NOT
    };
    for (int i = 0; kw[i]; i++)
        if (strcmp(s, kw[i]) == 0) { *t = kt[i]; return 1; }
    return 0;
}

/* scan ahead to next non-empty content line, compute its indent, and set pending_indent.
   Returns 1 if content line found, 0 if EOF.
   Called after '\n' has been consumed (l->pos at start of next line). */
static int scan_next_line(Lexer *l) {
    const char *s = l->source;
    l->pending_indent = 0;

    int indent = 0;
    size_t start = 0;

    /* skip blank lines and comments */
    while (1) {
        start = l->pos;

        /* peek: if line is blank/comment, skip it; otherwise break to indent computation */
        if (s[l->pos] == '\n' || s[l->pos] == '\0')
            return 0;
        if (s[l->pos] == '#') {
            while (s[l->pos] && s[l->pos] != '\n') l->pos++;
            if (s[l->pos] == '\n') { l->pos++; l->line++; l->col = 1; }
            continue;
        }
        /* not blank: compute indent */
        indent = 0;
        start = l->pos;
        while (s[l->pos] == ' ' || s[l->pos] == '\t') {
            indent += (s[l->pos] == '\t') ? 4 : 1;
            l->pos++;
        }
        /* if only whitespace to end of line, skip */
        if (s[l->pos] == '\n' || s[l->pos] == '\0' || s[l->pos] == '#') {
            if (s[l->pos] == '\n') { l->pos++; l->line++; l->col = 1; }
            continue;
        }
        /* content line found */
        l->pos = start;
        l->col = indent + 1;
        break;
    }

    /* compare with stack */
    int cur = l->indent_stack[l->indent_sp];
    if (indent > cur) {
        if (l->indent_sp >= 63) { l->pending_indent = 0; return 0; }
        l->indent_sp++;
        l->indent_stack[l->indent_sp] = indent;
        l->pending_indent = 1;  /* emit INDENT */
    } else if (indent < cur) {
        /* pop until we match or go past */
        int count = 0;
        while (l->indent_sp > 0 && indent < l->indent_stack[l->indent_sp]) {
            l->indent_sp--;
            count--;
        }
        l->pending_indent = count;  /* negative = DEDENTs */
    }
    return 1;
}

void lexer_free_token(Token *t) {
    free(t->text);
    t->text = NULL;
}

Token lexer_next(Lexer *l) {
    if (l->has_peek) {
        l->has_peek = 0;
        return l->peek;
    }

    /* emit pending indent/dedent */
    if (l->pending_indent != 0) {
        int val = l->pending_indent;
        if (val > 0) {
            l->pending_indent--;
            return token_new(l, TOK_INDENT);
        } else {
            l->pending_indent++;
            return token_new(l, TOK_DEDENT);
        }
    }

    const char *s = l->source;

    while (1) {
        /* skip whitespace (non-newline) */
        if (s[l->pos] == ' ' || s[l->pos] == '\t') {
            l->pos++; l->col++;
            continue;
        }

        /* newline: end of current line */
        if (s[l->pos] == '\n') {
            l->pos++; l->line++; l->col = 1;
            if (scan_next_line(l)) {
                /* if there are pending indents, emit NEWLINE now, indents follow */
                Token t = token_new(l, TOK_NEWLINE);
                return t;
            }
            continue;
        }

        /* comment */
        if (s[l->pos] == '#') {
            while (s[l->pos] && s[l->pos] != '\n') l->pos++;
            continue;
        }

        /* EOF */
        if (s[l->pos] == '\0') {
            /* emit trailing dedents */
            while (l->indent_sp > 0) {
                l->indent_sp--;
                return token_new(l, TOK_DEDENT);
            }
            return token_new(l, TOK_EOF);
        }

        /* regular token */
        break;
    }

    /* identifiers */
    if (isalpha((unsigned char)s[l->pos]) || s[l->pos] == '_') {
        size_t start = l->pos;
        while (isalnum((unsigned char)s[l->pos]) || s[l->pos] == '_') l->pos++;
        size_t toklen = l->pos - start;
        char *word = strndup(s + start, toklen);
        Token t = token_new(l, TOK_IDENT);
        t.text = word;
        t.len = toklen;
        l->col += toklen;
        TokenType kt;
        if (kw_match(word, &kt)) { free(t.text); t.text = NULL; t.type = kt; }
        return t;
    }

    /* integers, hex, bin, and floats */
    if (isdigit((unsigned char)s[l->pos])) {
        size_t start = l->pos;
        long long val = 0;
        if (s[l->pos] == '0' && (s[l->pos+1] == 'x' || s[l->pos+1] == 'X')) {
            l->pos += 2; l->col += 2;
            while (isxdigit((unsigned char)s[l->pos])) {
                char c = s[l->pos];
                if (c >= '0' && c <= '9') val = val * 16 + (c - '0');
                else if (c >= 'a' && c <= 'f') val = val * 16 + (c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') val = val * 16 + (c - 'A' + 10);
                l->pos++; l->col++;
            }
            Token t = token_new(l, TOK_INT);
            t.int_val = val;
            t.len = l->pos - start;
            return t;
        }
        if (s[l->pos] == '0' && (s[l->pos+1] == 'b' || s[l->pos+1] == 'B')) {
            l->pos += 2; l->col += 2;
            while (s[l->pos] == '0' || s[l->pos] == '1') {
                val = val * 2 + (s[l->pos] - '0');
                l->pos++; l->col++;
            }
            Token t = token_new(l, TOK_INT);
            t.int_val = val;
            t.len = l->pos - start;
            return t;
        }
        while (isdigit((unsigned char)s[l->pos])) {
            val = val * 10 + (s[l->pos] - '0');
            l->pos++; l->col++;
        }
        if (s[l->pos] == '.' || s[l->pos] == 'e' || s[l->pos] == 'E') {
            /* float literal requires '.' followed by digit/e/E */
            int is_float = 0;
            if (s[l->pos] == '.' && (isdigit((unsigned char)s[l->pos+1]) || s[l->pos+1] == 'e' || s[l->pos+1] == 'E')) {
                is_float = 1;
                l->pos++; l->col++;
                while (isdigit((unsigned char)s[l->pos])) { l->pos++; l->col++; }
            }
            if (s[l->pos] == 'e' || s[l->pos] == 'E') {
                is_float = 1;
                l->pos++; l->col++;
                if (s[l->pos] == '+' || s[l->pos] == '-') { l->pos++; l->col++; }
                while (isdigit((unsigned char)s[l->pos])) { l->pos++; l->col++; }
            }
            if (is_float) {
                size_t end = l->pos;
                size_t flen = end - start;
                char *fstr = strndup(s + start, flen);
                double fval = strtod(fstr, NULL);
                Token t = token_new(l, TOK_FLOAT);
                t.text = fstr;
                t.float_val = fval;
                t.len = flen;
                return t;
            }
        }
        Token t = token_new(l, TOK_INT);
        t.int_val = val;
        t.len = l->pos - start;
        return t;
    }

    /* char literals */
    if (s[l->pos] == '\'') {
        size_t cstart = l->pos;
        l->pos++; l->col++;
        long long cval = 0;
        if (s[l->pos] == '\\') {
            l->pos++; l->col++;
            char esc = s[l->pos];
            if (esc == 'n') cval = '\n';
            else if (esc == 'r') cval = '\r';
            else if (esc == 't') cval = '\t';
            else if (esc == '\\') cval = '\\';
            else if (esc == '\'') cval = '\'';
            else if (esc == '0') cval = '\0';
            else if (esc == 'x') {
                l->pos++; l->col++;
                unsigned char byte = 0;
                for (int h = 0; h < 2 && isxdigit(s[l->pos]); h++) {
                    byte *= 16;
                    char c = s[l->pos];
                    if (c >= '0' && c <= '9') byte += c - '0';
                    else if (c >= 'a' && c <= 'f') byte += c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F') byte += c - 'A' + 10;
                    l->pos++; l->col++;
                }
                cval = byte;
                goto char_done;
            } else { cval = esc; }
            l->pos++; l->col++;
        } else if (s[l->pos] && s[l->pos] != '\'') {
            cval = s[l->pos];
            l->pos++; l->col++;
        }
        char_done:
        Token t = token_new(l, TOK_CHAR);
        t.int_val = cval;
        t.len = l->pos - cstart + 1;
        if (s[l->pos] == '\'') { l->pos++; l->col++; }
        return t;
    }

    /* strings */
    if (s[l->pos] == '"') {
        size_t start = l->pos;
        l->pos++; l->col++;
        size_t cap = 4096;
        char *buf = malloc(cap);
        size_t cnt = 0;
        while (s[l->pos] && s[l->pos] != '"') {
            if (cnt >= cap - 1) {
                cap *= 2;
                buf = realloc(buf, cap);
            }
            if (s[l->pos] == '\\') {
                l->pos++; l->col++;
                char esc = s[l->pos];
                if (esc == 'n') { buf[cnt++] = '\n'; }
                else if (esc == 'r') { buf[cnt++] = '\r'; }
                else if (esc == 't') { buf[cnt++] = '\t'; }
                else if (esc == '\\') { buf[cnt++] = '\\'; }
                else if (esc == '"') { buf[cnt++] = '"'; }
                else if (esc == '0') { buf[cnt++] = '\0'; }
                else if (esc == 'x') {
                    l->pos++; l->col++;
                    unsigned char byte = 0;
                    for (int h = 0; h < 2 && isxdigit(s[l->pos]); h++) {
                        byte *= 16;
                        char c = s[l->pos];
                        if (c >= '0' && c <= '9') byte += c - '0';
                        else if (c >= 'a' && c <= 'f') byte += c - 'a' + 10;
                        else if (c >= 'A' && c <= 'F') byte += c - 'A' + 10;
                        l->pos++; l->col++;
                    }
                    buf[cnt++] = byte;
                    continue; /* already advanced pos */
                }
                else if (esc == 'u') {
                    l->pos++; l->col++;
                    unsigned int cp = 0;
                    for (int h = 0; h < 4 && isxdigit(s[l->pos]); h++) {
                        cp *= 16;
                        char c = s[l->pos];
                        if (c >= '0' && c <= '9') cp += c - '0';
                        else if (c >= 'a' && c <= 'f') cp += c - 'a' + 10;
                        else if (c >= 'A' && c <= 'F') cp += c - 'A' + 10;
                        l->pos++; l->col++;
                    }
                    /* encode UTF-8 */
                    if (cp < 0x80) { buf[cnt++] = cp; }
                    else if (cp < 0x800) {
                        buf[cnt++] = 0xC0 | (cp >> 6);
                        buf[cnt++] = 0x80 | (cp & 0x3F);
                    }
                    else if (cp < 0x10000) {
                        buf[cnt++] = 0xE0 | (cp >> 12);
                        buf[cnt++] = 0x80 | ((cp >> 6) & 0x3F);
                        buf[cnt++] = 0x80 | (cp & 0x3F);
                    }
                    else {
                        buf[cnt++] = 0xF0 | (cp >> 18);
                        buf[cnt++] = 0x80 | ((cp >> 12) & 0x3F);
                        buf[cnt++] = 0x80 | ((cp >> 6) & 0x3F);
                        buf[cnt++] = 0x80 | (cp & 0x3F);
                    }
                    continue; /* already advanced pos */
                }
                else { buf[cnt++] = esc; }
                l->pos++; l->col++;
            } else {
                buf[cnt++] = s[l->pos];
                l->pos++; l->col++;
            }
        }
        buf[cnt] = '\0';
        Token t = token_new(l, TOK_STR);
        t.text = buf;
        t.len = (l->pos - start) + 1;
        if (s[l->pos] == '"') { l->pos++; l->col++; }
        return t;
    }

    /* operators */
    while (1) {
        char c = s[l->pos];
        Token t = token_new(l, TOK_EOF);

        switch (c) {
        case '=':
            if (s[l->pos+1] == '=') { t.type = TOK_EQEQ; t.len = 2; l->pos+=2; l->col+=2; }
            else { t.type = TOK_EQ; l->pos++; l->col++; }
            break;
        case '!':
            if (s[l->pos+1] == '=') { t.type = TOK_NE; t.len = 2; l->pos+=2; l->col+=2; }
            else { t.type = TOK_NOT; l->pos++; l->col++; }
            break;
        case '<':
            if (s[l->pos+1] == '<') { t.type = TOK_SHL; t.len = 2; l->pos+=2; l->col+=2; }
            else if (s[l->pos+1] == '=') { t.type = TOK_LE; t.len = 2; l->pos+=2; l->col+=2; }
            else { t.type = TOK_LT; l->pos++; l->col++; }
            break;
        case '>':
            if (s[l->pos+1] == '>') { t.type = TOK_SHR; t.len = 2; l->pos+=2; l->col+=2; }
            else if (s[l->pos+1] == '=') { t.type = TOK_GE; t.len = 2; l->pos+=2; l->col+=2; }
            else { t.type = TOK_GT; l->pos++; l->col++; }
            break;
        case '-':
            if (s[l->pos+1] == '=') { t.type = TOK_MINUSEQ; t.len = 2; l->pos+=2; l->col+=2; }
            else if (s[l->pos+1] == '>') { t.type = TOK_ARROW; t.len = 2; l->pos+=2; l->col+=2; }
            else { t.type = TOK_MINUS; l->pos++; l->col++; }
            break;
        case '+':
            if (s[l->pos+1] == '=') { t.type = TOK_PLUSEQ; t.len = 2; l->pos+=2; l->col+=2; }
            else { t.type = TOK_PLUS; l->pos++; l->col++; }
            break;
        case '*':
            if (s[l->pos+1] == '*') { t.type = TOK_STARSTAR; t.len = 2; l->pos+=2; l->col+=2; }
            else if (s[l->pos+1] == '=') { t.type = TOK_STAREQ; t.len = 2; l->pos+=2; l->col+=2; }
            else { t.type = TOK_STAR; l->pos++; l->col++; }
            break;
        case '/':
            if (s[l->pos+1] == '=') { t.type = TOK_SLASHEQ; t.len = 2; l->pos+=2; l->col+=2; }
            else { t.type = TOK_SLASH; l->pos++; l->col++; }
            break;
        case '%': t.type = TOK_PERCENT; l->pos++; l->col++; break;
        case '(': t.type = TOK_LPAREN; l->pos++; l->col++; break;
        case ')': t.type = TOK_RPAREN; l->pos++; l->col++; break;
        case '[': t.type = TOK_LBRACK; l->pos++; l->col++; break;
        case ']': t.type = TOK_RBRACK; l->pos++; l->col++; break;
        case '.':
            if (s[l->pos+1] == '.') { t.type = TOK_DOTDOT; t.len = 2; l->pos+=2; l->col+=2; }
            else { t.type = TOK_DOT; l->pos++; l->col++; }
            break;
        case ',': t.type = TOK_COMMA; l->pos++; l->col++; break;
        case ':': t.type = TOK_COLON; l->pos++; l->col++; break;
        case '&':
            if (s[l->pos+1] == '&') { t.type = TOK_AND; t.len = 2; l->pos+=2; l->col+=2; }
            else { t.type = TOK_BITAND; l->pos++; l->col++; }
            break;
        case '|':
            if (s[l->pos+1] == '|') { t.type = TOK_OR; t.len = 2; l->pos+=2; l->col+=2; }
            else if (s[l->pos+1] == '>') { t.type = TOK_PIPE; t.len = 2; l->pos+=2; l->col+=2; }
            else { t.type = TOK_BITOR; l->pos++; l->col++; }
            break;
        case '^':
            t.type = TOK_BITXOR; l->pos++; l->col++; break;
        case '~':
            t.type = TOK_BITNOT; l->pos++; l->col++; break;
        default: l->pos++; l->col++; continue;
        }
        return t;
    }
}

Token lexer_peek(Lexer *l) {
    if (!l->has_peek) {
        l->peek = lexer_next(l);
        l->has_peek = 1;
    }
    return l->peek;
}
