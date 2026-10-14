#!/usr/bin/env wish
# cct-converter: convert ChooChooTracker (.cct) projects to and from other
# tracker formats. Pure Tcl/Tk, no dependencies. Usable from the command line
# (tclsh) or with a graphical interface (wish).

set ::script_dir [file dirname [file normalize [info script]]]
if {![info exists ::cct_bundled]} {
    foreach f {zipio cct fur convert fur2cct cli gui} { source [file join $::script_dir $f.tcl] }
}

proc main {argv} {
    if {[llength $argv] == 0 || [lindex $argv 0] eq "gui"} {
        if {[catch {package require Tk}]} {
            return [cli::main {help}]
        }
        gui::show
        return -1
    }
    return [cli::main $argv]
}

set rc [main $argv]
if {$rc >= 0} { exit $rc }
