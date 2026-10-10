# SimpleShell — Command-Line Interpreter

SimpleShell is a Unix-like command-line interpreter written in **C (C11)** for Linux/POSIX environments. It provides an interactive prompt, supports built-in commands, and launches external programs through POSIX process-management system calls.

## Project Overview

The shell follows a layered design:

1. **REPL** — displays the prompt, reads a command line, and repeats until exit or EOF.
2. **Lexer and parser** — tokenise input and build command structures, including pipeline and redirection information.
3. **Variable expansion** — handles shell values such as environment variables and command status.
4. **Executor** — runs built-in commands directly or starts external programs.
5. **POSIX interface** — uses process and file-descriptor operations to execute commands and connect pipelines.

## Features

The project specification and test plan describe support for:

- Interactive command prompt and clean EOF handling (`Ctrl-D`)
- Parsing whitespace and single-/double-quoted text
- Built-in commands: `cd`, `pwd`, `echo`, `export`, `unset`, `help`, and `exit`
- External command execution using `PATH` lookup and process creation
- Input/output redirection: `<`, `>`, `>>`, and `2>`
- Command pipelines (`|`), including chained pipelines
- Environment/status expansion, including `$VAR`, `${VAR}`, `$?`, and `$$`
- Signal handling for interactive `Ctrl-C` and `Ctrl-Z` scenarios

Features such as command history, tab completion, shell scripting control structures, and background-job commands are listed as out of scope for version 1.0.

## Requirements

- Linux/POSIX environment (Ubuntu 22.04 LTS is listed as the primary test environment; WSL2 can also be used)
- GCC 11+ or a compatible C11 compiler
- GNU Make
- Valgrind for memory checks (optional for building, useful for testing)

## Build and Run

From the project root, build using the Makefile:

```bash
make clean && make
```

The test plan specifies that this generates the `./shell` executable. Run it with:

```bash
./shell
```

If your repository's Makefile or executable name differs, use the commands documented in the source code repository.

## Testing

The test plan describes automated regression tests, interactive checks for signals, boundary tests for long input and pipelines, compiler-warning checks, and Valgrind memory analysis. Run any test scripts included in the repository according to their instructions.

Example static-analysis/build flags listed in the project documents:

```bash
gcc -Wall -Wextra -Wpedantic -std=c11
```

## Project Team

- Rishab Naveen — PES1UG24CS710
- Aadarsh Reddy Yedhala — PES1UG24CS706
- Vatsal Gupta — PES1UG24CS702
- Vineet — PES1UG24CS698

## Project Documents

- Software Requirements Specification (SRS)
- Software Architecture and Design Specification
- Test Plan Specification

---

**Note:** This README summarizes the intended design and feature scope described in the project documents. Actual behaviour depends on the implementation present in the repository.
