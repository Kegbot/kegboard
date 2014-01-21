/**
 * Copyright 2014 Mike Wakerly <opensource@hoho.com>
 *
 * This file is part of the Kegboard package of the Kegbot project.
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

#include <Arduino.h>
#include <EEPROM.h>

#include "kegboard_eeprom.h"

int eeprom_is_valid() {
  return EEPROM.read(0) == (EEP_MAGIC >> 8) && 
      EEPROM.read(1) == (EEP_MAGIC & 0xff);
}

int eeprom_read_serialno(uint8_t *buf) {
  if (!eeprom_is_valid()) {
    return 0;
  }
  int i = 0;
  for (int off=2; i < SERIAL_NUMBER_SIZE_BYTES - 1; i++, off++) {
    buf[i] = EEPROM.read(off);
    if (buf[i] == '\0') {
      break;
    }
  }
  buf[i] = '\0';
  return i - 1;
}

void eeprom_write_serialno(uint8_t *serialno) {
  EEPROM.write(0, '\0');
  EEPROM.write(1, '\0');
  for (int i=0, off=2; i < SERIAL_NUMBER_SIZE_BYTES; i++, off++) {
    EEPROM.write(off, serialno[i]);
  }
  EEPROM.write(0, EEP_MAGIC >> 8);
  EEPROM.write(1, EEP_MAGIC & 0xff);
}
