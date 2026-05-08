# For Humans

Thank you for contributing to VIS. This project is small and performance-sensitive, so the best contributions are focused, reproducible, and easy to review.

Before opening a pull request:

- Fork the repository and create a topic branch from `main`.
- Use the branch naming style `<type>/<short-name>`, for example `chore/contributing-vis-hygiene` or `fix/smi-window-rejection`.
- Keep each branch focused on one issue, bug, or feature.
- Build the affected component before opening a pull request.
- Run the relevant tests for the code you changed.
- Open the pull request against `main`.
- Link the related issue in the pull request body when one exists.

Commit and pull request naming:

- Use the commit message style `<type>(<scope>):<description>`.
- Do not add a space after the colon.
- Keep the description short, lowercase, and imperative when practical.
- Use the same text for the pull request title.
- Examples: `chore(docs):add contributing guide and VIS hygiene`, `fix(jitter):reject migrated samples`.

Local development:

- Install the documented build dependencies before compiling `vis-jitter`.
- Build with `make -C vis-jitter`.
- Run rootless test binaries directly when possible:
  - `make -C vis-jitter test_histogram test_report_json`
  - `./vis-jitter/test_histogram`
  - `./vis-jitter/test_report_json`
- Run the full `make -C vis-jitter test` only when you intend to use `sudo`, because `test_smi_audit` needs MSR access.
- Do not commit generated binaries, local reports, editor files, or cache directories.

Pull request expectations:

- Explain what changed and why.
- Include the commands you ran and their results.
- Mention any test that could not be run and why.
- Keep unrelated refactors out of the pull request.
- Prefer clear, direct code over broad abstractions unless the abstraction removes real duplication.
- Be responsive to review comments and update the pull request with focused follow-up commits.

# For Agents

When working in this repository as a coding agent, treat the working tree as shared with the human user.

Operating rules:

- Start by checking the current branch, repository status, and relevant issue or pull request context.
- Create a focused branch before editing when the task calls for one.
- Use the branch naming style `<type>/<short-name>`.
- Preserve user changes. Never revert, overwrite, or clean unrelated files unless the user explicitly asks.
- Read the nearby code and documentation before editing.
- Keep changes scoped to the requested issue.
- Do not introduce broad refactors, new frameworks, generated files, or formatting churn unless the task requires them.
- Use structured tools and existing project conventions before inventing new patterns.

Editing rules:

- Use ASCII spelling `VIS` in English-language docs and comments.
- Keep existing code identifiers such as `VIS_*` unchanged unless the task explicitly requires an API change.
- Keep `CONTRIBUTING.md` organized into the human and agent sections.
- Keep `.gitignore` patterns scoped and intentional. Prefer anchored project-specific ignores for generated binaries over broad patterns that can hide source files.
- Do not commit generated build outputs such as `vis-jitter/vis-jitter` or root-level test binaries.

Verification rules:

- Run the narrowest meaningful checks for the files changed.
- For documentation-only changes, run text searches that prove the requested spelling or policy is consistent.
- For `vis-jitter` build or source changes, run `make -C vis-jitter` when dependencies are available.
- Run rootless tests directly when possible.
- Do not run sudo-required tests unless the user explicitly approves the elevated workflow.
- Report any skipped check with the reason.

Git and review rules:

- Use the commit message style `<type>(<scope>):<description>`.
- Use the same text for the pull request title if the user asks for a pull request.
- Do not open a pull request unless the user explicitly asks.
- Before finishing, show the changed files and summarize verification.
