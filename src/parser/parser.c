#include "parser.h"
#include "../diag.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int is_struct(Parser *p, const char *name) {
    for (int i = 0; i < p->struct_count; i++)
        if (strcmp(p->structs[i].name, name) == 0) return 1;
    return 0;
}

void parser_init(Parser *p, const char *filename, const char *source, Diag *diag) {
    lexer_init(&p->lexer, source);
    p->error_count = 0;
    p->filename = filename;
    p->diag = diag;
    p->struct_count = 0;
}

void parser_free(Parser *p) { (void)p; }

static Token peek(Parser *p) { return lexer_peek(&p->lexer); }
static Token next(Parser *p) { return lexer_next(&p->lexer); }

static int match(Parser *p, TokenType type) {
    Token t = peek(p);
    if (t.type == type) { next(p); return 1; }
    return 0;
}

static Token consume(Parser *p, TokenType type, const char *what) {
    Token t = next(p);
    if (t.type != type) {
        diag_add(p->diag, 2000, SEV_ERROR, t.line, t.col, t.len, what);
        p->error_count++;
    }
    return t;
}

/* forward */
static Node *parse_expr(Parser *p);
static Node *parse_stmt(Parser *p, int consume_nl);
static Node *parse_block(Parser *p);
static Node *parse_type(Parser *p);
static Node *parse_params(Parser *p);

static Node *parse_block(Parser *p) {
    Node *block = node_new(NODE_BLOCK);
    Node **tail = &block->block.stmts;
    Token t = peek(p);
    if (t.type == TOK_INDENT) {
        next(p);
        while (1) {
            Token look = peek(p);
            if (look.type == TOK_DEDENT || look.type == TOK_EOF) break;
            if (look.type == TOK_NEWLINE) { next(p); continue; }
            *tail = parse_stmt(p, 1);
            tail = &(*tail)->next;
        }
        if (peek(p).type == TOK_DEDENT) next(p);
    } else {
        *tail = parse_stmt(p, 0);
        tail = &(*tail)->next;
    }
    return block;
}

static Node *parse_type(Parser *p) {
    Token t = consume(p, TOK_IDENT, "expected type name");
    Node *n = node_new(NODE_IDENT);
    n->ident = t.text; t.text = NULL;
    return n;
}

static Node *parse_params(Parser *p) {
    Node *head = NULL, **tail = &head;
    consume(p, TOK_LPAREN, "expected '(' in parameter list");
    if (peek(p).type != TOK_RPAREN) {
        Token name = next(p);
        if (name.type != TOK_IDENT)
            diag_add(p->diag, 2001, SEV_ERROR, name.line, name.col, name.len, "expected parameter name");
        Node *param = node_new(NODE_LET);
        param->let.name = name.text; name.text = NULL;
        param->let.type = NULL;
        if (match(p, TOK_COLON)) param->let.type = parse_type(p);
        *tail = param; tail = &param->next;
        while (match(p, TOK_COMMA)) {
            Token n2 = next(p);
            if (n2.type != TOK_IDENT)
                diag_add(p->diag, 2001, SEV_ERROR, n2.line, n2.col, n2.len, "expected parameter name");
            Node *param2 = node_new(NODE_LET);
            param2->let.name = n2.text; n2.text = NULL;
            param2->let.type = NULL;
            if (match(p, TOK_COLON)) param2->let.type = parse_type(p);
            *tail = param2; tail = &param2->next;
        }
    }
    consume(p, TOK_RPAREN, "expected ')' after parameters");
    return head;
}

static Node *parse_item(Parser *p) {
    Token t = next(p);

    if (t.type == TOK_FN) {
        Token name = consume(p, TOK_IDENT, "expected function name");
        Node *n = node_new(NODE_FN_DECL);
        n->fn.name = name.text; name.text = NULL;
        n->fn.params = parse_params(p);
        n->fn.ret_type = NULL;
        n->fn.effect = match(p, TOK_EFFECT);
        if (match(p, TOK_ARROW)) n->fn.ret_type = parse_type(p);
        if (peek(p).type == TOK_COLON) next(p);
        if (!match(p, TOK_NEWLINE)) {
            n->fn.body = node_new(NODE_BLOCK);
            Node **tail = &n->fn.body->block.stmts;
            *tail = parse_stmt(p, 0);
        } else {
            if (peek(p).type == TOK_INDENT)
                n->fn.body = parse_block(p);
            else
                n->fn.body = node_new(NODE_BLOCK);
            while (peek(p).type == TOK_NEWLINE) next(p);
        }
        return n;
    }

    if (t.type == TOK_EXTERN) {
        Token fnkw = consume(p, TOK_FN, "expected 'fn' after 'extern'");
        (void)fnkw;
        Token name = consume(p, TOK_IDENT, "expected function name");
        Node *n = node_new(NODE_EXTERN_DECL);
        n->ext.name = name.text; name.text = NULL;
        n->ext.params = parse_params(p);
        n->ext.ret_type = NULL;
        if (match(p, TOK_ARROW)) n->ext.ret_type = parse_type(p);
        while (peek(p).type == TOK_NEWLINE || peek(p).type == TOK_DEDENT) {
            if (peek(p).type == TOK_NEWLINE) { next(p); continue; }
            break;
        }
        return n;
    }

    if (t.type == TOK_STRUCT) {
        Token name = consume(p, TOK_IDENT, "expected struct name");
        Node *n = node_new(NODE_STRUCT_DECL);
        n->struct_decl.name = name.text; name.text = NULL;
        n->struct_decl.fields = NULL;
        Node **tail = &n->struct_decl.fields;
        if (peek(p).type == TOK_COLON) next(p);
        if (peek(p).type == TOK_NEWLINE) next(p);
        if (peek(p).type == TOK_INDENT) {
            next(p);
            while (peek(p).type != TOK_DEDENT && peek(p).type != TOK_EOF) {
                Token ft = next(p);
                if (ft.type == TOK_NEWLINE) continue;
                if (ft.type == TOK_IDENT) {
                    Node *f = node_new(NODE_LET);
                    f->let.name = ft.text; ft.text = NULL;
                    f->let.init = NULL;
                    *tail = f; tail = &f->next;
                } else {
                    diag_add(p->diag, 2008, SEV_ERROR, ft.line, ft.col, ft.len, "expected field name in struct");
                    p->error_count++;
                }
            }
            if (peek(p).type == TOK_DEDENT) next(p);
        }
        /* register struct in parser's struct table */
        if (p->struct_count < MAX_PSTRUCTS) {
            PStructDef *ps = &p->structs[p->struct_count++];
            strncpy(ps->name, n->struct_decl.name, 63); ps->name[63] = 0;
            ps->field_count = 0;
            for (Node *f = n->struct_decl.fields; f; f = f->next) {
                if (f->type == NODE_LET && f->let.name && ps->field_count < MAX_PFIELDS) {
                    strncpy(ps->fields[ps->field_count], f->let.name, 63);
                    ps->fields[ps->field_count][63] = 0;
                    ps->field_count++;
                }
            }
        }
        return n;
    }

    diag_add(p->diag, 2002, SEV_ERROR, t.line, t.col, t.len, "unexpected token at top level — expected 'fn', 'extern', or 'struct'");
    p->error_count++;
    return NULL;
}

static Node *parse_let_stmt(Parser *p, int consume_nl, int mut) {
    Token name = consume(p, TOK_IDENT, "expected variable name");
    Node *n = node_new(NODE_LET);
    n->let.name = name.text; name.text = NULL;
    n->let.type = NULL;
    n->let.mut = mut;
    if (match(p, TOK_COLON)) n->let.type = parse_type(p);
    n->let.init = NULL;
    if (match(p, TOK_EQ)) n->let.init = parse_expr(p);
    if (consume_nl) while (peek(p).type == TOK_NEWLINE) next(p);
    return n;
}

static Node *parse_stmt(Parser *p, int consume_nl) {
    Token t = peek(p);

    switch (t.type) {
    case TOK_NEWLINE: next(p); return parse_stmt(p, consume_nl);
    case TOK_LET: next(p); return parse_let_stmt(p, consume_nl, match(p, TOK_MUT));
    case TOK_VAR: next(p); return parse_let_stmt(p, consume_nl, 1);
    case TOK_USE: {
        next(p);
        Token name = consume(p, TOK_IDENT, "expected variable name after 'use'");
        consume(p, TOK_EQ, "expected '=' after variable name");
        Node *init = parse_expr(p);
        if (peek(p).type == TOK_COLON) next(p);
        if (peek(p).type == TOK_NEWLINE) next(p);
        Node *body = parse_block(p);
        Node *let_n = node_new(NODE_LET);
        let_n->let.name = name.text; name.text = NULL;
        let_n->let.type = NULL;
        let_n->let.mut = 0;
        let_n->let.init = init;
        Node *block = node_new(NODE_BLOCK);
        block->block.stmts = let_n;
        let_n->next = body->block.stmts;
        if (consume_nl) while (peek(p).type == TOK_NEWLINE) next(p);
        return block;
    }
    case TOK_IF: {
        next(p);
        Node *n = node_new(NODE_IF);
        n->if_stmt.cond = parse_expr(p);
        if (peek(p).type == TOK_COLON) next(p);
        if (peek(p).type == TOK_NEWLINE) next(p);
        n->if_stmt.then = parse_block(p);
        n->if_stmt.otherwise = NULL;
        Node **tail = &n->if_stmt.otherwise;
        while (peek(p).type == TOK_ELIF) {
            next(p);
            Node *elif = node_new(NODE_IF);
            elif->if_stmt.cond = parse_expr(p);
            if (peek(p).type == TOK_COLON) next(p);
            if (peek(p).type == TOK_NEWLINE) next(p);
            elif->if_stmt.then = parse_block(p);
            elif->if_stmt.otherwise = NULL;
            *tail = elif; tail = &elif->if_stmt.otherwise;
        }
        if (match(p, TOK_ELSE)) {
            if (peek(p).type == TOK_COLON) next(p);
            if (peek(p).type == TOK_NEWLINE) next(p);
            *tail = parse_block(p);
        }
        if (consume_nl) while (peek(p).type == TOK_NEWLINE) next(p);
        return n;
    }
    case TOK_WHILE: {
        next(p);
        Node *n = node_new(NODE_WHILE);
        n->while_stmt.cond = parse_expr(p);
        if (peek(p).type == TOK_COLON) next(p);
        if (peek(p).type == TOK_NEWLINE) next(p);
        n->while_stmt.body = parse_block(p);
        if (consume_nl) while (peek(p).type == TOK_NEWLINE) next(p);
        return n;
    }
    case TOK_FOR: {
        next(p);
        Token var_token = consume(p, TOK_IDENT, "expected loop variable");
        char *var_name = var_token.text;
        consume(p, TOK_IN, "expected 'in'");
        Node *start = parse_expr(p);
        Node *end;
        if (match(p, TOK_DOTDOT)) {
            end = parse_expr(p);
        } else {
            Token nxt = peek(p);
            diag_add(p->diag, 2005, SEV_ERROR, nxt.line, nxt.col, nxt.len,
                     "expected '..' in range expression");
            p->error_count++;
            end = node_new(NODE_INT); end->int_val = 0;
        }
        if (peek(p).type == TOK_COLON) next(p);
        if (peek(p).type == TOK_NEWLINE) next(p);
        Node *body = parse_block(p);
        Node *inc_lhs = node_new(NODE_IDENT); inc_lhs->ident = strdup(var_name);
        Node *inc_rhs_l = node_new(NODE_IDENT); inc_rhs_l->ident = strdup(var_name);
        Node *inc_rhs_r = node_new(NODE_INT); inc_rhs_r->int_val = 1;
        Node *inc_bin = node_new(NODE_BINARY);
        inc_bin->binary.left = inc_rhs_l; inc_bin->binary.right = inc_rhs_r; inc_bin->binary.op = 0;
        Node *inc = node_new(NODE_ASSIGN);
        inc->assign.lhs = inc_lhs; inc->assign.rhs = inc_bin;
        Node *while_body = node_new(NODE_BLOCK);
        if (body->block.stmts) {
            Node *tail = body->block.stmts;
            while (tail->next) tail = tail->next;
            tail->next = inc;
            while_body->block.stmts = body->block.stmts;
        } else {
            while_body->block.stmts = inc;
        }
        Node *cond_l = node_new(NODE_IDENT); cond_l->ident = strdup(var_name);
        Node *cond = node_new(NODE_BINARY);
        cond->binary.left = cond_l; cond->binary.right = end; cond->binary.op = 7;
        Node *while_node = node_new(NODE_WHILE);
        while_node->while_stmt.cond = cond; while_node->while_stmt.body = while_body;
        Node *let = node_new(NODE_LET);
        let->let.name = var_name; let->let.init = start; let->let.mut = 1;
        Node *block = node_new(NODE_BLOCK);
        block->block.stmts = let;
        let->next = while_node;
        if (consume_nl) while (peek(p).type == TOK_NEWLINE) next(p);
        return block;
    }
    case TOK_BREAK: {
        next(p);
        Node *n = node_new(NODE_BREAK);
        if (consume_nl) while (peek(p).type == TOK_NEWLINE) next(p);
        return n;
    }
    case TOK_CONTINUE: {
        next(p);
        Node *n = node_new(NODE_CONTINUE);
        if (consume_nl) while (peek(p).type == TOK_NEWLINE) next(p);
        return n;
    }
    case TOK_RETURN: {
        next(p);
        Node *n = node_new(NODE_RETURN);
        t = peek(p);
        if (t.type != TOK_NEWLINE && t.type != TOK_DEDENT && t.type != TOK_EOF &&
            t.type != TOK_ELIF && t.type != TOK_ELSE)
            n->ret.val = parse_expr(p);
        if (consume_nl) while (peek(p).type == TOK_NEWLINE) next(p);
        return n;
    }
    default: {
        Node *expr = parse_expr(p);
        TokenType tt = peek(p).type;
        if (tt == TOK_EQ || tt == TOK_PLUSEQ || tt == TOK_MINUSEQ || tt == TOK_STAREQ || tt == TOK_SLASHEQ) {
            Node *n = node_new(NODE_ASSIGN);
            n->assign.lhs = expr;
            if (tt == TOK_EQ) {
                next(p);
                n->assign.rhs = parse_expr(p);
            } else {
                int op = (tt == TOK_PLUSEQ) ? 0 : (tt == TOK_MINUSEQ) ? 1 : (tt == TOK_STAREQ) ? 2 : 3;
                if (expr->type != NODE_IDENT)
                    diag_add(p->diag, 2006, SEV_ERROR, peek(p).line, peek(p).col, peek(p).len,
                             "compound assignment requires a variable");
                next(p);
                Node *rhs = parse_expr(p);
                Node *id_copy = node_new(NODE_IDENT);
                id_copy->ident = (expr->type == NODE_IDENT) ? strdup(expr->ident) : strdup("?");
                Node *bin = node_new(NODE_BINARY);
                bin->binary.left = id_copy;
                bin->binary.right = rhs;
                bin->binary.op = op;
                n->assign.rhs = bin;
            }
            if (consume_nl) while (peek(p).type == TOK_NEWLINE) next(p);
            return n;
        }
        /* postfix if / unless */
        if (tt == TOK_IF) {
            next(p);
            Node *cond = parse_expr(p);
            Node *stmt = node_new(NODE_IF);
            stmt->if_stmt.cond = cond;
            Node *body = node_new(NODE_BLOCK);
            body->block.stmts = node_new(NODE_EXPR_STMT);
            body->block.stmts->expr_stmt.expr = expr;
            stmt->if_stmt.then = body;
            stmt->if_stmt.otherwise = NULL;
            if (consume_nl) while (peek(p).type == TOK_NEWLINE) next(p);
            return stmt;
        }
        if (tt == TOK_UNLESS) {
            next(p);
            Node *cond = parse_expr(p);
            Node *not_cond = node_new(NODE_UNARY);
            not_cond->unary.op = 0;
            not_cond->unary.operand = cond;
            Node *stmt = node_new(NODE_IF);
            stmt->if_stmt.cond = not_cond;
            Node *body = node_new(NODE_BLOCK);
            body->block.stmts = node_new(NODE_EXPR_STMT);
            body->block.stmts->expr_stmt.expr = expr;
            stmt->if_stmt.then = body;
            stmt->if_stmt.otherwise = NULL;
            if (consume_nl) while (peek(p).type == TOK_NEWLINE) next(p);
            return stmt;
        }
        Node *n = node_new(NODE_EXPR_STMT);
        n->expr_stmt.expr = expr;
        if (consume_nl) while (peek(p).type == TOK_NEWLINE) next(p);
        return n;
    }
    }
}

/* Expression parsing with precedence */
typedef enum { PREC_MIN, PREC_PIPE, PREC_OR, PREC_AND, PREC_EQ, PREC_CMP, PREC_ADD, PREC_MUL, PREC_UNARY, PREC_CALL } Precedence;

static Precedence token_prec(TokenType t) {
    switch (t) {
    case TOK_PIPE: return PREC_PIPE;
    case TOK_OR: return PREC_OR;
    case TOK_AND: return PREC_AND;
    case TOK_EQEQ: case TOK_NE: return PREC_EQ;
    case TOK_LT: case TOK_LE: case TOK_GT: case TOK_GE: return PREC_CMP;
    case TOK_PLUS: case TOK_MINUS: return PREC_ADD;
    case TOK_STAR: case TOK_SLASH: case TOK_PERCENT: return PREC_MUL;
    default: return PREC_MIN;
    }
}

static int binop_type(TokenType t) {
    switch (t) {
    case TOK_PLUS: return 0; case TOK_MINUS: return 1; case TOK_STAR: return 2;
    case TOK_SLASH: return 3; case TOK_PERCENT: return 4;
    case TOK_EQEQ: return 5; case TOK_NE: return 6;
    case TOK_LT: return 7; case TOK_LE: return 8; case TOK_GT: return 9; case TOK_GE: return 10;
    case TOK_AND: return 11; case TOK_OR: return 12;
    default: return -1;
    }
}

static Node *parse_expr_prec(Parser *p, Precedence min_prec) {
    Token t = peek(p);
    Node *left = NULL;

    switch (t.type) {
    case TOK_MINUS:
        next(p); left = node_new(NODE_UNARY);
        left->unary.op = 1;
        left->unary.operand = parse_expr_prec(p, PREC_UNARY);
        break;
    case TOK_NOT:
        next(p); left = node_new(NODE_UNARY);
        left->unary.op = 0;
        left->unary.operand = parse_expr_prec(p, PREC_UNARY);
        break;
    case TOK_INT:
        next(p); left = node_new(NODE_INT);
        left->int_val = t.int_val; left->next = NULL;
        break;
    case TOK_STR:
        next(p); left = node_new(NODE_STR);
        left->str_val = t.text; t.text = NULL;
        break;
    case TOK_TRUE: next(p); left = node_new(NODE_BOOL); left->bool_val = 1; break;
    case TOK_FALSE: next(p); left = node_new(NODE_BOOL); left->bool_val = 0; break;
    case TOK_IDENT:
        next(p); left = node_new(NODE_IDENT);
        left->ident = t.text; t.text = NULL;
        break;
    case TOK_LBRACK: {
        next(p);
        Node *map = parse_expr(p);
        if (peek(p).type == TOK_FOR) {
            next(p);
            Token var = consume(p, TOK_IDENT, "expected loop variable");
            consume(p, TOK_IN, "expected 'in'");
            Node *iter = parse_expr(p);
            Node *iter_end = NULL;
            if (match(p, TOK_DOTDOT)) iter_end = parse_expr(p);
            Node *filter = NULL;
            if (peek(p).type == TOK_IF) {
                next(p);
                filter = parse_expr(p);
            }
            consume(p, TOK_RBRACK, "expected ']'");
            Node *comp = node_new(NODE_COMPREHENSION);
            comp->comp.map = map;
            comp->comp.var = var.text; var.text = NULL;
            comp->comp.iter = iter;
            comp->comp.iter_end = iter_end;
            comp->comp.filter = filter;
            left = comp;
        } else {
            diag_add(p->diag, 2007, SEV_ERROR, peek(p).line, peek(p).col, peek(p).len,
                     "expected 'for' in list comprehension");
            p->error_count++;
            consume(p, TOK_RBRACK, "expected ']'");
            left = node_new(NODE_INT); left->int_val = 0;
        }
        break;
    }
    case TOK_LPAREN:
        next(p); left = parse_expr(p);
        consume(p, TOK_RPAREN, "expected ')'");
        break;
    default:
        diag_add(p->diag, 2004, SEV_ERROR, t.line, t.col, t.len, "expected expression");
        p->error_count++;
        next(p); left = node_new(NODE_INT); left->int_val = 0;
        break;
    }

    /* postfix: calls, indexing */
    while (1) {
        Token n = peek(p);
        if (n.type == TOK_LPAREN) {
            next(p);
            /* check if this is a struct literal */
            if (left->type == NODE_IDENT && is_struct(p, left->ident)) {
                Node *sl = node_new(NODE_STRUCT_LITERAL);
                sl->struct_literal.name = left->ident; left->ident = NULL;
                sl->struct_literal.args = NULL;
                Node **tail = &sl->struct_literal.args;
                if (peek(p).type != TOK_RPAREN) {
                    *tail = parse_expr(p); tail = &(*tail)->next;
                    while (match(p, TOK_COMMA)) { *tail = parse_expr(p); tail = &(*tail)->next; }
                }
                consume(p, TOK_RPAREN, "expected ')' after struct fields");
                left = sl;
            } else {
                Node *call = node_new(NODE_CALL);
                call->call.callee = left;
                call->call.args = NULL;
                Node **tail = &call->call.args;
                if (peek(p).type != TOK_RPAREN) {
                    *tail = parse_expr(p); tail = &(*tail)->next;
                    while (match(p, TOK_COMMA)) { *tail = parse_expr(p); tail = &(*tail)->next; }
                }
                consume(p, TOK_RPAREN, "expected ')' after arguments");
                left = call;
            }
        } else if (n.type == TOK_LBRACK) {
            next(p);
            Node *idx = node_new(NODE_INDEX);
            idx->index_expr.obj = left;
            idx->index_expr.index = parse_expr(p);
            consume(p, TOK_RBRACK, "expected ']'");
            left = idx;
        } else if (n.type == TOK_DOT) {
            next(p);
            Token field = consume(p, TOK_IDENT, "expected field name");
            Node *idx = node_new(NODE_INDEX);
            idx->index_expr.obj = left;
            idx->index_expr.index = node_new(NODE_IDENT);
            idx->index_expr.index->ident = field.text; field.text = NULL;
            left = idx;
        } else break;
    }

    /* binary */
    while (1) {
        Token n = peek(p);
        Precedence prec = token_prec(n.type);
        if (prec == PREC_MIN || prec <= min_prec) break;
        /* pipe operator: a |> f  =>  f(a) */
        if (n.type == TOK_PIPE) {
            next(p);
            Node *right = parse_expr_prec(p, (Precedence)(prec + 1));
            Node *call = node_new(NODE_CALL);
            if (right->type == NODE_CALL) {
                /* a |> f(b) => f(a, b): prepend left to existing args */
                Node **tail = &right->call.args;
                while (*tail) tail = &(*tail)->next;
                *tail = left;
                call = right;
            } else {
                /* a |> f => f(a): wrap right as callee */
                call->call.callee = right;
                call->call.args = left;
            }
            left = call;
            continue;
        }
        int op = binop_type(n.type);
        if (op < 0) break;
        next(p);
        Node *right = parse_expr_prec(p, (Precedence)(prec + 1));
        Node *bin = node_new(NODE_BINARY);
        bin->binary.left = left; bin->binary.right = right; bin->binary.op = op;
        left = bin;
    }
    return left;
}

static Node *parse_expr(Parser *p) { return parse_expr_prec(p, PREC_MIN); }

Node *parser_parse(Parser *p) {
    Node *prog = node_new(NODE_PROGRAM);
    Node **tail = &prog->next;

    while (1) {
        Token t = peek(p);
        if (t.type == TOK_EOF) break;
        if (t.type == TOK_NEWLINE) { next(p); continue; }
        if (t.type == TOK_DEDENT) { next(p); continue; }
        Node *item = parse_item(p);
        if (item) { *tail = item; tail = &item->next; }
    }
    return prog;
}
