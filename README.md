# Pico-W-A2DP-Sink

Bluetooth A2DP Sink implementation for Raspberry Pi Pico W using BTStack. This is nothing more than [the example from BTStack repo](https://github.com/bluekitchen/btstack/blob/master/example/a2dp_sink_demo.c), cleaned up from the redundant code and modularized for better readability. 

Audio output is done via I2S bus implemented using PIO block and fed with data by chained DMA double-buffering mechanism.

## Functionalities
* A2DP Sink implementation using BTStack
* Advertises as Audio Class Loudspeaker (CoD 0x200414)
* I2S audio output
* Modularized code for better readability and easier modifications

# Connections

## I2S

| Signal | Board GPIO pin |
|--------|----------------|
| SDO    | GP28           |
| LRCLK  | GP27           |
| BCLK   | GP26           |
