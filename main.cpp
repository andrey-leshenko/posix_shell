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
#include <iostream>

using std::vector;
using std::string;
using std::map;

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

// Prasing

#include "parser.cpp"

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