#!/usr/bin/env python3

import sys
import subprocess

TEST_BINARY = './main'
REFERENCE_BINARY = '/bin/bash'

TESTS = [
    # simple commands
    r'echo 123',
    r'echo 1 2 3',
    r'echo 1 2 3',

    # slash escape
    r'echo hello\ world',
    r'echo hello\ \ world',
    r'echo \ world',
    r'echo world\ ',
    r'echo \ ',
    r'echo \    \  x  \ ',
    r'echo \|\&\;\<\>\(\)\$\`\\\"\\\'',
    r'echo \a\b\c\d\e\f\g',

    # Multiple commands
    r'echo 1;echo 2 ;echo 3 ; echo 4',
    #r'echo 1 ; ; ; ; echo 2',

    # And or
    r'false && echo foo || echo bar',
    r'true && echo foo || echo bar',
    r'true || echo foo && echo bar',
    r'false || echo foo && echo bar',

    # Pipelines
    r'echo hello | xxd',
    r'echo hello | xxd | xxd | xxd | xxd | xxd',

    # redirections
    r'echo hello >/dev/null',
    r'echo hello 1> /dev/null',
    r'echo hello> /dev/null',
    r'echo hello \>/dev/null',
    r'echo hello > /tmp/x ; xxd /tmp/x ; rm /tmp/x',

    r'echo hello \>/dev/null',
    r'wc wrong_name',
    r'wc wrong_name 2>/dev/null',
    r'wc wrong_name 2> /tmp/x ; xxd /tmp/x ; rm /tmp/x',
    r'wc wrong_name 2>&1',
    r'echo hello >&2',
    r'echo hello 1>&2',

    # substitutions
    r'echo hello $(echo world) yay',
    r'echo hello `echo world` yay',
    r'echo hello $(echo $(echo world)) yay',
    r'echo hello `echo \`echo world\`` yay',

    # Variable scope
    r'echo $A ; A=123 ; echo $A',
    r'echo $A ; echo $(A=123) ; echo $A',
    r'echo $A ; echo `A=123` ; echo $A',
    r'echo $A ; (A=123) ; echo $A',

    # quoting

    r'echo "hello   world"',

    r"echo 'hello   world'",
    r"echo 'hello \ $(date) `date` ${date}   world'",

    r'echo',
    r'echo ""',
    r'echo "" ""',
    r"echo ''",
    r"echo '' ''",

    # field splitting

    # reserved words

    r'echo ! { } case do done elif else esac fi for if in then until while',

    # if
    r'if false; then echo true; else echo false; fi',

    # for
    r'for x in 1 2 3; do echo $x; done',
    r'for x in 1$(echo 1 2 3)3; do echo $x; done'
]

def run_test(command):
    p = subprocess.Popen([REFERENCE_BINARY, '-c', command], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    output_ref = p.communicate()
    p = subprocess.Popen([TEST_BINARY, '-c', command], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    output_test = p.communicate()

    if (output_test != output_ref):
        print('\nERROR!')
        print('Test:')
        print(command)
        print('Expected output:')
        print(output_ref)
        print('Our output:')
        print(output_test)
        return False
    else:
        return True

def main():
    passed_tests = 0
    for t in TESTS:
        if run_test(t):
            print('.', end='')
            sys.stdout.flush()
            passed_tests += 1
    
    print('\nPassed {}/{} tests'.format(passed_tests, len(TESTS)))

if __name__ == '__main__':
    main()
