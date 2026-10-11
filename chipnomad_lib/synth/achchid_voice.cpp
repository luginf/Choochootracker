#include "achchid_voice.h"
#include <math.h>
#include <string.h>

void AChChidVoice::init(float sampleRate) {
  synth_.setSampleRate(sampleRate);
  braidsPitchOffset_ = (int16_t)lroundf(1536.0f * (1.0f + log2f(sampleRate / 48000.0f)));
  braids_.Init(); braids_.set_shape(braids::MACRO_OSC_SHAPE_CSAW);
  braids_.set_parameters(16384, 16384);
}
void AChChidVoice::configure(uint8_t wave, int8_t fine, uint8_t model, uint16_t timbre, uint16_t color, uint8_t saturation,
                              uint16_t cutoff, uint8_t resonance, uint8_t envMod, uint16_t decay,
                              uint8_t accent, float gain) {
  wave_ = wave > 2 ? 1 : wave; gain_ = gain;
  synth_.setWaveform(wave_ == 0 ? 1.0 : 0.0);
  synth_.setCutoff(cutoff); synth_.setResonance(resonance); synth_.setEnvMod(envMod);
  synth_.setDecay(decay); synth_.setAccent(accent);
  braids_.set_shape(static_cast<braids::MacroOscillatorShape>(model > 46 ? 46 : model));
  braids_.set_parameters(timbre > 32767 ? 32767 : timbre, color > 32767 ? 32767 : color);
  fine_ = fine; saturation_ = saturation;
}
void AChChidVoice::noteOn(int note, bool accent, bool slide, uint8_t slideValue) {
  // Open303 scales its public setter by 0.2; compensate so ASL 00 is 60 ms.
  synth_.setSlideTime(5.0 * (60.0 + slideValue * 4.0));
  // Align Braids' pitch table with Open303 at the host rate.
  pitch_ = (note + 12) * 128 + fine_ * 128 / 100 + braidsPitchOffset_;
  if (slide && active_) synth_.slide(note + 12, accent); else synth_.trigger(note + 12, accent);
  braids_.Strike(); active_ = true;
}
void AChChidVoice::noteOff() { synth_.release(); }
void AChChidVoice::kill() { synth_.allNotesOff(); active_ = false; }
void AChChidVoice::renderBraidsBlock() {
  memset(braidsSync_, 0, sizeof(braidsSync_)); braids_.set_pitch(pitch_);
  braids_.Render(braidsSync_, braidsBlock_, 24); braidsPosition_ = 0;
}
void AChChidVoice::render(float* output, int samples) {
  for (int i = 0; i < samples; ++i) {
    float sample;
    if (wave_ == 2) { if (braidsPosition_ == 24) renderBraidsBlock();
      float braidsSample = braidsBlock_[braidsPosition_++] / 32768.0f;
      // MME's shaper gives a clear soft-clip-to-wavefold range before the
      // Open303 filter, with no static offset for its highpass to remove.
      float amount = saturation_ / 255.0f;
      float saturated = tanhf(braidsSample * (1.0f + amount * 5.0f));
      float folded = fabsf(fmodf(saturated * (1.0f + amount * 3.0f) + 1.0f, 4.0f) - 2.0f) - 1.0f;
      float foldMix = (amount - .55f) / .45f;
      if (foldMix < 0.0f) foldMix = 0.0f;
      if (foldMix > 1.0f) foldMix = 1.0f;
      braidsSample = saturated + (folded - saturated) * foldMix;
      sample = synth_.getSampleWithExternalInput(braidsSample);
    } else sample = synth_.getSample();
    // Continuous soft limiting prevents Open303 resonance/accent peaks from
    // overdriving the shared mixer without a discontinuous clip threshold.
    output[i] = 0.85f * tanhf(sample * gain_ / 0.85f);
  }
}
