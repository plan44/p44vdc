p44vdc
======

*[[if you want to support p44vdc development, please consider to sponsor plan44]](https://github.com/sponsors/plan44)* 

"p44vdc" is a a free (opensource, GPLv3) framework of C++ source files, which is used to build virtual device connectors (vdcs) for Digital Strom systems.

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

- Implements the complete Digital Strom vDC API including behaviours for buttons, binary inputs, lights, color lights, sensors, heating valves and shadow blinds.
- Provides the *plan44 vdcd external device API* for easily building custom devices as external scripts or programs, or implemented in the built-in [p44script](https://plan44.ch/p44-techdocs/en/script_ref/) language.
- Supports EnOcean TCM310 based gateway modules, connected via serial port or network
- Supports Philips hue lights via the hue bridge and its JSON API
- Supports building really fancy effect color LED lights out WS281x LED chip based LED chains/matrices, with moving segments, lightspots, gradients etc.
  On Raspberry Pi, just connect a WS2812's data-in to RPi P1 Pin 12, GPIO 18 (thanks to the [rpi_ws281x library](https://github.com/richardghirst/rpi_ws281x.git)).
  On MT7688 systems under OpenWrt, use the [p44-ledchain kernel driver](https://github.com/plan44/plan44-feed/tree/master/p44-ledchain). 
- Allows to use Linux GPIO pins (e.g. on RaspberryPi) as button inputs or on/off outputs
- Allows to use i2c and spi peripherals (supported chips e.g. TCA9555, PCF8574, PCA9685, MCP23017, MCP23S17) for digital I/O as well as PWM outputs
- Implements interface to [Open Lighting Architecture - OLA](http://www.openlighting.org/) to control DMX512 based lights (single channel, RGB, RGBW, RGBWA, moving head)


Getting Started
---------------

### Not here!

This is not a full vdc program, only important parts of it. Please go to the [**vdcd main project**](https://github.com/plan44/vdcd) for a ready-to-build (and run) program.

Supporting p44vdc
-----------------

1. use it!
2. See it in action by trying the p44 automation platform, such as [free Rpi images](https://plan44.ch/automation/p44-lc-x.php) or the [p44-open openwrt build](https://github.com/plan44/p44-xx-open).
3. support development via [github sponsors](https://github.com/sponsors/plan44) or [flattr](https://flattr.com/@luz)
4. Discuss it in the [plan44 community forum](https://forum.plan44.ch/t/opensource-c-vdcd).
5. contribute patches, report issues and suggest new functionality [on github](https://github.com/plan44/p44vdc) or in the [forum](https://forum.plan44.ch/t/opensource-c-vdcd).
6. Buy plan44.ch [products](https://plan44.ch/automation/products.php) - sales revenue is paying the time for contributing to opensource projects :-)


*(c) 2013-2025 by Lukas Zeller / [plan44.ch](http://www.plan44.ch/automation)*
