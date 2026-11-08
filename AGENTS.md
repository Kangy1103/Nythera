# Nythera Agent Instructions

These instructions apply to AI coding agents working in this repository. Their purpose is to minimise tokens, tool calls, build time, and unnecessary iteration **without weakening correctness, maintainability, or visual quality**.

## Priority and project context

- Follow the user's current request first. Respect existing project guidance in `ARCHITECTURE.md`, `CONTRIBUTING.md`, `BUILDING.md`, `SCOPE.md`, and `REBRAND.md` when relevant. If instructions conflict, surface the conflict briefly rather than silently bypassing a technical invariant.
- This is **Nythera**, a C++23 native Wayland shell fork, built with **Meson/Ninja** and orchestrated by `just`; do not treat it as the original Noctalia or a QML project.
- Development primarily targets **Umbriel**; preserve other compositor integrations unless a change explicitly targets them. Route compositor-specific behaviour through `CompositorPlatform` and the appropriate backend.
- Preserve stable upstream functionality. The design direction includes floating islands, dynamic bars, screen wrapping, configurable styling, and a dark sci-fi/fantasy aesthetic. Do not impose unrelated redesigns.
- Treat user-approved layout, appearance, behaviour and naming decisions as fixed until the user changes them. Never quietly reinterpret the requested design.
- Distinguish Nythera-owned components from Umbriel-owned components. Trace identifier ownership before renaming or modifying integrations. Shared development origins do not imply shared ownership.

## Research, initiative and implementation

- **Investigation is not implementation.** When asked how existing functionality works, whether a change is feasible, or what implementation would involve, proactively inspect relevant source code and configuration before answering.
- Do not provide speculative implementation outlines when the repository can establish the actual answer. Do not ask permission to perform routine, non-invasive investigation already implied by the request.
- For informational questions, investigate and explain without modifying code unless implementation is also requested.
- For explicit implementation requests, investigate and execute without repeatedly asking for permission to make routine decisions.
- During an ongoing implementation or debugging task, interpret follow-up questions, reported defects and visual observations in the context of the established objective. Investigate and correct outstanding defects without requiring another explicit implementation command.
- Ask for clarification only when genuinely different interpretations would substantially affect the result, or when an action could cause unwanted or destructive changes.
- Do not silently expand the scope. Distinguish necessary implementation work from unrelated improvements.

## Execution budget (default for every task)

1. **Orient once.** Run `git status --short` and inspect relevant changed files/diff only if needed. Identify the feature's owning subsystem with targeted path/name searches. Read the relevant portions of existing documentation and code; do not recursively dump the repository.
2. **Plan briefly and execute when authorised.** For straightforward implementation tasks, proceed immediately. For cross-cutting changes, form a short internal plan and communicate only material decisions. For investigation-only requests, inspect the implementation and provide a grounded answer without changing files.
3. **Search narrowly.** Use `rg --files`, `rg -n 'specificSymbol' src/<subsystem> tests/`, and bounded reads (`sed -n 'START,ENDp'`). Check file names before searching contents. Exclude `third_party/`, build trees, generated output, caches, and large assets unless directly relevant. Expand search only when evidence warrants it.
4. **Batch coherent edits.** Finish related changes in one deliberate pass where practical. Avoid multiple small edit/review cycles, mass reformatting, and touching unrelated code.
5. **Validate at the appropriate level.** Aim for one coherent validation pass using the smallest checks that actually exercise the change (matrix below). Combine independent checks where feasible. Repeat validation only after relevant changes, failures, or new evidence that justifies another pass.
6. **Live verification.** After successful build and applicable tests, use `nythera restart` only when live verification is necessary and interruption of the active shell has been authorised. Do not restart during a locked session or when doing so could disrupt ongoing work. Never restart after a failed build or test.
7. **Final diff review.** Aim for one final review of `git diff --check`, the changed-file list and focused diff. Check requested behaviour, unintended changes and applicable architectural invariants. Repeat only when subsequent edits or new evidence warrant it.
8. **Report concisely.** State what changed, what validation passed or failed, and any genuine limitations. No verbose command transcripts, speculative concerns, or long narrated reasoning.

The budget is a **default, not an excuse to ship broken code**. Expand investigation or validation for a concrete failure, unverified safety-critical behaviour, or clearly cross-cutting impact. State the reason briefly.

## Tool and output discipline

- Prefer repository-local tools and direct file reads to browsing. Consult external docs only for an actual API uncertainty, compatibility question, or changed upstream behaviour; do not repeatedly fetch the same sources.
- Reuse results, existing build directories and cached context. Do not re-read files already understood unless they changed or a new question requires it.
- Default to concise command output: filenames, matching lines, error summaries and exit codes. Avoid `cat` of large files, full compiler logs, `git diff` of the whole repository, and unconstrained recursive searches. Capture long failure logs to a temporary file and inspect the first relevant error with nearby context.
- Use batch/parallel reads for independent files; avoid simultaneous commands that mutate the same paths, build directory or Git index.
- Do not delegate to subagents by default. Delegate only clearly independent, bounded work that saves more effort than coordination costs; prevent duplicate inspection and overlapping edits. Return a short, actionable summary from each subagent.
- Do not create planning documents, scratch markdown, screenshots, diagnostics scripts, benchmark harnesses or new dependencies for routine tasks unless they serve a specific need. Temporary debug instrumentation must be removed.
- Never install system packages, alter global configuration, run privileged commands, clean build directories, or modify live user configuration without explicit authorisation.

## Choose validation by change type

| Change | Default validation |
| --- | --- |
| Documentation or comments only | `git diff --check`; run focused docs formatting check when Markdown structure or syntax changes. No C++ build/tests. |
| Pure UI styling, spacing, geometry or animations | Incremental `just build` if C++ changed; visual inspection in the real shell only if an appropriate working session is available and it adds value. Do not run the entire test suite or fabricate mock visual tests. |
| Normal C++ behaviour changes | Incremental `just build`; run only directly related Meson tests using `meson test -C build-debug <test-name> --print-errorlogs`. |
| Configuration, persistence, parsing, security or nontrivial state transitions | Incremental build and directly relevant tests; add a focused regression test when it protects a deterministic, meaningful contract not already covered. |
| Public headers, shared UI primitives, event loop, rendering backend or broad architecture | Incremental build plus a reasoned selection of affected tests; expand to full suite only when impact cannot be bounded. |
| Translation key additions or renames | `python3 tools/i18n-check.py` plus change-relevant checks. Edit the English catalogue only unless explicitly tasked otherwise. |
| Palette role/alpha changes | `just role-alpha-check` plus relevant checks. |
| CI/workflow scripts | `just test-workflows` when relevant; no C++ build unless build logic changes. |

- Use the existing `build-debug/` directory and incremental builds when available and correctly configured. `just build` configures if necessary; do not run `just configure` before every build.
- Do not run `just rebuild`, `just clean`, ASan, release builds, full `just lint`, full `just test`, or multi-distro builds by default.
- Prefer focused clang-tidy for touched `.cpp` files if lint is warranted: `clang-tidy -p build-debug --warnings-as-errors='*' <touched-files>`. Whole-tree `just lint` is a CI/explicit-request operation.
- Prefer formatting **only touched C++ files** with the project's configured clang-format version (currently 22). Project-wide `just format-code` rewrites all source/test files and should not be used for narrow changes.
- For docs, use the repository remark formatter or `just format-docs-check` when needed; note that the docs recipe may install its pinned local npm dependencies.
- Before a targeted Meson test, use `meson test -C build-debug --list` if the exact test name is unknown; do this once, not repeatedly. If no matching test exists, don't invent an arbitrary test merely to tick a box.
- The user's own visual review can be the appropriate final visual acceptance check. If a browser/GUI screenshot is useful, perform **at most one** targeted visual check per task by default; avoid repeated screenshot cycles and multiple browsers unless a concrete defect demands it.
- Never claim visual verification when only compilation ran. Compare visual results against the actual requested layout and behaviour, not merely whether the application rendered successfully.
- Distinguish **not run** from **passed**, and report failures accurately.

## Working with project documentation

- `AGENTS.md` is the entry point, not the entire knowledge base. Consult the relevant sections of existing documents rather than loading all of them for every task.
- `ARCHITECTURE.md`: UI lifecycle, rendering, services, surfaces and other implementation invariants; required for architectural/refactoring work.
- `CONTRIBUTING.md` and `BUILDING.md`: coding conventions, build/test guidance and contributor process as relevant.
- `SCOPE.md`: distinguish approved scope, experiments and future plans.
- `REBRAND.md`: Nythera-owned identity, external Noctalia contracts, plugin translations and migration; required for naming/API changes.
- `docs/NYTHERA-CUSTOMISATIONS.md`: documented Nythera behaviours, development status and compatibility risks. Check actual code before treating an item as implemented.
- `docs/UPSTREAM-INTEGRATION.md`: required procedure for integrating changes from Noctalia upstream. Do not use the standard narrow-change budget to justify skipping cross-subsystem compatibility checks for a broad update.
- When a task materially changes a documented behavioural contract or architectural rule, update its owning document in the same change. Avoid duplicating the same rules in multiple files.

## Git safety and upstream integration

- Inspect the current branch/status before making repository changes. Protect uncommitted work; do not reset, overwrite, stash, rebase, force-push, or merge into `main` without explicit approval and an accounted-for state.
- Use `dev` for ordinary development. For substantial or risky changes, especially upstream integrations, create a temporary task/integration branch (or a separate worktree when warranted). Do not create a branch for every trivial adjustment.
- Treat upstream integration as a **semantic migration**, not just Git conflict resolution: evaluate changed APIs and indirect Nythera dependencies, including custom files absent from the upstream diff.
- Preserve Nythera's **intended behaviour**, not necessarily old code. Where upstream replaces an architecture, migrate customisation to the new interface instead of blindly retaining obsolete mechanisms.
- For the first Noctalia update, start from the verified shared-ancestor SHA in `docs/UPSTREAM-INTEGRATION.md` and verify it against freshly fetched history; no upstream merge has occurred since the fork. For later updates, track the last upstream target merged/imported and carry forward exclusions or deferred items. Never infer completeness from ahead/behind counts, a clean merge or selective imports.
- Account for each relevant upstream change as incorporated, adapted, deliberately excluded, deferred or blocked, with rationale. A clean Git merge and successful build alone do not prove compatibility.
- For integration tasks, follow `docs/UPSTREAM-INTEGRATION.md`, check impacted entries in `docs/NYTHERA-CUSTOMISATIONS.md`, and record actual validation plus unresolved work before declaring completion.
- GitHub CI currently runs on pull requests and pushes to `main`, not ordinary pushes to `dev`. Do not treat a successful `dev` push as proof that CI passed; report actual checks performed.

### Committing and pushing session work

- When asked to commit or push "your changes", identify the complete relevant change set from the current task or session, not merely the most recently edited file. Include related implementation, tests, assets, documentation, additions and deletions.
- Use conversation context, current Git status and focused diffs to distinguish session work from unrelated user changes. Do not assume pre-existing uncommitted files are unrelated merely because they were modified earlier in a long session.
- Review the files to be staged and commit the full relevant change set; do not blindly stage the entire working tree. If ownership or inclusion is genuinely uncertain, ask before including those files.
- After pushing, verify the remote branch contains the intended commit and report what was committed, what was pushed and any relevant changes left uncommitted.

