#ifndef NETWORKSIGNALLING_H
#define NETWORKSIGNALLING_H

#include <QObject>
#include <QList>
#include <qsysinfo.h>
#include <QtEndian>
#include <time.h>
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
        DeRegistration = 0x02,
        NetDeRegistration = 0x42,
        GroupSubscription = 0x03,
        GroupUnSubscription = 0x04,
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
    };

    explicit NetworkSignalling(const Settings *settings, Logger *logger, QObject *parent = nullptr);
    ~NetworkSignalling();
    void createRegistrationMessage(CDMRData &data, unsigned int dmrId);
    void createDeRegistrationMessage(CDMRData &data, unsigned int dmrId);
    bool createGroupSubscriptionMessage(CDMRData &data, QList<unsigned int> tgs);
    bool createGroupUnSubscriptionMessage(CDMRData &data, QList<unsigned int> tgs);
    bool validateNetMessage(unsigned char *message, unsigned int size);


signals:

private:
    uint64_t getUnixTimestamp();
    const Settings *_settings;
    Logger *_logger;
    bool _le;

};

#endif // NETWORKSIGNALLING_H
