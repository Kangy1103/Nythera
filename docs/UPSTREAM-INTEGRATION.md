# Integrating Noctalia upstream into Nythera

Use this procedure for deliberate upstream updates. **Integration success means retaining Nythera's intended behaviour while adopting compatible upstream improvements, not retaining every old implementation.** Git's lack of text conflicts does not prove compatibility.

See [NYTHERA-CUSTOMISATIONS.md](NYTHERA-CUSTOMISATIONS.md), [../ARCHITECTURE.md](../ARCHITECTURE.md), [../REBRAND.md](../REBRAND.md) and [../CONTRIBUTING.md](../CONTRIBUTING.md). Read only relevant sections after initial orientation.

## Ground rules

- Work from a clean, accounted-for repository state. Never reset, discard, overwrite or stash another person's work without explicit approval.
- Never merge directly into `main` or replace the active development checkout during evaluation. Use a temporary `integration/noctalia-<version-or-date>` branch from `dev`; use a separate worktree when this protects the running build.
- Do not trigger shell restart, install packages, edit live configuration or use force-push without explicit permission.
- Do not assume an `upstream` Git remote exists locally; inspect `git remote -v`. Expected official source: `https://github.com/noctalia-dev/noctalia.git`. Establish/confirm the correct tracking branch before fetching or integrating.
- Keep external service identities, plugin compatibility, license/attribution and Nythera-owned rebranding boundaries from `REBRAND.md`.
- The shared ancestor for the **first** upstream integration is verified below. It is the *divergence baseline*, not evidence of an upstream merge after the fork. For later updates distinguish the last upstream target merged into Git history from changes actually adopted, intentionally excluded or deferred. Never misrepresent cherry-picked changes as a complete merge.

## Phase 1 — Read-only discovery

1. Record local branch, status, remotes and relevant upstream refs. For the first integration, confirm the recorded divergence SHA remains the merge base of local `dev` and the selected upstream branch (`git merge-base dev upstream/main`, after fetching). For later integrations, consult the last integration report and history for the last merged upstream target and any deferred changes. If records disagree, investigate before continuing.
2. Choose an explicit upstream target SHA/release. For the first integration set `OLD` to the verified divergence SHA below; for later integrations use the appropriate recorded upstream comparison baseline, while carrying forward deferred items. Collect bounded summaries first: `git log --oneline OLD..NEW`, `git diff --stat OLD NEW`, and `git diff --name-status OLD NEW`. Expand only as needed.
3. Classify changes by subsystem and risk: ordinary additions/fixes; feature overlap; renamed/deleted symbols; API, event, state, config, compositor, rendering and surface lifecycle changes.
4. Map changed upstream interfaces to **Nythera callers, overrides and dependent files**, including locally customised files unchanged upstream. Use the customisation register and inspect relevant source to avoid semantic conflicts that Git cannot detect.
5. Present a short integration scope: target, baseline confidence, impacted behaviour, known risks, intended merge method, and any unresolved design decisions.

## Phase 2 — Controlled integration

1. Create a branch/worktree isolated from `dev`. Preserve the starting commit.
2. Where the repositories share suitable history, prefer a normal merge from the selected upstream ref so Git retains ancestry. If a full merge is unsuitable, explain the reason and selectively migrate interdependent changes together. Do not mechanically cherry-pick dependent refactors one commit at a time.
3. For each impacted subsystem, choose explicitly:
   - **Compatible** — accept upstream behaviour.
   - **Custom overlap** — integrate upstream and restore Nythera's behavioural contract.
   - **Architecture replacement** — port Nythera features onto the new upstream service/interface; retire obsolete implementations.
   - **Intentional exclusion** — retain Nythera's design with documented rationale and associated upstream changes.
   - **Uncertain/blocked** — investigate or report as unresolved, never silently skip.
4. Examine upstream deletions, config migrations, renamed contracts and indirect callers, even when Git reports a clean merge.
5. Keep changes scoped. Do not rename genuine upstream endpoints or Umbriel-owned interfaces merely because they contain `noctalia`.

## Phase 3 — Verify and record

1. Run an incremental `just build` and directly affected tests. Broaden testing for shared primitives or cross-cutting changes; avoid full suites for isolated changes without justification.
2. Where available and authorised, check real shell behaviour for affected layouts, scaling, plugins, input and animations. Compilation alone does not verify appearance or functionality; record untested visual/compositor cases honestly.
3. Review the changed-file list and `git diff --check`, compare affected behaviour with the register, and ensure every material upstream change is integrated, adapted, excluded or explicitly deferred.
4. Write a concise integration report in the PR body or `docs/upstream-integrations/<target>.md` (only when useful) containing:
   - Previous upstream comparison baseline SHA, target upstream SHA and any previously deferred items.
   - Integration branch and method (merge or selective migration).
   - Affected subsystems and Nythera contract adaptations.
   - Included, intentionally excluded, blocked and deferred items, with upstream references.
   - Build/test/manual checks actually performed and remaining uncertainty.
   - Whether the target was **merged and accounted for**, **partially imported**, or **not integrated**. A full Git merge can still have documented Nythera-specific exclusions; it does not guarantee every upstream feature is enabled.
5. Only after review, merge the integration into `dev` through the normal review workflow. Promote `dev` to `main` separately when stable.

## Baseline tracking

**First-integration divergence baseline (verified 8 November 2026):** `03b9e9a0649c90485c16a6dc2a67db7105cf4d72`.

GitHub's cross-fork comparison of `Kangy1103/Nythera:dev` against `noctalia-dev/noctalia:main` returned that SHA as their common ancestor. The repository owner confirms there has been **no upstream merge since the fork**. This is therefore the starting point for the first upstream comparison, not a previously completed integration. Both Nythera `main` and `dev` shared this ancestor when checked. GitHub's ahead/behind numbers change as new commits arrive, so do not treat them as a permanent record.

**First integration target:** not selected. **Upstream updates integrated since divergence:** none confirmed.

For later updates, record the chosen upstream target and how it was integrated, plus any excluded or deferred changes. A normal merge may advance the Git ancestry while intentionally retaining Nythera behaviour; a selective import does not make the entire upstream range part of Git ancestry. Subsequent reviews must carry outstanding deferred items forward, even when the target is already an ancestor.

## Completion criteria

An integration is not complete merely because Git merged cleanly or the binary builds. Relevant upstream changes must be accounted for; Nythera behaviour must be preserved or deliberately changed with approval; and unresolved compatibility work must be visible as blocked/deferred rather than marked done.
