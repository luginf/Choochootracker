# Sampler Edit Window — Implementation Plan (Doc Set)

ChooChooTracker (`am0k161/Choochootracker`, branch `sampler`) — expanding the
**SAMPLE SETTINGS** screen into a fully functional sample editor, inspired by
the Dirtywave M8 sampler workflow but 100% app-native in UI conventions.

## What is in this folder

| File | Purpose |
|---|---|
| `00_MASTER_PLAN.md` | Goals, non-goals, hard constraints, chronological phase roadmap, risk register |
| `01_CODEBASE_ANALYSIS.md` | Deep-dive into the current sampler pipeline: data model, realtime boundary, rendering, input, file I/O, tests |
| `02_PHASE_0_EDITOR_FOUNDATION.md` | Feature 1 — responsive waveform zoom on fine/coarse adjust + editor screen foundation |
| `03_PHASE_1_SELECTION.md` | Feature 2 — independent processing-selection region |
| `04_PHASE_2_PROCESS_UNDO.md` | Feature 3 — PROCESS box (Crop / Normalize / Delete / Silence / Fade In / Fade Out), GO + one-slot undo |
| `05_PHASE_3_SAVE_RENAME.md` | Feature 4 — Save (overwrite), Save As, Rename; WAV writer |
| `06_PHASE_4_HARDENING.md` | Cross-phase QA, docs, platform builds, release checklist |
| `07_AGENT_PROMPTS.md` | Copy-paste phased prompts for a coding agent (prelude + one prompt per phase) |

## How to use with a coding agent

1. Commit this folder into the repo (suggested: `docs/sampler-edit-plan/`).
2. Feed the **prelude + Phase 0 prompt** from `07_AGENT_PROMPTS.md`.
3. After each phase: the agent runs `make -f Makefile.test -j4`, updates
   `docs/USER_MANUAL.md`, commits a single scoped commit.
4. Continue with the next phase prompt. Each phase ends in a compiling,
   testable state, so an agent session can die and be resumed from the doc
   set + git history without losing context.

## Scope decisions (agreed with the maintainer)

- **In scope:** the four features above. Advanced slicing engine stays on the
  roadmap but is explicitly deferred; the plan must not architect against it.
- **Detail level:** phase-level architecture + step outlines; the agent does
  file discovery itself using `01_CODEBASE_ANALYSIS.md` as the map.
- **UI:** strictly app-native (existing screen framework, color scheme, key
  conventions). M8 is functional inspiration only.
- **Dependencies:** no new dependencies recommended; small MIT-licensed libs
  were considered per feature and rejected with rationale (the entire DSP and
  WAV-writer workload is small enough that hand-rolled code is both smaller
  and lower-risk than any third-party integration on this platform stack).
- **QA:** per-feature manual checklists + doctest unit tests for the DSP core.
