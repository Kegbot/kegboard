/*
 * HID RFID Reader Wiegand Interface for Arduino Uno
 * Written by Daniel Smith, 2012.01.30  www.pagemac.com
 * Ported to C++ and modified for kegbot by C. Niggel June 2012
 *
 *
 * This program will decode the wiegand data from a HID RFID Reader (or, theoretically,
 * any other device that outputs weigand data).
 * The Wiegand interface has two data lines, DATA0 and DATA1.  These lines are normall held
 * high at 5V.  When a 0 is sent, DATA0 drops to 0V for a few us.  When a 1 is sent, DATA1 drops
 * to 0V for a few us.  There is usually a few ms between the pulses.
 *
 * Operation is simple - each of the data lines are connected to hardware interrupt lines.  When
 * one drops low, an interrupt routine is called and some bits are flipped.  After some time of
 * of not receiving any bits, the Arduino will decode the data.  26 bit and 35 bit formats
 * are supported, but you can easily add more.
 * Code is available for re-use, but please keep the attribution
 *
 * Add the following declarations to the file if you need to use the weigand module alone
 * for card testing
 * #define KB_INT_WEIGAND_BIT0 2
 * #define KB_INT_WEIGAND_BIT1 3
 * #define KB_PIN_WEIGAND_BIT0 21
 * #define KB_PIN_WEIGAND_BIT1 20
*/
#include "Arduino.h"
#include <HardwareSerial.h>
#include <avr/io.h>
#include <string.h>
#include <util/delay.h>
#include <wiring.h>
#include <avr/pgmspace.h>




class Weigand_Card {



  private:
#define MAX_BITS 100                 // max number of bits
#define WEIGAND_WAIT_TIME  1000      // time to wait for another weigand pulse.


static unsigned char databits[MAX_BITS];    // stores all of the data bits
static unsigned char bitCount;              // number of bits currently captured
static unsigned char flagDone;              // goes low when data is currently being captured
static unsigned int weigand_counter;        // countdown until we assume there are no more bits
static unsigned long cardCode;            // decoded facility + card code
static void reader1Zero();
static void reader1One();

  public:
  Weigand_Card();
  unsigned long getdata();
};
