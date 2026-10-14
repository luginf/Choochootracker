# Concatenates src/*.tcl into one runnable file, cct-converter.tcl.
# During development "wish src/main.tcl" works without building.

SRC = src/zipio.tcl src/cct.tcl src/fur.tcl src/convert.tcl src/cli.tcl src/gui.tcl

all: cct-converter.tcl

cct-converter.tcl: $(SRC) src/main.tcl Makefile
	( echo '#!/usr/bin/env wish'; echo 'set ::cct_bundled 1'; \
	  cat $(SRC); grep -v '^#!' src/main.tcl ) > $@
	chmod +x $@

test: all
	tclsh tests/roundtrip.tcl ../../tracker/packaging/common/projects ../..

clean:
	rm -f cct-converter.tcl

.PHONY: all test clean
