#ifndef CHOOCHOO_PLAITS_ALT_VOICE_H
#define CHOOCHOO_PLAITS_ALT_VOICE_H

#include "plaits_alt/dsp/voice.h"
#include "mutable_voice_base.h"

class PlaitsAltVoice : public MutableVoiceBase<plaits_alt::Voice, plaits_alt::Patch, plaits_alt::Modulations> {
 public:
  void configure(uint8_t engine, uint16_t harmonics, uint16_t timbre, uint16_t morph,
                 uint8_t auxMix, uint8_t envelopeMode, uint8_t decay, uint8_t sustain,
                 float note, float gain) {
    static constexpr float modelGain[24] = {
      1.995262f, 1.584893f, 1.584893f, 1.584893f, 1.412538f, 1.995262f,
      1.778279f, 1.778279f, 1.412538f, 1.258925f, 1.995262f, 1.995262f,
      1.995262f, 1.995262f, 1.412538f, 1.995262f, 1.995262f, 1.122018f,
      1.258925f, 1.995262f, 1.258925f, 1.778279f, 1.778279f, 1.258925f
    };
    MutableVoiceBase::configure(engine, harmonics, timbre, morph, auxMix, envelopeMode,
                                decay, sustain, note, gain, .9678f * modelGain[engine > 23 ? 23 : engine]);
  }
};

#endif
