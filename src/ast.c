#include "ast.h"
#include <stdlib.h>
#include <string.h>

Node *node_new(NodeType type) {
    Node *n = calloc(1, sizeof(Node));
    n->type = type;
    return n;
}

void node_free(Node *n) {
    if (!n) return;
    switch (n->type) {
    case NODE_FN_DECL:
        free(n->fn.name);
        node_free(n->fn.params);
        node_free(n->fn.ret_type);
        node_free(n->fn.body);
        break;
    case NODE_EXTERN_DECL:
        free(n->ext.name);
        node_free(n->ext.params);
        node_free(n->ext.ret_type);
        break;
    case NODE_BLOCK:
        node_free(n->block.stmts);
        break;
    case NODE_LET:
        free(n->let.name);
        node_free(n->let.type);
        node_free(n->let.init);
        break;
    case NODE_ASSIGN:
        node_free(n->assign.lhs);
        node_free(n->assign.rhs);
        break;
    case NODE_IF:
        node_free(n->if_stmt.cond);
        node_free(n->if_stmt.then);
        node_free(n->if_stmt.otherwise);
        break;
    case NODE_WHILE:
        node_free(n->while_stmt.cond);
        node_free(n->while_stmt.body);
        break;
    case NODE_RETURN:
        node_free(n->ret.val);
        break;
    case NODE_EXPR_STMT:
        node_free(n->expr_stmt.expr);
        break;
    case NODE_IDENT:
        free(n->ident);
        break;
    case NODE_STR:
        free(n->str_val);
        break;
    case NODE_CALL:
        node_free(n->call.callee);
        node_free(n->call.args);
        break;
    case NODE_BINARY:
        node_free(n->binary.left);
        node_free(n->binary.right);
        break;
    case NODE_UNARY:
        node_free(n->unary.operand);
        break;
    case NODE_INDEX:
        node_free(n->index_expr.obj);
        node_free(n->index_expr.index);
        break;
    case NODE_COMPREHENSION:
        node_free(n->comp.map);
        free(n->comp.var);
        node_free(n->comp.iter);
        node_free(n->comp.iter_end);
        node_free(n->comp.filter);
        break;
    case NODE_STRUCT_DECL:
        free(n->struct_decl.name);
        node_free(n->struct_decl.fields);
        break;
    case NODE_STRUCT_LITERAL:
        free(n->struct_literal.name);
        node_free(n->struct_literal.args);
        break;
    case NODE_BREAK:
    case NODE_CONTINUE:
        break;
    default: break;
    }
    if (n->next) node_free(n->next);
    free(n);
}
