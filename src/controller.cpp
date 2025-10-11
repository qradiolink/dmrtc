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

const unsigned int HOMEBREW_DATA_PACKET_LENGTH = 55U;

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
    _ack_handler = new AckHandler;
    _auth_responses = new QMap<unsigned int, unsigned int>;
    _dmr_rewrite = new DMRRewrite(settings, _registered_ms);
    _gateway_router = new GatewayRouter(_settings, _logger);
    _signalling_generator = new Signalling(_settings);
    _network_signalling = new NetworkSignalling(settings, logger);
    _dmr_message_handler = new DMRMessageHandler(settings, logger);
    _stop_thread = false;
    _late_entry_announcing = false;
    _system_freqs_announcing = false;
    _adjacent_sites_announcing = false;
    t1_ping_ms = std::chrono::high_resolution_clock::now();
    _startup_completed = false;
    _minute = 1;
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
    _subscribed_talkgroups->clear();
    delete _subscribed_talkgroups;
    _auth_responses->clear();
    delete _auth_responses;
    delete _dmr_rewrite;
    delete _gateway_router;
    delete _signalling_generator;
    delete _network_signalling;
    delete _dmr_message_handler;
    delete _ack_handler;
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
    QTimer announce_adjacent_sites_timer;
    QTimer auth_timer;
    QTimer ping_radio_timer;
    auth_timer.setSingleShot(true);
    auth_timer.setInterval(3000);
    QObject::connect(this, SIGNAL(startAuthTimer()), &auth_timer, SLOT(start()));
    QObject::connect(this, SIGNAL(stopAuthTimer()), &auth_timer, SLOT(stop()));
    QObject::connect(&auth_timer, SIGNAL(timeout()), this, SLOT(resetAuth()));
    ping_radio_timer.setSingleShot(true);
    QObject::connect(&ping_radio_timer, SIGNAL(timeout()), this, SLOT(timeoutPingResponse()));
    QObject::connect(this, SIGNAL(stopPingTimer()), &ping_radio_timer, SLOT(stop()));
    QObject::connect(this, SIGNAL(startPingTimer(int)), &ping_radio_timer, SLOT(start(int)));
    for(int i=0; i<_settings->channel_number; i++)
    {

        if(i == _settings->control_channel_physical_id)
        {
            _control_channel = new LogicalChannel(_settings, _logger, counter, i, 1, true);
            counter++;
            _logical_channels.append(_control_channel);
            if(_settings->control_channel_slot == 1)
            {
                LogicalChannel *payload_channel = new LogicalChannel(_settings, _logger, counter, i, 2, false, !_settings->headless_mode);
                counter++;
                QObject::connect(payload_channel, SIGNAL(channelDeallocated(unsigned int)), this, SLOT(handleIdleChannelDeallocation(unsigned int)));
                QObject::connect(payload_channel, SIGNAL(update()), this, SLOT(updateChannelsToGUI()));
                QObject::connect(payload_channel, SIGNAL(setCallStats(uint,uint,float,float,bool)),
                                 this, SLOT(setCallStats(uint,uint,float,float,bool)));
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
            LogicalChannel *payload_channel1 = new LogicalChannel(_settings, _logger, counter, i, 1, false, !_settings->headless_mode);
            counter++;
            LogicalChannel *payload_channel2 = new LogicalChannel(_settings, _logger, counter, i, 2, false, !_settings->headless_mode);
            counter++;
            QObject::connect(payload_channel1, SIGNAL(channelDeallocated(unsigned int)), this, SLOT(handleIdleChannelDeallocation(unsigned int)));
            QObject::connect(payload_channel2, SIGNAL(channelDeallocated(unsigned int)), this, SLOT(handleIdleChannelDeallocation(unsigned int)));
            QObject::connect(payload_channel1, SIGNAL(update()), this, SLOT(updateChannelsToGUI()));
            QObject::connect(payload_channel2, SIGNAL(update()), this, SLOT(updateChannelsToGUI()));
            QObject::connect(payload_channel1, SIGNAL(setCallStats(uint,uint,float,float,bool)),
                             this, SLOT(setCallStats(uint,uint,float,float,bool)));
            QObject::connect(payload_channel2, SIGNAL(setCallStats(uint,uint,float,float,bool)),
                             this, SLOT(setCallStats(uint,uint,float,float,bool)));
            _logical_channels.append(payload_channel1);
            _logical_channels.append(payload_channel2);
        }
    }
    for(int i=0; i<_logical_channels.size(); i++)
    {
        if(!_logical_channels[i]->isControlChannel())
        {
            int state = (_settings->channel_disable_bitmask >> i) & 1;
            _logical_channels[i]->setDisabled((bool)state);
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
        QObject::connect(client, SIGNAL(dmrData(unsigned char*, unsigned int, int, bool)), this, SLOT(processDMRPayload(unsigned char*, unsigned int, int, bool)), Qt::DirectConnection);
        QObject::connect(client, SIGNAL(newMMDVMConfig(unsigned char*,int)),
                         this, SLOT(updateMMDVMConfig(unsigned char*,int)), Qt::DirectConnection);

        // Disable all timeslots at startup
        CDMRData control1;
        control1.setControl(true);
        control1.setSlotNo(1);
        control1.setCommand(DMRCommand::ChannelEnableDisable);
        control1.setChannelEnable(false);
        CDMRData control2 = control1;
        control2.setSlotNo(2);
        client->writeDMRTrunkingParams(control1);
        client->writeDMRTrunkingParams(control2);

    }
    for(int i=0;i<_settings->gateway_number;i++)
    {
        UDPClient *gateway_udpclient = new UDPClient(_settings, _logger, i, _settings->gateway_listen_port + i, _settings->gateway_send_port + i,
                                                          _settings->gateway_remote_address, true);

        QObject::connect(gateway_udpclient, SIGNAL(dmrData(unsigned char*, unsigned int, int, bool)),
                         this, SLOT(processDMRPayload(unsigned char*,unsigned int,int, bool)));
        QObject::connect(gateway_udpclient, SIGNAL(newDMRNetworkMessage(unsigned char*, unsigned int)),
                         this, SLOT(processDMRNetworkMessage(unsigned char*,unsigned int)));
        QObject::connect(this, SIGNAL(writeDMRData(CDMRData&)),
                         gateway_udpclient, SLOT(writeDMRData(CDMRData&)));
        _gateway_channels.append(gateway_udpclient);
    }
    if(_gateway_channels.size() < 1)
    {
        _logger->log(Logger::LogLevelWarning, QString("No DMR Gateways configured"));
    }

    if(_settings->gateway_enabled)
    {
        for(int i=0;i<_gateway_channels.size();i++)
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
    announce_adjacent_sites_timer.setInterval(_settings->announce_adjacent_bs_interval * 1000);
    announce_adjacent_sites_timer.setSingleShot(true);
    announce_adjacent_sites_timer.start();


    /// Main thread loop where most things happen
    while(!_stop_thread)
    {
        if(!_startup_completed && _settings->registration_required)
        {
            requestMassRegistration();
        }
        QtConcurrent::run(this, &Controller::announceLateEntry);
        if(!announce_system_freqs_timer.isActive())
        {
            QtConcurrent::run(this, &Controller::announceSystemFreqs);
            announce_system_freqs_timer.start();
        }
        if(!announce_adjacent_sites_timer.isActive())
        {
            QtConcurrent::run(this, &Controller::announceAdjacentSites);
            announce_adjacent_sites_timer.start();
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
                if(dmr_data.getControl())
                {
                    _udp_channels.at(_logical_channels.at(i)->getPhysicalChannel())->writeDMRTrunkingParams(dmr_data);
                }
                else
                {
                    _udp_channels.at(_logical_channels.at(i)->getPhysicalChannel())->writeDMRData(dmr_data);
                }
            }

            /// Data going towards net
            CDMRData dmr_data_net;
            if(_logical_channels.at(i)->getNetQueue(dmr_data_net))
            {
                unsigned int gateway_id = 0;
                bool route_found = _gateway_router->findRoute(dmr_data_net, gateway_id);
                if((gateway_id + 1 <= (unsigned int)_gateway_channels.size()) && _settings->gateway_enabled)
                {
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

        }

        QThread::usleep(1000);
    }

    /// Thread stopping
    ///
    ///


    for(int i=0; i < _logical_channels.size(); i++)
    {
        _logical_channels.at(i)->stopTimeoutTimer();
    }
    for(int i=0; i < _udp_channels.size(); i++)
    {
        _udp_channels.at(i)->enable(false);
    }
    for(int i=0;i<_gateway_channels.size();i++)
    {
        _gateway_channels.at(i)->enable(false);
    }
    for(int i=0; i<_udp_channels.size(); i++)
    {
        delete _udp_channels.at(i);
    }
    for(int i=0; i<_logical_channels.size(); i++)
    {
        delete _logical_channels[i];
    }
    for(int i=0;i<_gateway_channels.size();i++)
    {
        delete _gateway_channels[i];
    }
    gateway_timer.stop();
    announce_system_freqs_timer.stop();
    emit finished();
}

void Controller::updateChannelsToGUI()
{
    if(!_settings->headless_mode)
    {
        emit updateLogicalChannels(&_logical_channels);
    }
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
        if(_logical_channels.at(i)->getBusy() && !(_logical_channels.at(i)->getDisabled())
                && !(_logical_channels.at(i)->isControlChannel())
                && (_logical_channels.at(i)->getDestination() != 0)
                && (_logical_channels.at(i)->getCallType() < CallType::CALL_TYPE_INDIV_PACKET)) // No late entry for packet data
        {
            LogicalChannel *logical_channel = _logical_channels.at(i);
            CDMRCSBK csbk;
            _signalling_generator->createLateEntryAnnouncement(logical_channel, csbk);
            CDMRCSBK csbk2;
            bool valid = _signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);
            transmitCSBK(csbk, logical_channel, _control_channel->getSlot(), _control_channel->getPhysicalChannel(), false);
            if(valid)
            {
                transmitCSBK(csbk2, logical_channel, _control_channel->getSlot(), _control_channel->getPhysicalChannel(), false, false);
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

void Controller::announceAdjacentSites()
{
    if(_adjacent_sites_announcing)
        return;
    _adjacent_sites_announcing = true;
    if(_stop_thread)
        return;
    _logger->log(Logger::LogLevelInfo, QString("Announcing adjacent sites: %1")
                 .arg(_settings->adjacent_sites.size()));
    for(int i = 0;i<_settings->adjacent_sites.size(); i++)
    {
        if(_settings->adjacent_sites[i].size() < 5)
            continue;
        QMap<QString, uint64_t> site = _settings->adjacent_sites[i];
        CDMRCSBK csbk;
        _signalling_generator->createAdjacentSiteAnnouncement(csbk, site);
        transmitCSBK(csbk, nullptr, _control_channel->getSlot(), _control_channel->getPhysicalChannel(), false);
        if(_stop_thread)
            return;
    }

    _adjacent_sites_announcing = false;
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
    QVector<LogicalChannel*> active_calls = findActiveChannels();
    QList<QString> messages;
    messages.append(QString("%1").arg(_settings->system_announcement_message));
    messages.append(QString("IP connection - %1").arg(_settings->gateway_enabled ? "yes" : "no"));
    messages.append(QString("Users - %1, channels - %2, active calls - %3")
            .arg(_registered_ms->size())
            .arg(_settings->logical_physical_channels.size())
            .arg(active_calls.size()));
    messages.append(QString("Send SMS to %1 for command help")
            .arg(_settings->service_ids.value("help", 0)));
    unsigned int dstId = StandardAddreses::ALLMSID;
    QtConcurrent::run(this, &Controller::sendUDTMultipartMessage, messages, dstId, StandardAddreses::DISPATI, false, 0);
}


void Controller::buildUDTShortMessageSequence(unsigned int srcId, unsigned int dstId, QString message,
                                              bool group)
{
    dstId = group ? Utils::convertBase10ToBase11GroupNumber(dstId) : dstId;
    unsigned int slot_no = _control_channel->getSlot();
    QVector<CDMRData> dmr_data_frames;
    _signalling_generator->buildUDTShortMessageSequence(dmr_data_frames, srcId, dstId, message, group, slot_no);
    _control_channel->putRFQueueMultiItem(dmr_data_frames);
}

void Controller::sendUDTShortMessage(QString message, unsigned int dstId, unsigned int srcId, bool group)
{
    unsigned int msg_size = message.size();
    if(msg_size < 1)
        return;
    if(msg_size > 46)
    {
        int num_msg = msg_size / 46;
        for(int i = 0;i<=num_msg;i++)
        {
            QString msg = message.mid(i * 46, 46);
            sendUDTShortMessage(msg, dstId, srcId, group);
        }
        return;
    }

    if(dstId == 0)
    {
        // don't expect ACKU from all
        dstId = StandardAddreses::ALLMSID;
    }
    else
    {
        // expect ACKU from target
        if(!group)
        {
            _ack_handler->addAck(dstId, ServiceAction::ActionMessageRequest);
            _logger->log(Logger::LogLevelInfo, QString("Sending system message %1 to radio: %2").arg(message).arg(dstId));
        }
        else
        {
            _logger->log(Logger::LogLevelInfo, QString("Sending system message %1 to group: %2").arg(message).arg(dstId));
        }
    }
    if(srcId == 0)
    {
        srcId = StandardAddreses::DISPATI;
    }

    buildUDTShortMessageSequence(srcId, dstId, message, group);
}

void Controller::sendUDTMultipartMessage(QList<QString> messages, unsigned int dstId, unsigned int srcId, bool group, uint8_t delay)
{
    QThread::sleep(delay);
    for(int i=0;i<messages.size();i++)
    {
        QString msg = messages.at(i);
        sendUDTShortMessage(msg, dstId, srcId, group);
        QThread::sleep(1);
    }
}

void Controller::sendUDTDGNA(QString dgids, unsigned int dstId, bool attach)
{
    if(dgids.size() < 1)
        return;
    unsigned char data[48];
    memset(data, 0U, 48U);
    QList<QString> tgids = dgids.split(" ");
    QList<unsigned int> dgna_tg;
    if(tgids.size() > 15)
        tgids = tgids.mid(0,15);
    data[0] = (attach) ? 0x01 : 0x00;
    for(int i=0,k=1;i<tgids.size();i++,k=k+3)
    {
        bool ok = false;
        unsigned int group = tgids.at(i).toUInt(&ok);
        if(!ok)
        {
            _logger->log(Logger::LogLevelWarning, QString("Unable to parse group %1 for radio: %2").arg(tgids.at(i)).arg(dstId));
            continue;
        }
        dgna_tg.append(group);
        unsigned int id = (Utils::convertBase10ToBase11GroupNumber(group));
        data[k] = (id >> 16) & 0xFF;
        data[k+1] = (id >> 8) & 0xFF;
        data[k+2] = id & 0xFF;
    }
    if(dgna_tg.size() < 1)
    {
        _logger->log(Logger::LogLevelWarning, QString("No valid DGNA talkgroups selected for radio: %1").arg(dstId));
        return;
    }
    unsigned int blocks = 4;
    QList<unsigned int> registered_tg = _talkgroup_attachments->value(dstId);
    registered_tg = registered_tg + dgna_tg;
    QList<unsigned int> unique_tgids = QSet<unsigned int>(registered_tg.begin(), registered_tg.end()).values();
    updateSubscriptions(unique_tgids, dstId);
    // expect ACKU from target
    _ack_handler->addAck(dstId, ServiceAction::ActionDGNARequest);
    _logger->log(Logger::LogLevelDebug, QString("Sending DGNA %1 to radio: %2").arg(dgids).arg(dstId));

    QVector<CDMRData> dmr_data_frames;
    unsigned int slot_no = _control_channel->getSlot();
    _signalling_generator->buildUDTDGNAShortMessageSequence(dmr_data_frames, dstId, data, blocks, slot_no);
    _control_channel->putRFQueueMultiItem(dmr_data_frames);
}

void Controller::sendUDTCallDivertInfo(unsigned int srcId, unsigned int dstId, unsigned int sap)
{
    unsigned char data[12];
    memset(data, 0U, 12U);
    data[0] = 0x00;
    data[1] = (dstId >> 16) & 0xFF;
    data[2] = (dstId >> 8) & 0xFF;
    data[3] = dstId & 0xFF;

    unsigned int blocks = 1;

    // don't expect ACKU from target
    _logger->log(Logger::LogLevelDebug, QString("Sending call divert information for radio: %1 to radio: %2").arg(dstId).arg(srcId));

    QVector<CDMRData> dmr_data_frames;
    unsigned int slot_no = _control_channel->getSlot();
    _signalling_generator->buildUDTCallDivertShortMessageSequence(dmr_data_frames, StandardAddreses::MSI, srcId, data, blocks, sap, slot_no);
    _control_channel->putRFQueueMultiItem(dmr_data_frames);
}

void Controller::sendRSSIInfo(float rssi, float ber, unsigned int srcId)
{
    QThread::sleep(2);
    QString message = QString("Your RSSI is %1, BER - %2").arg(rssi).arg(ber);
    sendUDTShortMessage(message, srcId, _settings->service_ids.value("signal_report", StandardAddreses::SDMI));
    _control_channel->setText(QString("Signal report message: %1").arg(srcId));
    if(!_settings->headless_mode)
    {
        emit updateLogicalChannels(&_logical_channels);
    }
}

void Controller::pingRadio(unsigned int target_id, bool group)
{
    if(target_id == 0)
        return;
    _logger->log(Logger::LogLevelInfo, QString("Checking presence for target: %1").arg(target_id));
    t1_ping_ms = std::chrono::high_resolution_clock::now();
    emit startPingTimer(3000);
    _ack_handler->addAck(target_id, ServiceAction::ActionPingRequest);
    CDMRCSBK csbk;
    _signalling_generator->createPresenceCheckAhoy(csbk, target_id, group);
    transmitCSBK(csbk, nullptr, _control_channel->getSlot(), _control_channel->getPhysicalChannel(), false, true);
}

void Controller::resetPing()
{
    _ack_handler->removeAckType(ServiceAction::ActionPingRequest);
}

void Controller::timeoutPingResponse()
{
    resetPing();
    emit pingTimeout();
}

void Controller::pollData(unsigned int target_id, unsigned int poll_format, unsigned int srcId)
{
    if(target_id == 0)
        return;
    _logger->log(Logger::LogLevelInfo, QString("Polling data from target: %1").arg(target_id));
    _ack_handler->addAck(target_id, ServiceAction::UDTPoll);
    _short_data_messages.insert(target_id, 1);
    if(srcId == 0)
        srcId = StandardAddreses::SDMI;
    CDMRCSBK csbk;
    // NMEA location poll test
    _signalling_generator->createRequestToUploadUDTPolledData(csbk, srcId, target_id, poll_format, 1);
    transmitCSBK(csbk, nullptr, _control_channel->getSlot(), _control_channel->getPhysicalChannel(), false, true);
}

void Controller::pollStatus(unsigned int target_id)
{
    if(target_id == 0)
        return;
    _logger->log(Logger::LogLevelInfo, QString("Polling status from target: %1").arg(target_id));
    _ack_handler->addAck(target_id, ServiceAction::ActionStatusPoll);
    CDMRCSBK csbk;
    _signalling_generator->createStatusPollAhoy(csbk, StandardAddreses::TSI, target_id, false);
    transmitCSBK(csbk, nullptr, _control_channel->getSlot(), _control_channel->getPhysicalChannel(), false, true);
}

bool Controller::sendAuthCheck(unsigned int target_id)
{
    if(target_id == 0 || !_settings->auth_keys.contains(target_id))
    {
        _logger->log(Logger::LogLevelInfo, QString("No valid authentication key stored for radio: %1").arg(target_id));
        return false;
    }
    _logger->log(Logger::LogLevelInfo, QString("Sending AUTH check to radio: %1").arg(target_id));
    _ack_handler->addAck(target_id, ServiceAction::ActionAuthCheck);
    CDMRCSBK csbk;
    QString key = _settings->auth_keys.value(target_id);
    QByteArray ba_k = QByteArray::fromHex(key.toLatin1());
    unsigned char *k = (unsigned char*)ba_k.constData();
    unsigned int random_number = 0;
    unsigned int response = 0;
    arc4_get_challenge_response((unsigned char*)(k), ba_k.size(), random_number, response);
    _auth_responses->insert(target_id, response);
    _signalling_generator->createAuthCheckAhoy(csbk, target_id, random_number);
    transmitCSBK(csbk, nullptr, _control_channel->getSlot(), _control_channel->getPhysicalChannel(), false, true);
    emit startAuthTimer();
    return true;
}

void Controller::resetAuth()
{
    _ack_handler->removeAckType(ServiceAction::ActionAuthCheck);
    _auth_responses->clear();
}

LogicalChannel* Controller::findNextFreePayloadChannel(unsigned int dstId, unsigned int srcId, bool local)
{
    for(int i=0; i<_logical_channels.size(); i++)
    {
        if(!(_logical_channels[i]->isControlChannel())
                && !(_logical_channels[i]->getDisabled())
                && !(_logical_channels[i]->getBusy()))
        {
            return _logical_channels[i];
        }
    }
    /// No free channels found, find lower priority call channels
    LogicalChannel* logical_channel = findLowerPriorityChannel(dstId, srcId, local);
    return logical_channel;
}

LogicalChannel* Controller::findLowerPriorityChannel(unsigned int dstId, unsigned int srcId, bool local)
{
    /// FIXME: this code only works for teardown of existing network inbound calls,
    ///  it will not work for local ones due to lack of reverse channel signalling
    unsigned int incoming_priority = _settings->call_priorities.value(dstId, 0);
    if(incoming_priority == 0)
        return nullptr;
    for(unsigned int priority=0;priority<3;priority++)
    {
        for(int i=0; i<_logical_channels.size(); i++)
        {
            if(!(_logical_channels[i]->isControlChannel())
                    && !(_logical_channels[i]->getDisabled()) && !(_logical_channels[i]->getLocalCall()))
            {
                unsigned int existing_call_priority = _settings->call_priorities.value(_logical_channels[i]->getDestination(), 0);
                if((existing_call_priority == priority) && (incoming_priority > existing_call_priority))
                {
                    _logger->log(Logger::LogLevelInfo, QString("Tearing down existing call to %1 to prioritize call from %2 towards %3")
                          .arg(_logical_channels[i]->getDestination())
                          .arg(srcId)
                          .arg(dstId));
                    /* // TODO
                    if(_logical_channels[i]->getLocalCall())
                    {
                        CDMRData dmr_control_data;
                        dmr_control_data.setSlotNo(_logical_channels[i]->getSlot());
                        dmr_control_data.setControl(true);
                        dmr_control_data.setCommand(DMRCommand::RCCeaseTransmission);
                        _logical_channels[i]->putRFQueue(dmr_control_data);
                        return nullptr;
                    }
                    */

                    _logical_channels[i]->setDestination(0);
                    _logical_channels[i]->clearNetQueue();
                    _logical_channels[i]->clearRFQueue();


                    if(local)
                    {
                        CDMRCSBK csbk;
                        _signalling_generator->createReplyWaitForSignalling(csbk, srcId);
                        for(int i = 0;i<3;i++)
                        {
                            transmitCSBK(csbk, nullptr, _control_channel->getSlot(), _control_channel->getPhysicalChannel(), false, false);
                        }
                    }
                    return _logical_channels[i];
                }
            }
        }
    }
    return nullptr;
}

LogicalChannel* Controller::findCallChannel(unsigned int dstId, unsigned int srcId, bool dst_only)
{
    for(int i=0; i<_logical_channels.size(); i++)
    {
        if((!_logical_channels[i]->isControlChannel()) && (_logical_channels[i]->getDestination() == dstId)
                && _logical_channels[i]->getBusy()
                && !(_logical_channels[i]->getDisabled()))
        {
            return _logical_channels[i];
        }
        if((!_logical_channels[i]->isControlChannel()) && ((_logical_channels[i]->getDestination() == dstId) ||
                                                           (_logical_channels[i]->getDestination() == srcId))
                && _logical_channels[i]->getBusy()
                && !(_logical_channels[i]->getDisabled()) && !dst_only)
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
                && !(_logical_channels[i]->getDisabled()))
        {
            active_channels.append(_logical_channels[i]);
        }
    }
    return active_channels;
}

LogicalChannel* Controller::getControlOrAlternateChannel()
{
    if(_settings->control_channel_slot == 1)
    {
        return _control_channel;
    }
    else
    {
        return _control_channel_alternate;
    }
}

void Controller::enableLogicalChannel(LogicalChannel *&logical_channel)
{
    CDMRData dmr_control_data;
    dmr_control_data.setCommand(DMRCommand::ChannelEnableDisable);
    dmr_control_data.setControl(true);
    dmr_control_data.setChannelEnable(true);
    dmr_control_data.setSlotNo(logical_channel->getSlot());
    logical_channel->putRFQueue(dmr_control_data, false);
}

void Controller::disableLogicalChannel(LogicalChannel *&logical_channel)
{
    CDMRData dmr_control_data;
    dmr_control_data.setControl(true);
    dmr_control_data.setCommand(DMRCommand::ChannelEnableDisable);
    dmr_control_data.setChannelEnable(false);
    dmr_control_data.setSlotNo(logical_channel->getSlot());
    logical_channel->putRFQueue(dmr_control_data, false);
}

void Controller::processDMRPayload(unsigned char *payload, unsigned int size, int udp_channel_id, bool from_gateway)
{
    bool uuid_present = (size == HOMEBREW_DATA_PACKET_LENGTH) ? false : true;
    unsigned char seqNo = payload[4U];
    unsigned int srcId = (payload[5U] << 16) | (payload[6U] << 8) | (payload[7U] << 0);
    unsigned int dstId = (payload[8U] << 16) | (payload[9U] << 8) | (payload[10U] << 0);
    unsigned int slotNo = (payload[15U] & 0x80U) == 0x80U ? 2U : 1U;
    unsigned int streamId = 0;
    unsigned char ber = payload[53U];
    unsigned char rssi = payload[54U];
    unsigned char uuid[16];
    memset(uuid, 0, 16U);
    if(uuid_present && (size == HOMEBREW_DATA_PACKET_LENGTH + 16U))
    {
        for(uint32_t i=0;i<16;i++)
        {
            uuid[i] = payload[55U+i];
        }
    }
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
    if(uuid_present)
        dmr_data.setUUID(uuid);

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

void Controller::updateSubscriptions(QList<unsigned int> tg_list, unsigned int srcId)
{
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
        QSet<unsigned int> *talkgroups = new QSet<unsigned int>(*_subscribed_talkgroups);
        emit updateTalkgroupSubscriptionList(talkgroups);
    }
}

void Controller::processTalkgroupSubscriptionsMessage(unsigned int srcId, unsigned int slotNo, DMRMessageHandler::data_message *dmessage,
                                                      unsigned int udp_channel_id)
{
    unsigned int size = dmessage->size * 12 - dmessage->pad_nibble / 2 - 2;
    unsigned char msg[size];
    memcpy(msg, dmessage->message, size);
    _ack_handler->removeAck(srcId, ServiceAction::RegistrationWithAttachment);
    bool existing_user = _registered_ms->contains(srcId);
    if(!existing_user)
        _registered_ms->append(srcId);
    if(!_settings->headless_mode)
    {
        QList<unsigned int> *registered_ms = new QList<unsigned int>(*_registered_ms);
        emit updateRegisteredMSList(registered_ms);
    }
    CDMRCSBK csbk;
    _signalling_generator->createReplyRegistrationAccepted(csbk, srcId);
    transmitCSBK(csbk, nullptr, slotNo, udp_channel_id, false, false);
    if(!existing_user && _settings->announce_system_message)
    {
        QVector<LogicalChannel*> active_calls = findActiveChannels();
        QList<QString> messages;
        messages.append(QString("Welcome %1").arg(_id_lookup->getCallsign(srcId)));
        messages.append(QString("Users - %1, channels - %2, active calls - %3")
                .arg(_registered_ms->size())
                .arg(_settings->logical_physical_channels.size())
                .arg(active_calls.size()));
        messages.append(QString("Send SMS to %1 for command help")
                .arg(_settings->service_ids.value("help", 0)));
        QtConcurrent::run(this, &Controller::sendUDTMultipartMessage, messages, srcId, StandardAddreses::DISPATI, false, 2);
    }

    QList<unsigned int> tg_list;
    for(unsigned int i=1;i<size;i=i+3)
    {
        unsigned int tg = 0;
        tg |= msg[i] << 16;
        tg |= msg[i+1] << 8;
        tg |= msg[i+2];
        if(tg == 0)
            continue;
        unsigned int converted_id = Utils::convertBase11GroupNumberToBase10(tg);
        tg_list.append(converted_id);
        _logger->log(Logger::LogLevelInfo, QString("Received talkgroup attachment data from %1: %2")
                     .arg(srcId).arg(converted_id));
    }
    _control_channel->setText(QString("Talkgroup attachment message: %1").arg(srcId));
    if(!_settings->headless_mode)
    {
        emit updateLogicalChannels(&_logical_channels);
    }
    updateSubscriptions(tg_list, srcId);
}

void Controller::processCallDivertMessage(unsigned int srcId, unsigned int slotNo, DMRMessageHandler::data_message *dmessage,
                                          unsigned int udp_channel_id)
{
    unsigned int size = dmessage->size * 12 - dmessage->pad_nibble / 2 - 2;
    unsigned char msg[size];
    memcpy(msg, dmessage->message, size);
    _ack_handler->removeAck(srcId, ServiceAction::CallDivert);

    unsigned int divert_id = 0;
    divert_id |= msg[1] << 16;
    divert_id |= msg[2] << 8;
    divert_id |= msg[3];
    if(divert_id == 0)
    {
        CDMRCSBK csbk;
        _signalling_generator->createReplyUDTCRCError(csbk, srcId);
        transmitCSBK(csbk, nullptr, slotNo, udp_channel_id, false, false);
        _logger->log(Logger::LogLevelInfo, QString("Received call divert with target 0, ignoring"));
        return;
    }

    CDMRCSBK csbk;
    _signalling_generator->createReplyCallDivertAccepted(csbk, srcId);
    transmitCSBK(csbk, nullptr, slotNo, udp_channel_id, false, false);
    _logger->log(Logger::LogLevelInfo, QString("Received call divert data from %1: %2")
                 .arg(srcId).arg(divert_id));
    if(_settings->announce_system_message)
    {
        QString message = QString("Calls to %1 are now diverted to %2").arg(_id_lookup->getCallsign(srcId)).arg(divert_id);
        sendUDTShortMessage(message, srcId);
    }
    _settings->call_diverts.insert(srcId, divert_id);
    _control_channel->setText(QString("Call divert message: %1").arg(srcId));
    if(!_settings->headless_mode)
    {
        emit updateLogicalChannels(&_logical_channels);
    }
}

void Controller::processNMEAMessage(unsigned int srcId, unsigned int dstId, DMRMessageHandler::data_message *dmessage)
{
    unsigned int size = dmessage->size * 12 - dmessage->pad_nibble / 2 - 2;
    unsigned char msg[size];
    memcpy(msg, dmessage->message, size);
    QList<QString> messages = Utils::readNMEA(msg , dmessage->size);
    QStringList allmsg(messages);
    _logger->log(Logger::LogLevelInfo, QString("Received NMEA UDT location message from %1 to %2: %3")
          .arg(srcId)
          .arg(dstId)
          .arg(allmsg.join(", ")));
    if(dstId != StandardAddreses::SDMI)
    {
        sendUDTMultipartMessage(messages, dstId, srcId, false, 1);
    }
    if(!_settings->headless_mode)
    {
        if(dstId == StandardAddreses::SDMI)
            emit positionResponse(srcId, allmsg.join(", "));
        emit updateMessageLog(srcId, dstId, allmsg.join(", "), false);
    }
    _control_channel->setText(QString("Position message: %1").arg(srcId));
    if(!_settings->headless_mode)
    {
        emit updateLogicalChannels(&_logical_channels);
    }
}

void Controller::processTextMessage(unsigned int dstId, unsigned int srcId, DMRMessageHandler::data_message *dmessage, bool group)
{
    if(group || dmessage->group)
        dstId = Utils::convertBase11GroupNumberToBase10(dstId);
    if((dmessage->udt_format == 4) || (dmessage->udt_format == 3) || (dmessage->udt_format == 7))
    {
        unsigned int size = dmessage->size * 12 - dmessage->pad_nibble / 2 - 2; // size does not include CRC16
        unsigned char msg[size];
        memcpy(msg, dmessage->message, size);
        QString text_message;
        // last character seems to be null termination
        if(dmessage->udt_format == 4)
            text_message = QString::fromUtf8((const char*)msg, size - 1).trimmed();
        else if(dmessage->udt_format == 3)
        {
            unsigned int bit7_size = 8 * size / 7;
            unsigned char converted[bit7_size];
            Utils::parseISO7bitToISO8bit(msg, converted, bit7_size, size);
            text_message = QString::fromUtf8((const char*)converted, bit7_size - 1).trimmed();
        }
        else if(dmessage->udt_format == 7)
        {
            Utils::parseUTF16(text_message, size - 1, msg);
            text_message = text_message.trimmed();
        }
        sendUDTShortMessage(text_message, dstId, srcId, group);
        if(group)
        {
            _logger->log(Logger::LogLevelInfo, QString("Received group UDT short data message from %1 to %2: %3")
                  .arg(srcId)
                  .arg(dstId)
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
                emit updateMessageLog(srcId, dstId, text_message, true);
            }
            else
            {
                emit updateMessageLog(srcId, dstId, text_message, false);
            }
        }
        _control_channel->setText(QString("Short text message: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
}

void Controller::confirmPDPMessageReception(unsigned int srcId, unsigned int slotNo,
                                            DMRMessageHandler::data_message *dmessage, unsigned int udp_channel_id)
{
    QVector<CDMRData> dmr_data_frames;
    LogicalChannel *data_rcv_channel = findChannelByPhysicalIdAndSlot(udp_channel_id, slotNo);
    unsigned int blocks = 1;
    CDMRData dmr_response = _signalling_generator->createConfirmedMessageResponseHeader(
                StandardAddreses::HDATA_GW, srcId, dmessage->seq_no,
                blocks, dmessage->sap, false, dmessage->missed_blocks);
    dmr_response.setSlotNo(slotNo);
    dmr_data_frames.append(dmr_response);

    for(uint8_t i=0;i<blocks;i++)
    {
        CDMRData dmr_data = _signalling_generator->createConfirmedDataResponsePayload(
                    StandardAddreses::HDATA_GW, srcId, dmessage->missed_blocks, i);
        dmr_data.setSlotNo(slotNo);
        dmr_data_frames.append(dmr_data);
    }

    CDMRData terminator = _signalling_generator->createDataTerminatorLC(
                StandardAddreses::HDATA_GW, srcId, dmessage->group, 1, 1, dmessage->seq_no);
    for(int i=0;i<10;i++)
    {
        dmr_data_frames.append(terminator);
    }
    data_rcv_channel->putRFQueueMultiItem(dmr_data_frames);
    data_rcv_channel->setText(QString("Response confirmation to: %1").arg(srcId));
}

void Controller::replayPacketData(unsigned int srcId, unsigned int dstId, unsigned int slotNo)
{
    CDMRCSBK csbk;
    bool channel_grant = true;
    LogicalChannel *logical_channel = nullptr;
    handlePrivatePacketDataCallRequest(csbk, logical_channel, slotNo,
                                       StandardAddreses::HDATA_GW, dstId, channel_grant, true);
    transmitCSBK(csbk, logical_channel, logical_channel->getSlot(), _control_channel->getPhysicalChannel(), channel_grant, false);
    CDMRCSBK csbk2;
    bool valid = false;

    if((csbk.getCSBKO() == CSBKO_PD_GRANT) || (csbk.getCSBKO() == CSBKO_PD_GRANT_MI))
        valid = _signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);

    if(valid)
    {
        transmitCSBK(csbk2, logical_channel, logical_channel->getSlot(), _control_channel->getPhysicalChannel(), channel_grant, false);
    }
    _logger->log(Logger::LogLevelInfo, QString("Sending packet data"
                                                " from %1, slot %2 to destination %3")
                 .arg(srcId).arg(slotNo).arg(dstId));
    QVector<CDMRData> *dmr_data_frames = _dmr_message_handler->getDataFromBuffer(srcId);
    if(dmr_data_frames != nullptr)
    {
        QVector<CDMRData> dmr_message_frames;
        for(int i=0;i<dmr_data_frames->size();i++)
        {
            CDMRData dmr_outbound_data = dmr_data_frames->at(i);
            dmr_outbound_data.setSrcId(StandardAddreses::HDATA_GW);
            dmr_outbound_data.setDstId(dstId);
            dmr_outbound_data.setSlotNo(logical_channel->getSlot());
            dmr_message_frames.append(dmr_outbound_data);
        }
        logical_channel->putRFQueueMultiItem(dmr_message_frames);
        _dmr_message_handler->clearDataBuffer(srcId);
    }
}

void Controller::processDataProtocolMessage(unsigned int dstId, unsigned int srcId,
                                            DMRMessageHandler::data_message *dmessage,
                                            unsigned int udp_channel_id, unsigned int slotNo)
{
    if(dmessage->udt == false)
    {
        srcId = dmessage->real_src;
        dstId = dmessage->group ? Utils::convertBase11GroupNumberToBase10(dmessage->real_dst) : dmessage->real_dst;
        QString text_message;
        text_message = QString::fromUtf8((const char*)dmessage->payload, dmessage->payload_len).trimmed();

        ///** Sending the message on the control channel
        int size = text_message.size();
        if(size > 0)
        {
            sendUDTShortMessage(text_message, dstId, srcId, dmessage->group);
        }
        //*/

        ///** TODO: replay received packet data on dedicated channel
        //replayPacketData(srcId, dstId, slotNo);

        if(dmessage->group)
        {
            _logger->log(Logger::LogLevelInfo, QString("Received group data protocol message from %1 to %2: %3")
                  .arg(srcId)
                  .arg(dstId)
                  .arg(text_message));
        }
        else
        {
            _logger->log(Logger::LogLevelInfo, QString("Received private data protocol message from %1 to %2: %3")
                      .arg(srcId)
                      .arg(dstId)
                      .arg(text_message));
        }
        if(!_settings->headless_mode)
        {
            if(dmessage->group)
            {
                emit updateMessageLog(srcId, dstId, text_message, true);
            }
            else
            {
                emit updateMessageLog(srcId, dstId, text_message, false);
            }
        }
        confirmPDPMessageReception(srcId, slotNo, dmessage, udp_channel_id);
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
}

void Controller::processUDPProtocolMessage(unsigned int dstId, unsigned int srcId,
                                            DMRMessageHandler::data_message *dmessage, bool from_gateway)
{
    dstId = (dmessage->group && !from_gateway) ? Utils::convertBase11GroupNumberToBase10(dstId) : dstId;
    QString text_message;
    for(uint i=0;i<dmessage->payload_len;i++)
    {
        char x = dmessage->payload[i];
        if(x != 0x00)
        {
           text_message.append(QChar(x));
        }
    }
    text_message = text_message.trimmed();

    ///** Sending the message on the control channel
    int size = text_message.size();
    if(size > 0)
    {
        sendUDTShortMessage(text_message, dstId, srcId, dmessage->group);
    }

    if(dmessage->group)
    {
        _logger->log(Logger::LogLevelInfo, QString("Received group UDP protocol message from %1 to %2: %3")
              .arg(srcId)
              .arg(dstId)
              .arg(text_message));
    }
    else
    {
        _logger->log(Logger::LogLevelInfo, QString("Received private UDP protocol message from %1 to %2: %3")
                  .arg(srcId)
                  .arg(dstId)
                  .arg(text_message));
    }
    if(!_settings->headless_mode)
    {
        if(dmessage->group)
        {
            emit updateMessageLog(srcId, dstId, text_message, true);
        }
        else
        {
            emit updateMessageLog(srcId, dstId, text_message, false);
        }
    }
    if(!_settings->headless_mode)
    {
        emit updateLogicalChannels(&_logical_channels);
    }
}

void Controller::processTextServiceRequest(CDMRData &dmr_data, DMRMessageHandler::data_message *dmessage, unsigned int udp_channel_id)
{
    /// Used for testing and debug purposes
    ///
    unsigned int dstId = dmr_data.getDstId();
    unsigned int srcId = dmr_data.getSrcId();
    /// Help
    if(dstId == (unsigned int)_settings->service_ids.value("help", 0))
    {
        CDMRCSBK csbk;
        _signalling_generator->createReplyMessageAccepted(csbk, srcId, dstId, false);
        transmitCSBK(csbk, nullptr, dmr_data.getSlotNo(), udp_channel_id, false, false);
        QList<QString> messages;
        messages.append(QString("Available commands"));
        QMapIterator<QString, unsigned int> it(_settings->service_ids);
        while(it.hasNext())
        {
            it.next();
            messages.append(QString("%1 - %2").arg(it.key()).arg(it.value()));
        }
        if(messages.size() < 2)
        {
            messages.append(QString("The site does not offer any user services"));
        }
        QtConcurrent::run(this, &Controller::sendUDTMultipartMessage, messages, srcId, _settings->service_ids.value("help", StandardAddreses::SDMI), false, 5);
        _control_channel->setText(QString("Help message: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
    /// Location query ???
    else if(dstId == (unsigned int)_settings->service_ids.value("location", 0))
    {
        CDMRCSBK csbk;
        _signalling_generator->createReplyMessageAccepted(csbk, srcId, dstId, false);
        transmitCSBK(csbk, nullptr, dmr_data.getSlotNo(), udp_channel_id, false, false);
        if((dmessage->udt_format == 4) || (dmessage->udt_format == 3) || (dmessage->udt_format == 7))
        {
            unsigned int size = dmessage->size * 12 - dmessage->pad_nibble / 2 - 2; // size does not include CRC16
            unsigned char msg[size];
            memcpy(msg, dmessage->message, size);
            QString text_message;
            // last character seems to be null termination
            if(dmessage->udt_format == 4)
                text_message = QString::fromUtf8((const char*)msg, size - 1).trimmed();
            else if(dmessage->udt_format == 3)
            {
                unsigned int bit7_size = 8 * size / 7;
                unsigned char converted[bit7_size];
                Utils::parseISO7bitToISO8bit(msg, converted, bit7_size, size);
                text_message = QString::fromUtf8((const char*)converted, bit7_size - 1).trimmed();
            }
            else if(dmessage->udt_format == 7)
            {
                Utils::parseUTF16(text_message, size - 1, msg);
                text_message = text_message.trimmed();
            }
            if(text_message.size() > 0)
            {
                bool ok = false;
                unsigned int target_id = text_message.toUInt(&ok);
                if(ok && (target_id > 0) && (target_id < 0xFFFFFFF))
                    pollData(target_id, PollFMT::PollNMEA, srcId);
            }
        }
        _control_channel->setText(QString("Location query message: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
    /// Signal report request
    else if(dstId == (unsigned int)_settings->service_ids.value("signal_report", 0))
    {
        CDMRCSBK csbk;
        _signalling_generator->createReplyMessageAccepted(csbk, srcId, dstId, false);
        transmitCSBK(csbk, nullptr, dmr_data.getSlotNo(), udp_channel_id, false, false);
        QtConcurrent::run(this, &Controller::sendRSSIInfo, dmessage->rssi, dmessage->ber, srcId);
    }
    /// DGNA
    else if(dstId == (unsigned int)_settings->service_ids.value("dgna", 0))
    {
        CDMRCSBK csbk;
        _signalling_generator->createReplyMessageAccepted(csbk, srcId, dstId, false);
        transmitCSBK(csbk, nullptr, dmr_data.getSlotNo(), udp_channel_id, false, false);
        if((dmessage->udt_format == 4) || (dmessage->udt_format == 3) || (dmessage->udt_format == 7))
        {
            unsigned int size = dmessage->size * 12 - dmessage->pad_nibble / 2 - 2; // size does not include CRC16
            unsigned char msg[size];
            memcpy(msg, dmessage->message, size);
            QString text_message;
            // last character seems to be null termination
            if(dmessage->udt_format == 4)
                text_message = QString::fromUtf8((const char*)msg, size - 1).trimmed();
            else if(dmessage->udt_format == 3)
            {
                unsigned int bit7_size = 8 * size / 7;
                unsigned char converted[bit7_size];
                Utils::parseISO7bitToISO8bit(msg, converted, bit7_size, size);
                text_message = QString::fromUtf8((const char*)converted, bit7_size - 1).trimmed();
            }
            else if(dmessage->udt_format == 7)
            {
                Utils::parseUTF16(text_message, size - 1, msg);
                text_message = text_message.trimmed();
            }
            if(text_message.size() > 0)
            {
                sendUDTDGNA(text_message, srcId);
            }
            _control_channel->setText(QString("DGNA request message: %1").arg(srcId));
            if(!_settings->headless_mode)
            {
                emit updateLogicalChannels(&_logical_channels);
            }
        }
    }
}

void Controller::processData(CDMRData &dmr_data, unsigned int udp_channel_id, bool from_gateway)
{
    bool local_data = !from_gateway;
    bool forward_to_gw = false;
    unsigned int dstId = dmr_data.getDstId();
    unsigned int srcId = dmr_data.getSrcId();
    unsigned int dstIdRewritten;
    /// Rewriting destination to match DMR tier III flat numbering
    if(local_data)
    {
        if(dmr_data.getFLCO() == FLCO_GROUP)
            dstIdRewritten = Utils::convertBase11GroupNumberToBase10(dmr_data.getDstId());
        else
            dstIdRewritten = dmr_data.getDstId();
    }
    else
    {
        dstIdRewritten = dmr_data.getDstId();
        _dmr_rewrite->rewriteSource(dmr_data);
    }
    LogicalChannel *logical_channel;
    logical_channel = findCallChannel(dstIdRewritten, srcId);
    if(logical_channel != nullptr)
    {
        bool update_gui = false;
        if(logical_channel->getLocalCall() != local_data)
        {
            logical_channel->setLocalCall(local_data);
            update_gui = true;
        }
        if(logical_channel->getSource() != srcId)
        {
            logical_channel->setSource(srcId);
            update_gui = true;
        }
        if(logical_channel->getDestination() != dstIdRewritten)
        {
            logical_channel->setDestination(dstIdRewritten);
            update_gui = true;
        }
        logical_channel->startTimeoutTimer();
        logical_channel->startLastFrameTimer();
        logical_channel->updateStats(dmr_data);

        if(update_gui && !_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }

    DMRMessageHandler::data_message *message = nullptr;
    if(dmr_data.getFLCO() == FLCO_GROUP)
    {
        message = _dmr_message_handler->processData(dmr_data, from_gateway);
        if(message != nullptr)
        {
            if(message->crc_valid)
            {
                if(message->udt)
                {
                    /// Location upload
                    if(message->udt_format == 5)
                    {
                        processNMEAMessage(srcId, dstId, message);
                    }

                    /// Text message
                    else if((message->udt_format == 4) || (message->udt_format == 3) || (message->udt_format == 7))
                    {
                        processTextMessage(dstId, srcId, message, dmr_data.getFLCO() == FLCO_GROUP);
                    }
                    _logger->log(Logger::LogLevelDebug, QString("DMR Slot %1, received UDT data MS to TG from %2 to %3")
                                 .arg(dmr_data.getSlotNo()).arg(srcId).arg(Utils::convertBase11GroupNumberToBase10(dstId)));
                    if(!from_gateway)
                    {
                        CDMRCSBK csbk;
                        _signalling_generator->createReplyMessageAccepted(csbk, dmr_data.getSrcId());
                        transmitCSBK(csbk, nullptr, dmr_data.getSlotNo(), udp_channel_id, false, true);
                    }
                }
                else if(message->sap == 4)
                {
                    forward_to_gw = true;
                    processUDPProtocolMessage(dstId, srcId, message, from_gateway);
                }
                else if(message->sap == 10 && message->type == 13)
                {
                    forward_to_gw = true;
                    processUDPProtocolMessage(dstId, srcId, message, from_gateway);
                }
                else
                {
                    forward_to_gw = true;
                    processDataProtocolMessage(dstId, srcId, message, udp_channel_id, dmr_data.getSlotNo());
                }
            }
            else if(message->udt && !from_gateway)
            {
                CDMRCSBK csbk;
                _signalling_generator->createReplyUDTCRCError(csbk, srcId);
                transmitCSBK(csbk, nullptr, dmr_data.getSlotNo(), udp_channel_id, false, false);
                _logger->log(Logger::LogLevelWarning, QString("Invalid UDT message CRC16 from %1").arg(srcId));
            }
            else if((message->type == DPF_CONFIRMED_DATA) && !from_gateway)
            {
                confirmPDPMessageReception(srcId, dmr_data.getSlotNo(), message, udp_channel_id);
            }
        }

    }
    else if(dmr_data.getFLCO() == FLCO_USER_USER)
    {
        message = _dmr_message_handler->processData(dmr_data, from_gateway);
        if(message != nullptr)
        {
            if(message->crc_valid)
            {
                /// Talkgroup attachment list
                if(_ack_handler->hasAck(srcId, ServiceAction::RegistrationWithAttachment) &&
                        (message->udt) &&
                        (message->udt_format==1))
                {
                    processTalkgroupSubscriptionsMessage(srcId, dmr_data.getSlotNo(), message, udp_channel_id);
                }
                /// Talkgroup attachment list
                else if(_ack_handler->hasAck(srcId, ServiceAction::CallDivert) &&
                        message->udt &&
                        (message->udt_format==1))
                {
                    processCallDivertMessage(srcId, dmr_data.getSlotNo(), message, udp_channel_id);
                }
                /// Text message
                else
                {
                    if(message->udt) // UDT message on control channel
                    {
                        if((message->udt_format == 4) || (message->udt_format == 3) || (message->udt_format == 7))
                        {
                            processTextMessage(dstId, srcId, message, false);
                            if(_settings->service_ids.values().contains(dstId))
                            {
                                processTextServiceRequest(dmr_data, message, udp_channel_id);
                            }
                        }
                        else if(message->udt_format == 5)
                        {
                            processNMEAMessage(srcId, dstId, message);
                        }
                    }
                    else if(message->sap == 4)
                    {
                        forward_to_gw = true;
                        processUDPProtocolMessage(dstId, srcId, message, from_gateway);
                    }
                    else if(message->sap == 10 && message->type == 13)
                    {
                        forward_to_gw = true;
                        processUDPProtocolMessage(dstId, srcId, message, from_gateway);
                    }
                    else
                    {
                        forward_to_gw = true;
                        processDataProtocolMessage(dstId, srcId, message, udp_channel_id, dmr_data.getSlotNo());
                    }
                }
            }
            else if(message->udt && !from_gateway)
            {
                CDMRCSBK csbk;
                _signalling_generator->createReplyUDTCRCError(csbk, srcId);
                transmitCSBK(csbk, nullptr, dmr_data.getSlotNo(), udp_channel_id, false, false);
                _logger->log(Logger::LogLevelWarning, QString("Invalid UDT message CRC16 from %1").arg(srcId));
            }
            else if((message->type == DPF_CONFIRMED_DATA) && !from_gateway)
            {
                confirmPDPMessageReception(srcId, dmr_data.getSlotNo(), message, udp_channel_id);
            }
        }
    }
    if(message != nullptr)
        delete message;

    /// Rewriting destination to match DMR tier III flat numbering
    if(from_gateway)
    {
        return; // cannot handle this type of data here
        if(dmr_data.getFLCO() == FLCO_GROUP)
        {
            if(_settings->receive_tg_attach &&
                    _settings->transmit_subscribed_tg_only &&
                    !_subscribed_talkgroups->contains(dstId))
            {
                // Do not transmit unsubscribed talkgroups if not configured to do so
                return;
            }
            dstId = Utils::convertBase10ToBase11GroupNumber(dmr_data.getDstId());
            _signalling_generator->rewriteUDTHeader(dmr_data, dstId);
        }
        else
        {
            if(_settings->call_diverts.contains(dstId))
            {
                dstId = _settings->call_diverts.value(dstId);
                _signalling_generator->rewriteUDTHeader(dmr_data, dstId);
            }
        }
        dmr_data.setDstId(dstId);
        dmr_data.setSlotNo(_control_channel->getSlot());
        _control_channel->putRFQueue(dmr_data);
    }
    else if(!from_gateway && forward_to_gw)
    {
        if(dmr_data.getFLCO() == FLCO_GROUP)
        {
            dstId = Utils::convertBase11GroupNumberToBase10(dmr_data.getDstId());
            _signalling_generator->rewriteUDTHeader(dmr_data, dstId);
        }
        else
        {
            if(_settings->call_diverts.contains(dstId))
            {
                dstId = _settings->call_diverts.value(dstId);
                _signalling_generator->rewriteUDTHeader(dmr_data, dstId);
            }
        }
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
        {
            if(_settings->receive_tg_attach &&
                    _settings->transmit_subscribed_tg_only &&
                    !_subscribed_talkgroups->contains(dstId))
            {
                // Do not transmit unsubscribed talkgroups if not configured to do so
                return;
            }
            dmr_data.setDstId(Utils::convertBase10ToBase11GroupNumber(dstId));
        }

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
        if(logical_channel->getLocalCall() != local_data)
        {
            logical_channel->setLocalCall(local_data);
            update_gui = true;
        }
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
    logical_channel = findNextFreePayloadChannel(dstId, srcId, local_data);
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
            if(_settings->call_diverts.contains(dstId))
            {
                dstId = _settings->call_diverts.value(dstId);
            }
            if(_registered_ms->contains(dstId) && !_private_calls.contains(dstId))
            {
                channel_grant = false;
                contactMSForVoiceCall(csbk_grant, dmr_data.getSlotNo(), srcId, dstId, false);
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
            transmitCSBK(csbk_grant, logical_channel, _control_channel->getSlot(),
                             _control_channel->getPhysicalChannel(), false, priority, true);
            if(valid)
            {
                transmitCSBK(csbk2, logical_channel, _control_channel->getSlot(), _control_channel->getPhysicalChannel(),
                                 false, false, true);
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
    dstId = csbk.getDstId();
    if((csbk.getServiceOptions() & 0x01) == 1)
    {
        unsigned int target_addr_cnts = (csbk.getCBF() >> 6) & 0x03;
        uab = (csbk.getCBF() << 4) & 0x03;
        if(target_addr_cnts == 1)
        {
            unsigned int converted_id = Utils::convertBase11GroupNumberToBase10(dstId);
            _logger->log(Logger::LogLevelInfo, QString("DMR Slot %1, received registration request from %2 with attachement to TG %3")
                         .arg(slotNo).arg(srcId).arg(converted_id));
            _signalling_generator->createReplyRegistrationAccepted(csbk, srcId);
            if(!_registered_ms->contains(srcId))
                _registered_ms->append(srcId);
            QList<unsigned int> tg_list;
            tg_list.append(converted_id);
            updateSubscriptions(tg_list, srcId);
        }
        else if((target_addr_cnts == 0))
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
        else if((target_addr_cnts == 2))
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
            QSet<unsigned int> *talkgroups = new QSet<unsigned int>(*_subscribed_talkgroups);
            emit updateTalkgroupSubscriptionList(talkgroups);
        }
    }
    if(!_settings->headless_mode)
    {
        QList<unsigned int> *registered_ms = new QList<unsigned int>(*_registered_ms);
        emit updateRegisteredMSList(registered_ms);
    }
    return sub;
}

void Controller::contactMSForVoiceCall(CDMRCSBK &csbk, unsigned int slotNo,
                            unsigned int srcId, unsigned int dstId, bool local)
{
    _logger->log(Logger::LogLevelInfo, QString("TSCC: DMR Slot %1, received private call request from %2 to TG %3")
                 .arg(slotNo).arg(srcId).arg(dstId));
    if(!_private_calls.contains(dstId))
        _private_calls.insert(dstId, srcId);
    _ack_handler->addAck(dstId, ServiceAction::ActionPrivateVoiceCallRequest);
    _signalling_generator->createPrivateVoiceCallRequest(csbk, local, srcId, dstId);
    return;
}

void Controller::contactMSForPacketCall(CDMRCSBK &csbk, unsigned int slotNo,
                            unsigned int srcId, unsigned int dstId)
{
    _logger->log(Logger::LogLevelInfo, QString("TSCC: DMR Slot %1, received private packet data call request from %2 to MS %3")
                 .arg(slotNo).arg(srcId).arg(dstId));
    if(!_private_calls.contains(dstId))
        _private_calls.insert(dstId, srcId);
    _ack_handler->addAck(dstId, ServiceAction::ActionPrivatePacketCallRequest);
    _signalling_generator->createPrivatePacketCallAhoy(csbk, srcId, dstId);
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
    unsigned int dest = (local) ? srcId : dstId;
    unsigned int src = (local) ? dstId : srcId;
    logical_channel = findNextFreePayloadChannel(dest, src, local);
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
        logical_channel->allocateChannel(srcId, dstId, CallType::CALL_TYPE_MS, local);
        logical_channel->setCallType(CallType::CALL_TYPE_MS);
        enableLogicalChannel(logical_channel);
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
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
    logical_channel = findNextFreePayloadChannel(dmrDstId, srcId, local);
    if(logical_channel == nullptr)
    {
        _logger->log(Logger::LogLevelWarning, "Could not find any free logical channels, telling MS to wait");
        _signalling_generator->createReplyCallDenied(csbk, srcId);
        if(!_settings->headless_mode)
        {
            emit updateRejectedCallsList(srcId, dmrDstId, local);
        }
        return;
    }
    else
    {
        _signalling_generator->createGroupVoiceGrant(csbk, logical_channel, srcId, dstId);
        channel_grant = true;
        logical_channel->allocateChannel(srcId, dmrDstId, CallType::CALL_TYPE_GROUP, local);
        logical_channel->setCallType(CallType::CALL_TYPE_GROUP);
        enableLogicalChannel(logical_channel);

        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
        return;
    }
}

void Controller::handlePrivatePacketDataCallRequest(CDMRCSBK &csbk, LogicalChannel *&logical_channel, unsigned int slotNo,
                            unsigned int srcId, unsigned int dstId, bool &channel_grant, bool local)
{
    _logger->log(Logger::LogLevelInfo, QString("TSCC: DMR Slot %1, received private packet data call request from %2 to destination %3")
                 .arg(slotNo).arg(srcId).arg(dstId));

    logical_channel = findCallChannel(dstId, srcId, true);
    if(logical_channel != nullptr)
    {
        _signalling_generator->createPrivatePacketDataGrant(csbk, logical_channel, srcId, dstId);
        if(srcId == logical_channel->getSource())
            channel_grant = true;
        logical_channel->setDestination(dstId);
        logical_channel->setSource(srcId);
        logical_channel->startTimeoutTimer();
        return;
    }

    // Next try to find a free payload channel to allocate
    logical_channel = findNextFreePayloadChannel(dstId, srcId, local);
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
        _signalling_generator->createPrivatePacketDataGrant(csbk, logical_channel, srcId, dstId);
        channel_grant = true;
        logical_channel->allocateChannel(srcId, dstId, CallType::CALL_TYPE_INDIV_PACKET, local);
        enableLogicalChannel(logical_channel);
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
        return;
    }
}

void Controller::handleGroupPacketDataCallRequest(CDMRCSBK &csbk, LogicalChannel *&logical_channel, unsigned int slotNo,
                            unsigned int srcId, unsigned int dstId, bool &channel_grant, bool local)
{
    _logger->log(Logger::LogLevelInfo, QString("TSCC: DMR Slot %1, received group packet data call request from %2 to destination %3")
                 .arg(slotNo).arg(srcId).arg(dstId));

    logical_channel = findCallChannel(dstId, srcId);
    if(logical_channel != nullptr)
    {
        _signalling_generator->createGroupPacketDataGrant(csbk, logical_channel, srcId, dstId);
        if(srcId == logical_channel->getSource())
            channel_grant = true;
        logical_channel->setDestination(dstId);
        logical_channel->setSource(srcId);
        logical_channel->startTimeoutTimer();
        return;
    }

    // Next try to find a free payload channel to allocate
    logical_channel = findNextFreePayloadChannel(dstId, srcId, local);
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
        _signalling_generator->createGroupPacketDataGrant(csbk, logical_channel, srcId, dstId);
        channel_grant = true;
        logical_channel->allocateChannel(srcId, dstId, CallType::CALL_TYPE_GROUP_PACKET, local);
        enableLogicalChannel(logical_channel);
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
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
    unsigned int call_type = _logical_channels.at(channel_id)->getCallType();
    if(call_type == CallType::CALL_TYPE_MS)
    {
        if(_private_calls.contains(_logical_channels.at(channel_id)->getDestination()))
            _private_calls.remove(_logical_channels.at(channel_id)->getDestination());
        if(_private_calls.contains(_logical_channels.at(channel_id)->getSource()))
            _private_calls.remove(_logical_channels.at(channel_id)->getSource());
    }

    CDMRCSBK csbk;
    if(call_type == CallType::CALL_TYPE_INDIV_PACKET)
        call_type = CallType::CALL_TYPE_MS;
    if(call_type == CallType::CALL_TYPE_GROUP_PACKET)
        call_type = CallType::CALL_TYPE_GROUP;
    _signalling_generator->createChannelIdleDeallocation(csbk, call_type);
    for(int i=0;i < 5;i++)
    {
        transmitCSBK(csbk, _logical_channels[channel_id], _logical_channels[channel_id]->getSlot(),
                         _logical_channels[channel_id]->getPhysicalChannel(), false);
    }
    disableLogicalChannel(_logical_channels[channel_id]);

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

void Controller::processRegistration(unsigned int srcId, unsigned int dstId, CDMRCSBK &csbk)
{
    bool existing_user = _registered_ms->contains(srcId);
    unsigned int uab = 0;
    LogicalChannel* logical_channel = nullptr;
    bool sub = handleRegistration(csbk, _control_channel->getSlot(), srcId, dstId, uab);
    if(sub)
    {
        // FIXME: order is wrong
        _ack_handler->addAck(srcId, ServiceAction::RegistrationWithAttachment);
        _signalling_generator->createRequestToUploadTgAttachments(csbk, srcId, uab);
        _short_data_messages.insert(srcId, uab);
        transmitCSBK(csbk, logical_channel, _control_channel->getSlot(), _control_channel->getPhysicalChannel(), false, false);
    }
    else
    {
        transmitCSBK(csbk, logical_channel, _control_channel->getSlot(), _control_channel->getPhysicalChannel(), false, true);
        if(!existing_user && _settings->announce_system_message)
        {
            QVector<LogicalChannel*> active_calls = findActiveChannels();
            QList<QString> messages;
            messages.append(QString("Welcome %1").arg(_id_lookup->getCallsign(srcId)));
            messages.append(QString("Users - %1, channels - %2, calls - %3,")
                    .arg(_registered_ms->size())
                    .arg(_settings->logical_physical_channels.size())
                    .arg(active_calls.size()));
            messages.append(QString("Send SMS to %1 for command help")
                    .arg(_settings->service_ids.value("help", 0)));
            QtConcurrent::run(this, &Controller::sendUDTMultipartMessage, messages, srcId, StandardAddreses::DISPATI, false, 1);
        }
    }
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
        if(_settings->authentication_required && !_registered_ms->contains(srcId))
        {
            _logger->log(Logger::LogLevelInfo, QString("User %1 is required to authenticate")
                         .arg(srcId));
            bool key_valid = sendAuthCheck(srcId);
            if(!key_valid)
            {
                _logger->log(Logger::LogLevelInfo, QString("User %1 does not have a registration key stored in config."
                            " Registration denied.").arg(srcId));
                _signalling_generator->createReplyRegistrationDenied(csbk, srcId);
                transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
            }
            else
            {
                _auth_user.insert(srcId, csbk);
            }
        }
        else
        {
            processRegistration(srcId, dstId, csbk);
            _control_channel->setText(QString("Registration / deregistration: %1").arg(srcId));
            if(!_settings->headless_mode)
            {
                emit updateLogicalChannels(&_logical_channels);
            }
        }
    }
    /// MS authentication response
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x90) &&
             _ack_handler->hasAck(srcId, ServiceAction::ActionAuthCheck))
    {
        _ack_handler->removeAck(srcId, ServiceAction::ActionAuthCheck);
        if(_auth_responses->contains(srcId))
        {
            if((dstId == _auth_responses->value(srcId)))
            {
                if(_auth_user.contains(srcId))
                {
                    CDMRCSBK csbk = _auth_user[srcId];
                    processRegistration(srcId, dstId, csbk);
                    _auth_user.remove(srcId);
                }
                _logger->log(Logger::LogLevelInfo, QString("Received authentication reply (SUCCESS) from %1, slot %2")
                             .arg(srcId).arg(slotNo));
                if(!_settings->headless_mode)
                    emit authSuccess(true);
                _control_channel->setText(QString("Successful authentication reply: %1").arg(srcId));
                if(!_settings->headless_mode)
                {
                    emit updateLogicalChannels(&_logical_channels);
                }
            }
            else
            {
                if(_auth_user.contains(srcId))
                {
                    _signalling_generator->createReplyRegistrationDenied(csbk, srcId);
                    transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
                    _auth_user.remove(srcId);
                }
                _logger->log(Logger::LogLevelInfo, QString("Received authentication reply (FAILED) from %1, slot %2")
                             .arg(srcId).arg(slotNo));
                if(!_settings->headless_mode)
                    emit authSuccess(false);
                _control_channel->setText(QString("Failed authentication reply: %1").arg(srcId));
                if(!_settings->headless_mode)
                {
                    emit updateLogicalChannels(&_logical_channels);
                }
            }
            _auth_responses->remove(srcId);
            emit stopAuthTimer();
        }
    }
    /// Service requested while not registered
    else if(!validateLocalSourceId(srcId))
    {
        _signalling_generator->createReplyNotRegistered(csbk, srcId);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
        _logger->log(Logger::LogLevelInfo, QString("Received service request while not registered from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));
        _control_channel->setText(QString("Unregistered service request: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
    ///
    /// All below signaling needs the MS to be registered
    ///
    /// MS ping reply
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x88) &&
             _ack_handler->hasAck(srcId, ServiceAction::ActionPingRequest))
    {
        std::chrono::high_resolution_clock::time_point t2_ping_ms = std::chrono::high_resolution_clock::now();
        uint64_t msec = std::chrono::duration_cast<std::chrono::nanoseconds>(t2_ping_ms - t1_ping_ms).count() / 1000000U;
        emit stopPingTimer();
        _ack_handler->removeAck(srcId, ServiceAction::ActionPingRequest);
        emit pingResponse(srcId, msec);
        _control_channel->setText(QString("Presence check response: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
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
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        if(valid)
        {
            transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        }
        _control_channel->setText(QString("Talkgroup voice request: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
    /// Group call with suplimentary data
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::GroupVoiceCall) && csbk.getSuplimentaryData())
    {
        _signalling_generator->createRequestToSendGroupCallSupplimentaryData(csbk, csbk.getSrcId());
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
        _control_channel->setText(QString("Talkgroup voice request with SUPLI: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
    /// Direct MS to MS call request
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::IndivVoiceCall))
    {
        if(_settings->call_diverts.contains(dstId))
        {
            sendUDTCallDivertInfo(srcId, _settings->call_diverts.value(dstId), 0);
            _logger->log(Logger::LogLevelInfo, QString("Received radio FOACSU call request (diverted) from %1, slot %2 to destination %3")
                         .arg(srcId).arg(slotNo).arg(dstId));
            return;
        }
        if(_registered_ms->contains(dstId))
        {
            /** FIXME: The standard call procedure does not include a wait notification
            CDMRCSBK csbk_wait;
            _signalling_generator->createReplyWaitForSignalling(csbk_wait, csbk.getSrcId());
            transmitCSBK(csbk_wait, logical_channel, slotNo, udp_channel_id, channel_grant, false);
            */
            contactMSForVoiceCall(csbk, slotNo, srcId, dstId, true);
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
            _logger->log(Logger::LogLevelInfo, QString("Received radio FOACSU call request from %1, slot %2 to destination %3")
                         .arg(srcId).arg(slotNo).arg(dstId));
        }
        else
        {
            handlePrivateCallRequest(csbk, logical_channel, slotNo, srcId, dstId, channel_grant, false);

            CDMRCSBK csbk2;
            bool valid = false;
            if(csbk.getCSBKO() == CSBKO_PV_GRANT)
                valid = _signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
            if(valid)
            {
                transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, channel_grant, false);
            }
            _logger->log(Logger::LogLevelInfo, QString("Received radio OACSU call request (not registered radio)"
                                                        " from %1, slot %2 to destination %3")
                         .arg(srcId).arg(slotNo).arg(dstId));
        }
        _control_channel->setText(QString("Private voice request: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
    /// MS acknowledgement of OACSU call
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x88) &&
             _ack_handler->hasAck(srcId, ServiceAction::ActionPrivateVoiceCallRequest))
    {

        if(_private_calls.contains(srcId))
            _private_calls.remove(srcId);
        _ack_handler->removeAck(srcId, ServiceAction::ActionPrivateVoiceCallRequest);
        csbk.setCSBKO(CSBKO_ACKD);
        csbk.setDstId(dstId);
        csbk.setSrcId(srcId);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        handlePrivateCallRequest(csbk, logical_channel, slotNo, srcId, dstId, channel_grant, true);

        CDMRCSBK csbk2;
        bool valid = false;
        if(csbk.getCSBKO() == CSBKO_PV_GRANT)
            valid = _signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        if(valid)
        {
            transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        }
        csbk.setDstId(srcId);
        csbk.setSrcId(dstId);

        if(csbk.getCSBKO() == CSBKO_PV_GRANT)
            valid = _signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        if(valid)
        {
            transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        }
        _logger->log(Logger::LogLevelInfo, QString("Received acknowledgement for OACSU call from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));
        _control_channel->setText(QString("OACSU call acknowledgemet: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
    /// MS acknowledgement of FOACSU call
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x8C))
    {
        if(_ack_handler->hasAck(srcId, ServiceAction::ActionPrivateVoiceCallRequest))
        {
            _ack_handler->removeAck(srcId, ServiceAction::ActionPrivateVoiceCallRequest);
        }
        csbk.setCSBKO(CSBKO_ACKD);
        csbk.setDstId(dstId);
        csbk.setSrcId(srcId);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        _logger->log(Logger::LogLevelDebug, QString("Received acknowledgement for FOACSU call from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));
        _control_channel->setText(QString("FOACSU call acknowledgemet: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
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
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        if(valid)
        {
            transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        }
        _logger->log(Logger::LogLevelInfo, QString("Received radio FOACSU call answer from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));
        _control_channel->setText(QString("FOACSU call answer: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
    /// call reject
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::CallAnswer) && ((csbk.getCBF() & 0xF0) == 0x20))
    {
        if(_private_calls.contains(srcId))
            _private_calls.remove(srcId);
        _signalling_generator->createReplyCallRejected(csbk, srcId, dstId);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        _control_channel->setText(QString("FOACSU call reject: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
    /// cancel private call
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::CancelCall) && (csbk.getDstId() > 0))
    {
        if(_private_calls.contains(srcId))
            _private_calls.remove(srcId);
        _signalling_generator->createCancelPrivateCallAhoy(csbk, csbk.getDstId());
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        _control_channel->setText(QString("FOACSU call cancelled: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
    /// cancel call
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::CancelCall) && (csbk.getDstId() == 0))
    {
        if(_private_calls.contains(srcId))
            _private_calls.remove(srcId);
        handleCallDisconnect(udp_channel_id, group_call, srcId, dstId, slotNo, logical_channel, csbk);
        for(int i=0;i<3;i++)
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        _control_channel->setText(QString("Call cancelled: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
        disableLogicalChannel(logical_channel);
    }
    /// Call disconnect
    else if ((csbko == CSBKO_MAINT) && (csbk.getServiceKind() == ServiceKind::IndivVoiceCall))
    {
        handleCallDisconnect(udp_channel_id, group_call, srcId, dstId, slotNo, logical_channel, csbk);
        for(int i=0;i<3;i++)
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        CDMRCSBK csbk_receiver;
        _signalling_generator->createCallDisconnect(csbk_receiver, srcId, group_call);
        for(int i=0;i<3;i++)
            transmitCSBK(csbk_receiver, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        _control_channel->setText(QString("Call disconnect: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
        disableLogicalChannel(logical_channel);
    }
    /// MS acknowledgement of short data message
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x88) &&
             _ack_handler->hasAck(srcId, ServiceAction::ActionMessageRequest))
    {
        _ack_handler->removeAck(srcId, ServiceAction::ActionMessageRequest);
        csbk.setCSBKO(CSBKO_ACKD);
        csbk.setDstId(dstId);
        csbk.setSrcId(srcId);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        csbk.setDstId(srcId);
        csbk.setSrcId(StandardAddreses::SDMI);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        _logger->log(Logger::LogLevelInfo, QString("Received read receipt for message request from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));
        _control_channel->setText(QString("Short message acknowledge: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
    /// MS acknowledgement of DGNA request
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x88) &&
             _ack_handler->hasAck(srcId, ServiceAction::ActionDGNARequest))
    {
        _ack_handler->removeAck(srcId, ServiceAction::ActionDGNARequest);
        csbk.setCSBKO(CSBKO_ACKD);
        csbk.setDstId(dstId);
        csbk.setSrcId(srcId);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        _logger->log(Logger::LogLevelInfo, QString("Received ACK for DGNA request from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));
        _control_channel->setText(QString("DGNA assignement acknowledge: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
    /// Short data service MS to MS
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::IndivUDTDataCall))
    {
        if(_settings->call_diverts.contains(dstId))
        {
            sendUDTCallDivertInfo(srcId, _settings->call_diverts.value(dstId), 4); // FIXME: SAP 0100 for UDT causes radio to transmit with ID set to 0???
            return;
        }
        _ack_handler->addAck(dstId, ServiceAction::ActionMessageRequest);
        unsigned int number_of_blocks = _signalling_generator->createRequestToUploadMessage(csbk, csbk.getSrcId());
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
        _short_data_messages.insert(srcId, number_of_blocks);
        _logger->log(Logger::LogLevelInfo, QString("Received private short data message request from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));
        _control_channel->setText(QString("Short private message request: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
    /// Short data service MS to TG
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::GroupUDTDataCall))
    {
        unsigned int number_of_blocks = _signalling_generator->createRequestToUploadMessage(csbk, csbk.getSrcId());
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
        _short_data_messages.insert(srcId, number_of_blocks);
        _logger->log(Logger::LogLevelInfo, QString("Received group short data message request to TG from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));
        _control_channel->setText(QString("Short talkgroup message request: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
    /// Status transport message MS to MS and MS toTG
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::StatusTransport))
    {
        uint8_t status = (csbk.getCBF() >> 4) & 0x03;
        status |= ((csbk.getData1() >> 1) & 0x1F) << 2;
        if((csbk.getData1() & 0x80) == 0x80)
        {
            CDMRCSBK csbk2;
            _signalling_generator->createReplyMessageAccepted(csbk2, srcId, StandardAddreses::TSI);
            transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, false, false);
            _signalling_generator->createStatusTransportAhoy(csbk, srcId, dstId, true);
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
            _logger->log(Logger::LogLevelInfo, QString("Received status transport request to TG from %1, slot %2 to destination %3")
                         .arg(srcId).arg(slotNo).arg(Utils::convertBase11GroupNumberToBase10(dstId)));
            _logger->log(Logger::LogLevelInfo, QString("Status of radio %1, for talkgroup %2 is %3")
                         .arg(srcId).arg(Utils::convertBase11GroupNumberToBase10(dstId)).arg(status));
            _control_channel->setText(QString("Status transfer to talkgroup: %1").arg(srcId));
            if(!_settings->headless_mode)
            {
                emit updateLogicalChannels(&_logical_channels);
            }
        }
        else
        {
            _ack_handler->addAck(dstId, ServiceAction::ActionStatusMsg);
            _signalling_generator->createStatusTransportAhoy(csbk, srcId, dstId, false);
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
            _logger->log(Logger::LogLevelInfo, QString("Received status transport request to MS from %1, slot %2 to destination %3")
                         .arg(srcId).arg(slotNo).arg(dstId));
            _logger->log(Logger::LogLevelInfo, QString("Status of radio %1, for radio %2 is %3")
                         .arg(srcId).arg(dstId).arg(status));
            _control_channel->setText(QString("Status transfer to radio: %1").arg(srcId));
            if(!_settings->headless_mode)
            {
                emit updateLogicalChannels(&_logical_channels);
            }
        }
    }
    /// MS acknowledgement of status transport message
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x88) &&
             _ack_handler->hasAck(srcId, ServiceAction::ActionStatusMsg))
    {
        _ack_handler->removeAck(srcId, ServiceAction::ActionStatusMsg);
        csbk.setCSBKO(CSBKO_ACKD);
        csbk.setDstId(dstId);
        csbk.setSrcId(srcId);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        _logger->log(Logger::LogLevelInfo, QString("Received read receipt for status transport from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));
        _control_channel->setText(QString("Acknowledge status transfer: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
    /// MS status poll reply
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x8E) &&
             _ack_handler->hasAck(srcId, ServiceAction::ActionStatusPoll))
    {
        _ack_handler->removeAck(srcId, ServiceAction::ActionStatusPoll);
        unsigned int status = csbk.getData1() >> 1;
        _logger->log(Logger::LogLevelInfo, QString("Received status poll reply %4 from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId).arg(status));
        _control_channel->setText(QString("Status poll reply: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
    /// MS status poll reply, service not supported
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x00) && ((csbk.getData1() & 0x01) == 0x00) &&
             _ack_handler->hasAck(srcId, ServiceAction::ActionStatusPoll))
    {
        _ack_handler->removeAck(srcId, ServiceAction::ActionStatusPoll);
        _logger->log(Logger::LogLevelInfo, QString("Received status poll reply UNSUPPORTED SERVICE from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));
        _control_channel->setText(QString("Status poll reply UNSUPPORTED: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
    /// MS UDT data poll reply, service not supported
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x00) && ((csbk.getData1() & 0x01) == 0x00) &&
             _ack_handler->hasAck(srcId, ServiceAction::UDTPoll))
    {
        _ack_handler->removeAck(srcId, ServiceAction::UDTPoll);
        _short_data_messages.remove(srcId);
        _logger->log(Logger::LogLevelInfo, QString("Received UDT data poll reply UNSUPPORTED SERVICE from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));
        _control_channel->setText(QString("UDT poll reply UNSUPPORTED: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
    /// Call diversion request
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::CallDiversion))
    {
        unsigned int service_options = csbk.getServiceOptions();
        bool divert = ((service_options >> 4) & 0x01) == 1;
        if(divert)
        {
            unsigned int number_of_blocks = _signalling_generator->createRequestToUploadDivertInfo(csbk, csbk.getSrcId());
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
            _short_data_messages.insert(srcId, number_of_blocks);
            _ack_handler->addAck(srcId, ServiceAction::CallDivert);
            _logger->log(Logger::LogLevelInfo, QString("Received call diversion request request from %1, slot %2 to destination %3")
                         .arg(srcId).arg(slotNo).arg(dstId));
            _control_channel->setText(QString("Call diversion request: %1").arg(srcId));
            if(!_settings->headless_mode)
            {
                emit updateLogicalChannels(&_logical_channels);
            }
        }
        else
        {
            _settings->call_diverts.remove(srcId);
            _signalling_generator->createReplyCallDivertAccepted(csbk, srcId);
            transmitCSBK(csbk, nullptr, slotNo, udp_channel_id, false, false);
            _logger->log(Logger::LogLevelInfo, QString("Received cancel call diversion request request from %1, slot %2 to destination %3")
                         .arg(srcId).arg(slotNo).arg(dstId));
            _control_channel->setText(QString("Cancel call diversion request: %1").arg(srcId));
            if(!_settings->headless_mode)
            {
                emit updateLogicalChannels(&_logical_channels);
            }
        }
    }
    /// Individual packet data call
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::IndivPacketDataCall))
    {
        if(_settings->call_diverts.contains(dstId))
        {
            sendUDTCallDivertInfo(srcId, _settings->call_diverts.value(dstId), 0);
            _logger->log(Logger::LogLevelInfo, QString("Received radio packet data call request (diverted) from %1, slot %2 to destination %3")
                         .arg(srcId).arg(slotNo).arg(dstId));
            return;
        }
        if(_registered_ms->contains(dstId))
        {
            qDebug() << "FID: " << csbk.getFID() << " data1: " << csbk.getData1() << " data2: " << csbk.getCBF();
            uint8_t service_options = csbk.getServiceOptions();
            _simi = service_options;
            contactMSForPacketCall(csbk, slotNo, srcId, dstId);
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
            _logger->log(Logger::LogLevelInfo, QString("Received private packet data call request from %1, slot %2 to destination %3")
                         .arg(srcId).arg(slotNo).arg(dstId));
        }
        else
        {
            handlePrivatePacketDataCallRequest(csbk, logical_channel, slotNo, srcId, dstId, channel_grant, true);
            CDMRCSBK csbk2;
            bool valid = false;
            if((csbk.getCSBKO() == CSBKO_PD_GRANT) || (csbk.getCSBKO() == CSBKO_PD_GRANT_MI))
                valid = _signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
            if(valid)
            {
                transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, channel_grant, false);
            }
            _logger->log(Logger::LogLevelInfo, QString("Received private packet data call request"
                                                        " from %1, slot %2 to destination %3 (not registered ID)")
                         .arg(srcId).arg(slotNo).arg(dstId));
        }
        _control_channel->setText(QString("Individual packet data call: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
    /// Group packet data call
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::GroupPacketDataCall))
    {

        handleGroupPacketDataCallRequest(csbk, logical_channel, slotNo, srcId, dstId, channel_grant, true);
        CDMRCSBK csbk2;
        bool valid = false;
        if((csbk.getCSBKO() == CSBKO_TD_GRANT) || (csbk.getCSBKO() == CSBKO_TD_GRANT_MI))
            valid = _signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        if(valid)
        {
            transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        }
        _logger->log(Logger::LogLevelInfo, QString("Received group packet data call request"
                                                    " from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));

        _control_channel->setText(QString("Group packet data call: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
    /// MS acknowledgement of private packet data call
    else if ((csbko == CSBKO_ACKU) &&
             _ack_handler->hasAck(srcId, ServiceAction::ActionPrivatePacketCallRequest))
    {
        _ack_handler->removeAck(srcId, ServiceAction::ActionPrivatePacketCallRequest);
        csbk.setCSBKO(CSBKO_ACKD);
        csbk.setDstId(dstId);
        csbk.setSrcId(srcId);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        _logger->log(Logger::LogLevelInfo, QString("Received packet data acknowledgement from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));
        // FIXME
        csbk.setData1((unsigned char)(_simi << 1));
        handlePrivatePacketDataCallRequest(csbk, logical_channel, slotNo, dstId, srcId, channel_grant, true);
        CDMRCSBK csbk2;
        bool valid = false;
        if((csbk.getCSBKO() == CSBKO_PD_GRANT) || (csbk.getCSBKO() == CSBKO_PD_GRANT_MI))
            valid = _signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        if(valid)
        {
            transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        }
        _control_channel->setText(QString("Individual packet data call response: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }

    }

    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x88))
    {
        _logger->log(Logger::LogLevelDebug, QString("Received unhandled ACKU from %1, slot %2 to destination %3")
                     .arg(srcId).arg(slotNo).arg(dstId));
        _control_channel->setText(QString("Unhandled radio reply: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
    }
    /// Not implemeted yet
    else
    {
        qDebug() << "CSBKO: " << QString::number(csbk.getCSBKO(), 16) <<
                    " FID " << csbk.getFID() <<
                    " data1: " << csbk.getData1() << " data2: " << csbk.getCBF() <<
                    " dst: " << dstId << " src: " << srcId;
        _logger->log(Logger::LogLevelDebug, QString("Unhandled CSBK type slot %1, channel %2").arg(slotNo).arg(udp_channel_id));
        _control_channel->setText(QString("Unknown service request: %1").arg(srcId));
        if(!_settings->headless_mode)
        {
            emit updateLogicalChannels(&_logical_channels);
        }
        return;
    }
}

void Controller::processNetworkCSBK(CDMRData &dmr_data, int udp_channel_id)
{
    (void)udp_channel_id;
    _logger->log(Logger::LogLevelDebug, QString("Received network CSBK from %1, slot %2 to destination %3")
                 .arg(dmr_data.getSrcId()).arg(dmr_data.getSlotNo()).arg(dmr_data.getDstId()));
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

    qDebug() << "CSBKO: " << QString::number(csbk.getCSBKO(), 16) <<
                " FID " << csbk.getFID() <<
                " data1: " << csbk.getData1() << " data2: " << csbk.getCBF() <<
                " dst: " << dstId << " src: " << srcId;
}


void Controller::transmitCSBK(CDMRCSBK &csbk, LogicalChannel *logical_channel, unsigned int slotNo,
                                  unsigned int udp_channel_id, bool channel_grant, bool priority_queue, bool announce_priority)
{
    CDMRData dmr_data_wait = _signalling_generator->createWaitForSignallingAnswer(slotNo, csbk, channel_grant);
    CDMRData dmr_data = _signalling_generator->createDataFromCSBK(slotNo, csbk);
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
    for(int i=0;i<_gateway_channels.size();i++)
    {
        _gateway_channels[i]->writeDMRConfig(config);
    }
}

void Controller::processDMRNetworkMessage(unsigned char* payload ,unsigned int size)
{
    if(!_network_signalling->validateNetMessage(payload, size))
        return;
}

QVector<LogicalChannel *> *Controller::getLogicalChannels()
{
    return &_logical_channels;
}

void Controller::setChannelEnabled(unsigned int index, bool state)
{
    if(index < (unsigned int)_logical_channels.size())
    {
        if(!_logical_channels[index]->isControlChannel())
        {
            if(state)
            {
                _settings->channel_disable_bitmask &= ~(1 << index);
            }
            else
            {
                _settings->channel_disable_bitmask |= 1 << index;
            }
            _logical_channels[index]->setDisabled(!state);
        }
    }
    if(!_settings->headless_mode)
    {
        emit updateLogicalChannels(&_logical_channels);
    }
}

bool Controller::validateLocalSourceId(unsigned int srcId)
{
    if(_registered_ms->contains(srcId) || !_settings->registration_required)
        return true;
    _logger->log(Logger::LogLevelWarning, QString("Rejected local id %1, not registered with the site").arg(srcId));
    return false;
}

void Controller::setCallStats(unsigned int srcId, unsigned int dstId, float rssi, float ber, bool private_call)
{
    if(!_settings->headless_mode)
    {
        emit updateCallLog(srcId, dstId, rssi, ber, private_call);
    }
    if(dstId == (unsigned int)_settings->service_ids.value("signal_report", 0))
    {
        QtConcurrent::run(this, &Controller::sendRSSIInfo, rssi, ber, srcId);
    }
}


