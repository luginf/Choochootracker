#ifndef CHOOCHOO_PLAITS_VOICE_H
#define CHOOCHOO_PLAITS_VOICE_H

#include "plaits/dsp/voice.h"
#include "mutable_voice_base.h"

class PlaitsVoice : public MutableVoiceBase<plaits::Voice, plaits::Patch, plaits::Modulations> {
 public:
  void configure(uint8_t engine, uint16_t harmonics, uint16_t timbre, uint16_t morph,
                 uint8_t auxMix, uint8_t envelopeMode, uint8_t decay, uint8_t sustain,
                 float note, float gain) {
    static constexpr float modelGain[24] = {
      1.584893f, 1.122018f, 1.122018f, 1.258925f, 1.122018f, 1.412538f,
      1.995262f, 1.0f, 1.412538f, 1.584893f, 1.412538f, 1.584893f,
      1.258925f, 1.778279f, 1.258925f, 1.122018f, 1.0f, 1.0f,
      1.995262f, 1.995262f, 1.995262f, 1.995262f, 1.995262f, 1.995262f
    };
    MutableVoiceBase::configure(engine, harmonics, timbre, morph, auxMix, envelopeMode,
                                decay, sustain, note, gain, modelGain[engine > 23 ? 23 : engine]);
  }
};

#endif
