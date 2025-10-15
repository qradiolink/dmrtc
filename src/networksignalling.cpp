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
    ts = _be ? ts : qbswap<quint64>(ts);
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
    ts = _be ? ts : qbswap<quint64>(ts);
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

bool NetworkSignalling::createUDTTransferMessage(CDMRData &data, unsigned int srcId, unsigned int dstId,
                                                 QString payload, unsigned char format, bool group)
{
    unsigned int payload_size = payload.size();
    if(payload_size > 46)
        return false;
    char *text = payload.toUtf8().data();
    uint8_t buf_size = 30U + payload_size;
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
    buffer[22U] = 0;
    buffer[23U] = (unsigned char) payload_size;
    buffer[24U] = (unsigned char)((srcId >> 16U) & 0xFF);
    buffer[25U] = (unsigned char)((srcId >> 8U) & 0xFF);
    buffer[26U] = (unsigned char)(srcId & 0xFF);
    buffer[27U] = (unsigned char)((dstId >> 16U) & 0xFF);
    buffer[28U] = (unsigned char)((dstId >> 8U) & 0xFF);
    buffer[29U] = (unsigned char)(dstId & 0xFF);
    memcpy(buffer + 30U, text, payload_size);
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
