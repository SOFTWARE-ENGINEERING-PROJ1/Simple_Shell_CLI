# SimpleShell — Command-Line Interpreter

SimpleShell is a Unix-like command-line interpreter written in **C (C11)** for Linux/POSIX environments.

## Features

- Interactive prompt and clean EOF handling (`Ctrl-D`)
- Quoted input parsing and environment/status expansion (`$VAR`, `${VAR}`, `$?`, `$$`)
- Built-ins: `cd`, `pwd`, `echo`, `export`, `unset`, `help`, `exit`
- External command execution through `PATH`
- Input/output/error redirection: `<`, `>`, `>>`, `2>`
- Pipelines using `|`
- Interactive command history (Up/Down) and reverse search (Ctrl+R)
- Tab completion for built-ins, PATH commands, and filenames
- Background execution using `&`
- Job management using `jobs`, `fg`, and `bg`
- Command chaining using `;`, `&&`, and `||`
- Interactive signal and foreground-process-group handling

> Feature availability is based on the source code in this project. The complete S1–S8 regression and interactive validation must be run before treating every feature as verified.

## Requirements

- Linux/POSIX environment (Ubuntu recommended)
- GCC or another C11-compatible compiler
- GNU Make
- GNU Readline development headers/library (`libreadline-dev` on Ubuntu)
- Python 3 for interactive/PTY tests
- Valgrind for memory checking (optional)

Install dependencies on Ubuntu:

```bash
sudo apt update
sudo apt install build-essential make libreadline-dev python3
# Optional memory checker:
sudo apt install valgrind
```

## Build and Run

```bash
make clean
make
./shell
```

## Testing

Run all S1–S8 automated and interactive tests:

```bash
make test
```

Run memory checking separately:

```bash
make memcheck
```

Run the S9 validation workflow, which creates timestamped logs under `test-evidence/`:

```bash
make s9-validation
```

Some tests use a pseudo-terminal and require an interactive-capable Linux environment. Review failures and test logs rather than treating a successful build alone as proof of correctness.

## Project Structure

- `main.c` — REPL, command execution, pipelines, redirection, command chaining
- `lexer.c` — tokenisation and operator recognition
- `expand.c` — environment, status, and PID expansion
- `builtins.c` — built-in commands, including job-control commands
- `signals.c` — signal handling and terminal process-group control
- `interactive.c` — GNU Readline, history, reverse search, and completion
- `jobs.c` — background job tracking, state updates, reaping, and cleanup
- `shell.h` — shared structures and function declarations
- `tests/` — regression and interactive tests
- `scripts/` — validation automation

## Project Team

- Rishab Naveen — PES1UG24CS710
- Aadarsh Reddy Yedhala — PES1UG24CS706
- Vatsal Gupta — PES1UG24CS702
- Vineet — PES1UG24CS698

## Project Documents

See `docs/` for the Software Requirements Specification, Architecture Design, and Test Plan. Update those controlled documents with the final S7/S8 results after S9 validation; do not mark test cases passed until logs provide evidence.
