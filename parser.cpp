//#include "parser.hpp"
#include "parser.tab.cpp"

#include <stdio.h>
#include <assert.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#include <vector>
#include <string>
#include <map>
#include <fstream>
#include <sstream>

using std::vector;
using std::string;
using std::map;

#define _C(X) X

vector<string> operators
{
    "&&",
    "||",
    ";;",
    "<<",
    ">>",
    "<&",
    ">&",
    "<>",
    "<<-",
    ">|",
};

map<string, int> tokens_map
{
    // Stand-alone chars
    {"&",   '&'         },
    {"|",   '|'         },
    {";",   ';'         },
    {"<",   '<'         },
    {">",   '>'         },
    // Operators
    {"&&",  AND_IF      },
    {"||",  OR_IF       },
    {";;",  DSEMI       },
    {"<<",  DLESS       },
    {">>",  DGREAT      },
    {"<&",  LESSAND     },
    {">&",  GREATAND    },
    {"<>",  LESSGREAT   },
    {"<<-", DLESSDASH   },
    {">|",  CLOBBER     },
    // Reserved words
    {"if",      If      },
    {"then",    Then    },
    {"else",    Else    },
    {"elif",    Elif    },
    {"fi",      Fi      },
    {"do",      Do      },
    {"done",    Done    },
    {"case",    Case    },
    {"esac",    Esac    },
    {"while",   While   },
    {"until",   Until   },
    {"for",     For     },
    {"{",       Lbrace  },
    {"}",       Rbrace  },
    {"!",       Bang    },
    {"in",      In      },
};

bool is_operator_prefix(const string& str)
{
    for (auto& op : operators)
        if (op.substr(0, str.size()) == str)
            return true;

    return false;
}

enum class TokenizerMode
{
    DEFAULT,
    OPERATOR,
    SLASH_QUOTE,
    SINGLE_QUOTE,
    DOUBLE_QUOTE,
    DOUBLE_QUOTE_SLASH_QUOTE,
};

vector<string> tokenize(const char* str, size_t len)
{
    vector<string> tokens;
    string tok;

    TokenizerMode mode = TokenizerMode::DEFAULT;

    #define DELIMIT_TOK if (tok.size()) {tokens.push_back(tok);}; tok.clear(); mode = TokenizerMode::DEFAULT;

    for (size_t i = 0; ; i++) {
        if (i >= len) {
            DELIMIT_TOK;
            break;
        }

        char c = str[i];

        if (c == '\n') {
            if (mode == TokenizerMode::SLASH_QUOTE || mode == TokenizerMode::DOUBLE_QUOTE_SLASH_QUOTE) {
                // Line continuations are removed
                assert(tok.size() && tok.back() == '\\');
                tok.pop_back();
                continue;
            }
            else {
                DELIMIT_TOK;
                tok.push_back('\n');
                DELIMIT_TOK;
                continue;
            }
        }

        if (mode == TokenizerMode::OPERATOR) {
            if (is_operator_prefix(tok + c)) {
                tok.push_back(c);
                continue;
            }
            else {
                DELIMIT_TOK;
            }
        }

        if (mode == TokenizerMode::SLASH_QUOTE) {
            if (c == '\n') {
                // Line continuations are removed
                assert(tok.size() && tok.back() == '\\');
                tok.pop_back();
                continue;
            }
            tok.push_back(c);
            mode = TokenizerMode::DEFAULT;
            continue;
        }
        else if (mode == TokenizerMode::SINGLE_QUOTE) {
            tok.push_back(c);
            if (c == '\'')
                mode = TokenizerMode::DEFAULT;
            continue;
        }
        else if (mode == TokenizerMode::DOUBLE_QUOTE) {
            tok.push_back(c);
            if (c == '"')
                mode = TokenizerMode::DEFAULT;
            if (c == '\\')
                mode = TokenizerMode::DOUBLE_QUOTE_SLASH_QUOTE;
            continue;
        }
        else if (mode == TokenizerMode::DOUBLE_QUOTE_SLASH_QUOTE) {
            tok.push_back(c);
            mode = TokenizerMode::DOUBLE_QUOTE;
            continue;
        }

        assert(mode == TokenizerMode::DEFAULT);

        if (c == '\\') {
            tok.push_back(c);
            mode = TokenizerMode::SLASH_QUOTE;
            continue;
        }
        else if (c == '\'') {
            tok.push_back(c);
            mode = TokenizerMode::SINGLE_QUOTE;
            continue;
        }
        else if (c == '"') {
            tok.push_back(c);
            mode = TokenizerMode::DOUBLE_QUOTE;
            continue;
        }

        if (is_operator_prefix(string{c})) {
            DELIMIT_TOK;
            tok.push_back(c);
            mode = TokenizerMode::OPERATOR;
            continue;
        }

        if (c == ' ') {
            DELIMIT_TOK;
            // Discard space
            continue;
        }

        if (tok.size()) {
            tok.push_back(c);
            continue;
        }

        if (c == '#') {
             while (i < len && str[i] != '\n')
                i++;
            if (i < len /* && i == '\n' */)
                i--; // We want to parse it again
            continue;
        }

        tok.push_back(c);
    }

    return tokens;
}

bool is_digits(const string &str)
{
    return str.find_first_not_of("0123456789") == std::string::npos;
}

int token_categorize(const string &token, char delimiter)
{
    if (token == "\n")
        return NEWLINE;

    if (tokens_map.find(token) != tokens_map.end()) {
        return tokens_map[token];
    }

    if (is_digits && (delimiter == '<' || delimiter == '>')) {
        return IO_NUMBER;
    }

    return WORD;
}

static vector<string> g_tokens;
static int g_token_i;

// YACC interface

int yylex()
{
    if (g_token_i >= g_tokens.size())
        return 0;
    
    const string &tok = g_tokens[g_token_i];
    g_token_i++;

    //printf("token: '%s'\n", tok.c_str());

    yylval = nullptr;
    int category = token_categorize(tok, 0);

    if (category == WORD || category == NAME || category == IO_NUMBER || category == ASSIGNMENT_WORD)
        yylval = leaf(tok);
    else
        yylval = nullptr;
    
    return category;
}

int yyerror(const char *s)
{
    fprintf(stderr, "%s\n", s);
}

int yywrap()
{
    return 1;
}

vector<string> g_args;

struct redirection
{
    string type;
    int left;
    string right;
};

struct command_environment
{
    vector<string> args;
    vector<redirection> redirs;
};

vector<command_environment> pipeline;

int execute(ast_node *root)
{
    if (!root)
        return -1;
    if (is_leaf(root))
        assert(0);
    
    if (root->str == ";") {
        execute(root->left);
        return execute(root->right);
    }
    else if (root->str == "&&") {
        int res = execute(root->left);
        if (!res)
            res = execute(root->right);
        return res;
    }
    else if (root->str == "||") {
        int res = execute(root->left);
        if (res)
            res = execute(root->right);
        return res;
    }
    else if (root->str == "!") {
        return !execute(root->left);
    }
    else if (root->str == "CMD") {
        // Probably can be removed
        execute(root->left);
    }
    else if (root->str == "ARG") {
        assert(is_leaf(root->left));
        pipeline.back().args.push_back(root->left->str);
        return execute(root->right);
    }
    else if (root->str == "PIPELINE") {
        pipeline.clear();
        pipeline.push_back({});
        execute(root->left);

        int rpipe[2] = {-1, -1};
        int wpipe[2] = {-1, -1};

        vector<pid_t> pids;

        if (pipeline.size() > 1)
            _C(pipe(rpipe));

        for (size_t i = 0; i < pipeline.size(); i++) {
            rpipe[0] = wpipe[0];
            rpipe[1] = wpipe[1];

            if (i + i < pipeline.size()) {
                _C(pipe(wpipe));
            }

            pid_t pid = fork();

            if (pid > 0) {
                // Parent process
                pids.push_back(pid);

                close(rpipe[0]);
                close(rpipe[1]);

                continue;
            }

            // Child process

            if (rpipe[0] >= 0) {
                dup2(rpipe[0], 0);
                close(rpipe[0]);
                close(rpipe[1]);
            }

            if (wpipe[1] >= 0) {
                dup2(wpipe[1], 1);
                close(wpipe[0]);
                close(wpipe[1]);
            }

            vector<const char*> argv;
            for (auto& a : pipeline[i].args)
                argv.push_back(a.c_str());
            argv.push_back(nullptr);
            // execve doesn't modify its arguments, so this is safe
            char ** argv_ptr = const_cast<char **>(&argv[0]);

            // Redirections
            // for (auto& r : redirections)
            //     redirect(r);

            execvp(argv[0], argv_ptr);
            panic("execve failed");
        }

        if (wpipe[0] >= 0) {
            close(wpipe[0]);
            close(wpipe[1]);
        }

        int exit_status = 0;

        for (auto pid : pids) {
            int wstatus;
            waitpid(pid, &wstatus, 0);
            exit_status = WEXITSTATUS(wstatus);
        }

        return exit_status;
    }
    else if (root->str == "|") {
        execute(root->left);
        // Begin new command
        pipeline.push_back({});
        execute(root->right);
    }
    else if (root->str == "<" || root->str == "<&" || root->str == ">"
        || root->str == ">&" || root->str == ">>" || root->str == "<>"
        || root->str == ">|") {
        redirection redir;
        redir.type = root->str;
        redir.right = root->right->str;
        if (root->left)
            redir.left = atoi(root->left->str.c_str());
        else
            redir.left = -1;
            
        pipeline.back().redirs.push_back(redir);
    }
    else {
        printf("Ignoring node: %s\n", root->str.c_str());
        //assert(0);
    }
}

void parse(const char *str, size_t len)
{
    //yydebug = 1;

    g_tokens = tokenize(str, len);
    g_token_i = 0;

    int res = yyparse();

    if (res)
        printf("Parsing error!");
}