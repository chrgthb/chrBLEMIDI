#pragma once

#include <Arduino.h>
#include <functional>

namespace chrBLEMIDI {
    using EventCallback = std::function<void(int8_t, const char*, uint8_t*, uint32_t)>;

    // Event codes
    // - no data events get data == nullptr and len == 0
    enum EventCode {
        EVENT_ERR = -10,
        EVENT_WARN = -1,
        EVENT_OK = 0,
        EVENT_NOTICE = 10,
        EVENT_CONNECT = 20,
        EVENT_DISCONNECT = 22,
        EVENT_ENABLED = 24,
        EVENT_ADVERTISING = 25,
        EVENT_DISABLED = 26,
        EVENT_READ = 30,
        EVENT_WRITE = 32
    };

    struct Config {
        const char* name = "Esp_BLE_Midi";
        const char* midiServiceUuid = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
        const char* midiCharacteristicUuid = "7772E5DB-3868-4112-A1A9-F2669D106BF3";

        bool advertiseScanResponse = true;
        uint16_t minPreferred = 0x06;
        uint16_t maxPreferred = 0x12;
    };

    void onEvent(EventCallback cb);

    void setup(const Config& config);
    const Config& getConfig();

    bool enable();
    void disable();
    void loop();

    // Send MIDI data programmatically over the BLE MIDI characteristic.
    bool write(uint8_t* data, uint32_t len);

    bool isEnabled();
    bool isInitialized();
    bool isAdvertising();
    bool hasPendingDisable();
    uint16_t connectedCount();

}