# chrBLEMIDI

`chrBLEMIDI` is a small Arduino library for sending and receiving MIDI messages over Bluetooth Low Energy. It uses NimBLE-Arduino and exposes a callback-based API for transport and MIDI events.

## Features

- Start and stop a BLE MIDI peripheral with configurable device name and service UUIDs.
- Receive decoded MIDI messages through an event callback.
- Send BLE MIDI packets to connected clients.
- Monitor advertising, connections, transport state, and pending shutdowns.
- Handle BLE operations without blocking the application's `loop()`.

## Requirements

- An ESP32 board supported by NimBLE-Arduino.
- Arduino framework.
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) 2.5.1 or later.

PlatformIO installs the NimBLE dependency from the project configuration. The library dependency is also declared in [library.json](library.json).

## Quick Start

Register an event callback, configure the peripheral, and enable it:

```cpp
#include <Arduino.h>
#include "chrBLEMIDI.h"

void onMidiEvent(int8_t code, const char* action, uint8_t* data, uint32_t len) {
    Serial.printf("MIDI event %d: %s (%lu bytes)\n",
                  code, action ? action : "", static_cast<unsigned long>(len));
}

void setup() {
    Serial.begin(115200);

    chrBLEMIDI::Config config;
    config.name = "My BLE MIDI Device";

    chrBLEMIDI::setEventCallback(onMidiEvent);
    chrBLEMIDI::setup(config);

    if (!chrBLEMIDI::enable()) {
        Serial.println("Could not enable BLE MIDI");
    }
}

void loop() {
    chrBLEMIDI::loop();
}
```

The complete example, including sending a note to connected clients, is in [examples/Basic/Basic.cpp](examples/Basic/Basic.cpp).

## Sending MIDI

`write()` accepts a complete BLE MIDI packet, including the BLE MIDI header and timestamp bytes. For example, this sends middle C with velocity 100 using a zero timestamp:

```cpp
uint8_t noteOn[] = {0x80, 0x80, 0x90, 60, 100};
chrBLEMIDI::write(noteOn, sizeof(noteOn));
```

Call `write()` after the transport is enabled. The function returns `false` for empty data or when the transport is not ready. Received MIDI messages are reported as `EVENT_READ`; the callback's data pointer is only valid for the duration of the callback.

## Configuration

`chrBLEMIDI::Config` provides these defaults:

| Setting | Default |
| --- | --- |
| `name` | `Esp_BLE_Midi` |
| `midiServiceUuid` | `03B80E5A-EDE8-4B33-A751-6CE34EC4C700` |
| `midiCharacteristicUuid` | `7772E5DB-3868-4112-A1A9-F2669D106BF3` |
| `advertiseScanResponse` | `true` |
| `minPreferred` | `0x06` |
| `maxPreferred` | `0x12` |

Pass the configured value to `setup()` before calling `enable()`.

## Events and Lifecycle

Register one `EventCallback` with `setEventCallback()`. Each event provides a numeric code, a short action string, an optional data pointer, and its length. Event codes are declared in [src/chrBLEMIDI.h](src/chrBLEMIDI.h):

| Code | Value | Meaning |
| --- | ---: | --- |
| `EVENT_ERR` | -10 | Operation failed |
| `EVENT_WARN` | -1 | Operation was ignored or needs attention |
| `EVENT_OK` | 0 | General success |
| `EVENT_NOTICE` | 10 | Informational transport state |
| `EVENT_CONNECT` | 20 | BLE client connected |
| `EVENT_DISCONNECT` | 22 | BLE client disconnected |
| `EVENT_ENABLED` | 24 | Transport enabled |
| `EVENT_ADVERTISING` | 25 | Advertising started |
| `EVENT_DISABLED` | 26 | Transport disabled |
| `EVENT_READ` | 30 | MIDI message parsed |
| `EVENT_WRITE` | 32 | Data submitted for sending |

Call `loop()` continuously from the Arduino `loop()`. When `disable()` is called while a client is connected, the library disconnects clients and completes deinitialization asynchronously; keep calling `loop()` until `hasPendingDisable()` returns `false`.

The state helpers `isEnabled()`, `isInitialized()`, `isAdvertising()`, and `connectedCount()` report the current transport state.

## Build

This repository includes a PlatformIO configuration targeting `esp32-s3-devkitc-1`. From the project directory, run:

```sh
pio run
```

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.