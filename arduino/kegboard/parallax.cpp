#if (ARDUINO >= 100)
 #include "Arduino.h"
#else
 #include <avr/io.h>
 #include "WProgram.h"
#endif
#include "CWRFID.h"

/** Library for reading the Parallax RFID reader (product #28140) with an Arduino Mega 2560 or Uno
Copyright (c) 2012 Christopher Richard Wojno

RFID Reader is the Parallax (TM) "RFID Card Reader Serial". That's the one with TTL outputs and not the USB version.
See: http://www.parallax.com/StoreSearchResults/tabid/768/txtSearch/28140/List/0/SortField/4/ProductID/114/Default.aspx

for more information.

This library was written for the Arduino family of boards. See: http://www.arduino.cc for more information about the board for which this was written. This library relies heavily on the HardwareSerial interface provided by the Arduino libraries.

License:
This work is licensed under the Creative Commons Attribution 3.0 Unported License. To view a copy of this license, visit http://creativecommons.org/licenses/by/3.0/ or send a letter to Creative Commons, 444 Castro Street, Suite 900, Mountain View, California, 94041, USA.

Use this library at your own risk. No warranty is expressed or implied.
*/

int cw_rfid_read_once( int *buffer, int id_len, unsigned long wait, HardwareSerial* serial ) {
  boolean start_found = false;
  int bytes_read = 0;
  int v = 0;
unsigned long started_at = millis();
  while( bytes_read < id_len + 1 ) {
    if( serial->available() > 0 ) {
      v = serial->read();
      if( !start_found ) {
        // We haven't seen the magic start bit yet
        if( v != 0x0A ) {
          break;
        } else {
          start_found = true;
        }
      } else {
        // Are we at the end?
        if( bytes_read == id_len ) {
        // This is the magic stop bit. If we don't see it, we have an error
          if( v == 0x0D ) {
            return bytes_read;
          } else {
            // magic stop-bit not found
            return -1;
          }
        } else {
          // Not at the end, keep reading, stick on the buffer
          buffer[bytes_read] = v;
          bytes_read += 1;
        }
      }
    } else {
// timeout?
if( wait != 0 && millis() > started_at + wait ) return -3;
}
  }
  return -2;
}

int cw_rfid_read( int *buffer, int id_len, unsigned long wait, HardwareSerial* serial ) {
  int *buffer1 = (int*)alloca(id_len*sizeof(int));
  int *buffer2 = (int*)alloca(id_len*sizeof(int));
unsigned long started_at = millis();
int ret;
unsigned long new_wait = wait;
  
ret = cw_rfid_read_once( buffer1, id_len, wait, serial );
  if( id_len != ret ) {
    return ret;
  }

// See time we have remaining to wait
new_wait = wait - (millis() - started_at);

// timed out?
if( wait != 0 && new_wait > wait ) { return -3; }
if( wait == 0 ) { new_wait = 0; }

// Read the ID again to filter out noise
  ret = cw_rfid_read_once( buffer2, id_len, new_wait, serial );
  if( id_len != ret ) {
    return ret;
  }
  
  if( memcmp( buffer1, buffer2, sizeof(int)*id_len ) == 0 ) {
    memcpy( buffer, buffer1, sizeof(int)*id_len );
    return 0;
  }
  else {
    return -1;
  }
}


int cw_rfid_p28140_read( int *buffer, unsigned long wait, HardwareSerial* serial ) {
int ret;
// This model uses 2400 buad rate, 8N1
serial->begin(2400);
ret = cw_rfid_read( buffer, 10, wait, serial );
serial->end();
return ret;
}