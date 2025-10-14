#include "networksignalling.h"

NetworkSignalling::NetworkSignalling(const Settings *settings, Logger *logger, QObject *parent)
    : QObject{parent}
{
    _settings = settings;
    _logger = logger;
    _le = (QSysInfo::ByteOrder == QSysInfo::BigEndian) ? false : true;
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

void NetworkSignalling::createRegistrationMessage(CDMRData &data , unsigned int dmrId)
{
    uint8_t size = 17;
    unsigned char buffer[size];
    uint64_t ts = getUnixTimestamp();
    ts = _le ? ts : qToLittleEndian(ts);
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
    data.setMessage(buffer, size);

}

void NetworkSignalling::createDeRegistrationMessage(CDMRData &data, unsigned int dmrId)
{
    uint8_t size = 17;
    unsigned char buffer[size];
    uint64_t ts = getUnixTimestamp();
    ts = _le ? ts : qToLittleEndian(ts);
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
    data.setMessage(buffer, size);
}

bool NetworkSignalling::createGroupSubscriptionMessage(CDMRData &data, QList<unsigned int> tgs)
{
    if((tgs.size() < 1) || (tgs.size() > 64))
        return false;
    uint8_t size = 6U + ((uint32_t)tgs.size() * 3U);
    unsigned char buffer[size];
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
    data.setMessage(buffer, size);
    return true;
}

bool NetworkSignalling::createGroupUnSubscriptionMessage(CDMRData &data, QList<unsigned int> tgs)
{
    if((tgs.size() < 1) || (tgs.size() > 64))
        return false;
    uint8_t size = 6U + ((uint32_t)tgs.size() * 3U);
    unsigned char buffer[size];
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
    data.setMessage(buffer, size);
    return true;
}
