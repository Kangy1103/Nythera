# Nythera — Ideas Backlog

> Brainstorming space, not an implementation specification. These are proposals, **not claims about features already implemented**. Validate against the current codebase and Wiki.js documentation before planning work.

## Direction
Make Nythera a distinctive, cohesive shell built on Noctalia, prioritising useful functionality as well as its cyberpunk / sci-fi aesthetic. Preserve the working Nythera bar rather than redesigning it without a concrete reason. Keep Umbriel's scrolling-window workflow in mind.

## New functionality to explore

### 1. Unified command interface
- Keyboard-first search across applications, open windows, shell actions, settings and configurable commands.
- Contextual results with fuzzy matching, recent actions and pinned commands.
- Optional calculator, unit conversions and clipboard-history providers.
- Extensible provider interface, so features can be added without turning the launcher into one giant component.
- **Guardrail:** never execute arbitrary shell commands from untrusted search results.

### 2. Workspace / window intelligence
- An Umbriel-aware overview of windows and workspaces with quick navigation.
- Recently focused windows, per-workspace labels and configurable jump actions.
- Optional session snapshots (record window/app layout and reopen applications where feasible).
- **Investigate:** compositor protocol/API support before promising window positioning or restoration.

### 3. Context-aware status and alerts
- Consolidate actionable events (network loss, low battery, failed background jobs, updates) into a quiet event centre.
- Priority tiers, snooze, history and user-configurable rules.
- Keep informational events separate from urgent notifications.

### 4. Automation / macros
- User-defined actions such as toggling a performance profile, opening a work setup, or launching a group of applications.
- Command palette integration and optional keyboard shortcuts.
- Confirm destructive operations; expose exactly what each action will run.

### 5. Plugin-style widgets and data sources
- A documented interface for small Nythera modules (e.g. hardware sensors, media, network status, homelab health).
- Configurable refresh rates and graceful failure/offline states.
- Avoid expensive polling across four monitors; favour event-driven updates.

### 6. Theme / profile modes
- Named visual and behaviour profiles (work, gaming, focus).
- Switch notification policy, animation intensity, layout preferences and optional quick actions together.
- Keep manual overrides and a simple reset path.

## Existing surfaces worth redesigning
These are visual/interaction projects, separate from the new functionality above:
- **Launcher:** futuristic command interface with excellent keyboard usability.
- **Control centre:** clearer hierarchy and richer system feedback.
- **Notifications:** consistent event styling and meaningful priority.
- **Lock screen:** cohesive Nythera identity without compromising authentication.
- **OSDs:** consistent volume/brightness/microphone treatment.
- **Window switcher:** design around Umbriel's workflow and available compositor capabilities.

## Shared design principles
- Frosted glass, controlled neon accents, readable typography and restrained animation.
- Cohesive spacing, borders, transitions and states across components.
- Distinctive, not a generic card grid; visuals should support usability.
- Accessibility: reduced motion, contrast and keyboard navigation.
- Performance matters, especially with multiple monitors; measure rather than guess.

## Before implementing an idea
1. Check existing Nythera/Noctalia functionality and Wiki.js notes to avoid duplication.
2. Confirm technical feasibility and compositor integration.
3. Define the smallest useful first iteration and acceptance criteria.
4. Make focused changes; test only affected functionality.
5. Record decisions and move completed ideas into project documentation.

## Parking lot / questions
- Which functionality is already present in the current Nythera branch?
- Which Umbriel APIs can be used reliably?
- Should the command interface replace or extend the current launcher?
- What is the preferred extension boundary for third-party data sources?

---
_Initial brainstorming notes, 7 November 2026. Keep this file lightweight and revise it as decisions are made._
