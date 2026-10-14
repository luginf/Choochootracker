# Usage: tclsh furdump.tcl file.fur   - print a summary of a Furnace module.
set here [file dirname [file normalize [info script]]]
source [file join $here .. src fur.tcl]
set m [fur::load [lindex $argv 0]]
puts "version [dict get $m version]  name '[dict get $m name]' author '[dict get $m author]'"
puts "chips [dict get $m chips]  channels [dict get $m channels]"
set i 0
foreach s [dict get $m songs] {
    puts "song $i: rate [dict get $s tickRate] patLen [dict get $s patLen] ordLen [dict get $s ordLen] speeds [expr {[dict exists $s speedPattern] ? [dict get $s speedPattern] : "[dict get $s speed]/[dict get $s speed2]"}]"
    incr i
}
puts "patterns [dict size [dict get $m patterns]] instruments [llength [dict get $m insBlocks]]"
set first [lindex [dict keys [dict get $m patterns]] 0]
puts "first pattern $first: [lrange [dict get $m patterns $first] 0 7]"
