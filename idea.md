# Nythera — Ideas & Design Directions

> A place to capture ideas and conversations, **not a development backlog or a list of approved tasks**. The six areas we discussed are the main subjects. Other possibilities are retained below as secondary ideas. Discussing an idea does not mean it has been approved for implementation.

## Overall direction

Nythera builds on Noctalia rather than needlessly recreating what it already does. Aim for a distinctive futuristic shell with a restrained cyberpunk influence: thoughtful geometry, readable typography, purposeful movement and theme-controlled accents — **not neon and effects everywhere**. Preserve existing functionality, Noctalia's powerful theming, system-level frosted glass and the Nythera bar we've already refined. Keep Umbriel's scrolling-window workflow and performance across multiple monitors in mind.

## Main ideas from our discussion

These are the **six areas we actually talked about**, not six mandatory projects. Design details remain open until explicitly agreed.

### 1. Redesign the launcher

**Original direction:** Give the application launcher a distinctly futuristic visual identity, a clear hierarchy and more expressive navigation and selection, while remaining quick and keyboard-friendly.

**Current status: Detailed launcher design agreed; Codex implementation underway.**

Research into the existing launcher showed that Noctalia already implements categories, providers, keyboard navigation, recent/pinned apps, configurable size and layouts, theming, and much more. **Do not replace the launcher or merge it with the Control Centre.**

Agreed design decisions:
- Compact, responsive branding header using the *actual Nythera SVG* and *Nythera Display font* for the NYTHERA wordmark only, with Off / Glyph only / Glyph + wordmark visibility options.
- Thin, understated, theme-derived diagonal header accent.
- Keep existing Recent, Pinned (when present), All, Categories and Providers navigation and controls. Active tab gets a subtle tint and a thin accent underline that slides between tabs.
- Keep application row density, existing UI fonts, icons, descriptions, current mouse hover and normal behaviour. Replace the solid keyboard-selected fill with a muted theme tint and narrow left accent, sliding between neighbouring results.
- Keep the bottom avatar/search/session toolbar. Avatar stays visual only. Apply subtle rounded/chamfered geometry to **the outer toolbar only**, keeping the search input rounded; add a fine inset separator above the toolbar.
- Preserve all existing theme sources, custom themes, and system glass. Inspect current theme overrides before adding any optional semantic hooks. No hardcoded scarlet.
- Leave opening/closing animations alone; a separate later exploration may examine integrating the existing Umbriel glitch animation across Nythera. Only the two approved sliding indicators belong in this launcher pass.
- Responsive branding without tiny or oversized marks, overlaps or loss of useful results space.

The separately agreed Codex implementation prompt is the detailed specification. Any final sizing/feel will be evaluated on the actual desktop.

### 2. Reimagine the Control Centre

**Status: Concept discussed, not specified or approved for implementation.**

Imagine a compact systems console with clearer sections for connectivity, audio, display and performance; custom animated controls where useful; subtle telemetry and coherent Nythera styling. Keep it **separate from the launcher**. Investigate current Control Centre functionality before deciding what, if anything, needs changing.

### 3. Custom notification system

**Status: Concept discussed, not specified or approved for implementation.**

Notifications could enter with restrained motion and a subtle accent edge. Explore different treatments for ordinary messages and warnings, plus a history panel that reads more like an event feed than stacked cards. Priorities, actionable events and snoozing are separate potential functionality ideas, not agreed requirements.

### 4. Nythera lock screen

**Status: Concept discussed, not specified or approved for implementation.**

Explore strong typography, ambient animation, restrained geometric details and an authentication interface that feels like unlocking a futuristic OS. Functionality, safety and readability take priority over spectacle.

### 5. Replace or restyle the OSDs

**Status: Concept discussed, not specified or approved for implementation.**

Consider custom volume, brightness and microphone overlays using **segmented energy-style indicators with a smooth filled region**, rather than conventional progress bars. Aim for consistent, functional feedback without decorative clutter. Confirm existing behaviour before planning changes.

### 6. Window switcher overhaul

**Status: Concept discussed, not specified or approved for implementation.**

Explore a more cinematic Alt+Tab experience with depth, focus transitions and useful previews — designed to complement Umbriel's scrolling layout, not fight it. Investigate compositor support and current switcher functionality before committing to any feature.

## Secondary ideas — possibilities, not decisions

These are additional directions worth keeping around, but they are **not the main six discussion topics** and are not approved work. Some may already exist in Nythera/Noctalia; verify first.

### Unified command interface
- Keyboard-first search across applications, open windows, shell actions, settings and configurable commands.
- Contextual results with fuzzy matching, recent actions and pinned commands.
- Optional calculator, unit conversions and clipboard-history providers.
- Extensible provider interface so features can be added without making one giant component.
- **Important:** Much of this is already supported by the existing launcher. Do not propose a replacement or duplicate providers without checking the code.
- **Guardrail:** never execute arbitrary shell commands from untrusted search results.

### Workspace / window intelligence
- Umbriel-aware overview of windows and workspaces with quick navigation.
- Recently focused windows, per-workspace labels and configurable jump actions.
- Optional session snapshots to record window/app layouts and reopen applications where feasible.
- **Investigate:** compositor protocol/API support before promising positioning or restoration.

### Context-aware status and alerts
- Consolidate actionable events (network loss, low battery, failed background jobs, updates) into a quiet event centre.
- Priority tiers, snooze, history and user-configurable rules.
- Keep informational events separate from urgent notifications.

### Automation / macros
- User-defined actions such as toggling a performance profile, opening a work setup, or launching groups of applications.
- Command-interface integration and optional shortcuts.
- Confirm destructive operations and make executed commands clear.

### Plugin-style widgets and data sources
- A documented interface for small Nythera modules (hardware sensors, media, network status, homelab health).
- Configurable refresh rates and graceful failure/offline states.
- Avoid expensive polling across four monitors; favour event-driven updates.

### Theme / profile modes
- Named visual and behaviour profiles (work, gaming, focus).
- Switch notification policy, animation intensity, layout preferences and optional quick actions together.
- Keep manual overrides and a simple reset path.

## Shared design principles

- Make the experience distinctive without resorting to a generic card grid or gratuitous neon.
- Keep functionality, readability, keyboard navigation and accessibility central.
- Use the existing theme system and existing glass treatment by default.
- Animation should communicate focus, movement or state; respect reduced-motion preferences.
- Performance matters, especially across multiple monitors. Measure before assuming.
- Keep agreed decisions separate from speculative suggestions.

## Before turning any idea into work

1. Check the current Nythera/Noctalia code and existing Wiki.js notes; do not duplicate functionality.
2. Discuss and agree the actual scope, constraints and design.
3. Check technical feasibility, including Umbriel integration where applicable.
4. Write an implementation specification **only after** decisions are made.
5. Keep changes focused, validate affected functionality and record any decisions reached.

## Open questions for later exploration

- What genuinely useful behaviour is missing from the current codebase?
- Which Umbriel APIs can reliably support window previews, workspace intelligence or restoration?
- Where should extension boundaries sit for third-party data sources?
- Which animation patterns should be shared shell-wide, including the existing Umbriel glitch effect?

---
_Updated 8 November 2026. Discussion document, not a committed work queue._
