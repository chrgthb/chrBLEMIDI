#pragma once

#include <Arduino.h>
#include <functional>

namespace chrBLEMIDI {
    using EventCallback = std::function<void(int8_t, uint8_t*, uint32_t)>;

    // Event codes
    // - no data events get data == nullptr and len == 0
    enum EventCode {
        EVENT_ERR_ADVERT = -30,
        EVENT_ERR_CHAR = -25,
        EVENT_ERR_SERVICE = -20,
        EVENT_ERR_SERVER = -15,
        EVENT_ERR_INIT = -10,
        EVENT_WARN_TRANSP_NOT_READY = -6,
        EVENT_WARN_NODATA = -5,
        EVENT_INIT = 0,
        EVENT_NOTICE_WAIT = 10,
        EVENT_CONNECT = 20,
        EVENT_DISCONNECT = 25,
        EVENT_ENABLED = 30,
        EVENT_ADVERTISING = 35,
        EVENT_DISABLED = 40,
        EVENT_READ = 45,
        EVENT_WRITE = 50
    };

    struct Config {
        const char* name = "Esp_BLE_Midi";
        const char* midiServiceUuid = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
        const char* midiCharacteristicUuid = "7772E5DB-3868-4112-A1A9-F2669D106BF3";

        bool advertiseScanResponse = true;
        uint16_t minPreferred = 0x06;
        uint16_t maxPreferred = 0x12;
    };

    void setEventCallback(EventCallback cb);

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

    const char* eventName(EventCode code);

}