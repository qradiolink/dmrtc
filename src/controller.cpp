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

#include "controller.h"

Controller::Controller(Settings *settings, Logger *logger, DMRIdLookup *id_lookup, QObject *parent) : QObject(parent)
{
    _settings = settings;
    _logger = logger;
    _id_lookup = id_lookup;
    _mmdvm_config = new QVector<unsigned char>;
    _registered_ms = new QList<unsigned int>;
    _talkgroup_attachments = new QMap<unsigned int, QList<unsigned int>>;
    _rejected_calls = new QSet<unsigned int>;
    _subscribed_talkgroups = new QSet<unsigned int>;
    // Because ACKU CSBK contains no ServiceKind, need to store some state to determine which service is it pertinent for
    _uplink_acks = new QMap<unsigned int, unsigned int>;
    _dmr_rewrite = new DMRRewrite(settings, _registered_ms);
    _gateway_router = new GatewayRouter(_settings, _logger);
    _signalling_generator = new Signalling(_settings);
    _stop_thread = false;
    _late_entry_announcing = false;
    _system_freqs_announcing = false;
    t1_ping_ms = std::chrono::high_resolution_clock::now();
    _startup_completed = false;
    _minute = 1;
    _data_msg_size = 0;
    _data_pad_nibble = 0;
    _udt_format = 0;
    memset(_data_message, 0, 48U);
}

Controller::~Controller()
{
    _mmdvm_config->clear();
    delete _mmdvm_config;
    _registered_ms->clear();
    delete _registered_ms;
    _talkgroup_attachments->clear();
    delete _talkgroup_attachments;
    _rejected_calls->clear();
    delete _rejected_calls;
    _uplink_acks->clear();
    _subscribed_talkgroups->clear();
    delete _subscribed_talkgroups;
    delete _uplink_acks;
    delete _dmr_rewrite;
    delete _gateway_router;
    delete _signalling_generator;
}

void Controller::stop()
{
    // end main thread execution
    _logger->log(Logger::LogLevelInfo, QString("Stopping controller thread"));
    _stop_thread=true;
    QThread::sleep(1);
}

void Controller::run()
{
    /// Create logical channels
    uint8_t counter = 0;
    QTimer gateway_timer;
    QTimer announce_system_freqs_timer;
    for(int i=0; i<_settings->channel_number; i++)
    {

        if(i == _settings->control_channel_physical_id)
        {
            _control_channel = new LogicalChannel(_settings, _logger, counter, i, 1, true);
            counter++;
            _logical_channels.append(_control_channel);
            if(_settings->control_channel_slot == 1)
            {
                LogicalChannel *payload_channel = new LogicalChannel(_settings, _logger, counter, i, 2, false);
                counter++;
                QObject::connect(payload_channel, SIGNAL(channelDeallocated(unsigned int)), this, SLOT(handleIdleChannelDeallocation(unsigned int)));
                _logical_channels.append(payload_channel);
            }
            else
            {
                _control_channel_alternate = new LogicalChannel(_settings, _logger, counter, i, 2, true);
                counter++;
                _logical_channels.append(_control_channel_alternate);
            }
        }
        else
        {
            LogicalChannel *payload_channel1 = new LogicalChannel(_settings, _logger, counter, i, 1, false);
            counter++;
            LogicalChannel *payload_channel2 = new LogicalChannel(_settings, _logger, counter, i, 2, false);
            counter++;
            QObject::connect(payload_channel1, SIGNAL(channelDeallocated(unsigned int)), this, SLOT(handleIdleChannelDeallocation(unsigned int)));
            QObject::connect(payload_channel2, SIGNAL(channelDeallocated(unsigned int)), this, SLOT(handleIdleChannelDeallocation(unsigned int)));
            _logical_channels.append(payload_channel1);
            _logical_channels.append(payload_channel2);
        }
    }
    if(!_settings->headless_mode)
    {
        emit updateLogicalChannels(&_logical_channels);
    }

    /// Create network sockets
    for(int i=0; i<_settings->channel_number; i++)
    {
        UDPClient *client = new UDPClient(_settings, _logger, i);
        _udp_channels.append(client);
        client->enable(true);
        QObject::connect(client, SIGNAL(dmrData(unsigned char*,int, bool)), this, SLOT(processDMRPayload(unsigned char*,int, bool)), Qt::DirectConnection);
        QObject::connect(client, SIGNAL(newMMDVMConfig(unsigned char*,int)),
                         this, SLOT(updateMMDVMConfig(unsigned char*,int)), Qt::DirectConnection);
    }
    for(int i=0;i<_settings->gateway_number;i++)
    {
        UDPClient *gateway_udpclient = new UDPClient(_settings, _logger, i, _settings->gateway_listen_port + i, _settings->gateway_send_port + i,
                                                          _settings->gateway_remote_address, true);

        QObject::connect(gateway_udpclient, SIGNAL(dmrData(unsigned char*,int, bool)),
                         this, SLOT(processDMRPayload(unsigned char*,int, bool)));
        QObject::connect(this, SIGNAL(writeDMRData(CDMRData&)),
                         gateway_udpclient, SLOT(writeDMRData(CDMRData&)));
        _gateway_channels.append(gateway_udpclient);
    }

    if(_settings->gateway_enabled)
    {
        for(int i=0;i<_settings->gateway_number;i++)
        {
            _gateway_channels.at(i)->enable(true);
        }
    }

    gateway_timer.setInterval(3000);
    gateway_timer.setSingleShot(false);
    if(_settings->gateway_enabled)
    {
        QObject::connect(&gateway_timer, SIGNAL(timeout()), this, SLOT(writeDMRConfig()));
        gateway_timer.start();
    }
    announce_system_freqs_timer.setInterval(_settings->announce_system_freqs_interval * 1000);
    announce_system_freqs_timer.setSingleShot(true);
    announce_system_freqs_timer.start();


    /// Main thread loop where most things happen
    while(!_stop_thread)
    {
        if(!_startup_completed)
        {
            requestMassRegistration();
        }
        QtConcurrent::run(this, &Controller::announceLateEntry);
        if(!announce_system_freqs_timer.isActive())
        {
            QtConcurrent::run(this, &Controller::announceSystemFreqs);
            announce_system_freqs_timer.start();
        }

        uint16_t min = QDateTime::currentDateTime().time().minute();
        if(((min == 0) || (min == 30) || (min == 15) || (min == 45)) && (min != _minute))
        {
            _minute = min;
            QtConcurrent::run(this, &Controller::announceLocalTime);
            if(_settings->announce_system_message && ((min == 0) || (min == 30)))
            {
                QtConcurrent::run(this, &Controller::announceSystemMessage);
            }
        }

        QCoreApplication::processEvents(); // process signals


        for(int i=0;i<_logical_channels.size();i++)
        {
            /// data going towards RF
            CDMRData dmr_data;
            if(_logical_channels.at(i)->getRFQueue(dmr_data))
            {
                _udp_channels.at(_logical_channels.at(i)->getPhysicalChannel())->writeDMRData(dmr_data);
            }

            /// Data going towards net
            CDMRData dmr_data_net;
            if(_logical_channels.at(i)->getNetQueue(dmr_data_net))
            {
                unsigned int gateway_id = 0;
                bool route_found = _gateway_router->findRoute(dmr_data_net, gateway_id);
                if(route_found)
                {
                    _gateway_channels[gateway_id]->writeDMRData(dmr_data_net);
                }
                else
                {
                    // send on default route, gateway 0
                    _gateway_channels[gateway_id]->writeDMRData(dmr_data_net);
                }
            }

        }

        QThread::usleep(1000);
    }

    /// Thread stopping
    ///
    ///


    for(int i=0; i<_logical_channels.size(); i++)
    {
        _logical_channels.at(i)->stopTimeoutTimer();
    }
    for(int i=0; i<_settings->channel_number; i++)
    {
        _udp_channels.at(i)->enable(false);
    }
    for(int i=0;i<_settings->gateway_number;i++)
    {
        _gateway_channels.at(i)->enable(false);
    }
    for(int i=0; i<_settings->channel_number; i++)
    {
        delete _udp_channels.at(i);
    }
    for(int i=0; i<_logical_channels.size(); i++)
    {
        delete _logical_channels[i];
    }
    for(int i=0;i<_settings->gateway_number;i++)
    {
        delete _gateway_channels[i];
    }
    gateway_timer.stop();
    announce_system_freqs_timer.stop();
    emit finished();
}



void Controller::announceLateEntry()
{
    if(_late_entry_announcing)
        return;
    _late_entry_announcing = true;
    if(_stop_thread)
        return;
    for(int i=0;i<_logical_channels.size();i++)
    {

        if(_logical_channels.at(i)->getBusy() && !_logical_channels.at(i)->getDisabled()
                && !_logical_channels.at(i)->isControlChannel()
                && (_logical_channels.at(i)->getDestination() != 0))
        {
            LogicalChannel *logical_channel = _logical_channels.at(i);
            CDMRCSBK csbk;
            _signalling_generator->createLateEntryAnnouncement(logical_channel, csbk);
            CDMRCSBK csbk2;
            bool valid = _signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);
            if(valid)
            {
                transmitCSBK(csbk, logical_channel, _control_channel->getSlot(), _control_channel->getPhysicalChannel(), false);
                transmitCSBK(csbk2, logical_channel, _control_channel->getSlot(), _control_channel->getPhysicalChannel(), false, false);
            }
            else
            {
                transmitCSBK(csbk, logical_channel, _control_channel->getSlot(), _control_channel->getPhysicalChannel(), false);
            }
            // TODO: Set late entry time so it doesn't clash with other data
            QThread::sleep((unsigned long) _settings->announce_late_entry_interval);
            if(_stop_thread)
                return;
        }
    }
    _late_entry_announcing = false;
}

void Controller::requestMassRegistration()
{
    _logger->log(Logger::LogLevelInfo, QString("Requesting mass registration"));
    CDMRCSBK csbk;
    _signalling_generator->createRegistrationRequest(csbk);
    transmitCSBK(csbk, nullptr, _control_channel->getSlot(), _control_channel->getPhysicalChannel(), false, true);
    _startup_completed = true;
    _registered_ms->clear();
}

void Controller::announceSystemFreqs()
{
    if(_system_freqs_announcing)
        return;
    _system_freqs_announcing = true;
    if(_stop_thread)
        return;
    _logger->log(Logger::LogLevelInfo, QString("Announcing site frequencies, used channels: %1")
                 .arg(_settings->logical_physical_channels.size()));
    for(int i = 0;i<_settings->logical_physical_channels.size(); i++)
    {
        if(_settings->logical_physical_channels[i].size() < 4)
            continue;
        QMap<QString, uint64_t> channel = _settings->logical_physical_channels[i];
        CDMRCSBK csbk, csbk_cont;
        _signalling_generator->createLogicalPhysicalChannelsAnnouncement(csbk, csbk_cont, channel);
        transmitCSBK(csbk, nullptr, _control_channel->getSlot(), _control_channel->getPhysicalChannel(), false);
        transmitCSBK(csbk_cont, nullptr, _control_channel->getSlot(), _control_channel->getPhysicalChannel(), false);
        if(_stop_thread)
            return;
    }

    _system_freqs_announcing = false;
}

void Controller::announceLocalTime()
{
    if(_stop_thread)
        return;
    QDateTime date_time = QDateTime::currentDateTime();
    _logger->log(Logger::LogLevelInfo, QString("Announcing local time: %1")
                 .arg(QDateTime::currentDateTime().toString()));
    CDMRCSBK csbk;
    _signalling_generator->createLocalTimeAnnouncement(csbk, date_time);
    transmitCSBK(csbk, nullptr, _control_channel->getSlot(), _control_channel->getPhysicalChannel(), false);
}

void Controller::announceSystemMessage()
{
    if(_stop_thread)
        return;
    announceLocalTime();
    QString message = _settings->system_announcement_message;
    _logger->log(Logger::LogLevelInfo, QString("Announcing system message: %1").arg(message));
    unsigned int dstId = StandardAddreses::ALLMSID;
    sendUDTShortMessage(message, dstId);
}

void Controller::sendUDTShortMessage(QString message, unsigned int dstId, unsigned int srcId)
{
    if(message.size() > 46)
        message = message.chopped(46);
    message = message.leftJustified(46, ' ', true);
    if(dstId == 0)
    {
        // don't process ACKU from all
        dstId = StandardAddreses::ALLMSID;
    }
    else
    {
        // process ACKU from target
        _uplink_acks->insert(dstId, ServiceAction::ActionMessageRequest);
        _logger->log(Logger::LogLevelInfo, QString("Sending system message %1 to radio: %2").arg(message).arg(dstId));
    }
    if(srcId == 0)
    {
        srcId = StandardAddreses::SDMI;
    }
    CDMRDataHeader header;
    header.setA(false);
    header.setGI(false);
    header.setRSVD(0x00);
    header.setFormat(0x00);
    header.setSAP(0x00);
    header.setUDTFormat(0x04);
    header.setDstId(dstId);
    header.setSrcId(srcId);
    header.setSF(false);
    header.setPF(false);
    header.setBlocks(4);
    header.setPadNibble(0);
    header.setOpcode(UDTOpcode::C_UDTHD);
    header.construct();
    unsigned char header_data[DMR_FRAME_LENGTH_BYTES];
    header.get(header_data);
    CDMRSlotType slotType;
    slotType.setColorCode(1);
    slotType.setDataType(DT_DATA_HEADER);
    slotType.getData(header_data);
    CSync::addDMRDataSync(header_data, true);
    CDMRData dmr_data_header;
    dmr_data_header.setSeqNo(0);
    dmr_data_header.setN(0);
    dmr_data_header.setDataType(DT_DATA_HEADER);
    dmr_data_header.setSlotNo(_control_channel->getSlot());
    dmr_data_header.setDstId(header.getDstId());
    dmr_data_header.setSrcId(header.getSrcId());
    dmr_data_header.setData(header_data);
    _control_channel->putRFQueue(dmr_data_header);


    unsigned char *data_message = (unsigned char*)(message.toLocal8Bit().constData());
    unsigned char data[48U];
    memset(data, 0U, 48U);
    memcpy(data, data_message, 46U);
    unsigned char payload_data[4][DMR_FRAME_LENGTH_BYTES];
    CCRC::addCCITT162(data, 48U);

    for(int i=0;i<3;i++)
    {
        unsigned char payload[12];
        memcpy(payload, data + i*12U, 12U);
        CBPTC19696 bptc1;
        bptc1.encode(payload, payload_data[i]);
        CDMRSlotType slotType1;
        slotType1.putData(payload_data[i]);
        slotType1.setDataType(DT_RATE_12_DATA);
        slotType1.setColorCode(1);
        slotType1.getData(payload_data[i]);
        CSync::addDMRDataSync(payload_data[i], true);
        CDMRData dmr_data;
        dmr_data.setSeqNo(0);
        dmr_data.setN(0);
        dmr_data.setDataType(DT_RATE_12_DATA);
        dmr_data.setSlotNo(_control_channel->getSlot());
        dmr_data.setDstId(header.getDstId());
        dmr_data.setSrcId(header.getSrcId());
        dmr_data.setData(payload_data[i]);
        _control_channel->putRFQueue(dmr_data);
    }
    unsigned char final_block[12U];
    memset(final_block, 0, 12U);
    memcpy(final_block, data + 36U, 10U);
    final_block[10U] = data[46U];
    final_block[11U] = data[47U];
    CBPTC19696 bptc3;
    bptc3.encode(final_block, payload_data[3]);
    CDMRSlotType slotType3;
    slotType3.putData(payload_data[3]);
    slotType3.setColorCode(1);
    slotType3.setDataType(DT_RATE_12_DATA);
    slotType3.getData(payload_data[3]);
    CSync::addDMRDataSync(payload_data[3], true);
    CDMRData dmr_data3;
    dmr_data3.setSeqNo(0);
    dmr_data3.setN(0);
    dmr_data3.setDataType(DT_RATE_12_DATA);
    dmr_data3.setSlotNo(_control_channel->getSlot());
    dmr_data3.setDstId(header.getDstId());
    dmr_data3.setSrcId(header.getSrcId());
    dmr_data3.setData(payload_data[3]);
    _control_channel->putRFQueue(dmr_data3);
}

void Controller::pingRadio(unsigned int target_id, bool group)
{
    if(target_id == 0)
        return;
    _logger->log(Logger::LogLevelInfo, QString("Checking presence for target: %1").arg(target_id));
    t1_ping_ms = std::chrono::high_resolution_clock::now();
    _uplink_acks->insert(target_id, ServiceAction::ActionPingRequest);
    CDMRCSBK csbk;
    _signalling_generator->createPresenceCheckAhoy(csbk, target_id, group);
    transmitCSBK(csbk, nullptr, _control_channel->getSlot(), _control_channel->getPhysicalChannel(), false, true);
}

void Controller::resetPing()
{
    QList<unsigned int> target_ids;
    QMapIterator<unsigned int, unsigned int> it(*_uplink_acks);
    while(it.hasNext())
    {
        it.next();
        if(it.value() == ServiceAction::ActionPingRequest)
        {
            target_ids.append(it.key());
        }
    }
    for(int i=0;i<target_ids.size();i++)
    {
        _uplink_acks->remove(target_ids[i]);
    }
}

LogicalChannel* Controller::findNextFreePayloadChannel()
{
    for(int i=0; i<_logical_channels.size(); i++)
    {
        if(!(_logical_channels[i]->isControlChannel())
                && !_logical_channels[i]->getDisabled()
                && !(_logical_channels[i]->getBusy()))
        {
            return _logical_channels[i];
        }
    }
    return nullptr;
}

LogicalChannel* Controller::findCallChannel(unsigned int dstId, unsigned int srcId)
{
    for(int i=0; i<_logical_channels.size(); i++)
    {
        if((!_logical_channels[i]->isControlChannel()) && (_logical_channels[i]->getDestination() == dstId)
                && _logical_channels[i]->getBusy()
                && !_logical_channels[i]->getDisabled())
        {
            return _logical_channels[i];
        }
        if((!_logical_channels[i]->isControlChannel()) && ((_logical_channels[i]->getDestination() == dstId) ||
                                                           (_logical_channels[i]->getDestination() == srcId))
                && _logical_channels[i]->getBusy()
                && !_logical_channels[i]->getDisabled())
        {
            return _logical_channels[i];
        }
    }
    return nullptr;
}

LogicalChannel* Controller::findChannelByPhysicalIdAndSlot(unsigned int physical_id, unsigned int slot)
{
    for(int i=0; i<_logical_channels.size(); i++)
    {
        if((_logical_channels[i]->getPhysicalChannel() == physical_id) && (_logical_channels[i]->getSlot() == slot))
        {
            return _logical_channels[i];
        }
    }
    return nullptr;
}

QVector<LogicalChannel*> Controller::findActiveChannels()
{
    QVector<LogicalChannel*> active_channels;
    for(int i=0; i<_logical_channels.size(); i++)
    {
        if((!_logical_channels[i]->isControlChannel())
                && _logical_channels[i]->getBusy()
                && !_logical_channels[i]->getDisabled())
        {
            active_channels.append(_logical_channels[i]);
        }
    }
    return active_channels;
}

void Controller::processDMRPayload(unsigned char *payload, int udp_channel_id, bool from_gateway)
{
    unsigned char seqNo = payload[4U];
    unsigned int srcId = (payload[5U] << 16) | (payload[6U] << 8) | (payload[7U] << 0);
    unsigned int dstId = (payload[8U] << 16) | (payload[9U] << 8) | (payload[10U] << 0);
    unsigned int slotNo = (payload[15U] & 0x80U) == 0x80U ? 2U : 1U;
    unsigned int streamId = 0;
    unsigned char ber = payload[53U];
    unsigned char rssi = payload[54U];
    ::memcpy(&streamId, payload + 16U, 4U);

    FLCO flco = (payload[15U] & 0x40U) == 0x40U ? FLCO_USER_USER : FLCO_GROUP;
    CDMRData dmr_data;
    dmr_data.setSeqNo(seqNo);
    dmr_data.setStreamId(streamId);
    dmr_data.setSlotNo(slotNo);
    dmr_data.setSrcId(srcId);
    dmr_data.setDstId(dstId);
    dmr_data.setFLCO(flco);
    dmr_data.setBER(ber);
    dmr_data.setRSSI(rssi);

    bool dataSync = (payload[15U] & 0x20U) == 0x20U;
    bool voiceSync = (payload[15U] & 0x10U) == 0x10U;

    if (dataSync)
    {
        unsigned char dataType = payload[15U] & 0x0FU;
        dmr_data.setData(payload + 20U);
        dmr_data.setDataType(dataType);
        dmr_data.setN(0U);
        if(dataType == DT_CSBK && !from_gateway)
        {
            // skip network csbk for now
            processSignalling(dmr_data, udp_channel_id);
        }
        else if(((dataType == DT_DATA_HEADER) ||
                (dataType == DT_RATE_12_DATA) ||
                (dataType == DT_RATE_1_DATA) ||
                (dataType == DT_RATE_34_DATA))
                )
        {
            processData(dmr_data, udp_channel_id, from_gateway);
        }
        else if(dataType == DT_CSBK && from_gateway)
        {
            processNetworkCSBK(dmr_data, udp_channel_id);
        }
        else
        {
            if(!from_gateway && !validateLocalSourceId(srcId))
            {
                delete[] payload;
                return;
            }
            processVoice(dmr_data, udp_channel_id, true, from_gateway);
        }
    }
    else if (voiceSync)
    {
        if(!from_gateway && !validateLocalSourceId(srcId))
        {
            delete[] payload;
            return;
        }
        dmr_data.setData(payload + 20U);
        dmr_data.setDataType(DT_VOICE_SYNC);
        dmr_data.setN(0U);
        processVoice(dmr_data, udp_channel_id, false, from_gateway);
    }
    else
    {
        if(!from_gateway && !validateLocalSourceId(srcId))
        {
            delete[] payload;
            return;
        }
        unsigned char n = payload[15U] & 0x0FU;
        dmr_data.setData(payload + 20U);
        dmr_data.setDataType(DT_VOICE);
        dmr_data.setN(n);
        processVoice(dmr_data, udp_channel_id, false, from_gateway);
    }
    delete[] payload;
    // forward to network

}

void Controller::updateTalkgroupSubscriptions(unsigned int srcId)
{
    unsigned int size = _data_msg_size * 12 - _data_pad_nibble / 2 - 2;
    unsigned char msg[size];
    memcpy(msg, _data_message, size);
    _uplink_acks->remove(srcId);
    bool existing_user = _registered_ms->contains(srcId);
    if(!existing_user)
        _registered_ms->append(srcId);
    if(!_settings->headless_mode)
    {
        emit updateRegisteredMSList(_registered_ms);
    }
    CDMRCSBK csbk;
    _signalling_generator->createReplyRegistrationAccepted(csbk, srcId);
    transmitCSBK(csbk, nullptr, _control_channel->getSlot(), _control_channel->getPhysicalChannel(), false, false);
    if(!existing_user && _settings->announce_system_message)
    {
        QString message = QString("Welcome %1, there are %2 users online").arg(_id_lookup->getCallsign(srcId)).arg(_registered_ms->size());
        sendUDTShortMessage(message, srcId);
    }

    QList<unsigned int> tg_list;
    for(unsigned int i=1;i<size;i=i+3)
    {
        unsigned int tg = 0;
        tg |= msg[i] << 16;
        tg |= msg[i+1] << 8;
        tg |= msg[i+2];
        unsigned int converted_id = Utils::convertBase11GroupNumberToBase10(tg);
        tg_list.append(converted_id);
        _subscribed_talkgroups->insert(converted_id);
        _logger->log(Logger::LogLevelInfo, QString("Received talkgroup attachment data from %1: %2")
                     .arg(srcId).arg(converted_id));
    }
    _talkgroup_attachments->insert(srcId, tg_list);
    _subscribed_talkgroups->clear();
    QMapIterator<unsigned int, QList<unsigned int>> it(*_talkgroup_attachments);
    while(it.hasNext())
    {
        it.next();
        _subscribed_talkgroups->unite(QSet<unsigned int> (it.value().begin(), it.value().end()));
    }
    if(!_settings->headless_mode)
    {
        emit updateTalkgroupSubscriptionList(_subscribed_talkgroups);
    }
}

void Controller::processTextMessage(unsigned int dstId, unsigned int srcId, bool group)
{
    if((_udt_format == 4) || (_udt_format == 3) || (_udt_format == 7))
    {
        unsigned int size = _data_msg_size * 12 - _data_pad_nibble / 2 - 2 - 1; // size does not include CRC16 and last character
        unsigned char msg[size];
        memcpy(msg, _data_message, size);
        QString text_message;
        if(_udt_format == 4)
            text_message = QString::fromUtf8((const char*)msg, size);
        else if(_udt_format == 3)
            text_message = QString::fromLatin1((const char*)msg, size);
        else if(_udt_format == 7)
            text_message = QString::fromUtf16((const char16_t*)msg, size);
        if(group)
        {
            _logger->log(Logger::LogLevelInfo, QString("Received group UDT short data message from %1 to %2: %3")
                  .arg(srcId)
                  .arg(Utils::convertBase11GroupNumberToBase10(dstId))
                  .arg(text_message));
        }
        else
        {
            _logger->log(Logger::LogLevelInfo, QString("Received private UDT short data message from %1 to %2: %3")
                      .arg(srcId)
                      .arg(dstId)
                      .arg(text_message));
        }
        if(!_settings->headless_mode)
        {
            if(group)
            {
                emit updateMessageLog(srcId,
                                   Utils::convertBase11GroupNumberToBase10(dstId), text_message, true);
            }
            else
            {
                emit updateMessageLog(srcId, dstId, text_message, false);
            }
        }

    }
}

void Controller::processData(CDMRData &dmr_data, unsigned int udp_channel_id, bool from_gateway)
{
    unsigned int srcId = dmr_data.getSrcId();
    unsigned int dstId = dmr_data.getDstId();
    if(dmr_data.getFLCO() == FLCO_GROUP)
    {
        if(dmr_data.getDataType() == DT_DATA_HEADER)
        {
            unsigned char data[DMR_FRAME_LENGTH_BYTES];
            dmr_data.getData(data);
            CDMRDataHeader header;
            header.put(data);
            if(header.getUDT())
            {
                _data_msg_size = header.getBlocks();
                _data_pad_nibble = header.getPadNibble();
                _data_block = _data_msg_size;
                _udt_format = header.getUDTFormat();
                qDebug() << "A: " << header.getA() << " GI: " << header.getGI() << " Format: " << header.getFormat()
                         << " UDTFormat: " << header.getUDTFormat() << " Opcode: " << header.getOpcode() << " RSVD: " << header.getRSVD()
                         << " PF: " << header.getPF() << " SF: " << header.getSF() << " SAP: " << header.getSAP();
            }
            else
            {
                _data_msg_size = 0;
            }

        }
        else if((dmr_data.getDataType() == DT_RATE_12_DATA) && (_data_msg_size > 0))
        {
            unsigned char block[12];
            memset(block, 0, 12U);
            unsigned char data[DMR_FRAME_LENGTH_BYTES];
            dmr_data.getData(data);
            CBPTC19696 bptc;
            bptc.decode(data, block);
            memcpy(_data_message + ((_data_msg_size - _data_block) * 12) , block, 12U);
            if(_data_block == 1)
            {
                bool valid = CCRC::checkCCITT162(_data_message, _data_msg_size*12);
                if(valid)
                {
                    /// Text message
                    processTextMessage(dstId, srcId, dmr_data.getFLCO() == FLCO_GROUP);
                }
                else
                {
                    CDMRCSBK csbk;
                    _signalling_generator->createReplyUDTCRCError(csbk, srcId);
                    transmitCSBK(csbk, nullptr, dmr_data.getSlotNo(), udp_channel_id, false, false);
                    _logger->log(Logger::LogLevelWarning, QString("Invalid UDT message CRC16 from %1").arg(srcId));
                }
                _data_msg_size = 0;
            }
            _data_block -= 1;
        }
        _logger->log(Logger::LogLevelDebug, QString("DMR Slot %1, received UDT data MS to TG from %2 to %3")
                     .arg(dmr_data.getSlotNo()).arg(srcId).arg(Utils::convertBase11GroupNumberToBase10(dstId)));
        if(_short_data_messages.contains(srcId))
        {
            unsigned int num_blocks = _short_data_messages.value(srcId);
            if(num_blocks == 0)
            {
                CDMRCSBK csbk;
                _signalling_generator->createReplyMessageAccepted(csbk, dmr_data.getSrcId());
                transmitCSBK(csbk, nullptr, dmr_data.getSlotNo(), udp_channel_id, false, true);
                _short_data_messages.remove(srcId);

            }
            else
            {
                _short_data_messages[srcId] = num_blocks - 1;
            }
        }
    }
    else if(dmr_data.getFLCO() == FLCO_USER_USER)
    {
        if(dmr_data.getDataType() == DT_DATA_HEADER)
        {
            unsigned char data[DMR_FRAME_LENGTH_BYTES];
            dmr_data.getData(data);
            CDMRDataHeader header;
            header.put(data);
            if(header.getUDT())
            {
                _data_msg_size = header.getBlocks();
                _data_pad_nibble = header.getPadNibble();
                _data_block = _data_msg_size;
                _udt_format = header.getUDTFormat();
                qDebug() << "A: " << header.getA() << " GI: " << header.getGI() << " Format: " << header.getFormat()
                         << " UDTFormat: " << header.getUDTFormat() << " Opcode: " << header.getOpcode() << " RSVD: " << header.getRSVD()
                         << " PF: " << header.getPF() << " SF: " << header.getSF() << " SAP: " << header.getSAP();
            }
            else
            {
                _data_msg_size = 0;
            }

        }
        else if((dmr_data.getDataType() == DT_RATE_12_DATA) && (_data_msg_size > 0))
        {
            unsigned char block[12];
            memset(block, 0, 12U);
            unsigned char data[DMR_FRAME_LENGTH_BYTES];
            dmr_data.getData(data);
            CBPTC19696 bptc;
            bptc.decode(data, block);
            memcpy(_data_message + ((_data_msg_size - _data_block) * 12) , block, 12U);
            if(_data_block == 1)
            {
                bool valid = CCRC::checkCCITT162(_data_message, _data_msg_size*12);

                if(valid)
                {
                    /// Talkgroup attachment list
                    if(_uplink_acks->contains(srcId) &&
                            (_uplink_acks->value(srcId) == ServiceAction::RegistrationWithAttachment) &&
                            (_udt_format==1))
                    {
                        updateTalkgroupSubscriptions(srcId);
                    }
                    /// Text message
                    else if((_udt_format == 4) || (_udt_format == 3) || (_udt_format == 7))
                    {
                        processTextMessage(dstId, srcId, false);
                    }
                    /// Location query (Hytera specific)
                    else if(dstId == (unsigned int)_settings->service_ids.value("location", 2))
                    {
                        CDMRCSBK csbk;
                        _signalling_generator->createReplyMessageAccepted(csbk, srcId);
                        transmitCSBK(csbk, nullptr, _control_channel->getSlot(), _control_channel->getPhysicalChannel(), false, false);
                    }
                }
                else
                {
                    CDMRCSBK csbk;
                    _signalling_generator->createReplyUDTCRCError(csbk, srcId);
                    transmitCSBK(csbk, nullptr, dmr_data.getSlotNo(), udp_channel_id, false, false);
                    _logger->log(Logger::LogLevelWarning, QString("Invalid UDT message CRC16 from %1").arg(srcId));
                }
                _data_msg_size = 0;
            }
            _data_block -= 1;
        }
        _logger->log(Logger::LogLevelDebug, QString("DMR Slot %1, received UDT data MS to MS from %2 to %3")
                     .arg(dmr_data.getSlotNo()).arg(srcId).arg(dstId));
        if(_short_data_messages.contains(srcId))
        {
            unsigned int num_blocks = _short_data_messages.value(srcId);
            if(num_blocks == 0)
            {
                _short_data_messages.remove(srcId);
            }
            else
            {
                _short_data_messages[srcId] = num_blocks - 1;
            }
        }
    }
    /// Rewriting destination to match DMR tier III flat numbering
    if(from_gateway)
    {
        if(dmr_data.getFLCO() == FLCO_GROUP)
            dstId = Utils::convertBase10ToBase11GroupNumber(dmr_data.getDstId());
        else
            dstId = dmr_data.getDstId();
        dmr_data.setDstId(dstId);
        dmr_data.setSlotNo(_control_channel->getSlot());
        _control_channel->putRFQueue(dmr_data);
    }
    else if(!from_gateway)
    {
        if(dmr_data.getFLCO() == FLCO_GROUP)
            dstId = Utils::convertBase11GroupNumberToBase10(dmr_data.getDstId());
        else
            dstId = dmr_data.getDstId();
        dmr_data.setDstId(dstId);
        if((dmr_data.getFLCO() == FLCO_GROUP) || !_registered_ms->contains(dstId))
        {
            _dmr_rewrite->rewriteSlot(dmr_data);
            _control_channel->putNetQueue(dmr_data);
        }
    }
}

void Controller::processVoice(CDMRData& dmr_data, unsigned int udp_channel_id,
                                    bool data_sync, bool from_gateway)
{
    bool local_data = !from_gateway;
    unsigned int dstId, srcId;

    /// Rewriting destination to match DMR tier III flat numbering
    if(local_data)
    {
        if(dmr_data.getFLCO() == FLCO_GROUP)
            dstId = Utils::convertBase11GroupNumberToBase10(dmr_data.getDstId());
        else
            dstId = dmr_data.getDstId();
        srcId = dmr_data.getSrcId();
    }
    else
    {
        dstId = dmr_data.getDstId();
        srcId = dmr_data.getSrcId();
        _dmr_rewrite->rewriteSource(dmr_data);

        if(dmr_data.getFLCO() == FLCO_GROUP)
            dmr_data.setDstId(Utils::convertBase10ToBase11GroupNumber(dstId));

        // rewrite RF header to use the converted destination id
        if(data_sync)
        {
            unsigned char data[DMR_FRAME_LENGTH_BYTES];
            dmr_data.getData(data);
            // match LC with rewritten src and destination
            CDMRFullLC fullLC;
            CDMRLC lc(dmr_data.getFLCO(), dmr_data.getSrcId(), dmr_data.getDstId());
            fullLC.encode(lc, data, dmr_data.getDataType());
            CDMRSlotType slotType;
            slotType.setColorCode(1);
            slotType.setDataType(dmr_data.getDataType());
            slotType.getData(data);
            CSync::addDMRDataSync(data, true);
            dmr_data.setData(data);
        }
    }
    LogicalChannel *logical_channel;
    // First try to find an existing allocated channel with the same destination id
    logical_channel = findCallChannel(dstId, srcId);
    if(logical_channel != nullptr)
    {
        bool update_gui = false;
        if(logical_channel->getSource() != srcId)
        {
            logical_channel->setSource(srcId);
            update_gui = true;
        }
        if(logical_channel->getDestination() != dstId)
        {
            logical_channel->setDestination(dstId);
            update_gui = true;
        }
        dmr_data.setSlotNo(logical_channel->getSlot());
        logical_channel->startTimeoutTimer();
        if(update_gui && !_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
            emit updateCallLog(srcId, dstId, dmr_data.getFLCO() == FLCO_USER_USER);
        }
        if(local_data)
        {
            dmr_data.setDstId(dstId);
            _dmr_rewrite->rewriteSlot(dmr_data);
            logical_channel->putNetQueue(dmr_data);
            return;
        }
        else
        {
            if(dmr_data.getFLCO() == FLCO_GROUP)
                dmr_data.setDstId(Utils::convertBase10ToBase11GroupNumber(dstId));
            logical_channel->putRFQueue(dmr_data);
            return;
        }
    }
    if(local_data)
    {
        // local call setup needs to be made through TV_GRANT / PV_GRANT
        // we are probably here because we didn't yet get the P_CLEAR due to latency and the radio thinks
        // the GRANT is still valid
        if(dmr_data.getDataType() == DT_TERMINATOR_WITH_LC)
        {
            unsigned int call_type = (dmr_data.getFLCO() == FLCO_GROUP) ? CallType::CALL_TYPE_GROUP : CallType::CALL_TYPE_MS;
            handleLocalVoiceOnUnallocatedChannel(call_type, dmr_data.getSlotNo(), udp_channel_id);
        }
        return;
    }
    // Could not find an existing active channel
    // Next try to find a free payload channel to allocate
    logical_channel = findNextFreePayloadChannel();
    if(logical_channel == nullptr)
    {
        if(!_rejected_calls->contains(dmr_data.getStreamId()))
        {
            _rejected_calls->insert(dmr_data.getStreamId());
            _logger->log(Logger::LogLevelWarning, "Could not find any free logical channels for net voice");
            if(!_settings->headless_mode)
            {
                emit updateRejectedCallsList(srcId, dstId, false);
            }
        }
    }
    else
    {
        unsigned int call_type = (dmr_data.getFLCO() == FLCO_USER_USER) ? CallType::CALL_TYPE_MS : CallType::CALL_TYPE_GROUP;
        CDMRCSBK csbk_grant;
        bool channel_grant = true;
        bool priority = false;
        if(call_type == CallType::CALL_TYPE_MS)
        {
            if(_registered_ms->contains(dstId) && !_private_calls.contains(dstId))
            {
                channel_grant = false;
                contactMSForCall(csbk_grant, dmr_data.getSlotNo(), srcId, dstId, false);
                transmitCSBK(csbk_grant, logical_channel, _control_channel->getSlot(),
                                 _control_channel->getPhysicalChannel(), false, priority, true);
                return;
            }
            //handlePrivateCallRequest(csbk_grant, logical_channel, dmr_data.getSlotNo(), srcId, dstId, channel_grant, false);
        }
        else
        {
            handleGroupCallRequest(csbk_grant, logical_channel, dmr_data.getSlotNo(), srcId, dstId, channel_grant, false);

            CDMRCSBK csbk2;
            bool valid = false;
            if(csbk_grant.getCSBKO() == CSBKO_TV_GRANT)
                valid = _signalling_generator->createAbsoluteParameters(csbk_grant, csbk2, logical_channel);
            if(valid)
            {
                transmitCSBK(csbk_grant, logical_channel, _control_channel->getSlot(),
                                 _control_channel->getPhysicalChannel(), false, false, true);
                transmitCSBK(csbk2, logical_channel, _control_channel->getSlot(), _control_channel->getPhysicalChannel(),
                                 false, false, true);
            }
            else
            {
                transmitCSBK(csbk_grant, logical_channel, _control_channel->getSlot(),
                                 _control_channel->getPhysicalChannel(), false, priority, true);
            }
            dmr_data.setSlotNo(logical_channel->getSlot());
            logical_channel->putRFQueue(dmr_data);
            return;
        }

    }
}

bool Controller::handleRegistration(CDMRCSBK &csbk, unsigned int slotNo,
                                    unsigned int srcId, unsigned int dstId,
                                    unsigned int &uab)
{
    bool sub = false;
    if((csbk.getServiceOptions() & 0x01) == 1)
    {
        unsigned int target_addr_cnts = (csbk.getCBF() << 6) & 0x03;
        uab = (csbk.getCBF() << 4) & 0x03;
        if(target_addr_cnts == 1)
        {
            _logger->log(Logger::LogLevelInfo, QString("DMR Slot %1, received registration request from %2 with attachement to TG %3")
                         .arg(slotNo).arg(srcId).arg(dstId));
            _signalling_generator->createReplyRegistrationAccepted(csbk, srcId);
            if(!_registered_ms->contains(srcId))
                _registered_ms->append(srcId);
        }
        else if((target_addr_cnts == 0) && 0) // FIXME: this doesn't work on Hytera??
        {
            _logger->log(Logger::LogLevelInfo, QString("DMR Slot %1, received registration request from %2 to syscode %3")
                         .arg(slotNo).arg(srcId).arg(dstId));
            unsigned int system_code = _settings->system_identity_code << 2;
            system_code |= 3; // TODO: PAR
            if(dstId == (system_code & 0xFFFFFF))
            {
                _signalling_generator->createReplyRegistrationAccepted(csbk, srcId);
                if(!_registered_ms->contains(srcId))
                    _registered_ms->append(srcId);
            }
        }
        else if((target_addr_cnts == 2) || (target_addr_cnts == 0)) // Hytera bug? TG attachment list request should set this to 2
        {
            _logger->log(Logger::LogLevelInfo, QString("DMR Slot %1, received registration request from %2 with TG subscription list")
                         .arg(slotNo).arg(srcId));
            _signalling_generator->createReplyRegistrationAccepted(csbk, srcId);
            sub = (bool)_settings->receive_tg_attach;
            if(!sub)
            {
                if(!_registered_ms->contains(srcId))
                    _registered_ms->append(srcId);
            }
        }
    }
    else if((csbk.getServiceOptions() & 0x01) == 0)
    {
        _signalling_generator->createReplyDeregistrationAccepted(csbk, srcId);
        _logger->log(Logger::LogLevelInfo, QString("DMR Slot %1, received de-registration request from %2 to TG %3")
                     .arg(slotNo).arg(srcId).arg(dstId));
        _registered_ms->removeAll(srcId);
        _talkgroup_attachments->remove(srcId);
        _subscribed_talkgroups->clear();
        QMapIterator<unsigned int, QList<unsigned int>> it(*_talkgroup_attachments);
        while(it.hasNext())
        {
            it.next();
            _subscribed_talkgroups->unite(QSet<unsigned int> (it.value().begin(), it.value().end()));
        }
        if(!_settings->headless_mode)
        {
            emit updateTalkgroupSubscriptionList(_subscribed_talkgroups);
        }
    }
    if(!_settings->headless_mode)
    {
        emit updateRegisteredMSList(_registered_ms);
    }
    return sub;
}

void Controller::contactMSForCall(CDMRCSBK &csbk, unsigned int slotNo,
                            unsigned int srcId, unsigned int dstId, bool local)
{
    _logger->log(Logger::LogLevelInfo, QString("TSCC: DMR Slot %1, received private call request from %2 to TG %3")
                 .arg(slotNo).arg(srcId).arg(dstId));
    if(!_private_calls.contains(dstId))
        _private_calls.insert(dstId, srcId);
    _uplink_acks->insert(dstId, ServiceAction::ActionPrivateCallRequest);
    _signalling_generator->createPrivateCallRequest(csbk, local, srcId, dstId);
    return;
}

void Controller::handlePrivateCallRequest(CDMRCSBK &csbk, LogicalChannel *&logical_channel, unsigned int slotNo,
                            unsigned int srcId, unsigned int dstId, bool &channel_grant, bool local)
{
    _logger->log(Logger::LogLevelInfo, QString("TSCC: DMR Slot %1, received private call request from %2 to destination %3")
                 .arg(slotNo).arg(srcId).arg(dstId));

    if(local)
    {
        unsigned int temp = srcId;
        srcId = dstId;
        dstId = temp;
    }
    logical_channel = findCallChannel(dstId, srcId);
    if(logical_channel != nullptr)
    {
        _signalling_generator->createPrivateVoiceGrant(csbk, logical_channel, srcId, dstId);
        if(srcId == logical_channel->getSource())
            channel_grant = true;
        logical_channel->setDestination(dstId);
        logical_channel->setSource(srcId);
        logical_channel->startTimeoutTimer();
        return;
    }

    // Next try to find a free payload channel to allocate
    logical_channel = findNextFreePayloadChannel();
    if(logical_channel == nullptr)
    {
        _logger->log(Logger::LogLevelWarning, "Could not find any free logical channels, telling MS to wait");
        _signalling_generator->createReplyCallDenied(csbk, srcId);
        if(!_settings->headless_mode)
        {
            emit updateRejectedCallsList(srcId, dstId, local);
        }
        return;
    }
    else
    {
        _signalling_generator->createPrivateVoiceGrant(csbk, logical_channel, srcId, dstId);
        channel_grant = true;
        logical_channel->allocateChannel(srcId, dstId);
        logical_channel->setCallType(CallType::CALL_TYPE_MS);
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
            emit updateCallLog(srcId, dstId, true);
        }

        return;
    }
}

void Controller::handleGroupCallRequest(CDMRCSBK &csbk, LogicalChannel *&logical_channel, unsigned int slotNo,
                                        unsigned int srcId, unsigned int dstId, bool &channel_grant, bool local)
{
    unsigned int dmrDstId = (local) ? Utils::convertBase11GroupNumberToBase10(dstId) : dstId;
    _logger->log(Logger::LogLevelInfo, QString("TSCC: DMR Slot %1, received group call request from %2 to TG %3")
                 .arg(slotNo).arg(srcId).arg(dmrDstId));

    // First try to find an existing busy channel with the same TG

    logical_channel = findCallChannel(dmrDstId, srcId);
    if(logical_channel != nullptr)
    {
        _signalling_generator->createGroupVoiceGrant(csbk, logical_channel, srcId, dstId);
        if(srcId == logical_channel->getSource())
            channel_grant = true;
        logical_channel->setDestination(dmrDstId);
        logical_channel->setSource(srcId);
        logical_channel->startTimeoutTimer();
        return;
    }

    // Next try to find a free payload channel to allocate
    logical_channel = findNextFreePayloadChannel();
    if(logical_channel == nullptr)
    {
        _logger->log(Logger::LogLevelWarning, "Could not find any free logical channels, telling MS to wait");
        _signalling_generator->createReplyCallDenied(csbk, srcId);
        if(!_settings->headless_mode)
        {
            emit updateRejectedCallsList(srcId, dstId, local);
        }
        return;
    }
    else
    {
        _signalling_generator->createGroupVoiceGrant(csbk, logical_channel, srcId, dstId);
        channel_grant = true;
        logical_channel->allocateChannel(srcId, dmrDstId);
        logical_channel->setCallType(CallType::CALL_TYPE_GROUP);
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
            emit updateCallLog(srcId, dmrDstId, false);
        }
        return;
    }
}

void Controller::handleCallDisconnect(int udp_channel_id, bool group_call,
                                      unsigned int &srcId, unsigned int &dstId, unsigned int slotNo,
                                      LogicalChannel *&logical_channel, CDMRCSBK &csbk)
{
    logical_channel = findChannelByPhysicalIdAndSlot(udp_channel_id, slotNo);
    if(logical_channel == nullptr)
    {
        _logger->log(Logger::LogLevelDebug, QString("Could not find active physical channel %1, with slot %2")
                     .arg(udp_channel_id).arg(slotNo));
        return;
    }
    else
    {
        _logger->log(Logger::LogLevelInfo, QString("Channel %1 Slot %2, received call disconnect from %3 to destination %4")
                     .arg(logical_channel->getPhysicalChannel()).arg(slotNo).arg(srcId).arg(dstId));
        _signalling_generator->createClearChannelUserInitiated(csbk, logical_channel, dstId, group_call);
        if(logical_channel->getBusy() && !logical_channel->getDisabled())
        {
            logical_channel->deallocateChannel();
            updateLogicalChannels(&_logical_channels);
        }
    }

}

void Controller::handleIdleChannelDeallocation(unsigned int channel_id)
{
    if(_logical_channels.at(channel_id)->getCallType() == CallType::CALL_TYPE_MS)
    {
        if(_private_calls.contains(_logical_channels.at(channel_id)->getDestination()))
            _private_calls.remove(_logical_channels.at(channel_id)->getDestination());
        if(_private_calls.contains(_logical_channels.at(channel_id)->getSource()))
            _private_calls.remove(_logical_channels.at(channel_id)->getSource());
    }
    unsigned int call_type = _logical_channels.at(channel_id)->getCallType();
    CDMRCSBK csbk;
    _signalling_generator->createChannelIdleDeallocation(csbk, call_type);

    transmitCSBK(csbk, _logical_channels[channel_id], _logical_channels[channel_id]->getSlot(),
                     _logical_channels[channel_id]->getPhysicalChannel(), false);
    transmitCSBK(csbk, _logical_channels[channel_id], _logical_channels[channel_id]->getSlot(),
                     _logical_channels[channel_id]->getPhysicalChannel(), false);
    transmitCSBK(csbk, _logical_channels[channel_id], _logical_channels[channel_id]->getSlot(),
                     _logical_channels[channel_id]->getPhysicalChannel(), false);
    if(!_settings->headless_mode)
    {
        emit updateLogicalChannels(&_logical_channels);
    }
}

void Controller::handleLocalVoiceOnUnallocatedChannel(unsigned int call_type, unsigned int slotNo, unsigned int udp_channel_id)
{
    CDMRCSBK csbk;
    _signalling_generator->createClearChannelAll(csbk, call_type);
    transmitCSBK(csbk, nullptr, slotNo, udp_channel_id, false);
}

void Controller::processSignalling(CDMRData &dmr_data, int udp_channel_id)
{
    unsigned int srcId = dmr_data.getSrcId();
    unsigned int dstId = dmr_data.getDstId();
    unsigned int slotNo = dmr_data.getSlotNo();
    CDMRCSBK csbk;
    unsigned char buf[DMR_FRAME_LENGTH_BYTES];
    dmr_data.getData(buf);
    bool valid = csbk.put(buf);
    if (!valid)
    {
        _logger->log(Logger::LogLevelDebug, QString("Received invalid CSBK from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));
        return;
    }

    CSBKO csbko = csbk.getCSBKO();
    if (csbko == CSBKO_BSDWNACT)
        return;
    bool channel_grant = false;
    LogicalChannel *logical_channel = nullptr; // for TV_GRANT and PV_GRANT
    bool group_call = dmr_data.getFLCO() == FLCO_GROUP;

    /// Registration or deregistration request
    if (csbko == CSBKO_RAND && csbk.getServiceKind() == ServiceKind::RegiAuthMSCheck)
    {
        bool existing_user = _registered_ms->contains(srcId);
        unsigned int uab = 0;
        bool sub = handleRegistration(csbk, slotNo, srcId, dstId, uab);
        if(sub)
        {
            // FIXME: order is wrong
            _uplink_acks->insert(srcId, ServiceAction::RegistrationWithAttachment);
            _signalling_generator->createRequestToUploadTgAttachments(csbk, srcId, uab);
            _short_data_messages.insert(srcId, uab);
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
        }
        else
        {
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, true);
            if(!existing_user && _settings->announce_system_message)
            {
                QString message1 = QString("Welcome %1, there are %2 users online").arg(_id_lookup->getCallsign(srcId)).arg(_registered_ms->size());
                sendUDTShortMessage(message1, srcId);
            }
        }

    }
    /// Service requested while not registered
    else if(!validateLocalSourceId(srcId))
    {
        _signalling_generator->createReplyNotRegistered(csbk, srcId);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
        _logger->log(Logger::LogLevelInfo, QString("Received service request while not registered from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));
    }
    ///
    /// All below signaling needs the MS to be registered
    ///
    /// MS ping reply
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x88) &&
             _uplink_acks->contains(srcId) &&
             _uplink_acks->value(srcId) == ServiceAction::ActionPingRequest)
    {
        std::chrono::high_resolution_clock::time_point t2_ping_ms = std::chrono::high_resolution_clock::now();
        uint64_t msec = std::chrono::duration_cast<std::chrono::nanoseconds>(t2_ping_ms - t1_ping_ms).count() / 1000000U;
        _uplink_acks->remove(srcId);
        emit pingResponse(srcId, msec);

    }
    /// Group call request
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::GroupVoiceCall)
             && !csbk.getSuplimentaryData())
    {
        bool broadcast_call = false;
        if(csbk.getBroadcast())
            broadcast_call = true;
        handleGroupCallRequest(csbk, logical_channel, slotNo, srcId, dstId, channel_grant, true);
        if(broadcast_call)
            csbk.setCSBKO(CSBKO_BTV_GRANT);
        CDMRCSBK csbk2;
        bool valid = false;
        if((csbk.getCSBKO() == CSBKO_TV_GRANT) || (csbk.getCSBKO() == CSBKO_BTV_GRANT))
            valid = _signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);
        if(valid)
        {
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
            transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        }
        else
        {
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        }
    }
    /// Group call with suplimentary data
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::GroupVoiceCall) && csbk.getSuplimentaryData())
    {
        _signalling_generator->createRequestToSendGroupCallSupplimentaryData(csbk, csbk.getSrcId());
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
    }
    /// Direct MS to MS call request
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::IndivVoiceCall))
    {
        if(_registered_ms->contains(dstId))
        {
            /** FIXME: The standard call procedure does not include a wait notification
            CDMRCSBK csbk_wait;
            _signalling_generator->createReplyWaitForSignalling(csbk_wait, csbk.getSrcId());
            transmitCSBK(csbk_wait, logical_channel, slotNo, udp_channel_id, channel_grant, false);
            */
            contactMSForCall(csbk, slotNo, srcId, dstId, true);
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        }
        else
        {
            handlePrivateCallRequest(csbk, logical_channel, slotNo, srcId, dstId, channel_grant, false);

            CDMRCSBK csbk2;
            bool valid = false;
            if(csbk.getCSBKO() == CSBKO_PV_GRANT)
                valid = _signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);
            if(valid)
            {
                transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
                transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, channel_grant, false);
            }
            else
            {
                transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
            }
        }
    }
    /// MS acknowledgement of OACSU call
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x88) &&
             _uplink_acks->contains(srcId) &&
             _uplink_acks->value(srcId) == ServiceAction::ActionPrivateCallRequest)
    {

        if(_private_calls.contains(srcId))
            _private_calls.remove(srcId);
        _uplink_acks->remove(srcId);
        csbk.setCSBKO(CSBKO_ACKD);
        csbk.setDstId(dstId);
        csbk.setSrcId(srcId);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        handlePrivateCallRequest(csbk, logical_channel, slotNo, srcId, dstId, channel_grant, true);

        CDMRCSBK csbk2;
        bool valid = false;
        if(csbk.getCSBKO() == CSBKO_PV_GRANT)
            valid = _signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);
        if(valid)
        {
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
            transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        }
        else
        {
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        }
        csbk.setDstId(srcId);
        csbk.setSrcId(dstId);

        if(csbk.getCSBKO() == CSBKO_PV_GRANT)
            valid = _signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);
        if(valid)
        {
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
            transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        }
        else
        {
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        }
        _logger->log(Logger::LogLevelInfo, QString("Received acknowledgement for OACSU call from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));
    }
    /// MS acknowledgement of FOACSU call
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x8C))
    {
        csbk.setCSBKO(CSBKO_ACKD);
        csbk.setDstId(dstId);
        csbk.setSrcId(srcId);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        _logger->log(Logger::LogLevelDebug, QString("Received acknowledgement for FOACSU call from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));
    }
    /// MS FOACSU call answer
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::CallAnswer) && ((csbk.getCBF() & 0xF0) == 0))
    {
        if(_private_calls.contains(srcId))
            _private_calls.remove(srcId);
        handlePrivateCallRequest(csbk, logical_channel, slotNo, srcId, dstId, channel_grant, true);

        CDMRCSBK csbk2;
        bool valid = false;
        if(csbk.getCSBKO() == CSBKO_PV_GRANT)
            valid = _signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);
        if(valid)
        {
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
            transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        }
        else
        {
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        }
        _logger->log(Logger::LogLevelInfo, QString("Received radio FOACSU call answer from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));
    }
    /// call reject
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::CallAnswer) && ((csbk.getCBF() & 0xF0) == 0x20))
    {
        if(_private_calls.contains(srcId))
            _private_calls.remove(srcId);
        _signalling_generator->createReplyCallRejected(csbk, srcId, dstId);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
    }
    /// cancel private call
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::CancelCall) && (csbk.getDstId() > 0))
    {
        if(_private_calls.contains(srcId))
            _private_calls.remove(srcId);
        _signalling_generator->createCancelPrivateCallAhoy(csbk, csbk.getDstId());
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
    }
    /// cancel call
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::CancelCall) && (csbk.getDstId() == 0))
    {
        if(_private_calls.contains(srcId))
            _private_calls.remove(srcId);
        handleCallDisconnect(udp_channel_id, group_call, srcId, dstId, slotNo, logical_channel, csbk);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
    }
    /// Call disconnect
    else if ((csbko == CSBKO_MAINT) && (csbk.getServiceKind() == ServiceKind::IndivVoiceCall))
    {
        handleCallDisconnect(udp_channel_id, group_call, srcId, dstId, slotNo, logical_channel, csbk);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        CDMRCSBK csbk_receiver;
        _signalling_generator->createCallDisconnect(csbk_receiver, srcId, group_call);
        transmitCSBK(csbk_receiver, logical_channel, slotNo, udp_channel_id, channel_grant, false);
    }
    /// MS acknowledgement of short data message
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x88) &&
             _uplink_acks->contains(srcId) &&
             _uplink_acks->value(srcId) == ServiceAction::ActionMessageRequest)
    {
        _uplink_acks->remove(srcId);
        csbk.setCSBKO(CSBKO_ACKD);
        csbk.setDstId(dstId);
        csbk.setSrcId(srcId);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        _logger->log(Logger::LogLevelInfo, QString("Received read receipt for message request from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));
    }
    /// Short data service MS to MS
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::IndivUDTDataCall))
    {
        _uplink_acks->insert(dstId, ServiceAction::ActionMessageRequest);
        unsigned int number_of_blocks = _signalling_generator->createRequestToUploadMessage(csbk, csbk.getSrcId());
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
        _short_data_messages.insert(srcId, number_of_blocks);
        _logger->log(Logger::LogLevelInfo, QString("Received private short data message request from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));
    }
    /// Short data service MS to TG
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::GroupUDTDataCall))
    {
        unsigned int number_of_blocks = _signalling_generator->createRequestToUploadMessage(csbk, csbk.getSrcId());
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
        _short_data_messages.insert(srcId, number_of_blocks);
        _logger->log(Logger::LogLevelInfo, QString("Received group short data message request to TG from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));
    }
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x88))
    {
        _logger->log(Logger::LogLevelDebug, QString("Received unhandled ACKU from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));
    }
    /// Not implemeted yet
    else
    {
        qDebug() << "CSBKO: " << QString::number(csbk.getCSBKO(), 16) <<
                    " FID " << csbk.getFID() <<
                    " data1: " << csbk.getData1() << " data2: " << csbk.getCBF() <<
                    " dst: " << dstId << " src: " << srcId;
        _logger->log(Logger::LogLevelDebug, "Unhandled CSBK type");
        return;
    }

}

void Controller::processNetworkCSBK(CDMRData &dmr_data, int udp_channel_id)
{
    (void)udp_channel_id;
    _logger->log(Logger::LogLevelDebug, QString("Received network CSBK from %1, slot %2 to destination %3")
                 .arg(dmr_data.getSrcId()).arg(dmr_data.getSlotNo()).arg(dmr_data.getDstId()));
}

void Controller::transmitCSBK(CDMRCSBK &csbk, LogicalChannel *logical_channel, unsigned int slotNo,
                                  unsigned int udp_channel_id, bool channel_grant, bool priority_queue, bool announce_priority)
{
    CDMRData dmr_data_wait;
    if(channel_grant)
    {
        CDMRCSBK csbk_wait;
        _signalling_generator->createReplyWaitForSignalling(csbk_wait, csbk.getDstId());
        dmr_data_wait.setSeqNo(0);
        dmr_data_wait.setN(0);
        dmr_data_wait.setDataType(DT_CSBK);
        dmr_data_wait.setSlotNo(slotNo);

        unsigned char repacked_data_wait[DMR_FRAME_LENGTH_BYTES];
        // Regenerate the CSBK data
        csbk_wait.get(repacked_data_wait);
        CDMRSlotType slotType;
        slotType.setColorCode(1);
        slotType.setDataType(DT_CSBK);
        slotType.getData(repacked_data_wait);

        CSync::addDMRDataSync(repacked_data_wait, 1);
        dmr_data_wait.setDstId(csbk_wait.getDstId());
        dmr_data_wait.setSrcId(csbk_wait.getSrcId());
        dmr_data_wait.setData(repacked_data_wait);
    }
    unsigned char dataType = csbk.getDataType();
    CDMRData dmr_data;
    dmr_data.setSeqNo(0);
    dmr_data.setN(0);
    dmr_data.setDataType(dataType);
    dmr_data.setSlotNo(slotNo);

    unsigned char repacked_data[DMR_FRAME_LENGTH_BYTES];
    // Regenerate the CSBK data
    csbk.get(repacked_data);
    CDMRSlotType slotType;
    slotType.putData(repacked_data);
    slotType.setColorCode(1);
    slotType.setDataType(dataType);
    slotType.getData(repacked_data);

    CSync::addDMRDataSync(repacked_data, 1);
    dmr_data.setDstId(csbk.getDstId());
    dmr_data.setSrcId(csbk.getSrcId());
    dmr_data.setData(repacked_data);
    LogicalChannel *main_channel = findChannelByPhysicalIdAndSlot(udp_channel_id, slotNo);

    if (channel_grant)
    {
        // Wake up payload channel, allow time for the chain latency
        CDMRData payload_channel_data = dmr_data;
        payload_channel_data.setSlotNo(logical_channel->getSlot());
        logical_channel->putRFQueue(payload_channel_data);
        main_channel->putRFQueue(dmr_data, priority_queue);
        for(int i=0; i < 4;i++)
        {
            logical_channel->putRFQueue(payload_channel_data);
        }
        if(_settings->announce_priority)
        {
            QVector<LogicalChannel*> active_channels = findActiveChannels();
            for(int i=0;i<active_channels.size();i++)
            {
                CDMRData announce_data = payload_channel_data;
                announce_data.setSlotNo(active_channels[i]->getSlot());
                active_channels[i]->putRFQueue(announce_data);
            }
        }

    }
    else
    {
        main_channel->putRFQueue(dmr_data, priority_queue);
        if(announce_priority && _settings->announce_priority)
        {
            QVector<LogicalChannel*> active_channels = findActiveChannels();
            for(int i=0;i<active_channels.size();i++)
            {
                CDMRData announce_data = dmr_data;
                announce_data.setSlotNo(active_channels[i]->getSlot());
                active_channels[i]->putRFQueue(announce_data);
            }
        }
    }

}

void Controller::updateMMDVMConfig(unsigned char* payload, int size)
{
    if(_mmdvm_config->size() > 1)
    {
        delete[] payload;
        return;
    }

    int configLen = size - 8;

    unsigned char *start_conf = payload + 8U;
    for(int i=0; i<configLen; i++)
    {
        _mmdvm_config->push_back(start_conf[i]);
    }

    _logger->log(Logger::LogLevelDebug, "Updated Gateway config");
    delete[] payload;
}

void Controller::writeDMRConfig()
{
    if(_mmdvm_config->size() < 8)
        return;
    QVector<unsigned char> config;
    for(int i = 0; i< _mmdvm_config->size(); i++)
    {
        config.push_back(_mmdvm_config->at(i));
    }
    for(int i=0;i<_settings->gateway_number;i++)
    {
        _gateway_channels[i]->writeDMRConfig(config);
    }
}

QVector<LogicalChannel *> *Controller::getLogicalChannels()
{
    return &_logical_channels;
}

void Controller::setChannelEnabled(unsigned int index, bool state)
{
    if(index < (unsigned int)_logical_channels.size())
    {
        _logical_channels[index]->setDisabled(!state);
    }
    if(!_settings->headless_mode)
    {
        emit updateLogicalChannels(&_logical_channels);
    }
}

bool Controller::validateLocalSourceId(unsigned int srcId)
{
    if(_registered_ms->contains(srcId))
        return true;
    _logger->log(Logger::LogLevelWarning, QString("Rejected local id %1, not registered with the site").arg(srcId));
    return false;
}


