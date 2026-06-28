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
    case NODE_NULLSAFE:
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
    case NODE_MATCH:
        node_free(n->match.expr);
        node_free(n->match.arms);
        break;
    case NODE_MATCH_ARM:
        free(n->match_arm.variant);
        node_free(n->match_arm.payload);
        node_free(n->match_arm.guard);
        node_free(n->match_arm.body);
        break;
    case NODE_ENUM_DECL:
        free(n->enum_decl.name);
        node_free(n->enum_decl.type_params);
        node_free(n->enum_decl.variants);
        break;
    case NODE_ENUM_LITERAL:
        free(n->enum_literal.enum_name);
        free(n->enum_literal.variant);
        node_free(n->enum_literal.payload);
        break;
    case NODE_BORROW:
    case NODE_MUT_BORROW:
        node_free(n->borrow.operand);
        break;
    case NODE_DEREF:
        node_free(n->borrow.operand);
        break;
    case NODE_TRAIT_DECL:
        free(n->trait_decl.name);
        node_free(n->trait_decl.type_params);
        node_free(n->trait_decl.methods);
        break;
    case NODE_IMPL_BLOCK:
        free(n->impl_block.name);
        node_free(n->impl_block.for_type);
        node_free(n->impl_block.methods);
        break;
    case NODE_FOR:
        free(n->for_stmt.var);
        node_free(n->for_stmt.iter);
        node_free(n->for_stmt.iter_end);
        node_free(n->for_stmt.body);
        break;
    case NODE_BREAK:
    case NODE_CONTINUE:
    case NODE_PROGRAM:
    case NODE_INT:
    case NODE_FLOAT:
    case NODE_BOOL:
        break;
    }
    if (n->next) node_free(n->next);
    free(n);
}
