#include <Arduino.h>
#include "chrBLEMIDI.h"

namespace {
constexpr uint8_t MIDI_NOTE = 60;
constexpr uint32_t NOTE_INTERVAL_MS = 1000;

uint32_t lastNoteTime = 0;
bool noteIsOn = false;

void onMidiEvent(int8_t code, uint8_t* data, uint32_t len) {
    const auto eventCode = static_cast<chrBLEMIDI::EventCode>(code);
    Serial.printf("MIDI event %d: %s (%lu bytes)",
                  code, chrBLEMIDI::eventName(eventCode), static_cast<unsigned long>(len));

    if (code == chrBLEMIDI::EVENT_READ && data != nullptr) {
        Serial.print(" data=");
        for (uint32_t i = 0; i < len; ++i) {
            Serial.printf("%02X ", data[i]);
        }
    }

    Serial.println();
}

void sendNote(bool turnOn) {
    const uint32_t timestamp = millis() & 0x1FFF;
    uint8_t packet[] = {
        static_cast<uint8_t>(0x80 | ((timestamp >> 7) & 0x3F)),
        static_cast<uint8_t>(0x80 | (timestamp & 0x7F)),
        static_cast<uint8_t>(turnOn ? 0x90 : 0x80),
        MIDI_NOTE,
        static_cast<uint8_t>(turnOn ? 100 : 0)
    };

    chrBLEMIDI::write(packet, sizeof(packet));
}
}

void setup() {
    Serial.begin(115200);
    Serial.println("Starting BLE MIDI example...");

    chrBLEMIDI::Config config;
    config.name = "chrBLEMIDI Basic";

    chrBLEMIDI::setEventCallback(onMidiEvent);
    chrBLEMIDI::setup(config);

    if (!chrBLEMIDI::enable()) {
        Serial.println("Failed to enable BLE MIDI");
    }
}

void loop() {
    chrBLEMIDI::loop();

    if (chrBLEMIDI::connectedCount() > 0 && millis() - lastNoteTime >= NOTE_INTERVAL_MS) {
        lastNoteTime = millis();
        sendNote(!noteIsOn);
        noteIsOn = !noteIsOn;
    }

    delay(5);
}