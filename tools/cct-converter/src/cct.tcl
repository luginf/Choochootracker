# ChooChooTracker .cct reader/writer.
#
# The format is Markdown-like text that grows over time (new instrument types,
# FX, header keys). To stay tolerant the file is kept as a tree of verbatim
# lines, split on "##" and "###" headings, so unknown content survives a
# load/save round trip byte for byte. Typed views (header, song, chains,
# phrases, instruments) are parsed on demand from that tree.
#
# Document layout (a Tcl dict):
#   preamble  list of lines before the first "## " heading
#   sections  list of {name lines subs}; subs is a list of {heading lines}
#             where "lines" are the verbatim lines under the heading.

namespace eval cct {
    variable error ""
}

# --- low level -------------------------------------------------------------

proc cct::readText {path} {
    set fd [open $path rb]
    set data [read $fd]
    close $fd
    # A zip-packaged .cct holds project.cct next to samples/NNN.wav
    if {[string range $data 0 3] eq "PK\x03\x04"} {
        set data [zipio::extract $data project.cct]
    }
    return [encoding convertfrom utf-8 $data]
}

proc cct::parse {text} {
    set preamble {}
    set sections {}
    set curSection {}   ;# {name lines subs}
    set curSub {}       ;# {heading lines}
    set where pre
    foreach line [split $text "\n"] {
        if {[string match "## *" $line]} {
            if {$where eq "sub"} { lset curSection 2 end+1 $curSub }
            if {$where ne "pre"} { lappend sections $curSection }
            set curSection [list [string range $line 3 end] {} {}]
            set where sec
        } elseif {[string match "### *" $line] && $where ne "pre"} {
            if {$where eq "sub"} { lset curSection 2 end+1 $curSub }
            set curSub [list [string range $line 4 end] {}]
            set where sub
        } else {
            switch $where {
                pre { lappend preamble $line }
                sec { lset curSection 1 end+1 $line }
                sub { lset curSub 1 end+1 $line }
            }
        }
    }
    if {$where eq "sub"} { lset curSection 2 end+1 $curSub }
    if {$where ne "pre"} { lappend sections $curSection }
    return [dict create preamble $preamble sections $sections]
}

proc cct::load {path} {
    return [parse [readText $path]]
}

proc cct::serialize {doc} {
    set out [dict get $doc preamble]
    foreach sec [dict get $doc sections] {
        lassign $sec name lines subs
        lappend out "## $name"
        lappend out {*}$lines
        foreach sub $subs {
            lassign $sub heading slines
            lappend out "### $heading"
            lappend out {*}$slines
        }
    }
    return [join $out "\n"]
}

proc cct::save {doc path} {
    set fd [open $path wb]
    puts -nonewline $fd [encoding convertto utf-8 [serialize $doc]]
    close $fd
}

# --- typed views -----------------------------------------------------------

# Header "- Key: value" lines. Repeated keys (Insert, MIDI CC...) are kept as
# a list of values in file order.
proc cct::header {doc} {
    set h [dict create]
    foreach line [dict get $doc preamble] {
        if {[regexp {^- ([^:]+): ?(.*)$} $line -> k v]} {
            dict lappend h $k $v
        }
    }
    return $h
}

proc cct::headerValue {doc key {default ""}} {
    set h [header $doc]
    if {[dict exists $h $key]} { return [lindex [dict get $h $key] 0] }
    return $default
}

proc cct::section {doc name} {
    foreach sec [dict get $doc sections] {
        if {[lindex $sec 0] eq $name} { return $sec }
    }
    return {}
}

# Lines inside the first ``` fence of a list of lines.
proc cct::fenced {lines} {
    set out {}
    set in 0
    foreach l $lines {
        if {[string match "```*" $l]} {
            if {$in} break
            set in 1
        } elseif {$in} {
            lappend out $l
        }
    }
    return $out
}

# Song: list of rows, each a list of chain ids ("" for an empty cell).
proc cct::song {doc} {
    set rows {}
    foreach l [fenced [lindex [section $doc Song] 1]] {
        set row {}
        foreach c [split $l " "] {
            if {$c eq ""} continue
            lappend row [expr {$c eq "--" ? "" : [string trimright $c *]}]
        }
        lappend rows $row
    }
    return $rows
}

# Numbered subsections ("Chain 3", "Phrase 1F"...) as {index {fenced lines}}.
proc cct::numbered {doc sectionName prefix} {
    set out {}
    foreach sub [lindex [section $doc $sectionName] 2] {
        lassign $sub heading lines
        if {[regexp "^$prefix (\[0-9A-Fa-f\]+)" $heading -> idx]} {
            lappend out [scan $idx %x] [fenced $lines]
        }
    }
    return $out
}

# Chains: dict index -> list of {phrase transpose}; phrase "" ends the chain.
proc cct::chains {doc} {
    set out [dict create]
    foreach {idx lines} [numbered $doc Chains Chain] {
        set rows {}
        foreach l $lines {
            lassign $l p t
            lappend rows [list [expr {[string match -* $p] ? "" : [scan $p %x]}] [scan $t %x]]
        }
        dict set out $idx $rows
    }
    return $out
}

# Phrases: dict index -> list of rows; row = {note inst vol {fx val} x3}.
# Cells keep their text form ("---" / "--" for empty); callers decode.
proc cct::phrases {doc} {
    set out [dict create]
    foreach {idx lines} [numbered $doc Phrases Phrase] {
        set rows {}
        foreach l $lines { lappend rows [list {*}$l] }
        dict set out $idx $rows
    }
    return $out
}

# Instruments: dict index -> dict of "- Key: value" properties plus the raw
# line list under key _lines (for engine-specific "Chip data" blocks).
proc cct::instruments {doc} {
    set out [dict create]
    foreach sub [lindex [section $doc Instruments] 2] {
        lassign $sub heading lines
        if {![regexp {^Instrument ([0-9A-Fa-f]+)} $heading -> idx]} continue
        set props [dict create _lines $lines]
        foreach l $lines {
            if {[regexp {^- ([^:]+): ?(.*)$} $l -> k v]} { dict set props $k $v }
        }
        dict set out [scan $idx %x] $props
    }
    return $out
}

proc cct::describe {doc} {
    set n [dict create]
    dict set n title  [headerValue $doc Title]
    dict set n author [headerValue $doc Author]
    dict set n rate   [headerValue $doc "Frame rate"]
    dict set n chips  [headerValue $doc "Chips count"]
    dict set n rows   [llength [song $doc]]
    dict set n chains [dict size [chains $doc]]
    dict set n phrases [dict size [phrases $doc]]
    set types {}
    dict for {i p} [instruments $doc] { dict incr types [dict get $p Type] }
    dict set n instruments [dict size [instruments $doc]]
    dict set n instrumentTypes $types
    return $n
}
