# Contributing to ChooChooTracker

Pull requests are welcome.

ChooChooTracker is built with AI-assisted and agent-assisted development in mind. Humans, coding agents, and mixed workflows are all welcome here. The important part is that a change is understandable, tested, and useful to people making music on the device.

## What fits the project

ChooChooTracker is a handheld groovebox and tracker. Keep changes focused on fast music-making with a small, readable codebase. Good contributions improve sequencing, sound design, performance controls, file compatibility, portability, or the practical workflow of the instrument. New sound engines are welcome.

Please do not turn it into a conventional DAW. Features that need a mouse-first workflow, a large arrangement view, or deep studio-style editing probably belong somewhere else. Avoid 1:1 copies of features from other trackers. We aren't a clone even though all trackers sort of look the same.

## Before opening a pull request

- Feel free to visit on the Discord, discuss with other contributors or project owner.
- Keep the change small and focused. One problem per pull request is ideal. 
- Preserve the MIT license wherever possible. Do not copy code or assets with incompatible terms.
- Add or update tests for behavior changes. Run `make -f Makefile.test -j4` from `tracker` for engine or playback changes.
- Update `docs/USER_MANUAL.md` when a user-facing behavior changes.
- Explain what changed, how you tested it, and any limitations in the pull request description.
- Test your changes on real hardware. Windows is good, Portmaster/Android are better. Try to break your own code in controlled environment before users do it in the wild.

## Working with agents

If an agent wrote some or all of a change, that is fine. Review the result before submitting it, keep the diff narrow, and include the same tests and context you would for handwritten code. A clear pull request is more useful than a claim about who wrote it. Do not include agent generated doc such as project architecture, etc. We already have it. Keep your agents.md to yourself.

Thorough human testing is strongly encouraged. Hunt for edge cases, do stupid things. If it breaks, it's probably not ready for PR.

## Getting started

Open an issue if you want to discuss a larger idea before writing code. For a small fix, a pull request with a short description is usually enough.

Thank you for helping keep ChooChooTracker focused, playable, and fun.
