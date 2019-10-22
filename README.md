# What is gbs-control-atmega?

This project provides control software for Tvia Trueview5725 based video converter boards.
These boards are a cost efficient way to adapt legacy game consoles to modern displays.

Gbs-control-atmega replaces the original control software and better optimizes the Trueview5725 multimedia processor for the task of upscaling "240p/288p" 15kHz content on 4:3, 5:4 and 16:10 displays.

It is an arduino-only version, secifically made for modern widescreen displays, including overscan, external Buttons etc.

Previous work:  
https://github.com/dooklink/gbs-control  
https://github.com/ramapcsx2/gbs-control  
https://github.com/mybook4/DigisparkSketches/tree/master/GBS_Control  
https://ianstedman.wordpress.com/  

# Installation instructions

1. Solder a Jumper on your GBS8200 Board, just below the P5 Header. This Jumper disables the internal MTV230M Microcontroller and let you program the TV5725 via I2C.

2. Use the P6 Header to power your Arduino. This way you ensure, that you got the same GND connection and as the GBS8200 Board uses 5V, it's a good Idea to use this power also for your arduino.

3. Connect  
    SDA -> A4  
    SCL -> A5  
    V(sync) -> D10  
These are the requred pins to run gbs-control-atmega.

4. If you want to use Buttons and Switches, connect 3 push-buttons to arduino pin D2, D3 and D4 and a Switch to D8. Since the inputs are LOW-Active(!), you need to connect the other pin of the button/switch to GND.
  
# Further intructions

*Switch 1 (D8) switches between widescreen and fullscreen. It has to be a switch with 2 states (on/off)  
*Button 1 (D2) selects the mode: move vertial, scale vertial, move horizontal, scale horizontal  
*Button 2 (D3) is "down"  
*Button 3 (D4) is "up"  
  
Pressing Button 2 and 3 together resets the picture to the saved preset.
