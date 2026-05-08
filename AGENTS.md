# Repository Guidelines

## Project Structure & Module Organization

This repository currently contains the VIS documentation and the `vis-jitter` C++ tool. Root-level `README.md` describes the project vision, architecture, CLI usage, and benchmark results. The implementation lives in `vis-jitter/`: public headers are in `vis-jitter/include/`, source files are in `vis-jitter/src/`, and component tests are in `vis-jitter/tests/`. The built executable and test binaries are emitted directly into `vis-jitter/`.

## Build, Test, and Development Commands

Run commands from `vis-jitter/` unless noted otherwise.

```bash
make
```

Builds the `vis-jitter` binary with `g++`, C++17, `-Wall -Wextra -O2`, `libnuma`, and pthreads.

```bash
make test
```

Builds and runs the rootless component tests.

```bash
make test-root
```

Builds and runs `test_smi_audit` with `sudo`. The SMI test requires MSR access and may prompt for credentials.

```bash
make clean
```

Removes generated binaries. For local measurement, load MSR support first with `sudo modprobe msr`, then run examples such as `sudo ./vis-jitter --cpu 2 --duration 60 --threshold 100`.

## Coding Style & Naming Conventions

Use C++17 and keep the existing C-style API surface. Public types and functions use the `vis_` prefix, for example `vis_report_t`, `vis_measure`, and `vis_histogram_compute`. Keep headers in `include/` paired with implementation files in `src/`. Follow the current formatting: 4-space indentation, braces on function lines, aligned continuation lines where it improves readability, and short explanatory comments for hardware-sensitive logic.

## Testing Guidelines

Tests are standalone C++ executables under `vis-jitter/tests/` named `test_<component>.cpp`. Prefer deterministic checks that return nonzero on failure and print clear `[test]` status lines. Add a Makefile target for each new test and include it in `make test` when it is safe to run locally. Separate rootless unit tests from tests requiring privileged MSR or NUMA behavior.

## Commit & Pull Request Guidelines

Recent history uses both conventional prefixes (`fix:`, `docs:`) and milestone-style messages (`V1: ...`). Prefer concise imperative commits with a scope when useful, such as `fix: emit valid JSON reports` or `docs: update SMI results`. Pull requests should describe the behavioral change, list test commands run, call out root-required validation, and include sample report output or screenshots when CLI/report formatting changes.

## Security & Configuration Tips

Do not commit generated reports containing machine-specific identifiers unless they are intentional evidence. Treat `sudo`, MSR access, CPU isolation settings, and NUMA assumptions as environment-specific; document them in PR notes when they affect results.
