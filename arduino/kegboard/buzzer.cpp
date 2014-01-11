/**
 * buzzer.cpp - Arduino buzzer routines
 * Copyright 2014 Mike Wakerly <opensource@hoho.com>
 *
 * This file is part of the Kegbot package of the Kegbot project.
 * For more information on Kegbot, see http://kegbot.org/
 *
 * Kegbot is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * Kegbot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Kegbot.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "buzzer.h"

#include <util/delay.h>

// Play a sequence of MelodyNotes
// Sequence must terminate with octave == -1
void play_notes(uint32_t* notes, int pin)
{
  int i=0;
  noTone(pin);

  while (true) {
    uint32_t note = notes[i++];
    if (note == MELODY_END) {
      break;
    }

    uint32_t frequency = FREQUENCY(note);
    uint32_t duration = DURATION(note);

    if (frequency == 0) {
      noTone(pin);
    } else {
      tone(pin, frequency);
    }

    delay(duration);
  }
  noTone(pin);
}
