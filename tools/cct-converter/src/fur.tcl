# Furnace (.fur) module reader.
#
# Supports the song structure of modules written by Furnace 0.6 and newer:
# old INFO/SONG (<240) and INF2/SNG2 (>=240) layouts, with PATN patterns
# (>=157). Instruments are kept as raw blocks for the instrument mappers.
#
# fur::load returns a dict:
#   version  format version
#   name author  system name strings
#   chips    list of {id channels}   (legacy compound ids already flattened)
#   channels total channel count
#   songs    list of dicts: tickRate speed patLen ordLen orders chanNames
#            (orders: list per order of per-channel pattern indices)
#   patterns dict "subsong,chan,idx" -> list of rows; a row is a dict
#            (note ins vol fx {{cmd val} ...}) holding only present cells
#   insBlocks list of raw INS2 bodies (>=127) or old instrument bodies
#   tuning   A-4 tuning

namespace eval fur {
    variable d ""
    variable p 0
    variable ver 0
    # nominal channel counts per chip id
    variable chipChannels {
        1 17 3 4 4 4 5 6 6 5 7 3 71 3 128 3 129 4 130 8 131 6 132 2 133 4 134 1
        135 8 136 3 137 9 138 1 139 3 140 8 141 6 142 16 143 9 144 9 145 18
        146 28 147 1 148 4 149 8 150 4 151 6 152 8 153 1 154 3 155 16 156 6
        157 6 158 16 159 6 160 9 161 5 162 11 163 11 164 20 165 14 166 17
        167 11 168 4 170 4 171 1 172 17 173 2 174 42 175 44 176 16 177 32
        178 10 179 12 180 5 181 8 182 9 183 19 184 8 185 3 186 8 187 8 188 8
        189 11 190 7 191 4 192 1 193 10 194 18 195 10 196 20 197 20 198 2
        199 4 200 3 202 5 203 3 204 4 205 2 206 24 207 16 209 18 212 4 213 6
        214 16 215 2 216 16 217 4 222 19 224 19 226 4 227 4 228 8 229 5
        231 1 240 3 241 5 245 7 252 1 253 8
    }
    # legacy compound ids flatten into {id channels} lists
    variable legacy {
        2   {{131 6} {3 4}}
        8   {{130 8} {155 5}}
        9   {{165 13}}
        66  {{160 9} {3 4}}
        67  {{3 4} {137 9}}
        70  {{6 5} {157 6}}
        73  {{166 16}}
        169 {{155 5}}
    }
}

# --- cursor helpers -------------------------------------------------------

proc fur::u8  {} { variable d; variable p; binary scan $d @${p}cu v; incr p; return $v }
proc fur::s8  {} { variable d; variable p; binary scan $d @${p}c v; incr p; return $v }
proc fur::u16 {} { variable d; variable p; binary scan $d @${p}su v; incr p 2; return $v }
proc fur::u32 {} { variable d; variable p; binary scan $d @${p}iu v; incr p 4; return $v }
proc fur::f32 {} { variable d; variable p; binary scan $d @${p}r v; incr p 4; return $v }
proc fur::skip {n} { variable p; incr p $n }
proc fur::str {} {
    variable d; variable p
    set e [string first "\x00" $d $p]
    set s [encoding convertfrom utf-8 [string range $d $p [expr {$e - 1}]]]
    set p [expr {$e + 1}]
    return $s
}
proc fur::bytes {n} {
    variable d; variable p
    set s [string range $d $p [expr {$p + $n - 1}]]
    incr p $n
    return $s
}

# --- loading --------------------------------------------------------------

proc fur::readFile {path} {
    set fd [open $path rb]
    set data [read $fd]
    close $fd
    if {[string range $data 0 15] ne "-Furnace module-"} {
        # Furnace modules are zlib-compressed on disk
        if {[catch {set data [zlib decompress $data]}]} { error "not a Furnace module" }
    }
    if {[string range $data 0 15] ne "-Furnace module-"} { error "not a Furnace module" }
    return $data
}

proc fur::expandChips {ids} {
    variable chipChannels
    variable legacy
    set out {}
    foreach id $ids {
        if {$id == 0} break
        if {[dict exists $legacy $id]} {
            lappend out {*}[dict get $legacy $id]
        } elseif {[dict exists $chipChannels $id]} {
            lappend out [list $id [dict get $chipChannels $id]]
        } else {
            error [format "unknown Furnace chip id 0x%02x" $id]
        }
    }
    return $out
}

# One subsong's channel-dependent tables. oldLayout: SONG/INFO order data
proc fur::readSongBody {nchan out} {
    variable ver
    set patLen [dict get $out patLen]
    set ordLen [dict get $out ordLen]
    set orders {}
    set raw [bytes [expr {$nchan * $ordLen}]]
    for {set o 0} {$o < $ordLen} {incr o} {
        set row {}
        for {set c 0} {$c < $nchan} {incr c} {
            binary scan $raw @[expr {$c * $ordLen + $o}]cu v
            lappend row $v
        }
        lappend orders $row
    }
    dict set out orders $orders
    set fxCols {}
    for {set c 0} {$c < $nchan} {incr c} { lappend fxCols [u8] }
    dict set out fxCols $fxCols
    skip [expr {$nchan * 2}]  ;# hide + collapse status
    set names {}
    for {set c 0} {$c < $nchan} {incr c} { lappend names [str] }
    dict set out chanNames $names
    set short {}
    for {set c 0} {$c < $nchan} {incr c} { lappend short [str] }
    dict set out chanShort $short
    return $out
}

proc fur::load {path} {
    variable d
    variable p
    variable ver
    set d [readFile $path]
    set p 16
    set ver [u16]
    skip 2
    set infoPtr [u32]
    set p $infoPtr
    set id [bytes 4]
    set size [u32]
    set result [dict create version $ver songs {} patterns [dict create] insBlocks {}]
    if {$id eq "INF2"} {
        loadInf2 result
    } elseif {$id eq "INFO"} {
        loadInfo result
    } else {
        error "unexpected song info block '$id'"
    }
    return $result
}

proc fur::loadInfo {resVar} {
    variable ver
    variable p
    variable d
    upvar 1 $resVar res
    skip 1                          ;# time base
    set speed1 [u8]; set speed2 [u8]
    skip 1                          ;# arp time
    set rate [f32]
    set patLen [u16]; set ordLen [u16]
    skip 2                          ;# highlights
    set insN [u16]; set waveN [u16]; set smpN [u16]; set patN [u32]
    set ids {}
    for {set i 0} {$i < 32} {incr i} { lappend ids [u8] }
    skip 64                         ;# chip volumes + panning
    skip 128                        ;# chip flag pointers
    dict set res name [str]
    dict set res author [str]
    dict set res tuning [f32]
    skip 20                         ;# compat flags (limit slides .. reset note base)
    set insPtrs {}; for {set i 0} {$i < $insN} {incr i} { lappend insPtrs [u32] }
    skip [expr {$waveN * 4}]
    skip [expr {$smpN * 4}]
    set patPtrs {}; for {set i 0} {$i < $patN} {incr i} { lappend patPtrs [u32] }
    set chips [expandChips $ids]
    set nchan 0
    foreach c $chips { incr nchan [lindex $c 1] }
    dict set res chips $chips
    dict set res channels $nchan
    set song [dict create tickRate $rate speed $speed1 speed2 $speed2 patLen $patLen ordLen $ordLen]
    set song [readSongBody $nchan $song]
    set comment [str]
    if {$ver >= 59} { f32 }                      ;# master volume
    if {$ver >= 70} { skip 28 }                  ;# extended compat flags
    if {$ver >= 96} { skip 4 }                   ;# virtual tempo
    set extra {}
    if {$ver >= 95} {
        dict set song name [str]
        str                                      ;# subsong comment
        set nsub [u8]
        skip 3
        for {set i 0} {$i < $nsub} {incr i} { lappend extra [u32] }
    }
    if {$ver >= 103} { for {set i 0} {$i < 6} {incr i} { set m($i) [str] }; dict set res systemName $m(0) }
    if {$ver >= 135} {
        skip [expr {12 * [llength $chips]}]
        set nconn [u32]; skip [expr {$nconn * 4}]
        if {$ver >= 136} { skip 1 }
    }
    if {$ver >= 138} { skip 8 }
    if {$ver >= 139} {
        set slen [u8]
        set spd {}
        for {set i 0} {$i < 16} {incr i} { lappend spd [u8] }
        dict set song speedPattern [lrange $spd 0 [expr {$slen - 1}]]
        set ng [u8]
        for {set i 0} {$i < $ng} {incr i} { skip 17 }
    }
    dict lappend res songs $song
    # additional subsongs
    foreach ptr $extra {
        set p $ptr
        bytes 4; u32
        skip 1; set s1 [u8]; set s2 [u8]; skip 1
        set r [f32]; set pl [u16]; set ol [u16]
        skip 2; skip 4
        set sg [dict create tickRate $r speed $s1 speed2 $s2 patLen $pl ordLen $ol]
        dict set sg name [str]; str
        set sg [readSongBody $nchan $sg]
        if {$ver >= 139} {
            set slen [u8]; set spd {}
            for {set i 0} {$i < 16} {incr i} { lappend spd [u8] }
            dict set sg speedPattern [lrange $spd 0 [expr {$slen - 1}]]
        }
        dict lappend res songs $sg
    }
    set all [lsort -integer [concat $insPtrs $patPtrs $extra]]
    foreach ptr $insPtrs {
        set p $ptr
        set bid [bytes 4]
        set sz [u32]
        if {$sz == 0} {
            # very old modules wrote no block size: run to the next block
            set next [string length $d]
            foreach q $all { if {$q > $ptr} { set next $q; break } }
            set sz [expr {$next - $p}]
        }
        dict lappend res insBlocks [list $bid [bytes $sz]]
    }
    foreach ptr $patPtrs { readPatn res $ptr }
}

proc fur::loadInf2 {resVar} {
    variable ver
    variable p
    upvar 1 $resVar res
    dict set res name [str]
    dict set res author [str]
    dict set res systemName [str]
    str; for {set i 0} {$i < 4} {incr i} { str }
    dict set res tuning [f32]
    skip 1
    f32                                  ;# master volume
    set nchan [u16]
    set nchips [u16]
    set chips {}
    for {set i 0} {$i < $nchips} {incr i} {
        set id [u16]; set n [u16]
        skip 12
        lappend chips [list $id $n]
    }
    dict set res chips $chips
    dict set res channels $nchan
    set nconn [u32]; skip [expr {$nconn * 4}]
    skip 1
    set songPtrs {}; set insPtrs {}; set patPtrs {}
    while 1 {
        set t [u8]
        if {$t == 0} break
        set n [u32]
        set ptrs {}
        for {set i 0} {$i < $n} {incr i} { lappend ptrs [u32] }
        switch $t {
            1 { set songPtrs $ptrs }
            4 { set insPtrs $ptrs }
            7 { set patPtrs $ptrs }
        }
    }
    foreach ptr $songPtrs {
        set p $ptr
        bytes 4; u32
        set rate [f32]
        skip 1; skip 1
        set pl [u16]; set ol [u16]
        skip 2; skip 4
        set slen [u8]
        set spd {}
        for {set i 0} {$i < 16} {incr i} { lappend spd [u16] }
        set sg [dict create tickRate $rate patLen $pl ordLen $ol]
        dict set sg speedPattern [lrange $spd 0 [expr {$slen - 1}]]
        dict set sg name [str]; str
        set sg [readSongBody2 $nchan $sg]
        dict lappend res songs $sg
    }
    foreach ptr $insPtrs {
        set p $ptr
        set bid [bytes 4]
        set sz [u32]
        dict lappend res insBlocks [list $bid [bytes $sz]]
    }
    foreach ptr $patPtrs { readPatn res $ptr }
}

# SNG2 body: same tables as SONG but colors follow the short names.
proc fur::readSongBody2 {nchan out} {
    set out [readSongBody $nchan $out]
    skip [expr {$nchan * 4}]
    return $out
}

proc fur::readPatn {resVar ptr} {
    variable ver
    variable p
    variable d
    upvar 1 $resVar res
    set p $ptr
    if {[string range $d $p [expr {$p + 3}]] eq "PATR"} { return [readPatr res $ptr] }
    bytes 4
    set size [u32]
    set end [expr {$p + $size}]
    set sub [u8]
    set chan [expr {$ver >= 240 ? [u16] : [u8]}]
    set idx [u16]
    if {$ver >= 51} { str }
    set patLen 256
    set rows {}
    set r 0
    while {$p < $end} {
        set m [u8]
        if {$m == 0xff} break
        if {$m & 0x80} { incr r [expr {($m & 0x7f) + 2}]; continue }
        if {$m == 0} { incr r; continue }
        set note ""; set ins ""; set vol ""
        set fxm 0
        set fxm2 0
        if {$m & 0x20} { set fxm [u8] }
        if {$m & 0x40} { set fxm2 [u8] }
        set mask [expr {$fxm | ($fxm2 << 8)}]
        if {$m & 0x08} { set mask [expr {$mask | 0x01}] }
        if {$m & 0x10} { set mask [expr {$mask | 0x02}] }
        if {$m & 1} { set note [u8] }
        if {$m & 2} { set ins [u8] }
        if {$m & 4} { set vol [u8] }
        set fx {}
        for {set e 0} {$e < 8} {incr e} {
            set c ""; set v ""
            if {$mask & (1 << (2 * $e))}     { set c [u8] }
            if {$mask & (1 << (2 * $e + 1))} { set v [u8] }
            if {$c ne "" || $v ne ""} { lappend fx [list $e $c $v] }
        }
        set cell [dict create note $note ins $ins vol $vol fx $fx]
        if {$note == 183} { dict set cell raw [u32] }
        dict set rows $r $cell
        incr r
    }
    dict set res patterns "$sub,$chan,$idx" $rows
}

# Old pattern block (<157): fixed-size 16-bit cells.
proc fur::readPatr {resVar ptr} {
    variable ver
    variable p
    upvar 1 $resVar res
    set p [expr {$ptr + 4}]
    set size [u32]
    set end [expr {$p + $size}]
    set chan [u16]
    set idx [u16]
    set sub [expr {$ver >= 95 ? [u16] : 0}]
    if {$ver < 95} { skip 2 }
    skip 2
    set song [lindex [dict get $res songs] $sub]
    set patLen [dict get $song patLen]
    set fxCols [lindex [dict get $song fxCols] $chan]
    set rows {}
    for {set r 0} {$r < $patLen} {incr r} {
        set note [s16]; set oct [s16]; set ins [s16]; set vol [s16]
        set fx {}
        for {set e 0} {$e < $fxCols} {incr e} {
            set c [s16]; set v [s16]
            if {$c >= 0 || $v >= 0} { lappend fx [list $e [expr {$c < 0 ? "" : $c}] [expr {$v < 0 ? "" : $v}]] }
        }
        set n ""
        if {$note == 100} { set n 180
        } elseif {$note == 101} { set n 181
        } elseif {$note == 102} { set n 182
        } elseif {$note != 0 || $oct != 0} {
            if {$oct > 127} { incr oct -256 }
            set n [expr {60 + $oct * 12 + $note % 12 + ($note == 12 ? 12 : 0)}]
        }
        if {$n eq "" && $ins < 0 && $vol < 0 && ![llength $fx]} continue
        dict set rows $r [dict create note $n ins [expr {$ins < 0 ? "" : $ins}] vol [expr {$vol < 0 ? "" : $vol}] fx $fx]
    }
    dict set res patterns "$sub,$chan,$idx" $rows
}
proc fur::s16 {} { variable d; variable p; binary scan $d @${p}s v; incr p 2; return $v }
