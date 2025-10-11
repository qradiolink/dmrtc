#include "networksignalling.h"

NetworkSignalling::NetworkSignalling(const Settings *settings, Logger *logger, QObject *parent)
    : QObject{parent}
{
    _settings = settings;
    _logger = logger;
}

NetworkSignalling::~NetworkSignalling()
{

}

uint64_t NetworkSignalling::getUnixTimestamp()
{
    return (uint64_t)timegm(NULL);
}

bool NetworkSignalling::validateNetMessage(unsigned char *message, unsigned int size)
{
    if(size < 9)
    {
        _logger->log(Logger::LogLevelWarning, QString("Received invalid network message with size %1").arg(size));
        return false;
    }
    if (memcmp(message, "DMRT", 4U) != 0)
    {
        _logger->log(Logger::LogLevelWarning, QString("Received network message with unknown signature"));
        return false;
    }
    unsigned char opcode = message[4U];
    if((opcode & 0x40) != 0x40)
    {
        _logger->log(Logger::LogLevelWarning, QString("Received network message with invalid opcode %1").arg(opcode));
        return false;
    }
    return true;
}

void NetworkSignalling::createRegistrationMessage(unsigned char* buffer, unsigned int &size, unsigned int dmrId)
{
    size = 17;
    uint64_t ts = getUnixTimestamp();
    buffer[0U]  = 'D';
    buffer[1U]  = 'M';
    buffer[2U]  = 'R';
    buffer[3U]  = 'T';
    buffer[4U]  = (unsigned char)Registration;
    memcpy(buffer + 5U, &ts, sizeof(uint64_t));
    buffer[13U] = 1;
    buffer[14U] = (unsigned char)((dmrId >> 16U) & 0xFF);
    buffer[15U] = (unsigned char)((dmrId >> 8U) & 0xFF);
    buffer[16U] = (unsigned char)(dmrId & 0xFF);
}

void NetworkSignalling::createDeRegistrationMessage(unsigned char* buffer, unsigned int &size, unsigned int dmrId)
{
    size = 17;
    uint64_t ts = getUnixTimestamp();
    buffer[0U]  = 'D';
    buffer[1U]  = 'M';
    buffer[2U]  = 'R';
    buffer[3U]  = 'T';
    buffer[4U]  = (unsigned char)DeRegistration;
    memcpy(buffer + 5U, &ts, sizeof(uint64_t));
    buffer[13U] = 1;
    buffer[14U] = (unsigned char)((dmrId >> 16U) & 0xFF);
    buffer[15U] = (unsigned char)((dmrId >> 8U) & 0xFF);
    buffer[16U] = (unsigned char)(dmrId & 0xFF);
}

bool NetworkSignalling::createGroupSubscriptionMessage(unsigned char* buffer, unsigned int &size, QList<unsigned int> tgs)
{
    if((tgs.size() < 1) || (tgs.size() > 64))
        return false;
    size = 6U + ((uint32_t)tgs.size() * 3U);
    buffer[0U]  = 'D';
    buffer[1U]  = 'M';
    buffer[2U]  = 'R';
    buffer[3U]  = 'T';
    buffer[4U]  = (unsigned char)GroupSubscription;
    buffer[5U]  = (unsigned char)(tgs.size() & 0xFF);
    for(int i=0,j=0;i<tgs.size();i++,j=j+3)
    {
        buffer[6U + j] = (unsigned char)((tgs.at(i) >> 16U) & 0xFF);
        buffer[6U + j + 1] = (unsigned char)((tgs.at(i) >> 8U) & 0xFF);
        buffer[6U + j + 2] = (unsigned char)(tgs.at(i) & 0xFF);
    }
    return true;
}

bool NetworkSignalling::createGroupUnSubscriptionMessage(unsigned char* buffer, unsigned int &size, QList<unsigned int> tgs)
{
    if((tgs.size() < 1) || (tgs.size() > 64))
        return false;
    size = 6U + ((uint32_t)tgs.size() * 3U);
    buffer[0U]  = 'D';
    buffer[1U]  = 'M';
    buffer[2U]  = 'R';
    buffer[3U]  = 'T';
    buffer[4U]  = (unsigned char)GroupUnSubscription;
    buffer[5U]  = (unsigned char)(tgs.size() & 0xFF);
    for(int i=0,j=0;i<tgs.size();i++,j=j+3)
    {
        buffer[6U + j] = (unsigned char)((tgs.at(i) >> 16U) & 0xFF);
        buffer[6U + j + 1] = (unsigned char)((tgs.at(i) >> 8U) & 0xFF);
        buffer[6U + j + 2] = (unsigned char)(tgs.at(i) & 0xFF);
    }
    return true;
}
