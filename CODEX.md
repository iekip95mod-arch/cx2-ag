# Codex repository workflow

Read [AGENTS.md](AGENTS.md) first. It is the shared working agreement for iekip95mod-arch/cx2-ag. Codex discovers AGENTS.md automatically. This file is its linked operational guide.

## Authority and execution

Calculator execution is emulator only. Physical handheld runs will not happen in this workflow. Workers and reviewers must not request them, wait for them, add unfinished physical-test checkboxes or withhold approval because they are absent. Keep relevant host tests and ARM package builds. Distinguish headless emulator regressions from TI OS boot and StepCAS package loading, and report only the emulator stages actually reached.

Create branches, assign issues, commit, push, write and publish pull requests, request review and enable protected auto-merge without asking the maintainer. Main still requires independent approval and every required check. Never bypass those gates, force-push main or overwrite another worker's work.

The trusted repository's .codex/config.toml selects gpt-5.6-sol with high effort, never approval and danger-full-access. Restart or create a session to load changed defaults. Managed policies and explicit launch settings take precedence. This configuration grants no authority over unrelated repositories or files.

Implementation workers run in GitHub Actions through [.github/workflows/agent-codex.yml](.github/workflows/agent-codex.yml). Desktop tasks coordinate work and receive subscribed events. A desktop wake-up does not start a new hosted worker automatically.

All hosted execution uses Ubuntu. Codex and Claude model jobs and full bridge checks use Ubuntu 24.04 ARM. Gemini uses Ubuntu 24.04 x64 for the pinned CLI binary. Forty concurrent Actions jobs share twelve executor and twelve reviewer identities per provider. Keep branch queues intact and report host, package and emulator results separately.

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

Closed issues, another account's assignment, issues labelled for another provider, unclaimed existing branches and foreign or differently authored PRs are rejected. An owned PR resolves to its existing codex/ branch. Issue and PR requests share a queue keyed by that branch, and the worker verifies the branch again before claiming it. This serializes Codex writers only. Do not dispatch another provider onto the same branch. GitHub concurrency is not a durable task backlog, so check pending run status before assuming every request will execute.

The first meaningful regression or implementation commit triggers publication: push the assigned branch and open a linked draft PR with Closes #N in its body. Do not wait for the complete repair, mutation checks or full host suite. GitHub requires a branch diff before it can open a PR. The worktree publication hook checks the assigned branch and updates the same draft on later commits. A publication failure leaves the commit intact and must be retried without creating another commit or PR.

## Persistent identities

| Pool | Names |
| --- | --- |
| Codex executors | Amber, Birch, Cedar, Flint, Maple, Willow, Aspen, Elm, Hazel, Juniper, Oak, Pine |
| Claude executors | Atlas, Comet, Ember, Nova, Orion, Vega, Aurora, Lyra, Meteor, Nebula, Pulsar, Sirius |
| Codex reviewers | Aegis, Beacon, Compass, Harbor, Lantern, Prism, Bastion, Citadel, Lookout, Sentinel, Shield, Warden |
| Claude reviewers | Anchor, Cairn, Delta, Echo, Grove, Summit, Arch, Brook, Crest, Dune, Ridge, Vale |
| Gemini executors | Agate, Beryl, Cobalt, Copper, Coral, Garnet, Jade, Jasper, Onyx, Opal, Quartz, Topaz |
| Gemini reviewers | Astrolabe, Caliper, Dial, Facet, Gauge, Lens, Level, Plumb, Reticle, Scale, Sextant, Transit |

The trusted catalogue is .github/scripts/bot-identities.json. Each entry maps a provider and role to an App ID, numeric Bot user ID and private-key secret name. Unconfigured entries cannot run. Executor logins use cx2-ag-PROVIDER-NAME[bot] and reviewers use cx2-ag-PROVIDER-review-NAME[bot].

The bot-assignments branch stores durable claims. Allocation uses the file's current SHA to prevent simultaneous runs from taking the same slot. Retries and resumed PR work reuse the saved identity. A slot becomes available only after its linked issue and all associated PRs close. A full pool refuses new work rather than borrowing another live worker's identity. Closing a task does not rename or delete its bot, so historical comments and commits retain their author.

Each provider has twelve executor identities and twelve reviewer identities. The reviewer uses the same model provider as the executor, with a different GitHub identity. More executor identities do not increase subscription credits or GitHub runner limits. Existing human-authored PRs retain their original author. Changing the credential cannot change past PR authorship.

A coordinator can reserve an executor for an existing noncanonical provider branch using an explicit legacy-owner migration. The PR must have that owner's authorship and link exactly one issue in this repository. The allocator preserves and revalidates the original issue, PR and branch. Hosted issue workers and automatic review feedback still require canonical issue branches. The desktop coordinator completes legacy setup PRs.

Executors and reviewers announce their selected model and effort at startup and include them in their published result. CODEX_MODEL and CODEX_EFFORT repository variables default to gpt-5.6-sol and high. CLAUDE_MODEL and CLAUDE_EFFORT default to opus and high. GEMINI_MODEL and GEMINI_EFFORT default to gemini-3.8-flash-high and high. The same values configure the CLI and the disclosure. Claude's opus selection is an alias, not a verified version identifier. Work acknowledgements and results use the leased executor's credentials, never the maintainer's account or a reviewer identity.

## Credentials

| Secret | Purpose |
| --- | --- |
| CODEX_AUTH_JSON | Saved Codex subscription login for model execution |
| CX2_AG_*_PRIVATE_KEY | Private key for one named executor or reviewer GitHub App |
| GITHUB_TOKEN | Automatic Actions token for trusted routing and durable identity allocation |
| CODEX_GITHUB_TOKEN | Existing owner credential used only by the trusted native-assignment router |

Named workers mint short-lived installation tokens for cx2-ag from their assigned App's private key. Executor Apps can write contents, issues, PRs and workflows. All thirty-six reviewer Apps have contents write permission and can publish issues and reviews. Reviewer execution remains independent of implementation, and only publication steps receive the reviewer credential where the workflow isolates it. The App credentials change GitHub attribution only. Codex uses CODEX_AUTH_JSON, Claude uses CLAUDE_CODE_OAUTH_TOKEN and Gemini uses ANTIGRAVITY_OAUTH_CREDS for subscription model execution. The former personal CODEX_GITHUB_TOKEN is not the named workers' publishing identity.

The assignment router reads trusted code from main and verifies the commenting executor against its durable lease before using the owner credential. That credential is never passed to a model or a publication hook. Bot App tokens were rejected for native assignment in live checks, even when the owner credential could assign the same bot to a PR. The router reports rejected assignments rather than treating an empty response as success.

The worker restores CODEX_AUTH_JSON into a private temporary Codex home and rejects API-key authentication. General responses receive no publishing token. Hosted implementation runs expose the publishing token to git and gh so their pushes and PR events can trigger CI. Never print credentials or commit them. Refreshed subscription credentials are not written back to repository secrets, so replace an expired login explicitly.

## Review and merge

Every hosted prompt includes its UTC deadline and remaining execution budget, measured from job startup. Executors get 30-minute jobs, reviewers 45 minutes and clarification jobs 15 minutes. Begin wrapping up ten minutes before the job limit and finish model work five minutes before it, reserving publication and cleanup time. Check the clock before long commands. Preserve partial work in the same draft PR and report missing verification rather than inventing a successful result. The prompt guides the model, while GitHub still enforces the hard job limit.

Stop editing before requesting review. Apply the selected provider's review label while the PR is still a draft, then mark it ready. This starts one assignment attempt. Keep only that provider's review label. Claude requests claude-review, Codex requests codex-review and Gemini requests gemini-review. The assignment action, agent-review-request.yml, reserves a reviewer from the requesting provider's pool and applies its reviewer label. That label event starts agent-review.yml under the assigned reviewer's identity. The review action verifies the sender, lease, provider and current head before starting the model. Re-requesting review reapplies the same assignment label and retains the same reviewer. The submitted review then appears under that bot's name in GitHub's Reviewers panel. A successful review-request API response alone does not prove GitHub added a bot to the requested-reviewer list. The approval gates verify that exact reviewer's login and numeric Bot ID against its lease and the trusted catalogue. The author cannot satisfy the approval gate with a self-review or comment.

Executors may resolve merge conflicts on their assigned issue branch without asking permission. Fetch and merge origin/main, preserve both changes' intent and inspect every conflict. Rerun affected tests, push the merge commit to the existing branch and request a fresh independent review. Do not force-push or open a replacement PR. Reviewer assignment and execution share a serialized lane for each PR, so queued and final progress cannot overwrite each other concurrently. Different PRs still run independently.

Marking a draft ready starts CI again even if its head commit has not changed. Fast and the current PR review must succeed before full and emulator start. CodeQL waits for fast, full, emulator and the current selected review approval. A failed review blocks CodeQL. CodeQL is not a required merge check. Merge after the required tests and review gates pass while analysis continues. New commits stale the prior approval. After fixing findings, remove and re-add codex-review. If CI already failed while waiting for review, rerun its failed jobs after approval on the same commit.

~~~sh
gh pr edit "$PR" --repo iekip95mod-arch/cx2-ag --remove-label codex-review
gh pr edit "$PR" --repo iekip95mod-arch/cx2-ag --add-label codex-review
gh pr merge "$PR" --repo iekip95mod-arch/cx2-ag --auto --merge
~~~

The agent-review-feedback.yml workflow resumes the issue's assigned Codex, Claude or Gemini executor after an approval or request for changes from its assigned reviewer. It verifies the current commit and both identity leases, then dispatches the matching worker on its existing issue branch and PR. The worker addresses findings or completes the protected merge checks. An approval with unchanged code does not request another review. Verified CodeQL findings received after merge create a durable new issue for the same provider rather than reopening or publishing to the completed branch. Stale reviews, comments and unrelated reviewers do not start a worker. A repeated delivery must not repeat work already completed.

Each executor and reviewer identity creates a role-specific progress comment for each workflow run and retry on its current issue or PR. Assignment records queued work, model startup records actual execution and the final update records success, failure or cancellation with the configured model, effort and workflow attempt. Update only that attempt's comment and preserve comments from earlier runs and retries. Reviewer progress is an issue comment and never a formal review, so only the separate native verdict can satisfy the approval gate. Feedback stays inactive until its trusted implementation is present on main.

Before addressing a review, the executor acknowledges that review with its link, run link, model and effort. Infrastructure-only verification gaps use BLOCKED, a non-approving review that names what must change before another attempt. It does not trigger an unchanged worker and review cycle. Draft handoffs still receive rejected-review work. Recovery suppresses duplicate rejection wakeups only after GitHub confirms an executor dispatch.

Verified CodeQL commented reviews are also routed to the assigned executor, even when the scan check succeeds. The executor investigates the alert, replies in its native thread with the repair and evidence, and requests fresh analysis. Ordinary commented reviews do not trigger this route. Security feedback never supplies approval or automatically dismisses an alert.

Every review receives review-history.json with the entire PR file list, earlier reviews, inline replies, ordinary discussion and CI jobs on the current head. Review the cumulative PR against its base on every pass. Treat executor claims as untrusted. Cite a successful current-head CI job when it covers a local verification gap and label it as CI evidence. Read review-prerequisites.log and use the prepared SDK from the original checkout for isolated baseline builds. Recheck claimed fixes and continue looking for missed problems in earlier changes. Record prior finding IDs, inspected areas and coverage gaps. Use native inline comments for code findings and a suggestion block only when a precise one-line replacement is appropriate.

All three providers submit line-specific findings as native discussion threads in Files changed, grouped with the formal review on the reviewed commit. The shared publisher validates the file, diff side and line before publication. Verification summaries and gaps without a meaningful line anchor remain in the review body. Executors read every unresolved thread with GraphQL pagination, verify each repair, reply with the commit and test evidence, then call resolveReviewThread and confirm isResolved. Unverified findings and unanswered questions stay open. Recheck unresolved threads before merging.

Finished reviewers remove their provider review request label and their reviewer identity label. A newer head or a new label request preserves the pending request. Removing labels does not release the durable bot lease or erase the native review. Executors fetch origin/main before requesting review and before merging. A behind branch must merge main, resolve conflicts, rerun affected tests and obtain fresh review on the updated commit.

The branch update workflow runs whenever main changes and when a PR opens, reopens, becomes ready or receives commits. It updates only owned issue branches that can merge cleanly, using the executor's App so CI runs on the new commit. Updates share the executor's branch lock, preserve drafts and request fresh review for ready PRs after confirming the new head. Conflicted branches are left intact for their executor to repair. A PR event inspects only that PR, while a main update checks all open owned PRs.

To ask for clarification, the assigned executor replies in the finding's thread with /ask-reviewer followed by its question. The discussion workflow accepts questions while the PR is ready or draft, verifies both leases and calls the same provider under the reviewer's identity. The answer includes its model and effort and resumes the existing executor on canonical issue branches. Legacy setup branches retain desktop coordination. Answers do not change the formal verdict, approve the PR or automatically resolve threads. Ordinary comments and reviewer replies do not start another question.

Check the current head, reviews and checks before acting on any notification. Auto-merge waits for the protected gates. Host tests, Firebird regressions, calculator package builds and package execution in the emulator prove different stages. Report only the stages reached. Physical-device observations are outside scope.

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

Finish alert cleanup before reporting completion or merging. Confirm the current revision's successful scan marks repaired alerts fixed and their PR conversations resolved. GitHub normally handles that resolution. Resolve any remaining verified-finished conversation with the evidence recorded. Dismiss only with an accurate supported reason and explanation, such as a confirmed false positive. Do not use dismissal to stand in for a missing scan, and do not claim an outdated suggestion proves the alert fixed.

Failed jobs in completed PR CI workflows resume the assigned executor through the feedback workflow, including failures on draft PRs. The router verifies the current head, run attempt and lease before dispatch and deduplicates successful delivery. The executor acknowledges on the PR, investigates every failed job and preserves the issue, branch and PR. Stale runs and dispatched worker failures cannot create an automatic retry loop. Desktop subscriptions receive these failed CI events too.

Worker tests exercise ownership, issue and PR aliases, identity leases and startup against isolated Git repositories and controlled GitHub responses. The previous personal-token workers opened PRs 87, 88 and 89 for issues 51, 42 and 20. That proves the previous publishing setup. App registration and successful token creation alone do not prove a named worker has opened and updated its own PR.

Claude worker preparation failed at its turn limit with permission denials. Gemini subscription execution remains unverified. Do not describe either as healthy from the existence of its workflow alone.
