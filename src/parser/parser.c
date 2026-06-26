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

static int is_enum(Parser *p, const char *name) {
    for (int i = 0; i < p->enum_count; i++)
        if (strcmp(p->enums[i].name, name) == 0) return 1;
    return 0;
}

static int find_enum_variant(Parser *p, const char *name, int *out_enum_idx, int *out_variant_idx) {
    for (int ei = 0; ei < p->enum_count; ei++) {
        for (int vi = 0; vi < p->enums[ei].variant_count; vi++) {
            if (strcmp(p->enums[ei].variants[vi], name) == 0) {
                if (out_enum_idx) *out_enum_idx = ei;
                if (out_variant_idx) *out_variant_idx = vi;
                return 1;
            }
        }
    }
    return 0;
}

void parser_init(Parser *p, const char *filename, const char *source, Diag *diag) {
    lexer_init(&p->lexer, source);
    p->error_count = 0;
    p->filename = filename;
    p->diag = diag;
    p->struct_count = 0;
    p->enum_count = 0;
    p->current_type_params.count = 0;
    p->lambda_count = 0;
    p->lambdas = NULL;
    p->lambdas_tail = &p->lambdas;
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
    Token t = peek(p);
    if (t.type == TOK_SELF) {
        next(p);
        Node *n = node_new(NODE_IDENT);
        n->ident = strdup("Self");
        n->src_line = t.line; n->src_col = t.col;
        return n;
    }
    if (t.type == TOK_LPAREN) {
        Token n2 = lexer_peek(&p->lexer);
        if (n2.type == TOK_RPAREN) {
            next(p); next(p);
            Node *n = node_new(NODE_IDENT);
            n->ident = strdup("()");
            n->src_line = t.line; n->src_col = t.col;
            return n;
        }
    }
    t = consume(p, TOK_IDENT, "expected type name");
    Node *n = node_new(NODE_IDENT);
    n->ident = t.text; t.text = NULL;
    n->src_line = t.line; n->src_col = t.col;
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
        param->src_line = name.line; param->src_col = name.col;
        if (match(p, TOK_COLON)) param->let.type = parse_type(p);
        *tail = param; tail = &param->next;
        while (match(p, TOK_COMMA)) {
            Token n2 = next(p);
            if (n2.type != TOK_IDENT)
                diag_add(p->diag, 2001, SEV_ERROR, n2.line, n2.col, n2.len, "expected parameter name");
            Node *param2 = node_new(NODE_LET);
            param2->let.name = n2.text; n2.text = NULL;
            param2->let.type = NULL;
            param2->src_line = n2.line; param2->src_col = n2.col;
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
        n->src_line = t.line; n->src_col = t.col;
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
        n->src_line = t.line; n->src_col = t.col;
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
        n->src_line = t.line; n->src_col = t.col;
        n->struct_decl.fields = NULL;
        Node **tail = &n->struct_decl.fields;
        if (peek(p).type == TOK_COLON) next(p);
        if (peek(p).type == TOK_NEWLINE) {
            next(p);
            if (peek(p).type == TOK_INDENT) {
                next(p);
                while (peek(p).type != TOK_DEDENT && peek(p).type != TOK_EOF) {
                    Token ft = next(p);
                    if (ft.type == TOK_NEWLINE) continue;
                    if (ft.type == TOK_IDENT) {
                        Node *f = node_new(NODE_LET);
                        f->let.name = ft.text; ft.text = NULL;
                        f->let.init = NULL;
                        f->src_line = ft.line; f->src_col = ft.col;
                        *tail = f; tail = &f->next;
                    } else {
                        diag_add(p->diag, 2008, SEV_ERROR, ft.line, ft.col, ft.len, "expected field name in struct");
                        p->error_count++;
                    }
                }
                if (peek(p).type == TOK_DEDENT) next(p);
            }
        } else {
            /* one-liner: struct Foo: bar */
            while (peek(p).type == TOK_IDENT) {
                Token ft = next(p);
                Node *f = node_new(NODE_LET);
                f->let.name = ft.text; ft.text = NULL;
                f->let.init = NULL;
                f->src_line = ft.line; f->src_col = ft.col;
                *tail = f; tail = &f->next;
                if (peek(p).type == TOK_COMMA) next(p);
            }
        }
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

    if (t.type == TOK_ENUM) {
        Token name = consume(p, TOK_IDENT, "expected enum name");
        Node *n = node_new(NODE_ENUM_DECL);
        n->enum_decl.name = name.text; name.text = NULL;
        n->enum_decl.type_params = NULL;
        n->src_line = t.line; n->src_col = t.col;
        /* parse optional type params <T, E> */
        if (peek(p).type == TOK_LT) {
            next(p);
            Node **tp_tail = &n->enum_decl.type_params;
            do {
                Token tp = consume(p, TOK_IDENT, "expected type parameter");
                Node *tp_node = node_new(NODE_IDENT);
                tp_node->ident = tp.text; tp.text = NULL;
                *tp_tail = tp_node; tp_tail = &tp_node->next;
                if (p->current_type_params.count < MAX_TYPE_PARAMS) {
                    strncpy(p->current_type_params.names[p->current_type_params.count], tp_node->ident, 63);
                    p->current_type_params.count++;
                }
            } while (match(p, TOK_COMMA));
            consume(p, TOK_GT, "expected '>' after type parameters");
        }
        n->enum_decl.variants = NULL;
        Node **tail = &n->enum_decl.variants;
        if (peek(p).type == TOK_COLON) next(p);
        if (peek(p).type == TOK_NEWLINE) next(p);
        if (peek(p).type == TOK_INDENT) {
            next(p);
            while (peek(p).type != TOK_DEDENT && peek(p).type != TOK_EOF) {
                Token vt = next(p);
                if (vt.type == TOK_NEWLINE) continue;
                if (vt.type == TOK_IDENT) {
                    Node *variant = node_new(NODE_MATCH_ARM);
                    variant->match_arm.variant = vt.text; vt.text = NULL;
                    variant->match_arm.payload = NULL;
                    variant->match_arm.guard = NULL;
                    variant->match_arm.body = NULL;
                    /* check for payload: Option -> Some(Type) */
                    if (peek(p).type == TOK_LPAREN) {
                        next(p);
                        variant->match_arm.payload = parse_type(p);
                        while (match(p, TOK_COMMA)) {
                            Node *next_type = parse_type(p);
                            Node *last = variant->match_arm.payload;
                            while (last->next) last = last->next;
                            last->next = next_type;
                        }
                        consume(p, TOK_RPAREN, "expected ')' after variant payload");
                    }
                    *tail = variant; tail = &variant->next;
                } else {
                    diag_add(p->diag, 2008, SEV_ERROR, vt.line, vt.col, vt.len, "expected variant name in enum");
                    p->error_count++;
                }
            }
            if (peek(p).type == TOK_DEDENT) next(p);
        }
        /* register enum in parser table */
        if (p->enum_count < MAX_PENUMS) {
            PEnumDef *pe = &p->enums[p->enum_count++];
            strncpy(pe->name, n->enum_decl.name, 63); pe->name[63] = 0;
            pe->variant_count = 0;
            pe->total_size = 0;
            for (Node *v = n->enum_decl.variants; v; v = v->next) {
                if (v->type == NODE_MATCH_ARM && v->match_arm.variant && pe->variant_count < MAX_VARIANTS) {
                    strncpy(pe->variants[pe->variant_count], v->match_arm.variant, 63);
                    pe->variants[pe->variant_count][63] = 0;
                    int payload_size = 0;
                    for (Node *pld = v->match_arm.payload; pld; pld = pld->next) payload_size += 8;
                    pe->payloads[pe->variant_count] = payload_size;
                    if (payload_size > pe->total_size) pe->total_size = payload_size;
                    pe->variant_count++;
                }
            }
            pe->total_size += 8;
        }
        p->current_type_params.count = 0;
        return n;
    }

    if (t.type == TOK_TRAIT) {
        Token name = consume(p, TOK_IDENT, "expected trait name");
        Node *n = node_new(NODE_TRAIT_DECL);
        n->trait_decl.name = name.text; name.text = NULL;
        n->trait_decl.type_params = NULL;
        n->trait_decl.methods = NULL;
        n->src_line = t.line; n->src_col = t.col;
        if (match(p, TOK_COLON)) {}
        if (peek(p).type == TOK_NEWLINE) next(p);
        if (peek(p).type == TOK_INDENT) {
            next(p);
            Node **mtail = &n->trait_decl.methods;
            while (peek(p).type != TOK_DEDENT && peek(p).type != TOK_EOF) {
                Token mt = next(p);
                if (mt.type == TOK_NEWLINE) continue;
                if (mt.type == TOK_FN) {
                    Token mname = consume(p, TOK_IDENT, "expected method name");
                    Node *method = node_new(NODE_FN_DECL);
                    method->fn.name = mname.text; mname.text = NULL;
                    method->fn.params = parse_params(p);
                    method->fn.ret_type = NULL;
                    method->fn.effect = match(p, TOK_EFFECT);
                    if (match(p, TOK_ARROW)) method->fn.ret_type = parse_type(p);
                    *mtail = method; mtail = &method->next;
                } else {
                    diag_add(p->diag, 2002, SEV_ERROR, mt.line, mt.col, mt.len, "expected 'fn' in trait body");
                    p->error_count++;
                }
            }
            if (peek(p).type == TOK_DEDENT) next(p);
        }
        return n;
    }

    if (t.type == TOK_IMPL) {
        Token for_type_token = consume(p, TOK_IDENT, "expected type name after 'impl'");
        Node *n = node_new(NODE_IMPL_BLOCK);
        n->impl_block.name = for_type_token.text; for_type_token.text = NULL;
        n->impl_block.for_type = NULL;
        if (peek(p).type == TOK_FOR) {
            next(p);
            n->impl_block.for_type = node_new(NODE_IDENT);
            n->impl_block.for_type->ident = strdup(peek(p).text);
            next(p);
        }
        n->src_line = t.line; n->src_col = t.col;
        n->impl_block.methods = NULL;
        if (peek(p).type == TOK_COLON) next(p);
        if (peek(p).type == TOK_NEWLINE) next(p);
        if (peek(p).type == TOK_INDENT) {
            next(p);
            Node **mtail = &n->impl_block.methods;
            while (peek(p).type != TOK_DEDENT && peek(p).type != TOK_EOF) {
                Token mt = next(p);
                if (mt.type == TOK_NEWLINE) continue;
                if (mt.type == TOK_FN) {
                    Token mname = consume(p, TOK_IDENT, "expected method name");
                    Node *method = node_new(NODE_FN_DECL);
                    method->fn.name = mname.text; mname.text = NULL;
                    method->fn.params = parse_params(p);
                    method->fn.ret_type = NULL;
                    method->fn.effect = match(p, TOK_EFFECT);
                    if (match(p, TOK_ARROW)) method->fn.ret_type = parse_type(p);
                    if (peek(p).type == TOK_COLON) next(p);
                    if (!match(p, TOK_NEWLINE)) {
                        method->fn.body = node_new(NODE_BLOCK);
                        Node **tail = &method->fn.body->block.stmts;
                        *tail = parse_stmt(p, 0);
                    } else {
                        if (peek(p).type == TOK_INDENT)
                            method->fn.body = parse_block(p);
                        else
                            method->fn.body = node_new(NODE_BLOCK);
                        while (peek(p).type == TOK_NEWLINE) next(p);
                    }
                    *mtail = method; mtail = &method->next;
                } else {
                    diag_add(p->diag, 2002, SEV_ERROR, mt.line, mt.col, mt.len, "expected 'fn' in impl body");
                    p->error_count++;
                }
            }
            if (peek(p).type == TOK_DEDENT) next(p);
        }
        return n;
    }

    diag_add(p->diag, 2002, SEV_ERROR, t.line, t.col, t.len, "unexpected token at top level — expected 'fn', 'extern', 'struct', 'enum', 'trait', or 'impl'");
    p->error_count++;
    return NULL;
}

static Node *parse_let_stmt(Parser *p, int consume_nl, int mut) {
    Token name = consume(p, TOK_IDENT, "expected variable name");
    Node *n = node_new(NODE_LET);
    n->let.name = name.text; name.text = NULL;
    n->let.type = NULL;
    n->let.mut = mut;
    n->src_line = name.line; n->src_col = name.col;
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
        let_n->src_line = name.line; let_n->src_col = name.col;
        Node *block = node_new(NODE_BLOCK);
        block->block.stmts = let_n;
        let_n->next = body->block.stmts;
        if (consume_nl) while (peek(p).type == TOK_NEWLINE) next(p);
        return block;
    }
    case TOK_MATCH: {
        next(p);
        Node *expr = parse_expr(p);
        Node *n = node_new(NODE_MATCH);
        n->match.expr = expr;
        n->match.arms = NULL;
        n->src_line = t.line; n->src_col = t.col;
        if (peek(p).type == TOK_COLON) next(p);
        if (peek(p).type == TOK_NEWLINE) next(p);
        if (peek(p).type == TOK_INDENT) {
            next(p);
            Node **tail = &n->match.arms;
            while (peek(p).type != TOK_DEDENT && peek(p).type != TOK_EOF) {
                Token at = peek(p);
                if (at.type == TOK_NEWLINE) { next(p); continue; }
                /* parse match arm: pattern => body */
                Node *arm = node_new(NODE_MATCH_ARM);
                arm->match_arm.variant = NULL;
                arm->match_arm.payload = NULL;
                arm->match_arm.guard = NULL;
                arm->match_arm.body = NULL;
                arm->src_line = at.line; arm->src_col = at.col;
                /* pattern: wildcard _, ident, or Variant(payload) */
                if (peek(p).type == TOK_IDENT) {
                    Token pt = next(p);
                    /* check for wildcard _ */
                    if (strcmp(pt.text, "_") == 0) {
                        arm->match_arm.variant = strdup("_");
                    }
                    /* check if this is a variant pattern like Some(x) or just ident */
                    else if (peek(p).type == TOK_LPAREN) {
                        arm->match_arm.variant = pt.text; pt.text = NULL;
                        next(p);
                        arm->match_arm.payload = parse_expr(p);
                        /* for id patterns like Some(x), use x as payload ident */
                        consume(p, TOK_RPAREN, "expected ')' after pattern args");
                    } else {
                        arm->match_arm.variant = pt.text; pt.text = NULL;
                    }
                }
                else if (peek(p).type == TOK_INT || peek(p).type == TOK_CHAR || peek(p).type == TOK_TRUE || peek(p).type == TOK_FALSE) {
                    Token lt = next(p);
                    arm->match_arm.variant = strdup("_literal");
                    Node *lit_body = node_new(NODE_EXPR_STMT);
                    if (lt.type == TOK_INT || lt.type == TOK_CHAR) {
                        lit_body->expr_stmt.expr = node_new(NODE_INT);
                        lit_body->expr_stmt.expr->int_val = lt.int_val;
                    } else {
                        lit_body->expr_stmt.expr = node_new(NODE_BOOL);
                        lit_body->expr_stmt.expr->bool_val = (lt.type == TOK_TRUE) ? 1 : 0;
                    }
                    arm->match_arm.body = lit_body;
                }
                /* consume '=>', '->', or ':' after pattern */
                if (match(p, TOK_EQ)) {
                    if (peek(p).type == TOK_GT) next(p);
                }
                if (match(p, TOK_ARROW)) {}
                if (match(p, TOK_COLON)) {}
                /* if guard */
                if (match(p, TOK_IF)) {
                    arm->match_arm.guard = parse_expr(p);
                }
                /* body */
                if (peek(p).type == TOK_NEWLINE) {
                    next(p);
                    if (peek(p).type == TOK_INDENT) {
                        arm->match_arm.body = parse_block(p);
                    } else {
                        arm->match_arm.body = parse_stmt(p, 1);
                    }
                } else {
                    arm->match_arm.body = parse_stmt(p, 0);
                }
                *tail = arm; tail = &arm->next;
            }
            if (peek(p).type == TOK_DEDENT) next(p);
        }
        if (consume_nl) while (peek(p).type == TOK_NEWLINE) next(p);
        return n;
    }
    case TOK_IF: {
        next(p);
        Node *n = node_new(NODE_IF);
        n->if_stmt.cond = parse_expr(p);
        n->src_line = t.line; n->src_col = t.col;
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
        n->src_line = t.line; n->src_col = t.col;
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
        let->src_line = var_token.line; let->src_col = var_token.col;
        Node *block = node_new(NODE_BLOCK);
        block->block.stmts = let;
        let->next = while_node;
        if (consume_nl) while (peek(p).type == TOK_NEWLINE) next(p);
        return block;
    }
    case TOK_UNSAFE: {
        next(p);
        if (match(p, TOK_COLON)) {}
        if (peek(p).type == TOK_NEWLINE) {
            next(p);
            Node *block = parse_block(p);
            if (consume_nl) while (peek(p).type == TOK_NEWLINE) next(p);
            return block;
        }
        Node *body = parse_stmt(p, consume_nl);
        if (consume_nl) while (peek(p).type == TOK_NEWLINE) next(p);
        return body;
    }
    case TOK_BREAK: {
        next(p);
        Node *n = node_new(NODE_BREAK);
        n->src_line = t.line; n->src_col = t.col;
        if (consume_nl) while (peek(p).type == TOK_NEWLINE) next(p);
        return n;
    }
    case TOK_CONTINUE: {
        next(p);
        Node *n = node_new(NODE_CONTINUE);
        n->src_line = t.line; n->src_col = t.col;
        if (consume_nl) while (peek(p).type == TOK_NEWLINE) next(p);
        return n;
    }
    case TOK_RETURN: {
        next(p);
        Node *n = node_new(NODE_RETURN);
        n->src_line = t.line; n->src_col = t.col;
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
            n->src_line = t.line; n->src_col = t.col;
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
        n->src_line = t.line; n->src_col = t.col;
        if (consume_nl) while (peek(p).type == TOK_NEWLINE) next(p);
        return n;
    }
    }
}

/* Expression parsing with precedence */
typedef enum {
    PREC_MIN, PREC_PIPE,
    PREC_OR, PREC_BITOR, PREC_BITXOR,
    PREC_AND, PREC_BITAND,
    PREC_EQ,
    PREC_CMP, PREC_SHIFT,
    PREC_ADD,
    PREC_MUL,
    PREC_UNARY,
    PREC_POW,
    PREC_CALL
} Precedence;

static Precedence token_prec(TokenType t) {
    switch (t) {
    case TOK_PIPE: return PREC_PIPE;
    case TOK_OR: return PREC_OR;
    case TOK_BITOR: return PREC_BITOR;
    case TOK_AND: return PREC_AND;
    case TOK_BITXOR: return PREC_BITXOR;
    case TOK_BITAND: return PREC_BITAND;
    case TOK_EQEQ: case TOK_NE: return PREC_EQ;
    case TOK_LT: case TOK_LE: case TOK_GT: case TOK_GE: return PREC_CMP;
    case TOK_SHL: case TOK_SHR: return PREC_SHIFT;
    case TOK_PLUS: case TOK_MINUS: return PREC_ADD;
    case TOK_STAR: case TOK_SLASH: case TOK_PERCENT: return PREC_MUL;
    case TOK_AS: return PREC_ADD;
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
    case TOK_BITOR: return 13; case TOK_BITXOR: return 14; case TOK_BITAND: return 15;
    case TOK_SHL: return 16; case TOK_SHR: return 17;
    default: return -1;
    }
}

static Node *parse_expr_prec(Parser *p, Precedence min_prec) {
    Token t = peek(p);
    Node *left = NULL;

    switch (t.type) {
    case TOK_AMPERSAND:
    case TOK_BITAND: {
        int mut = 0;
        next(p);
        if (peek(p).type == TOK_MUT) { next(p); mut = 1; }
        left = node_new(mut ? NODE_MUT_BORROW : NODE_BORROW);
        left->borrow.operand = parse_expr_prec(p, PREC_UNARY);
        left->borrow.mut = mut;
        left->src_line = t.line; left->src_col = t.col;
        break;
    }
    case TOK_STAR:
        next(p); left = node_new(NODE_DEREF);
        left->borrow.operand = parse_expr_prec(p, PREC_UNARY);
        left->src_line = t.line; left->src_col = t.col;
        break;
    case TOK_MINUS:
        next(p); left = node_new(NODE_UNARY);
        left->unary.op = 1;
        left->unary.operand = parse_expr_prec(p, PREC_UNARY);
        left->src_line = t.line; left->src_col = t.col;
        break;
    case TOK_NOT:
        next(p); left = node_new(NODE_UNARY);
        left->unary.op = 0;
        left->unary.operand = parse_expr_prec(p, PREC_UNARY);
        left->src_line = t.line; left->src_col = t.col;
        break;
    case TOK_BITNOT:
        next(p); left = node_new(NODE_UNARY);
        left->unary.op = 2;
        left->unary.operand = parse_expr_prec(p, PREC_UNARY);
        left->src_line = t.line; left->src_col = t.col;
        break;
    case TOK_REF:
        next(p); left = node_new(NODE_BORROW);
        left->borrow.operand = parse_expr_prec(p, PREC_UNARY);
        left->borrow.mut = 0;
        left->src_line = t.line; left->src_col = t.col;
        break;
    case TOK_MUT_REF:
        next(p); left = node_new(NODE_MUT_BORROW);
        left->borrow.operand = parse_expr_prec(p, PREC_UNARY);
        left->borrow.mut = 1;
        left->src_line = t.line; left->src_col = t.col;
        break;
    case TOK_MOVE:
        next(p); left = node_new(NODE_UNARY);
        left->unary.op = 3;
        left->unary.operand = parse_expr_prec(p, PREC_UNARY);
        left->src_line = t.line; left->src_col = t.col;
        break;
    case TOK_INT:
        next(p); left = node_new(NODE_INT);
        left->int_val = t.int_val;
        left->src_line = t.line; left->src_col = t.col;
        break;
    case TOK_FLOAT:
        next(p); left = node_new(NODE_FLOAT);
        left->float_val = t.float_val;
        left->src_line = t.line; left->src_col = t.col;
        break;
    case TOK_STR:
        next(p); left = node_new(NODE_STR);
        left->str_val = t.text; t.text = NULL;
        left->src_line = t.line; left->src_col = t.col;
        break;
    case TOK_CHAR:
        next(p); left = node_new(NODE_INT);
        left->int_val = t.int_val;
        left->src_line = t.line; left->src_col = t.col;
        break;
    case TOK_TRUE: next(p); left = node_new(NODE_BOOL); left->bool_val = 1; left->src_line = t.line; left->src_col = t.col; break;
    case TOK_FALSE: next(p); left = node_new(NODE_BOOL); left->bool_val = 0; left->src_line = t.line; left->src_col = t.col; break;
    case TOK_IDENT: {
        next(p);
        /* bare enum variant without parens: None, Some, etc. */
        int ei, vi;
        int is_variant = (t.text[0] >= 'A' && t.text[0] <= 'Z' && find_enum_variant(p, t.text, &ei, &vi));
        /* Only create enum literal now if NOT followed by ( — that's handled in postfix */
        if (is_variant && peek(p).type != TOK_LPAREN) {
            Node *el = node_new(NODE_ENUM_LITERAL);
            el->enum_literal.enum_name = strdup(p->enums[ei].name);
            free(t.text); t.text = NULL;
            el->enum_literal.variant = strdup(p->enums[ei].variants[vi]);
            el->enum_literal.payload = NULL;
            el->src_line = t.line; el->src_col = t.col;
            left = el;
        } else {
            left = node_new(NODE_IDENT);
            left->ident = t.text; t.text = NULL;
            left->src_line = t.line; left->src_col = t.col;
        }
        break;
    }
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
    case TOK_LPAREN: {
        next(p);
        if (peek(p).type == TOK_RPAREN) {
            next(p);
            left = node_new(NODE_INT);
            left->int_val = 0;
            left->src_line = t.line; left->src_col = t.col;
        } else {
            left = parse_expr(p);
            consume(p, TOK_RPAREN, "expected ')'");
        }
        break;
    }
    case TOK_FN: {
        next(p);
        char lambda_name[64];
        snprintf(lambda_name, sizeof(lambda_name), ".__lambda_%d", p->lambda_count++);
        Node *fn = node_new(NODE_FN_DECL);
        fn->fn.name = strdup(lambda_name);
        fn->src_line = t.line; fn->src_col = t.col;
        fn->fn.params = parse_params(p);
        fn->fn.ret_type = NULL;
        fn->fn.effect = 0;
        if (match(p, TOK_ARROW)) fn->fn.ret_type = parse_type(p);
        if (peek(p).type == TOK_COLON) next(p);
        if (peek(p).type == TOK_NEWLINE) {
            next(p);
            fn->fn.body = parse_block(p);
        } else {
            fn->fn.body = node_new(NODE_BLOCK);
            Node *stmt = parse_stmt(p, 0);
            if (stmt && stmt->type == NODE_EXPR_STMT) {
                Node *ret = node_new(NODE_RETURN);
                ret->ret.val = stmt->expr_stmt.expr;
                stmt->expr_stmt.expr = NULL;
                node_free(stmt);
                fn->fn.body->block.stmts = ret;
            } else {
                fn->fn.body->block.stmts = stmt;
            }
        }
        *p->lambdas_tail = fn;
        p->lambdas_tail = &fn->next;
        left = node_new(NODE_IDENT);
        left->ident = strdup(lambda_name);
        left->src_line = t.line; left->src_col = t.col;
        break;
    }
    default:
        diag_add(p->diag, 2004, SEV_ERROR, t.line, t.col, t.len, "expected expression");
        p->error_count++;
        next(p); left = node_new(NODE_INT); left->int_val = 0;
        break;
    }

    /* postfix: calls, indexing, power, field access */
    while (1) {
        Token n = peek(p);
        if (n.type == TOK_LPAREN) {
            next(p);
            /* check if this is a struct literal (PascalCase names only) */
            if (left->type == NODE_IDENT && left->ident[0] >= 'A' && left->ident[0] <= 'Z' && is_struct(p, left->ident)) {
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
            } else if (left->type == NODE_IDENT && left->ident[0] >= 'A' && left->ident[0] <= 'Z' && is_enum(p, left->ident)) {
                /* enum literal: Option(Some(val)) — parse as constructor call */
                Node *el = node_new(NODE_ENUM_LITERAL);
                el->enum_literal.enum_name = left->ident; left->ident = NULL;
                el->enum_literal.variant = NULL;
                el->enum_literal.payload = NULL;
                if (peek(p).type == TOK_IDENT) {
                    Token vname = next(p);
                    el->enum_literal.variant = vname.text; vname.text = NULL;
                }
                if (peek(p).type == TOK_LPAREN) {
                    next(p);
                    el->enum_literal.payload = parse_expr(p);
                    while (match(p, TOK_COMMA)) {
                        Node *next_arg = parse_expr(p);
                        Node *last = el->enum_literal.payload;
                        while (last->next) last = last->next;
                        last->next = next_arg;
                    }
                    consume(p, TOK_RPAREN, "expected ')'");
                }
                consume(p, TOK_RPAREN, "expected ')' after enum");
                left = el;
            } else if (left->type == NODE_IDENT && left->ident[0] >= 'A' && left->ident[0] <= 'Z') {
                /* enum variant shorthand: Some(42) */
                int ei, vi;
                if (find_enum_variant(p, left->ident, &ei, &vi)) {
                    Node *el = node_new(NODE_ENUM_LITERAL);
                    el->enum_literal.enum_name = strdup(p->enums[ei].name);
                    free(left->ident); left->ident = NULL;
                    el->enum_literal.variant = strdup(p->enums[ei].variants[vi]);
                    el->enum_literal.payload = NULL;
                    if (peek(p).type != TOK_RPAREN) {
                        el->enum_literal.payload = parse_expr(p);
                        Node **tail = &el->enum_literal.payload;
                        while (match(p, TOK_COMMA)) {
                            *tail = parse_expr(p);
                            tail = &(*tail)->next;
                        }
                    }
                    consume(p, TOK_RPAREN, "expected ')'");
                    left = el;
                } else {
                    Node *call = node_new(NODE_CALL);
                    call->call.callee = left;
                    call->call.args = NULL;
                    call->src_line = n.line; call->src_col = n.col;
                    Node **tail = &call->call.args;
                    if (peek(p).type != TOK_RPAREN) {
                        *tail = parse_expr(p); tail = &(*tail)->next;
                        while (match(p, TOK_COMMA)) { *tail = parse_expr(p); tail = &(*tail)->next; }
                    }
                    consume(p, TOK_RPAREN, "expected ')' after arguments");
                    left = call;
                }
            } else {
                Node *call = node_new(NODE_CALL);
                call->call.callee = left;
                call->call.args = NULL;
                call->src_line = n.line; call->src_col = n.col;
                Node **tail = &call->call.args;
                if (peek(p).type != TOK_RPAREN) {
                    *tail = parse_expr(p); tail = &(*tail)->next;
                    while (match(p, TOK_COMMA)) { *tail = parse_expr(p); tail = &(*tail)->next; }
                }
                consume(p, TOK_RPAREN, "expected ')' after arguments");
                left = call;
            }
        } else if (n.type == TOK_STARSTAR) {
            next(p);
            Node *right = parse_expr_prec(p, PREC_POW);
            Node *bin = node_new(NODE_BINARY);
            bin->binary.left = left; bin->binary.right = right; bin->binary.op = 18;
            left = bin;
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
                Node **tail = &right->call.args;
                while (*tail) tail = &(*tail)->next;
                *tail = left;
                call = right;
            } else {
                call->call.callee = right;
                call->call.args = left;
            }
            left = call;
            continue;
        }
        if (n.type == TOK_AS) {
            next(p);
            Node *target = parse_type(p);
            Node *cast = node_new(NODE_UNARY);
            cast->unary.op = 4;
            cast->unary.operand = left;
            cast->src_line = n.line; cast->src_col = n.col;
            (void)target;
            left = cast;
            continue;
        }
        int op = binop_type(n.type);
        if (op < 0) break;
        next(p);
        Node *right = parse_expr_prec(p, (Precedence)(prec + 1));
        /* string concatenation: lower str + str to __string_concat call */
        if (op == 0 && (left->type == NODE_STR || right->type == NODE_STR)) {
            Node *call = node_new(NODE_CALL);
            call->call.callee = node_new(NODE_IDENT);
            call->call.callee->ident = strdup("__string_concat");
            call->call.args = NULL;
            Node **tail = &call->call.args;
            *tail = left; tail = &(*tail)->next;
            *tail = right;
            left = call;
        } else {
            Node *bin = node_new(NODE_BINARY);
            bin->binary.left = left; bin->binary.right = right; bin->binary.op = op;
            left = bin;
        }
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
        if (t.type == TOK_PUB) { next(p); continue; }
        Node *item = parse_item(p);
        if (item) { *tail = item; tail = &item->next; }
    }
    /* append lambdas to program */
    if (p->lambdas) *tail = p->lambdas;
    return prog;
}
