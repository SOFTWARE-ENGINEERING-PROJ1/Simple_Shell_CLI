# S9 — Integration, Regression Testing and Documentation

## User story
As a developer, I want to integrate and validate all newly implemented shell features so that the shell remains stable, reliable, and properly documented.

## S9 acceptance checklist

### Build and integration
- [ ] Install documented dependencies, including `libreadline-dev`.
- [ ] Perform a clean Ubuntu build.
- [ ] Resolve all compiler warnings.
- [ ] Verify S7/S8 changes do not regress S1–S6.

### Background process validation
- [ ] Confirm completed background children are reaped while the shell is idle at the prompt.
- [ ] Confirm no zombie processes remain after completion.
- [ ] Test multiple simultaneous background jobs.
- [ ] Test background pipelines.
- [ ] Verify shell exit behaviour with active and stopped jobs.

### Job management validation
- [ ] Test `jobs`, `fg`, and `bg` with valid job IDs.
- [ ] Test invalid and completed job IDs.
- [ ] Test Ctrl+C and Ctrl+Z on foreground jobs.
- [ ] Test stopping and resuming jobs.
- [ ] Verify terminal foreground ownership is restored.

### Command chaining validation
- [ ] Test `;`, `&&`, and `||` individually and in combinations.
- [ ] Test chaining with pipelines and redirection.
- [ ] Test leading, trailing, and repeated operators.
- [ ] Verify `$?` after successful, failed, and chained commands.
- [x] Reject malformed adjacent operator runs such as `|||`, `&&&`, `||&`, and `|&&` during lexing.
- [x] Pre-validate every command-list segment and pipeline before executing any part of the input.
- [x] Add regression tests proving malformed later segments do not allow earlier commands to execute.

### Regression and memory validation
- [ ] Run `make test` and retain the complete output.
- [ ] Confirm all S1–S8 test suites pass.
- [ ] Run `make memcheck` and inspect definite/indirect losses and error summary.
- [ ] Check for zombie/orphaned processes after the interactive job tests.

### Documentation
- [ ] Update SRS scope and requirements for S7/S8.
- [ ] Update Architecture Design with `interactive.c` and `jobs.c` responsibilities.
- [ ] Update Test Plan with S7/S8 cases and evidence-backed results.
- [ ] Update README dependencies, features, build, and test instructions.
- [ ] Record a final demonstration video.

## Evidence policy
Do not mark an acceptance item as passed until the corresponding test has been executed and its output/log has been reviewed. The script `scripts/run_s9_validation.sh` stores timestamped build, regression, and Valgrind logs under `test-evidence/`.
