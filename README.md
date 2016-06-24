
p44vdc
======

[![Flattr this git repo](http://api.flattr.com/button/flattr-badge-large.png)](https://flattr.com/submit/auto?user_id=luz&url=https://github.com/plan44/p44vdc&title=p44vdc&language=&tags=github&category=software) 

"p44vdc" is a a free (opensource, GPLv3) framework of C++ source files, which is used to build virtual device connectors (vdcs) for digitalSTROM systems.

"p44vdc" was extracted as a submodule from the ["vdcd" (virtual device connector daemon) project](https://github.com/plan44/vdcd), of which it is an essential part.

The reason for extracting these sources into this separate repository is to allow to build vdcs possibly differing in areas such build system, target platforms, default configuration, supported devices etc., but still based on the plan44 vdc framework.

**If you want a full implementation of a ready-to run vdc host with support for many device types already built-in, please see the [vdcd project](https://github.com/plan44/vdcd) instead**.

This repository is only a *building block* for a working vdc deamon.

License
-------

vdcd is licensed under the GPLv3 License (see COPYING).

If that's a problem for your particular application, I am open to provide a commercial license, please contact me at [luz@plan44.ch](mailto:luz@plan44.ch).


Features
--------

- Implements the complete digitalSTROM vDC API including behaviours for buttons, binary inputs, lights, color lights, sensors, heating valves and shadow blinds.
- Provides the *plan44 vdcd external device API* for easily building custom devices as external scripts or programs.
- Supports EnOcean TCM310 based gateway modules, connected via serial port or network
- Supports Philips hue lights via the hue bridge and its JSON API
- Supports WS2812 LED chip based RGB LED chains on Raspberry Pi (just connect a WS2812's data-in to RPi P1 Pin 12, GPIO 18), thanks to the [rpi_ws281x library](https://github.com/richardghirst/rpi_ws281x.git)
- Allows to use Linux GPIO pins (e.g. on RaspberryPi) as button inputs or on/off outputs
- Allows to use i2c and spi peripherals (supported chips e.g. TCA9555, PCF8574, PCA9685, MCP23017, MCP23S17) for digital I/O as well as PWM outputs
- Implements interface to [Open Lighting Architecture - OLA](http://www.openlighting.org/) to control DMX512 based lights (single channel, RGB, RGBW, RGBWA, moving head)


Getting Started
---------------

### Not here!

This is not a full vdc project, only parts of it. Please go to the vdcd main project at [vdcd project](https://github.com/plan44/vdcd).
