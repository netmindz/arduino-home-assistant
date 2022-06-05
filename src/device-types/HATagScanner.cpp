#include "HATagScanner.h"
#ifndef EX_ARDUINOHA_TAG_SCANNER

#include "../HAMqtt.h"
#include "../utils/HASerializer.h"

HATagScanner::HATagScanner(const char* uniqueId) :
    BaseDeviceType("tag", uniqueId)
{

}

bool HATagScanner::tagScanned(const char* tag)
{
    if (!tag || strlen(tag) == 0) {
        return false;
    }

    return publishOnDataTopic(HATopic, tag);
}

void HATagScanner::buildSerializer()
{
    if (_serializer || !uniqueId()) {
        return;
    }

    _serializer = new HASerializer(this);
    _serializer->set(HASerializer::WithDevice);
    _serializer->topic(HATopic);
}

void HATagScanner::onMqttConnected()
{
    if (!uniqueId()) {
        return;
    }

    publishConfig();
}

#endif
