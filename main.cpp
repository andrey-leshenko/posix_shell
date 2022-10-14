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
#include <memory>
#include <variant>

using std::vector;
using std::string;
using std::map;
using std::variant;

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
    "&",
    "|",
    ";",
    "<",
    ">",
    "(",
    ")",
};

vector<string> special_chars
{
    "&",
    "|",
    ";",
    "<",
    ">",
    "(",
    ")",
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
            if (at("\\\"")) {
                // This slash is no longer needed when we remove quotes
                pop();
                if (keep_quotes)
                    result.push_back('\\');
                result.push_back(pop());
            }
            else if (at("\\$") || at("\\`") || at("\\\\")) {
                // This slash still carries meaning
                result.push_back(pop());
                result.push_back(pop());
            }
            else if (at('`'))
                result.append(read_subshell_backquote(true));
            else if (at("$"))
                result.append(read_dollar(true));
            else
                result.push_back(pop());
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
            if (at("\\`")) {
                // This slash is no longer needed when we remove quotes
                pop();
                if (keep_quotes)
                    result.push_back('\\');
                result.push_back(pop());
            }
            else if (at("\\$") || at("\\\\")) {
                // This slash still carries meaning
                result.push_back(pop());
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

    TokenType token_type(bool parse_reserved)
    {
        if (is_io_number)
            return TokenType::IO_NUMBER;

        if (token == "\n")
            return TokenType::NEWLINE;

        if (std::find(operators.begin(), operators.end(), token) != operators.end()) {
            return TokenType::OPERATOR;
        }

        if (parse_reserved) {
            if (std::find(reserved_words.begin(), reserved_words.end(), token) != reserved_words.end()) {
                return TokenType::RESERVED_WORD;
            }
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
        if (expected_type != token_type(false))
            panic("unexpected token type");
        return pop();
    }

    bool at(TokenType type)
    {
        return !eof() && token_type(false) == type;
    }

    bool at(TokenType type, const char *value)
    {
        return at(type) && token == value;
    }

    // TODO: Simplify the reserved/unreserved code

    bool at_reserved(TokenType type)
    {
        return !eof() && token_type(true) == type;
    }

    bool at_reserved(TokenType type, const char *value)
    {
        return at_reserved(type) && token == value;
    }

    void eat_reserved(TokenType type, const char *value)
    {
        if (!at_reserved(type, value)) {
            if (eof())
                printf("syntax error near unexpected EOF\n");
            else
                printf("syntax error near unexpected token %s\n", token.c_str());
            panic("could not eat token");
        }
        pop();
    }
};

struct ast_redirect
{
    string lhs;
    string op;
    string rhs;
};

struct ast_and_or;

struct ast_compound_list
{
    vector<ast_and_or> and_ors;
};

struct ast_simple_command
{
    vector<string> assignments;
    vector<string> args;
    vector<ast_redirect> redirections;
};

struct ast_brace_group
{
    ast_compound_list commands;
};

struct ast_subshell
{
    ast_compound_list commands;
};

struct ast_for_clause
{
    string var_name;
    vector<string> wordlist;
    ast_compound_list body;
};

struct ast_case_clause
{
    string value;
    vector<vector<string>> patterns;
    vector<ast_compound_list> bodies;
};

struct ast_if_clause
{
    vector<ast_compound_list> conditions;
    vector<ast_compound_list> bodies;
};

struct ast_while_clause
{
    ast_compound_list condition;
    ast_compound_list body;
    bool until = false;
};

struct ast_command
{
    variant<
        ast_simple_command,
        ast_brace_group,
        ast_subshell,
        ast_for_clause,
        ast_case_clause,
        ast_if_clause,
        ast_while_clause> cmd;
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
    ast_compound_list commands;
};

// Read zero or more new lines
void parse_skip_linebreak(TokenReader &r)
{
    while (r.at(TokenType::NEWLINE))
        r.pop();
}

bool at_redirect_operator(TokenReader &r)
{
    return r.at(TokenType::OPERATOR, "<")
        || r.at(TokenType::OPERATOR, ">")
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

bool at_assignment_word(TokenReader &r)
{
    if (!r.at(TokenType::WORD))
        return false;

    string word = r.peek();
    size_t equals_index = word.find_first_of('=');

    if (equals_index == string::npos || equals_index == 0)
        return false;
    
    for (size_t i = 0; i < equals_index; i++)
        if (!isalnum(word[i]) && word[i] != '_')
            return false;
    
    return true;
}

ast_simple_command parse_simple_command(TokenReader &r)
{
    ast_simple_command simple_command;

    while (true) {
        if (at_assignment_word(r))
            simple_command.assignments.push_back(r.pop());
        else if (at_redirect(r))
            simple_command.redirections.push_back(parse_redirect(r));
        else
            break;
    }

    while (true) {
        if (r.at(TokenType::WORD))
            simple_command.args.push_back(r.pop());
        else if (at_redirect(r))
            simple_command.redirections.push_back(parse_redirect(r));
        else
            break;
    }

    return simple_command;
}

ast_compound_list parse_compound_list(TokenReader &r);

ast_brace_group parse_brace_group(TokenReader &r)
{
    ast_brace_group brace_group;

    r.eat_reserved(TokenType::RESERVED_WORD, "{");
    brace_group.commands = parse_compound_list(r);
    r.eat_reserved(TokenType::RESERVED_WORD, "}");

    return brace_group;
}

ast_subshell parse_subshell(TokenReader &r)
{
    ast_subshell subshell;

    r.eat_reserved(TokenType::OPERATOR, "(");
    subshell.commands = parse_compound_list(r);
    r.eat_reserved(TokenType::OPERATOR, ")");

    return subshell;
}

ast_for_clause parse_for_clause(TokenReader &r)
{
    ast_for_clause for_clause;

    r.eat_reserved(TokenType::RESERVED_WORD, "for");
    for_clause.var_name = r.pop(TokenType::WORD);
    parse_skip_linebreak(r);

    if (r.at_reserved(TokenType::RESERVED_WORD, "in")) {
        r.pop();
        while (r.at(TokenType::WORD))
            for_clause.wordlist.push_back(r.pop());
    }

    if (r.at(TokenType::OPERATOR, ";"))
        r.pop();
    parse_skip_linebreak(r);

    r.eat_reserved(TokenType::RESERVED_WORD, "do");
    for_clause.body = parse_compound_list(r);
    r.eat_reserved(TokenType::RESERVED_WORD, "done");

    return for_clause;
}

ast_case_clause parse_case_clause(TokenReader &r)
{
    ast_case_clause case_clause;

    r.eat_reserved(TokenType::RESERVED_WORD, "case");
    case_clause.value = r.pop(TokenType::WORD);
    parse_skip_linebreak(r);
    r.eat_reserved(TokenType::RESERVED_WORD, "in");
    parse_skip_linebreak(r);

    while (!r.at_reserved(TokenType::RESERVED_WORD, "esac")) {
        if (r.at(TokenType::OPERATOR, "("))
            r.pop();
        
        vector<string> pattern;

        pattern.push_back(r.pop(TokenType::WORD));

        while (r.at(TokenType::OPERATOR, "|")) {
            r.pop();
            pattern.push_back(r.pop(TokenType::WORD));
        }

        // TODO: Doesn't really need to be reserved
        r.eat_reserved(TokenType::OPERATOR, ")");

        case_clause.patterns.push_back(pattern);
        case_clause.bodies.push_back(parse_compound_list(r));

        if (r.at_reserved(TokenType::OPERATOR, ";;")) {
            r.pop();
            parse_skip_linebreak(r);
        }
    }

    r.eat_reserved(TokenType::RESERVED_WORD, "esac");

    return case_clause;
}

ast_if_clause parse_if_clause(TokenReader &r)
{
    ast_if_clause if_clause;

    r.eat_reserved(TokenType::RESERVED_WORD, "if");

    do {
        if_clause.conditions.push_back(parse_compound_list(r));
        r.eat_reserved(TokenType::RESERVED_WORD, "then");
        if_clause.bodies.push_back(parse_compound_list(r));
    } while(r.at_reserved(TokenType::RESERVED_WORD, "elif"));

    if (r.at_reserved(TokenType::RESERVED_WORD, "else")) {
        r.pop();
        if_clause.bodies.push_back(parse_compound_list(r));
    }
    r.eat_reserved(TokenType::RESERVED_WORD, "fi");

    return if_clause;
}

ast_while_clause parse_while_clause(TokenReader &r)
{
    ast_while_clause while_clause;

    while_clause.until = r.at_reserved(TokenType::RESERVED_WORD, "until");
    r.pop();
    while_clause.condition = parse_compound_list(r);
    r.eat_reserved(TokenType::RESERVED_WORD, "do");
    while_clause.body = parse_compound_list(r);
    r.eat_reserved(TokenType::RESERVED_WORD, "done");

    return while_clause;
}

ast_command parse_command(TokenReader &r)
{
    ast_command command;

    if (r.at_reserved(TokenType::RESERVED_WORD, "{"))
        command.cmd = parse_brace_group(r);
    else if (r.at_reserved(TokenType::OPERATOR, "("))
        command.cmd = parse_subshell(r);
    else if (r.at_reserved(TokenType::RESERVED_WORD, "for"))
        command.cmd = parse_for_clause(r);
    else if (r.at_reserved(TokenType::RESERVED_WORD, "case"))
        command.cmd = parse_case_clause(r);
    else if (r.at_reserved(TokenType::RESERVED_WORD, "if"))
        command.cmd = parse_if_clause(r);
    else if (r.at_reserved(TokenType::RESERVED_WORD, "while") || r.at_reserved(TokenType::RESERVED_WORD, "until"))
        command.cmd = parse_while_clause(r);
    else
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

        if (r.at(TokenType::OPERATOR, "|")) {
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

    if (r.at(TokenType::OPERATOR, ";") || r.at(TokenType::OPERATOR, "&")) {
        and_or.async = r.at(TokenType::OPERATOR, "&");
        r.pop();
    }

    return and_or;
}

bool at_compound_list_end(TokenReader &r)
{
    return r.at_reserved(TokenType::OPERATOR, ")")
        || r.at_reserved(TokenType::RESERVED_WORD, "then")
        || r.at_reserved(TokenType::RESERVED_WORD, "else")
        || r.at_reserved(TokenType::RESERVED_WORD, "elif")
        || r.at_reserved(TokenType::RESERVED_WORD, "fi")
        || r.at_reserved(TokenType::RESERVED_WORD, "do")
        || r.at_reserved(TokenType::RESERVED_WORD, "done")
        || r.at_reserved(TokenType::RESERVED_WORD, "esac")
        || r.at_reserved(TokenType::RESERVED_WORD, "}")
        || r.at_reserved(TokenType::OPERATOR, ";;");
}

ast_compound_list parse_compound_list(TokenReader &r)
{
    ast_compound_list compound_list;

    parse_skip_linebreak(r);

    while (!r.eof() && !at_compound_list_end(r)) {
        compound_list.and_ors.push_back(parse_and_or(r));
        parse_skip_linebreak(r);
    }

    return compound_list;
}

ast_program parse_program(TokenReader &r)
{
    ast_program program;

    program.commands = parse_compound_list(r);

    if (!r.eof())
        panic("unexpected token at end of program");

    return program;
}

// Shell Execution environment

struct var
{
    string value;
    bool exported;
};

class ex_env
{
    map<string, var> vars;

public:
    bool contains(const string &name)
    {
        return vars.find(name) != vars.end();
    }

    const string &get(const string &name)
    {
        return vars.at(name).value;
    }
    
    void set(const string &name, const string &value)
    {
        vars[name].value = value;
    }

    void mark_export(const string &name)
    {
        vars[name].exported = true;
    }

    void init_from_environ()
    {
        for (char **s = environ; *s; s++) {
            char *equals = strchr(*s, '=');
            if (!equals)
                continue;
            
            string name = string(*s, equals - *s);
            const char *value = equals + 1;

            set(name, value);
            mark_export(name);
        }
    }
};

ex_env xenv;

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

int execute_in_subshell(const string &program);

string expand_command(const string &command)
{
    // TODO: Find something more elegant than redirecting to a file

    // The X-s will be replaced by mkstemp
    char filename[] = "/tmp/shell_stdoutXXXXXX";
    int new_stdout = mkstemp(filename);
    int old_stdout = dup(1);

    dup2(new_stdout, 1);
    close(new_stdout);
    execute_in_subshell(command);
    dup2(old_stdout, 1);
    close(old_stdout);

    string result = read_file(filename);
    unlink(filename);

    while (result.size() && result.back() == '\n')
        result.pop_back();

    return result;
}

string expand_param(const string &param)
{
    if (!xenv.contains(param))
        return "";
    
    return xenv.get(param);
}

// Field splitting

void field_split(vector<string> &fields, const string &str)
{
    const char *ifs = getenv("IFS");

    if (!ifs)
        ifs = " \t\n";
    
    if (strlen(ifs) == 0) {
        fields.back().append(str);
        return;
    }

    // IFS is separated into whitespace IFS (what we call soft IFS)
    // and non-whitespace IFS (what we call hard IFS)
    string soft_ifs;
    string hard_ifs;

    for (const char *c = ifs; *c; c++) {
        if (isspace(*c))
            soft_ifs.push_back(*c);
        else
            hard_ifs.push_back(*c);
    }

    // Soft IFS spans are merged together into a single delimiter,
    // and are ignored at the beginning and end of input.
    // Hard IFS aren't merged together, and can delimit empty fields.
    // Hard IFS are merged with the soft IFS around them.
    // Hard IFS at the beginning of the input cause an empty field.

    enum class Mode {
        START,          // We didn't yet start the first field
        FIELD,          // We are in the middle of the field
        SOFT_DELIMIT,   // In a soft IFS span
        HARD_DELIMIT,   // In a span of soft IFS + one hard IFS char
    };

    Mode mode = fields.size() == 0 ? Mode::START : Mode::FIELD;

    for (size_t i = 0; i < str.size(); i++) {
        char c = str[i];

        if (soft_ifs.find(c) != string::npos) {
            if (mode == Mode::START)
                ; // Ignore soft IFS at the beginning of input
            if (mode == Mode::FIELD)
                mode = Mode::SOFT_DELIMIT;
        }
        else if (hard_ifs.find(c) != string::npos) {
            if (mode == Mode::START || mode == Mode::HARD_DELIMIT)
                // Add an empty field
                fields.push_back(string{});

            mode = Mode::HARD_DELIMIT;
        }
        else {
            if (mode != Mode::FIELD) {
                // Begin new field
                fields.push_back(string{});
                mode = Mode::FIELD;
            }

            fields.back().push_back(c);
        }
    }
}

void field_append(vector<string> &fields, char c)
{
    if (fields.size())
        fields.back().push_back(c);
    else
        fields.push_back(string{c});
}

void field_append(vector<string> &fields, const string &str)
{
    if (fields.size())
        fields.back().append(str);
    else
        fields.push_back(str);
}

string expand_dollar_or_backquote(Reader &r)
{
    if (r.at("$(("))
        //return r.read_arithmetic_expand(false);
        panic("arithmetic expansion not implemented");
    else if (r.at("$("))
        return expand_command(r.read_subshell(false));
    else if (r.at("${"))
        //return r.read_param_expand_in_braces(false);
        panic("parameter expansion not implemented");
    else if (r.at('$'))
        return expand_param(r.read_param_expand(false));
    else if (r.at('`'))
        return expand_command(r.read_subshell_backquote(false));
    else
        assert(0);
}

vector<string> expand_word(const string &word)
{
    Reader r(word);
    vector<string> fields;

    // TODO: deal with variable assignments that support multiple tilde-prefixes
    if (r.at('~')) {
        string first_part = r.read_regular_part();
        size_t slash = first_part.find('/');
        // This works nicely when slash is string::npos
        string tilde_prefix = first_part.substr(1, slash - 1);

        field_append(fields, expand_tilde_prefix(tilde_prefix));

        if (slash != string::npos)
            field_append(fields, first_part.substr(slash));
    }

    // TODO: Impelment the different expansions

    while (!r.eof()) {
        if (r.at('\\')) {
            field_append(fields, r.read_slash_quote(false));
        }
        else if (r.at('\'')) {
            // Empty Quotes create empty field
            if (fields.size() == 0)
                fields.push_back(string{});

            field_append(fields, r.read_single_quote(false));
        }
        else if (r.at('\"')) {
            string inner_data = r.read_double_quote(false);
            Reader r(inner_data);

            // Empty Quotes create empty field
            if (fields.size() == 0)
                fields.push_back(string{});

            while (!r.eof()) {
                if (r.at("\\$") || r.at("\\`") || r.at("\\\\"))
                    field_append(fields, string{"\\"} + r.pop());
                else if (r.at('$') || r.at('`'))
                    field_append(fields, expand_dollar_or_backquote(r));
                else
                    field_append(fields, r.pop());
            }
        }
        else if (r.at('$') || r.at('`'))
            field_split(fields, expand_dollar_or_backquote(r));
        else
            field_append(fields, r.read_regular_part());
    }

    return fields;
}

string expand_word_no_split(const string &word)
{
    // TODO: Implement this properly

    vector<string> fields = expand_word(word);
    if (fields.size() != 1)
        panic("accidentally did field splitting...");
    return fields[0];
}

vector<string> expand_words(const vector<string> &words)
{
    vector<string> expanded;

    for (const string &word : words) {
        vector<string> fields = expand_word(word);
        expanded.insert(expanded.end(), fields.begin(), fields.end());
    }

    return expanded;
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
        
        vector<string> results = expand_word(redirect.rhs);
        if (results.size() != 1)
            panic("ambiguous redirect");

        int right_fd = open(results[0].c_str(), flags, 0666);
        if (right_fd < 0)
            panic("redirect file open failed");
        dup2(right_fd, left_fd);
        close(right_fd);
    }
}

void execute_assignment(const string &assignment_word)
{
    size_t equals = assignment_word.find_first_of('=');
    assert(equals != string::npos);

    vector<string> value_parts = expand_word(assignment_word.substr(equals + 1));
    string value;

    if (value_parts.size() == 0)
        value = string();
    else if (value_parts.size() == 1)
        value = value_parts[0];
    else
        // TODO: decide how to handle this case
        panic("assignment of multiple fields");

    xenv.set(assignment_word.substr(0, equals), value);
}

int execute_simple_command(const ast_simple_command &simple_command)
{
    vector<string> expanded_args = expand_words(simple_command.args);

    if (expanded_args.size() > 0) {
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
    }
    else {
        // When there is no command to execute, we don't fork so the assignments
        // will change the current execution environment.
    }

    for (const ast_redirect &redirect : simple_command.redirections)
        execute_redirect(redirect);

    for (const string &assignment : simple_command.assignments)
        execute_assignment(assignment);
    
    if (expanded_args.size() > 0) {
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
    else {
        return 0;
    }
}

int execute_compound_list(const ast_compound_list &compound_list);

int execute_subshell(const ast_subshell &subshell)
{
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

    exit(execute_compound_list(subshell.commands));
}

int execute_for_clause(const ast_for_clause &for_clause)
{
    int exit_status = 0;

    if (for_clause.wordlist.size() == 0)
        panic("for with no wordlist not implemented");

    for (const string &word : expand_words(for_clause.wordlist)) {
        xenv.set(for_clause.var_name, word);
        exit_status = execute_compound_list(for_clause.body);
    }

    return exit_status;
}

int execute_case_clause(const ast_case_clause &case_clause)
{
    int exit_status = 0;
    
    string expanded_value = expand_word_no_split(case_clause.value);

    for (size_t i = 0; i < case_clause.patterns.size(); i++) {
        const vector<string> &patterns = case_clause.patterns[i];

        bool matched = false;

        for (const string &pattern : patterns) {
            string expanded_pattern = expand_word_no_split(pattern);
            // TODO: Implement pattern matching
            if (expanded_pattern == expanded_value) {
                matched = true;
                break;
            }
        }

        if (matched) {
            exit_status = execute_compound_list(case_clause.bodies[i]);
            break;
        }
    }

    return exit_status;
}

int execute_if_clause(const ast_if_clause &if_clause)
{
    for (size_t i = 0; i < if_clause.conditions.size(); i++)
        if (execute_compound_list(if_clause.conditions[i]) == 0)
            return execute_compound_list(if_clause.bodies[i]);
    
    // Else
    if (if_clause.bodies.size() > if_clause.conditions.size()) {
        assert(if_clause.bodies.size() == if_clause.conditions.size() + 1);
        return execute_compound_list(if_clause.bodies.back());
    }
    else {
        return 0;
    }
}

int execute_while_clause(const ast_while_clause &while_clause)
{
    int exit_status = 0;

    while ((execute_compound_list(while_clause.condition) == 0) == !while_clause.until) {
        exit_status = execute_compound_list(while_clause.body);
    }

    return exit_status;
}

int execute_command(const ast_command &command)
{
    if (std::holds_alternative<ast_simple_command>(command.cmd)) {
        return execute_simple_command(std::get<ast_simple_command>(command.cmd));
    }
    else if (std::holds_alternative<ast_brace_group>(command.cmd)) {
        return execute_compound_list(std::get<ast_brace_group>(command.cmd).commands);
    }
    else if (std::holds_alternative<ast_subshell>(command.cmd)) {
        return execute_subshell(std::get<ast_subshell>(command.cmd));
    }
    else if (std::holds_alternative<ast_for_clause>(command.cmd)) {
        return execute_for_clause(std::get<ast_for_clause>(command.cmd));
    }
    else if (std::holds_alternative<ast_case_clause>(command.cmd)) {
        return execute_case_clause(std::get<ast_case_clause>(command.cmd));
    }
    else if (std::holds_alternative<ast_if_clause>(command.cmd)) {
        return execute_if_clause(std::get<ast_if_clause>(command.cmd));
    }
    else if (std::holds_alternative<ast_while_clause>(command.cmd)) {
        return execute_while_clause(std::get<ast_while_clause>(command.cmd));
    }
    else {
        panic("command type execution not implemented");
    }
}

int execute_pipeline(const ast_pipeline &pipeline)
{
    int exit_status = 0;
    
    int rpipe[2] = {-1, -1};
    int wpipe[2] = {-1, -1};

    vector<pid_t> pids;

    const vector<ast_command> &commands = pipeline.commands;

    if (commands.size() == 1) {
        // This is both an optimization, and it is required for variable
        // assignments to modify the current execution environment.
        exit_status = execute_command(commands[0]);
        goto ret;
    }
    
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

    for (auto pid : pids) {
        int wstatus;
        waitpid(pid, &wstatus, 0);
        exit_status = WEXITSTATUS(wstatus);
    }

ret:

    if (pipeline.invert_exit_code)
        return !exit_status;
    else
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

int execute_compound_list(const ast_compound_list &compound_list)
{
    int exit_status = 0;

    for (const ast_and_or& and_or : compound_list.and_ors) {
        exit_status = execute_and_or(and_or);
    }

    return exit_status;
}

int execute_program(const ast_program &program)
{
    return execute_compound_list(program.commands);
}

int execute(const string &program)
{
    TokenReader r = TokenReader(Reader(program));
    ast_program p = parse_program(r);
    return execute_program(p);
}

int execute_in_subshell(const string &program)
{
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

    exit(execute(program));
}

int_least32_t repl()
{
    string line;
    int exit_status = 0;

    std::cout << "$ " << std::flush;
    while (std::getline(std::cin, line)) {
        // Reader r(line);
        // while (!r.eof())
        //     std::cout << r.read_token() << std::endl;
        exit_status = execute(line);
        std::cout << "$ " << std::flush;
    }

    return exit_status;
}

int main(int argc, const char *argv[])
{
    xenv.init_from_environ();

    if (argc == 3 && strcmp(argv[1], "-c") == 0) {
        // TODO: Robust argument parsing
        return execute(argv[2]);
    }

    return repl();

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