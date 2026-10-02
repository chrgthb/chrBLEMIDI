#include "chrBLEMIDI.h"

#include <NimBLEDevice.h>
#include <cstring>

namespace chrBLEMIDI {
    static EventCallback onEventCallback = nullptr;
    static Config config;

    static bool enabled = false;
    static bool deinitPending = false;
    static NimBLEServer* server = nullptr;
    static NimBLECharacteristic* characteristic = nullptr;
    static void fireEvent(int8_t code, const char* action, uint8_t* data = nullptr, uint32_t len = 0) {
        if (!onEventCallback) return;
        onEventCallback(code, action, data, len);
    }

    static bool startAdvertising() {
        NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
        if (advertising == nullptr || !advertising->start()) {
            fireEvent(EVENT_ERR, "advertising failed", nullptr, 0);
            return false;
        }

        fireEvent(EVENT_ADVERTISING, "advertising started", nullptr, 0);
        return true;
    }

    static uint8_t getMidiDataLength(uint8_t status) {
        switch (status & 0xF0) {
            case 0x80:
            case 0x90:
            case 0xA0:
            case 0xB0:
            case 0xE0:
                return 2;
            case 0xC0:
            case 0xD0:
                return 1;
            default:
                if (status == 0xF2) return 2;
                if (status == 0xF1 || status == 0xF3) return 1;
                return 0;
        }
    }

    static void emitReadEvent(uint8_t* msg, size_t len) {
        if (len == 0) return;
        fireEvent(EVENT_READ, "read", msg, static_cast<uint32_t>(len));
    }

    static void processMidiPayload(const uint8_t* data, size_t len) {
        if (data == nullptr || len < 3) return;

        size_t ptr = 1;
        uint8_t runningStatus = 0;

        while (ptr < len) {
            if (data[ptr] >= 0x80) ptr++;
            if (ptr >= len) break;

            uint8_t msgBuf[128];
            size_t msgLen = 0;

            uint8_t currentByte = data[ptr];
            if (currentByte >= 0x80) {
                if (currentByte < 0xF8) runningStatus = currentByte;
                if (msgLen < sizeof(msgBuf)) msgBuf[msgLen++] = currentByte;
                ptr++;
            } else {
                if (runningStatus != 0) {
                    if (msgLen < sizeof(msgBuf)) msgBuf[msgLen++] = runningStatus;
                } else {
                    ptr++;
                    continue;
                }
            }

            const bool isSysex = (msgLen > 0 && msgBuf[0] == 0xF0);

            if (isSysex) {
                while (ptr < len && data[ptr] < 0x80) {
                    if (msgLen < sizeof(msgBuf)) msgBuf[msgLen++] = data[ptr];
                    ptr++;
                }

                if (ptr + 1 < len && data[ptr] >= 0x80 && data[ptr + 1] == 0xF7) {
                    if (msgLen < sizeof(msgBuf)) msgBuf[msgLen++] = 0xF7;
                    ptr += 2;
                } else if (ptr < len && data[ptr] == 0xF7) {
                    if (msgLen < sizeof(msgBuf)) msgBuf[msgLen++] = data[ptr];
                    ptr++;
                }
            } else if (msgLen > 0 && msgBuf[0] < 0xF8) {
                uint8_t expectedDataLen = getMidiDataLength(msgBuf[0]);
                for (uint8_t i = 0; i < expectedDataLen && ptr < len; i++) {
                    if (data[ptr] < 0x80) {
                        if (msgLen < sizeof(msgBuf)) msgBuf[msgLen++] = data[ptr];
                        ptr++;
                    } else {
                        break;
                    }
                }
            }

            emitReadEvent(msgBuf, msgLen);
        }
    }

    static void finishDisable() {
        server = nullptr;
        characteristic = nullptr;
        deinitPending = false;
        fireEvent(EVENT_DISABLED, "transport disabled", nullptr, 0);
    }

    class ServerCallbacks : public NimBLEServerCallbacks {
    public:
        void onConnect(NimBLEServer* connectedServer, NimBLEConnInfo& connInfo) override {
            (void)connectedServer;
            (void)connInfo;
            fireEvent(EVENT_CONNECT, "connected", nullptr, 0);
        }

        void onDisconnect(NimBLEServer* disconnectedServer, NimBLEConnInfo& connInfo, int reason) override {
            (void)disconnectedServer;
            (void)connInfo;
            (void)reason;
            fireEvent(EVENT_DISCONNECT, "disconnected", nullptr, 0);

            if (enabled) {
                startAdvertising();
            }
        }
    };

    class CharacteristicCallbacks : public NimBLECharacteristicCallbacks {
    public:
        void onWrite(NimBLECharacteristic* targetCharacteristic, NimBLEConnInfo& connInfo) override {
            (void)connInfo;
            if (!enabled) return;

            const NimBLEAttValue value = targetCharacteristic->getValue();
            const uint8_t* data = value.data();
            const size_t len = value.size();

            if (data != nullptr && len > 0) {
                processMidiPayload(data, len);
            }
        }

    };

    void onEvent(EventCallback cb) {
        onEventCallback = cb;
    }

    void setup(const Config& newConfig) {
        config = newConfig;
    }

    const Config& getConfig() {
        return config;
    }

    bool enable() {
        deinitPending = false;

        if (enabled && NimBLEDevice::isInitialized()) {
            return true;
        }

        if (!NimBLEDevice::isInitialized() || server == nullptr) {
            if (!NimBLEDevice::init(config.name)) {
                fireEvent(EVENT_ERR, "init failed", nullptr, 0);
                return false;
            }

            server = NimBLEDevice::createServer();
            if (server == nullptr) {
                fireEvent(EVENT_ERR, "create server failed", nullptr, 0);
                return false;
            }

            server->setCallbacks(new ServerCallbacks());

            NimBLEService* midiService = server->createService(config.midiServiceUuid);
            if (midiService == nullptr) {
                fireEvent(EVENT_ERR, "create service failed", nullptr, 0);
                return false;
            }

            characteristic = midiService->createCharacteristic(
                config.midiCharacteristicUuid,
                NIMBLE_PROPERTY::READ |
                    NIMBLE_PROPERTY::WRITE |
                    NIMBLE_PROPERTY::WRITE_NR |
                    NIMBLE_PROPERTY::NOTIFY |
                    NIMBLE_PROPERTY::INDICATE
            );

            if (characteristic == nullptr) {
                fireEvent(EVENT_ERR, "create characteristic failed", nullptr, 0);
                return false;
            }

            characteristic->setCallbacks(new CharacteristicCallbacks());
            static const uint8_t emptyValue[] = {0};
            characteristic->setValue(emptyValue, 0);

            server->start();
        }

        NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
        if (advertising == nullptr) {
            fireEvent(EVENT_ERR, "get advertising failed", nullptr, 0);
            return false;
        }

        advertising->addServiceUUID(config.midiServiceUuid);
        advertising->enableScanResponse(config.advertiseScanResponse);
        advertising->setPreferredParams(config.minPreferred, config.maxPreferred);

        if (!startAdvertising()) {
            return false;
        }

        enabled = true;

        fireEvent(EVENT_ENABLED, "transport enabled", nullptr, 0);
        return true;
    }

    void disable() {
        if (!enabled && !NimBLEDevice::isInitialized()) return;

        enabled = false;
        NimBLEDevice::stopAdvertising();

        if (NimBLEDevice::isInitialized() && server != nullptr && server->getConnectedCount() > 0) {
            auto peers = server->getPeerDevices();
            for (const auto& peer : peers) {
                server->disconnect(peer);
            }

            deinitPending = true;
            fireEvent(EVENT_NOTICE, "waiting for disconnection", nullptr, 0);
            return;
        }

        if (NimBLEDevice::isInitialized()) {
            NimBLEDevice::deinit();
        }

        finishDisable();
    }

    void loop() {
        if (deinitPending && NimBLEDevice::isInitialized() && server != nullptr && server->getConnectedCount() == 0) {
            NimBLEDevice::deinit();
            finishDisable();
        }
    }

    bool write(uint8_t* data, uint32_t len) {
        if (data == nullptr || len == 0) {
            fireEvent(EVENT_WARN, "write ignored: empty data", nullptr, 0);
            return false;
        }

        if (!enabled || !NimBLEDevice::isInitialized() || characteristic == nullptr) {
            fireEvent(EVENT_WARN, "write ignored: transport not ready", nullptr, 0);
            return false;
        }

        characteristic->setValue(data, len);
        characteristic->notify();
        processMidiPayload(data, len);

        fireEvent(EVENT_WRITE, "write", data, len);
        return true;
    }

    bool isEnabled() {
        return enabled;
    }

    bool isInitialized() {
        return NimBLEDevice::isInitialized();
    }

    bool isAdvertising() {
        NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
        return advertising != nullptr && advertising->isAdvertising();
    }

    bool hasPendingDisable() {
        return deinitPending;
    }

    uint16_t connectedCount() {
        if (!NimBLEDevice::isInitialized() || server == nullptr) return 0;
        return server->getConnectedCount();
    }

}