#include <stdio.h>
#include <assert.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/wait.h>
#include <fcntl.h>

#include <iostream>
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

// Utils

void panic [[ noreturn ]] (const char* msg)
{
    printf("panic: %s\n", msg);
    exit(-1);
}

int str_to_int(const char *str)
{
    char* p;
    long n = strtol(str, &p, 10);
    if (*p) {
        panic("convertion to int failed");
    }
    return n;
}

string read_file(const char *path)
{
    std::ifstream f(path);
    std::stringstream buffer;
    buffer << f.rdbuf();
    return buffer.str();
}

// Prasing

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
            if (at("\\$") || at("\\`") || at("\\\\")) {
                // The only cases when backslash is special
                pop();
                if (keep_quotes)
                    result.push_back('\\');
                result.push_back(pop());
            }
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

    string read_regular_part()
    {
        string result;

        while (!eof() && !at('\\') && !at('\'') && !at('"') && !at('`') && !at('$'))
            result.push_back(pop());
        
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
            r.pop();
            parse_skip_linebreak(r);
        }
        else {
            break;
        }
    }

    if (r.at(TokenType::SPECIAL_CHAR, ";") || r.at(TokenType::SPECIAL_CHAR, "&")) {
        and_or.async = r.at(TokenType::SPECIAL_CHAR, "&");
        r.pop();
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

// Expansion

string expand_tilde_prefix(const string &tilde_prefix)
{
    string expand_value;

    if (tilde_prefix.size() == 0) {
        return getenv("HOME");
    }
    else {
        passwd *pass = getpwnam(tilde_prefix.c_str());

        if (!pass)
            return tilde_prefix;
        
        return pass->pw_dir;
    }
}

void execute(const string &program);

void subshell(const string &program)
{
    // TODO: Really open a subshell here
    execute(program);
}

string expand_command(const string &command)
{
    // TODO: Find something more elegant than redirecting to a file

    // The X-s will be replaced by mkstemp
    char filename[] = "stdoutXXXXXX";
    int new_stdout = mkstemp(filename);
    int old_stdout = dup(1);

    dup2(new_stdout, 1);
    close(new_stdout);
    subshell(command);
    dup2(old_stdout, 1);
    close(old_stdout);

    string result = read_file(filename);
    unlink(filename);

    while (result.size() && result.back() == '\n')
        result.pop_back();

    return result;
}

vector<string> expand_word(const string &word)
{
    Reader r(word);
    string result;

    // TODO: deal with variable assignments that support multiple tilde-prefixes
    if (r.at('~')) {
        string first_part = r.read_regular_part();
        size_t slash = first_part.find('/');
        // This works nicely when slash is string::npos
        string tilde_prefix = first_part.substr(1, slash - 1);

        result.append(expand_tilde_prefix(tilde_prefix));

        if (slash != string::npos)
            result.append(first_part.substr(slash));
    }

    // TODO: Impelment the different expansions

    while (!r.eof()) {
        if (r.at('\\'))
            result.append(r.read_slash_quote(false));
        else if (r.at('\''))
            result.append(r.read_single_quote(false));
        else if (r.at('\"'))
            result.append(r.read_double_quote(false));
        else if (r.at('`'))
            result.append(expand_command(r.read_subshell_backquote(false)));
        else if (r.at("$(("))
            result.append(r.read_arithmetic_expand(false));
        else if (r.at("$("))
            result.append(expand_command(r.read_subshell(false)));
        else if (r.at("${"))
            result.append(r.read_param_expand_in_braces(false));
        else if (r.at('$'))
            result.append(r.read_param_expand(false));
        else
            result.append(r.read_regular_part());
    }

    return {result};
}

// Execution

void execute_redirect(const ast_redirect &redirect)
{
    int left_fd;

    if (redirect.lhs.size()) {
        left_fd = str_to_int(redirect.lhs.c_str());
    }
    else if (redirect.op == "<" || redirect.op == "<&" || redirect.op == "<>") {
        left_fd = 0;
    }
    else if (redirect.op == ">" || redirect.op == ">&" || redirect.op == ">>" || redirect.op == ">|") {
        left_fd = 1;
    }
    else {
        assert(0);
    }

    if (redirect.op == "<&" || redirect.op == ">&") {
        if (redirect.rhs == "-") {
            close(left_fd);
        }
        else {
            int right_fd = str_to_int(redirect.rhs.c_str());
            dup2(right_fd, left_fd);
        }
    }
    else {
        int flags = 0;

        if (redirect.op == "<")
            flags = O_RDONLY;
        else if (redirect.op == ">" || redirect.op == ">|")
            flags = O_WRONLY | O_CREAT | O_TRUNC;
        else if (redirect.op == ">>")
            flags = O_WRONLY | O_CREAT | O_APPEND;
        else if (redirect.op == "<>")
            flags = O_RDWR | O_CREAT;
        else
            assert(0);

        int right_fd = open(redirect.rhs.c_str(), flags, 0666);
        if (right_fd < 0)
            panic("redirect file open failed");
        dup2(right_fd, left_fd);
        close(right_fd);
    }
}

int execute_simple_command(const ast_simple_command &simple_command)
{
    if (simple_command.assignments.size())
        panic("not implemented");
    
    vector<string> expanded_args;

    for (const string &word : simple_command.args) {
        vector<string> fields = expand_word(word);
        expanded_args.insert(expanded_args.end(), fields.begin(), fields.end());
    }

    for (const ast_redirect &redirect : simple_command.redirections)
        execute_redirect(redirect);
    
    pid_t pid = fork();

    if (pid < 0) {
        panic("fork failed");
    }

    if (pid > 0) {
        // Parent
        int wstatus;
        waitpid(pid, &wstatus, 0);
        return WEXITSTATUS(wstatus);
    }

    // Child
    vector<const char*> argv;

    for (auto& arg : expanded_args)
        argv.push_back(arg.c_str());

    argv.push_back(nullptr);

    // execve doesn't modify its arguments, so it should be safe
    char ** argv_ptr = const_cast<char **>(&argv[0]);

    execvp(argv[0], argv_ptr);
    panic("execve failed");
}

int execute_command(const ast_command &command)
{
    return execute_simple_command(command.cmd);
}

int execute_pipeline(const ast_pipeline &pipeline)
{
    int rpipe[2] = {-1, -1};
    int wpipe[2] = {-1, -1};

    vector<pid_t> pids;

    const vector<ast_command> &commands = pipeline.commands;
    
    for (size_t i = 0; i < commands.size(); i++) {
        rpipe[0] = wpipe[0];
        rpipe[1] = wpipe[1];

        if (i + 1 < commands.size()) {
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

        // TODO: When executing a simple command, we don't really need to have
        // both the forked process waiting, and the command process running.
        // Maybe we can do some kind of tail-execv optimization?
        exit(execute_command(commands[i]));
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

int execute_and_or(const ast_and_or &and_or)
{
    int exit_status = 0;

    if (and_or.async)
        panic("not implemented");
    
    for (size_t i = 0; i < and_or.pipelines.size(); i++) {
        if (i > 0) {
            if (and_or.is_and[i - 1] && exit_status != 0) {
                // short-circuit AND
                continue;
            }
            else if (!and_or.is_and[i - 1] && exit_status == 0) {
                // short-circuit OR
                continue;
            }
        }

        exit_status = execute_pipeline(and_or.pipelines[i]);
    }

    return exit_status;
}

int execute_program(const ast_program &program)
{
    int exit_status = 0;

    for (const ast_and_or& and_or : program.and_ors) {
        exit_status = execute_and_or(and_or);
    }

    return exit_status;
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
    execute_program(p);
}

void repl()
{
    string line;

    std::cout << "$ " << std::flush;
    while (std::getline(std::cin, line)) {
        // Reader r(line);
        // while (!r.eof())
        //     std::cout << r.read_token() << std::endl;
        execute(line);
        std::cout << "$ " << std::flush;
    }
}

int main()
{
    // run_command("echo", {"hello", "world", "from echo"}, {});
    // run_command("cat", {}, {redirection{0, "/etc/hostname", -1, RedirType::INPUT}});
    // run_command("echo", {"test redirection"}, {redirection{1, "/tmp/redir1", -1, RedirType::OUTPUT}});
    // run_command("echo", {"test append"}, {redirection{1, "/tmp/redir2", -1, RedirType::APPEND}});
    // run_command("echo", {"test append"}, {redirection{1, "/tmp/redir2", -1, RedirType::APPEND}});
    // run_command("echo", {"test append"}, {redirection{1, "/tmp/redir2", -1, RedirType::APPEND}});
    // run_command("echo", {"test dup"}, {redirection{1, nullptr, 2, RedirType::INPUT_DUP}});

    repl();
    return 0;

    std::ifstream f("test2.sh");
    std::stringstream buffer;
    buffer << f.rdbuf();
    string script = buffer.str();

    // vector<string> tokens = tokenize(script.c_str(), script.size());

    // for (auto& s : tokens) {
    //     if (s == "\n")
    //         printf("'\\n'\n");
    //     else
    //         printf("%s\n", s.c_str());
    // }

    //parse(script.c_str(), script.size());

    return 0;
}