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

#ifndef NETWORKSIGNALLING_H
#define NETWORKSIGNALLING_H

#include <QObject>
#include <QList>
#include <qsysinfo.h>
#include <QtEndian>
#include <QDateTime>
#include <uuid/uuid.h>
#include "MMDVM/DMRData.h"
#include "settings.h"
#include "logger.h"

class NetworkSignalling : public QObject
{
    Q_OBJECT
public:
    enum OpCode
    {
        Registration = 0x01,
        RegistrationConfirmation = 0x41,
        DeRegistration = 0x02,
        NetDeRegistration = 0x42,
        GroupSubscription = 0x03,
        GroupSubscriptionConfirmation = 0x43,
        GroupUnSubscription = 0x04,
        GroupUnSubscriptionConfirmation = 0x44,
        UDTMessage = 0x05,
        NetUDTMessage = 0x45,
        UDTAccept = 0x06,
        NetUDTAccept = 0x46,
        PrivateCallSetup = 0x07,
        NetPrivateCallSetup = 0x47,
        PrivateCallAccept = 0x08,
        NetPrivateCallAccept = 0x48,
        PrivateCallReject = 0x09,
        NetPrivateCallReject = 0x49,
        StatusMessage = 0x0A,
        NetStatusMessage = 0x4A,
        LoginConfirmation = 0xC1,
        MasterClose = 0xC2,
        ChannelEnableDisable = 0x81,
        RCCeaseTransmission = 0x82,
        RCRequestCeaseTransmission = 0x83,
        RCPowerIncreaseOneStep = 0x84,
        RCPowerDecreaseOneStep = 0x85,
        RCMaximumPower = 0x86,
        RCMinimumPower = 0x87,
    };

    explicit NetworkSignalling(const Settings *settings, Logger *logger, QObject *parent = nullptr);
    ~NetworkSignalling();

    bool validateNetMessage(unsigned char *message, unsigned int size);

    void createRegistrationMessage(CDMRData &data, unsigned int dmrId);
    void createDeRegistrationMessage(CDMRData &data, unsigned int dmrId);
    bool createGroupSubscriptionMessage(CDMRData &data, QList<unsigned int> tgs);
    bool createGroupUnSubscriptionMessage(CDMRData &data, QList<unsigned int> tgs);
    bool createUDTTransferMessage(CDMRData &data, unsigned int srcId, unsigned int dstId,
                                  QString payload, unsigned char format, bool group);
    void createUDTAcceptMessage(CDMRData &data, unsigned int srcId, unsigned int dstId, unsigned char *uuid);
    void createPrivateCallSetupMessage(CDMRData &data, unsigned int srcId, unsigned int dstId, unsigned char service_options);
    void createPrivateCallReplyMessage(CDMRData &data, unsigned int srcId, unsigned int dstId, unsigned char *uuid, bool accept);
    void createStatusTransferMessage(CDMRData &data, unsigned int srcId, unsigned int dstId, uint8_t status, bool group);
    bool parseUDTTransferMessage(unsigned char *payload, unsigned int size, unsigned int &srcId, unsigned int &dstId,
                                  QString &message, unsigned char &format, bool &group, unsigned char *uuid);
    bool parseUDTAcceptMessage(unsigned char* payload, unsigned int size, unsigned int &srcId, unsigned int &dstId,
                               unsigned char *uuid);
    bool parseRegistrationConfirmationMessage(unsigned char* payload, unsigned int size, unsigned int &srcId, bool &accept);
    bool parseSubscriptionConfirmationMessage(unsigned char* payload, unsigned int size, QList<unsigned int> &confirmed_tgs);
    bool parseUnSubscriptionConfirmationMessage(unsigned char* payload, unsigned int size, QList<unsigned int> &confirmed_tgs);



signals:

private:
    uint64_t getUnixTimestamp();
    const Settings *_settings;
    Logger *_logger;
    bool _be;

};

#endif // NETWORKSIGNALLING_H
