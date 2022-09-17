#include <stdio.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#include <vector>
#include <string>
#include <map>

using std::vector;
using std::string;
using std::map;

void panic(const char* msg)
{
    printf("panic: %s\n", msg);
    exit(-1);
}

enum class RedirType {
    INPUT,
    OUTPUT,
    APPEND,
    INPUT_DUP,
    CLOSE,
};

struct redirection
{
    int src_fd;
    const char* target_file;
    int target_fd;
    RedirType type;
};

void redirect(const redirection& redir)
{
    if (redir.type == RedirType::INPUT || redir.type == RedirType::OUTPUT || redir.type == RedirType::APPEND) {
        int flags = 0;

        if (redir.type == RedirType::INPUT) {
            flags = O_RDONLY;
        }
        else if (redir.type == RedirType::OUTPUT) {
            flags = O_WRONLY | O_CREAT | O_TRUNC;
        }
        else if (redir.type == RedirType::APPEND) {
            flags = O_WRONLY | O_CREAT | O_APPEND;
        }

        int fd = open(redir.target_file, flags, 0666);
        if (fd < 0)
            panic("redirect file open failed");
        dup2(fd, redir.src_fd);
        close(fd);
    }
    else if (redir.type == RedirType::INPUT_DUP) {
        dup2(redir.target_fd, redir.src_fd);
    }
    else if (redir.type == RedirType::CLOSE) {
        close(redir.src_fd);
    }
}

int run_command(
    const string& command,
    const vector<string>& args,
    const vector<redirection>& redirections)
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

    // Child
    vector<const char*> argv;
    argv.push_back(command.c_str());
    for (auto& a : args)
        argv.push_back(a.c_str());
    argv.push_back(nullptr);
    // execve doesn't modify its arguments
    char ** argv_ptr = const_cast<char **>(&argv[0]);

    // Redirections
    for (auto& r : redirections)
        redirect(r);

    execvp(argv[0], argv_ptr);
    panic("execve failed");
}

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

bool is_first_operator_char(char c)
{
    for (auto& op : operators)
        if (op[0] == c)
            return true;

    return false;
}

bool is_operator_continuation(const string& tok, char c)
{
    string combined = tok + c;

    for (auto& op : operators)
        if (op.substr(0, combined.size()) == combined)
            return true;

    return false;
}

void tokenize(const char* str, size_t len)
{
    vector<string> tokens;
    string tok;
    bool in_operator = false;
    bool quoted = false;
    bool in_slash_quote = false;

    #define DELIMIT_TOK in_operator = false; if (tok.size()) {tokens.emplace_back(std::move(tok));}

    size_t i = 0;

    while (true) {
        if (i >= len) {
            tokens.emplace_back(std::move(tok));
            break;
        }

        char c = str[i];

        if (in_operator) {
            if (is_operator_continuation(tok, c)) {
                tok.push_back(c);
                continue;
            }
            else {
                DELIMIT_TOK;
            }
        }

        if (!quoted) {
            if (c == '\\') {
                tok.push_back(c);
                in_slash_quote = true;
                continue;
            }
            // handle ' "
            // handle $ `

            if (is_first_operator_char(c)) {
                DELIMIT_TOK;
                tok.clear();
                tok.push_back(c);
                in_operator = true;
                continue;
            }
            if (c == ' ') {
                DELIMIT_TOK;
                continue;
            }
        }

        if (prev_part_of_word) {
            tok.push_back(c);
        }

        if (c == '#') {
            while (i < len && str[i] != '\n')
                i++;
            // What now???
        }



    }
}

int main()
{
    run_command("echo", {"hello", "world", "from echo"}, {});
    run_command("cat", {}, {redirection{0, "/etc/hostname", -1, RedirType::INPUT}});
    run_command("echo", {"test redirection"}, {redirection{1, "/tmp/redir1", -1, RedirType::OUTPUT}});
    run_command("echo", {"test append"}, {redirection{1, "/tmp/redir2", -1, RedirType::APPEND}});
    run_command("echo", {"test append"}, {redirection{1, "/tmp/redir2", -1, RedirType::APPEND}});
    run_command("echo", {"test append"}, {redirection{1, "/tmp/redir2", -1, RedirType::APPEND}});
    run_command("echo", {"test dup"}, {redirection{1, nullptr, 2, RedirType::INPUT_DUP}});
    return 0;
}