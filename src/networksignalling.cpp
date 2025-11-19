// Written by Adrian Musceac YO8RZZ , started October 2025.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include "networksignalling.h"

NetworkSignalling::NetworkSignalling(const Settings *settings, Logger *logger, QObject *parent)
    : QObject{parent}
{
    _settings = settings;
    _logger = logger;
    _be = (QSysInfo::ByteOrder == QSysInfo::BigEndian) ? true : false;
}

NetworkSignalling::~NetworkSignalling()
{

}

uint64_t NetworkSignalling::getUnixTimestamp()
{
    return (uint64_t)QDateTime::currentSecsSinceEpoch();
}

bool NetworkSignalling::validateNetMessage(unsigned char *message, unsigned int size)
{
    if(size < 5)
    {
        _logger->log(Logger::LogLevelWarning, QString("Received invalid network message with size %1").arg(size));
        return false;
    }
    if (memcmp(message, "DMRT", 4U) != 0)
    {
        _logger->log(Logger::LogLevelWarning, QString("Received network message with unknown signature"));
        return false;
    }
    uint8_t opcode_validation = (uint8_t)message[4U] & 0x40;
    if(opcode_validation != 0x40)
    {
        _logger->log(Logger::LogLevelWarning, QString("Received network message with invalid opcode %1").arg(message[4U]));
        return false;
    }
    return true;
}

void NetworkSignalling::createRegistrationMessage(CDMRData &data , unsigned int dmrId)
{
    uint8_t size = 17;
    unsigned char buffer[size];
    uint64_t ts = getUnixTimestamp();
    ts = _be ? qbswap<quint64>(ts) : ts;
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
    ts = _be ? qbswap<quint64>(ts) : ts;
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
    {
        _logger->log(Logger::LogLevelWarning, QString("Cannot create subscription message with %1 talkgroups.").arg(tgs.size()));
        return false;
    }
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
    {
        _logger->log(Logger::LogLevelWarning, QString("Cannot create subscription message with %1 talkgroups.").arg(tgs.size()));
        return false;
    }
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

bool NetworkSignalling::createUDTTransferMessage(CDMRData &data, unsigned int srcId, unsigned int dstId,
                                                 QString payload, unsigned char format, bool group)
{
    unsigned int payload_size = payload.size();
    if(payload_size > 46)
        return false;
    char *text = payload.toUtf8().data();
    uint8_t buf_size = 29U + payload_size;
    unsigned char buffer[buf_size];
    uuid_t uuid;
    uuid_generate_random(uuid);
    buffer[0U]  = 'D';
    buffer[1U]  = 'M';
    buffer[2U]  = 'R';
    buffer[3U]  = 'T';
    buffer[4U]  = (unsigned char)UDTMessage;
    memcpy(buffer + 5U, uuid, 16U);
    buffer[21U] = (group ? 1 << 7 : 0) | (group ? 0 : 1 << 6) | (format & 0x0F);
    buffer[22U] = (unsigned char) payload_size;
    buffer[23U] = (unsigned char)((srcId >> 16U) & 0xFF);
    buffer[24U] = (unsigned char)((srcId >> 8U) & 0xFF);
    buffer[25U] = (unsigned char)(srcId & 0xFF);
    buffer[26U] = (unsigned char)((dstId >> 16U) & 0xFF);
    buffer[27U] = (unsigned char)((dstId >> 8U) & 0xFF);
    buffer[28U] = (unsigned char)(dstId & 0xFF);
    memcpy(buffer + 29U, text, payload_size);
    data.setMessage(buffer, buf_size);
    return true;
}

void NetworkSignalling::createUDTAcceptMessage(CDMRData &data, unsigned int srcId, unsigned int dstId, unsigned char *uuid)
{
    uint8_t buf_size = 27U;
    unsigned char buffer[buf_size];
    buffer[0U]  = 'D';
    buffer[1U]  = 'M';
    buffer[2U]  = 'R';
    buffer[3U]  = 'T';
    buffer[4U]  = (unsigned char)UDTAccept;
    memcpy(buffer + 5U, uuid, 16U);
    buffer[21U] = (unsigned char)((srcId >> 16U) & 0xFF);
    buffer[22U] = (unsigned char)((srcId >> 8U) & 0xFF);
    buffer[23U] = (unsigned char)(srcId & 0xFF);
    buffer[24U] = (unsigned char)((dstId >> 16U) & 0xFF);
    buffer[25U] = (unsigned char)((dstId >> 8U) & 0xFF);
    buffer[26U] = (unsigned char)(dstId & 0xFF);
    data.setMessage(buffer, buf_size);
}

void NetworkSignalling::createPrivateCallSetupMessage(CDMRData &data, unsigned int srcId, unsigned int dstId, unsigned char service_options)
{
    uint8_t buf_size = 28U;
    unsigned char buffer[buf_size];
    uuid_t uuid;
    uuid_generate_random(uuid);
    buffer[0U]  = 'D';
    buffer[1U]  = 'M';
    buffer[2U]  = 'R';
    buffer[3U]  = 'T';
    buffer[4U]  = (unsigned char)PrivateCallSetup;
    memcpy(buffer + 5U, uuid, 16U);
    buffer[21U] = service_options;
    buffer[22U] = (unsigned char)((srcId >> 16U) & 0xFF);
    buffer[23U] = (unsigned char)((srcId >> 8U) & 0xFF);
    buffer[24U] = (unsigned char)(srcId & 0xFF);
    buffer[25U] = (unsigned char)((dstId >> 16U) & 0xFF);
    buffer[26U] = (unsigned char)((dstId >> 8U) & 0xFF);
    buffer[27U] = (unsigned char)(dstId & 0xFF);
    data.setMessage(buffer, buf_size);
}

void NetworkSignalling::createPrivateCallReplyMessage(CDMRData &data, unsigned int srcId, unsigned int dstId, unsigned char *uuid, bool accept)
{
    uint8_t buf_size = 27U;
    unsigned char buffer[buf_size];
    buffer[0U]  = 'D';
    buffer[1U]  = 'M';
    buffer[2U]  = 'R';
    buffer[3U]  = 'T';
    buffer[4U]  = (unsigned char)(accept ? PrivateCallAccept : PrivateCallReject);
    memcpy(buffer + 5U, uuid, 16U);
    buffer[21U] = (unsigned char)((srcId >> 16U) & 0xFF);
    buffer[22U] = (unsigned char)((srcId >> 8U) & 0xFF);
    buffer[23U] = (unsigned char)(srcId & 0xFF);
    buffer[24U] = (unsigned char)((dstId >> 16U) & 0xFF);
    buffer[25U] = (unsigned char)((dstId >> 8U) & 0xFF);
    buffer[26U] = (unsigned char)(dstId & 0xFF);
    data.setMessage(buffer, buf_size);
}

void NetworkSignalling::createStatusTransferMessage(CDMRData &data, unsigned int srcId, unsigned int dstId, uint8_t status, bool group)
{
    uint8_t buf_size = 28U;
    unsigned char buffer[buf_size];
    uuid_t uuid;
    uuid_generate_random(uuid);
    buffer[0U]  = 'D';
    buffer[1U]  = 'M';
    buffer[2U]  = 'R';
    buffer[3U]  = 'T';
    buffer[4U]  = (unsigned char)StatusMessage;
    memcpy(buffer + 5U, uuid, 16U);
    buffer[21U] = (group ? 1 << 7 : 0) | (status & 0x7F);
    buffer[22U] = (unsigned char)((srcId >> 16U) & 0xFF);
    buffer[23U] = (unsigned char)((srcId >> 8U) & 0xFF);
    buffer[24U] = (unsigned char)(srcId & 0xFF);
    buffer[25U] = (unsigned char)((dstId >> 16U) & 0xFF);
    buffer[26U] = (unsigned char)((dstId >> 8U) & 0xFF);
    buffer[27U] = (unsigned char)(dstId & 0xFF);
    data.setMessage(buffer, buf_size);
}

bool NetworkSignalling::parseUDTTransferMessage(unsigned char* payload, unsigned int size, unsigned int &srcId, unsigned int &dstId,
                              QString &message, unsigned char &format, bool &group, unsigned char *uuid)
{
    if(size < 29U)
        return false;
    memcpy(uuid, payload + 5U, 16U);
    group = ((payload[21U] >> 7) == 0x01) ? true : false;
    format = (payload[21U] & 0x7F);
    unsigned int payload_size = (unsigned int)payload[22U];
    if(size < 29U + payload_size)
        return false;
    srcId = ((unsigned int)payload[23U] << 16U) | ((unsigned int)payload[24U] << 8U) | (unsigned int)payload[25U];
    dstId = ((unsigned int)payload[26U] << 16U) | ((unsigned int)payload[27U] << 8U) | (unsigned int)payload[28U];
    message = QString::fromUtf8((const char*)(payload + 29U), payload_size);
    return true;
}

bool NetworkSignalling::parseUDTAcceptMessage(unsigned char* payload, unsigned int size, unsigned int &srcId, unsigned int &dstId,
                              unsigned char *uuid)
{
    if(size < 27U)
        return false;
    memcpy(uuid, payload + 5U, 16U);
    srcId = ((unsigned int)payload[21U] << 16U) | ((unsigned int)payload[22U] << 8U) | (unsigned int)payload[23U];
    dstId = ((unsigned int)payload[24U] << 16U) | ((unsigned int)payload[25U] << 8U) | (unsigned int)payload[26U];
    return true;
}

bool NetworkSignalling::parseRegistrationConfirmationMessage(unsigned char* payload, unsigned int size, unsigned int &srcId, bool &accept)
{
    if(size < 18U)
        return false;
    accept = (bool)(payload[13U] & 0x01);
    srcId = ((unsigned int)payload[15U] << 16U) | ((unsigned int)payload[16U] << 8U) | (unsigned int)payload[17U];
    return true;
}

bool NetworkSignalling::parseSubscriptionConfirmationMessage(unsigned char* payload, unsigned int size, QList<unsigned int> &confirmed_tgs)
{
    if(size < 9U)
        return false;
    uint8_t tg_size = (uint8_t)payload[5U];
    if(tg_size < 1)
        return true;
    uint8_t k = 0;
    for(uint8_t i=0;i<tg_size;i++)
    {
        unsigned int tg = ((unsigned int)payload[6U + k] << 16U) | ((unsigned int)payload[7U + k] << 8U) | (unsigned int)payload[8U + k];
        confirmed_tgs.append(tg);
        k += 3;
    }
    return true;
}

bool NetworkSignalling::parseUnSubscriptionConfirmationMessage(unsigned char* payload, unsigned int size, QList<unsigned int> &confirmed_tgs)
{
    if(size < 9U)
        return false;
    uint8_t tg_size = (uint8_t)payload[5U];
    if(tg_size < 1)
        return true;
    uint8_t k = 0;
    for(uint8_t i=0;i<tg_size;i++)
    {
        unsigned int tg = ((unsigned int)payload[6U + k] << 16U) | ((unsigned int)payload[7U + k] << 8U) | (unsigned int)payload[8U + k];
        confirmed_tgs.append(tg);
        k += 3;
    }
    return true;
}
