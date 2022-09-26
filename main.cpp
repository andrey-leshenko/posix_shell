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

struct _redirection
{
    int src_fd;
    const char* target_file;
    int target_fd;
    RedirType type;
};

void redirect(const _redirection& redir)
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
    const vector<string>& args
)
    // const vector<redirection>& redirections)
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
    for (auto& a : args)
        argv.push_back(a.c_str());
    argv.push_back(nullptr);
    // execve doesn't modify its arguments
    char ** argv_ptr = const_cast<char **>(&argv[0]);

    // Redirections
    // for (auto& r : redirections)
    //     redirect(r);

    execvp(argv[0], argv_ptr);
    panic("execve failed");
}

// Prasing

#include "parser.cpp"

int main()
{
    // run_command("echo", {"hello", "world", "from echo"}, {});
    // run_command("cat", {}, {redirection{0, "/etc/hostname", -1, RedirType::INPUT}});
    // run_command("echo", {"test redirection"}, {redirection{1, "/tmp/redir1", -1, RedirType::OUTPUT}});
    // run_command("echo", {"test append"}, {redirection{1, "/tmp/redir2", -1, RedirType::APPEND}});
    // run_command("echo", {"test append"}, {redirection{1, "/tmp/redir2", -1, RedirType::APPEND}});
    // run_command("echo", {"test append"}, {redirection{1, "/tmp/redir2", -1, RedirType::APPEND}});
    // run_command("echo", {"test dup"}, {redirection{1, nullptr, 2, RedirType::INPUT_DUP}});

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

    parse(script.c_str(), script.size());

    return 0;
}