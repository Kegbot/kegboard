#ifndef _CWRFID_H_
#define _CWRFID_H_

#include <alloca.h>
#include <HardwareSerial.h>

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

/** Enable RFID Hardware
*
* Given the pin_number, will configure the pin to the LOW state, as the Parallax 28140 assumes /ENABLE and thus, the device will be enabled and can read keys as they pass by. Consequently, it will also draw power to engage the inductor.
*
* This is a define to avoid bloating your code needlessly (if you don't use them, no hit to your code size ;-))
*
* Arguments:
* pin_number[in]: The digital (only digital pins work as outputs, don't try it with analog) pin to drive the /ENABLE pin on the Parallax RFID Card Reader
*
* Returns:
* nothing (void)
*/
#define CW_RFID_ENABLE_PIN(pin_number) digitalWrite(pin_number,LOW)

/** Disable RFID Hardware
*
* Given the pin_number, will configure the pin to the HIGH state, as the Parallax 28140 assumes /ENABLE and thus, the device will be disabled and cannot read keys. Consequently, the device will enter a lower power state and conserve your battery power, if applicable.
*
* This is a define to avoid bloating your code needlessly (if you don't use them, no hit to your code size ;-))
*
* Arguments:
* pin_number[in]: The digital (only digital pins work as outputs, don't try it with analog) pin to drive the /ENABLE pin on the Parallax RFID Card Reader
*
* Returns:
* nothing (void)
*/
#define CW_RFID_DISABLE_PIN(pin_number) digitalWrite(pin_number,HIGH)

/** Read RFID Device Once
*
* Reads the RFID from the serial device once after waiting (possibly indefinitely) for data to be available. Please note, this might read junk data from noise or interference on the RFID reader's radio frequency. There really is no way to deal with this except to read the id twice in a row and check that the id's match.
*
* Prerequisits:
* Serial port to which the sensor is attached is configured with a call to "begin(YOUR_BAUD)" and is configured for reading
*
* Arguments:
* buffer[out]: Signed Integer (int) buffer of length: id_len. This holds the ID of the tag read.
* id_len[in]: The length, in integers, of the buffer provided in buffer, and, consequently, the length of the ID of the RFID key's tag.
* wait[in]: The length of time to wait for data to be available on the serial line before returning a timeout error code. Specify 0 to wait indefinitely (ignore).
* serial[in,out]: The address of the serial object (&Serial, &Serial1, &Serial2, &Serial3) you're using for the RFID reader. Usually, the USB port uses Serial, so you can specify &Serial1 to use the RX1 port to look for data from your RFID reader.
*
* Returns:
* * id_len: The length of the ID requested, if all is well and an ID was read. This ignores the start and stop codes (0x0A and 0x0D, respectively). These codes are NOT included in the buffer and do not consume space in id_len. These start and stop codes are used to verify that a key was read successfully by the RFID reader
* * -1: Missing stop bit
* * -2: Missing stop bit (indicates that I made a programming mistake as this code should never be returned)
* * -3: Timeout: the wait provided (if non-zero) has expired and the call returned without reading a complete key. Buffer may or may not be altered and should be considered to be junk.
*/
int cw_rfid_read_once( int *buffer, int id_len, unsigned long wait, HardwareSerial* serial );

/** Read RFID Device Reliably
*
* Reads the RFID from the serial device twice after waiting (possibly indefinitely) for data to be available. This function is very reliable and is extremely likely to always return a proper key code. As the likelyhood of reading the same junk noise data in a row is rather low (but not impossible). This function takes care of obtaining the RFID key's ID twice and comparing their values for you.
*
* Prerequisits:
* Serial port to which the sensor is attached is configured with a call to "begin(YOUR_BAUD)" and is configured for reading
*
* Arguments:
* buffer[out]: Signed Integer (int) buffer of length: id_len. This holds the ID of the tag read.
* id_len[in]: The length, in integers, of the buffer provided in buffer, and, consequently, the length of the ID of the RFID key's tag.
* wait[in]: The length of time to wait for data to be available on the serial line before returning a timeout error code. Specify 0 to wait indefinitely (ignore).
* serial[in,out]: The address of the serial object (&Serial, &Serial1, &Serial2, &Serial3) you're using for the RFID reader. Usually, the USB port uses Serial, so you can specify &Serial1 to use the RX1 port to look for data from your RFID reader.
*
* Returns:
* * 0: Successfully read the RFID key's ID with high probability that the ID is accurate
* * -1: Read failed. This is usually caused when the RFID keys read the first and second time do not match. This is usually caused by noise. It is highly suggested that you wait a spell before reading again. This is also caused by moving the RFID tag away from the reader before it's had a chance to read the ID two times in a row. This takes about 1 second, so hold your key up to the reader for about 1 second to avoid getting read errors
* * -3: Timeout: the wait provided (if non-zero) has expired and the call returned without reading a complete key. Buffer may or may not be altered and should be considered to be junk.
*/
int cw_rfid_read( int *buffer, int id_len, unsigned long wait, HardwareSerial* serial );

/** Read Parallax RFID Device Reliably (Product ID: 28140)
*
* Reads the Parallax RFID Reader (Product ID: #28140) realiably. This uses the cw_rfid_read function, fully configured for the exact specifications of the Parallax device (See: "RFID Card Reader Serial & USB (#28140 / 28340) v2.2 3/22/2010"). This includes:
* * initializing the serial device provided witha 2400 baud rate
* * assuming that the "buffer" argument is exactly 10 integers wide (20 characters)
*
* After the call completes, the Serial device is disabled. If you don't want this, use the cw_rfid_read call instead, but you will be responsible for Serial configuration.
*
* Prerequisits:
* There are no software requirements. You must have your RFID reader configured in hardware on the serial port you indicate in the argument: serial. You must also drive the /ENABLE to ground (the LED labelled: "/ENABLE" will be red on the RFID reader instead of green). The serial line will be configured for you as indicated in the datasheet provided by Parallax.
*
* Arguments:
* buffer[out]: Signed Integer (int) buffer of length: 10 (AKA: buffer[10]). This holds the ID of the tag read.
* wait[in]: The length of time to wait for data to be available on the serial line before returning a timeout error code. Specify 0 to wait indefinitely (ignore).
* serial[in,out]: The address of the serial object (&Serial, &Serial1, &Serial2, &Serial3) you're using for the RFID reader. Usually, the USB port uses Serial, so you can specify &Serial1 to use the RX1 port to look for data from your RFID reader.
*
* Returns:
* * 0: Successfully read the RFID key's ID with high probability that the ID is accurate
* * -1: Read failed. This is usually caused when the RFID keys read the first and second time do not match. This is usually caused by noise. It is highly suggested that you wait a spell before reading again.
* * -3: Timeout: the wait provided (if non-zero) has expired and the call returned without reading a complete key. Buffer may or may not be altered and should be considered to be junk.
*
* Example:
#include <CWRFID.h>

// HARDWARE SETUP:
// Connect the RFID card reader's "VCC" pin to 5V on the Arduino board
// Connect the RFID card reader's "/ENABLE" pin to the Digital (PWM) 2 pin (digital 2)
// Connect the RFID card reader's "SOUT" pin to the RX1 pin under (Communication) (You MUST have an Arduino Mega)
// Connect the RFID card reader's "GND" pin to the "GND" pin on the Arduino

// Sets which pin we'll use to enable and disable our RFID device. In this case, it's digital pin #2
#define RFIDEN_PIN 2
// Helper function: enableds the RFID device using the helper defines given in CWRFID.h
// You do not have to use them, but it helps make the code below a bit more readable
#define ENABLE_RFID() CW_RFID_ENABLE_PIN(RFIDEN_PIN)
#define DISABLE_RFID() CW_RFID_DISABLE_PIN(RFIDEN_PIN)
void setup() {
// Configure the Arduino IDE console (assumes Arduino 2560 Mega) so we can see IDs that are read
Serial.begin(9600);
// Configure the RFID enable output
pinMode(RFIDEN_PIN,OUTPUT);
// Turn off the reader at start (no need to waste power until we're ready)
DISABLE_RFID();
}

void loop() {
// This is our storage location for the ID that we're going to read
int buffer[10];
// output of the RFID call: 0 = OK, -1 = read error (try again), -3 = timeout
int rfid_status = 0;
// Turn on the radio
ENABLE_RFID();
// Ask the user to present his/her RFID key
Serial.println("Please present your RFID key");
/// Read the RFID Key Reader's serial data. Store the ID (if available) in "buffer",
/// wait 10 seconds before timing out
/// use RX2 as the input
rfid_status = cw_rfid_p28140_read( buffer, 10000, &Serial1 );
switch( rfid_status ) {
// Success!
case 0:
// Write the ID to the console, separate each integer with a colon (:)
// The ID is exactly 10 integer digits
for( int i = 0; i < 10; ++i ) {
Serial.print( (unsigned int)buffer[i] );
// Add a colon separator, except for the last character, add a newline.
if( i != 9 ) { Serial.print( ":" ); } else { Serial.print("\n"); }
}
// Turn off the radio
DISABLE_RFID();
// wait 5 seconds. You could, at this point, cross reference this ID
// with a database of known keys to see if this person has access
// If they do, let them in or do something. I'm just waiting here just because
delay(5000);
// Do stuff
break;
case -3:
// This means they didn't hold up a key
// Turn off the radio
DISABLE_RFID();
Serial.println( "Timeout reached" );
// Do something else. The user is clearly not paying attention or doesn't want to present
// an RFID key to us
delay(5000);
break;
case -1:
// Sometimes, the RFID reader will encounter noise. This will generate keys with spurious IDs
// The library filters these out for you and will indicate a -1 when you should ignore the key
// This is also caused by moving the RFID tag away from the reader before it's had a chance to read
// the ID two times in a row. This takes about 1 second
Serial.println( "Unable to read the key, please try again" );
// Turn off the radio
DISABLE_RFID();
// Wait for the noise to dissipate
delay(1000);
break;
default:
// You shouldn't be able to read this state. Assume any values other than those
// specified above are error conditions
Serial.println("UNKNOWN ERROR");
}
}
*
*/
int cw_rfid_p28140_read( int *buffer, unsigned long wait, HardwareSerial* serial );

#endif//_CWRFIC_H_