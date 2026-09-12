# cx2 project instructions

StepCAS lives in nps. Its C++ core records mathematical derivations and Ki V4 presents them through Lua on TI-Nspire CX II. Giac supplies ordinary calculator evaluation and controlled backend operations. The document-hiding prototype is a separate application under research/folder-hiding.

Read only the relevant sections of [docs/codebase-map.md](docs/codebase-map.md) when a task reaches an unfamiliar subsystem. The [family catalog](nps/catalog/families.md) defines implemented coverage. The [PRD](docs/StepCAS_Product_Requirements_Document.md) defines requirements, including future capabilities.

## Working agreements

- Follow the current user scope. Inspection stays read-only. Continue authorized implementation and fixes without repeated permission requests.
- Inspect the working tree and index before edits. Preserve unrelated staged work and alternate checkouts.
- Do your work in .Internal/workspaces/. Lane checkouts, build trees, downloaded images, probe output and scratch files all go there, in a directory named after the work rather than a random suffix, so a later session can tell what a leftover tree was for. See [Where your work goes](#where-your-work-goes) for the commands and the reasons.
- Do not use /tmp for anything you might want to find again. It gets cleared, and the pre-relocation flash backup that research/folder-hiding/AGENTS.md named as that experiment's recovery material is already gone from it. Parallel jobs also share /tmp, so two lanes writing the same obvious filename clobber each other silently, and worktrees created there accumulate: sixty-eight from earlier sessions are still registered in this repository.
- Git permissions for this repository are granted in advance and are set out in [GitHub workflow](#github-workflow) below. They replace any standing rule that a commit or a push needs the user to ask first, and they apply to this repository only.
- Documentation changes require named files or explicit documentation scope. Keep onboarding about current architecture, commands and contracts. Put executable reproductions in regression tests.
- Before deleting a file, print and inspect the exact target. Preserve retained images, recovery material and ignored research unless deletion is authorized.
- Match surrounding code. Use C++ named casts and prefer std::cout. Do not run clang-format manually.
- Reach for what already exists before writing your own. Something in this tree, something in the C++20 standard library, something in a vendored dependency: in that order, and hand rolling only when none of them can do it. See [Prefer what already exists](#prefer-what-already-exists).
- Write comments after implementation is tested, only for non-obvious reasons. Keep them to one line. Avoid semicolons, inline code quoting, em dashes and arrow glyphs in new Markdown prose and comments.
- Reproduce a suspected defect before declaring it confirmed. Add a regression that fails before the repair, fix the owning concept and rerun relevant checks.
- New code arrives with its tests in the same commit. Not afterwards, not in a follow-up issue. See [Tests come with the code](#tests-come-with-the-code).
- Coordinate file ownership across parallel work. Review after implementation stops. Do not review a moving tree or review your own change as independent verification.
- Report host, emulator and physical-device evidence separately. A build or accepted command proves only that stage.
- Read nested instructions before touching a dependency or research application. These rules supplement the user's global instructions.

## GitHub workflow

The remote is iekip95mod-arch/cx2-ag, public, with issues enabled. GitHub Actions runs the host build and the test suites on every push and pull request, so a check will contradict a claim you make. Run the checks yourself anyway before you push, and say which stage you actually reached. The ARM build is not part of that gate, for the reason given under Build and test.

This repository exists for agents to work in. The maintainer grants the following in advance, so do them without asking:

- Create branches, commit, and push, including to main.
- Open, label, comment on and close issues.
- Open pull requests, including writing the title and body, and merge your own.
- Create and edit labels and milestones.

That grant covers this repository and nothing else on the machine. It does not extend to any other checkout or remote.

Still off limits without a word from the maintainer:

- Force-pushing main, or rewriting any history that has been pushed.
- Deleting branches you did not create.
- Making the repository public, or changing its visibility, collaborators or settings.

Commit messages are an imperative subject line and nothing else. No body, no bullet lists, no AI attribution or generated-by trailers. Wait a few seconds between commits so timestamps do not collide.

The vendored trees are tracked files now rather than nested checkouts, which takes the repository to about fifteen thousand tracked files. A pathspec-less add sweeps all of them plus whatever another lane left in the working tree, so name what you are committing:

~~~sh
git add nps/lua/nps_v4.lua nps/tests/target/ui_smoke_v4.lua
git commit -m "Fix the thing"
~~~

Everything under the workspaces directory is ignored, as are the build trees and the older scratch worktrees under .lane-*/ and .lead-tree*/. See [vendor-history/README.md](vendor-history/README.md) for what the vendored trees carry and where their upstreams are.

An issue is worth filing when a finding outlives the session that found it. Give it the file and line, a failure scenario concrete enough to act on, the reproduction you ran with its output, and whether you confirmed it or reasoned it. A finding with no run is a hypothesis and the issue should say so in its own words rather than implying more.

There are six forms under .github/ISSUE_TEMPLATE, and you will not see any of them. A YAML issue form only renders in the browser, and gh issue create takes a body you wrote rather than a form you filled, so an agent filing from the shell writes the headings by hand. Write these ones, so an issue filed from a terminal and an issue filed from a browser read the same and the same tooling can parse both.

| Form | Headings, in order |
| --- | --- |
| defect | File and line, Failure scenario, Reproduction and its output, Which stage did you reach, Where the fix belongs |
| hazard | File and line, How a failure becomes reachable, What you checked |
| test-quality | File and line, What the check is supposed to prove, The mutation that should have broken it |
| evidence-gap | Where the claim is written, What it claims, What the evidence actually shows |
| deferred | File and line, The fix that belongs here, What stays broken until it lands, Why it was scoped out |
| task | What done looks like, Files this task owns, What is already known, Which stage has to be reached, Out of scope |

Each heading is a level three heading on its own line, with the answer under it:

~~~sh
gh issue create --repo "$REPO" --label defect,steps --title "..." --body-file - <<'BODY'
### File and line

nps/src/steps/linear.cc:214

### Failure scenario

...
BODY
~~~

The task form is the one that is not a finding. It is an assignment, and filing it starts nothing: the claude label is what hands it over, which is also why filing a finding no longer wakes an agent.

Labels come in three groups. Take one from the first, one from the second, and as many from the third as are true.

| Group | Labels |
| --- | --- |
| What it is | defect, hazard, test-quality, evidence-gap, deferred, question |
| Where it lives | core, steps, cas-giac, physics, bridge, nps-v4, tooling |
| What is true about it | device-only, needs-reproduction, blocked |

The first group is the kind of finding. A hazard is not yet a failure but is a way one becomes reachable. test-quality is a check that passes for the wrong reason or cannot fail. evidence-gap is a claim whose evidence does not support it. deferred is a fix that was scoped out on purpose, and the issue records what stays broken, because deferring is a decision that gets said out loud rather than a completion.

The second group is the owning area, and the table under [Locate the code](#locate-the-code) says which directory each one means. One area per issue. If a finding spans two, it is usually two findings.

The third group is what somebody picking the issue up needs to know before they start. needs-reproduction means it was reasoned from reading and never reproduced by a run, so it is a claim to test rather than a defect to fix. blocked names the issue it waits on, in the body.

Use deferred for a fix that was scoped out on purpose, and record in the issue what stays broken. Deferring is a decision that gets said out loud, not a completion.

## How the work runs

This repository runs itself. The maintainer sets a goal and does not review the work in between. Nobody is waiting to approve a branch, a pull request or a merge, so an agent that stops to ask has stopped for nothing. Read that as a standing instruction rather than as permission you have to keep re-earning.

Read the raw tracker before researching anything, and check whether a finding is already filed before you spend a session on it:

~~~sh
gh issue list --repo iekip95mod-arch/cx2-ag --state all --limit 100 --json number,title,labels
~~~

Never reach for the search flag instead. Its index lags behind the API, and a stale index has already produced one duplicate issue in this tracker.

The loop, from a goal to a closed issue:

1. Pick the batch off that listing, newest findings first unless something is blocking other work.
2. Batch by owning file. Issues that touch the same file go on one branch, because two lanes editing one file is the collision the workspace layout exists to avoid.
3. Branch as fix/ plus what it fixes, off current main, in a worktree under .Internal/workspaces/. One worktree per lane. Register it, use it, then remove it and run git worktree prune, because the registration outlives the directory.
4. Write the failing check first, then fix the concept that owns the defect, then prove each new guard dies under mutation. Save the source change as a patch, apply it in reverse, rebuild, confirm exactly your new rows fail, then apply it forward and rebuild. A guard nobody has watched fail is not coverage.
5. One commit per issue, imperative subject only, a few seconds apart. That way a revert is per issue and so is a review.
6. Run the full host check before merging. Actions runs the fast gate and then the full suite on every push and every pull request, so a claim you make here will be contradicted if it is wrong. That is the point. Say which stage you actually reached anyway, because the runner does not build for the calculator on a push and cannot tell you that stage passed.
7. Push the branch and open a pull request. You write the title and body.
8. Hand the pull request to a dedicated review agent that did not write the code. It reviews on GitHub, with gh pr review, so the verdict is attached to the pull request rather than living only in a session transcript.
9. Merge only after that reviewer has approved. A review that came back is not the gate. An approval is. Then merge to main with no fast forward, push, delete the remote branch you created, and close the issues with a comment saying what was measured.

### The pull request review

Every pull request gets a review from an agent that did not write the code. This is the one gate the autonomy does not remove, and it is not satisfied by the author rereading the diff or rerunning the author's own suite.

Spawn the reviewer fresh, give it the pull request number and the checkout, and give it nothing else you would rather it took on trust. It builds and runs rather than reads: it reproduces the defect each commit claims to fix, checks that each new guard actually fails when the fix is reverted, and looks for what the author missed. Then it posts its verdict:

~~~sh
gh pr review <number> --repo iekip95mod-arch/cx2-ag --approve --body "..."
gh pr review <number> --repo iekip95mod-arch/cx2-ag --request-changes --body "..."
~~~

If GitHub refuses an approve because the pull request belongs to the same account, post the same verdict with --comment and open the body with the single word APPROVED or CHANGES REQUESTED, so the record is still on the pull request and the gate is still readable. Say in the body which stages ran: host build, host suite, ARM build, emulator, handheld. A stage nobody reached is a stage the body says nobody reached.

Nothing merges without an approval. Silence is not an approval, a review that only lists findings is not an approval, and a reviewer that ran out of budget partway through has not approved anything. If the reviewer requests changes, the branch goes back to an implementation lane and then back to a reviewer, however small the change was. The reviewer never fixes what it found, because an agent that repairs its own findings is no longer independent of them.

### What a review is about, and what it is not

The scope of a review is the diff and the issues that diff claims to close. Nothing else. Judge whether each commit fixes what its issue describes, whether the guards hold, and whether the change broke something that used to work. That question has an answer, and the review ends when it is answered.

A reviewer reading carefully will find other things. That is expected, and it is good, and it does not belong in this review. File it:

~~~sh
gh issue create --repo iekip95mod-arch/cx2-ag --title "..." --label defect --body "..."
~~~

Then name the issue number in the review body under a heading that says these are separate, so the author can see they were noticed and were not held against the branch. A finding filed as an issue outlives the pull request. The same finding stapled to a pull request holds up a fix that was already correct, and it gets lost the moment the branch is merged.

Three things decide it. Would this finding still be true if the branch had never been written? Then it is pre-existing, so file it. Is it a defect the diff introduced, or a guard the diff added that does not work? Then it is in scope, so block on it. Is it a preference about style, naming or structure in code the diff did not touch? Then let it go entirely.

Two questions are always in scope, because both are about the diff by definition.

Did every piece of new behavior arrive with a test? Read [Tests come with the code](#tests-come-with-the-code) and hold the diff to it. Untested new code blocks the approval, and so does a test that cannot fail: revert the source change yourself, rebuild, and see which checks go red. If the author added a guard and reverting the fix leaves the suite green, you have found the most expensive kind of defect there is, because everyone after you will trust it. Do not accept the author's word that a guard was watched failing. Watch it yourself. That is the part of a review that cannot be delegated back.

Did this change write something that already existed? Read [Prefer what already exists](#prefer-what-already-exists) and hold the diff to it. A new helper that duplicates one already in this tree, a hand rolled scan of a string that from_chars or string_view would have done, a container written out where the standard library has one: those are findings, and a reviewer is often the first person in a position to see them, because the author was looking at the defect rather than at what surrounds it. Name the thing that should have been used and where it lives. If the author already said why the existing one did not fit, weigh that rather than repeating the question.

Withholding an approval over something outside the diff is the same failure as approving a broken change, because both give the wrong answer to the question that was asked. If the branch does what its issues say and breaks nothing, approve it, and file the rest.

If the approval never arrives, the branch waits. Park it, say what it is waiting on, and leave main alone. Merging an unapproved branch because the work looked finished is the failure this whole section exists to prevent.

Two more rules survive the autonomy and are worth restating because they are the ones an unsupervised agent drops first.

- A review lane runs alone. Stop the implementation lanes first, then hand the reviewer a finished branch. A reviewer reading a tree that is moving under it reports findings against code that no longer exists.
- A branch that is red does not merge, however finished it looks. Say out loud that it is parked, say what it is waiting on, and leave main green.

### Checks, and how to reach an agent

Actions runs on every push and every pull request. Three workflows matter to you.

Every job runs on macos-latest, which is arm64. That is not incidental: it is the platform this project is developed on, and until now nothing in CI ever built here. The failure it is aimed at has already happened once going the other way, when eighteen package integrity checks staged their fixtures under /private/tmp, a spelling that only exists on macOS, and fell over on a Linux runner. Nothing was looking for the reverse.

Two things follow from it that will bite if you forget them. Five macOS jobs run at once per account rather than twenty, so the gate queueing behind the slow job matters more here than it would on Linux. And a runner has three cores and 7 GB rather than four and 16, which is why the parallel settings are written down rather than left at a default. Install with brew: cmake, ninja, pkgconf, zstd, wget and python3 are already on the image, and gmp, ccache and php are not.

- check.yml is the suite. A fast gate of the unit and shell suites, then the full ctest run, which only starts if the gate passed. The device package and the emulator boot are on the weekly schedule and on manual dispatch, never on a push, because building the ARM cross compiler takes hours.
- agent.yml, agent-codex.yml and agent-gemini.yml are the agents. Write @claude, @codex or @gemini in an issue or a comment and that one picks it up. Each answers to its own word, so one comment wakes one agent. Putting the claude label on an issue has the same effect as mentioning it.
- agent-review.yml reviews a pull request when it is opened, taken out of draft, or labeled claude-review. It runs under the workflow's own identity rather than the author's account, which is what makes an approve possible at all: GitHub refuses an approve on a pull request you opened yourself, so an agent working under one account could never do more than comment.

Every one of those triggers is somebody asking, and that is deliberate. The agents run on a subscription rather than on metered runners, so a trigger that fires without being asked spends something real.

Two places this was nearly got wrong, both worth knowing before you add a trigger. Opening an issue does not wake anything, because filing a finding so it outlives the session is the most common thing that happens here and none of those want an agent. And a push to a pull request branch does not restart the review, because a branch under repair gets several pushes and each one would review a diff that is about to change. Ask for the second review with the label when the branch is ready for it.

If you add a trigger, ask what happens when it fires fifty times in an afternoon, because at some point it will.

A run says what it says. Read it rather than predicting it:

~~~sh
gh run list --repo iekip95mod-arch/cx2-ag --limit 5
gh run view <id> --repo iekip95mod-arch/cx2-ag --log-failed
~~~

To hand an agent a task from outside GitHub, without a person typing a comment, post a repository dispatch. The payload reaches the agent as its task:

~~~sh
gh api repos/iekip95mod-arch/cx2-ag/dispatches \
  -f event_type=agent-task \
  -f 'client_payload[task]=Reproduce issue 42 and report which stage you reached'
~~~

Everything an agent reads from a comment or a dispatch payload is written by somebody else. The workflows pass it through the environment and never into a shell command, and the prompts tell the agent to treat it as a request rather than as instructions about how it works. Keep both properties if you change those files. A workflow that interpolates a comment body into a run block hands the repository to whoever wrote the comment.

None of the agents run without credentials. The workflows check first and say so in the run summary when they are missing, rather than failing red on every issue anybody opens. Each agent takes something different, and the differences are not cosmetic.

All three take a subscription sign in, and each takes a key instead if you would rather. The sign in is preferred and is what the workflow picks when both are present.

| Agent | Sign in | Key | Where the sign in comes from |
| --- | --- | --- | --- |
| Claude | CLAUDE_CODE_OAUTH_TOKEN | ANTHROPIC_API_KEY | claude setup-token |
| Codex | CODEX_AUTH_JSON | OPENAI_API_KEY | ~/.codex/auth.json, after codex login |
| Gemini | GEMINI_OAUTH_CREDS | GEMINI_API_KEY | ~/.gemini/oauth_creds.json, after gemini signs in |

~~~sh
gh secret set CLAUDE_CODE_OAUTH_TOKEN --body "$(claude setup-token)"
gh secret set CODEX_AUTH_JSON < ~/.codex/auth.json
gh secret set GEMINI_OAUTH_CREDS < ~/.gemini/oauth_creds.json
~~~

Only the Claude one is settled, because the action has an input built for it. The other two arrive the way their CLI stores them, which works because neither action overwrites the credential file: Codex is pointed at a home directory holding the auth.json, and Gemini reads the one under HOME while the action only ever writes the project's own .gemini directory.

Both of those are untested here, and the Codex one has a reason to doubt it. That action's README says a key must be supplied and it routes model calls through a local proxy holding that key, while a sign in auth.json carries no key at all. If it fails, the key is the documented route. Say which one you used when you report a run.

A sign in expires and a key does not, so a copy taken once goes stale and the secret has to be replaced. That is the trade, and it is the only one between them.

Nothing breaks while these are unset. Each workflow checks first and writes a line into the run summary saying it did not run.

### The config files, and what they do not do

Three directories at the root configure the three agents locally: .claude/settings.json, .gemini/settings.json and .codex/config.toml. A global ignore on the maintainer's machine hides .claude everywhere, which is why .gitignore has to un-ignore the directory before it can un-ignore the file inside it. Codex reads its one only after you trust the project, so it is a suggestion until then rather than something a clone applies to you.

The Codex file said danger-full-access and now says workspace-write. The first is right for a runner, which is a machine thrown away at the end of the job, and wrong for anything checked in, because the file is read on whatever machine clones it. An agent that can write the checkout can do the work here.

What is in them is the set of lookups nobody should be asked about twice: reading git state and reading the tracker. Nothing that writes, and nothing that builds.

cmake and ctest are deliberately absent, which is the surprising one, because those are the commands this project runs most. cmake -E environment prints every variable in the process, and in a run on a public repository the agent's own credential is one of them and the log is world readable. rg --pre runs a command per file. ctest -S runs a script. Each of those arrives under a name that reads like a build tool, so a rule naming the tool grants rather more than the tool. They go through the ordinary check instead, which passes an ordinary build and is the thing that looks at what was actually typed.

What is not in them is a deny list, also on purpose. One was written and then taken out. Denying gh secret leaves gh api reaching the same endpoint, denying Read on .env leaves .env.local, and any rule over a shell is one spelling away from being wrong. A guard that holds for the spellings somebody thought of reads as coverage, and the next agent trusts it. The boundary that actually holds is the permissions block on each workflow job, which scopes the token the agent is handed. That one is enforced by GitHub rather than by a pattern match, so that is where a restriction belongs.

On a pull request event the Claude action restores .claude, CLAUDE.md and .mcp.json from the base branch before it runs, so a pull request cannot widen its own reviewer's permissions by editing them. That is the action's behavior rather than ours, and it is worth knowing before you rely on a change to these files taking effect in the same pull request that makes it.

## Locate the code

| Responsibility | Entry points |
| --- | --- |
| AST, lexical grammar, normalization and context | nps/include/nps/core and nps/src/core |
| Derivation records, rule schema and solving | nps/include/nps/steps and nps/src/steps |
| Physics models and units | nps/src/physics and nps/src/units |
| Backend requests and typed Giac conversion | nps/src/cas/giac |
| Lua input/output, ABI and resource boundaries | nps/src/platform/nspire/lua_module.cc |
| Ki V4 shell, menus, hints and rendering | nps/lua/nps_v4.lua |
| Tests, fixtures and invariant checks | nps/tests |
| Coverage, traceability and package integrity | nps/catalog, nps/tools and nps/CMakeLists.txt |
| Active Giac fork | vendor/khi-src. vendor/giac-src is older reference material |
| SDK and resident runtime | vendor/ndl-src |
| Emulator | vendor/firebird-src |
| Physical USB transport and command tools | vendor/libnspire-src and tools/nsptool |
| Resident key service | tools/keysvc |
| Hidden document storage and runtime relocation | research/folder-hiding |

Search the owning directories with rg. Broad workspace scans include compiler sources, environments, generated artifacts and alternate worktrees.

Dependency checkouts have independent Git state. Ignored files can be authored sources. Inspect .gitignore, .git/info/exclude and the dependency's status before relying on the outer tracked-file list.

## Build and test

Run commands from the cx2 workspace root. nps uses CMake directly. ndl and Giac keep their own dependency build systems. Read [nps/README.md](nps/README.md#build-and-test) for prerequisites and specialized targets.

~~~sh
cmake -S nps -B nps/build/host -G Ninja
cmake --build nps/build/host --target check
~~~

check builds its dependencies and runs CTest. ctest does not build, so build first or you will grade a stale binary. There is no host Giac on this machine, so the tests/giac suite never runs here and the luax vehicle scripts a fake backend in its place. A worktree reports one test fewer than the primary checkout until it has an ARM image, because report-size needs one, which is not a regression.

Choose a focused check when that answers the change:

~~~sh
ctest --test-dir nps/build/host --output-on-failure -R ui_smoke
~~~

For sanitizer coverage:

~~~sh
cmake -S nps -B nps/build/san -G Ninja -DNPS_SANITIZE=ON
cmake --build nps/build/san --target nps_host
ctest --test-dir nps/build/san --output-on-failure -R '^unit$'
cmake --build nps/build/san --target luaxhost
~~~

luaxhost supplies the sanitizer runtime on macOS. Direct nps_host execution requires the nps source working directory. Standalone UI probes should unset NPS_EVIDENCE to avoid appending to a shared evidence file. CTest manages evidence fixtures for normal suite runs.

For calculator packages:

~~~sh
cmake -S nps -B nps/build/device -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ndl-arm926ej-s.cmake
cmake --build nps/build/device --target unified
~~~

Check CMAKE_HOME_DIRECTORY and dependency roots in an existing build cache before using it. The root device directory and .lane-*, .lead-tree* or .headcheck* trees can select other checkouts.

The ARM toolchain is not in this repository. vendor/ndl-src/ndl-sdk/toolchain tracks build_toolchain.sh and a gitignore and nothing else, and nspire-gcc is not tracked either, so a fresh clone cannot configure the device build until that toolchain has been built. That is why Actions does not gate on the ARM build, and why the device workflow is manual rather than running on every push.

The package integrity checks inside the unit binary stage their own packages in a temporary directory rather than needing a device build, so the ARM toolchain is not what decides them. They did fail on Linux for a while, all eighteen of them, because the staging path was written as /private/tmp, which exists on macOS and nowhere else. That is fixed. It is worth knowing as a shape rather than as a fact about those tests: a host suite that has only ever run on one platform can carry a path, a locale or a filesystem assumption that reads as a real failure on the first runner that sees it.

Use resident_exit_lint, tidy, fuzz_run, document, probe and luax when relevant. FUZZ_CASES and FUZZ_SEED are CMake cache options. regold rewrites source fixtures and requires explicit review. Read generated reports for unmet requirements even when the reporting process exits successfully.

## Where your work goes

If you need somewhere to put anything that is not a tracked source file, it goes under .Internal/workspaces/. Lane checkouts, build trees, downloaded images, probe drivers, log captures, patch files, comparison output, a scratch script you will run twice: all of it, all the time, no exceptions worth arguing about.

Name the directory after the work, not after a random suffix or a date. A later session has to be able to tell what a leftover tree was for without opening it.

~~~sh
git worktree add -b fix/what-it-fixes .Internal/workspaces/what-it-fixes/tree main
mkdir -p .Internal/workspaces/what-it-fixes/probe
~~~

Three reasons this is not a style preference.

- /.Internal/workspaces/ is ignored, at .gitignore:143, so nothing you leave there can be swept into a commit by an add that forgot its pathspec. The rest of .Internal/ is tracked, so this applies to the workspaces directory and not to its parent.
- /tmp gets cleared, and parallel lanes share it, so two agents writing the same obvious filename clobber each other in silence. Recovery material has already been lost from /tmp in this repository, and worktrees created there accumulate registrations that outlive the directories.
- A named directory is how the next agent, or you after a context break, tells a live lane from an abandoned one.

Remove your worktree when you are done and run git worktree prune, because the registration otherwise outlives the directory and the stale entry misleads everyone after you. If you are leaving a tree behind on purpose, say so in your report and say what it holds.

## Tests come with the code

Every new function, field, enumerator, branch and bridge record ships with a test in the same commit. A commit that adds behavior and no test is not finished, and a follow-up issue promising the test later is a promise nobody keeps, because the person who understood the code has moved on by then.

Where a test goes, by what it exercises:

| What you added | Where its test goes |
| --- | --- |
| An engine, a rule, a predicate, anything in src/core, src/steps, src/physics or src/units | nps/tests/unit, in the file named after the engine |
| Backend requests and typed Giac conversion | nps/tests/unit/adapter_tests.cc, with a scripted backend |
| A field or a record the Lua bridge emits | nps/tests/target/luax_host.lua |
| Ki V4 shell, menu, hint or rendering behavior | nps/tests/target/ui_smoke_v4.lua |
| A requirement the catalog tracks | nps/tests/corpus, so the acceptance audit sees it |

Write it before the code where you can. It is the only way to know the test can fail, and a test that has never failed is decoration.

Three tests that look like coverage and are not. Watch for all three in your own work, because they pass, and a passing suite is exactly what stops you looking.

- A test that asserts an absence. Checking that a field is missing, a call did not happen or an error was not raised passes just as well for a code path that never ran. Pair it with a control that asserts the positive on the path that should reach it. This has already produced a green suite over a broken bridge in this repository, twice.
- A test whose subject never moves. If the fixture, the golden file or the recorded value would satisfy the assertion whatever the code did, the assertion is about the fixture.
- A guard nobody has watched fail. Revert the code, rebuild, confirm exactly your new checks fail and nothing else does, then restore it. If reverting the change leaves the suite green, the test is not testing the change.

The device is not part of this. Host build and host suite are what a commit can claim. An ARM build proves it compiles and links, nothing more, and emulator or handheld evidence is a separate stage that gets reported separately or not at all.

## Prefer what already exists

Hand rolling is the last resort, not the default. Before you write a helper, a parser, a container, a formatter or an algorithm, look for it in this order and stop at the first one that fits.

1. This tree. Most of what a task needs is already here under another name, and a second copy of it is a second thing to keep correct. The #7 and #34 findings are both this failure in miniature: a producer that reinvented a field the record schema already defined.
2. The C++ standard library. This is C++20, set at nps/CMakeLists.txt:17, so the algorithms header, string_view, optional, variant, span, charconv and the ranges are all available to you. The device toolchain builds with no exceptions and no RTTI, at nps/cmake/toolchains/ndl-arm926ej-s.cmake:51, so anything that reports failure by throwing needs the non-throwing form instead. from_chars over strtod, and an error code over a raise.
3. A vendored dependency. Giac through khi-src, the SDK and resident runtime through ndl-src, the USB transport through libnspire-src, Lua through the SDK. These are already built, already linked and already carried onto the device.

Only then write your own, and when you do, say in the commit or the pull request body what you looked at and why it did not fit. A sentence naming the thing you rejected is worth more than a paragraph describing what you built.

Two cases where the answer is genuinely to write it yourself, so nobody has to relitigate them. Text scanning, because regular expressions are ruled out here for reasons set out in the user's global instructions and a character-at-a-time scanner is the replacement. And anything that would add a new third-party dependency to the device target, which is a decision about the package, not about the code, so raise it as an issue rather than making it inside a fix.

None of this licenses pulling something in for its own sake. A dependency already in the tree costs nothing extra to use and a new one costs an argument.

## Contracts to preserve

- NodeId values and node references belong to their Arena. Arena resource failure is sticky. Sample agreement is weaker than symbolic proof.
- Derivation records carry the operation, conditions and verification evidence. A returned value and a verified walkthrough are separate facts.
- Keep cancellation, resource exhaustion, unsupported input and failed verification distinct. Do not call Giac after a terminal resource or cancellation status.
- Validate raising Lua arguments before GcPause or other request-owned C++ objects. Contain inherited table lookups in a protected Lua call so field errors return input refusals without skipping native destructors. Share identifier validation and normalization with the parser.
- The Lua bridge copies native results into Lua tables. Hint exposure, folding and detail navigation operate on those tables without another solver call.
- answer_only identifies the displayed answer's source. Native steps can coexist with it. Use canonicalStepCount and answerWithoutSteps for walkthrough decisions.
- Bound summary rendering. Full result text and oversized step expressions remain reachable by scrolling. Hint mode withholds the final result until progression permits it.
- Preserve Ki V1-derived shell behavior when changing V4. KhiCAS is a dependency and UI reference.
- Unit conversion uses exact multiplicative scales. Measured precision is distinct from algebraic numeric mode. Vector operations require compatible frames, ranks and dimensions.
- A compiled object, linked ELF, packaged TNS, loaded module and visible device effect are separate verification stages. Match sidecars to the actual package bytes.

## Device and research work

Physical uploads, key events, restarts and file moves change external state. Keep them within the user's authorized task. Do not replay an action after a transport failure when its application is unknown.

The autonomy above stops at the USB cable. One handheld is shared by every lane, so exactly one agent drives it at a time and that agent is the session lead. If you are a teammate, ask the lead rather than reaching for the device or the emulator yourself, and never assume an earlier session left the device in the state your instructions describe.

keysvc acknowledges before applying events. Host tools, emulator input and the resident key service are different mechanisms. Verify visible effects when claiming UI behavior.

Firebird snapshots and flash images can depend on backing images. Establish provenance before changing or replacing them. Native calculator filenames and transfer names belong to different namespaces.

Read [research/folder-hiding/AGENTS.md](research/folder-hiding/AGENTS.md) before storage or relocation work. Its manifests, original files and rollback material are part of recovery. The prototype conceals files from browsing without encrypting them.

## Keep instructions maintainable

AGENTS.md is the shared instruction source. CLAUDE.md and GEMINI.md refer to it. Keep detailed architecture in docs/codebase-map.md and exact build workflows in nps/README.md. Read relevant sections on demand instead of loading every document. Agent startup settings are documented in [the codebase map](docs/codebase-map.md#agent-environment).

Keep user instructions, requirements, implementation descriptions and historical evidence distinct. Preserve requirement IDs and table schemas consumed by reporting tools. Do not turn a documented requirement into a claim that it is implemented.
