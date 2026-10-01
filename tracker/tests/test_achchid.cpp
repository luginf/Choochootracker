#include "doctest.h"
#include "project_instruments.h"
#include "project.h"
#include "synth/achchid_voice.h"
#include <cmath>
#include <vector>

TEST_CASE("aChChid has native controls and dedicated FX") {
  Instrument instrument = {};
  getInstrumentFunctions(InstrumentType::AChChid).init(&instrument);
  CHECK(instrument.type == InstrumentType::AChChid);
  CHECK(instrument.chip.achchid.wave == AChChidWave::saw);
  CHECK(instrument.chip.achchid.decay == 1000);
  CHECK(instrument.chip.achchid.saturation == 0);
  CHECK(instrumentVoicePostSettings(&instrument) == nullptr);
  CHECK(instrumentFXAvailable(InstrumentType::AChChid, fxASL));
  CHECK(instrumentFXAvailable(InstrumentType::AChChid, fxATY));
  CHECK(instrumentFXAvailable(InstrumentType::AChChid, fxADC));
  CHECK(instrumentFXAvailable(InstrumentType::AChChid, fxAAC));
  CHECK(instrumentFXAvailable(InstrumentType::AChChid, fxATM));
  CHECK(instrumentFXAvailable(InstrumentType::AChChid, fxACL));
  CHECK(instrumentFXAvailable(InstrumentType::AChChid, fxACF));
  CHECK(instrumentFXAvailable(InstrumentType::AChChid, fxARS));
  CHECK(instrumentFXAvailable(InstrumentType::AChChid, fxAEM));
  CHECK_FALSE(instrumentFXAvailable(InstrumentType::AChChid, fxBMD));
}

TEST_CASE("aChChid output is softly bounded before the mixer") {
  AChChidVoice voice;
  voice.init(48000.0f);
  voice.configure(1, 0, 0, 16384, 16384, 0, 400, 100, 100, 2000, 100, 4.0f);
  voice.noteOn(48, true, false, 0);
  std::vector<float> output(4096);
  voice.render(output.data(), (int)output.size());
  for (float sample : output) {
    CHECK(std::isfinite(sample));
    CHECK(std::fabs(sample) <= 0.85001f);
  }
}

TEST_CASE("aChChid Braids Shaper visibly reshapes the oscillator") {
  AChChidVoice clean;
  AChChidVoice asym;
  clean.init(48000.0f);
  asym.init(48000.0f);
  clean.configure(2, 0, 0, 16384, 16384, 0, 20000, 0, 0, 2000, 0, 1.0f);
  asym.configure(2, 0, 0, 16384, 16384, 255, 20000, 0, 0, 2000, 0, 1.0f);
  clean.noteOn(48, false, false, 0);
  asym.noteOn(48, false, false, 0);
  std::vector<float> cleanOutput(4096), asymOutput(4096);
  clean.render(cleanOutput.data(), (int)cleanOutput.size());
  asym.render(asymOutput.data(), (int)asymOutput.size());

  float difference = 0.0f;
  for (size_t i = 1024; i < cleanOutput.size(); ++i)
    difference += std::fabs(cleanOutput[i] - asymOutput[i]);
  CHECK(difference / (cleanOutput.size() - 1024) > 0.05f);
}

TEST_CASE("aChChid Braids oscillator keeps Open303's octave at 48 kHz") {
  AChChidVoice voice;
  voice.init(48000.0f);
  voice.configure(2, 0, 0, 0, 0, 0, 20000, 0, 0, 2000, 0, 1.0f);
  voice.noteOn(45, false, false, 0); // A3 = MIDI 57 = 220 Hz.
  std::vector<float> output(48000);
  voice.render(output.data(), (int)output.size());

  int crossings = 0;
  for (size_t i = 4801; i < output.size(); ++i)
    if (output[i - 1] <= 0.0f && output[i] > 0.0f) ++crossings;
  CHECK(crossings == doctest::Approx(198).epsilon(0.1));
}
