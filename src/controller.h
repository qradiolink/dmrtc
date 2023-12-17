// Written by Adrian Musceac YO8RZZ , started October 2023.
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

#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <QObject>
#include <QMutex>
#include <QVector>
#include <QList>
#include <QSet>
#include <QThread>
#include <QDateTime>
#include <QCoreApplication>
#include <QtConcurrent/QtConcurrent>
#include "src/settings.h"
#include "src/logger.h"
#include "src/udpclient.h"
#include "src/logicalchannel.h"
#include "src/standard_addresses.h"
#include "src/utils.h"
#include "src/dmrrewrite.h"
#include "src/gatewayrouter.h"
#include "src/signalling.h"
#include "MMDVM/DMRDefines.h"
#include "MMDVM/DMRData.h"
#include "MMDVM/DMRDataHeader.h"
#include "MMDVM/BPTC19696.h"
#include "MMDVM/DMRLC.h"
#include "MMDVM/DMRFullLC.h"
#include "MMDVM/DMRCSBK.h"
#include "MMDVM/DMRSlotType.h"
#include "MMDVM/CRC.h"
#include "MMDVM/Sync.h"

enum ServiceAction {
    ActionPingRequest,
    ActionMessageRequest,
    ActionPrivateCallRequest,
    RegistrationWithAttachment,
};

class Controller : public QObject
{
    Q_OBJECT
public:
    explicit Controller(Settings *settings, Logger *logger, QObject *parent = nullptr);
    ~Controller();
    QVector<LogicalChannel*>* getLogicalChannels();
    void announceSystemFreqs();
    void announceLateEntry();
    void announceLocalTime();
    void announceSystemMessage();

    CDMRCSBK createRegistrationRequest();

public slots:
    void run();
    void stop();
    void processDMRPayload(unsigned char *payload, int udp_channel_id, bool from_gateway);
    void updateMMDVMConfig(unsigned char* payload, int size);
    void writeDMRConfig();
    void handleIdleChannelDeallocation(unsigned int channel_id);
    void requestMassRegistration();
    void setChannelEnabled(unsigned int index, bool state);
    void sendUDTShortMessage(QString message, unsigned int target_id);
    void pingRadio(unsigned int target_id, bool group=false);
    void resetPing();

signals:
    void finished();
    void writeDMRData(CDMRData &dmr_data);
    void signalDMRConfig(QVector<unsigned char> data);
    void updateLogicalChannels(QVector<LogicalChannel*> *logical_channels);
    void updateCallLog(unsigned int srcId, unsigned int dstId, bool private_call);
    void updateRegisteredMSList(QList<unsigned int>* registered_ms);
    void updateRejectedCallsList(unsigned int srcId, unsigned int dstId, bool local_call);
    void updateMessageLog(unsigned int srcId, unsigned int dstId, QString message, bool tg);
    void pingResponse(unsigned int srcId, unsigned int time);

private:
    LogicalChannel *findNextFreePayloadChannel();
    LogicalChannel* findChannelByPhysicalIdAndSlot(unsigned int physical_id, unsigned int slot);
    LogicalChannel* findCallChannel(unsigned int dstId, unsigned int srcId);
    QVector<LogicalChannel*> findActiveChannels();
    bool validateLocalSourceId(unsigned int srcId);
    void processSignalling(CDMRData &dmr_data, int udp_channel_id);
    void processNetworkCSBK(CDMRData &dmr_data, int udp_channel_id);
    void transmitCSBK(CDMRCSBK &csbk, LogicalChannel *logical_channel, unsigned int slotNo,
                          unsigned int udp_channel_id, bool channel_grant=false, bool priority_queue=false, bool announce_priority=false);
    void processVoice(CDMRData &dmr_data, unsigned int udp_channel_id, bool data_sync, bool from_gateway=false);
    bool handleRegistration(CDMRCSBK &csbk, unsigned int slotNo, unsigned int srcId, unsigned int dstId, unsigned int &uab);
    void handleGroupCallRequest(CDMRCSBK &csbk, LogicalChannel *&logical_channel, unsigned int slotNo,
                                unsigned int srcId, unsigned int dstId, bool &channel_grant, bool local=false);
    void handlePrivateCallRequest(CDMRCSBK &csbk, LogicalChannel *&logical_channel, unsigned int slotNo,
                                unsigned int srcId, unsigned int dstId, bool &channel_grant, bool local=false);
    void contactMSForCall(CDMRCSBK &csbk, unsigned int slotNo,
                                unsigned int srcId, unsigned int dstId, bool local=false);
    void handleCallDisconnect(int udp_channel_id, bool group_call, unsigned int &srcId, unsigned int &dstId,
                              unsigned int slotNo, LogicalChannel *&logical_channel, CDMRCSBK &csbk);
    void handleLocalVoiceOnUnallocatedChannel(unsigned int call_type, unsigned int slotNo, unsigned int udp_channel_id);
    void processData(CDMRData &dmr_data, unsigned int udp_channel_id, bool from_gateway);

    LogicalChannel* _control_channel;
    Settings *_settings;
    Logger *_logger;
    GatewayRouter *_gateway_router;
    DMRRewrite *_dmr_rewrite;
    Signalling *_signalling_generator;
    QVector<unsigned char> *_mmdvm_config;
    QMutex _mutex;
    QVector<UDPClient*> _udp_channels;
    QVector<UDPClient*> _gateway_channels;
    QVector<LogicalChannel*> _logical_channels;
    QMap<unsigned int, unsigned int> _private_calls;
    QMap<unsigned int, unsigned int> _short_data_messages;
    QList<unsigned int> *_registered_ms;
    QMap<unsigned int, QList<unsigned int>> *_talkgroup_attachments;
    QMap<unsigned int, unsigned int> *_uplink_acks;
    QSet<unsigned int> *_rejected_calls;
    QSet<unsigned int> *_subscribed_talkgroups;

    std::chrono::high_resolution_clock::time_point t1_ping_ms;

    bool _stop_thread;
    bool _startup_completed;
    bool _late_entry_announcing;
    bool _system_freqs_announcing;
    unsigned int _minute;
    unsigned char _data_message[48];
    unsigned int _data_msg_size;
    unsigned int _data_block;
    unsigned int _data_pad_nibble;
    unsigned int _udt_format;

};

#endif // CONTROLLER_H
