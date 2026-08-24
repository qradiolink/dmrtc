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

#ifndef LOGICALCHANNEL_H
#define LOGICALCHANNEL_H

#include <chrono>
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QQueue>
#include <QMutex>
#include <QByteArray>
#include <QSysInfo>
#include <cstdint>
#include <uuid/uuid.h>
#include "src/MMDVM/DMRData.h"
#include "src/MMDVM/DMREMB.h"
#include "src/MMDVM/DMREmbeddedData.h"
#include "src/MMDVM/DMRFullLC.h"
#include "src/MMDVM/DMRLC.h"
#include "src/MMDVM/Sync.h"
#include "src/MMDVM/Utils.h"
#include "src/logger.h"
#include "src/settings.h"
#include "src/trunking_utils.h"
#include "src/dmrrewrite.h"


namespace CallType
{
    enum CallType {
        CALL_TYPE_MS = 0,
        CALL_TYPE_GROUP = 1,
        CALL_TYPE_INDIV_PACKET = 2,
        CALL_TYPE_GROUP_PACKET = 3,
    };
}

namespace CallState
{
    enum CallState {
        CALL_STATE_LC,
        CALL_STATE_SYNC,
        CALL_STATE_NONE
    };
}

class LogicalChannel : public QObject
{
    Q_OBJECT
public:
    LogicalChannel(const Settings* settings, Logger* logger, unsigned int id,
                   unsigned int physical_channel, unsigned int slot, bool control_channel = false, bool gui_enabled = false, QObject* parent = 0);
    ~LogicalChannel();

    void allocateChannel(unsigned int srcId, unsigned int dstId, unsigned int call_type = CallType::CALL_TYPE_GROUP, bool local = false);
    void deallocateChannel();
    void updateChannel(unsigned int srcId, unsigned int dstId, unsigned int call_type = CallType::CALL_TYPE_GROUP);
    void startTimeoutTimer();
    void stopTimeoutTimer();
    void updateStats(CDMRData& dmr_data, bool end_call = false);
    void setBusy(bool busy);
    void setDisabled(bool disabled);
    void setDestination(unsigned int destination);
    void setSource(unsigned int source);
    void setCallType(unsigned int call_type);
    void setLocalCall(bool local);
    bool getLocalCall();
    bool isControlChannel();
    unsigned int getPhysicalChannel();
    unsigned int getLogicalChannel();
    unsigned int getSlot();
    unsigned int getDestination();
    unsigned int getSource();
    unsigned int getCallType();
    float getBER();
    float getMaxBER();
    float getRSSI();
    bool getBusy();
    bool getTimeout();
    unsigned int getCallTime();
    bool getDisabled();
    QString getText();
    void setText(QString txt, bool control_channel = true);
    void setGPSInfo(float longitude, float latitude, std::string error);
    QString getGPSInfo();
    void putRFQueue(CDMRData& dmr_data, bool first = false);
    bool getRFQueue(CDMRData& dmr_data);
    void putRFQueueMultiItem(QVector<CDMRData>& dmr_data_items, bool first = false);
    void putNetQueue(CDMRData& dmr_data);
    bool getNetQueue(CDMRData& dmr_data);
    void clearRFQueue();
    void clearNetQueue();
    bool getChannelParams(uint64_t& params, uint8_t& colour_code);
    void startLastFrameTimer();
    void stopLastFrameTimer();
    bool getChannelLock(unsigned int srcId, unsigned int dataType);
    bool removeLastRFQueue();


public slots:
    void setChannelIdle();
    void notifyLastFrame();

signals:
    void channelDeallocated(unsigned int channel_id);
    void internalStartTimer();
    void internalStopTimer();
    void internalStartLastFrameTimer();
    void internalStopLastFrameTimer();
    void update();
    void setCallStats(unsigned int srcId, unsigned int dstId,
                      float rssi, float ber, float max_ber, unsigned int call_time, bool private_call);
    void updateCallStats(unsigned int srcId, unsigned int dstId, float rssi, float ber, float max_ber, bool private_call);

private:
    void rewriteEmbeddedData(CDMRData& dmr_data, bool send_embedded_data);
    void processTalkerAlias();
    void lockChannel(CDMRData& dmr_data);


    void setUUID(CDMRData& dmr_data);

    const Settings* m_settings;
    Logger* m_logger;
    DMRRewrite* m_dmr_rewrite;
    bool m_gui_enabled;
    unsigned int m_id;
    unsigned int m_physical_channel;
    unsigned int m_slot;
    bool m_control_channel;
    bool m_busy;
    bool m_frame_timeout;
    bool m_call_in_progress;
    bool m_disabled;
    bool m_local_call;
    unsigned int m_src_lock;
    unsigned int m_call_type;
    unsigned int m_source_address;
    unsigned int m_destination_address;
    unsigned int m_stats_dst_id;
    unsigned int m_stats_src_id;
    QTimer m_timeout_timer;
    QTimer m_last_frame_timer;
    QElapsedTimer m_call_timer;
    QMutex m_rf_queue_mutex;
    QMutex m_net_queue_mutex;
    QMutex m_data_mutex;
    QVector<CDMRData> m_rf_queue;
    QVector<CDMRData> m_net_queue;
    std::chrono::high_resolution_clock::time_point t1_rf;
    std::chrono::high_resolution_clock::time_point t1_net;
    CDMREmbeddedData m_embedded_data[2];
    CDMREmbeddedData m_default_embedded_data;
    unsigned int m_emb_read;
    unsigned int m_emb_write;
    QString m_text;
    QString m_gps_info;
    bool m_talker_alias_received;
    unsigned int m_ta_df;
    unsigned int m_ta_dl;
    QByteArray m_ta_data;
    CDMRLC m_lc;
    unsigned int m_state;
    uint64_t m_rx_freq;
    uint64_t m_tx_freq;
    uint64_t m_colour_code;
    uint64_t m_lcn;
    unsigned int m_stream_id;
    unsigned int m_data_frames;
    float m_rssi_accumulator;
    float m_ber_accumulator;
    float m_ber;
    float m_max_ber;
    float m_rssi;
    unsigned char* m_call_uuid;

};

#endif // LOGICALCHANNEL_H
