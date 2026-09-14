# Gemini project context

@./AGENTS.md

Read [AGENTS.md](AGENTS.md) for the shared workspace instructions if imports are not expanded by the client. Read the relevant sections of [the codebase map](docs/codebase-map.md) for detailed architecture and [the build guide](nps/README.md#build-and-test) for specialized CMake commands.

The hosted Gemini pool has twelve executors and twelve separate reviewers. [CODEX.md](CODEX.md) lists all names and the shared issue, PR, review and recovery contracts. Gemini uses agent-gemini.yml, the gemini issue label and gemini/issue-N branches. Apply gemini-review when ready for review. Both roles retain their identity until the issue and associated PRs close.

Start one issue worker from main:

~~~sh
gh workflow run agent-gemini.yml --repo iekip95mod-arch/cx2-ag --ref main \
  -f issue_number=42 \
  -f task='Handle issue 42 only. Reproduce and fix the issue, publish a linked draft after the first meaningful commit, validate the repair and request gemini-review.'
~~~

An @gemini comment from an owner, member or collaborator resumes its issue or owned PR. Repository dispatch uses gemini-task with client_payload.issue_number and client_payload.task. A manual task without an issue is a reply with no commands, file edits or publishing token.

Model execution uses ANTIGRAVITY_OAUTH_CREDS from the saved Antigravity subscription login. GEMINI_MODEL defaults to gemini-3.8-flash-high and GEMINI_EFFORT to high. The GitHub App token supplies authorship separately. Keep credentials out of comments and commits. Jobs do not save refreshed subscription tokens back to secrets.

Executors run in the prepared checkout with a publishing token. Reviewers get the entire cumulative PR and prior discussion, run checks and return structured findings. The shared publisher submits native inline comments and the verdict under the assigned reviewer. Reviewer model execution receives no GitHub publishing token. Requests for clarification use /ask-reviewer in an existing native thread. Answers, review verdicts and applicable CI failures resume the leased Gemini executor on its existing branch.

Gemini jobs use Ubuntu 24.04 x64 for the pinned CLI. Its native Linux command sandbox segfaults on the hosted runner, so those jobs use the disposable GitHub VM as their execution boundary. They retain file-tool permissions but shell commands can access the runner filesystem. Other environments keep native sandboxing enabled. The manual verify_runtime input checks subscription file edits, command execution and review output without claiming an issue. Model timeouts include setup time and reserve five minutes for publication. Preserve partial work in the same draft before that deadline. Calculator execution is emulator only.
