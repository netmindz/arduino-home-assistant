#include "HAMqtt.h"

#ifndef ARDUINOHA_TEST
#include <PubSubClient.h>
#endif

#include "HADevice.h"
#include "device-types/BaseDeviceType.h"
#include "mocks/PubSubClientMock.h"

#define HAMQTT_INIT \
    _device(device), \
    _messageCallback(nullptr), \
    _connectedCallback(nullptr), \
    _connectionFailedCallback(nullptr), \
    _initialized(false), \
    _discoveryPrefix(DefaultDiscoveryPrefix), \
    _dataPrefix(DefaultDataPrefix), \
    _username(nullptr), \
    _password(nullptr), \
    _lastConnectionAttemptAt(0), \
    _devicesTypesNb(0), \
    _devicesTypes(nullptr), \
    _lastWillTopic(nullptr), \
    _lastWillMessage(nullptr), \
    _lastWillRetain(false)

static const char* DefaultDiscoveryPrefix = "homeassistant";
static const char* DefaultDataPrefix = "aha";

HAMqtt* HAMqtt::_instance = nullptr;

void onMessageReceived(char* topic, uint8_t* payload, unsigned int length)
{
    if (HAMqtt::instance() == nullptr || length > UINT16_MAX) {
        return;
    }

    HAMqtt::instance()->processMessage(topic, payload, static_cast<uint16_t>(length));
}

#ifdef ARDUINOHA_TEST
HAMqtt::HAMqtt(PubSubClientMock* pubSub, HADevice& device) :
    _mqtt(pubSub),
    HAMQTT_INIT
{
    _instance = this;
}
#else
HAMqtt::HAMqtt(Client& netClient, HADevice& device) :
    _mqtt(new PubSubClient(netClient)),
    HAMQTT_INIT
{
    _instance = this;
}
#endif

HAMqtt::~HAMqtt()
{
    if (_mqtt) {
        delete _mqtt;
    }

    _instance = nullptr;
}

bool HAMqtt::begin(
    const IPAddress serverIp,
    const uint16_t serverPort,
    const char* username,
    const char* password
)
{
    ARDUINOHA_DEBUG_PRINTF("AHA: init server %s:%d\n", String(serverIp).c_str(), serverPort);

    if (_device.getUniqueId() == nullptr) {
        ARDUINOHA_DEBUG_PRINTLN("AHA: init failed. Missing device unique ID");
        return false;
    }

    if (_initialized) {
        ARDUINOHA_DEBUG_PRINTLN("AHA: already initialized");
        return false;
    }

    _username = username;
    _password = password;
    _initialized = true;

    _mqtt->setServer(serverIp, serverPort);
    _mqtt->setCallback(onMessageReceived);

    return true;
}

bool HAMqtt::begin(
    const IPAddress serverIp,
    const char* username,
    const char* password
)
{
    return begin(serverIp, HAMQTT_DEFAULT_PORT, username, password);
}

bool HAMqtt::begin(
    const char* hostname,
    const uint16_t serverPort,
    const char* username,
    const char* password
)
{
    ARDUINOHA_DEBUG_PRINTF("AHA: init server %s:%d\n", hostname, serverPort);

    if (_device.getUniqueId() == nullptr) {
        ARDUINOHA_DEBUG_PRINTLN("AHA: init failed. Missing device unique ID");
        return false;
    }

    if (_initialized) {
        ARDUINOHA_DEBUG_PRINTLN("AHA: already initialized");
        return false;
    }

    _username = username;
    _password = password;
    _initialized = true;

    _mqtt->setServer(hostname, serverPort);
    _mqtt->setCallback(onMessageReceived);

    return true;
}

bool HAMqtt::begin(
    const char* hostname,
    const char* username,
    const char* password
)
{
    return begin(hostname, HAMQTT_DEFAULT_PORT, username, password);
}

bool HAMqtt::disconnect()
{
    if (!_initialized) {
        return false;
    }

    ARDUINOHA_DEBUG_PRINTLN("AHA: disconnecting");

    _initialized = false;
    _lastConnectionAttemptAt = 0;
    _mqtt->disconnect();

    return true;
}

void HAMqtt::loop()
{
    if (_initialized && !_mqtt->loop()) {
        connectToServer();
    }
}

bool HAMqtt::isConnected()
{
    return _mqtt->connected();
}

void HAMqtt::addDeviceType(BaseDeviceType* deviceType)
{
    BaseDeviceType** data = (BaseDeviceType**)realloc(
        _devicesTypes,
        sizeof(BaseDeviceType*) * (_devicesTypesNb + 1)
    );

    if (data != nullptr) {
        _devicesTypes = data;
        _devicesTypes[_devicesTypesNb] = deviceType;
        _devicesTypesNb++;
    }
}

bool HAMqtt::publish(const char* topic, const char* payload, bool retained)
{
    if (!isConnected()) {
        return false;
    }

    ARDUINOHA_DEBUG_PRINTF("AHA: publishing %s, len: %d\n", topic, strlen(payload));

    _mqtt->beginPublish(topic, strlen(payload), retained);
    _mqtt->write((const uint8_t*)(payload), strlen(payload));
    return _mqtt->endPublish();
}

bool HAMqtt::beginPublish(
    const char* topic,
    uint16_t payloadLength,
    bool retained
)
{
    ARDUINOHA_DEBUG_PRINTF("AHA: being publish %s, len: %d\n", topic, payloadLength);

    return _mqtt->beginPublish(topic, payloadLength, retained);
}

bool HAMqtt::writePayload(const char* data, uint16_t length)
{
    return (_mqtt->write((const uint8_t*)(data), length) > 0);
}

bool HAMqtt::writePayload_P(const char* src)
{
    char data[strlen_P(src) + 1];
    strcpy_P(data, src);

    return _mqtt->write((const uint8_t*)(data), strlen(data));
}

bool HAMqtt::endPublish()
{
    return _mqtt->endPublish();
}

bool HAMqtt::subscribe(const char* topic)
{
    ARDUINOHA_DEBUG_PRINTF("AHA: subscribing %s\n", topic);

    return _mqtt->subscribe(topic);
}

void HAMqtt::processMessage(char* topic, uint8_t* payload, uint16_t length)
{
    ARDUINOHA_DEBUG_PRINTF("AHA: received call %s, len: %d\n", topic, length);

    if (_messageCallback) {
        _messageCallback(topic, payload, length);
    }

    for (uint8_t i = 0; i < _devicesTypesNb; i++) {
        _devicesTypes[i]->onMqttMessage(topic, payload, length);
    }
}

void HAMqtt::connectToServer()
{
    if (_lastConnectionAttemptAt > 0 &&
            (millis() - _lastConnectionAttemptAt) < ReconnectInterval) {
        return;
    }

    _lastConnectionAttemptAt = millis();
    ARDUINOHA_DEBUG_PRINTF("AHA: connecting, client ID %s\n", _device.getUniqueId());

    _mqtt->connect(
        _device.getUniqueId(),
        _username,
        _password,
        _lastWillTopic,
        0,
        _lastWillRetain,
        _lastWillMessage,
        true
    );

    if (isConnected()) {
        ARDUINOHA_DEBUG_PRINTLN("AHA: connected");
        onConnectedLogic();
    } else {
        ARDUINOHA_DEBUG_PRINTLN("AHA: failed to connect");

        if (_connectionFailedCallback) {
            _connectionFailedCallback();
        }
    }
}

void HAMqtt::onConnectedLogic()
{
    if (_connectedCallback) {
        _connectedCallback();
    }

    _device.publishAvailability();

    for (uint8_t i = 0; i < _devicesTypesNb; i++) {
        _devicesTypes[i]->onMqttConnected();
    }
}
