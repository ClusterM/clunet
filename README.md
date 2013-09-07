#CLUNET

<h4>CLUNET library - simple 1-wire peer-to-peer network for AVR microcontrollers</h4>

Perfect way to interconnect microcontrollers in your house.

* Only few cheap additional components
* Using only 1 wire
* Using only 2 pins
* There is no master device, all devices are equal
* You can use very long cable, up to 100 metres and more
* You don't need to care about collisions
* Pseudo multitask using interrupts
* Up to 255 devices on one bus

##How to use
###Hardware part
<img src="images/sample-schematic.png"/>

You need pin with external interrupt to read data and any other pin to send data.

###Software part
You will need one free 8-bit timer in your firmware.
