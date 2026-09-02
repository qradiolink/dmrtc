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

#include "logicalchannel.h"

const long long TX_TIME = 58000000L;

LogicalChannel::LogicalChannel(const Settings* settings, Logger* logger, unsigned int id,
                               unsigned int physical_channel, unsigned int slot, bool control_channel, bool gui_enabled, QObject* parent) : QObject(parent)
{
    m_id = id;
    m_settings = settings;
    m_logger = logger;
    const QList<unsigned int>* dummy_ms_list = nullptr;
    m_dmr_rewrite = new DMRRewrite(settings, dummy_ms_list);
    m_gui_enabled = gui_enabled;
    m_physical_channel = physical_channel;
    m_slot = slot;
    m_control_channel = control_channel;
    m_busy = false;
    m_frame_timeout = false;
    m_call_timer.start();
    m_call_in_progress = false;
    m_disabled = false;
    m_local_call = false;
    m_src_lock = 0;
    m_state = CallState::CALL_STATE_NONE;
    m_source_address = 0;
    m_destination_address = 0;
    m_emb_read = 1;
    m_emb_write = 0;
    m_text = "";
    m_gps_info = "";
    m_talker_alias_received = false;
    m_ta_df = 0;
    m_ta_dl = 0;
    m_rx_freq = 0;
    m_tx_freq = 0;
    m_colour_code = 1;
    m_lcn = m_physical_channel + 1;
    m_stream_id = 0;
    m_stats_dst_id = 0;
    m_stats_src_id = 0;
    m_call_uuid = new unsigned char[16U];
    memset(m_call_uuid, 0, 16U);
    m_data_frames = 0;
    m_rssi_accumulator = 0.0f;
    m_ber_accumulator = 0.0f;
    m_ber = 0.0f;
    m_max_ber = 0.0f;
    m_rssi = 0.0f;
    t1_rf = std::chrono::high_resolution_clock::now();
    t1_net = std::chrono::high_resolution_clock::now();
    m_timeout_timer.setInterval(m_settings->payload_channel_idle_timeout * 1000);
    m_timeout_timer.setSingleShot(true);
    QObject::connect(&m_timeout_timer, SIGNAL(timeout()), this, SLOT(setChannelIdle()), Qt::DirectConnection);
    QObject::connect(this, SIGNAL(internalStartTimer()), &m_timeout_timer, SLOT(start()));
    QObject::connect(this, SIGNAL(internalStopTimer()), &m_timeout_timer, SLOT(stop()));
    m_last_frame_timer.setInterval(4 * TX_TIME / 1000000);
    m_last_frame_timer.setSingleShot(true);
    QObject::connect(&m_last_frame_timer, SIGNAL(timeout()), this, SLOT(notifyLastFrame()), Qt::DirectConnection);
    QObject::connect(this, SIGNAL(internalStartLastFrameTimer()), &m_last_frame_timer, SLOT(start()));
    QObject::connect(this, SIGNAL(internalStopLastFrameTimer()), &m_last_frame_timer, SLOT(stop()));
    QMap<QString, uint64_t> channel;

    for (int i = 0; i < m_settings->logical_physical_channels.size(); i++) {
        if (m_settings->logical_physical_channels[i].value("channel_id") == (m_physical_channel + 1)) {
            channel = m_settings->logical_physical_channels[i];
            break;
        }
    }

    if (channel.size() >= 5) {
        m_rx_freq = channel.value("rx_freq");
        m_tx_freq = channel.value("tx_freq");
        m_colour_code = channel.value("colour_code");

        if (!m_settings->use_fixed_channel_plan) {
            m_lcn = channel.value("logical_channel");
        } else {
            m_lcn = (channel.value("tx_freq") - m_settings->freq_base) / m_settings->freq_separation + 1;
        }
    } else {
        m_logger->log(Logger::LogLevelWarning, QString("Could not find settings for logical channel %1 in"
                                                       " the config file (section logical_physical_channels)").arg(m_lcn));
    }

}

LogicalChannel::~LogicalChannel()
{
    delete m_dmr_rewrite;
    delete[] m_call_uuid;
}

bool LogicalChannel::getChannelParams(uint64_t& params, uint8_t& colour_code)
{
    uint64_t lcn = m_lcn;
    uint64_t tx_value_khz = m_tx_freq % 1000000 / 125;
    uint64_t rx_value_khz = m_rx_freq % 1000000 / 125;
    uint64_t tx_value_Mhz = m_tx_freq / 1000000;
    uint64_t rx_value_Mhz = m_rx_freq / 1000000;
    params = 0;
    params |= rx_value_khz;
    params |= rx_value_Mhz << 13;
    params |= tx_value_khz << 23;
    params |= tx_value_Mhz << 36;
    params |= lcn << 46;
    colour_code = (uint8_t) m_colour_code;
    return true;
}

void LogicalChannel::allocateChannel(unsigned int srcId, unsigned int dstId, unsigned int call_type, bool local)
{
    m_data_mutex.lock();
    m_source_address = srcId;
    m_destination_address = dstId;
    m_call_type = call_type;
    m_text = "";
    m_gps_info = "";
    m_talker_alias_received = false;
    m_ta_df = 0;
    m_ta_dl = 0;
    m_ta_data.clear();
    m_busy = true;
    m_src_lock = 0;
    m_frame_timeout = false;
    m_call_in_progress = false;
    m_call_timer.start();
    m_local_call = local;
    m_data_frames = 0;
    m_embedded_data[0].reset();
    m_embedded_data[1].reset();
    m_data_mutex.unlock();
    emit internalStartTimer();

    if (m_gui_enabled)
        emit internalStartLastFrameTimer();

    m_logger->log(Logger::LogLevelDebug, QString("Allocated physical channel %1, logical channel %2, slot %3 to destination %4 and source %5")
                  .arg(m_physical_channel).arg(m_lcn).arg(m_slot).arg(dstId).arg(srcId));
}

void LogicalChannel::deallocateChannel()
{
    m_data_mutex.lock();
    m_busy = false;
    m_frame_timeout = false;
    m_call_in_progress = false;
    m_local_call = false;
    m_src_lock = 0;
    m_state = CallState::CALL_STATE_NONE;
    m_embedded_data[0].reset();
    m_embedded_data[1].reset();
    m_text = "";
    m_gps_info = "";
    m_talker_alias_received = false;
    m_ta_df = 0;
    m_ta_dl = 0;
    m_ta_data.clear();
    m_lc = CDMRLC(FLCO::FLCO_USER_USER, 0, 0);
    m_data_mutex.unlock();

    CDMRData dummy_data;
    emit internalStopTimer();
    emit internalStopLastFrameTimer();
    updateStats(dummy_data, true);
    m_logger->log(Logger::LogLevelDebug, QString("Deallocated physical channel %1, slot %2, source %3, destination %4")
                  .arg(m_physical_channel).arg(m_slot).arg(m_source_address).arg(m_destination_address));
}

void LogicalChannel::updateChannel(unsigned int srcId, unsigned int dstId, unsigned int call_type)
{
    m_data_mutex.lock();
    m_source_address = srcId;
    m_call_in_progress = true;
    m_src_lock = srcId;
    m_destination_address = dstId;
    m_call_type = call_type;
    m_data_mutex.unlock();
    startTimeoutTimer();
    m_logger->log(Logger::LogLevelDebug, QString("Updated physical channel %1, slot %2 to destination %3 and source %4")
                  .arg(m_physical_channel).arg(m_slot).arg(dstId).arg(srcId));
}

void LogicalChannel::updateStats(CDMRData& dmr_data, bool end_call)
{
    unsigned char dataType = dmr_data.getDataType();

    if ((dataType == DT_CSBK)
        || (dataType == DT_MBC_HEADER)
        || (dataType == DT_MBC_CONTINUATION)
        || ((dataType == DT_IDLE) && !end_call))
        return;

    m_data_mutex.lock();

    if (end_call) {
        if (m_data_frames > 0) {
            m_rssi = m_rssi_accumulator / float(m_data_frames);
            m_ber = m_ber_accumulator / float(m_data_frames);
            unsigned int call_time = (unsigned int)(m_call_timer.elapsed() / 1000);

            if (call_time >= (unsigned int)m_settings->payload_channel_idle_timeout)
                call_time -= (unsigned int)m_settings->payload_channel_idle_timeout;

            emit setCallStats(m_stats_src_id, m_stats_dst_id, m_rssi, m_ber, m_max_ber, call_time, (m_call_type == CallType::CALL_TYPE_MS));
        }

        m_stream_id = 0;
        m_stats_dst_id = 0;
        m_stats_src_id = 0;
        m_data_frames = 0;
        m_rssi_accumulator = 0.0f;
        m_ber_accumulator = 0.0f;
        m_max_ber = 0.0f;
        m_data_mutex.unlock();
        return;
    }

    unsigned int new_stream_id = dmr_data.getStreamId();
    unsigned int old_stream_id = m_stream_id;

    if (new_stream_id != old_stream_id) {
        m_stream_id = new_stream_id;

        if ((old_stream_id != 0) && (m_data_frames > 0) && (new_stream_id != 0) && (m_stats_src_id != 0)) {
            m_rssi = m_rssi_accumulator / float(m_data_frames);
            m_ber = m_ber_accumulator / float(m_data_frames);
            unsigned int call_time = (unsigned int)(m_call_timer.elapsed() / 1000);
            emit setCallStats(m_stats_src_id, m_stats_dst_id, m_rssi, m_ber, m_max_ber, call_time, (m_call_type == CallType::CALL_TYPE_MS));
        }

        float frame_ber = float(dmr_data.getBER()) / 1.41f;
        m_rssi_accumulator = float(dmr_data.getRSSI()) * -1.0f;
        m_ber_accumulator = frame_ber;
        m_max_ber = frame_ber;
        m_data_frames = 1;
        m_call_timer.start();
        m_stats_dst_id = dmr_data.getDstId();
        m_stats_src_id = dmr_data.getSrcId();
        emit update();
    } else {
        float frame_ber = float(dmr_data.getBER()) / 1.41f;
        m_rssi_accumulator += float(dmr_data.getRSSI()) * -1.0f;
        m_ber_accumulator += frame_ber;
        m_data_frames++;
        m_rssi = m_rssi_accumulator / float(m_data_frames);
        m_ber = m_ber_accumulator / float(m_data_frames);
        m_max_ber = (frame_ber > m_max_ber) ? frame_ber : m_max_ber;

        if (((m_data_frames % 10) == 0) && (m_data_frames > 0))
            emit update();
    }

    m_data_mutex.unlock();
}

void LogicalChannel::putRFQueue(CDMRData& dmr_data, bool first)
{
    if (getChannelLock(dmr_data.getSrcId(), dmr_data.getDataType()))
        return;

    lockChannel(dmr_data);
    startLastFrameTimer();
    rewriteEmbeddedData(dmr_data, true);
    m_rf_queue_mutex.lock();

    if (first)
        m_rf_queue.prepend(dmr_data);
    else
        m_rf_queue.append(dmr_data);

    m_rf_queue_mutex.unlock();
    m_data_mutex.lock();
    m_call_in_progress = true;
    m_data_mutex.unlock();

    if (dmr_data.getFLCO() != FLCO_USER_USER) {
        dmr_data.setDstId(TrunkingUtils::convertBase11GroupNumberToBase10(dmr_data.getDstId()));
    }

    updateStats(dmr_data);
}

void LogicalChannel::putRFQueueMultiItem(QVector<CDMRData>& dmr_data_items, bool first)
{
    startLastFrameTimer();

    for (int i = 0; i < dmr_data_items.size(); i++) {
        CDMRData dmr_data = dmr_data_items[i];
        rewriteEmbeddedData(dmr_data, true);
    }

    m_rf_queue_mutex.lock();

    if (first) {
        for (int i = dmr_data_items.size() - 1; i >= 0; i--) {
            CDMRData dmr_data = dmr_data_items[i];
            m_rf_queue.prepend(dmr_data);
        }
    } else {
        for (int i = 0; i < dmr_data_items.size(); i++) {
            CDMRData dmr_data = dmr_data_items[i];
            m_rf_queue.append(dmr_data);
        }
    }

    m_rf_queue_mutex.unlock();
    m_data_mutex.lock();
    m_call_in_progress = true;
    m_data_mutex.unlock();

    for (int i = 0; i < dmr_data_items.size(); i++) {
        CDMRData dmr_data = dmr_data_items[i];

        if (dmr_data.getFLCO() != FLCO_USER_USER) {
            dmr_data.setDstId(TrunkingUtils::convertBase11GroupNumberToBase10(dmr_data.getDstId()));
        }

        updateStats(dmr_data);
    }
}

bool LogicalChannel::getRFQueue(CDMRData& dmr_data)
{
    m_rf_queue_mutex.lock();

    if (m_rf_queue.size() < 1) {
        m_rf_queue_mutex.unlock();
        return false;
    }

    CDMRData dt_first = m_rf_queue.constFirst();
    m_rf_queue_mutex.unlock();
    std::chrono::high_resolution_clock::time_point t2_rf = std::chrono::high_resolution_clock::now();

    if (m_settings->prevent_mmdvm_overflows || (dt_first.getDataType() == DT_CSBK)
        || (dt_first.getDataType() == DT_VOICE_LC_HEADER)
        || (dt_first.getDataType() == DT_RATE_12_DATA)
        || (dt_first.getDataType() == DT_RATE_1_DATA)
        || (dt_first.getDataType() == DT_RATE_34_DATA)) {
        if (dt_first.getControl()) {
            m_rf_queue_mutex.lock();
            dmr_data = m_rf_queue.takeFirst();
            m_rf_queue_mutex.unlock();
            // for control frames, do not update the time as they are not transmitted over RF
            return true;
        }

        if (std::chrono::duration_cast<std::chrono::nanoseconds>(t2_rf - t1_rf).count() < TX_TIME) {
            return false;
        }
    }

    m_rf_queue_mutex.lock();
    dmr_data = m_rf_queue.takeFirst();
    m_rf_queue_mutex.unlock();

    if (dmr_data.getControl())
        return true;

    t1_rf = std::chrono::high_resolution_clock::now();

    if (dmr_data.getDummy())
        return false;

    return true;
}

bool LogicalChannel::removeLastRFQueue()
{
    m_rf_queue_mutex.lock();

    if (m_rf_queue.size() < 1) {
        m_rf_queue_mutex.unlock();
        return false;
    }

    CDMRData dt_last = m_rf_queue.constLast();
    unsigned int dataType = dt_last.getDataType();

    if (((dataType != DT_VOICE) && (dataType != DT_VOICE_SYNC)) || dt_last.getControl() || dt_last.getDummy()) {
        m_rf_queue_mutex.unlock();
        return false;
    }

    m_rf_queue.removeLast();
    m_rf_queue_mutex.unlock();
    return true;
}

void LogicalChannel::putNetQueue(CDMRData& dmr_data)
{
    startLastFrameTimer();
    setUUID(dmr_data);
    m_data_mutex.lock();
    m_call_in_progress = true;
    m_data_mutex.unlock();
    rewriteEmbeddedData(dmr_data, true);
    m_net_queue_mutex.lock();
    m_net_queue.append(dmr_data);
    m_net_queue_mutex.unlock();
    updateStats(dmr_data);
}

bool LogicalChannel::getNetQueue(CDMRData& dmr_data)
{
    m_net_queue_mutex.lock();

    if (m_net_queue.size() < 1) {
        m_net_queue_mutex.unlock();
        return false;
    }

    m_net_queue_mutex.unlock();
    std::chrono::high_resolution_clock::time_point t2_net = std::chrono::high_resolution_clock::now();

    if (std::chrono::duration_cast<std::chrono::nanoseconds>(t2_net - t1_net).count() < (long long)TX_TIME) {
        return false;
    }

    m_net_queue_mutex.lock();
    dmr_data = m_net_queue.takeFirst();
    m_net_queue_mutex.unlock();
    t1_net = std::chrono::high_resolution_clock::now();

    if (dmr_data.getDummy())
        return false;

    return true;
}

void LogicalChannel::clearNetQueue()
{
    m_net_queue_mutex.lock();
    m_net_queue.clear();
    m_net_queue_mutex.unlock();
}

void LogicalChannel::clearRFQueue()
{
    m_rf_queue_mutex.lock();
    m_rf_queue.clear();
    m_rf_queue_mutex.unlock();
}

void LogicalChannel::lockChannel(CDMRData& dmr_data)
{
    unsigned int dataType = dmr_data.getDataType();
    unsigned int srcId = dmr_data.getSrcId();

    if (!getChannelLock(srcId, dataType) && srcId > 0 && srcId != m_src_lock
        && (dataType != DT_CSBK)
        && (dataType != DT_MBC_HEADER)
        && (dataType != DT_MBC_CONTINUATION)
        && (dataType != DT_TERMINATOR_WITH_LC)
        && (dataType != DT_IDLE)) {
        m_data_mutex.lock();
        m_src_lock = srcId;
        m_data_mutex.unlock();
    } else if ((dataType == DT_TERMINATOR_WITH_LC) && (srcId == m_src_lock)) {
        m_data_mutex.lock();
        m_src_lock = 0;
        m_data_mutex.unlock();
    }
}

bool LogicalChannel::getChannelLock(unsigned int srcId, unsigned int dataType)
{
    if (m_control_channel)
        return false;

    m_data_mutex.lock();
    bool locked = (m_src_lock != 0) && (m_src_lock != srcId)
                  && (dataType != DT_CSBK)
                  && (dataType != DT_MBC_HEADER)
                  && (dataType != DT_MBC_CONTINUATION)
                  && (dataType != DT_IDLE);
    m_data_mutex.unlock();
    return locked;
}

void LogicalChannel::startTimeoutTimer()
{
    emit internalStartTimer();
}

void LogicalChannel::stopTimeoutTimer()
{
    emit internalStopTimer();
}

void LogicalChannel::startLastFrameTimer()
{
    m_data_mutex.lock();
    m_frame_timeout = false;
    m_data_mutex.unlock();

    if (m_gui_enabled)
        emit internalStartLastFrameTimer();
}

void LogicalChannel::stopLastFrameTimer()
{
    emit internalStopLastFrameTimer();
}

void LogicalChannel::setChannelIdle()
{
    m_data_mutex.lock();
    m_busy = false;
    m_src_lock = 0;
    m_frame_timeout = false;
    m_data_mutex.unlock();
    CDMRData dummy_data;
    updateStats(dummy_data, true);
    emit channelDeallocated(m_id);
    m_logger->log(Logger::LogLevelDebug, QString("Physical channel %1, slot %2 to destination %3 and source %4 is marked as idle and deallocated")
                  .arg(m_physical_channel).arg(m_slot).arg(m_destination_address).arg(m_source_address));
}

void LogicalChannel::notifyLastFrame()
{
    m_data_mutex.lock();
    m_frame_timeout = true;
    m_src_lock = 0;
    m_data_mutex.unlock();
    emit update();
}

bool LogicalChannel::isControlChannel()
{
    m_data_mutex.lock();
    bool control = m_control_channel;
    m_data_mutex.unlock();
    return control;
}

unsigned int LogicalChannel::getPhysicalChannel()
{
    m_data_mutex.lock();
    unsigned int physical_channel = m_physical_channel;
    m_data_mutex.unlock();
    return physical_channel;
}

unsigned int LogicalChannel::getLogicalChannel()
{
    m_data_mutex.lock();
    unsigned int logical_channel = m_lcn;
    m_data_mutex.unlock();
    return logical_channel;
}

unsigned int LogicalChannel::getSlot()
{
    m_data_mutex.lock();
    unsigned int slot = m_slot;
    m_data_mutex.unlock();
    return slot;
}

bool LogicalChannel::getBusy()
{
    m_data_mutex.lock();
    bool busy = m_busy;
    m_data_mutex.unlock();
    return busy;
}

bool LogicalChannel::getTimeout()
{
    m_data_mutex.lock();
    bool timeout = m_frame_timeout;
    m_data_mutex.unlock();
    return timeout;
}

unsigned int LogicalChannel::getCallTime()
{
    m_data_mutex.lock();
    unsigned int call_time = 0;

    if (m_busy)
        call_time = (unsigned int)(m_call_timer.elapsed() / 1000);

    m_data_mutex.unlock();
    return call_time;
}

bool LogicalChannel::getDisabled()
{
    m_data_mutex.lock();
    bool disabled = m_disabled;
    m_data_mutex.unlock();
    return disabled;
}


void LogicalChannel::setDisabled(bool disabled)
{
    m_data_mutex.lock();
    m_disabled = disabled;
    m_frame_timeout = false;
    m_src_lock = 0;
    m_busy = false;
    m_data_mutex.unlock();
    m_logger->log(Logger::LogLevelInfo, QString("State of channel %1, slot %2 changed to %3")
                  .arg(m_physical_channel)
                  .arg(m_slot)
                  .arg(disabled ? "disabled" : "enabled"));
}

unsigned int LogicalChannel::getDestination()
{
    m_data_mutex.lock();
    unsigned int destination = m_destination_address;
    m_data_mutex.unlock();
    return destination;
}

unsigned int LogicalChannel::getSource()
{
    m_data_mutex.lock();
    unsigned int source = m_source_address;
    m_data_mutex.unlock();
    return source;
}

void LogicalChannel::setBusy(bool busy)
{
    m_data_mutex.lock();
    m_busy = busy;
    m_data_mutex.unlock();
}

void LogicalChannel::setLocalCall(bool local)
{
    m_data_mutex.lock();
    m_local_call = local;
    m_data_mutex.unlock();
}

bool LogicalChannel::getLocalCall()
{
    m_data_mutex.lock();
    bool local = m_local_call;
    m_data_mutex.unlock();
    return local;
}

float LogicalChannel::getBER()
{
    m_data_mutex.lock();
    float ber = m_ber;
    m_data_mutex.unlock();
    return ber;
}

float LogicalChannel::getMaxBER()
{
    m_data_mutex.lock();
    float max_ber = m_max_ber;
    m_data_mutex.unlock();
    return max_ber;
}

float LogicalChannel::getRSSI()
{
    m_data_mutex.lock();
    float rssi = m_rssi;
    m_data_mutex.unlock();
    return rssi;
}

void LogicalChannel::setDestination(unsigned int destination)
{
    m_data_mutex.lock();
    m_destination_address = destination;
    m_data_mutex.unlock();
}

void LogicalChannel::setSource(unsigned int source)
{
    m_data_mutex.lock();
    m_source_address = source;
    m_data_mutex.unlock();
}

void LogicalChannel::setCallType(unsigned int call_type)
{
    m_data_mutex.lock();
    m_call_type = call_type;
    m_data_mutex.unlock();
}

unsigned int LogicalChannel::getCallType()
{
    m_data_mutex.lock();
    unsigned int call_type = m_call_type;
    m_data_mutex.unlock();
    return call_type;
}

QString LogicalChannel::getText()
{
    m_data_mutex.lock();
    QString text = m_text;
    m_data_mutex.unlock();
    return text;
}

void LogicalChannel::setText(QString txt, bool control_channel)
{
    if (txt.size() < 1)
        return;

    m_data_mutex.lock();

    if (control_channel)
        m_text = txt;
    else
        m_text = QString("%1 %2").arg(txt).arg((m_ta_df == 3) ? "(UTF-16)" : ((m_ta_df == 0) ? ("(ISO 7)") : "(ISO 8)"));

    m_data_mutex.unlock();
    emit update();
}

void LogicalChannel::setGPSInfo(float longitude, float latitude, std::string error)
{
    m_data_mutex.lock();
    m_gps_info = QString("Longitude: %1, Latitude: %2, Error: %3")
                 .arg(longitude)
                 .arg(latitude)
                 .arg(QString::fromStdString(error));
    m_data_mutex.unlock();
    emit update();
    m_logger->log(Logger::LogLevelDebug, QString("GPS Info received from %1 to %2: %3")
                  .arg(QString::number(getSource())).arg(QString::number(getDestination())).arg(m_gps_info));
}

QString LogicalChannel::getGPSInfo()
{
    QString gps_info;
    m_data_mutex.lock();
    gps_info = m_gps_info;
    m_data_mutex.unlock();
    return gps_info;
}

void LogicalChannel::processTalkerAlias()
{
    unsigned int size = m_ta_data.size();

    if ((size < 1) || (size > 28))
        return;

    unsigned int bit7_size = 8 * size / 7;
    QString txt;

    if (((m_ta_df == 1 || m_ta_df == 2) && (size >= m_ta_dl)) ||
        ((m_ta_df == 3) && (size >= m_ta_dl * 2)) ||
        ((m_ta_df == 0) && (bit7_size >= m_ta_dl))) {

        if (m_ta_df == 1 || m_ta_df == 2) {
            txt = QString::fromUtf8(m_ta_data);
        } else if (m_ta_df == 0) {
            unsigned char converted[32U];
            ::memset(converted, 0U, 32U);
            TrunkingUtils::parseISO7bitToISO8bit((unsigned char*)m_ta_data.constData(), converted, bit7_size, size);
            txt = QString::fromUtf8((const char*)converted + 1, bit7_size - 1).trimmed();
        } else if (m_ta_df == 3) {
            if (QSysInfo::ByteOrder == QSysInfo::BigEndian) {
                txt = QString::fromUtf16((char16_t*)m_ta_data.constData(), size / 2);
            } else {
                TrunkingUtils::parseUTF16(txt, size, (unsigned char*)m_ta_data.data());
            }
        }

        setText(txt, false);
        m_talker_alias_received = true;
        m_ta_data.clear();
        m_ta_dl = 0;
        m_ta_df = 0;
    }
}

void LogicalChannel::rewriteEmbeddedData(CDMRData& dmr_data, bool send_embedded_data)
{
    m_lc = CDMRLC(dmr_data.getFLCO(), dmr_data.getSrcId(), dmr_data.getDstId());
    unsigned int N = dmr_data.getN();
    m_default_embedded_data.setLC(m_lc);
    unsigned char data[DMR_FRAME_LENGTH_BYTES];
    dmr_data.getData(data);
    unsigned int dataType = dmr_data.getDataType();

    if (dataType == DT_VOICE_LC_HEADER || dataType == DT_TERMINATOR_WITH_LC) {
        // match LC with rewritten src and destination
        CDMRFullLC fullLC;
        CDMRLC lc(dmr_data.getFLCO(), dmr_data.getSrcId(), dmr_data.getDstId());
        fullLC.encode(lc, data, dataType);
        CDMRSlotType slotType;
        slotType.setColorCode(1);
        slotType.setDataType(dataType);
        slotType.getData(data);
        CSync::addDMRDataSync(data, true);
        m_data_mutex.lock();
        m_gps_info = "";
        m_talker_alias_received = false;
        m_ta_df = 0;
        m_ta_dl = 0;
        m_ta_data.clear();
        m_embedded_data[0].reset();
        m_embedded_data[1].reset();
        m_default_embedded_data.reset();
        m_data_mutex.unlock();
    }

    if (dataType == DT_TERMINATOR_WITH_LC) {
        m_lc = CDMRLC(FLCO::FLCO_USER_USER, 0, 0);
    } else if (dataType == DT_VOICE_LC_HEADER) {
        m_default_embedded_data.setLC(m_lc);
        m_data_mutex.lock();
        m_text = "";
        m_data_mutex.unlock();
    } else if (dataType == DT_VOICE_SYNC) {
        m_emb_read = (m_emb_read + 1) % 2;
        m_emb_write = (m_emb_write + 1) % 2;
        m_embedded_data[m_emb_write].reset();
    } else if (dataType == DT_VOICE) {
        CDMREMB emb;
        emb.putData(data);
        unsigned char lcss = emb.getLCSS();

        bool ret = m_embedded_data[m_emb_write].addData(data, lcss);

        if (ret) {
            FLCO flco = m_embedded_data[m_emb_write].getFLCO();
            unsigned char raw_data[9U];
            m_embedded_data[m_emb_write].getRawData(raw_data);

            switch (flco) {
                case FLCO_GROUP:
                case FLCO_USER_USER: {
                    if (m_embedded_data[m_emb_write].isValid()) {
                        m_embedded_data[m_emb_write].setLC(m_lc);
                    }
                }
                break;

                case FLCO_GPS_INFO: {
                    float longitude, latitude = 0.0f;
                    std::string error;
                    CUtils::extractGPSPosition(raw_data, error, longitude, latitude);
                    setGPSInfo(longitude, latitude, error);
                }
                break;

                case FLCO_TALKER_ALIAS_HEADER: {
                    m_talker_alias_received = false;
                    m_ta_df = (raw_data[2] >> 6) & 0x03;
                    m_ta_dl = (raw_data[2] >> 1) & 0x1F;
                    m_ta_data.clear();

                    if (m_ta_df == 0) {
                        // for 7 bit TA the MSB is last bit of byte 3
                        m_ta_data.append(raw_data[2] & 0x01);
                    }

                    for (int i = 3; i < 9; i++) {
                        m_ta_data.append(raw_data[i]);
                    }

                    processTalkerAlias();
                }
                break;

                case FLCO_TALKER_ALIAS_BLOCK1: {
                    if (!m_talker_alias_received && (m_ta_dl > 0)) {
                        for (int i = 2; i < 9; i++) {
                            m_ta_data.append(raw_data[i]);
                        }

                        processTalkerAlias();
                    }
                }
                break;

                case FLCO_TALKER_ALIAS_BLOCK2: {
                    if (!m_talker_alias_received && (m_ta_dl > 0)) {
                        for (int i = 2; i < 9; i++) {
                            m_ta_data.append(raw_data[i]);
                        }

                        processTalkerAlias();
                    }
                }
                break;

                case FLCO_TALKER_ALIAS_BLOCK3: {
                    if (!m_talker_alias_received && (m_ta_dl > 0)) {
                        for (int i = 2; i < 9; i++) {
                            m_ta_data.append(raw_data[i]);
                        }

                        processTalkerAlias();
                    }
                }
                break;

                default:
                    m_logger->log(Logger::LogLevelDebug, QString("Unknown Embedded Data %1")
                                  .arg(QString::fromLocal8Bit((const char*)raw_data, 9)));
                    break;
            }

        }

        // Regenerate the previous super blocks Embedded Data or substitude the LC for it
        if (m_embedded_data[m_emb_read].isValid() && send_embedded_data) {
            lcss = m_embedded_data[m_emb_read].getData(data, N);
        } else {
            lcss = m_default_embedded_data.getData(data, N);
        }

        // Regenerate the EMB
        emb.setColorCode(1);
        emb.setLCSS(lcss);
        emb.getData(data);
    }

    dmr_data.setData(data);
}

void LogicalChannel::setUUID(CDMRData& dmr_data)
{
    unsigned int dataType = dmr_data.getDataType();

    if (dataType == DT_TERMINATOR_WITH_LC) {
        dmr_data.setUUID(m_call_uuid);
        // reset embedded data buffers
        ::memset(m_call_uuid, 0U, 16U);
    } else if ((dataType == DT_VOICE_LC_HEADER) ||
               (dataType == DT_DATA_HEADER)) {
        ::memset(m_call_uuid, 0U, 16U);
        ::uuid_generate_random(m_call_uuid);
        dmr_data.setUUID(m_call_uuid);
    } else {
        dmr_data.setUUID(m_call_uuid);
    }
}
