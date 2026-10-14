# Concatenates src/*.tcl into one runnable file, cct-converter.tcl.
# During development "wish src/main.tcl" works without building.

SRC = src/zipio.tcl src/cct.tcl src/fur.tcl src/convert.tcl src/fur2cct.tcl src/cli.tcl src/gui.tcl

all: cct-converter.tcl

# The blank project used by the importers is embedded (deflate + base64).
build/template.tcl: src/template.cct embed_template.tcl
	@mkdir -p build
	tclsh embed_template.tcl > $@

cct-converter.tcl: $(SRC) src/main.tcl build/template.tcl Makefile
	( echo '#!/usr/bin/env wish'; echo 'set ::cct_bundled 1'; \
	  cat build/template.tcl $(SRC); grep -v '^#!' src/main.tcl ) > $@
	chmod +x $@

test: all
	tclsh tests/roundtrip.tcl ../../tracker/packaging/common/projects ../..

clean:
	rm -rf cct-converter.tcl build

.PHONY: all test clean
