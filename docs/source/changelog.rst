.. _kegboard-changelog:

Changelog
=========

Arduino Firmware
-----------------

v18 (2014-05-14)
^^^^^^^^^^^^^^^^
* Added heartbeat: device will send a Hello message every 10 seconds.
* Hello message now includes uptime information.

v17 (2014-02-12)
^^^^^^^^^^^^^^^^
* Fixed a bug that broke serial communication (introduced in v16).

v16 (2014-02-07)
^^^^^^^^^^^^^^^^
* Added chip LED support for Kegboard Pro Mini.

v15 (2014-01-16)
^^^^^^^^^^^^^^^^
* Added `set_serial_number` command.

v14 (2013-07-23)
^^^^^^^^^^^^^^^^
* Flow LEDs are now toggled on system startup and during pours.
* Experimental debounce feature.
* Support for Parallax RFID readers.

v13 (2012-10-28)
^^^^^^^^^^^^^^^^
* Adds support for Wiegand RFID readers (HID ProxPro and similar).

v12 (2012-07-07)
^^^^^^^^^^^^^^^^
* Respond to ping with a short melody.

v11 (2012-05-02)
^^^^^^^^^^^^^^^^
* Updates for Arduino SDK v1.0; no functional changes.

v10 (2011-06-19)
^^^^^^^^^^^^^^^^
* Reverse ID-12 RFID endianness.

v9 (2011-06-13)
^^^^^^^^^^^^^^^
* Support ID-12 RFID input

v8 (2011-06-11)
^^^^^^^^^^^^^^^
* Expand 'set_output' to support onboard kegboard relay's, flow led's

v7 (2011-03-16)
^^^^^^^^^^^^^^^
* Added implementation of `set_output` command, relay output watchdog.

v6 (2010-09-22)
^^^^^^^^^^^^^^^
* Added auth_token message.

v5 (2010-01-10)
^^^^^^^^^^^^^^^
* Fix issue that caused flow events to be reported too frequently.

v4 (2010-01-04)
^^^^^^^^^^^^^^^
* Initial documented version.

Support Library
---------------

v1.0.0 (2012-07-01)
^^^^^^^^^^^^^^^^^^^
* Support library added to kegboard repository.
* Previous versions were located in the old master Kegbot repository: https://github.com/Kegbot/kegbot
