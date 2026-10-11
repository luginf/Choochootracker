# Standalone CLI converter between ChooChooTracker projects (.cct) and
# Standard MIDI Files. No SDL2/graphics dependency: links chipnomad_lib
# (the same source set proven to build without audio/UI deps by
# tracker/Makefile.test) plus the MIDI-specific import/export code.

CXX = c++
CFLAGS = -std=c++17 -Wall -O2 -DTEST

TRACKER_DIR = ../../tracker
CHIPNOMAD_DIR = ../../chipnomad_lib

INCLUDES = -I$(TRACKER_DIR) -I$(TRACKER_DIR)/src -I$(TRACKER_DIR)/src/corelib \
           -I$(TRACKER_DIR)/src/screens -I$(TRACKER_DIR)/src/import \
           -I$(CHIPNOMAD_DIR) -I$(CHIPNOMAD_DIR)/chips -I$(CHIPNOMAD_DIR)/external/ayumi \
           -I$(CHIPNOMAD_DIR)/external/mutable -I$(CHIPNOMAD_DIR)/external/open303/Source/DSPCode

CHIPNOMAD_LIB_SOURCES = $(wildcard $(CHIPNOMAD_DIR)/*.cpp) \
                        $(wildcard $(CHIPNOMAD_DIR)/chips/*.cpp) \
                        $(wildcard $(CHIPNOMAD_DIR)/synth/*.cpp) \
                        $(wildcard $(CHIPNOMAD_DIR)/export/export_midi.cpp) \
                        $(wildcard $(CHIPNOMAD_DIR)/midi/*.cpp) \
                        $(wildcard $(CHIPNOMAD_DIR)/external/open303/Source/DSPCode/*.cpp) \
                        $(wildcard $(CHIPNOMAD_DIR)/external/ayumi/*.cpp) \
                        $(wildcard $(CHIPNOMAD_DIR)/external/mutable/braids/*.cc) \
                        $(wildcard $(CHIPNOMAD_DIR)/external/mutable/stmlib/utils/*.cc) \
                        $(wildcard $(CHIPNOMAD_DIR)/external/mutable/stmlib/dsp/*.cc) \
                        $(wildcard $(CHIPNOMAD_DIR)/external/mutable/plaits/*.cc) \
                        $(wildcard $(CHIPNOMAD_DIR)/external/mutable/plaits/dsp/*.cc) \
                        $(wildcard $(CHIPNOMAD_DIR)/external/mutable/plaits/dsp/chords/*.cc) \
                        $(wildcard $(CHIPNOMAD_DIR)/external/mutable/plaits/dsp/engine/*.cc) \
                        $(wildcard $(CHIPNOMAD_DIR)/external/mutable/plaits/dsp/engine2/*.cc) \
                        $(wildcard $(CHIPNOMAD_DIR)/external/mutable/plaits/dsp/fm/*.cc) \
                        $(wildcard $(CHIPNOMAD_DIR)/external/mutable/plaits/dsp/physical_modelling/*.cc) \
                        $(wildcard $(CHIPNOMAD_DIR)/external/mutable/plaits/dsp/speech/*.cc) \
                        $(CHIPNOMAD_DIR)/external/mutable/plaits_alt/resources.cc \
                        $(wildcard $(CHIPNOMAD_DIR)/external/mutable/plaits_alt/dsp/*.cc) \
                        $(wildcard $(CHIPNOMAD_DIR)/external/mutable/plaits_alt/dsp/chords/*.cc) \
                        $(wildcard $(CHIPNOMAD_DIR)/external/mutable/plaits_alt/dsp/engine/*.cc) \
                        $(wildcard $(CHIPNOMAD_DIR)/external/mutable/plaits_alt/dsp/engine2/*.cc) \
                        $(wildcard $(CHIPNOMAD_DIR)/external/mutable/plaits_alt/dsp/fm/*.cc) \
                        $(wildcard $(CHIPNOMAD_DIR)/external/mutable/plaits_alt/dsp/physical_modelling/*.cc) \
                        $(wildcard $(CHIPNOMAD_DIR)/external/mutable/plaits_alt/dsp/speech/*.cc)

TRACKER_SRC_SOURCES = $(TRACKER_DIR)/src/string_utils.cpp \
                      $(TRACKER_DIR)/src/project_utils.cpp \
                      $(TRACKER_DIR)/src/pitch_table_utils.cpp \
                      $(wildcard $(TRACKER_DIR)/src/corelib/corelib_file.cpp) \
                      $(TRACKER_DIR)/src/import/import_common.cpp \
                      $(TRACKER_DIR)/src/import/import_midi.cpp

SOURCES = src/main.cpp $(CHIPNOMAD_LIB_SOURCES) $(TRACKER_SRC_SOURCES)

BUILD_DIR = build
TARGET = cctmidi

object_for = $(BUILD_DIR)/$(subst ../,up/,$(basename $(1))).o
OBJECTS := $(foreach source,$(SOURCES),$(call object_for,$(source)))
DEPS := $(OBJECTS:.o=.d)

.PHONY: all clean

all: $(TARGET)

define compile_source
$(call object_for,$(1)): $(1) Makefile
	@mkdir -p $$(dir $$@)
	$(CXX) $(CFLAGS) $(INCLUDES) -MMD -MP -c $$< -o $$@
endef
$(foreach source,$(SOURCES),$(eval $(call compile_source,$(source))))
-include $(DEPS)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@ -lm

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
