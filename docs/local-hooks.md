# Local Hooks

VIS keeps local hooks optional. The repository is a small C++/Makefile project,
so it does not require Husky or a Node/npm workflow.

The recommended split is:

- `pre-commit`: fast checks such as `make -C vis-jitter lint`
- `pre-push`: build and rootless tests with `make -C vis-jitter` and
  `make -C vis-jitter test`

Do not run `sudo`, `make test-root`, MSR access, SMI auditing, CPU isolation, or
other machine-sensitive validation from local hooks. Those checks depend on the
specific host and should stay manual unless a self-hosted runner is configured
for that purpose.

## Example hooks

Create `.git/hooks/pre-commit`:

```sh
#!/bin/sh
set -eu

make -C vis-jitter lint
```

Create `.git/hooks/pre-push`:

```sh
#!/bin/sh
set -eu

make -C vis-jitter
make -C vis-jitter test
```

Then make them executable:

```bash
chmod +x .git/hooks/pre-commit .git/hooks/pre-push
```

These hooks are intentionally local and untracked. Contributors who prefer the
`pre-commit` framework can wrap the same commands there, but the project does
not require it.
