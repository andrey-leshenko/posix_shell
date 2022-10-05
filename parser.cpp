#include "parser.hpp"

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
#include <algorithm>

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

vector<string> special_chars
{
    "&",
    "|",
    ";",
    "<",
    ">",
};

vector<string> reserved_words
{
    "if",
    "then",
    "else",
    "elif",
    "fi",
    "do",
    "done",
    "case",
    "esac",
    "while",
    "until",
    "for",
    "{",
    "}",
    "!",
    "in",
};

enum class TokenType
{
    WORD,
    ASSIGNMENT_WORD,
    NAME,
    NEWLINE,
    IO_NUMBER,

    OPERATOR,
    SPECIAL_CHAR,
    RESERVED_WORD,
};

bool is_operator_prefix(const string& str)
{
    for (auto& op : operators)
        if (op.substr(0, str.size()) == str)
            return true;

    return false;
}

bool is_digits(const string &str)
{
    return str.find_first_not_of("0123456789") == std::string::npos;
}

bool is_special_param(char c)
{
    return c == '@' || c == '*' || c == '#' || c == '?' || c =='-' || c == '$' || c == '!' || c =='0';
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
        bool out_is_io_number;
        return read_token(&out_is_io_number);
    }

    string read_token(bool *out_is_io_number)
    {
        *out_is_io_number = false;

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
            else if (is_operator_prefix(string{peek()})) {
                if (result.size()) {
                    if (is_digits(result) && (at('<') || at('>')))
                        *out_is_io_number = true;
                    break;
                }
                else {
                    return read_operator();
                }
            }
            else if (at(' ')) {
                pop();
                if (result.size())
                    break;
            }
            else if (at('\n')) {
                if (result.size()) {
                    break;
                }
                else {
                    return string{pop()};
                }
            }
            else if (result.size() == 0 && at('#')) {
                read_comment();
            }
            else {
                result.push_back(pop());
            }
        }

        return result;
    }
};

class TokenReader
{
    Reader r;
    string token;
    bool is_io_number;

    TokenType token_type()
    {
        if (is_io_number)
            return TokenType::IO_NUMBER;

        if (token == "\n")
            return TokenType::NEWLINE;

        if (std::find(operators.begin(), operators.end(), token) != operators.end()) {
            return TokenType::OPERATOR;
        }

        if (std::find(special_chars.begin(), special_chars.end(), token) != special_chars.end()) {
            return TokenType::SPECIAL_CHAR;
        }

        // TODO: Apply this only for command name
        if (std::find(reserved_words.begin(), reserved_words.end(), token) != reserved_words.end()) {
            return TokenType::RESERVED_WORD;
        }

        return TokenType::WORD;
    }

public:
    TokenReader(const Reader &r)
        : r{r}
    {
        // Initialize the token
        pop();
    }

    bool eof()
    {
        return token.size() == 0;
    }

    string peek()
    {
        return token;
    }

    string pop()
    {
        string result = token;
        token = this->r.read_token(&is_io_number);
        return result;
    }
    string pop(TokenType expected_type)
    {
        if (expected_type != token_type())
            panic("unexpected token type");
        return pop();
    }

    bool at(TokenType type)
    {
        return !eof() && token_type() == type;
    }

    bool at(TokenType type, const char *value)
    {
        return at(type) && token == value;
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
    bool invert_exit_code = 0;
    vector<ast_command> commands;
};

struct ast_and_or
{
    bool async = 0;
    vector<ast_pipeline> pipelines;
    vector<bool> is_and;
};

struct ast_program
{
    vector<ast_and_or> and_ors;
};

// Read zero or more new lines
void parse_skip_linebreak(TokenReader &r)
{
    while (r.at(TokenType::NEWLINE))
        r.pop();
}

bool at_redirect_operator(TokenReader &r)
{
    return r.at(TokenType::SPECIAL_CHAR, "<")
        || r.at(TokenType::SPECIAL_CHAR, ">")
        || r.at(TokenType::OPERATOR, "<&")
        || r.at(TokenType::OPERATOR, ">&")
        || r.at(TokenType::OPERATOR, ">>")
        || r.at(TokenType::OPERATOR, "<>")
        || r.at(TokenType::OPERATOR, ">|");
}

bool at_redirect(TokenReader &r)
{
    return r.at(TokenType::IO_NUMBER) || at_redirect_operator(r);
}

ast_redirect parse_redirect(TokenReader &r)
{
    ast_redirect redirect;

    if (r.at(TokenType::IO_NUMBER)) {
        redirect.lhs = r.pop(TokenType::IO_NUMBER);
    }

    if (!at_redirect_operator(r)) {
        panic("unexpected token");
    }

    redirect.op = r.pop();        

    redirect.rhs = r.pop(TokenType::WORD);

    return redirect;
}

ast_simple_command parse_simple_command(TokenReader &r)
{
    ast_simple_command simple_command;

    // r.set_rule(GrammarRule::PRE_COMMAND_ASSIGNMENT);

    while (true) {
        // if (r.at(TokenType::ASSIGNMENT_WORD)) {
        //     simple_command.assignments.push_back(r.pop());
        // }
        // else if (at_redirect(r)) {
        if (at_redirect(r)) {
            simple_command.redirections.push_back(parse_redirect(r));
        }
        else {
            break;
        }
    }

    // r.clear_rule();

    while (true) {
        if (r.at(TokenType::WORD)) {
            simple_command.args.push_back(r.pop());
        }
        else if (at_redirect(r)) {
            simple_command.redirections.push_back(parse_redirect(r));
        }
        else {
            break;
        }
    }

    return simple_command;
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

    if (r.at(TokenType::RESERVED_WORD, "!")) {
        r.pop();
        pipeline.invert_exit_code = true;
    }

    while (true) {
        pipeline.commands.push_back(parse_command(r));

        if (r.at(TokenType::SPECIAL_CHAR, "|")) {
            r.pop();
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

        if (r.at(TokenType::OPERATOR, "&&") || r.at(TokenType::OPERATOR, "||")) {
            and_or.is_and.push_back(r.at(TokenType::OPERATOR, "&&"));
            parse_skip_linebreak(r);
        }
        else {
            break;
        }
    }

    if (r.at(TokenType::SPECIAL_CHAR, ";") || r.at(TokenType::SPECIAL_CHAR, "&")) {
        and_or.async = r.at(TokenType::SPECIAL_CHAR, "&");
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

void execute(const string &program)
{
    TokenReader r = TokenReader(Reader(program));
    ast_program p = parse_program(r);
}

#if 0

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

#endif