#ifndef __CORELIB_INPUT_H__
#define __CORELIB_INPUT_H__

#include <stdint.h>

enum class InputDeviceType {
  none = 0,
  logical = 1,
  keyboard = 2,
  gamepad = 3,
};

struct InputCode {
  InputDeviceType deviceType;
  int32_t code;
};

enum Key {
  keyLeft = 0x1,
  keyRight = 0x2,
  keyUp = 0x4,
  keyDown = 0x8,
  keyEdit = 0x10,
  keyOpt = 0x20,
  keyPlay = 0x40,
  keyShift = 0x80,
  keyMotionLive = 0x100,
  keyMotionRecord = 0x200,
  keyMotionErase = 0x400,
  keyUnmapped = 0x800,
};

enum GamepadInputCode {
  gamepadTriggerLeft = 0x1000,
  gamepadTriggerRight,
};

// Initialize default key mappings based on platform/keyboard layout
void inputInitDefaultKeyMapping(void);

// Convert input code to human-readable name
const char* inputGetKeyName(InputCode input);

// Key jazz (desktop): semitone offset (0-28) for a keyboard input's physical
// key position, or -1 if it isn't one of the note keys. Uses the key's
// scancode (physical position), not the character it produces, so the
// mapping is the same on AZERTY/QWERTY/QWERTZ keyboards.
int inputKeyJazzNoteOffset(InputCode input);

// Key jazz (desktop): true if this input is the on/off toggle key (Esc).
int inputIsKeyJazzToggle(InputCode input);

// Key jazz (desktop): octave shift direction for this input, -1/0/1
// (not an octave key when 0).
int inputKeyJazzOctaveDelta(InputCode input);

#endif
