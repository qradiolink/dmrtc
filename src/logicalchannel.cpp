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

#include "logicalchannel.h"

const long long TX_TIME = 58000000L; // needs to be ~ two timeslots

LogicalChannel::LogicalChannel(Settings *settings, Logger *logger, unsigned int id,
                               unsigned int physical_channel, unsigned int slot, bool control_channel, QObject *parent) : QObject(parent)
{
    _id = id;
    _settings = settings;
    _logger = logger;
    _physical_channel = physical_channel;
    _slot = slot;
    _control_channel = control_channel;
    _busy = false;
    _call_in_progress = false;
    _disabled = false;
    _local_call = false;
    _state = CallState::CALL_STATE_NONE;
    _source_address = 0;
    _destination_address = 0;
    _emb_read = 1;
    _emb_write = 0;
    _text = "";
    _gps_info = "";
    _talker_alias_received = false;
    _ta_df = 0;
    _ta_dl = 0;
    _rx_freq = 0;
    _tx_freq = 0;
    _colour_code = 1;
    _lcn = _physical_channel + 1;
    _stream_id = 0;
    _data_frames = 0;
    _rssi = 0.0f;
    _ber = 0.0f;
    t1_rf = std::chrono::high_resolution_clock::now();
    t1_net = std::chrono::high_resolution_clock::now();
    _timeout_timer.setInterval(_settings->payload_channel_idle_timeout * 1000);
    _timeout_timer.setSingleShot(true);
    QObject::connect(&_timeout_timer, SIGNAL(timeout()), this, SLOT(setChannelIdle()), Qt::DirectConnection);
    QObject::connect(this, SIGNAL(internalStartTimer()), &_timeout_timer, SLOT(start()));
    QObject::connect(this, SIGNAL(internalStopTimer()), &_timeout_timer, SLOT(stop()));
    QMap<QString, uint64_t> channel;
    for(int i=0;i<_settings->logical_physical_channels.size();i++)
    {
        if(_settings->logical_physical_channels[i].value("logical_channel") == (_physical_channel + 1))
        {
            channel = _settings->logical_physical_channels[i];
            break;
        }
    }
    if(channel.size() >= 4)
    {
        _rx_freq = channel.value("rx_freq");
        _tx_freq = channel.value("tx_freq");
        _colour_code = channel.value("colour_code");
        _lcn = channel.value("logical_channel");
    }
    else
    {
        _logger->log(Logger::LogLevelWarning, QString("Could not find settings for logical channel %1 in"
            " the config file (section logical_physical_channels)").arg(_lcn));
    }

}

bool LogicalChannel::getChannelParams(uint64_t &params, uint8_t &colour_code)
{
    uint64_t lcn = _lcn;
    uint64_t tx_value_khz = _tx_freq % 1000000 / 125;
    uint64_t rx_value_khz = _rx_freq % 1000000 / 125;
    uint64_t tx_value_Mhz = _tx_freq / 1000000;
    uint64_t rx_value_Mhz = _rx_freq / 1000000;
    params = 0;
    params |= rx_value_khz;
    params |= rx_value_Mhz << 13;
    params |= tx_value_khz << 23;
    params |= tx_value_Mhz << 36;
    params |= lcn << 46;
    colour_code = (uint8_t) _colour_code;
    return true;
}

void LogicalChannel::allocateChannel(unsigned int srcId, unsigned int dstId, unsigned int call_type, bool local)
{
    _data_mutex.lock();
    _source_address = srcId;
    _destination_address = dstId;
    _call_type = call_type;
    _text = "";
    _gps_info = "";
    _talker_alias_received = false;
    _ta_df = 0;
    _ta_dl = 0;
    _ta_data.clear();
    _busy = true;
    _call_in_progress = false;
    _local_call = local;
    _data_frames = 0;
    _embedded_data[0].reset();
    _embedded_data[1].reset();
    _data_mutex.unlock();
    emit internalStartTimer();
    _logger->log(Logger::LogLevelDebug, QString("Allocated physical channel %1, slot %2 to destination %3 and source %4")
                 .arg(_physical_channel).arg(_slot).arg(dstId).arg(srcId));
}

void LogicalChannel::deallocateChannel()
{
    _data_mutex.lock();
    _busy = false;
    _call_in_progress = false;
    _local_call = false;
    _state = CallState::CALL_STATE_NONE;
    _embedded_data[0].reset();
    _embedded_data[1].reset();
    _text = "";
    _gps_info = "";
    _talker_alias_received = false;
    _ta_df = 0;
    _ta_dl = 0;
    _ta_data.clear();
    _data_mutex.unlock();
    _lc = CDMRLC(FLCO::FLCO_USER_USER, 0, 0);
    emit internalStopTimer();
    _logger->log(Logger::LogLevelDebug, QString("Deallocated physical channel %1, slot %2 from destination %3")
                 .arg(_physical_channel).arg(_slot).arg(_destination_address));
}

void LogicalChannel::updateChannel(unsigned int srcId, unsigned int dstId, unsigned int call_type)
{
    _data_mutex.lock();
    _source_address = srcId;
    _call_in_progress = true;
    _destination_address = dstId;
    _call_type = call_type;
    _data_mutex.unlock();
    startTimeoutTimer();
    _logger->log(Logger::LogLevelDebug, QString("Updated physical channel %1, slot %2 to destination %3 and source %4")
                 .arg(_physical_channel).arg(_slot).arg(dstId).arg(srcId));
}

void LogicalChannel::updateStats(CDMRData &dmr_data)
{
    unsigned int new_stream_id = dmr_data.getStreamId();
    unsigned int old_stream_id = _stream_id;
    float ber = 0.0f;
    float rssi = 0.0f;
    if(new_stream_id != old_stream_id)
    {
        _stream_id = new_stream_id;
        if(old_stream_id != 0)
        {
            rssi = _rssi / float(_data_frames);
            ber = _ber / float(_data_frames);
        }
        _rssi = float(dmr_data.getRSSI()) * -1.0f;
        _ber = float(dmr_data.getBER()) / 1.41f;
        _data_frames = 1;
    }
    else
    {
        _rssi += float(dmr_data.getRSSI()) * -1.0f;
        _ber += float(dmr_data.getBER()) / 1.41f;
        _data_frames++;
    }
}

void LogicalChannel::putRFQueue(CDMRData &dmr_data, bool first)
{
    rewriteEmbeddedData(dmr_data);
    _rf_queue_mutex.lock();
    if(first)
        _rf_queue.prepend(dmr_data);
    else
        _rf_queue.append(dmr_data);
    _rf_queue_mutex.unlock();
    _data_mutex.lock();
    _call_in_progress = true;
    _data_mutex.unlock();
}

bool LogicalChannel::getRFQueue(CDMRData &dmr_data)
{
    _rf_queue_mutex.lock();
    if(_rf_queue.size() < 1)
    {
        _rf_queue_mutex.unlock();
        return false;
    }
    CDMRData dt_first = _rf_queue.constFirst();
    _rf_queue_mutex.unlock();
    std::chrono::high_resolution_clock::time_point t2_rf = std::chrono::high_resolution_clock::now();

    if(_settings->prevent_mmdvm_overflows || (dt_first.getDataType() == DT_CSBK)
            || (dt_first.getDataType() == DT_VOICE_LC_HEADER)
            || (dt_first.getDataType() == DT_RATE_12_DATA)
            || (dt_first.getDataType() == DT_RATE_1_DATA)
            || (dt_first.getDataType() == DT_RATE_34_DATA))
    {
        if(std::chrono::duration_cast<std::chrono::nanoseconds>(t2_rf - t1_rf).count() < TX_TIME)
        {
            return false;
        }
    }
    _rf_queue_mutex.lock();
    dmr_data = _rf_queue.takeFirst();
    _rf_queue_mutex.unlock();
    t1_rf = std::chrono::high_resolution_clock::now();
    if(dmr_data.getDummy())
        return false;
    return true;
}

void LogicalChannel::putNetQueue(CDMRData &dmr_data)
{
    _data_mutex.lock();
    _call_in_progress = true;
    _data_mutex.unlock();
    rewriteEmbeddedData(dmr_data);
    _net_queue_mutex.lock();
    _net_queue.append(dmr_data);
    _net_queue_mutex.unlock();
}

bool LogicalChannel::getNetQueue(CDMRData &dmr_data)
{
    _net_queue_mutex.lock();
    if(_net_queue.size() < 1)
    {
        _net_queue_mutex.unlock();
        return false;
    }
    _net_queue_mutex.unlock();
    std::chrono::high_resolution_clock::time_point t2_net = std::chrono::high_resolution_clock::now();
    if(std::chrono::duration_cast<std::chrono::nanoseconds>(t2_net - t1_net).count() < (long long)TX_TIME)
    {
        return false;
    }
    _net_queue_mutex.lock();
    dmr_data = _net_queue.takeFirst();
    _net_queue_mutex.unlock();
    t1_net = std::chrono::high_resolution_clock::now();
    if(dmr_data.getDummy())
        return false;
    return true;
}

void LogicalChannel::clearNetQueue()
{
    _net_queue_mutex.lock();
    _net_queue.clear();
    _net_queue_mutex.unlock();
}

void LogicalChannel::clearRFQueue()
{
    _rf_queue_mutex.lock();
    _rf_queue.clear();
    _rf_queue_mutex.unlock();
}

void LogicalChannel::startTimeoutTimer()
{
    _timeout_timer.start();
}

void LogicalChannel::stopTimeoutTimer()
{
    _timeout_timer.stop();
}

void LogicalChannel::setChannelIdle()
{
    _data_mutex.lock();
    _busy = false;
    _data_mutex.unlock();
    emit channelDeallocated(_id);
    _logger->log(Logger::LogLevelDebug, QString("Physical channel %1, slot %2 to destination %3 and source %4 is marked as idle and deallocated")
                 .arg(_physical_channel).arg(_slot).arg(_destination_address).arg(_source_address));
}

bool LogicalChannel::isControlChannel()
{
    _data_mutex.lock();
    bool control = _control_channel;
    _data_mutex.unlock();
    return control;
}

unsigned int LogicalChannel::getPhysicalChannel()
{
    _data_mutex.lock();
    unsigned int physical_channel = _physical_channel;
    _data_mutex.unlock();
    return physical_channel;
}

unsigned int LogicalChannel::getSlot()
{
    _data_mutex.lock();
    unsigned int slot = _slot;
    _data_mutex.unlock();
    return slot;
}

bool LogicalChannel::getBusy()
{
    _data_mutex.lock();
    bool busy = _busy;
    _data_mutex.unlock();
    return busy;
}

bool LogicalChannel::getDisabled()
{
    _data_mutex.lock();
    bool disabled = _disabled;
    _data_mutex.unlock();
    return disabled;
}


void LogicalChannel::setDisabled(bool disabled)
{
    _data_mutex.lock();
    _disabled = disabled;
    _data_mutex.unlock();
    _logger->log(Logger::LogLevelInfo, QString("State of channel %1, slot %2 changed to %3")
                 .arg(_physical_channel + 1)
                 .arg(_slot)
                 .arg(disabled ? "disabled" : "enabled"));
}

unsigned int LogicalChannel::getDestination()
{
    _data_mutex.lock();
    unsigned int destination = _destination_address;
    _data_mutex.unlock();
    return destination;
}

unsigned int LogicalChannel::getSource()
{
    _data_mutex.lock();
    unsigned int source = _source_address;
    _data_mutex.unlock();
    return source;
}

void LogicalChannel::setBusy(bool busy)
{
    _data_mutex.lock();
    _busy = busy;
    _data_mutex.unlock();
}

void LogicalChannel::setLocalCall(bool local)
{
    _data_mutex.lock();
    _local_call = local;
    _data_mutex.unlock();
}

bool LogicalChannel::getLocalCall()
{
    _data_mutex.lock();
    bool local = _local_call;
    _data_mutex.unlock();
    return local;
}

void LogicalChannel::setDestination(unsigned int destination)
{
    _data_mutex.lock();
    _destination_address = destination;
    _data_mutex.unlock();
}

void LogicalChannel::setSource(unsigned int source)
{
    _data_mutex.lock();
    _source_address = source;
    _data_mutex.unlock();
}

void LogicalChannel::setCallType(unsigned int call_type)
{
    _data_mutex.lock();
    _call_type = call_type;
    _data_mutex.unlock();
}

unsigned int LogicalChannel::getCallType()
{
    _data_mutex.lock();
    unsigned int call_type = _call_type;
    _data_mutex.unlock();
    return call_type;
}

QString LogicalChannel::getText()
{
    _data_mutex.lock();
    QString text = _text;
    _data_mutex.unlock();
    return text;
}

void LogicalChannel::setText(QString txt)
{
    _data_mutex.lock();
    _text = QString("%1 %2").arg(txt).arg((_ta_df == 3) ? "UTF-16" : "");
    _data_mutex.unlock();
    emit update();
}

void LogicalChannel::setGPSInfo(float longitude, float latitude, std::string error)
{
    _data_mutex.lock();
    _gps_info = QString("Longitude: %1, Latitude: %2, Error: %3")
            .arg(longitude)
            .arg(latitude)
            .arg(QString::fromStdString(error));
    _data_mutex.unlock();
    emit update();
    _logger->log(Logger::LogLevelDebug, QString("GPS Info received from %1 to %2: %3")
                 .arg(QString::number(getSource())).arg(QString::number(getDestination())).arg(_gps_info));
}

QString LogicalChannel::getGPSInfo()
{
    QString gps_info;
    _data_mutex.lock();
    gps_info = _gps_info;
    _data_mutex.unlock();
    return gps_info;
}

void LogicalChannel::processTalkerAlias()
{
    unsigned int size = _ta_data.size();
    if(((_ta_df == 1 || _ta_df == 2) && (size >= _ta_dl)) || ((_ta_df == 3) && (size >= _ta_dl*2)))
    {
        // TODO: handle ISO 7 bit
        if(_ta_df == 1 || _ta_df == 2)
        {
            QString txt = QString::fromUtf8(_ta_data);
            setText(txt);
        }
        else if(_ta_df == 3)
        {
            if(QSysInfo::ByteOrder == QSysInfo::BigEndian)
            {
                QString txt = QString::fromUtf16((char16_t*)_ta_data.constData(), size/2);
                setText(txt);
            }
            else
            {
                QString txt;
                Utils::parseUTF16(txt, size, (unsigned char*)_ta_data.data());
                setText(txt);
            }
        }
        _talker_alias_received = true;
        _ta_data.clear();
    }
}

void LogicalChannel::rewriteEmbeddedData(CDMRData &dmr_data)
{
    _lc = CDMRLC(dmr_data.getFLCO(), dmr_data.getSrcId(), dmr_data.getDstId());
    unsigned int N = dmr_data.getN();
    _default_embedded_data.setLC(_lc);
    unsigned char data[DMR_FRAME_LENGTH_BYTES];
    dmr_data.getData(data);
    unsigned int dataType = dmr_data.getDataType();
    if(dataType == DT_VOICE_LC_HEADER || dataType == DT_TERMINATOR_WITH_LC)
    {
        // match LC with rewritten src and destination
        CDMRFullLC fullLC;
        CDMRLC lc(dmr_data.getFLCO(), dmr_data.getSrcId(), dmr_data.getDstId());
        fullLC.encode(lc, data, dataType);
        CDMRSlotType slotType;
        slotType.setColorCode(1);
        slotType.setDataType(dataType);
        slotType.getData(data);
        CSync::addDMRDataSync(data, true);
        _data_mutex.lock();
        _gps_info = "";
        _talker_alias_received = false;
        _ta_df = 0;
        _ta_dl = 0;
        _ta_data.clear();
        _data_mutex.unlock();
    }
    if(dataType == DT_TERMINATOR_WITH_LC)
    {
        // reset embedded data buffers
        _lc = CDMRLC(FLCO::FLCO_USER_USER, 0, 0);
        _embedded_data[0].reset();
        _embedded_data[1].reset();
        _default_embedded_data.reset();

    }
    else if(dataType == DT_VOICE_LC_HEADER)
    {
        _embedded_data[0].reset();
        _embedded_data[1].reset();
        _default_embedded_data.reset();
        _default_embedded_data.setLC(_lc);
    }
    else if(dataType == DT_VOICE_SYNC)
    {

        _emb_read = (_emb_read + 1) % 2;
        _emb_write = (_emb_write + 1) % 2;
        _embedded_data[_emb_write].reset();
    }
    else if(dataType == DT_VOICE)
    {
        CDMREMB emb;
        emb.putData(data);
        unsigned char lcss = emb.getLCSS();


        bool ret = _embedded_data[_emb_write].addData(data, lcss);
        if (ret)
        {
            FLCO flco = _embedded_data[_emb_write].getFLCO();
            unsigned char raw_data[9U];
            _embedded_data[_emb_write].getRawData(raw_data);

            switch (flco) {
            case FLCO_GROUP:
            case FLCO_USER_USER:
            {
                if(_embedded_data[_emb_write].isValid())
                {
                    _embedded_data[_emb_write].setLC(_lc);
                }
            }
                break;
            case FLCO_GPS_INFO:
            {
                float longitude, latitude = 0.0f;
                std::string error;
                CUtils::extractGPSPosition(raw_data, error, longitude, latitude);
                setGPSInfo(longitude, latitude, error);
            }
                break;

            case FLCO_TALKER_ALIAS_HEADER:
            {
                if(!_talker_alias_received)
                {
                    _ta_df = (raw_data[2] >> 6) & 0x03;
                    _ta_dl = (raw_data[2] >> 1) & 0x1F;
                    for(int i=3;i<9;i++)
                    {
                        _ta_data.append(raw_data[i]);
                    }
                    processTalkerAlias();
                }
            }
                break;

            case FLCO_TALKER_ALIAS_BLOCK1:
            {
                if(!_talker_alias_received)
                {
                    for(int i=2;i<9;i++)
                    {
                        _ta_data.append(raw_data[i]);
                    }
                    processTalkerAlias();
                }
            }
                break;

            case FLCO_TALKER_ALIAS_BLOCK2:
            {
                if(!_talker_alias_received)
                {
                    for(int i=2;i<9;i++)
                    {
                        _ta_data.append(raw_data[i]);
                    }
                    processTalkerAlias();
                }
            }
                break;

            case FLCO_TALKER_ALIAS_BLOCK3:
            {
                if(!_talker_alias_received)
                {
                    for(int i=2;i<9;i++)
                    {
                        _ta_data.append(raw_data[i]);
                    }
                    processTalkerAlias();
                }
            }
                break;

            default:
                _logger->log(Logger::LogLevelDebug, QString("Unknown Embedded Data %1")
                             .arg(QString::fromLocal8Bit((const char*)raw_data, 9)));
                break;
            }

        }


        // Regenerate the previous super blocks Embedded Data or substitude the LC for it
        if (_embedded_data[_emb_read].isValid())
        {
            lcss = _embedded_data[_emb_read].getData(data, N);
        }
        else
        {
            lcss = _default_embedded_data.getData(data, N);
        }

        // Regenerate the EMB
        emb.setColorCode(1);
        emb.setLCSS(lcss);
        emb.getData(data);
    }
    dmr_data.setData(data);
}
