# Prints a Tcl snippet embedding src/template.cct (deflate + base64).
set f [open src/template.cct rb]; set d [read $f]; close $f
puts "set ::cct_template_z {[binary encode base64 [zlib compress $d]]}"
