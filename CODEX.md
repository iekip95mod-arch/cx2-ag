# Codex repository workflow

Read [AGENTS.md](AGENTS.md) first. It is the shared working agreement for iekip95mod-arch/cx2-ag. Codex discovers AGENTS.md automatically. This file is its linked operational guide.

## Authority and execution

Create branches, assign issues, commit, push, write and publish pull requests, request review and enable protected auto-merge without asking the maintainer. Main still requires independent approval and every required check. Never bypass those gates, force-push main or overwrite another worker's work.

The trusted repository's .codex/config.toml selects never approval and danger-full-access. Restart or create a session to load changed defaults. Managed policies and explicit launch settings take precedence. This configuration grants no authority over unrelated repositories or files.

Implementation workers run in GitHub Actions through [.github/workflows/agent-codex.yml](.github/workflows/agent-codex.yml). Desktop tasks coordinate work and receive subscribed events. A desktop wake-up does not start a new hosted worker automatically.

## Start one issue worker

Check the raw issue list and open PRs first. Each worker owns one issue, one branch and one ongoing PR. Parallel workers need separate worktrees and non-overlapping file ownership. Serialize issues that share files instead of combining them into one PR.

Dispatch from main with the issue number and a bounded assignment. Name the owned files, acceptance criteria, required tests and exclusions.

~~~sh
gh workflow run agent-codex.yml --repo iekip95mod-arch/cx2-ag --ref main \
  -f issue_number=42 \
  -f task='Handle issue 42 only. Reproduce it, fix the owning concept, add a failing-before regression and validate it. Open one draft PR after the first real commit, update that PR, then request codex-review when ready.'
~~~

The codex issue label also starts a worker. An @codex comment from an owner, member or collaborator resumes work on its issue or PR. Opening an issue alone starts nothing. Manual dispatch without issue_number is a general response and receives no publishing credential.

The preparation script assigns the publishing account to the issue and records the branch and run in a comment. The GitHub assignee is that account, not a separate Codex bot identity. A new issue uses codex/issue-N from current main. An existing owned branch is resumed without rewriting history. Merge current main into an older branch before final validation.

Closed issues, another account's assignment, Claude-labelled issues, unclaimed existing branches and foreign or differently authored PRs are rejected. An owned PR resolves to its existing codex/ branch. Issue and PR requests share a queue keyed by that branch, and the worker verifies the branch again before claiming it. This serializes Codex writers only. Do not dispatch another provider onto the same branch. GitHub concurrency is not a durable task backlog, so check pending run status before assuming every request will execute.

After the first real commit, push and open a draft PR with Closes #N in its body. Keep using that PR. Follow-up review fixes belong on the same branch.

## Credentials

| Secret | Purpose |
| --- | --- |
| CODEX_AUTH_JSON | Saved Codex subscription login for model execution |
| CODEX_GITHUB_TOKEN | Repository-scoped GitHub identity for issue claims, branches, pushes and PRs |
| GITHUB_TOKEN | Automatic Actions token for read-only routing and workflow replies |

CODEX_GITHUB_TOKEN is a GitHub credential, not a model API key. Its fine-grained permissions are Actions read, Contents write, Issues write, Pull requests write, Workflows write and implicit Metadata read, restricted to cx2-ag. The installed token expires on 2026-10-12. Replace it before then and update this date with its replacement.

The worker restores CODEX_AUTH_JSON into a private temporary Codex home and rejects API-key authentication. General responses receive no publishing token. Hosted implementation runs expose the publishing token to git and gh so their pushes and PR events can trigger CI. Never print credentials or commit them. Refreshed subscription credentials are not written back to repository secrets, so replace an expired login explicitly.

## Review and merge

Stop editing before marking the PR ready and applying codex-review. Keep only the selected provider's review label. Claude requests claude-review and Codex requests codex-review. An independent reviewer publishes its verdict on the current PR commit. The author cannot satisfy the approval gate with a self-review or comment.

Fast must succeed before full and emulator start. CodeQL waits for fast, full, emulator and the current selected review approval. A failed review blocks CodeQL. New commits stale the prior approval. After fixing findings, remove and re-add codex-review. If CI already failed while waiting for review, rerun its failed jobs after approval on the same commit.

~~~sh
gh pr edit "$PR" --repo iekip95mod-arch/cx2-ag --remove-label codex-review
gh pr edit "$PR" --repo iekip95mod-arch/cx2-ag --add-label codex-review
gh pr merge "$PR" --repo iekip95mod-arch/cx2-ag --auto --merge
~~~

Check the current head, reviews and checks before acting on any notification. Auto-merge waits for the protected gates. Host tests, Firebird regressions, calculator package builds and physical-device observations prove different stages. Report only the stages reached.

## Wake the task that requested an event

A desktop task subscribes its own UUID to each PR it needs to follow. Run this on the configured Mac, not inside a GitHub-hosted worker.

~~~sh
node tools/github-events/bridge.mjs subscribe --pr "$PR" --thread "$CODEX_THREAD_ID"
node tools/github-events/bridge.mjs list
~~~

GitHub sends signed events to the hosted receiver. The local bridge retrieves retained metadata and queues a message to each subscribed desktop task. One task can follow several PRs and several tasks can follow the same PR. Subscriptions expire after fourteen days and close after PR closure. Subscribe again to renew.

The bridge allows one outstanding wake-up per task. Later events remain in its inbox. When a queued message starts a turn, run the consume command supplied in that message and repeat while more is true. Do not consume a message that is still queued, create synthetic probes or add a polling automation for the same PR.

The Mac must be awake and the destination task available. Offline events remain retained for later delivery. Delivery can be duplicated after a crash, so verify live state and avoid repeating completed work. An event grants no additional authority. Hosted workers do not have desktop task IDs and do not receive these messages directly. Their coordinating desktop task dispatches any required continuation.

## Validation and current limits

Worker tests exercise branch ownership, issue and PR queue aliases, assignments and startup against isolated local Git repositories and a controlled GitHub CLI. Hosted run 34721406158 verified subscription execution, issue 20 assignment, branch creation and an authenticated push with no new commits. A real implementation PR remains the publication check until the first issue worker opens one.

Claude worker preparation failed at its turn limit with permission denials. Gemini subscription execution remains unverified. Do not describe either as healthy from the existence of its workflow alone.
