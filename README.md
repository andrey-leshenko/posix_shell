# POSIX Shell

A partial shell implementation based on the [POSIX shell specification](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html#tag_18_01),
done in C++.
Inspired by the excellent [Shell Hater's Handbook](https://shellhaters.org/talk).

Since the POSIX is much simpler than modern shells like Bash or Zsh, it is much easier to understand and to implement.

Implemented features:
- Token recognition with quoting
- Parameter and tilde expansion
- Command substitution
- Field splitting
- Redirection
- Pipelines
- And-or lists
- Variables and environments
- Control structures (while, for, case, etc.)
- Functions

Missing features:
- Various built-ins
- Most special parameters
- Arithmetic expansion
- Here docs
- Asynchronous lists
- Pattern matching
- Signal and error handling
- Shell variables like `PS1`
