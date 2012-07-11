/*
 * Copyright 2012 Mike Wakerly <opensource@hoho.com>
 *
 * This file is part of the Kegboard package of the Kegbot project.
 * For more information on Kegboard or Kegbot, see http://kegbot.org/
 *
 * Kegboard is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * Kegboard is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Kegboard.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "Arduino.h"
#include "Wiegand.h"
#include "pins_arduino.h"

Wiegand::Wiegand() {
  bitpos_ = 0;
}

void Wiegand::handleData0Pulse() {
  bitpos_++;
}

void Wiegand::handleData1Pulse() {
  int index = bitpos_ / 8;
  if (index >= WIEGAND_BUFSIZ) {
    return;
  }
  int offset = bitpos_ % 8;
  buf_[index] |= (uint8_t) (1 << offset);
  bitpos_++;
}

int Wiegand::getData(uint8_t* data) {
  memcpy(data, buf_, WIEGAND_BUFSIZ);
  return bitpos_;
}

void Wiegand::reset() {
  bitpos_ = 0;
  memset(buf_, 0, WIEGAND_BUFSIZ);
}

