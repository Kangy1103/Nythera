# Dynamic bar: initial prototype

Status: proposed implementation plan, 4 November 2026. No runtime changes yet.

## Saved starting point

- Baseline branch: `codex/bar-baseline-2026-11-04`, commit `c53f851f6`.
- Working branch: `codex/dynamic-bar-prototype`.
- `main` remains at its previous commit. The baseline includes the eight previously
  uncommitted files, including configuration isolation and the surface-mode selector.
- Design references: [Bar Ideas](https://wiki.kangy.uk/personal-projects/umbriel-cyberpunk-shell/bar-ideas)
  and [Bar](https://wiki.kangy.uk/personal-projects/umbriel-cyberpunk-shell/bar).

## First result to evaluate

A rounded, translucent bar with three functional sections. It can use a continuous
background or three genuine section backgrounds, with a clock-centred area that
smoothly expands to show media information. Workspace-aware behaviour follows as a
separate step. The screen-wrapping frame is a later experiment using the same pieces.

Use the selected palette, current widget membership, native content/font scaling,
and existing bar placement. Rounded silhouettes and restrained emphasis should do
the visual work; animation should communicate a change of state.

## 1. Establish reliable section geometry

Inspect `Bar::buildScene`, layout, palette updates, input regions, shadows and attached
panel geometry together before changing backgrounds. The current islands implementation
hides the continuous background and relies on configured capsules; that alone does
not guarantee one complete background per section.

- Give start, centre and end sections their own measured background bounds in islands
  mode, enclosing every module assigned to that section. Empty sections paint nothing.
- Support top, bottom, left and right using main/cross-axis geometry. Start/end become
  top/bottom for vertical placement; changing modes must not change the selected edge.
- Keep the existing continuous-bar geometry and corner policy for full mode.
- Scope input and compositor blur regions to the actual island shapes. Transparent
  gaps must not intercept desktop clicks or request a full-width blur.
- Reuse palette roles, configured opacity, radii and outlines. Palette changes update
  both modes without rebuilding their identity around fixed colours.
- Reuse the existing Full Bar / Floating Islands setting and per-monitor overrides.

Checkpoint: switch layouts at every edge and confirm section grouping, corners,
spacing, attached popups, click-through gaps and configuration persistence.

## 2. Prototype the dynamic centre

Create a small presentation controller adjacent to `BarInstance`, with explicit
resting, media and hover-expanded states. Use the existing `AnimationManager` for
size and opacity, and existing clock/media widgets and services for content.

- Resting: clock in a compact section.
- Playing: expand along the bar's main axis to show a bounded track label and small
  visualiser beside the clock. Keep the centre anchored during the resize.
- Hover: reveal transport controls; keyboard activation should offer the same actions.
- Paused/stopped: settle the visualiser; return to the compact state after a short
  delay unless the pointer, keyboard focus or an attached popup is holding it open.
- Track changes crossfade text within the available extent instead of jumping widths.
- Reserve room for start/end sections, truncate long titles and collapse optional
  details before allowing overlap. Vertical bars use compact content appropriate to
  their width rather than squeezing horizontal text into them.
- Retarget an interrupted animation from its current value. Rapid playback and track
  changes must not queue a chain of stale transitions.

Start with existing motion durations/easing. Respect global animation disable/speed
and reduced-motion behaviour. Reuse `PipeWireSpectrum` and the existing visualiser;
the native implementation does not need a second audio capture or CAVA process.

Expose the new behaviour in the Settings GUI in the same implementation commit as
its configuration: dynamic centre enabled/disabled and visualiser visibility. Prefer
existing widget controls for detail that is already configurable. Leave the feature
disabled by default; enable it deliberately when previewing the prototype.

Checkpoint: judge compact, playback, hover and transition states live before extending
the controller to notifications, volume or other temporary content. Those are candidates,
not commitments for this first pass.

## 3. Add workspace-aware adaptation

Keep this independent of full/islands presentation and preserve the chosen screen edge.
The requested relocation concerns adaptation along that edge to workspace content;
it does not mean automatically moving between screen edges.

Use the existing compositor workspace/window data and empty-workspace detection.
Before choosing an algorithm, inspect Umbriel's actual geometry/event support and
the supplied ArchEclipse implementation to establish the behaviour we can reproduce.
If only occupancy is available, describe that limit explicitly rather than pretending
occupancy-based hiding reproduces content-aware placement.

First preview: an optional compact/expanded response to empty versus occupied workspaces,
with smooth transitions and a fixed edge. Keep true content-aware repositioning as a
separate selectable behaviour once its geometry rules are established. Expose each
implemented behaviour in Settings and handle rapid workspace switching without flicker.

## 4. Evaluate a screen wrap separately

After the centre and section geometry are accepted, prototype rounded rails around
the desktop opening, with configurable modules on each side and per-monitor enablement.
Decide how adjoining bars share corners, input and reserved space before joining rails.
Use the same widgets, palette and animation controller where appropriate.

Do not assume a single transparent monitor-sized rendering surface is cheap. Compare
edge surfaces and shared frame rendering on the actual four-monitor setup before
settling the architecture. This phase is not required for the first usable prototype.

## Validation and delivery

Implement each checkpoint as a separate commit on the prototype branch. Use focused
geometry/state tests where they cover meaningful failures, build the shell, and exercise
the relevant Settings and runtime paths. Keep preview changes in the fork's isolated
configuration; switching Git branches does not restore user configuration.

Measure baseline and prototype with the same build type and monitor setup: idle,
playback, repeated expansion and workspace switching. Record RAM, process-attributable
VRAM where available, CPU and frame timing. Report attribution limitations and actual
measurements rather than a promised resource budget. Check that idle animation stops
and hidden visualisers stop scheduling frames.

Review after phase 2: visual judgement belongs to Kangy. Continue with workspace
adaptation and the frame only after the initial behaviour provides a useful direction.
