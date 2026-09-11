# Claude project context

@./AGENTS.md

The shared instructions above apply to this workspace. Read the relevant sections of [the codebase map](docs/codebase-map.md) for detailed architecture and [the build guide](nps/README.md#build-and-test) for specialized CMake commands.

## Team handoffs

Give each teammate the exact checkout, owned files, authorized scope, relevant map sections, verified findings and required validation. Teammates load project instructions but do not inherit the lead's conversation. Repeat shared-tree preservation and the requirement to stop implementation before independent review in the assignment. See [Claude team context](https://code.claude.com/docs/en/agent-teams#context-and-communication).

For Explore and Plan subagents, repeat essential constraints in the prompt. These built-in agents omit CLAUDE.md and Git status. Ordinary subagents load project instructions but need task context supplied explicitly. See [subagent startup context](https://code.claude.com/docs/en/sub-agents#what-loads-at-startup).
