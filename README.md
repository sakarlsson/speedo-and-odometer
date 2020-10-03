# speedo-and-odometer
Arduino based speedometer and odometer

Made to work for my El Camino 1970 with a Holley Terminator X EFI.

The code is not generally useful, by may be useful as inspiration.

A quirk is that my printed dial had an error in speed per division between 100 kph and 120 kph, which I have compensated for in the code. 

The odometer is using a Adafruit Monochrome 128x32 I2C OLED graphic display [ADA931].
It will store the current km reading in the eeprom (using a rotating scheme to get more than the 100000 writes per cell).

The speedo is using the SwitechX25 lib, but I had to fix a bug in the lib. When it initialize and move to zero its not using its built in acceleration code, so it will miss steps and not zero correctly. (Can't find the fix right now).

![Speedo-odo and Tacho](speedo-odo.jpg)

I used the Arduino Nano for a small form factor and milled out a plastic console
out of POM to hold it all together.

For the tachometer I had to CNC mill a new needle as I couldn't reuse the original.

The speedo will get power from the IGN 12+V, but the tachometer will have constant
battery power. As it will not otherwise be able to go to Zero when you turn off the IGN.

The tachometer code will enter Low Power mode when there are no pulses detected, and
did draw about 2-3 mA (after I desoldered the power LED on the Nano).

The Holley Terminator X was programmed to output the Speed on one of the free output pins.
