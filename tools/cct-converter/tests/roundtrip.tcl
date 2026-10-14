# Parse and re-serialize every .cct found under the given directories; the
# output must be identical to the input.
set here [file dirname [file normalize [info script]]]
foreach f {zipio cct} { source [file join $here .. src $f.tcl] }

proc scan_dir {dir} {
    set files [glob -nocomplain -directory $dir *.cct]
    foreach d [glob -nocomplain -type d -directory $dir *] {
        if {[file tail $d] in {.git .tmp build node_modules}} continue
        lappend files {*}[scan_dir $d]
    }
    return $files
}

set fail 0; set n 0
foreach dir $argv {
    foreach f [scan_dir $dir] {
        incr n
        set text [cct::readText $f]
        if {[cct::serialize [cct::parse $text]] ne $text} {
            puts "MISMATCH $f"; incr fail
        }
    }
}
puts "$n files, $fail mismatches"
exit [expr {$fail > 0}]
