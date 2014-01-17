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

 // Kegboard EEPROM constants.

// Memory Layout
//
// BYTES       DESCRIPTION
// ----------- --------------------------------------------------------
// 0-1         EEP_MAGIC flag (2 bytes); indicates programmed EEPROM.
// 2-31        Board serial number (8 bytes).
// 31-Max      Reserved.

#ifndef KEGBOARD_EEPROM_H_
#define KEGBOARD_EEPROM_H_

#include <inttypes.h>

#define EEP_MAGIC 0x4a1e
#define SERIAL_NUMBER_SIZE_BYTES 30

int eeprom_is_valid();
int eeprom_read_serialno(uint8_t *buf);
void eeprom_write_serialno(uint8_t *serialno);

#endif // KEGBOARD_EEPROM_H_