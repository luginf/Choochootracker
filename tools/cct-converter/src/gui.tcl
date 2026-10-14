# Tk front end: pick an input and an output file, convert, read the log.

namespace eval gui {
    variable in ""
    variable out ""
}

proc gui::browseIn {} {
    variable in
    variable out
    set exts {.cct}
    foreach id [converter::ids] {
        if {[converter::info $id import] ne ""} { lappend exts {*}[converter::info $id exts] }
    }
    set types [list [list "Supported files" $exts] [list "All files" *]]
    set f [tk_getOpenFile -title "Input file" -filetypes $types]
    if {$f eq ""} return
    set in $f
    if {$out eq ""} { suggestOut }
}

proc gui::suggestOut {} {
    variable in
    variable out
    if {[string tolower [file extension $in]] eq ".cct"} {
        set ids [converter::ids]
        foreach id $ids {
            if {[converter::info $id export] ne ""} {
                set out [file rootname $in][lindex [converter::info $id exts] 0]
                return
            }
        }
    } else {
        set out [file rootname $in].cct
    }
}

proc gui::browseOut {} {
    variable in
    variable out
    set f [tk_getSaveFile -title "Output file" -initialfile [file tail $out]]
    if {$f ne ""} { set out $f }
}

proc gui::log {msg} {
    .log configure -state normal
    .log insert end "$msg\n"
    .log see end
    .log configure -state disabled
}

proc gui::convert {} {
    variable in
    variable out
    if {$in eq "" || $out eq ""} { log "Choose an input and an output file."; return }
    if {[catch {converter::run $in $out} err]} {
        log "Error: $err"
    } else {
        log "Converted [file tail $in] -> [file tail $out]"
    }
}

proc gui::show {} {
    wm title . "CCT Converter"
    ttk::frame .f -padding 8
    ttk::label .f.li -text "Input"
    ttk::entry .f.ei -textvariable gui::in -width 50
    ttk::button .f.bi -text "Browse..." -command gui::browseIn
    ttk::label .f.lo -text "Output"
    ttk::entry .f.eo -textvariable gui::out -width 50
    ttk::button .f.bo -text "Browse..." -command gui::browseOut
    ttk::button .f.go -text "Convert" -command gui::convert
    grid .f.li .f.ei .f.bi -sticky ew -pady 2
    grid .f.lo .f.eo .f.bo -sticky ew -pady 2
    grid .f.go -column 1 -sticky w -pady 6
    grid columnconfigure .f 1 -weight 1
    text .log -height 8 -width 70 -state disabled
    pack .f -fill x
    pack .log -fill both -expand 1 -padx 8 -pady 8
}
