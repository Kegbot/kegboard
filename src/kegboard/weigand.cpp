/*
 * HID RFID Reader Wiegand Interface for Arduino Uno
 * Written by Daniel Smith, 2012.01.30  www.pagemac.com
 * Ported to C++ and modified for kegbot by C. Niggel June 2012
 *
 *
 * This program will decode the wiegand data from a HID RFID Reader (or, theoretically,
 * any other device that outputs weigand data).
 * The Wiegand interface has two data lines, DATA0 and DATA1.  These lines are normally held
 * high at 5V.  When a 0 is sent, DATA0 drops to 0V for a few us.  When a 1 is sent, DATA1 drops
 * to 0V for a few us.  There is usually a few ms between the pulses.
 *
 * Operation is simple - each of the data lines are connected to hardware interrupt lines.  When
 * one drops low, an interrupt routine is called and some bits are flipped.  After some time of
 * of not receiving any bits, the Arduino will decode the data.  26 bit, 35 bit, and 37 bit formats
 * are supported, but you can easily add more.
 * Code is available for re-use, but please keep the attribution
*/

#include "weigand.h"
#include "kegboard_config.h"
#include <interrupt.h>

unsigned char Weigand_Card::databits[MAX_BITS];    // stores all of the data bits
unsigned char Weigand_Card::bitCount;              // number of bits currently captured
unsigned char Weigand_Card::flagDone;              // goes low when data is currently being captured
unsigned int Weigand_Card::weigand_counter;        // countdown until we assume there are no more bits
unsigned long Weigand_Card::cardCode;              // output value from the RFID card. We munge the site
                                                   // facility, and card code, but it will be unique

Weigand_Card::Weigand_Card() {
  // Attach pin change interrupt service routines from the Wiegand RFID readers

  //initialize the reader pins
  pinMode(KB_PIN_WEIGAND_BIT0, INPUT);
  pinMode(KB_PIN_WEIGAND_BIT1, INPUT);
  //attach interrupts
  attachInterrupt(KB_INT_WEIGAND_BIT1, reader1One, FALLING);
  attachInterrupt(KB_INT_WEIGAND_BIT0, reader1Zero, FALLING);


  weigand_counter = WEIGAND_WAIT_TIME;

  //digitalWrite(13, HIGH);  // show Arduino has finished initilisation

}

unsigned long Weigand_Card::getdata() {
  //intitalize output variable
  unsigned long lngCardData = 0;
 // This waits to make sure that there have been no more data pulses before processing data
  if (!flagDone) {
    if (--weigand_counter == 0)
      flagDone = 1;
  }

  // if we have bits and the weigand counter went out
  if (bitCount > 0 && flagDone) {
    unsigned char i;

    // we will decode the bits differently depending on how many bits we have
    // see www.pagemac.com/azure/data_formats.php for mor info
    if (bitCount == 35)
    {
      // 35 bit HID Corporate 1000 format
      // facility code = bits 2 to 14, card code bits 15 to 34
      for (i=2; i<34; i++)
      {
         lngCardData <<=1;
         lngCardData |= databits[i];
      }
    }
    else if (bitCount == 26)
    {
      // standard 26 bit format
      // facility code = bits 2 to 9, card code = bits 10 to 23
      for (i=1; i<25; i++)
      {
         lngCardData <<=1;
         lngCardData |= databits[i];
      }
    }
    else if (bitCount == 37)
    {
      // standard 37 bit format
      // facility code = bits 2 to 13, card code = bits 14 to 36
      for (i=1; i<36; i++)
      {
         lngCardData <<=1;
         lngCardData |= databits[i];
      }
    }
    else {
      // you can add other formats if you want!
     lngCardData = 1;
    }

     // cleanup and get ready for the next card
     bitCount = 0;
     for (i=0; i<MAX_BITS; i++)
     {
       databits[i] = 0;
     }
  }
  return lngCardData;
}

void Weigand_Card::reader1One() {
  databits[bitCount] = 1;
  bitCount++;
  flagDone = 0;
  weigand_counter = WEIGAND_WAIT_TIME;
}

void Weigand_Card::reader1Zero() {
  bitCount++;
  flagDone = 0;
  weigand_counter = WEIGAND_WAIT_TIME;

}

