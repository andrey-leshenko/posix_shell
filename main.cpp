#include <stdio.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

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

int run_command(const string& command, const vector<string> args)
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

    execvp(argv[0], argv_ptr);
    panic("execve failed");
}



int main()
{
    run_command("echo", {"hello", "world", "from echo"});
    return 0;
}