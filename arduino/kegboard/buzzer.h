#ifndef BUZZER_H
#define BUZZER_H

#include "Arduino.h"

// Note is:
//  16 bits for frequency
//  16 bits for duration
#define NOTE(frequency, duration) \
	(((frequency & 0xffffUL) << 16) | (duration & 0xffffUL))
#define DURATION(note) (note & 0xffffUL)
#define FREQUENCY(note) ((note >> 16) & 0xffffUL)
#define SILENT(duration)  NOTE(0, duration)
#define MELODY_END 0

void play_notes(uint32_t* notes, int pin);

#endif // BUZZER_H
