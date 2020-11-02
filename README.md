This is intended to be a replacement for dfu-util, but streamlined for dealing
with Maple boards and the Maple boot loader.

I was getting annoyed with dfu-util and its terrible error messages,
so I decided to write this and learn a bit about libusb in the process.

This all started in October, 2020 when I purchased four Maple r5 boards and
got an Olimexino-STM32 board thrown in for free.  All of them arrived with
the Maple loader installed and were clearly designed to be used with
the Maple loader and just a USB cable.
