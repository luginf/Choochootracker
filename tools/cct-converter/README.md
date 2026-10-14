# cct-converter

Convert ChooChooTracker (`.cct`) projects to and from other tracker formats.
Pure Tcl/Tk 8.6, no dependencies (`apt install tcl tk`). Core and GUI are the
same code; the command line needs only `tclsh`.

```sh
make                                    # builds the single file cct-converter.tcl
tclsh cct-converter.tcl info song.cct
tclsh cct-converter.tcl convert song.cct song.mid
wish  cct-converter.tcl                 # graphical interface
make test                               # .cct parse/serialize round trip on every project in the repo
```

During development `wish src/main.tcl` works without building.

## Layout

- `src/cct.tcl` - tolerant `.cct` reader/writer. The file is kept as verbatim
  lines split on `##`/`###` headings, so unknown or future content survives a
  round trip unchanged; typed views (header, song, chains, phrases,
  instruments) are parsed on demand.
- `src/zipio.tcl` - reads zip-packaged `.cct` (the sample bundle).
- `src/convert.tcl` - format registry; each format registers
  import/export procs with `converter::register`.
- `src/cli.tcl`, `src/gui.tcl`, `src/main.tcl` - front ends.

## Status

Skeleton only: no foreign format is registered yet. Next: Furnace (`.fur`)
import/export, then the MIDI converter now in `tools/cctmidi`.
