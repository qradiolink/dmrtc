/*
 *   Copyright (C) 2023-2026 by Adrian Musceac YO8RZZ
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

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
#include <QtConcurrent/qtconcurrentrun.h>
#include <cstdint>
#include "src/settings.h"
#include "src/logger.h"
#include "src/logicalchannel.h"
#include "src/standard_PDU.h"
#include "src/trunking_utils.h"
#include "src/dmrrewrite.h"
#include "src/dmridlookup.h"
#include "src/gatewayrouter.h"
#include "src/udpclient.h"
#include "src/signalling.h"
#include "src/dmrmessagehandler.h"
#include "src/ackhandler.h"
#include "src/networksignalling.h"
#include "src/rc4.h"
#include "src/dmr_commands.h"
#include "src/MMDVM/DMRDefines.h"
#include "src/MMDVM/DMRData.h"
#include "src/MMDVM/DMRDataHeader.h"
#include "src/MMDVM/BPTC19696.h"
#include "src/MMDVM/DMRLC.h"
#include "src/MMDVM/DMRFullLC.h"
#include "src/MMDVM/DMRCSBK.h"
#include "src/MMDVM/DMRSlotType.h"
#include "src/MMDVM/CRC.h"
#include "src/MMDVM/Sync.h"


namespace ServiceAction
{
    enum ServiceAction {
        ActionPingRequest,
        ActionMessageRequest,
        ActionDGNARequest,
        ActionPrivateVoiceCallRequest,
        ActionPrivatePacketCallRequest,
        RegistrationWithAttachment,
        CallDivert,
        UDTPoll,
        ActionAuthCheck,
        ActionStatusMsg,
        ActionStatusPoll,
    };
}

class Controller : public QObject
{
    Q_OBJECT
public:
    explicit Controller(Settings* settings, Logger* logger, DMRIdLookup* id_lookup, QObject* parent = nullptr);
    ~Controller();
    QVector<LogicalChannel*>* getLogicalChannels();
    void announceLateEntry();
    CDMRCSBK createRegistrationRequest();

public slots:
    void run();
    void stop();
    void inputNetDMRPayload(unsigned char* payload, unsigned int size, unsigned int udp_channel_id, bool from_gateway);
    void processDMRNetworkMessage(unsigned char* payload, unsigned int size);
    void updateChannelsToGUI();
    void setCallStats(unsigned int srcId, unsigned int dstId, float rssi, float ber, float max_ber, unsigned int call_time, bool private_call);
    void handleIdleChannelDeallocation(unsigned int channel_id);
    void requestMassRegistration();
    bool userRegister(unsigned int dmrId);
    bool userDeRegister(unsigned int dmrId);
    void setChannelEnabled(unsigned int index, bool state);
    void sendUDTShortMessage(QString message, unsigned int dstId, unsigned int srcId = 0, bool group = false, bool forward_to_gw = false);
    void sendUDTMultipartMessage(QList<QString> messages, unsigned int dstId, unsigned int srcId = 0, bool group = false, uint8_t delay = 0);
    void sendUDTDGNA(QString dgids, unsigned int dstId, bool attach = true);
    void sendUDTCallDivertInfo(unsigned int srcId, unsigned int dstId, unsigned int sap = 0);
    void pingRadio(unsigned int target_id, bool group = false);
    void pollData(unsigned int target_id, unsigned int poll_format, unsigned int srcId = 0);
    void timeoutPingResponse();
    void resetAuth();
    bool sendAuthCheck(unsigned int target_id);
    void pollStatus(unsigned int target_id);
    void announceLocalTime();
    void announceSystemFreqs();
    void announceAdjacentSites();
    void announceSystemMessage();
    void announcePrivateCalls();

signals:
    void finished();
    void writeDMRData(CDMRData& dmr_data);
    void signalDMRConfig(QVector<unsigned char> data);
    void updateLogicalChannels(QVector<LogicalChannel*>* logical_channels);
    void updateCallLog(unsigned int srcId, unsigned int dstId,
                       float rssi, float ber, float max_ber, unsigned int call_time, bool private_call);
    void updateRegisteredMSList(QList<unsigned int>* registered_ms);
    void updateTalkgroupSubscriptionList(QSet<unsigned int>* subscribed_talkgroups);
    void updateRejectedCallsList(unsigned int srcId, unsigned int dstId, bool local_call);
    void updateMessageLog(unsigned int srcId, unsigned int dstId, QString message, bool tg);
    void pingResponse(unsigned int srcId, unsigned int time);
    void positionResponse(unsigned int srcId, QString message);
    void pingTimeout();
    void startPingTimer(int msec);
    void stopPingTimer();
    void authSuccess(bool successful);
    void startAuthTimer();
    void stopAuthTimer();

private:
    LogicalChannel* findNextFreePayloadChannel(unsigned int dstId, unsigned int srcId, bool local);
    LogicalChannel* findLowerPriorityChannel(unsigned int dstId, unsigned int srcId, bool local);
    LogicalChannel* findChannelByPhysicalIdAndSlot(unsigned int physical_id, unsigned int slot);
    LogicalChannel* findCallChannel(unsigned int dstId, unsigned int srcId, bool dst_only = false);
    LogicalChannel* getControlOrAlternateChannel();
    void disableLogicalChannel(LogicalChannel*& logical_channel);
    void enableLogicalChannel(LogicalChannel*& logical_channel);
    QVector<LogicalChannel*> findActiveChannels();
    bool validateLocalSourceId(unsigned int srcId);
    void processSignalling(CDMRData& dmr_data, unsigned int udp_channel_id);
    void processNetworkCSBK(CDMRData& dmr_data, unsigned int udp_channel_id);
    void transmitCSBK(CDMRCSBK& csbk, LogicalChannel* logical_channel, unsigned int slotNo,
                      unsigned int udp_channel_id, bool channel_grant = false, bool priority_queue = false, bool announce_priority = false);
    void processVoice(CDMRData& dmr_data, unsigned int udp_channel_id, bool data_sync, bool from_gateway = false);
    void processRegistration(unsigned int srcId, unsigned int dstId, CDMRCSBK& csbk);
    bool handleRegistration(CDMRCSBK& csbk, unsigned int slotNo, unsigned int srcId,
                            unsigned int dstId, unsigned int& uab);
    void handleGroupCallRequest(CDMRCSBK& csbk, LogicalChannel*& logical_channel, unsigned int slotNo,
                                unsigned int srcId, unsigned int dstId, bool& channel_grant, bool local = false);
    void handlePrivateCallRequest(CDMRCSBK& csbk, LogicalChannel*& logical_channel, unsigned int slotNo,
                                  unsigned int srcId, unsigned int dstId, bool& channel_grant, bool local = false);
    void handlePrivatePacketDataCallRequest(CDMRCSBK& csbk, LogicalChannel*& logical_channel, unsigned int slotNo,
                                            unsigned int srcId, unsigned int dstId, bool& channel_grant, bool local = false);
    void handleGroupPacketDataCallRequest(CDMRCSBK& csbk, LogicalChannel*& logical_channel, unsigned int slotNo,
                                          unsigned int srcId, unsigned int dstId, bool& channel_grant, bool local = false);
    void contactMSForVoiceCall(CDMRCSBK& csbk, unsigned int slotNo,
                               unsigned int srcId, unsigned int dstId, bool local = false);
    void contactMSForPacketCall(CDMRCSBK& csbk, unsigned int slotNo,
                                unsigned int srcId, unsigned int dstId);
    void handleCallDisconnect(unsigned int udp_channel_id, bool group_call, unsigned int& srcId, unsigned int& dstId,
                              unsigned int slotNo, LogicalChannel*& logical_channel, CDMRCSBK& csbk);
    void handleLocalVoiceOnUnallocatedChannel(unsigned int call_type, unsigned int slotNo, unsigned int udp_channel_id);
    void processData(CDMRData& dmr_data, unsigned int udp_channel_id, bool from_gateway);
    void forwardDataToGateway(QVector<CDMRData>& dmr_data_frames);
    void processTalkgroupSubscriptionsMessage(unsigned int srcId, unsigned int slotNo, DMRMessageHandler::data_message* dmessage, unsigned int udp_channel_id);
    void processCallDivertMessage(unsigned int srcId, unsigned int slotNo, DMRMessageHandler::data_message* dmessage, unsigned int udp_channel_id);
    void processNMEAMessage(unsigned int srcId, unsigned int dstId, DMRMessageHandler::data_message* message);
    bool processTextServiceRequest(CDMRData& dmr_data, DMRMessageHandler::data_message* dmessage, unsigned int udp_channel_id);
    void processTextMessage(unsigned int dstId, unsigned int srcId, DMRMessageHandler::data_message* dmessage, bool group, bool from_gateway);
    void processDigits(unsigned int dstId, unsigned int srcId,
                       DMRMessageHandler::data_message* dmessage, bool group);
    void processDataProtocolMessage(unsigned int dstId, unsigned int srcId,
                                    DMRMessageHandler::data_message* dmessage, unsigned int udp_channel_id,
                                    unsigned int slotNo, bool from_gateway);
    void processUDPProtocolMessage(unsigned int dstId, unsigned int srcId,
                                   DMRMessageHandler::data_message* dmessage, bool from_gateway);
    void updateSubscriptions(QList<unsigned int> tg_list, unsigned int srcId);
    void resetPing();
    void buildUDTShortMessageSequence(unsigned int srcId, unsigned int dstId, QString message, bool group, bool forward_to_gw = false);
    void confirmPDPMessageReception(unsigned int srcId, unsigned int slotNo,
                                    DMRMessageHandler::data_message* dmessage, unsigned int udp_channel_id);
    void replayPacketData(unsigned int srcId, unsigned int dstId, unsigned int slotNo);
    void sendRSSIInfo(float rssi, float ber, unsigned int srcId);
    void createSubscriptionList();
    void subscribeNetworkTG(QList<unsigned int> old_tgs);
    void unsubscribeNetworkTG(QList<unsigned int> old_tgs);
    void subscribeStaticTalkgroups();
    void cleanupSubscriptions();

    LogicalChannel* m_control_channel;
    LogicalChannel* m_control_channel_alternate;
    Settings* m_settings;
    Logger* m_logger;
    DMRIdLookup* m_id_lookup;
    GatewayRouter* m_gateway_router;
    DMRRewrite* m_dmr_rewrite;
    Signalling* m_signalling_generator;
    NetworkSignalling* m_network_signalling;
    DMRMessageHandler* m_dmr_message_handler;
    QMutex m_control_mutex;
    QVector<UDPClient*> m_udp_channels;
    QMap<unsigned int, UDPClient*> m_gateway_channels;
    QVector<LogicalChannel*> m_logical_channels;
    QMap<unsigned int, unsigned int> m_private_calls;
    QMap<unsigned int, unsigned int> m_short_data_messages;
    QList<unsigned int>* m_registered_ms;
    QMap<unsigned int, QList<unsigned int>>* m_talkgroup_attachments;
    QMap<unsigned int, QList<unsigned int>>* m_talkgroup_dgna;
    AckHandler* m_ack_handler;
    QSet<unsigned int>* m_rejected_calls;
    QSet<unsigned int>* m_subscribed_talkgroups;
    QMap<unsigned int, unsigned int>* m_auth_responses;
    QMap<unsigned int, CDMRCSBK> m_auth_user;

    std::chrono::high_resolution_clock::time_point t1_ping_ms;

    bool m_stop_thread;
    bool m_startup_completed;
    bool m_late_entry_announcing;
    bool m_system_freqs_announcing;
    bool m_adjacent_sites_announcing;
    bool m_private_calls_announcing;
    unsigned int m_minute;
    unsigned int m_simi;

};

#endif // CONTROLLER_H
