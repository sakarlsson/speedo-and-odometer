# speedo-and-odometer
Arduino based speedometer and odometer

Made to work for my El Camino 1970 with a Holley Terminator X EFI.

The code is not generally usefull, by may be usefull as inspiration.

A quirk is that my printed dial had an error in speed per division between 100 kph and 120 kph, which I have compensated for in the code. 

The odometer is using a Adafruit Monochrome 128x32 I2C OLED graphic display [ADA931].
It will store the current km reading in the eeprom (using a rotating schem to get more than the 100000 writes per cell).

The speedo is using the SwitechX25 lib, but I had to fix a bug in the lib. When it initialize and move to zero its not using its accelleration code, so it will miss step. Can't find the fix..


![Speedo-odo and Tacho](speedo-odo.jpg)
