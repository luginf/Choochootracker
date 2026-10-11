# Native chip and FM instruments

This contribution is based on upstream main `8c87091`. It extracts the current
instrument implementation from the user-accepted R36H personal build. Personal
module discovery, launcher branding, deployment receipts and unrelated sample
stretch/import fixes are not part of this branch. The original development and
deployment history remains on `personal/r36h` and `feature/native-chip-instruments`.

## Included

SID; OPLL and VRC7; AdLib/OPL2 and OPL3; Sega PSG; Game Boy Pulse and Noise;
Genesis/YM2612 and Arcade/YM2151 FM; and DX7/MSFA instruments. Native patches
are owned by the instrument and saved in CNI/CCT files, including song archives.
The contribution retains the tested archive integration for mixed sample/native
songs and embedded wavetable layouts.

The shared preset browser provides bank/category selection and owned audition
previews. DX7 also supports a persistent folder of original SysEx banks.
The checked-in library contains 1,196 preset files, with source manifests,
conversion tools and runtime notices. Entries and unique sounds are different
counts; duplicate source identities and exclusions are recorded by the tools.

Tracker FX use the final compact per-engine list documented in the user manual.
The operator selector has been removed, retained operator commands address
operator 1, and live modulation keeps its explicit operator targets. Preset/range
readouts use native bounds. No retired personal FX translation is included.

## Dependencies and limits

The sources include ymfm, emu76489, gb_apu, the MSFA scalar DX7 component, and
the floooh/chips SID implementation. License/provenance notices accompany each.
Preset content has its own source licenses; consult the packaged notices.
No Dexed application, JUCE, alternate SID model or network importer is added.

SID is a digital/per-cycle 6581-style approximation with a per-note filter,
not a calibrated analog revision model. Native voice allocation and handheld
budgets remain as tested in the accepted personal build. Polyphony, release tails,
other instruments and inserts share the CPU budget; the user manual describes
practical limits. The historical chip report/CSV files retain earlier workload
measurements and their qualifications.

## Review checks

The accepted combined build has host/device validation and user listening behind
it. This isolated contribution builds against current upstream. Five focused cases
passed (927 assertions): compact FX availability, single-command motion recording,
OPLL CNI/CCT persistence, mixed native/sample archives, and DX7 folder rescanning.
The regenerated web build also passed JavaScript syntax and WebAssembly validation.
The extraction was checked with focused tests, without a full suite, stress run,
or new device installation.

### PR #39 MIDI lifetime follow-up (2026-10-06)

Review found that the MIDI tests and standalone converter passed uninitialized
projects into loaders that release the previous instrument data on success.
These callers now initialize an empty project first, matching the tracker UI.
The MIDI API documents that successful imports replace the destination and
failed imports leave it unchanged. No audio-engine behavior changed.

The five MIDI tests pass (49 assertions), including repeated replacement of a
sample-owning project and preservation after missing, malformed, or empty MIDI
input. The converter entry point also passed a MIDI/CCT/MIDI/CCT round trip with
allocation scribbling enabled, linked against the cached core and test support
objects; this was not a standalone release-package build.

### PR #39 FM controls and full-suite follow-up (2026-10-06)

The failing FM test still required all live modulation destinations to appear
in tracker FX. Its assertions now distinguish native modulation metadata from
the intentionally smaller per-engine tracker list, which retains its explicit
availability tests. No removed tracker command was restored.

Additional coverage switches one OPL3 instrument between two-operator,
four-operator, dual-voice, and back to two-operator configurations, checking
actual operator availability. CNI and CCT reload checks preserve each topology,
operator settings, and modulation destinations. The existing instance-level
availability rules passed these checks without synth or runtime changes.

`make -C tracker -f Makefile.test -j4` passed locally: 501 test cases,
112,209,328 assertions, zero failures or skipped cases. This includes the MIDI,
native preset, and existing project/instrument persistence tests. Linux GitHub
CI must still rerun after the branch update; local Docker was unavailable.

### PR #39 Windows portability follow-up (2026-10-06)

GitHub Linux CI and Android passed for `7602f23`. The Windows build found that
the OPLL resampler used the nonstandard `M_PI` macro, which is unavailable in
the Windows C++17 configuration. It now uses a local `constexpr double` with
the same value. The failure reproduced with the MinGW cross-compiler, and the
updated source compiles successfully there. Optimized host object files before
and after this change are byte-identical; the resampling calculation is unchanged.
The full local suite passed again (501 cases, 112,209,328 assertions), and the
regenerated web bundle passed JavaScript syntax and WebAssembly validation.

### PR #39 Windows SID build follow-up (2026-10-06)

The next Windows CI run progressed past OPLL and found the same optional CRT
math constant in the pinned SID implementation. Native Windows and Docker
cross-build flags now enable `_USE_MATH_DEFINES`; the vendored source and sound
calculations remain unchanged. A complete local Windows executable cross-build
passed with MinGW GCC 16.2 and the repository's SDL2 2.32.6 development package.
This validates compilation and linking, not Windows playback or release packaging.
Linux tests, web-contract, and Android passed on the preceding `5e73b36` commit;
current-head GitHub checks must rerun after this build-configuration update.

### PR #39 Windows archive-name follow-up (2026-10-06)

Windows CI compiled and linked `8807037` successfully, then failed while creating
the ZIP because the PR ref `39/merge` became part of the output path. Packaging
now replaces ref slashes with hyphens and quotes the archive filename. The exact
workflow packaging commands passed local ZIP creation/content checks for PR refs,
branch refs, release tags, and the manual fallback. Application and audio code
are unchanged; Windows CI still needs to confirm the complete package upload.

### Maintainer preview and Windows test follow-up (2026-10-06)

The maintainer's waveform previews (`ec50dd1`), native FX grouping (`0dafab7`),
and SID static preview without a DSP voice (`5a2c9f2`) are retained. Review of
the shared preview redraw found that SID bank/preset
fields also use it, although SID has no optional FM amp. The helper now checks
for an amp before drawing its envelope and keeps SID's waveform at its existing
row when the persistent waveform setting is enabled. The original null access
was reproduced with UndefinedBehaviorSanitizer.

The regular test target now includes the real preview helper, with graphics and
envelope-overlay boundaries mocked. Its regression case covers SID and FM,
both persistent-waveform settings, enabled/bypassed FM overlays, and preservation
of the instrument data. `Makefile.test` also enables `_USE_MATH_DEFINES` for
Windows, matching the application build without modifying the vendored SID core.

Validation: all 502 local test cases passed, including the focused preview
regression's 36 assertions. The full Windows test executable cross-compiled and
linked with MinGW; it was not run on
Windows locally. The checked-in web bundle was regenerated, and its JavaScript
syntax and WebAssembly binary validation passed. Audio engine code and the
simplified tracker FX list are unchanged.
