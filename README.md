This is intended to be a replacement for dfu-util, but streamlined for dealing
with Maple boards and the Maple boot loader.

I was getting annoyed with dfu-util and its terrible error messages,
so I decided to write this and learn a bit about libusb in the process.

This all started in October, 2020 when I purchased four Maple r5 boards and
got an Olimexino-STM32 board thrown in for free.  All of them arrived with
the Maple loader installed and were clearly designed to be used with
the Maple loader and just a USB cable.

The 368 byte "blink.bin" is a test file, which is a bare metal blink application
that will blink the LED on a geniune maple board.  It is linked to be loaded
at address 0x08005000, as per the expectation of the maple DFU loader.
It does not have any kind of USB code to respond to the usual maple reset
scheme.  So anyone who burns it will have to hit reset, then quickly hit and
hold the "extra button" on the maple for a while, to put the boot loader into
perpetual boot loader mode.
