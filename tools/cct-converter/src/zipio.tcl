# Minimal zip reader (stored and deflated entries), enough to open a
# zip-packaged .cct. Uses Tcl's built-in zlib.

namespace eval zipio {}

proc zipio::u16 {data off} { binary scan $data @${off}su v; return $v }
proc zipio::u32 {data off} { binary scan $data @${off}iu v; return $v }

proc zipio::extract {data name} {
    # Find the end of central directory record
    set eocd [string last "PK\x05\x06" $data]
    if {$eocd < 0} { error "not a zip file" }
    set count [u16 $data [expr {$eocd + 10}]]
    set pos   [u32 $data [expr {$eocd + 16}]]
    for {set i 0} {$i < $count} {incr i} {
        if {[string range $data $pos [expr {$pos + 3}]] ne "PK\x01\x02"} break
        set method [u16 $data [expr {$pos + 10}]]
        set csize  [u32 $data [expr {$pos + 20}]]
        set nlen   [u16 $data [expr {$pos + 28}]]
        set xlen   [u16 $data [expr {$pos + 30}]]
        set clen   [u16 $data [expr {$pos + 32}]]
        set lho    [u32 $data [expr {$pos + 42}]]
        set fname  [string range $data [expr {$pos + 46}] [expr {$pos + 45 + $nlen}]]
        if {$fname eq $name} {
            set lnlen [u16 $data [expr {$lho + 26}]]
            set lxlen [u16 $data [expr {$lho + 28}]]
            set start [expr {$lho + 30 + $lnlen + $lxlen}]
            set body  [string range $data $start [expr {$start + $csize - 1}]]
            switch $method {
                0 { return $body }
                8 { return [zlib inflate $body] }
                default { error "unsupported zip method $method" }
            }
        }
        set pos [expr {$pos + 46 + $nlen + $xlen + $clen}]
    }
    error "$name not found in zip"
}
