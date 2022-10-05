//#include "parser.hpp"
#include "parser.tab.cpp"

#include <stdio.h>
#include <assert.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
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

bool is_special_param(char c)
{
    return c == '@' || c == '*' || c == '#' || c == '?' || c =='-' || c == '$' || c == '!' || c =='0';
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

void yyerror(const char *s)
{
    fprintf(stderr, "%s\n", s);
}

int yywrap()
{
    return 1;
}

class Reader
{
    string data;
    size_t i;

public:

    Reader(const string &data)
        : data{data}, i{0} { }

    bool eof() { return i >= data.size(); }

    char peek()
    {
        assert(!eof());
        return data[i];
    }

    char pop()
    {
        assert(!eof());
        return data[i++];
    }

    bool at(char prefix)
    {
        return !eof() && data[i] == prefix;
    }

    bool at(const char *prefix)
    {
        for (size_t k = 0; prefix[k]; k++)
            if (i + k >= data.size() || data[i + k] != prefix[k])
                return false;
        
        return true;
    }

    void eat(char prefix)
    {
        assert(peek() == prefix);
        pop();
    }

    void eat(const char *prefix)
    {
        assert(at(prefix));
        i += strlen(prefix);
    }

    string read_slash_quote(bool keep_quotes)
    {
        string result;
        
        eat('\\');

        if (eof()) {
            // Interpreted as a regular slash
            result.push_back('\\');
        }
        else if (at('\n')) {
            // escaped newlines are removed entirely
            result.clear();
        }
        else {
            if (keep_quotes)
                result.push_back('\\');
            result.push_back(pop());
        }

        return result;
    }

    string read_single_quote(bool keep_quotes)
    {
        string result;

        eat('\'');
        if (keep_quotes)
            result.push_back('\'');

        while (!eof() && !at('\''))
            result.push_back(pop());
        
        if (eof())
            panic("EOF in '");
        
        eat('\'');
        if (keep_quotes)
            result.push_back('\'');

        return result;
    }

    string read_double_quote(bool keep_quotes)
    {
        string result;

        eat('"');
        if (keep_quotes)
            result.push_back('"');
        
        while (!eof() && !at('"')) {
            if (at("\\$") || at("\\`") || at("\\\"") || at("\\\\")) {
                // Only allowed escapes inside double quotes
                pop();
                if (keep_quotes)
                    result.push_back('\\');
                result.push_back(pop());
            }
            else if (at('`'))
                result.append(read_subshell_backquote(true));
            else if (at("$"))
                result.append(read_dollar(true));
            else {
                result.push_back(pop());
            }
        }
        
        if (eof())
            panic("EOF in \"");
        
        eat('"');
        if (keep_quotes)
            result.push_back('"');

        return result;
    }

    string read_param_expand(bool keep_quotes)
    {
        string result;

        eat('$');
        if (keep_quotes)
            result.push_back('$');
        
        if (eof())
            // Interpreted as a regular $
            return "$";
        
        if (isdigit(peek()) || is_special_param(peek())) {
            result.push_back(pop());
            return result;
        }
        else if (isalpha(peek()) || peek() == '_') {
            while (!eof() && (isalnum(peek()) || peek() == '_'))
                result.push_back(pop());
        }
        else {
            result = "$";
        }

        return result;
    }

    string read_param_expand_in_braces(bool keep_quotes)
    {
        return read_recursive("${", "}", nullptr, nullptr, keep_quotes);
    }
    string read_subshell(bool keep_quotes)
    {
        return read_recursive("$(", ")", "(", ")", keep_quotes);
    }
    string read_subshell_backquote(bool keep_quotes)
    {
        string result;

        eat('`');
        if (keep_quotes)
            result.push_back('`');
        
        while (!eof() && !at('`')) {
            if (at('\\'))
                result.append(read_slash_quote(true));
            else
                result.push_back(pop());
        }

        if (eof())
            panic("EOF in `");

        eat('`');
        if (keep_quotes)
            result.push_back('`');
        
        return result;
    }

    string read_arithmetic_expand(bool keep_quotes)
    {
        return read_recursive("$((", "))", "(", ")", keep_quotes);
    }

    string read_dollar(bool keep_quotes)
    {
            if (at("$(("))
                return read_arithmetic_expand(true);
            else if (at("$("))
                return read_subshell(true);
            else if (at("${"))
                return read_param_expand_in_braces(true);
            else if (at('$'))
                return read_param_expand(true);
            else
                assert(0);
    }

    string read_recursive(const char *start, const char *end, const char *brace_left, const char *brace_right, bool keep_quotes)
    {
        string result;

        int brace_level = 0;

        eat(start);
        if (keep_quotes)
            result.append(start);
        
        while (!eof()) {
            if (brace_level == 0 && at(end)) {
                break;
            }

            if (brace_left && at(brace_left)) {
                result.append(brace_left);
                eat(brace_left);
                brace_level++;
            }
            else if (brace_right && at(brace_right)) {
                result.append(brace_right);
                eat(brace_right);
                brace_level--;
            }
            else if (at('\''))
                result.append(read_single_quote(true));
            else if (at('\"'))
                result.append(read_double_quote(true));
            else if (at('\\'))
                result.append(read_slash_quote(true));
            else if (at('`'))
                result.append(read_subshell_backquote(true));
            else if (at("$"))
                result.append(read_dollar(true));
            else {
                result.push_back(pop());
            }
        }

        if (eof())
            panic("EOF in nested expression");

        eat(end);
        if (keep_quotes)
            result.append(end);
        
        return result;
    }

    string read_operator()
    {
        string result;

        while (!eof() && is_operator_prefix(result + peek()))
            result.push_back(pop());
        
        return result;
    }

    void read_comment()
    {
        eat('#');
        while (!eof() && !at('\n'))
            pop();
    }

    string read_token()
    {
        while (at('#'))
            read_comment();
        
        if (eof())
            // TODO: This is a strange situation. Maybe we should make it imporrislbe
            return "";

        if (is_operator_prefix(string{peek()})) {
            return read_operator();
        }

        string result;

        while (!eof()) {
            if (at('\\'))
                result.append(read_slash_quote(true));
            else if (at('\''))
                result.append(read_single_quote(true));
            else if (at('\"'))
                result.append(read_double_quote(true));
            else if (at('`'))
                result.append(read_subshell_backquote(true));
            else if (at('$'))
                result.append(read_dollar(true));
            else if (is_operator_prefix(string{peek()}))
                break;
            else if (at(' ')) {
                pop();
                if (result.size())
                    break;
            }
            else {
                result.push_back(pop());
            }
        }

        while (at('#'))
            read_comment();

        return result;
    }
};

struct ast_redirect
{
    string lhs;
    string op;
    string rhs;
};

struct ast_simple_command
{
    vector<string> assignments;
    vector<string> args;
    vector<ast_redirect> redirections;
};

struct ast_command
{
    ast_simple_command cmd;
};

struct ast_pipeline
{
    bool invert_exit_code;
    vector<ast_command> commands;
};

struct ast_and_or
{
    bool async;
    vector<ast_pipeline> pipelines;
    vector<bool> is_and;
};

struct ast_program
{
    vector<ast_and_or> and_ors;
};

class TokenReader
{
public:
    bool eof();
    char peek();
    char pop();
    bool at(char prefix);
};

// Read zero or more new lines
void parse_skip_linebreak(TokenReader &r)
{
    while (r.at(NEWLINE))
        r.eat(NEWLINE);
}

ast_simple_command parse_simple_command(TokenReader &r)
{
    ast_simple_command simple_command;

    while (true) {
        if (parse_assignment_possible(r)) {
            simple_command.assignments.push_back(r.pop());
        }
        else if (parse_redirect_possible(r)) {
            simple_command.redirections.push_back(parse_redirect(r));
        }
        else {
            break;
        }
    }

    while (true) {
        if (r.at(WORD)) {
            simple_command.args.push_back(r.pop());
        }
        else if (parse_redirect_possible(r)) {
            simple_command.redirections.push_back(parse_redirect(r));
        }
        else {
            break;
        }
    }

    return simple_command;
}

ast_redirect parse_redirect(TokenReader &r)
{
    
}

ast_command parse_command(TokenReader &r)
{
    ast_command command;
    command.cmd = parse_simple_command(r);
    return command;
}

ast_pipeline parse_pipeline(TokenReader &r)
{
    ast_pipeline pipeline;

    if (r.at(Bang)) {
        r.eat(Bang);
        pipeline.invert_exit_code = true;
    }

    while (true) {
        pipeline.commands.push_back(parse_command(r));

        if (r.at('|')) {
            r.eat('|');
            parse_skip_linebreak(r);
        }
        else {
            break;
        }
    }

    return pipeline;
};

ast_and_or parse_and_or(TokenReader &r)
{
    ast_and_or and_or;

    while (true) {
        and_or.pipelines.push_back(parse_pipeline(r));

        if (r.at(AND_IF) || r.at(OR_IF)) {
            and_or.is_and.push_back(r.at(AND_IF));
            parse_skip_linebreak(r);
        }
        else {
            break;
        }
    }

    if (r.at(';') || r.at('&')) {
        and_or.async = r.at('&');
    }

    return and_or;
}

ast_program parse_program(TokenReader &r)
{
    ast_program program;

    parse_skip_linebreak(r);

    while (!r.eof()) {
        program.and_ors.push_back(parse_and_or(r));
        parse_skip_linebreak(r);
    }

    return program;
}







// 2.6.1 Tilde Expansion

string tilde_expand(const string &word)
{
    if (word.size() == 0 || word[0] != '~')
        return word;
    
    string tilde_prefix;
    string expand_value;

    for (size_t i = 1; i < word.size(); i++) {
        if (word[i] == '/')
            break;
        else if (word[i] == '\\')
            // None of the characters in the tilde-prefix can be quoted.
            return word;
        else
            tilde_prefix.push_back(word[i]);
    }

    if (tilde_prefix.size() == 0) {
        expand_value = getenv("HOME");
    }
    else {
        passwd *pass = getpwnam(tilde_prefix.c_str());

        if (!pass)
            return word;
        
        expand_value = pass->pw_dir;
    }

    // TODO: quote the expanded value to prevent field splitting and pathname expansion
    return expand_value + word.substr(tilde_prefix.size() + 1);
}

// 2.6.7 Quote Removal

string quote_remove(const string &word)
{
    string result;
    size_t i = 0;

    while (i < word.size()) {
        if (word[i] == '\\') {
            // Skip the slash
            i++;
            // The next char goes in as-is
            if (i < word.size()) {
                result.push_back(word[i]);
                i++;
            }
        }
        else if (word[i] == '\'') {
            // Skip the opening quote
            i++;
            while (i < word.size() && word[i] != '\'') {
                // Everything inside gets as-is
                result.push_back(word[i]);
                i++;
            }
            // Skip the closing quote
            i++;
        }
        else if (word[i] == '"') {
            // Skip the opening quote
            i++;
            while (i < word.size() && word[i] != '\"') {
                if (word[i] == '\\'
                    && i + 1 < word.size()
                    && (word[i + 1] == '$' || word[i + 1] == '`' || word[i + 1] == '"' || word[i + 1] == '\\')) {
                    // Skip special slashes
                    i++;
                }
                result.push_back(word[i]);
                i++;
            }
            // Skip the closing quote
            i++;
        }
        else {
            result.push_back(word[i]);
            i++;
        }
    }

    return result;
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

        string s = root->left->str;
        s = tilde_expand(s);
        s = quote_remove(s);

        pipeline.back().args.push_back(s);
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

            if (i + 1 < pipeline.size()) {
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