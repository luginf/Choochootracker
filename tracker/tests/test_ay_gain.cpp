#include "doctest.h"
#include "chips/chips.h"

#include <cmath>
#include <vector>

TEST_CASE("per-track AY and YM calibration stays below full scale") {
  for (int ym = 0; ym < 2; ++ym) {
    ChipSetup setup{}; setup.ay.clock = 1773400; setup.ay.isYM = ym;
    SoundChipAY chip(48000, setup);
    chip.setRegister(0, 212); chip.setRegister(1, 1); chip.setRegister(7, 0x3e);
    chip.setRegister(8, 15); chip.setRegister(9, 0); chip.setRegister(10, 0);
    std::vector<float> output(48000 * 2); chip.render(output.data(), 48000);
    float peak = 0.0f;
    for (float sample : output) { CHECK(std::isfinite(sample)); peak = std::fmax(peak, std::fabs(sample)); }
    CHECK(peak > .1f); CHECK(peak < 1.0f);
  }
}
