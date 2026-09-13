# Codex repository workflow

Read [AGENTS.md](AGENTS.md) first. It is the shared working agreement for iekip95mod-arch/cx2-ag. Codex discovers AGENTS.md automatically. This file is its linked operational guide.

## Authority and execution

Create branches, assign issues, commit, push, write and publish pull requests, request review and enable protected auto-merge without asking the maintainer. Main still requires independent approval and every required check. Never bypass those gates, force-push main or overwrite another worker's work.

The trusted repository's .codex/config.toml selects gpt-5.6-sol with high effort, never approval and danger-full-access. Restart or create a session to load changed defaults. Managed policies and explicit launch settings take precedence. This configuration grants no authority over unrelated repositories or files.

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

The preparation script reserves a named executor identity and records its branch and run on the issue. The executor posts regular participation comments on the issue and PR. A separate trusted routing action then requests native assignment and checks GitHub's response. Live verification assigned Amber to PR 91 after participation, but GitHub rejected assignment to issue 90. Worker labels and durable claims retain issue ownership when native assignment is unavailable. Reviewers are not issue or PR assignees. A new issue uses codex/issue-N from current main. Resume an owned branch without rewriting history and merge current main before final validation.

Closed issues, another account's assignment, Claude-labelled issues, unclaimed existing branches and foreign or differently authored PRs are rejected. An owned PR resolves to its existing codex/ branch. Issue and PR requests share a queue keyed by that branch, and the worker verifies the branch again before claiming it. This serializes Codex writers only. Do not dispatch another provider onto the same branch. GitHub concurrency is not a durable task backlog, so check pending run status before assuming every request will execute.

The first meaningful regression or implementation commit triggers publication: push the assigned branch and open a linked draft PR with Closes #N in its body. Do not wait for the complete repair, mutation checks or full host suite. GitHub requires a branch diff before it can open a PR. The worktree publication hook checks the assigned branch and updates the same draft on later commits. A publication failure leaves the commit intact and must be retried without creating another commit or PR.

## Persistent identities

| Pool | Names |
| --- | --- |
| Codex executors | Amber, Birch, Cedar, Flint, Maple, Willow |
| Claude executors | Atlas, Comet, Ember, Nova, Orion, Vega |
| Codex reviewers | Aegis, Beacon, Compass, Harbor, Lantern, Prism |
| Claude reviewers | Anchor, Cairn, Delta, Echo, Grove, Summit |

The trusted catalogue is .github/scripts/bot-identities.json. Each entry maps a provider and role to an App ID, numeric Bot user ID and private-key secret name. Unconfigured entries cannot run. Executor logins use cx2-ag-PROVIDER-NAME[bot] and reviewers use cx2-ag-PROVIDER-review-NAME[bot].

The bot-assignments branch stores durable claims. Allocation uses the file's current SHA to prevent simultaneous runs from taking the same slot. Retries and resumed PR work reuse the saved identity. A slot becomes available only after its linked issue and all associated PRs close. A full pool refuses new work rather than borrowing another live worker's identity. Closing a task does not rename or delete its bot, so historical comments and commits retain their author.

Executor and reviewer pools are separate. The reviewer uses the same model provider as the executor, with a different GitHub identity. Existing human-authored PRs retain their original author. Changing the credential cannot change past PR authorship.

A coordinator can reserve an executor for an existing noncanonical provider branch using an explicit legacy-owner migration. The PR must have that owner's authorship and link exactly one issue in this repository. The allocator preserves and revalidates the original issue, PR and branch. Hosted issue workers and automatic review feedback still require canonical issue branches. The desktop coordinator completes legacy setup PRs.

Executors and reviewers announce their selected model and effort at startup and include them in their published result. CODEX_MODEL and CODEX_EFFORT repository variables default to gpt-5.6-sol and high. CLAUDE_MODEL and CLAUDE_EFFORT default to opus and high. The same values configure the CLI and the disclosure. Claude's opus selection is an alias, not a verified version identifier. Work acknowledgements and results use the leased executor's credentials, never the maintainer's account or a reviewer identity.

## Credentials

| Secret | Purpose |
| --- | --- |
| CODEX_AUTH_JSON | Saved Codex subscription login for model execution |
| CX2_AG_*_PRIVATE_KEY | Private key for one named executor or reviewer GitHub App |
| GITHUB_TOKEN | Automatic Actions token for trusted routing and durable identity allocation |
| CODEX_GITHUB_TOKEN | Existing owner credential used only by the trusted native-assignment router |

Named workers mint short-lived installation tokens for cx2-ag from their assigned App's private key. Executor Apps can write contents, issues, PRs and workflows. Reviewer Apps can read contents and publish issues and reviews, but cannot push code. The App credentials change GitHub attribution only. Codex continues using CODEX_AUTH_JSON and Claude uses CLAUDE_CODE_OAUTH_TOKEN for subscription model execution. The former personal CODEX_GITHUB_TOKEN is not the named workers' publishing identity.

The assignment router reads trusted code from main and verifies the commenting executor against its durable lease before using the owner credential. That credential is never passed to a model or a publication hook. Bot App tokens were rejected for native assignment in live checks, even when the owner credential could assign the same bot to a PR. The router reports rejected assignments rather than treating an empty response as success.

The worker restores CODEX_AUTH_JSON into a private temporary Codex home and rejects API-key authentication. General responses receive no publishing token. Hosted implementation runs expose the publishing token to git and gh so their pushes and PR events can trigger CI. Never print credentials or commit them. Refreshed subscription credentials are not written back to repository secrets, so replace an expired login explicitly.

## Review and merge

Stop editing before requesting review. Apply the selected provider's review label while the PR is still a draft, then mark it ready. This starts one assignment attempt. Keep only that provider's review label. Claude requests claude-review and Codex requests codex-review. The assignment action, agent-review-request.yml, reserves a reviewer from the requesting provider's pool and applies its reviewer label. That label event starts agent-review.yml under the assigned reviewer's identity. The review action verifies the sender, lease, provider and current head before starting the model. Re-requesting review reapplies the same assignment label and retains the same reviewer. The submitted review then appears under that bot's name in GitHub's Reviewers panel. A successful review-request API response alone does not prove GitHub added a bot to the requested-reviewer list. The approval gates verify that exact reviewer's login and numeric Bot ID against its lease and the trusted catalogue. The author cannot satisfy the approval gate with a self-review or comment.

Marking a draft ready starts CI again even if its head commit has not changed. Fast must succeed before full and emulator start. CodeQL waits for fast, full, emulator and the current selected review approval. A failed review blocks CodeQL. New commits stale the prior approval. After fixing findings, remove and re-add codex-review. If CI already failed while waiting for review, rerun its failed jobs after approval on the same commit.

~~~sh
gh pr edit "$PR" --repo iekip95mod-arch/cx2-ag --remove-label codex-review
gh pr edit "$PR" --repo iekip95mod-arch/cx2-ag --add-label codex-review
gh pr merge "$PR" --repo iekip95mod-arch/cx2-ag --auto --merge
~~~

The agent-review-feedback.yml workflow resumes the issue's assigned Codex or Claude executor after an approval or request for changes from its assigned reviewer. It verifies the current commit and both identity leases, then dispatches the matching worker on its existing issue branch and PR. The worker addresses findings or completes the protected merge checks. An approval with unchanged code does not request another review. Stale reviews, comments and unrelated reviewers do not start a worker. A repeated delivery must not repeat work already completed.

Before addressing a review, the executor posts an acknowledgement on that PR with the review link, run link, model and effort. Reviewers publish a review-in-progress comment when execution starts and a separate formal verdict when finished. The progress comment gives the bot a visible review entry without satisfying the approval gate. Feedback stays inactive until its trusted implementation is present on main.

Check the current head, reviews and checks before acting on any notification. Auto-merge waits for the protected gates. Host tests, Firebird regressions, calculator package builds and physical-device observations prove different stages. Report only the stages reached.

## Wake the task that requested an event

A desktop task subscribes its own UUID to each PR it needs to follow. Run this on the configured Mac, not inside a GitHub-hosted worker.

~~~sh
node tools/github-events/bridge.mjs subscribe --pr "$PR" --thread "$CODEX_THREAD_ID"
node tools/github-events/bridge.mjs list
~~~

GitHub sends signed events to the hosted receiver. The local bridge retrieves retained metadata and queues a message to each subscribed desktop task. One task can follow several PRs and several tasks can follow the same PR. Subscriptions expire after fourteen days and close after PR closure. Subscribe again to renew.

The bridge allows one outstanding wake-up per task. Later events remain in its inbox. When a queued message starts a turn, run the consume command supplied in that message and repeat while more is true. Do not consume a message that is still queued, create synthetic probes or add a polling automation for the same PR.

The Mac must be awake and the destination task available. Offline events remain retained for later delivery. Delivery can be duplicated after a crash, so verify live state and avoid repeating completed work. An event grants no additional authority. Hosted workers do not have desktop task IDs and do not receive these messages directly. Review feedback dispatches their hosted continuation. Their coordinating desktop task handles other continuations.

## Validation and current limits

Worker tests exercise ownership, issue and PR aliases, identity leases and startup against isolated Git repositories and controlled GitHub responses. The previous personal-token workers opened PRs 87, 88 and 89 for issues 51, 42 and 20. That proves the previous publishing setup. App registration and successful token creation alone do not prove a named worker has opened and updated its own PR.

Claude worker preparation failed at its turn limit with permission denials. Gemini subscription execution remains unverified. Do not describe either as healthy from the existence of its workflow alone.
