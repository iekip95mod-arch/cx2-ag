# Nspire Physics Solver — coding-agent specification pack

This pack converts the research request into a repository-ready agent workflow rather than relying on one oversized chat prompt.

## Files

- `MASTER_AGENT_PROMPT.md` — the complete, self-contained prompt to give a coding agent.
- `AGENTS.md` — concise persistent repository instructions. Keep this at the repository root.
- `docs/ARCHITECTURE_SPEC.md` — technical architecture and invariants.
- `docs/CORPUS_AUDIT.md` — findings from the supplied EPUB and the required corpus methodology.
- `tasks/M0_FEASIBILITY.md` — the first task. This is a blocking feasibility gate.
- `tasks/M1_VERTICAL_SLICE.md` — the first product vertical slice after M0 passes.
- `templates/ADR_TEMPLATE.md` — architecture-decision record template.
- `templates/TASK_BRIEF_TEMPLATE.md` — bounded task template for later agents.
- `REFERENCES.md` — web research and primary-source links.

## Recommended use

1. Keep the supplied EPUB outside version control. Set `PHYSICS_CORPUS_EPUB` to its local path.
2. Copy this pack into the new repository.
3. Replace `<PROJECT_NAME>` only after choosing a project name; otherwise use the working name `nspire-physics-solver`.
4. Give the agent this initial instruction:

   ```text
   Read AGENTS.md, MASTER_AGENT_PROMPT.md, docs/ARCHITECTURE_SPEC.md,
   docs/CORPUS_AUDIT.md, and tasks/M0_FEASIBILITY.md. Execute M0 only.
   Do not start M1 or broad physics implementation. Record every material
   architectural decision as an ADR and run every acceptance command.
   ```

5. Do not ask an agent to implement “all physics” in one run. Assign one milestone or one problem-archetype family per task.
6. After M0 passes, issue `tasks/M1_VERTICAL_SLICE.md` as the next bounded task.

## Why the pack is split

Official Codex, Claude Code, and GitHub Copilot guidance all emphasize persistent repository instructions, exact build/test/validation commands, scoped context, and reliable automated checks. The long specification belongs in `docs/`; `AGENTS.md` should stay concise enough to be read every session. See `REFERENCES.md` entries R1–R7.
