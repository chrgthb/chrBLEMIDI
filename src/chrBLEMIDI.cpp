#include "chrBLEMIDI.h"

#include <NimBLEDevice.h>
#include <cstring>

namespace chrBLEMIDI {
namespace {
    EventCallback _onEvent = nullptr;
    Config _config;

    bool _enabled = false;
    bool _deinitPending = false;
    NimBLEServer* _server = nullptr;
    NimBLECharacteristic* _characteristic = nullptr;
    void _fireEvent(int8_t code, const char* action, uint8_t* data = nullptr, uint32_t len = 0) {
        if (!_onEvent) return;
        _onEvent(code, action, data, len);
    }

    bool _startAdvertising() {
        NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
        if (advertising == nullptr || !advertising->start()) {
            _fireEvent(EVENT_ERR, "advertising failed", nullptr, 0);
            return false;
        }

        _fireEvent(EVENT_ADVERTISING, "advertising started", nullptr, 0);
        return true;
    }

    uint8_t _getMidiDataLength(uint8_t status) {
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

    void _emitReadEvent(uint8_t* msg, size_t len) {
        if (len == 0) return;
        _fireEvent(EVENT_READ, "read", msg, static_cast<uint32_t>(len));
    }

    void _processMidiPayload(const uint8_t* data, size_t len) {
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
                uint8_t expectedDataLen = _getMidiDataLength(msgBuf[0]);
                for (uint8_t i = 0; i < expectedDataLen && ptr < len; i++) {
                    if (data[ptr] < 0x80) {
                        if (msgLen < sizeof(msgBuf)) msgBuf[msgLen++] = data[ptr];
                        ptr++;
                    } else {
                        break;
                    }
                }
            }

            _emitReadEvent(msgBuf, msgLen);
        }
    }

    void _finishDisable() {
        _server = nullptr;
        _characteristic = nullptr;
        _deinitPending = false;
        _fireEvent(EVENT_DISABLED, "transport disabled", nullptr, 0);
    }

    class _ServerCallbacks : public NimBLEServerCallbacks {
    public:
        void onConnect(NimBLEServer* connectedServer, NimBLEConnInfo& connInfo) override {
            (void)connectedServer;
            (void)connInfo;
            _fireEvent(EVENT_CONNECT, "connected", nullptr, 0);
        }

        void onDisconnect(NimBLEServer* disconnectedServer, NimBLEConnInfo& connInfo, int reason) override {
            (void)disconnectedServer;
            (void)connInfo;
            (void)reason;
            _fireEvent(EVENT_DISCONNECT, "disconnected", nullptr, 0);

            if (_enabled) {
                _startAdvertising();
            }
        }
    };

    class _CharacteristicCallbacks : public NimBLECharacteristicCallbacks {
    public:
        void onWrite(NimBLECharacteristic* targetCharacteristic, NimBLEConnInfo& connInfo) override {
            (void)connInfo;
            if (!_enabled) return;

            const NimBLEAttValue value = targetCharacteristic->getValue();
            const uint8_t* data = value.data();
            const size_t len = value.size();

            if (data != nullptr && len > 0) {
                _processMidiPayload(data, len);
            }
        }

    };
}

    void setEventCallback(EventCallback cb) {
        _onEvent = cb;
    }

    void setup(const Config& newConfig) {
        _fireEvent(EVENT_NOTICE, "init...", nullptr, 0);
        _config = newConfig;
    }

    const Config& getConfig() {
        return _config;
    }

    bool enable() {
        _deinitPending = false;

        if (_enabled && NimBLEDevice::isInitialized()) {
            return true;
        }

        if (!NimBLEDevice::isInitialized() || _server == nullptr) {
            if (!NimBLEDevice::init(_config.name)) {
                _fireEvent(EVENT_ERR, "init failed", nullptr, 0);
                return false;
            }

            _server = NimBLEDevice::createServer();
            if (_server == nullptr) {
                _fireEvent(EVENT_ERR, "create server failed", nullptr, 0);
                return false;
            }

            _server->setCallbacks(new _ServerCallbacks());

            NimBLEService* midiService = _server->createService(_config.midiServiceUuid);
            if (midiService == nullptr) {
                _fireEvent(EVENT_ERR, "create service failed", nullptr, 0);
                return false;
            }

            _characteristic = midiService->createCharacteristic(
                _config.midiCharacteristicUuid,
                NIMBLE_PROPERTY::READ |
                    NIMBLE_PROPERTY::WRITE |
                    NIMBLE_PROPERTY::WRITE_NR |
                    NIMBLE_PROPERTY::NOTIFY |
                    NIMBLE_PROPERTY::INDICATE
            );

            if (_characteristic == nullptr) {
                _fireEvent(EVENT_ERR, "create characteristic failed", nullptr, 0);
                return false;
            }

            _characteristic->setCallbacks(new _CharacteristicCallbacks());
            static const uint8_t emptyValue[] = {0};
            _characteristic->setValue(emptyValue, 0);

            _server->start();
        }

        NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
        if (advertising == nullptr) {
            _fireEvent(EVENT_ERR, "get advertising failed", nullptr, 0);
            return false;
        }

        advertising->addServiceUUID(_config.midiServiceUuid);
        advertising->enableScanResponse(_config.advertiseScanResponse);
        advertising->setPreferredParams(_config.minPreferred, _config.maxPreferred);

        if (!_startAdvertising()) {
            return false;
        }

        _enabled = true;

        _fireEvent(EVENT_ENABLED, "transport enabled", nullptr, 0);
        return true;
    }

    void disable() {
        if (!_enabled && !NimBLEDevice::isInitialized()) return;

        _enabled = false;
        NimBLEDevice::stopAdvertising();

        if (NimBLEDevice::isInitialized() && _server != nullptr && _server->getConnectedCount() > 0) {
            auto peers = _server->getPeerDevices();
            for (const auto& peer : peers) {
                _server->disconnect(peer);
            }

            _deinitPending = true;
            _fireEvent(EVENT_NOTICE, "waiting for disconnection", nullptr, 0);
            return;
        }

        if (NimBLEDevice::isInitialized()) {
            NimBLEDevice::deinit();
        }

        _finishDisable();
    }

    void loop() {
        if (_deinitPending && NimBLEDevice::isInitialized() && _server != nullptr && _server->getConnectedCount() == 0) {
            NimBLEDevice::deinit();
            _finishDisable();
        }
    }

    bool write(uint8_t* data, uint32_t len) {
        if (data == nullptr || len == 0) {
            _fireEvent(EVENT_WARN, "write ignored: empty data", nullptr, 0);
            return false;
        }

        if (!_enabled || !NimBLEDevice::isInitialized() || _characteristic == nullptr) {
            _fireEvent(EVENT_WARN, "write ignored: transport not ready", nullptr, 0);
            return false;
        }

        _characteristic->setValue(data, len);
        _characteristic->notify();
        _processMidiPayload(data, len);

        _fireEvent(EVENT_WRITE, "write", data, len);
        return true;
    }

    bool isEnabled() {
        return _enabled;
    }

    bool isInitialized() {
        return NimBLEDevice::isInitialized();
    }

    bool isAdvertising() {
        NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
        return advertising != nullptr && advertising->isAdvertising();
    }

    bool hasPendingDisable() {
        return _deinitPending;
    }

    uint16_t connectedCount() {
        if (!NimBLEDevice::isInitialized() || _server == nullptr) return 0;
        return _server->getConnectedCount();
    }

}