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

#define WIEGAND_BUFSIZ 5

class Wiegand {
  public:
    Wiegand();
    void handleData0Pulse();
    void handleData1Pulse();
    int getData(uint8_t* data);
    void reset();
  private:
    uint8_t data0_pin_;  // Wiegand DATA0 pin
    uint8_t data1_pin_;  // Wiegand DATA1 pin
    volatile int bitpos_;     // Current bit position
    uint8_t buf_[WIEGAND_BUFSIZ];
};
