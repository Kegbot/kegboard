=================
Kegboard Overview
=================

This page describes *Kegboard*, the Arduino-based controller board for Kegbot.

What is a Kegboard?
===================

*Kegboard* is the name we use for the microcontroller board used in a Kegbot
system.  Kegboard is the device that monitors all sensors, including the flow
sensors that are essential in any Kegbot configuration.

There are two commonly-used Kegboard targets:

* `Kegboard Pro Mini <https://kegbot.org/kbpm>`_, a fully-assembled board,
  introduced in early 2014 and sold at the `Kegbot Store <http://store.kegbot.org>`_.
* `DIY Arduino Kegboard <https://kegbot.org/kegboard-diy>`_, a do-it-yourself
  option that can be built using an `Arduino <http://arduino.cc>`_
  board.

The Kegboard software package includes firmware and support libraries that
work with either kind of Kegboard.

Features
========

Since not all Kegbots are alike, the Kegbot firmware is designed with
flexibility in mind: We try to support many features and add-on devices in the
core firmware, while still keeping basic functionality tight and fast for the
common configurations.

Depending on hardware, the Kegboard firmware can support the following
features:

* **Flow Sensing:** Two independent flow meter inputs (or 6 on Arduino Mega),
  allowing you to monitor that many individual beer taps with just one board.
* **Temperature Sensing:** Dedicated OneWire bus for reading DS1820 (DS18S20 and
  DS18B20) temperature sensors.  An unlimited number of sensors can be
  connected, allowing you to independently track keg temperature and ambient
  temperature.
* **RFID Authentication:** Authenticate users with cheap 125kHz RFIDs by
  connecting the optional ID-12 RFID reader.
* **OneWire Authentication:** Authenticate users with durable iButtons.
* **Relay/Value Control:** Four general purpose outputs can be used
  to toggle external devices, such as a valve to prevent unauthorized access.
  Relays are monitored by an internal watchdog.
* **Buzzer:** Kegboard will play a short melody whenever an
  authentication token is connected or swiped.
* **Extensible Serial Protocol:** If you don't want to use the
  rest of the Kegbot software, you can still use Kegboard by implementing its
  simple and extensible serial protocol in your system.  (See
  :ref:`kegboard-serial-protocol`).

.. note::
  
  Because of its limited size, certain features (such as relay control and
  RFID reading) are not available on Kegboard Pro Mini.

Kegboard's firmware is designed to operate correctly even when a feature is
not being used.  For example, if the temperature sensor input is not
connected, other features will continue to operate normally.
