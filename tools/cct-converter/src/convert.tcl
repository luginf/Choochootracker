# Converter registry. Each format module registers itself with
#   converter::register <id> <extensions> <importProc> <exportProc> <title>
# importProc <path> <opts>        -> cct document (see cct.tcl)
# exportProc <doc> <path> <opts>  -> writes the foreign file
# opts is a dict of format options (e.g. channels).
# Either proc may be "" when a direction is not supported.

namespace eval converter {
    variable formats [dict create]
}

proc converter::register {id exts importProc exportProc title} {
    variable formats
    dict set formats $id [dict create exts $exts import $importProc export $exportProc title $title]
}

proc converter::ids {} {
    variable formats
    return [dict keys $formats]
}

proc converter::info {id key} {
    variable formats
    return [dict get $formats $id $key]
}

proc converter::byExtension {path} {
    variable formats
    set ext [string tolower [file extension $path]]
    dict for {id f} $formats {
        if {$ext in [dict get $f exts]} { return $id }
    }
    return ""
}

# Convert in -> out; direction is derived from the file extensions.
proc converter::run {in out {opts {}}} {
    set inExt  [string tolower [file extension $in]]
    set outExt [string tolower [file extension $out]]
    if {$inExt eq ".cct"} {
        set id [byExtension $out]
        if {$id eq "" || [info $id export] eq ""} { error "no export format for '$outExt'" }
        [info $id export] [cct::load $in] $out $opts
    } elseif {$outExt eq ".cct"} {
        set id [byExtension $in]
        if {$id eq "" || [info $id import] eq ""} { error "no import format for '$inExt'" }
        cct::save [[info $id import] $in $opts] $out
    } else {
        error "one side must be a .cct file"
    }
}
