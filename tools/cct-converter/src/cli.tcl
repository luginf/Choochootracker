# Command line front end (tclsh; no Tk needed).

namespace eval cli {}

proc cli::usage {} {
    puts stderr "Usage:
  cct-converter.tcl convert <input> <output>   convert; one side must be .cct
  cct-converter.tcl info <file.cct>            print a project summary
  cct-converter.tcl formats                    list supported formats
  cct-converter.tcl gui                        open the graphical interface
  (no arguments with wish opens the GUI)"
}

proc cli::main {argv} {
    if {[llength $argv] == 0} { usage; return 1 }
    set cmd [lindex $argv 0]
    switch -- $cmd {
        convert {
            if {[llength $argv] != 3} { usage; return 1 }
            lassign [lrange $argv 1 2] in out
            if {[catch {converter::run $in $out} err]} {
                puts stderr "Error: $err"
                return 1
            }
            puts "Converted '$in' -> '$out'"
            return 0
        }
        info {
            if {[llength $argv] != 2} { usage; return 1 }
            if {[catch {cct::load [lindex $argv 1]} doc]} {
                puts stderr "Error: $doc"
                return 1
            }
            dict for {k v} [cct::describe $doc] { puts [format "%-16s %s" $k $v] }
            return 0
        }
        formats {
            puts "cct (.cct)  ChooChooTracker project (native)"
            foreach id [converter::ids] {
                set dirs {}
                if {[converter::info $id import] ne ""} { lappend dirs import }
                if {[converter::info $id export] ne ""} { lappend dirs export }
                puts "$id ([join [converter::info $id exts] ,])  [converter::info $id title] - [join $dirs /]"
            }
            return 0
        }
        default { usage; return 1 }
    }
}
