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

#ifndef LOGICALCHANNEL_H
#define LOGICALCHANNEL_H

#include <chrono>
#include <QObject>
#include <QTimer>
#include <QQueue>
#include <QMutex>
#include <QByteArray>
#include <QSysInfo>
#include "src/logger.h"
#include "src/settings.h"
#include "src/utils.h"
#include "MMDVM/DMRData.h"
#include "MMDVM/DMREMB.h"
#include "MMDVM/DMREmbeddedData.h"
#include "MMDVM/DMRFullLC.h"
#include "MMDVM/DMRLC.h"
#include "MMDVM/Sync.h"
#include "MMDVM/Utils.h"

enum CallType
{
    CALL_TYPE_MS=0,
    CALL_TYPE_GROUP=1,
    CALL_TYPE_INDIV_PACKET=2,
    CALL_TYPE_GROUP_PACKET=3,
};

enum CallState
{
    CALL_STATE_LC,
    CALL_STATE_SYNC,
    CALL_STATE_NONE
};

class LogicalChannel : public QObject
{
    Q_OBJECT
public:
    LogicalChannel(Settings *settings, Logger *logger, unsigned int id,
                   unsigned int physical_channel, unsigned int slot, bool control_channel=false, QObject *parent=0);

    void allocateChannel(unsigned int srcId, unsigned int dstId, unsigned int call_type=CALL_TYPE_GROUP, bool local=false);
    void deallocateChannel();
    void updateChannel(unsigned int srcId, unsigned int dstId, unsigned int call_type=CALL_TYPE_GROUP);
    void startTimeoutTimer();
    void stopTimeoutTimer();
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
    float getRSSI();
    bool getBusy();
    bool getDisabled();
    QString getText();
    void setText(QString txt);
    void setGPSInfo(float longitude, float latitude, std::string error);
    QString getGPSInfo();
    void putRFQueue(CDMRData &dmr_data, bool first=false);
    bool getRFQueue(CDMRData &dmr_data);
    void putNetQueue(CDMRData &dmr_data);
    bool getNetQueue(CDMRData &dmr_data);
    void clearRFQueue();
    void clearNetQueue();
    bool getChannelParams(uint64_t &params, uint8_t &colour_code);

public slots:
    void setChannelIdle();

signals:
    void channelDeallocated(unsigned int channel_id);
    void internalStartTimer();
    void internalStopTimer();
    void update();
    void setCallStats(unsigned int srcId, unsigned int dstId, float ber, float rssi, bool private_call);
    void updateCallStats(unsigned int srcId, unsigned int dstId, float rssi, float ber, bool private_call);

private:
    void rewriteEmbeddedData(CDMRData &dmr_data);
    void updateStats(CDMRData &dmr_data, bool end_call=false);

    Settings *_settings;
    Logger *_logger;
    unsigned int _id;
    unsigned int _physical_channel;
    unsigned int _slot;
    bool _control_channel;
    bool _busy;
    bool _call_in_progress;
    bool _disabled;
    bool _local_call;
    unsigned int _call_type;
    unsigned int _source_address;
    unsigned int _destination_address;
    unsigned int _stats_dst_id;
    unsigned int _stats_src_id;
    QTimer _timeout_timer;
    QMutex _rf_queue_mutex;
    QMutex _net_queue_mutex;
    QMutex _data_mutex;
    QVector<CDMRData> _rf_queue;
    QVector<CDMRData> _net_queue;
    std::chrono::high_resolution_clock::time_point t1_rf;
    std::chrono::high_resolution_clock::time_point t1_net;
    CDMREmbeddedData _embedded_data[2];
    CDMREmbeddedData _default_embedded_data;
    unsigned int _emb_read;
    unsigned int _emb_write;
    QString _text;
    QString _gps_info;
    bool _talker_alias_received;
    unsigned int _ta_df;
    unsigned int _ta_dl;
    QByteArray _ta_data;
    CDMRLC _lc;
    unsigned int _state;
    uint64_t _rx_freq;
    uint64_t _tx_freq;
    uint64_t _colour_code;
    uint64_t _lcn;
    unsigned int _stream_id;
    unsigned int _data_frames;
    float _rssi_accumulator;
    float _ber_accumulator;
    float _ber;
    float _rssi;

    void processTalkerAlias();
};

#endif // LOGICALCHANNEL_H
