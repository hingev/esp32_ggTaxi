
# Goal

**GG Taxi** is a Taxi service created in Armenia, originally made as a mobile app.

Get a device, which can order a GG Taxi with a press of a single button.

The project is based around the *ESP32* SoC.

# How it works..

Using the same API as the web version: https://dashboard.ggtaxi.com/

Most of the API is RESTful, but the order creation part is done through a web-socket over HTTPS.

Schematics:

```
                                                   BUT1

                                   +----------+    +---+
+--+---+---+---+---+----+          |          |
|  |   |   |   |   |    |          | ESP 32   +----+   +-------+
|  |   |   |   |   |    +----------+          |GPIO23          |
+--+---+---+---+---+----+   GPIO13 |          |                |
    WS2812 LED STRIP               |          |              +---+
                                   |          |               +-+
                                   |          |                +
                                   +----------+
```

The LED strip is used as a display.

# Getting started

Do `make menuconfig` and set your *WiFi* and *GG account* options properly.

# Todo list

Mainly:

- [x] Login over HTTPS, getting auth tokens
- [x] Secure web-socket creation, encapsulation and things
- [x] Order creation / cancellation
- [ ] Car plate number display on the RGB leds ?
- [ ] Order status update on the LED strip

Maybe:

- [ ] HTTP server for configuration through Web ?
- [ ] Geolocation api ?
