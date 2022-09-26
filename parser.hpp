#pragma once

int yylex();
int yyerror(const char *s);
int yywrap();

#define NODE_CONCAT ";"
#define NODE_ARG "$"
#define NODE_ASYNC "&"
#define NODE_PIPE "|"
#define NODE_AND "&&"
#define NODE_OR "||"
#define NODE_PRE_CMD "^"
#define NODE_ASSIGN "="
#define NODE_REDIT ">"
#define NODE_REDIR_FD "*>"

struct ast_node
{
    // Stores the type if left/right aren't empty.
    // Stores the leaf value otherwise.
    string str;
    ast_node *left;
    ast_node *right;

    ast_node(const string &type, ast_node *left, ast_node *right)
        : str{type}, left{left}, right{right} { }
};

ast_node *leaf(const string &value)
{
    return new ast_node{value, nullptr, nullptr};
}

ast_node *node(const string &type, ast_node *left=nullptr, ast_node *right=nullptr)
{
    return new ast_node{type, left, right};
}

bool is_leaf(ast_node *root)
{
    return !root->left && !root->right;
}

void dump_node(ast_node *root, int indent=0)
{
    if (!root)
        return;
    
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }

    if (is_leaf(root)) {
        printf("\"%s\"\n", root->str.c_str());
    }
    else {
        printf("%s\n", root->str.c_str());
        dump_node(root->left, indent + 1);
        dump_node(root->right, indent + 1);
    }
}

int execute(ast_node *root);