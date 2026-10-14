# Furnace (.fur) -> ChooChooTracker (.cct) import.
#
# Scope: YM2612 (Genesis FM, including the extended-mode channels played as
# plain FM) and SN76489 (Sega PSG). Other chips are reported and skipped.
# ChooChooTracker has 8 tracks, so a subset of Furnace channels is chosen
# (option -channels, default: the first 8 supported ones).
# Furnace patterns are cut into 16-row phrases; a pattern becomes a chain.

namespace eval fur2cct {
    variable warnings {}
    # Furnace stores DT as a linear index (3 = no detune); the YM2612
    # register, and the .cct, hold the hardware value.
    variable dtTable {7 6 5 0 1 2 3 4}
    variable emptyPhrase 1023
}

proc fur2cct::warn {msg} {
    variable warnings
    if {$msg ni $warnings} { lappend warnings $msg }
}

proc fur2cct::template {} {
    if {[info exists ::cct_template_z]} {
        return [encoding convertfrom utf-8 [zlib decompress [binary decode base64 $::cct_template_z]]]
    }
    set fd [open [file join $::script_dir template.cct] rb]
    set t [encoding convertfrom utf-8 [read $fd]]
    close $fd
    return $t
}

# --- Furnace side ---------------------------------------------------------

# Channel table: list of {kind chipId localIndex}; kind is fm, psg, noise
# or "" when the chip is not supported.
proc fur2cct::channelKinds {mod} {
    set out {}
    foreach chip [dict get $mod chips] {
        lassign $chip id n
        for {set i 0} {$i < $n} {incr i} {
            switch $id {
                131 - 160 { set k fm }
                189       { set k [expr {$i < 9 ? "fm" : ""}] }
                190       { set k [expr {$i < 6 ? "fm" : ""}] }
                3         { set k [expr {$i == 3 ? "noise" : "psg"}] }
                default   { set k "" }
            }
            lappend out [list $k $id $i]
        }
    }
    return $out
}

# Decode an instrument block {id body}: INS2 (>=127) or old INST (<127).
# Returns dict: type name fm
proc fur2cct::parseInstrument {entry} {
    lassign $entry bid block
    if {$bid eq "INST"} { return [parseOldInstrument $block] }
    binary scan $block susu iver itype
    set res [dict create type $itype name "" version $iver]
    set pos 4
    set len [string length $block]
    while {$pos + 4 <= $len} {
        set code [string range $block $pos [expr {$pos + 1}]]
        binary scan $block @[expr {$pos + 2}]su flen
        set body [string range $block [expr {$pos + 4}] [expr {$pos + 3 + $flen}]]
        incr pos [expr {4 + $flen}]
        switch -- $code {
            EN { break }
            NA { dict set res name [encoding convertfrom utf-8 [lindex [split $body "\x00"] 0]] }
            FM { dict set res fm [parseFM $body $iver] }
        }
    }
    return $res
}

proc fur2cct::parseOldInstrument {block} {
    binary scan $block sucu iver itype
    set res [dict create type $itype name "" version $iver]
    set e [string first "\x00" $block 4]
    dict set res name [encoding convertfrom utf-8 [string range $block 4 [expr {$e - 1}]]]
    if {$itype != 1} { return $res }
    set b {}
    binary scan [string range $block [expr {$e + 1}] [expr {$e + 8 + 4 * 32}]] cu* b
    set alg [lindex $b 0]; set fb [lindex $b 1]; set fms [lindex $b 2]; set ams [lindex $b 3]
    set pos 8
    set ops {}
    set enabled 0
    for {set i 0} {$i < 4} {incr i} {
        lassign [lrange $b $pos [expr {$pos + 21}]] am ar dr mult rr sl tl dt2 rs dt d2r ssg dam dvb egt ksl sus vib ws ksr en kvs
        incr pos 32
        lappend ops [dict create mul $mult dt $dt tl $tl ar $ar rs $rs dr $dr am $am d2r $d2r rr $rr sl $sl ssg [expr {$ssg & 15}]]
        # old files store operator enable in logical-slot order 1,3,2,4 like INS2
        if {$iver < 114 || $en} { set enabled [expr {$enabled | (1 << $i)}] }
    }
    dict set res fm [dict create alg $alg fb $fb fms $fms ams $ams enabled $enabled ops $ops]
    return $res
}

proc fur2cct::parseFM {body iver} {
    set b {}
    binary scan $body cu* b
    set flags [lindex $b 0]
    set opCount [expr {$flags & 15}]
    set enabled [expr {$flags >> 4}]
    set b1 [lindex $b 1]; set b2 [lindex $b 2]
    set pos [expr {$iver >= 224 ? 5 : 4}]
    set ops {}
    for {set i 0} {$i < $opCount} {incr i} {
        set o [lrange $b $pos [expr {$pos + 7}]]
        incr pos 8
        lassign $o o0 o1 o2 o3 o4 o5 o6 o7
        lappend ops [dict create \
            mul [expr {$o0 & 15}] dt [expr {($o0 >> 4) & 7}] \
            tl [expr {$o1 & 127}] \
            ar [expr {$o2 & 31}] rs [expr {$o2 >> 6}] \
            dr [expr {$o3 & 31}] am [expr {$o3 >> 7}] \
            d2r [expr {$o4 & 31}] \
            rr [expr {$o5 & 15}] sl [expr {$o5 >> 4}] \
            ssg [expr {$o6 & 15}]]
    }
    return [dict create alg [expr {($b1 >> 4) & 7}] fb [expr {$b1 & 7}] \
        fms [expr {$b2 & 7}] ams [expr {($b2 >> 3) & 3}] enabled $enabled ops $ops]
}

# --- .cct side --------------------------------------------------------------

proc fur2cct::genesisInstrumentLines {name fm} {
    variable dtTable
    set ops [dict get $fm ops]
    # Furnace stores operators in register slot order 1,3,2,4; the tracker
    # uses logical order 1,2,3,4.
    set slot {0 2 1 3}
    set lines {}
    lappend lines "- Name: [string range $name 0 14]" "- Type: 25" "- Table speed: 1" \
        "- Volume: 255" "- Transpose: 1" "- Modulation:" \
        "- Mod1: 0,0,0,0,0,0,0,0" "- Mod2: 1,0,0,0,0,0,0,0" \
        "- Mod3: 2,0,0,0,0,0,0,0" "- Mod4: 2,0,0,0,0,0,0,0" "- Chip data:"
    set mask 0
    set e [dict get $fm enabled]
    # enabled bits 4..7 of the flags cover operators 1,3,2,4 (logical 0,2,1,3)
    foreach {bit logical} {0 0 1 2 2 1 3 3} {
        if {$e & (1 << $bit)} { set mask [expr {$mask | (1 << $logical)}] }
    }
    if {$mask == 0} { set mask 15 }
    lappend lines "- FourOp base: 1,[dict get $fm alg],[dict get $fm fb],3,[dict get $fm ams],[dict get $fm fms],0,0,0,0,0,$mask,0,0,0"
    lappend lines "- FourOp name: [string range $name 0 14]"
    for {set i 0} {$i < 4} {incr i} {
        set o [lindex $ops [lindex $slot $i]]
        if {$o eq ""} { set o [dict create mul 1 dt 3 tl 127 ar 31 rs 0 dr 0 am 0 d2r 0 rr 15 sl 0 ssg 0] }
        dict with o {
            lappend lines "- FourOp op$i: $mul,[lindex $dtTable $dt],$tl,$rs,$ar,$dr,$d2r,$rr,$sl,$ssg,0,$am"
        }
    }
    return $lines
}

proc fur2cct::psgInstrumentLines {name noise} {
    return [list "- Name: [string range $name 0 14]" "- Type: 21" "- Table speed: 1" \
        "- Volume: 255" "- Transpose: 1" "- Modulation:" \
        "- Mod1: 0,0,0,0,0,0,0,0" "- Mod2: 1,0,0,0,0,0,0,0" \
        "- Mod3: 2,0,0,0,0,0,0,0" "- Mod4: 2,0,0,0,0,0,0,0" "- Chip data:" \
        "- Simple chip: 1,0,[expr {$noise ? 1 : 0}],0,0,0,15,0,0,0,0,0,0"]
}

proc fur2cct::fenceBlock {rows} {
    return [concat [list "" "```"] $rows [list "```" ""]]
}

proc fur2cct::noteNames {tmpl} {
    set names {}
    foreach l [cct::fenced [lindex [cct::section $tmpl "Pitch table"] 1]] {
        lappend names [lindex $l 0]
    }
    return $names
}

# --- conversion -------------------------------------------------------------

proc fur2cct::import {path {opts {}}} {
    variable warnings
    variable emptyPhrase
    set warnings {}
    set mod [fur::load $path]
    return [convert $mod $opts]
}

proc fur2cct::convert {mod opts} {
    variable warnings
    variable emptyPhrase
    set tmpl [cct::parse [template]]
    set names [noteNames $tmpl]
    set kinds [channelKinds $mod]
    set songIdx [expr {[dict exists $opts song] ? [dict get $opts song] : 0}]
    set song [lindex [dict get $mod songs] $songIdx]
    if {$song eq ""} { error "no subsong $songIdx" }

    # channel selection
    if {[dict exists $opts channels] && [dict get $opts channels] ne ""} {
        set chans [split [dict get $opts channels] ,]
    } else {
        set chans {}
        for {set i 0} {$i < [llength $kinds]} {incr i} {
            if {[lindex $kinds $i 0] ne ""} { lappend chans $i }
        }
        if {[llength $chans] > 8} {
            warn "module has [llength $chans] channels, keeping the first 8 (use -channels to choose)"
            set chans [lrange $chans 0 7]
        }
    }
    foreach c $chans {
        if {$c >= [llength $kinds]} { error "channel $c does not exist" }
        if {[lindex $kinds $c 0] eq ""} { warn "channel $c: chip [lindex $kinds $c 1] is not supported (notes kept, default sound)" }
    }
    if {[llength $chans] == 0} {
        set ids {}
        foreach c [dict get $mod chips] { lappend ids [format "0x%02X" [lindex $c 0]] }
        error "no supported channel: this module uses chip(s) [join $ids {, }]; only YM2612 (Genesis FM) and SN76489 (Sega PSG) are converted so far"
    }
    if {[llength $chans] > 8} { error "at most 8 channels can be converted" }

    # instruments
    set furIns {}
    set idx 0
    foreach blk [dict get $mod insBlocks] {
        dict set furIns $idx [parseInstrument $blk]
        incr idx
    }
    set insSlot [dict create]       ;# "furIns,kind" -> slot
    set insLines [dict create]      ;# slot -> property lines
    set nextSlot 0
    set slotFor {}
    proc ::fur2cct::slotFor {fi kind} {
        upvar 1 furIns furIns insSlot insSlot insLines insLines nextSlot nextSlot
        set key "$fi,$kind"
        if {[dict exists $insSlot $key]} { return [dict get $insSlot $key] }
        if {$nextSlot > 127} { warn "more than 128 instruments needed, extra ones use instrument 7F"; return 127 }
        set slot $nextSlot
        incr nextSlot
        set ins [expr {[dict exists $furIns $fi] ? [dict get $furIns $fi] : {}}]
        set nm ""
        if {$ins ne ""} { set nm [dict get $ins name] }
        if {$nm eq ""} { set nm [format "Ins %02X" [expr {$fi < 0 ? 0 : $fi}]] }
        if {$kind eq "fm" && $ins ne "" && [dict get $ins type] == 1 && [dict exists $ins fm]} {
            set lines [genesisInstrumentLines $nm [dict get $ins fm]]
        } elseif {$kind eq "fm"} {
            if {$fi >= 0} { warn "instrument $fi (Furnace type [expr {$ins eq "" ? "?" : [dict get $ins type]}]) is not a YM2612 FM instrument, default FM patch used" }
            set lines [genesisInstrumentLines $nm [defaultFM]]
        } else {
            set lines [psgInstrumentLines $nm [expr {$kind eq "noise"}]]
        }
        dict set insSlot $key $slot
        dict set insLines $slot $lines
        return $slot
    }

    # phrases, chains, song
    set patLen [dict get $song patLen]
    set orders [dict get $song orders]
    set phraseIds [dict create]      ;# text -> id
    set phraseText [dict create]     ;# id -> rows
    set nextPhrase 0
    set chainIds [dict create]
    set chainRows [dict create]
    set nextChain 0
    set songRows {}
    set nTracks [llength $chans]
    set phrasesPerPattern [expr {($patLen + 15) / 16}]
    if {$phrasesPerPattern > 16} { error "pattern length $patLen too long" }
    if {$patLen % 16} { warn "pattern length $patLen is not a multiple of 16: rows are padded" }
    set clamped 0
    set dropped [dict create]
    set grooves [dict create]   ;# speed -> groove id (0Fxx), id 0 is the song groove

    foreach order $orders {
        set cells {}
        foreach c $chans {
            lassign [lindex $kinds $c] kind chipId local
            set kindName [expr {$kind eq "" ? "psg" : $kind}]
            set pat [lindex $order $c]
            set key "$songIdx,$c,$pat"
            set rows [expr {[dict exists [dict get $mod patterns] $key] ? [dict get [dict get $mod patterns] $key] : {}}]
            set chain {}
            for {set ph 0} {$ph < $phrasesPerPattern} {incr ph} {
                set lines {}
                set blank 1
                for {set r 0} {$r < 16} {incr r} {
                    set row [expr {$ph * 16 + $r}]
                    set note "---"; set ins "--"; set vol "--"; set fxText "--- 00 --- 00 --- 00"
                    if {$row < $patLen && [dict exists $rows $row]} {
                        set cell [dict get $rows $row]
                        set n [dict get $cell note]
                        if {$n ne ""} {
                            if {$n >= 180 && $n <= 182} {
                                set note OFF
                            } elseif {$n < 180} {
                                set ix [expr {$n - 60}]
                                if {$ix < 0} { set ix 0; incr clamped }
                                if {$ix >= [llength $names]} { set ix [expr {[llength $names] - 1}]; incr clamped }
                                set note [lindex $names $ix]
                            }
                        }
                        set i [dict get $cell ins]
                        if {$i ne ""} { set ins [format %02X [slotFor $i $kindName]] }
                        set v [dict get $cell vol]
                        if {$v ne ""} {
                            if {$kind eq "fm"} { set v [expr {min($v, 127)}] } else { set v [expr {int($v * 127.0 / 15 + 0.5)}] }
                            set curVol($c) [expr {min($v, 127)}]
                        }
                        # The tracker has no persistent channel volume, so a
                        # note row without one would be silent: carry the
                        # last Furnace volume (initially full) on every note.
                        if {$note ne "---" && $note ne "OFF" || $v ne ""} {
                            set vol [format %02X [expr {[info exists curVol($c)] ? $curVol($c) : 127}]]
                        }
                        set fxs {}
                        foreach f [dict get $cell fx] {
                            lassign $f col cmd val
                            if {$cmd eq ""} continue
                            if {$val eq ""} { set val 0 }
                            set m [mapEffect $cmd $val grooves]
                            if {$m eq ""} {
                                dict incr dropped [format %02X $cmd]
                            } elseif {[llength $fxs] < 3} {
                                lappend fxs $m
                            } else {
                                dict incr dropped extra
                            }
                        }
                        while {[llength $fxs] < 3} { lappend fxs "--- 00" }
                        set fxText [join $fxs " "]
                        set blank 0
                    }
                    lappend lines "$note $ins $vol $fxText"
                }
                set text [join $lines \n]
                if {$blank} {
                    set pid $emptyPhrase
                } elseif {[dict exists $phraseIds $text]} {
                    set pid [dict get $phraseIds $text]
                } else {
                    if {$nextPhrase >= $emptyPhrase} { error "too many phrases" }
                    set pid $nextPhrase
                    incr nextPhrase
                    dict set phraseIds $text $pid
                    dict set phraseText $pid $lines
                }
                lappend chain $pid
            }
            if {[dict exists $chainIds $chain]} {
                set cid [dict get $chainIds $chain]
            } else {
                if {$nextChain > 254} { error "too many chains" }
                set cid $nextChain
                incr nextChain
                dict set chainIds $chain $cid
                dict set chainRows $cid $chain
            }
            lappend cells [format %02X $cid]
        }
        while {[llength $cells] < 8} { lappend cells -- }
        lappend songRows [join $cells " "]
    }
    if {[llength $songRows] > 256} { error "more than 256 orders" }
    if {$clamped} { warn "$clamped notes were outside the tracker's range and were clamped" }
    if {[dict size $dropped]} {
        set l {}
        dict for {c n} $dropped { lappend l "$c ($n)" }
        warn "effects not converted (Furnace command, count): [join $l {, }]"
    }

    # groove from the speed pattern
    set speeds [expr {[dict exists $song speedPattern] ? [dict get $song speedPattern] : [list [dict get $song speed] [dict get $song speed2]]}]
    set grooveRows {}
    for {set i 0} {$i < 16} {incr i} {
        set s [lindex $speeds $i]
        if {$s eq ""} {
            lappend grooveRows "--"
        } else {
            lappend grooveRows [format %02X [expr {min(255, max(1, $s))}]]
        }
    }

    # assemble the document
    set doc $tmpl
    set pre {}
    set title [string range [dict get $mod name] 0 23]
    set author [string range [dict get $mod author] 0 23]
    foreach l [dict get $doc preamble] {
        if {[string match "- Title:*" $l]} { set l "- Title: $title" }
        if {[string match "- Author:*" $l]} { set l "- Author: $author" }
        if {[string match "- Frame rate:*" $l]} { set l [format "- Frame rate: %f" [dict get $song tickRate]] }
        lappend pre $l
    }
    dict set doc preamble $pre
    set secs {}
    foreach s [dict get $doc sections] {
        lassign $s name lines subs
        switch $name {
            Song {
                set s [list Song [fenceBlock $songRows] {}]
            }
            Chains {
                set subs {}
                foreach cid [lsort -integer [dict keys $chainRows]] {
                    set rows {}
                    foreach pid [dict get $chainRows $cid] { lappend rows [format "%03X 00" $pid] }
                    while {[llength $rows] < 16} { lappend rows "--- 00" }
                    lappend subs [list [format "Chain %X" $cid] [fenceBlock $rows]]
                }
                set s [list Chains {""} $subs]
            }
            Grooves {
                set gsubs [list [list "Groove 0" [fenceBlock $grooveRows]]]
                dict for {spd gid} $grooves {
                    set rows [list [format %02X $spd]]
                    while {[llength $rows] < 16} { lappend rows "--" }
                    lappend gsubs [list [format "Groove %X" $gid] [fenceBlock $rows]]
                }
                set s [list Grooves {""} $gsubs]
            }
            Phrases {
                set subs {}
                foreach pid [lsort -integer [dict keys $phraseText]] {
                    lappend subs [list [format "Phrase %X" $pid] [fenceBlock [dict get $phraseText $pid]]]
                }
                set s [list Phrases {""} $subs]
            }
            Instruments {
                set subs {}
                foreach slot [lsort -integer [dict keys $insLines]] {
                    lappend subs [list [format "Instrument %X" $slot] [concat {""} [dict get $insLines $slot] {""}]]
                }
                set s [list Instruments {""} $subs]
            }
        }
        lappend secs $s
    }
    dict set doc sections $secs
    return $doc
}

# Furnace effect -> "NAM vv" cct FX cell, or "" when there is no equivalent.
proc fur2cct::mapEffect {cmd val groovesVar} {
    upvar 1 $groovesVar grooves
    switch $cmd {
        0   { if {$val == 0} { return "" }; return [format "ARP %02X" $val] }
        4   { return [format "PVB %02X" $val] }
        8   {
            set l [expr {$val >> 4}]; set r [expr {$val & 15}]
            if {$l == 0 && $r == 0} { return "" }
            return [format "PAN %02X" [expr {max(0, min(255, int(128 + ($r - $l) * 128.0 / 15)))}]]
        }
        15  {
            if {$val == 0 || $val > 31} { return "" }
            if {![dict exists $grooves $val]} {
                if {[dict size $grooves] >= 31} { return "" }
                dict set grooves $val [expr {[dict size $grooves] + 1}]
            }
            return [format "GGR %02X" [dict get $grooves $val]]
        }
        236 { return [format "OFF %02X" $val] }
        237 { return [format "DEL %02X" $val] }
    }
    return ""
}

proc fur2cct::defaultFM {} {
    set op {mul 1 dt 3 tl 30 ar 31 rs 1 dr 10 am 0 d2r 3 rr 7 sl 5 ssg 0}
    return [dict create alg 4 fb 0 fms 0 ams 0 enabled 15 ops [list $op $op $op $op]]
}

converter::register furnace {.fur} fur2cct::import "" "Furnace Tracker module"
