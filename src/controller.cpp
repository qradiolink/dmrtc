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

#include "controller.h"

const unsigned int HOMEBREW_DATA_PACKET_LENGTH = 55U;

Controller::Controller(Settings* settings, Logger* logger, DMRIdLookup* id_lookup, QObject* parent) : QObject(parent)
{
    m_settings = settings;
    m_logger = logger;
    m_id_lookup = id_lookup;
    m_registered_ms = new QList<unsigned int>;
    m_talkgroup_attachments = new QMap<unsigned int, QList<unsigned int>>;
    m_talkgroup_dgna = new QMap<unsigned int, QList<unsigned int>>;
    m_rejected_calls = new QSet<unsigned int>;
    m_subscribed_talkgroups = new QSet<unsigned int>;
    // Because ACKU CSBK contains no ServiceKind, need to store some state to determine which service is it pertinent for
    m_ack_handler = new AckHandler;
    m_auth_responses = new QMap<unsigned int, unsigned int>;
    m_dmr_rewrite = new DMRRewrite(settings, m_registered_ms);
    m_gateway_router = new GatewayRouter(m_settings, m_logger);
    m_signalling_generator = new Signalling(m_settings);
    m_network_signalling = new NetworkSignalling(settings, logger);
    m_dmr_message_handler = new DMRMessageHandler(settings, logger);
    m_stop_thread = false;
    m_late_entry_announcing = false;
    m_system_freqs_announcing = false;
    m_adjacent_sites_announcing = false;
    m_private_calls_announcing = false;
    t1_ping_ms = std::chrono::high_resolution_clock::now();
    m_startup_completed = false;
    m_minute = 1;
}

Controller::~Controller()
{
    m_registered_ms->clear();
    delete m_registered_ms;
    m_talkgroup_attachments->clear();
    delete m_talkgroup_attachments;
    m_talkgroup_dgna->clear();
    delete m_talkgroup_dgna;
    m_rejected_calls->clear();
    delete m_rejected_calls;
    m_subscribed_talkgroups->clear();
    delete m_subscribed_talkgroups;
    m_auth_responses->clear();
    delete m_auth_responses;
    delete m_dmr_rewrite;
    delete m_gateway_router;
    delete m_signalling_generator;
    delete m_network_signalling;
    delete m_dmr_message_handler;
    delete m_ack_handler;
}

void Controller::stop()
{
    if (m_settings->use_trunking_protocol) {
        cleanupSubscriptions();
        QThread::sleep(2);
    }

    // end main thread execution
    m_logger->log(Logger::LogLevelInfo, QString("Stopping controller thread"));
    m_stop_thread = true;
    QThread::sleep(1);
}

void Controller::run()
{
    uint8_t counter = 0;
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

    /// Create logical channels
    for (int i = 0; i < m_settings->channel_number; i++) {

        if (i == m_settings->control_channel_physical_id) {
            m_control_channel = new LogicalChannel(m_settings, m_logger, counter, i, 1, true);
            counter++;
            m_logical_channels.append(m_control_channel);

            if (m_settings->control_channel_slot == 1) {
                LogicalChannel* payload_channel = new LogicalChannel(m_settings, m_logger, counter, i, 2, false, !m_settings->headless_mode);
                counter++;
                QObject::connect(payload_channel, SIGNAL(channelDeallocated(unsigned int)), this, SLOT(handleIdleChannelDeallocation(unsigned int)));
                QObject::connect(payload_channel, SIGNAL(update()), this, SLOT(updateChannelsToGUI()));
                QObject::connect(payload_channel, SIGNAL(setCallStats(uint, uint, float, float, float, uint, bool)),
                                 this, SLOT(setCallStats(uint, uint, float, float, float, uint, bool)));
                m_logical_channels.append(payload_channel);
            } else {
                m_control_channel_alternate = new LogicalChannel(m_settings, m_logger, counter, i, 2, true);
                counter++;
                m_logical_channels.append(m_control_channel_alternate);
            }
        } else {
            LogicalChannel* payload_channel1 = new LogicalChannel(m_settings, m_logger, counter, i, 1, false, !m_settings->headless_mode);
            counter++;
            LogicalChannel* payload_channel2 = new LogicalChannel(m_settings, m_logger, counter, i, 2, false, !m_settings->headless_mode);
            counter++;
            QObject::connect(payload_channel1, SIGNAL(channelDeallocated(unsigned int)), this, SLOT(handleIdleChannelDeallocation(unsigned int)));
            QObject::connect(payload_channel2, SIGNAL(channelDeallocated(unsigned int)), this, SLOT(handleIdleChannelDeallocation(unsigned int)));
            QObject::connect(payload_channel1, SIGNAL(update()), this, SLOT(updateChannelsToGUI()));
            QObject::connect(payload_channel2, SIGNAL(update()), this, SLOT(updateChannelsToGUI()));
            QObject::connect(payload_channel1, SIGNAL(setCallStats(uint, uint, float, float, float, uint, bool)),
                             this, SLOT(setCallStats(uint, uint, float, float, float, uint, bool)));
            QObject::connect(payload_channel2, SIGNAL(setCallStats(uint, uint, float, float, float, uint, bool)),
                             this, SLOT(setCallStats(uint, uint, float, float, float, uint, bool)));
            m_logical_channels.append(payload_channel1);
            m_logical_channels.append(payload_channel2);
        }
    }

    for (unsigned int i = 0U; i < m_logical_channels.size(); i++) {
        if (!m_logical_channels[i]->isControlChannel()) {
            int state = (m_settings->channel_disable_bitmask >> i) & 1;
            m_logical_channels[i]->setDisabled((bool)state);
        }
    }

    if (!m_settings->headless_mode) {
        emit updateLogicalChannels(&m_logical_channels);
    }

    /// Create network sockets
    for (int i = 0; i < m_settings->channel_number; i++) {
        UDPClient* client = new UDPClient(m_settings, m_logger, i);
        m_udp_channels.append(client);
        client->enable(true);
        QObject::connect(client, SIGNAL(dmrData(unsigned char*, uint, uint, bool)),
                         this, SLOT(inputNetDMRPayload(unsigned char*, uint, uint, bool)), Qt::DirectConnection);

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

    QList<unsigned int> gw_ids = m_gateway_router->getGatewayIds();

    for (unsigned int i = 0; i < gw_ids.size(); i++) {
        UDPClient* gateway_udpclient = new UDPClient(m_settings, m_logger, i, m_settings->gateway_listen_port + i, m_settings->gateway_send_port + i,
                                                     m_settings->gateway_remote_address, true);

        QObject::connect(gateway_udpclient, SIGNAL(dmrData(unsigned char*, unsigned int, unsigned int, bool)),
                         this, SLOT(inputNetDMRPayload(unsigned char*, unsigned int, unsigned int, bool)));
        QObject::connect(gateway_udpclient, SIGNAL(newDMRNetworkMessage(unsigned char*, unsigned int)),
                         this, SLOT(processDMRNetworkMessage(unsigned char*, unsigned int)));
        QObject::connect(this, SIGNAL(writeDMRData(CDMRData&)),
                         gateway_udpclient, SLOT(writeDMRData(CDMRData&)));
        m_gateway_channels.insert(gw_ids.at(i), gateway_udpclient);
    }

    if (m_settings->gateway_enabled && (m_gateway_channels.size() < 1)) {
        m_logger->log(Logger::LogLevelWarning, QString("No DMR Gateways configured"));
    }


    for (unsigned int i = 0; i < m_gateway_channels.size(); i++) {
        m_gateway_channels[m_gateway_channels.keys().at(i)]->enable(true);
    }


    announce_system_freqs_timer.setInterval(m_settings->announce_system_freqs_interval * 1000);
    announce_system_freqs_timer.setSingleShot(true);
    announce_system_freqs_timer.start();
    announce_adjacent_sites_timer.setInterval(m_settings->announce_adjacent_bs_interval * 1000);
    announce_adjacent_sites_timer.setSingleShot(true);
    announce_adjacent_sites_timer.start();

    if (m_settings->use_trunking_protocol) {
        subscribeStaticTalkgroups();
    }


    /// Main thread loop where most things happen
    while (!m_stop_thread) {
        if (!m_startup_completed && m_settings->registration_required) {
            requestMassRegistration();
        }

        (void)QtConcurrent::run(&Controller::announceLateEntry, this);
        (void)QtConcurrent::run(&Controller::announcePrivateCalls, this);

        if (!announce_system_freqs_timer.isActive()) {
            (void)QtConcurrent::run(&Controller::announceSystemFreqs, this);
            announce_system_freqs_timer.start();
        }

        if (!announce_adjacent_sites_timer.isActive()) {
            (void)QtConcurrent::run(&Controller::announceAdjacentSites, this);
            announce_adjacent_sites_timer.start();
        }

        uint16_t min = QDateTime::currentDateTime().time().minute();

        if (((min == 0) || (min == 30) || (min == 15) || (min == 45)) && (min != m_minute)) {
            m_minute = min;
            (void)QtConcurrent::run(&Controller::announceLocalTime, this);

            if (m_settings->announce_system_message && ((min == 0) || (min == 30))) {
                (void)QtConcurrent::run(&Controller::announceSystemMessage, this);
            }
        }

        QCoreApplication::processEvents(); // process signals


        for (unsigned int i = 0U; i < m_logical_channels.size(); i++) {
            /// data going towards RF
            CDMRData dmr_data;

            while (m_logical_channels.at(i)->getRFQueue(dmr_data)) {
                if (dmr_data.getControl()) {
                    m_udp_channels.at(m_logical_channels.at(i)->getPhysicalChannel())->writeDMRTrunkingParams(dmr_data);
                } else {
                    m_udp_channels.at(m_logical_channels.at(i)->getPhysicalChannel())->writeDMRData(dmr_data);
                }
            }

            /// Data going towards net
            CDMRData dmr_data_net;

            while (m_logical_channels.at(i)->getNetQueue(dmr_data_net)) {
                if (!((dmr_data_net.getFLCO() == FLCO_USER_USER) && m_registered_ms->contains(dmr_data_net.getDstId()))) {
                    unsigned int gateway_id = 0;
                    bool route_found = m_gateway_router->findRoute(dmr_data_net, gateway_id);

                    if (route_found && m_gateway_channels.contains(gateway_id) && m_settings->gateway_enabled) {
                        m_gateway_channels[gateway_id]->writeDMRData(dmr_data_net);

                    } else if (m_settings->gateway_enabled) {
                        m_logger->log(Logger::LogLevelWarning,
                                      QString("No Gateway route found for destination %1").arg(dmr_data_net.getDstId()));
                    }
                }
            }

        }

        QThread::usleep(1000);
    }

    /// Thread stopping
    ///
    ///


    for (unsigned int i = 0U; i < m_logical_channels.size(); i++) {
        m_logical_channels.at(i)->stopTimeoutTimer();
    }

    for (unsigned int i = 0U; i < m_udp_channels.size(); i++) {
        m_udp_channels.at(i)->enable(false);
    }

    for (unsigned int i = 0U; i < m_gateway_channels.size(); i++) {
        m_gateway_channels[m_gateway_channels.keys().at(i)]->enable(false);
    }

    for (unsigned int i = 0U; i < m_udp_channels.size(); i++) {
        delete m_udp_channels.at(i);
    }

    for (unsigned int i = 0U; i < m_logical_channels.size(); i++) {
        delete m_logical_channels[i];
    }

    for (unsigned int i = 0U; i < m_gateway_channels.size(); i++) {
        delete m_gateway_channels[m_gateway_channels.keys().at(i)];
    }

    announce_system_freqs_timer.stop();
    emit finished();
}

void Controller::updateChannelsToGUI()
{
    if (!m_settings->headless_mode) {
        emit updateLogicalChannels(&m_logical_channels);
    }
}

void Controller::announceLateEntry()
{
    if (m_late_entry_announcing)
        return;

    m_late_entry_announcing = true;

    if (m_stop_thread)
        return;

    for (unsigned int i = 0U; i < m_logical_channels.size(); i++) {
        if (m_logical_channels.at(i)->getBusy() && !(m_logical_channels.at(i)->getDisabled())
            && !(m_logical_channels.at(i)->isControlChannel())
            && (m_logical_channels.at(i)->getDestination() != 0)
            && (m_logical_channels.at(i)->getCallType() < CallType::CALL_TYPE_INDIV_PACKET)) { // No late entry for packet data
            LogicalChannel* logical_channel = m_logical_channels.at(i);
            CDMRCSBK csbk;
            m_signalling_generator->createLateEntryAnnouncement(logical_channel, csbk);
            CDMRCSBK csbk2;
            bool valid = m_signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);
            transmitCSBK(csbk, logical_channel, m_control_channel->getSlot(), m_control_channel->getPhysicalChannel(),
                         false);

            if (valid) {
                transmitCSBK(csbk2, logical_channel, m_control_channel->getSlot(), m_control_channel->getPhysicalChannel(),
                             false, false);
            }

            // TODO: Set late entry time so it doesn't clash with other data
            QThread::sleep((unsigned long) m_settings->announce_late_entry_interval);

            if (m_stop_thread)
                return;
        }
    }

    m_late_entry_announcing = false;
}

void Controller::requestMassRegistration()
{
    m_logger->log(Logger::LogLevelInfo, QString("Requesting mass registration"));
    CDMRCSBK csbk;
    m_signalling_generator->createRegistrationRequest(csbk);
    transmitCSBK(csbk, nullptr, m_control_channel->getSlot(), m_control_channel->getPhysicalChannel(), false, true);
    m_startup_completed = true;
    m_registered_ms->clear();
}

void Controller::announceSystemFreqs()
{
    if (m_system_freqs_announcing)
        return;

    m_system_freqs_announcing = true;

    if (m_stop_thread)
        return;

    m_logger->log(Logger::LogLevelInfo, QString("Announcing site frequencies, used channels: %1")
                  .arg(m_settings->logical_physical_channels.size()));

    for (unsigned int i = 0U; i < m_settings->logical_physical_channels.size(); i++) {
        if (m_settings->logical_physical_channels[i].size() < 4)
            continue;

        QMap<QString, uint64_t> channel = m_settings->logical_physical_channels[i];
        CDMRCSBK csbk, csbk_cont;
        m_signalling_generator->createLogicalPhysicalChannelsAnnouncement(csbk, csbk_cont, channel);
        transmitCSBK(csbk, nullptr, m_control_channel->getSlot(), m_control_channel->getPhysicalChannel(), false);
        transmitCSBK(csbk_cont, nullptr, m_control_channel->getSlot(), m_control_channel->getPhysicalChannel(), false);

        if (m_stop_thread)
            return;
    }

    m_system_freqs_announcing = false;
}

void Controller::announceAdjacentSites()
{
    if (m_adjacent_sites_announcing)
        return;

    m_adjacent_sites_announcing = true;

    if (m_stop_thread)
        return;

    m_logger->log(Logger::LogLevelInfo, QString("Announcing adjacent sites: %1")
                  .arg(m_settings->adjacent_sites.size()));

    for (unsigned int i = 0U; i < m_settings->adjacent_sites.size(); i++) {
        if (m_settings->adjacent_sites[i].size() < 5)
            continue;

        QMap<QString, uint64_t> site = m_settings->adjacent_sites[i];
        CDMRCSBK csbk;
        m_signalling_generator->createAdjacentSiteAnnouncement(csbk, site);
        transmitCSBK(csbk, nullptr, m_control_channel->getSlot(), m_control_channel->getPhysicalChannel(), false);

        if (m_stop_thread)
            return;
    }

    m_adjacent_sites_announcing = false;
}

void Controller::announceLocalTime()
{
    if (m_stop_thread)
        return;

    QDateTime date_time = QDateTime::currentDateTime();
    m_logger->log(Logger::LogLevelInfo, QString("Announcing local time: %1")
                  .arg(QDateTime::currentDateTime().toString()));
    CDMRCSBK csbk;
    m_signalling_generator->createLocalTimeAnnouncement(csbk, date_time);
    transmitCSBK(csbk, nullptr, m_control_channel->getSlot(), m_control_channel->getPhysicalChannel(), false);
}

void Controller::announceSystemMessage()
{
    if (m_stop_thread)
        return;

    QVector<LogicalChannel*> active_calls = findActiveChannels();
    QList<QString> messages;
    messages.append(QString("%1").arg(m_settings->system_announcement_message));
    messages.append(QString("IP connection - %1").arg(m_settings->gateway_enabled ? "yes" : "no"));
    messages.append(QString("Frequencies - "));

    for (unsigned int i = 0U; i < m_settings->logical_physical_channels.size(); i++) {
        if (m_settings->logical_physical_channels[i].size() < 4)
            continue;

        messages.append(QString::number(m_settings->logical_physical_channels[i].value("rx_freq")) + " ");
    }

    messages.append(QString("Repeater shift - %1").arg(QString::number(m_settings->freq_duplexsplit)));
    messages.append(QString("Users - %1, channels - %2, active calls - %3")
                    .arg(m_registered_ms->size())
                    .arg(m_settings->logical_physical_channels.size())
                    .arg(active_calls.size()));
    messages.append(QString("Send SMS to %1 for command help")
                    .arg(m_settings->service_ids.value("help", 0)));
    unsigned int dstId = StandardAddreses::ALLMSID;
    (void)QtConcurrent::run(&Controller::sendUDTMultipartMessage, this, messages, dstId, StandardAddreses::DISPATI, false, 0);
}

void Controller::announcePrivateCalls()
{
    if (m_private_calls_announcing)
        return;

    m_private_calls_announcing = true;

    if (m_stop_thread)
        return;

    QMap<unsigned int, unsigned int> thread_local_private_calls = m_private_calls;
    QMapIterator<unsigned int, unsigned int> it(thread_local_private_calls);

    while (it.hasNext()) {
        it.next();
        unsigned int dstId = it.key();
        unsigned int srcId = it.value();
        m_logger->log(Logger::LogLevelInfo, QString("TSCC: Announce private call setup from %1 to %2")
                      .arg(srcId).arg(dstId));

        CDMRCSBK csbk;
        m_signalling_generator->createPrivateVoiceCallRequest(csbk, true, srcId, dstId);
        transmitCSBK(csbk, nullptr, m_control_channel->getSlot(),
                     m_control_channel->getPhysicalChannel(), false, false, false);
        QThread::sleep((unsigned long) m_settings->announce_late_entry_interval * 10);

        if (m_stop_thread)
            return;

    }

    m_private_calls_announcing = false;
}


void Controller::buildUDTShortMessageSequence(unsigned int srcId, unsigned int dstId, QString message,
                                              bool group)
{
    dstId = group ? TrunkingUtils::convertBase10ToBase11GroupNumber(dstId) : dstId;
    unsigned int slot_no = m_control_channel->getSlot();
    QVector<CDMRData> dmr_data_frames;
    m_signalling_generator->buildUDTShortMessageSequence(dmr_data_frames, srcId, dstId, message, group, slot_no);
    m_control_channel->putRFQueueMultiItem(dmr_data_frames);
}

void Controller::sendUDTShortMessage(QString message, unsigned int dstId, unsigned int srcId, bool group)
{
    unsigned int msg_size = message.size();

    if (msg_size < 1U)
        return;

    if (msg_size > 46) {
        unsigned int num_msg = msg_size / 46;

        for (unsigned int i = 0U; i <= num_msg; i++) {
            QString msg = message.mid(i * 46, 46);
            sendUDTShortMessage(msg, dstId, srcId, group);
        }

        return;
    }

    if (dstId == 0) {
        // don't expect ACKU from all
        dstId = StandardAddreses::ALLMSID;
    } else {
        // expect ACKU from target
        if (!group) {
            m_ack_handler->addAck(dstId, ServiceAction::ActionMessageRequest);
            m_logger->log(Logger::LogLevelInfo, QString("Sending system message %1 to radio: %2").arg(message).arg(dstId));
        } else {
            m_logger->log(Logger::LogLevelInfo, QString("Sending system message %1 to group: %2").arg(message).arg(dstId));
        }
    }

    if (srcId == 0) {
        srcId = StandardAddreses::DISPATI;
    }

    buildUDTShortMessageSequence(srcId, dstId, message, group);
}

void Controller::sendUDTMultipartMessage(QList<QString> messages, unsigned int dstId, unsigned int srcId, bool group, uint8_t delay)
{
    QThread::sleep(delay);

    for (unsigned int i = 0U; i < messages.size(); i++) {
        QString msg = messages.at(i);
        sendUDTShortMessage(msg, dstId, srcId, group);
        QThread::sleep(1);
    }
}

void Controller::sendUDTDGNA(QString dgids, unsigned int dstId, bool attach)
{
    if (dgids.size() < 1)
        return;

    unsigned char data[48];
    memset(data, 0U, 48U);
    QList<QString> tgids = dgids.split(" ");
    QList<unsigned int> dgna_tg;

    if (tgids.size() > 15)
        tgids = tgids.mid(0, 15);

    data[0] = (attach) ? 0x01 : 0x00;

    for (unsigned int i = 0U, k = 1U; i < tgids.size(); i++, k = k + 3) {
        bool ok = false;
        unsigned int group = tgids.at(i).toUInt(&ok);

        if (!ok) {
            m_logger->log(Logger::LogLevelWarning, QString("Unable to parse group %1 for radio: %2").arg(tgids.at(i)).arg(dstId));
            continue;
        }

        if (group == 0U)
            continue;

        dgna_tg.append(group);
        unsigned int id = (TrunkingUtils::convertBase10ToBase11GroupNumber(group));
        data[k] = (id >> 16) & 0xFF;
        data[k + 1] = (id >> 8) & 0xFF;
        data[k + 2] = id & 0xFF;
    }

    bool clear_dgna = false;

    if (dgna_tg.size() < 1) {
        clear_dgna = true;
        m_logger->log(Logger::LogLevelInfo, QString("No valid DGNA talkgroups selected for radio: %1, will clear all DGNA allocations")
                      .arg(dstId));
    }

    unsigned int blocks = 4;

    if (!clear_dgna) {
        QList<unsigned int> registered_tg = m_talkgroup_attachments->value(dstId);
        registered_tg = registered_tg + dgna_tg;
        QList<unsigned int> existing_dgna_tg = m_talkgroup_dgna->value(dstId);
        existing_dgna_tg = existing_dgna_tg + dgna_tg;
        m_talkgroup_dgna->insert(dstId, existing_dgna_tg);
        QList<unsigned int> unique_tgids = QSet<unsigned int>(registered_tg.begin(), registered_tg.end()).values();
        updateSubscriptions(unique_tgids, dstId);
        // expect ACKU from target
        m_ack_handler->addAck(dstId, ServiceAction::ActionDGNARequest);
        m_logger->log(Logger::LogLevelDebug, QString("Sending DGNA %1 to radio: %2").arg(dgids).arg(dstId));

        QVector<CDMRData> dmr_data_frames;
        unsigned int slot_no = m_control_channel->getSlot();
        m_signalling_generator->buildUDTDGNAShortMessageSequence(dmr_data_frames, dstId, data, blocks, slot_no);
        m_control_channel->putRFQueueMultiItem(dmr_data_frames);
    } else {

        QList<unsigned int> existing_dgna_tg = m_talkgroup_dgna->value(dstId);
        QList<unsigned int> registered_tg = m_talkgroup_attachments->value(dstId);

        for (unsigned int i = 0U; i < existing_dgna_tg.size(); i++) {
            registered_tg.removeAll(existing_dgna_tg.at(i));
        }

        m_talkgroup_attachments->insert(dstId, registered_tg);
        m_talkgroup_dgna->clear();
        QList<unsigned int> unique_tgids = QSet<unsigned int>(registered_tg.begin(), registered_tg.end()).values();
        updateSubscriptions(unique_tgids, dstId);
        // expect ACKU from target
        m_ack_handler->addAck(dstId, ServiceAction::ActionDGNARequest);
        m_logger->log(Logger::LogLevelDebug, QString("Sending DGNA %1 to radio: %2").arg(dgids).arg(dstId));

        QVector<CDMRData> dmr_data_frames;
        unsigned int slot_no = m_control_channel->getSlot();
        m_signalling_generator->buildUDTDGNAShortMessageSequence(dmr_data_frames, dstId, data, blocks, slot_no);
        m_control_channel->putRFQueueMultiItem(dmr_data_frames);
    }
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
    m_logger->log(Logger::LogLevelDebug, QString("Sending call divert information for radio: %1 to radio: %2").arg(dstId).arg(srcId));

    QVector<CDMRData> dmr_data_frames;
    unsigned int slot_no = m_control_channel->getSlot();
    m_signalling_generator->buildUDTCallDivertShortMessageSequence(dmr_data_frames, StandardAddreses::MSI, srcId, data, blocks, sap, slot_no);
    m_control_channel->putRFQueueMultiItem(dmr_data_frames);
}

void Controller::sendRSSIInfo(float rssi, float ber, unsigned int srcId)
{
    QThread::sleep(2);
    QString message = QString("Your RSSI is %1, BER - %2").arg(rssi).arg(ber);
    sendUDTShortMessage(message, srcId, m_settings->service_ids.value("signal_report", StandardAddreses::SDMI));
    m_control_channel->setText(QString("Signal report message: %1").arg(srcId));

    if (!m_settings->headless_mode) {
        emit updateLogicalChannels(&m_logical_channels);
    }
}

void Controller::pingRadio(unsigned int target_id, bool group)
{
    if (target_id == 0)
        return;

    m_logger->log(Logger::LogLevelInfo, QString("Checking presence for target: %1").arg(target_id));
    t1_ping_ms = std::chrono::high_resolution_clock::now();
    emit startPingTimer(3000);
    m_ack_handler->addAck(target_id, ServiceAction::ActionPingRequest);
    CDMRCSBK csbk;
    m_signalling_generator->createPresenceCheckAhoy(csbk, target_id, group);
    transmitCSBK(csbk, nullptr, m_control_channel->getSlot(), m_control_channel->getPhysicalChannel(), false, true);
}

void Controller::resetPing()
{
    m_ack_handler->removeAckType(ServiceAction::ActionPingRequest);
}

void Controller::timeoutPingResponse()
{
    resetPing();
    emit pingTimeout();
}

void Controller::pollData(unsigned int target_id, unsigned int poll_format, unsigned int srcId)
{
    if (target_id == 0)
        return;

    m_logger->log(Logger::LogLevelInfo, QString("Polling data from target: %1").arg(target_id));
    m_ack_handler->addAck(target_id, ServiceAction::UDTPoll);
    m_short_data_messages.insert(target_id, 1);

    if (srcId == 0)
        srcId = StandardAddreses::SDMI;

    CDMRCSBK csbk;
    // NMEA location poll test
    m_signalling_generator->createRequestToUploadUDTPolledData(csbk, srcId, target_id, poll_format, 1);
    transmitCSBK(csbk, nullptr, m_control_channel->getSlot(), m_control_channel->getPhysicalChannel(), false, true);
}

void Controller::pollStatus(unsigned int target_id)
{
    if (target_id == 0)
        return;

    m_logger->log(Logger::LogLevelInfo, QString("Polling status from target: %1").arg(target_id));
    m_ack_handler->addAck(target_id, ServiceAction::ActionStatusPoll);
    CDMRCSBK csbk;
    m_signalling_generator->createStatusPollAhoy(csbk, StandardAddreses::TSI, target_id, false);
    transmitCSBK(csbk, nullptr, m_control_channel->getSlot(), m_control_channel->getPhysicalChannel(), false, true);
}

bool Controller::sendAuthCheck(unsigned int target_id)
{
    if (target_id == 0 || !m_settings->auth_keys.contains(target_id)) {
        m_logger->log(Logger::LogLevelInfo, QString("No valid authentication key stored for radio: %1").arg(target_id));
        return false;
    }

    QString key = m_settings->auth_keys.value(target_id);
    QByteArray ba_k = QByteArray::fromHex(key.toLatin1());

    if (ba_k.size() != 16) {
        m_logger->log(Logger::LogLevelWarning, QString("Authentication key format for radio: %1 is wrong").arg(target_id));
        return false;
    }

    m_logger->log(Logger::LogLevelInfo, QString("Sending AUTH check to radio: %1").arg(target_id));
    m_ack_handler->addAck(target_id, ServiceAction::ActionAuthCheck);
    unsigned char* k = (unsigned char*)ba_k.constData();
    unsigned int random_number = 0;
    unsigned int response = 0;
    arc4_get_challenge_response((unsigned char*)(k), ba_k.size(), random_number, response);
    m_auth_responses->insert(target_id, response);
    CDMRCSBK csbk;
    m_signalling_generator->createAuthCheckAhoy(csbk, target_id, random_number);
    transmitCSBK(csbk, nullptr, m_control_channel->getSlot(), m_control_channel->getPhysicalChannel(), false, true);
    emit startAuthTimer();
    return true;
}

void Controller::resetAuth()
{
    m_ack_handler->removeAckType(ServiceAction::ActionAuthCheck);
    m_auth_responses->clear();
}

bool Controller::userRegister(unsigned int dmrId)
{
    bool existing_user = m_registered_ms->contains(dmrId);

    if (!existing_user) {
        m_registered_ms->append(dmrId);

        if (m_settings->use_trunking_protocol && m_settings->send_network_registrations) {
            CDMRData register_message;
            m_network_signalling->createRegistrationMessage(register_message, dmrId);
            m_control_channel->putNetQueue(register_message);
        }
    }

    return existing_user;
}

bool Controller::userDeRegister(unsigned int dmrId)
{
    bool existing_user = m_registered_ms->contains(dmrId);

    if (existing_user) {
        m_registered_ms->removeAll(dmrId);

        if (m_settings->use_trunking_protocol && m_settings->send_network_registrations) {
            CDMRData deregister_message;
            m_network_signalling->createDeRegistrationMessage(deregister_message, dmrId);
            m_control_channel->putNetQueue(deregister_message);
        }

        QList<unsigned int> old_tgs(m_subscribed_talkgroups->begin(), m_subscribed_talkgroups->end());
        m_talkgroup_attachments->remove(dmrId);
        createSubscriptionList();
        unsubscribeNetworkTG(old_tgs);
    }

    return existing_user;
}

void Controller::updateSubscriptions(QList<unsigned int> tg_list, unsigned int srcId)
{
    m_talkgroup_attachments->insert(srcId, tg_list);
    QList<unsigned int> old_tgs(m_subscribed_talkgroups->begin(), m_subscribed_talkgroups->end());
    createSubscriptionList();
    subscribeNetworkTG(old_tgs);
    unsubscribeNetworkTG(old_tgs);
}

void Controller::createSubscriptionList()
{
    m_subscribed_talkgroups->clear();
    QMapIterator<unsigned int, QList<unsigned int>> it(*m_talkgroup_attachments);

    while (it.hasNext()) {
        it.next();
        m_subscribed_talkgroups->unite(QSet<unsigned int> (it.value().begin(), it.value().end()));
    }

    if (!m_settings->headless_mode) {
        QSet<unsigned int>* talkgroups = new QSet<unsigned int>(*m_subscribed_talkgroups);
        emit updateTalkgroupSubscriptionList(talkgroups);
    }
}

void Controller::subscribeNetworkTG(QList<unsigned int> old_tgs)
{
    if (!m_settings->use_trunking_protocol)
        return;

    QList<unsigned int> new_tg_list(m_subscribed_talkgroups->begin(), m_subscribed_talkgroups->end());
    QSet<unsigned int> reg_diff = QSet<unsigned int>(new_tg_list.begin(), new_tg_list.end()) - QSet<unsigned int>(old_tgs.begin(), old_tgs.end());
    QList<unsigned int> tg_list(reg_diff.begin(), reg_diff.end());

    QList<unsigned int> new_tg;
    bool result = m_gateway_router->getTrunkingSubscriptions(tg_list, new_tg);

    for (unsigned int i = 0U; i < new_tg.size(); i++) {
        m_logger->log(Logger::LogLevelInfo, QString("Requesting talkgroup subscription from network for TG %1")
                      .arg(new_tg.at(i)));
    }

    if (result && (new_tg.size() > 0U)) {
        CDMRData tg_sub_message;
        m_network_signalling->createGroupSubscriptionMessage(tg_sub_message, new_tg);
        m_control_channel->putNetQueue(tg_sub_message);
    }
}

void Controller::unsubscribeNetworkTG(QList<unsigned int> old_tgs)
{
    if (!m_settings->use_trunking_protocol)
        return;

    QList<unsigned int> new_tg_list(m_subscribed_talkgroups->begin(), m_subscribed_talkgroups->end());
    QSet<unsigned int> dereg_diff = QSet<unsigned int>(old_tgs.begin(), old_tgs.end()) - QSet<unsigned int>(new_tg_list.begin(), new_tg_list.end());
    QList<unsigned int> tg_list(dereg_diff.begin(), dereg_diff.end());
    QList<unsigned int> new_tg;
    bool result = m_gateway_router->getTrunkingUnSubscriptions(tg_list, new_tg);

    for (unsigned int i = 0; i < new_tg.size(); i++) {
        m_logger->log(Logger::LogLevelInfo, QString("Requesting talkgroup unsubscription from network for TG %1")
                      .arg(new_tg.at(i)));
    }

    if (result && (new_tg.size() > 0U)) {
        CDMRData tg_unsub_message;
        m_network_signalling->createGroupUnSubscriptionMessage(tg_unsub_message, new_tg);
        m_control_channel->putNetQueue(tg_unsub_message);
    }
}

void Controller::subscribeStaticTalkgroups()
{
    unsigned int gateway_id = 0;
    bool trunked_gw = m_gateway_router->getTrunkingGateway(gateway_id);

    if (trunked_gw && m_settings->use_trunking_protocol) {
        /// Subscribe static talkgroups and request re-subscriptions
        if (m_settings->subscribe_static_tgs) {
            CDMRData tg_sub_message;
            QList<unsigned int> static_tgs;
            QList<unsigned int> prefix_removed_static_tgs;
            bool result = m_gateway_router->getStaticTgList(static_tgs);

            if (result) {
                for (unsigned int i = 0U; i < static_tgs.size(); i++) {
                    unsigned int static_tg_id = static_tgs.at(i);
                    m_gateway_router->removeTalkgroupPrefix(static_tg_id, gateway_id);
                    prefix_removed_static_tgs.append(static_tg_id);
                }

                m_network_signalling->createGroupSubscriptionMessage(tg_sub_message, prefix_removed_static_tgs);
                m_control_channel->putNetQueue(tg_sub_message);
                m_logger->log(Logger::LogLevelDebug, QString("Requesting static talkgroup subscriptions"));
            }
        }

        CDMRData tg_sub_message;
        QList<unsigned int> resub_tgs;
        QList<unsigned int> prefix_removed_net_tgs;
        bool result = m_gateway_router->getNetSubscriptions(resub_tgs);

        if (result) {
            for (unsigned int i = 0U; i < resub_tgs.size(); i++) {
                unsigned int resub_tg_id = resub_tgs.at(i);
                m_gateway_router->removeTalkgroupPrefix(resub_tg_id, gateway_id);
                prefix_removed_net_tgs.append(resub_tg_id);
            }

            m_network_signalling->createGroupSubscriptionMessage(tg_sub_message, prefix_removed_net_tgs);
            m_control_channel->putNetQueue(tg_sub_message);
            m_logger->log(Logger::LogLevelDebug, QString("Requesting re-subscription of already assigned network subscriptions"));
        }
    }
}

void Controller::cleanupSubscriptions()
{
    /// Unubscribe static talkgroups
    unsigned int gateway_id = 0;
    bool trunked_gw = m_gateway_router->getTrunkingGateway(gateway_id);

    if (!trunked_gw)
        return;

    if (m_settings->use_trunking_protocol && m_settings->subscribe_static_tgs) {
        CDMRData tg_unsub_message;
        QList<unsigned int> static_tgs;
        QList<unsigned int> prefix_removed_static_tgs;
        bool result = m_gateway_router->getStaticTgList(static_tgs);

        if (result) {
            for (unsigned int i = 0U; i < static_tgs.size(); i++) {
                unsigned int static_tg_id = static_tgs.at(i);
                m_gateway_router->removeTalkgroupPrefix(static_tg_id, gateway_id);
                prefix_removed_static_tgs.append(static_tg_id);
            }

            m_network_signalling->createGroupUnSubscriptionMessage(tg_unsub_message, prefix_removed_static_tgs);
            m_control_channel->putNetQueue(tg_unsub_message);
        }
    }

    if (m_settings->use_trunking_protocol) {
        CDMRData net_tg_unsub_message;
        QList<unsigned int> sub_tgs;
        QList<unsigned int> prefix_removed_net_tgs;
        bool result = m_gateway_router->getNetSubscriptions(sub_tgs);

        if (result) {
            for (unsigned int i = 0U; i < sub_tgs.size(); i++) {
                unsigned int sub_tg_id = sub_tgs.at(i);
                m_gateway_router->removeTalkgroupPrefix(sub_tg_id, gateway_id);
                prefix_removed_net_tgs.append(sub_tg_id);
            }

            m_network_signalling->createGroupUnSubscriptionMessage(net_tg_unsub_message, prefix_removed_net_tgs);
            m_control_channel->putNetQueue(net_tg_unsub_message);
        }
    }

    if (m_settings->use_trunking_protocol && m_settings->send_network_registrations) {
        // deregister MSs from network
        for (unsigned int i = 0U; i < m_registered_ms->size(); i++) {
            CDMRData deregister_message;
            m_network_signalling->createDeRegistrationMessage(deregister_message, m_registered_ms->at(i));
            m_control_channel->putNetQueue(deregister_message);
        }
    }
}

LogicalChannel* Controller::findNextFreePayloadChannel(unsigned int dstId, unsigned int srcId, bool local)
{
    for (unsigned int i = 0U; i < m_logical_channels.size(); i++) {
        if (!(m_logical_channels[i]->isControlChannel())
            && !(m_logical_channels[i]->getDisabled())
            && !(m_logical_channels[i]->getBusy())) {
            return m_logical_channels[i];
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
    unsigned int incoming_priority = m_settings->call_priorities.value(dstId, 0);

    if (incoming_priority == 0U)
        return nullptr;

    for (unsigned int priority = 0U; priority < 3; priority++) {
        for (unsigned int i = 0U; i < m_logical_channels.size(); i++) {
            if (!(m_logical_channels[i]->isControlChannel())
                && !(m_logical_channels[i]->getDisabled()) && !(m_logical_channels[i]->getLocalCall())) {
                unsigned int existing_call_priority = m_settings->call_priorities.value(m_logical_channels[i]->getDestination(), 0);

                if ((existing_call_priority == priority) && (incoming_priority > existing_call_priority)) {
                    m_logger->log(Logger::LogLevelInfo, QString("Tearing down existing call to %1 to prioritize call from %2 towards %3")
                                  .arg(m_logical_channels[i]->getDestination())
                                  .arg(srcId)
                                  .arg(dstId));
                    /* // TODO
                    if(m_logical_channels[i]->getLocalCall())
                    {
                        CDMRData dmr_control_data;
                        dmr_control_data.setSlotNo(m_logical_channels[i]->getSlot());
                        dmr_control_data.setControl(true);
                        dmr_control_data.setCommand(DMRCommand::RCCeaseTransmission);
                        m_logical_channels[i]->putRFQueue(dmr_control_data);
                        return nullptr;
                    }
                    */

                    m_logical_channels[i]->setDestination(0);
                    m_logical_channels[i]->clearNetQueue();
                    m_logical_channels[i]->clearRFQueue();


                    if (local) {
                        CDMRCSBK csbk;
                        m_signalling_generator->createReplyWaitForSignalling(csbk, srcId);

                        for (unsigned int i = 0U; i < 3U; i++) {
                            transmitCSBK(csbk, nullptr, m_control_channel->getSlot(), m_control_channel->getPhysicalChannel(),
                                         false, false);
                        }
                    }

                    return m_logical_channels[i];
                }
            }
        }
    }

    return nullptr;
}

LogicalChannel* Controller::findCallChannel(unsigned int dstId, unsigned int srcId, bool dst_only)
{
    for (unsigned int i = 0U; i < m_logical_channels.size(); i++) {
        if ((!m_logical_channels[i]->isControlChannel()) && (m_logical_channels[i]->getDestination() == dstId)
            && m_logical_channels[i]->getBusy()
            && !(m_logical_channels[i]->getDisabled())) {
            return m_logical_channels[i];
        }

        if ((!m_logical_channels[i]->isControlChannel()) && ((m_logical_channels[i]->getDestination() == dstId) ||
                                                             (m_logical_channels[i]->getDestination() == srcId))
            && m_logical_channels[i]->getBusy()
            && !(m_logical_channels[i]->getDisabled()) && !dst_only) {
            return m_logical_channels[i];
        }
    }

    return nullptr;
}

LogicalChannel* Controller::findChannelByPhysicalIdAndSlot(unsigned int physical_id, unsigned int slot)
{
    for (unsigned int i = 0U; i < m_logical_channels.size(); i++) {
        if ((m_logical_channels[i]->getPhysicalChannel() == physical_id) && (m_logical_channels[i]->getSlot() == slot)) {
            return m_logical_channels[i];
        }
    }

    return nullptr;
}

QVector<LogicalChannel*> Controller::findActiveChannels()
{
    QVector<LogicalChannel*> active_channels;

    for (unsigned int i = 0U; i < m_logical_channels.size(); i++) {
        if ((!m_logical_channels[i]->isControlChannel())
            && m_logical_channels[i]->getBusy()
            && !(m_logical_channels[i]->getDisabled())) {
            active_channels.append(m_logical_channels[i]);
        }
    }

    return active_channels;
}

LogicalChannel* Controller::getControlOrAlternateChannel()
{
    if (m_settings->control_channel_slot == 1) {
        return m_control_channel;
    } else {
        return m_control_channel_alternate;
    }
}

void Controller::enableLogicalChannel(LogicalChannel*& logical_channel)
{
    CDMRData dmr_control_data;
    dmr_control_data.setCommand(DMRCommand::ChannelEnableDisable);
    dmr_control_data.setControl(true);
    dmr_control_data.setChannelEnable(true);
    dmr_control_data.setSlotNo(logical_channel->getSlot());
    logical_channel->putRFQueue(dmr_control_data, false);
}

void Controller::disableLogicalChannel(LogicalChannel*& logical_channel)
{
    CDMRData dmr_control_data;
    dmr_control_data.setControl(true);
    dmr_control_data.setCommand(DMRCommand::ChannelEnableDisable);
    dmr_control_data.setChannelEnable(false);
    dmr_control_data.setSlotNo(logical_channel->getSlot());
    logical_channel->putRFQueue(dmr_control_data, false);
}

void Controller::inputNetDMRPayload(unsigned char* payload, unsigned int size, unsigned int udp_channel_id, bool from_gateway)
{
    bool uuid_present = (size == HOMEBREW_DATA_PACKET_LENGTH) ? false : true;
    unsigned char seqNo = payload[4U];
    unsigned int srcId = (payload[5U] << 16) | (payload[6U] << 8) | (payload[7U] << 0);
    unsigned int dstId = (payload[8U] << 16) | (payload[9U] << 8) | (payload[10U] << 0);
    unsigned int slotNo = (payload[15U] & 0x80U) == 0x80U ? 2U : 1U;
    unsigned int streamId = 0U;
    unsigned char ber = payload[53U];
    unsigned char rssi = payload[54U];
    ::memcpy(&streamId, payload + 16U, 4U);
    unsigned char uuid[16U];
    ::memset(uuid, 0, 16U);

    if (uuid_present && (size == HOMEBREW_DATA_PACKET_LENGTH + 16U)) {
        ::memcpy(uuid, payload + 55U, 16U);
        crc_t crc = crc32_init();
        crc = crc32_update(crc, (const unsigned char*)uuid, 16U);
        crc = crc32_finalize(crc);
        streamId = crc;
    }

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

    if (uuid_present)
        dmr_data.setUUID(uuid);

    // if trunking protocol, add prefix as defined
    //if(from_gateway)
    //    m_dmr_rewrite->addTalkgroupPrefix(dmr_data, udp_channel_id);

    bool dataSync = (payload[15U] & 0x20U) == 0x20U;
    bool voiceSync = (payload[15U] & 0x10U) == 0x10U;

    if (dataSync) {
        unsigned char dataType = payload[15U] & 0x0FU;
        dmr_data.setData(payload + 20U);
        dmr_data.setDataType(dataType);
        dmr_data.setN(0U);

        if (dataType == DT_CSBK && !from_gateway) {
            // skip network csbk for now
            processSignalling(dmr_data, udp_channel_id);
        } else if (((dataType == DT_DATA_HEADER) ||
                    (dataType == DT_RATE_12_DATA) ||
                    (dataType == DT_RATE_1_DATA) ||
                    (dataType == DT_RATE_34_DATA))
                  ) {
            processData(dmr_data, udp_channel_id, from_gateway);
        } else if (dataType == DT_CSBK && from_gateway) {
            processNetworkCSBK(dmr_data, udp_channel_id);
        } else {
            if (!from_gateway && !validateLocalSourceId(srcId)) {
                delete[] payload;
                return;
            }

            processVoice(dmr_data, udp_channel_id, true, from_gateway);
        }
    } else if (voiceSync) {
        if (!from_gateway && !validateLocalSourceId(srcId)) {
            delete[] payload;
            return;
        }

        dmr_data.setData(payload + 20U);
        dmr_data.setDataType(DT_VOICE_SYNC);
        dmr_data.setN(0U);
        processVoice(dmr_data, udp_channel_id, false, from_gateway);
    } else {
        if (!from_gateway && !validateLocalSourceId(srcId)) {
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

void Controller::processTalkgroupSubscriptionsMessage(unsigned int srcId, unsigned int slotNo, DMRMessageHandler::data_message* dmessage,
                                                      unsigned int udp_channel_id)
{
    unsigned int size = dmessage->size * 12 - dmessage->pad_nibble / 2 - 2;
    unsigned char msg[48U];
    memcpy(msg, dmessage->message, size);
    m_ack_handler->removeAck(srcId, ServiceAction::RegistrationWithAttachment);
    bool existing_user = userRegister(srcId);

    if (!m_settings->headless_mode) {
        QList<unsigned int>* registered_ms = new QList<unsigned int>(*m_registered_ms);
        emit updateRegisteredMSList(registered_ms);
    }

    CDMRCSBK csbk;
    m_signalling_generator->createReplyRegistrationAccepted(csbk, srcId);
    transmitCSBK(csbk, nullptr, slotNo, udp_channel_id, false, false);

    if (!existing_user && m_settings->announce_system_message) {
        QVector<LogicalChannel*> active_calls = findActiveChannels();
        QList<QString> messages;
        messages.append(QString("Welcome %1").arg(m_id_lookup->getCallsign(srcId)));
        messages.append(QString("Users - %1, channels - %2, active calls - %3")
                        .arg(m_registered_ms->size())
                        .arg(m_settings->logical_physical_channels.size())
                        .arg(active_calls.size()));
        messages.append(QString("Send SMS to %1 for command help")
                        .arg(m_settings->service_ids.value("help", 0)));
        (void)QtConcurrent::run(&Controller::sendUDTMultipartMessage, this, messages, srcId, StandardAddreses::DISPATI, false, 2);
    }

    QList<unsigned int> tg_list;

    for (unsigned int i = 1; i < size; i = i + 3) {
        unsigned int tg = 0;
        tg |= msg[i] << 16;
        tg |= msg[i + 1] << 8;
        tg |= msg[i + 2];

        if (tg == 0)
            continue;

        unsigned int converted_id = TrunkingUtils::convertBase11GroupNumberToBase10(tg);
        tg_list.append(converted_id);
        m_logger->log(Logger::LogLevelInfo, QString("Received talkgroup attachment data from %1: %2")
                      .arg(srcId).arg(converted_id));
    }

    m_control_channel->setText(QString("Talkgroup attachment message: %1").arg(srcId));

    if (!m_settings->headless_mode) {
        emit updateLogicalChannels(&m_logical_channels);
    }

    updateSubscriptions(tg_list, srcId);
}

void Controller::processCallDivertMessage(unsigned int srcId, unsigned int slotNo, DMRMessageHandler::data_message* dmessage,
                                          unsigned int udp_channel_id)
{
    unsigned int size = dmessage->size * 12 - dmessage->pad_nibble / 2 - 2;
    unsigned char msg[48U];
    memcpy(msg, dmessage->message, size);
    m_ack_handler->removeAck(srcId, ServiceAction::CallDivert);

    unsigned int divert_id = 0;
    divert_id |= msg[1] << 16;
    divert_id |= msg[2] << 8;
    divert_id |= msg[3];

    if (divert_id == 0) {
        CDMRCSBK csbk;
        m_signalling_generator->createReplyUDTCRCError(csbk, srcId);
        transmitCSBK(csbk, nullptr, slotNo, udp_channel_id, false, false);
        m_logger->log(Logger::LogLevelInfo, QString("Received call divert with target 0, ignoring"));
        return;
    }

    CDMRCSBK csbk;
    m_signalling_generator->createReplyCallDivertAccepted(csbk, srcId);
    transmitCSBK(csbk, nullptr, slotNo, udp_channel_id, false, false);
    m_logger->log(Logger::LogLevelInfo, QString("Received call divert data from %1: %2")
                  .arg(srcId).arg(divert_id));

    if (m_settings->announce_system_message) {
        QString message = QString("Calls to %1 are now diverted to %2").arg(m_id_lookup->getCallsign(srcId)).arg(divert_id);
        sendUDTShortMessage(message, srcId);
    }

    m_settings->call_diverts.insert(srcId, divert_id);
    m_control_channel->setText(QString("Call divert message: %1").arg(srcId));

    if (!m_settings->headless_mode) {
        emit updateLogicalChannels(&m_logical_channels);
    }
}

void Controller::processNMEAMessage(unsigned int srcId, unsigned int dstId, DMRMessageHandler::data_message* dmessage)
{
    unsigned int size = dmessage->size * 12 - dmessage->pad_nibble / 2 - 2;
    unsigned char msg[48U];
    memcpy(msg, dmessage->message, size);
    QList<QString> messages = TrunkingUtils::readNMEA(msg, dmessage->size);
    QStringList allmsg(messages);
    m_logger->log(Logger::LogLevelInfo, QString("Received NMEA UDT location message from %1 to %2: %3")
                  .arg(srcId)
                  .arg(dstId)
                  .arg(allmsg.join(", ")));

    if (dstId != StandardAddreses::SDMI) {
        sendUDTMultipartMessage(messages, dstId, srcId, false, 1);
    }

    if (!m_settings->headless_mode) {
        if (dstId == StandardAddreses::SDMI)
            emit positionResponse(srcId, allmsg.join(", "));

        emit updateMessageLog(srcId, dstId, allmsg.join(", "), false);
    }

    m_control_channel->setText(QString("Position message: %1").arg(srcId));

    if (!m_settings->headless_mode) {
        emit updateLogicalChannels(&m_logical_channels);
    }
}

void Controller::processTextMessage(unsigned int dstId, unsigned int srcId,
                                    DMRMessageHandler::data_message* dmessage, bool group, bool from_gateway)
{
    if (group || dmessage->group)
        dstId = TrunkingUtils::convertBase11GroupNumberToBase10(dstId);

    if ((dmessage->udt_format == 4) || (dmessage->udt_format == 3) || (dmessage->udt_format == 7)) {
        unsigned int size = dmessage->size * 12 - dmessage->pad_nibble / 2 - 2; // size does not include CRC16
        unsigned char msg[48U];
        memcpy(msg, dmessage->message, size);
        QString text_message;

        // last character seems to be null termination
        if (dmessage->udt_format == 4)
            text_message = QString::fromUtf8((const char*)msg, size - 1).trimmed();
        else if (dmessage->udt_format == 3) {
            unsigned int bit7_size = 8 * size / 7;
            unsigned char converted[96U];
            ::memset(converted, 0U, 96U);
            TrunkingUtils::parseISO7bitToISO8bit(msg, converted, bit7_size, size);
            text_message = QString::fromUtf8((const char*)converted, bit7_size - 1).trimmed();
        } else if (dmessage->udt_format == 7) {
            TrunkingUtils::parseUTF16(text_message, size - 1, msg);
            text_message = text_message.trimmed();
        }

        sendUDTShortMessage(text_message, dstId, srcId, group);

        if (group) {
            m_logger->log(Logger::LogLevelInfo, QString("Received group UDT short data message from %1 to %2: %3")
                          .arg(srcId)
                          .arg(dstId)
                          .arg(text_message));
        } else {
            m_logger->log(Logger::LogLevelInfo, QString("Received private UDT short data message from %1 to %2: %3")
                          .arg(srcId)
                          .arg(dstId)
                          .arg(text_message));
        }

        if (!from_gateway && m_settings->use_trunking_protocol && (group || !m_registered_ms->contains(dstId))) {
            CDMRData text_message_data;
            m_network_signalling->createUDTTransferMessage(text_message_data, srcId, dstId, text_message, 4, group); // TODO: format
            m_control_channel->putNetQueue(text_message_data);
            m_logger->log(Logger::LogLevelDebug, QString("Sending UDT message to network"));
        }

        if (!m_settings->headless_mode) {
            if (group) {
                emit updateMessageLog(srcId, dstId, text_message, true);
            } else {
                emit updateMessageLog(srcId, dstId, text_message, false);
            }
        }

        m_control_channel->setText(QString("Short text message: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
}

void Controller::processDigits(unsigned int dstId, unsigned int srcId,
                               DMRMessageHandler::data_message* dmessage, bool group)
{
    if (group || dmessage->group)
        dstId = TrunkingUtils::convertBase11GroupNumberToBase10(dstId);

    if (dmessage->udt_format == 2) {
        unsigned int dialId = TrunkingUtils::parseBCDDigits(dmessage->message, dmessage->size, dmessage->pad_nibble);
        QString text_message = QString("Received call digits via UDT from %1 to %2: %3")
                               .arg(srcId)
                               .arg(dstId)
                               .arg(dialId);
        /// poke in PBX call logic
        ///
        ///

        m_logger->log(Logger::LogLevelInfo, text_message);

        if (!m_settings->headless_mode) {
            emit updateMessageLog(srcId, dstId, text_message, group);
        }

        m_control_channel->setText(QString("%1 uploaded PABX call digits: %2").arg(srcId).arg(dialId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }

        CDMRCSBK csbk_wait;
        m_signalling_generator->createReplyCallQueued(csbk_wait, srcId);
        transmitCSBK(csbk_wait, nullptr, m_control_channel->getSlot(),
                     m_control_channel->getPhysicalChannel(), false, false);
    }
}

void Controller::confirmPDPMessageReception(unsigned int srcId, unsigned int slotNo,
                                            DMRMessageHandler::data_message* dmessage, unsigned int udp_channel_id)
{
    QVector<CDMRData> dmr_data_frames;
    LogicalChannel* data_rcv_channel = findChannelByPhysicalIdAndSlot(udp_channel_id, slotNo);
    unsigned int blocks = 1;
    CDMRData dmr_response = m_signalling_generator->createConfirmedMessageResponseHeader(
                                StandardAddreses::HDATA_GW, srcId, dmessage->seq_no,
                                blocks, dmessage->sap, false, dmessage->missed_blocks);
    dmr_response.setSlotNo(slotNo);
    dmr_data_frames.append(dmr_response);

    for (uint8_t i = 0; i < blocks; i++) {
        CDMRData dmr_data = m_signalling_generator->createConfirmedDataResponsePayload(
                                StandardAddreses::HDATA_GW, srcId, dmessage->missed_blocks, i);
        dmr_data.setSlotNo(slotNo);
        dmr_data_frames.append(dmr_data);
    }

    CDMRData terminator = m_signalling_generator->createDataTerminatorLC(
                              StandardAddreses::HDATA_GW, srcId, dmessage->group, 1, 1, dmessage->seq_no);

    for (unsigned int i = 0U; i < 10U; i++) {
        dmr_data_frames.append(terminator);
    }

    data_rcv_channel->putRFQueueMultiItem(dmr_data_frames);
    data_rcv_channel->setText(QString("Response confirmation to: %1").arg(srcId));
}

void Controller::replayPacketData(unsigned int srcId, unsigned int dstId, unsigned int slotNo)
{
    CDMRCSBK csbk;
    bool channel_grant = true;
    LogicalChannel* logical_channel = nullptr;
    handlePrivatePacketDataCallRequest(csbk, logical_channel, slotNo,
                                       StandardAddreses::HDATA_GW, dstId, channel_grant, true);
    transmitCSBK(csbk, logical_channel, logical_channel->getSlot(), m_control_channel->getPhysicalChannel(), channel_grant, false);
    CDMRCSBK csbk2;
    bool valid = false;

    if ((csbk.getCSBKO() == CSBKO_PD_GRANT) || (csbk.getCSBKO() == CSBKO_PD_GRANT_MI))
        valid = m_signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);

    if (valid) {
        transmitCSBK(csbk2, logical_channel, logical_channel->getSlot(), m_control_channel->getPhysicalChannel(),
                     channel_grant, false);
    }

    m_logger->log(Logger::LogLevelInfo, QString("Sending packet data"
                                                " from %1, slot %2 to destination %3")
                  .arg(srcId).arg(slotNo).arg(dstId));
    QVector<CDMRData>* dmr_data_frames = m_dmr_message_handler->getDataFromBuffer(srcId);

    if (dmr_data_frames != nullptr) {
        QVector<CDMRData> dmr_message_frames;

        for (unsigned int i = 0U; i < dmr_data_frames->size(); i++) {
            CDMRData dmr_outbound_data = dmr_data_frames->at(i);
            dmr_outbound_data.setSrcId(StandardAddreses::HDATA_GW);
            dmr_outbound_data.setDstId(dstId);
            dmr_outbound_data.setSlotNo(logical_channel->getSlot());
            dmr_message_frames.append(dmr_outbound_data);
        }

        logical_channel->putRFQueueMultiItem(dmr_message_frames);
        m_dmr_message_handler->clearDataBuffer(srcId);
    }
}

void Controller::processDataProtocolMessage(unsigned int dstId, unsigned int srcId,
                                            DMRMessageHandler::data_message* dmessage,
                                            unsigned int udp_channel_id, unsigned int slotNo, bool from_gateway)
{
    if (dmessage->udt == false) {
        srcId = dmessage->real_src;
        dstId = dmessage->group ? TrunkingUtils::convertBase11GroupNumberToBase10(dmessage->real_dst) : dmessage->real_dst;
        QString text_message;
        text_message = QString::fromUtf8((const char*)dmessage->payload, dmessage->payload_len).trimmed();
        confirmPDPMessageReception(srcId, slotNo, dmessage, udp_channel_id);

        ///** Sending the message on the control channel
        int size = text_message.size();

        if (size > 0) {
            sendUDTShortMessage(text_message, dstId, srcId, dmessage->group);
        }

        //*/

        ///** TODO: replay received packet data on dedicated channel
        //replayPacketData(srcId, dstId, slotNo);

        if (dmessage->group) {
            m_logger->log(Logger::LogLevelInfo, QString("Received group data protocol message from %1 to %2: %3")
                          .arg(srcId)
                          .arg(dstId)
                          .arg(text_message));
        } else {
            m_logger->log(Logger::LogLevelInfo, QString("Received private data protocol message from %1 to %2: %3")
                          .arg(srcId)
                          .arg(dstId)
                          .arg(text_message));
        }

        if (!from_gateway && m_settings->use_trunking_protocol && !m_registered_ms->contains(dstId)) {
            int num_msg = text_message.size() / 46;

            for (int i = 0; i <= num_msg; i++) {
                QString msg = text_message.mid(i * 46, 46);
                CDMRData text_message_data;
                m_network_signalling->createUDTTransferMessage(text_message_data, srcId, dstId, msg, 4, dmessage->group); // TODO: format
                m_control_channel->putNetQueue(text_message_data);
                m_logger->log(Logger::LogLevelDebug, QString("Sending packet data protocol message chunk %1 to network").arg(i));
            }
        }

        if (!m_settings->headless_mode) {
            if (dmessage->group) {
                emit updateMessageLog(srcId, dstId, text_message, true);
            } else {
                emit updateMessageLog(srcId, dstId, text_message, false);
            }
        }

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
}

void Controller::processUDPProtocolMessage(unsigned int dstId, unsigned int srcId,
                                           DMRMessageHandler::data_message* dmessage, bool from_gateway)
{
    dstId = (dmessage->group && !from_gateway) ? TrunkingUtils::convertBase11GroupNumberToBase10(dstId) : dstId;
    QString text_message;

    for (uint i = 0; i < dmessage->payload_len; i++) {
        char x = dmessage->payload[i];

        if (x != 0x00) {
            text_message.append(QChar(x));
        }
    }

    text_message = text_message.trimmed();

    ///** Sending the message on the control channel
    int size = text_message.size();

    if (size > 0) {
        sendUDTShortMessage(text_message, dstId, srcId, dmessage->group);
    }

    if (dmessage->group) {
        m_logger->log(Logger::LogLevelInfo, QString("Received group UDP protocol message from %1 to %2: %3")
                      .arg(srcId)
                      .arg(dstId)
                      .arg(text_message));
    } else {
        m_logger->log(Logger::LogLevelInfo, QString("Received private UDP protocol message from %1 to %2: %3")
                      .arg(srcId)
                      .arg(dstId)
                      .arg(text_message));
    }

    if (!m_settings->headless_mode) {
        if (dmessage->group) {
            emit updateMessageLog(srcId, dstId, text_message, true);
        } else {
            emit updateMessageLog(srcId, dstId, text_message, false);
        }
    }

    if (!m_settings->headless_mode) {
        emit updateLogicalChannels(&m_logical_channels);
    }
}

bool Controller::processTextServiceRequest(CDMRData& dmr_data, DMRMessageHandler::data_message* dmessage, unsigned int udp_channel_id)
{
    /// Used for testing and debug purposes
    ///
    unsigned int dstId = dmr_data.getDstId();
    unsigned int srcId = dmr_data.getSrcId();

    /// Help
    if (dstId == (unsigned int)m_settings->service_ids.value("help", 0)) {
        CDMRCSBK csbk;
        m_signalling_generator->createReplyMessageAccepted(csbk, srcId, dstId, false);
        transmitCSBK(csbk, nullptr, dmr_data.getSlotNo(), udp_channel_id, false, false);
        QList<QString> messages;
        messages.append(QString("Available commands"));
        QMapIterator<QString, unsigned int> it(m_settings->service_ids);

        while (it.hasNext()) {
            it.next();
            messages.append(QString("%1 - %2").arg(it.key()).arg(it.value()));
        }

        if (messages.size() < 2) {
            messages.append(QString("The site does not offer any user services"));
        }

        (void)QtConcurrent::run(&Controller::sendUDTMultipartMessage, this, messages, srcId, m_settings->service_ids.value("help", StandardAddreses::SDMI), false, 5);
        m_control_channel->setText(QString("Help message: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
            emit updateMessageLog(srcId, dstId, QString("help"), false);
        }

        return true;
    }
    /// Location query ???
    else if (dstId == (unsigned int)m_settings->service_ids.value("location", 0)) {
        CDMRCSBK csbk;
        m_signalling_generator->createReplyMessageAccepted(csbk, srcId, dstId, false);
        transmitCSBK(csbk, nullptr, dmr_data.getSlotNo(), udp_channel_id, false, false);
        QString text_message;

        if ((dmessage->udt_format == 4) || (dmessage->udt_format == 3) || (dmessage->udt_format == 7)) {
            unsigned int size = dmessage->size * 12 - dmessage->pad_nibble / 2 - 2; // size does not include CRC16
            unsigned char msg[48U];
            memcpy(msg, dmessage->message, size);

            // last character seems to be null termination
            if (dmessage->udt_format == 4)
                text_message = QString::fromUtf8((const char*)msg, size - 1).trimmed();
            else if (dmessage->udt_format == 3) {
                unsigned int bit7_size = 8 * size / 7;
                unsigned char converted[96U];
                ::memset(converted, 0U, 96U);
                TrunkingUtils::parseISO7bitToISO8bit(msg, converted, bit7_size, size);
                text_message = QString::fromUtf8((const char*)converted, bit7_size - 1).trimmed();
            } else if (dmessage->udt_format == 7) {
                TrunkingUtils::parseUTF16(text_message, size - 1, msg);
                text_message = text_message.trimmed();
            }

            if (text_message.size() > 0) {
                bool ok = false;
                unsigned int target_id = text_message.toUInt(&ok);

                if (ok && (target_id > 0) && (target_id < 0xFFFFFFF))
                    pollData(target_id, PollFMT::PollNMEA, srcId);
            }
        }

        m_control_channel->setText(QString("Location query message: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
            emit updateMessageLog(srcId, dstId, text_message, false);
        }

        return true;
    }
    /// Signal report request
    else if (dstId == (unsigned int)m_settings->service_ids.value("signal_report", 0)) {
        CDMRCSBK csbk;
        m_signalling_generator->createReplyMessageAccepted(csbk, srcId, dstId, false);
        transmitCSBK(csbk, nullptr, dmr_data.getSlotNo(), udp_channel_id, false, false);
        (void)QtConcurrent::run(&Controller::sendRSSIInfo, this, dmessage->rssi, dmessage->ber, srcId);
        return true;
    }
    /// DGNA
    else if (dstId == (unsigned int)m_settings->service_ids.value("dgna", 0)) {
        CDMRCSBK csbk;
        m_signalling_generator->createReplyMessageAccepted(csbk, srcId, dstId, false);
        transmitCSBK(csbk, nullptr, dmr_data.getSlotNo(), udp_channel_id, false, false);

        if ((dmessage->udt_format == 4) || (dmessage->udt_format == 3) || (dmessage->udt_format == 7)) {
            unsigned int size = dmessage->size * 12 - dmessage->pad_nibble / 2 - 2; // size does not include CRC16
            unsigned char msg[48U];
            memcpy(msg, dmessage->message, size);
            QString text_message;

            // last character seems to be null termination
            if (dmessage->udt_format == 4)
                text_message = QString::fromUtf8((const char*)msg, size - 1).trimmed();
            else if (dmessage->udt_format == 3) {
                unsigned int bit7_size = 8 * size / 7;
                unsigned char converted[96U];
                ::memset(converted, 0U, 96U);
                TrunkingUtils::parseISO7bitToISO8bit(msg, converted, bit7_size, size);
                text_message = QString::fromUtf8((const char*)converted, bit7_size - 1).trimmed();
            } else if (dmessage->udt_format == 7) {
                TrunkingUtils::parseUTF16(text_message, size - 1, msg);
                text_message = text_message.trimmed();
            }

            if (text_message.size() > 0) {
                sendUDTDGNA(text_message, srcId);
            }

            m_control_channel->setText(QString("DGNA request message: %1").arg(srcId));

            if (!m_settings->headless_mode) {
                emit updateLogicalChannels(&m_logical_channels);
                emit updateMessageLog(srcId, dstId, text_message, false);
            }
        }

        return true;
    }

    return false;
}

void Controller::processData(CDMRData& dmr_data, unsigned int udp_channel_id, bool from_gateway)
{
    bool local_data = !from_gateway;
    bool forward_to_gw = false;
    unsigned int dstId = dmr_data.getDstId();
    unsigned int srcId = dmr_data.getSrcId();
    unsigned int dstIdRewritten = 0U;

    /// Rewriting destination to match DMR tier III flat numbering
    if (local_data) {
        if (dmr_data.getFLCO() == FLCO_GROUP)
            dstIdRewritten = TrunkingUtils::convertBase11GroupNumberToBase10(dmr_data.getDstId());
        else
            dstIdRewritten = dmr_data.getDstId();
    } else {
        dstIdRewritten = dmr_data.getDstId();
        m_dmr_rewrite->rewriteSource(dmr_data);
    }

    LogicalChannel* logical_channel;
    logical_channel = findCallChannel(dstIdRewritten, srcId);

    if (logical_channel != nullptr) {
        bool update_gui = false;

        if (logical_channel->getLocalCall() != local_data) {
            logical_channel->setLocalCall(local_data);
            update_gui = true;
        }

        if (logical_channel->getSource() != srcId) {
            logical_channel->setSource(srcId);
            update_gui = true;
        }

        if (logical_channel->getDestination() != dstIdRewritten) {
            logical_channel->setDestination(dstIdRewritten);
            update_gui = true;
        }

        logical_channel->startTimeoutTimer();
        logical_channel->startLastFrameTimer();
        logical_channel->updateStats(dmr_data);

        if (update_gui && !m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }

    DMRMessageHandler::data_message* message = nullptr;

    if (dmr_data.getFLCO() == FLCO_GROUP) {
        message = m_dmr_message_handler->processData(dmr_data, from_gateway);

        if (message != nullptr) {
            if (message->crc_valid) {
                if (message->udt) {
                    /// Location upload
                    if (message->udt_format == 5) {
                        forward_to_gw = false;
                        processNMEAMessage(srcId, dstId, message);
                    }

                    /// Text message
                    else if ((message->udt_format == 4) || (message->udt_format == 3) || (message->udt_format == 7)) {
                        forward_to_gw = true;
                        processTextMessage(dstId, srcId, message, dmr_data.getFLCO() == FLCO_GROUP, from_gateway);
                    }
                    /// Digits
                    else if ((message->udt_format == 2) && !from_gateway) {
                        forward_to_gw = false;
                        processDigits(dstId, srcId, message, dmr_data.getFLCO() == FLCO_GROUP);
                    }

                    m_logger->log(Logger::LogLevelDebug, QString("DMR Slot %1, received UDT data MS to TG from %2 to %3")
                                  .arg(dmr_data.getSlotNo()).arg(srcId).arg(TrunkingUtils::convertBase11GroupNumberToBase10(dstId)));

                    if (!from_gateway) {
                        CDMRCSBK csbk;
                        m_signalling_generator->createReplyMessageAccepted(csbk, dmr_data.getSrcId());
                        transmitCSBK(csbk, nullptr, dmr_data.getSlotNo(), udp_channel_id, false, true);
                    }
                } else if (message->sap == 4) {
                    forward_to_gw = true;
                    processUDPProtocolMessage(dstId, srcId, message, from_gateway);
                } else if (message->sap == 10 && message->type == 13) {
                    forward_to_gw = true;
                    processUDPProtocolMessage(dstId, srcId, message, from_gateway);
                } else {
                    forward_to_gw = true;
                    processDataProtocolMessage(dstId, srcId, message, udp_channel_id, dmr_data.getSlotNo(), from_gateway);
                }
            } else if (message->udt && !from_gateway) {
                CDMRCSBK csbk;
                m_signalling_generator->createReplyUDTCRCError(csbk, srcId);
                transmitCSBK(csbk, nullptr, dmr_data.getSlotNo(), udp_channel_id, false, false);
                m_logger->log(Logger::LogLevelWarning, QString("Invalid UDT message CRC16 from %1").arg(srcId));
            } else if ((message->type == DPF_CONFIRMED_DATA) && !from_gateway) {
                confirmPDPMessageReception(srcId, dmr_data.getSlotNo(), message, udp_channel_id);
            }
        }

    } else if (dmr_data.getFLCO() == FLCO_USER_USER) {
        message = m_dmr_message_handler->processData(dmr_data, from_gateway);

        if (message != nullptr) {
            if (message->crc_valid) {
                /// Talkgroup attachment list
                if (m_ack_handler->hasAck(srcId, ServiceAction::RegistrationWithAttachment) &&
                    (message->udt) &&
                    (message->udt_format == 1)) {
                    forward_to_gw = false;
                    processTalkgroupSubscriptionsMessage(srcId, dmr_data.getSlotNo(), message, udp_channel_id);
                }
                /// Talkgroup attachment list
                else if (m_ack_handler->hasAck(srcId, ServiceAction::CallDivert) &&
                         message->udt &&
                         (message->udt_format == 1)) {
                    forward_to_gw = false;
                    processCallDivertMessage(srcId, dmr_data.getSlotNo(), message, udp_channel_id);
                }
                /// Text message
                else {
                    if (message->udt) { // UDT message on control channel
                        if ((message->udt_format == 4) || (message->udt_format == 3) || (message->udt_format == 7)) {
                            if (m_settings->service_ids.values().contains(dstId)) {
                                forward_to_gw = false;
                                processTextServiceRequest(dmr_data, message, udp_channel_id);
                            } else {
                                forward_to_gw = true;
                                processTextMessage(dstId, srcId, message, false, from_gateway);
                            }

                        } else if ((message->udt_format == 2) && !from_gateway) {
                            forward_to_gw = false;
                            processDigits(dstId, srcId, message, false);
                        } else if (message->udt_format == 5) {
                            forward_to_gw = false;
                            processNMEAMessage(srcId, dstId, message);
                        }
                    } else if (message->sap == 4) {
                        forward_to_gw = true;
                        processUDPProtocolMessage(dstId, srcId, message, from_gateway);
                    } else if (message->sap == 10 && message->type == 13) {
                        forward_to_gw = true;
                        processUDPProtocolMessage(dstId, srcId, message, from_gateway);
                    } else {
                        forward_to_gw = true;
                        processDataProtocolMessage(dstId, srcId, message, udp_channel_id, dmr_data.getSlotNo(), from_gateway);
                    }
                }
            } else if (message->udt && !from_gateway) {
                CDMRCSBK csbk;
                m_signalling_generator->createReplyUDTCRCError(csbk, srcId);
                transmitCSBK(csbk, nullptr, dmr_data.getSlotNo(), udp_channel_id, false, false);
                m_logger->log(Logger::LogLevelWarning, QString("Invalid UDT message CRC16 from %1").arg(srcId));
            } else if ((message->type == DPF_CONFIRMED_DATA) && !from_gateway) {
                confirmPDPMessageReception(srcId, dmr_data.getSlotNo(), message, udp_channel_id);
            }
        }
    }

    if (message != nullptr)
        delete message;

    /// Rewriting destination to match DMR tier III flat numbering
    if (from_gateway) {

        if (dmr_data.getFLCO() == FLCO_GROUP) {
            if (m_settings->receive_tg_attach &&
                m_settings->transmit_subscribed_tg_only &&
                !m_subscribed_talkgroups->contains(dstId)) {
                // Do not transmit unsubscribed talkgroups if not configured to do so
                return;
            }

            dstId = TrunkingUtils::convertBase10ToBase11GroupNumber(dmr_data.getDstId());
            m_signalling_generator->rewriteUDTHeader(dmr_data, dstId);
        } else {
            if (m_settings->call_diverts.contains(dstId)) {
                dstId = m_settings->call_diverts.value(dstId);
                m_signalling_generator->rewriteUDTHeader(dmr_data, dstId);
            }
        }

        dmr_data.setDstId(dstId);
        dmr_data.setSlotNo(m_control_channel->getSlot());
        m_control_channel->putRFQueue(dmr_data);
    } else if (!from_gateway && forward_to_gw) {
        unsigned int lastId = dmr_data.getSrcId();
        QVector<CDMRData>* data_frames = m_dmr_message_handler->getDataFromBuffer(dmr_data.getSrcId());
        for(unsigned int i=0;i < data_frames->size();i++) {
            CDMRData forwarded_data = data_frames->at(i);
            if (forwarded_data.getFLCO() == FLCO_GROUP) {
                m_signalling_generator->rewriteUDTHeader(forwarded_data, dstIdRewritten);
                forwarded_data.setDstId(dstIdRewritten);
            } else {
                if (m_settings->call_diverts.contains(forwarded_data.getDstId())) {
                    dstId = m_settings->call_diverts.value(dstId);
                    m_signalling_generator->rewriteUDTHeader(forwarded_data, dstId);
                }
            }

            if ((forwarded_data.getFLCO() == FLCO_GROUP) || !m_registered_ms->contains(forwarded_data.getDstId())) {
                m_control_channel->putNetQueue(forwarded_data);
            }
        }
        m_dmr_message_handler->clearDataBuffer(lastId);
    }
}

void Controller::processVoice(CDMRData& dmr_data, unsigned int udp_channel_id,
                              bool data_sync, bool from_gateway)
{
    bool local_data = !from_gateway;
    unsigned int dstId, srcId;
    unsigned int dataType = dmr_data.getDataType();

    /// Rewriting destination to match DMR tier III flat numbering
    if (local_data) {
        if (dmr_data.getFLCO() == FLCO_GROUP)
            dstId = TrunkingUtils::convertBase11GroupNumberToBase10(dmr_data.getDstId());
        else
            dstId = dmr_data.getDstId();

        srcId = dmr_data.getSrcId();
    } else {
        dstId = dmr_data.getDstId();
        srcId = dmr_data.getSrcId();
        m_dmr_rewrite->rewriteSource(dmr_data);

        if (dmr_data.getFLCO() == FLCO_GROUP) {
            if (m_settings->receive_tg_attach &&
                m_settings->transmit_subscribed_tg_only &&
                !m_subscribed_talkgroups->contains(dstId)) {
                // Do not transmit unsubscribed talkgroups if not configured to do so
                return;
            }

            dmr_data.setDstId(TrunkingUtils::convertBase10ToBase11GroupNumber(dstId));
        }

        // rewrite RF header to use the converted destination id
        if (data_sync) {
            m_dmr_rewrite->rewriteLC(dmr_data);
        }
    }

    LogicalChannel* logical_channel;
    // First try to find an existing allocated channel with the same destination id
    logical_channel = findCallChannel(dstId, srcId);

    if (logical_channel != nullptr) {
        bool update_gui = false;

        if (logical_channel->getLocalCall() != local_data) {
            logical_channel->setLocalCall(local_data);
            update_gui = true;
        }

        if ((logical_channel->getSource() != srcId) && !logical_channel->getChannelLock(srcId, dataType)) {
            logical_channel->setSource(srcId);
            update_gui = true;
        }

        if (logical_channel->getDestination() != dstId) {
            logical_channel->setDestination(dstId);
            update_gui = true;
        }

        dmr_data.setSlotNo(logical_channel->getSlot());
        logical_channel->startTimeoutTimer();

        if (update_gui && !m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }

        if (local_data) {
            dmr_data.setDstId(dstId);
            m_dmr_rewrite->rewriteSlot(dmr_data);
            logical_channel->putNetQueue(dmr_data);
            return;
        } else {
            if (dmr_data.getFLCO() == FLCO_GROUP)
                dmr_data.setDstId(TrunkingUtils::convertBase10ToBase11GroupNumber(dstId));

            logical_channel->putRFQueue(dmr_data);
            return;
        }
    }

    if (local_data) {
        // local call setup needs to be made through TV_GRANT / PV_GRANT
        // we are probably here because we didn't yet get the P_CLEAR due to latency and the radio thinks
        // the GRANT is still valid
        if (dmr_data.getDataType() == DT_TERMINATOR_WITH_LC) {
            unsigned int call_type = (dmr_data.getFLCO() == FLCO_GROUP) ? CallType::CALL_TYPE_GROUP : CallType::CALL_TYPE_MS;
            handleLocalVoiceOnUnallocatedChannel(call_type, dmr_data.getSlotNo(), udp_channel_id);
        }

        return;
    }

    // Could not find an existing active channel
    // Next try to find a free payload channel to allocate
    logical_channel = findNextFreePayloadChannel(dstId, srcId, local_data);

    if (logical_channel == nullptr) {
        if (!m_rejected_calls->contains(dmr_data.getStreamId())) {
            m_rejected_calls->insert(dmr_data.getStreamId());
            m_logger->log(Logger::LogLevelWarning, "Could not find any free logical channels for net voice");

            if (!m_settings->headless_mode) {
                emit updateRejectedCallsList(srcId, dstId, false);
            }
        }
    } else {
        unsigned int call_type = (dmr_data.getFLCO() == FLCO_USER_USER) ? CallType::CALL_TYPE_MS : CallType::CALL_TYPE_GROUP;
        CDMRCSBK csbk_grant;
        bool channel_grant = true;
        bool priority = false;

        if (call_type == CallType::CALL_TYPE_MS) {
            if (m_settings->call_diverts.contains(dstId)) {
                dstId = m_settings->call_diverts.value(dstId);
            }

            if (m_registered_ms->contains(dstId) && !m_private_calls.contains(dstId)) {
                channel_grant = false;
                contactMSForVoiceCall(csbk_grant, dmr_data.getSlotNo(), srcId, dstId, false);
                transmitCSBK(csbk_grant, logical_channel, m_control_channel->getSlot(),
                             m_control_channel->getPhysicalChannel(), false, priority, true);
                return;
            }

            //handlePrivateCallRequest(csbk_grant, logical_channel, dmr_data.getSlotNo(), srcId, dstId, channel_grant, false);
        } else {
            handleGroupCallRequest(csbk_grant, logical_channel, dmr_data.getSlotNo(), srcId, dstId, channel_grant, false);

            CDMRCSBK csbk2;
            bool valid = false;

            if (csbk_grant.getCSBKO() == CSBKO_TV_GRANT)
                valid = m_signalling_generator->createAbsoluteParameters(csbk_grant, csbk2, logical_channel);

            transmitCSBK(csbk_grant, logical_channel, m_control_channel->getSlot(),
                         m_control_channel->getPhysicalChannel(), false, priority, true);

            if (valid) {
                transmitCSBK(csbk2, logical_channel, m_control_channel->getSlot(), m_control_channel->getPhysicalChannel(),
                             false, false, true);
            }

            dmr_data.setSlotNo(logical_channel->getSlot());
            logical_channel->putRFQueue(dmr_data);
            return;
        }
    }
}

bool Controller::handleRegistration(CDMRCSBK& csbk, unsigned int slotNo,
                                    unsigned int srcId, unsigned int dstId,
                                    unsigned int& uab)
{
    bool sub = false;
    dstId = csbk.getDstId();

    if ((csbk.getServiceOptions() & 0x01) == 1) {
        unsigned int target_addr_cnts = (csbk.getCBF() >> 6) & 0x03;
        uab = (csbk.getCBF() << 4) & 0x03;

        if (target_addr_cnts == 1) {
            unsigned int converted_id = TrunkingUtils::convertBase11GroupNumberToBase10(dstId);
            m_logger->log(Logger::LogLevelInfo, QString("DMR Slot %1, received registration request from %2 with attachement to TG %3")
                          .arg(slotNo).arg(srcId).arg(converted_id));
            m_signalling_generator->createReplyRegistrationAccepted(csbk, srcId);
            userRegister(srcId);
            QList<unsigned int> tg_list;
            tg_list.append(converted_id);
            updateSubscriptions(tg_list, srcId);
        } else if ((target_addr_cnts == 0)) {
            m_logger->log(Logger::LogLevelInfo, QString("DMR Slot %1, received registration request from %2 to syscode %3")
                          .arg(slotNo).arg(srcId).arg(dstId));
            unsigned int system_code = m_settings->system_identity_code << 2;
            system_code |= 3; // TODO: PAR

            if (dstId == (system_code & 0xFFFFFF)) {
                m_signalling_generator->createReplyRegistrationAccepted(csbk, srcId);
                userRegister(srcId);
            }
        } else if ((target_addr_cnts == 2)) {
            m_logger->log(Logger::LogLevelInfo, QString("DMR Slot %1, received registration request from %2 with TG subscription list")
                          .arg(slotNo).arg(srcId));
            m_signalling_generator->createReplyRegistrationAccepted(csbk, srcId);
            sub = (bool)m_settings->receive_tg_attach;

            if (!sub) {
                userRegister(srcId);
            }
        }
    } else if ((csbk.getServiceOptions() & 0x01) == 0) {
        m_signalling_generator->createReplyDeregistrationAccepted(csbk, srcId);
        m_logger->log(Logger::LogLevelInfo, QString("DMR Slot %1, received de-registration request from %2 to TG %3")
                      .arg(slotNo).arg(srcId).arg(dstId));
        userDeRegister(srcId);
    }

    if (!m_settings->headless_mode) {
        QList<unsigned int>* registered_ms = new QList<unsigned int>(*m_registered_ms);
        emit updateRegisteredMSList(registered_ms);
    }

    return sub;
}

void Controller::contactMSForVoiceCall(CDMRCSBK& csbk, unsigned int slotNo,
                                       unsigned int srcId, unsigned int dstId, bool local)
{
    m_logger->log(Logger::LogLevelInfo, QString("TSCC: DMR Slot %1, received private call request from %2 to %3")
                  .arg(slotNo).arg(srcId).arg(dstId));

    if (!m_private_calls.contains(dstId))
        m_private_calls.insert(dstId, srcId);

    m_ack_handler->addAck(dstId, ServiceAction::ActionPrivateVoiceCallRequest);
    m_signalling_generator->createPrivateVoiceCallRequest(csbk, local, srcId, dstId);
}

void Controller::contactMSForPacketCall(CDMRCSBK& csbk, unsigned int slotNo,
                                        unsigned int srcId, unsigned int dstId)
{
    m_logger->log(Logger::LogLevelInfo, QString("TSCC: DMR Slot %1, received private packet data call request from %2 to MS %3")
                  .arg(slotNo).arg(srcId).arg(dstId));

    if (!m_private_calls.contains(dstId))
        m_private_calls.insert(dstId, srcId);

    m_ack_handler->addAck(dstId, ServiceAction::ActionPrivatePacketCallRequest);
    m_signalling_generator->createPrivatePacketCallAhoy(csbk, srcId, dstId);
    return;
}

void Controller::handlePrivateCallRequest(CDMRCSBK& csbk, LogicalChannel*& logical_channel, unsigned int slotNo,
                                          unsigned int srcId, unsigned int dstId, bool& channel_grant, bool local)
{
    m_logger->log(Logger::LogLevelInfo, QString("TSCC: DMR Slot %1, received private call request from %2 to destination %3")
                  .arg(slotNo).arg(srcId).arg(dstId));

    if (local) {
        unsigned int temp = srcId;
        srcId = dstId;
        dstId = temp;
    }

    logical_channel = findCallChannel(dstId, srcId);

    if (logical_channel != nullptr) {
        m_signalling_generator->createPrivateVoiceGrant(csbk, logical_channel, srcId, dstId);

        if (srcId == logical_channel->getSource())
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

    if (logical_channel == nullptr) {
        m_logger->log(Logger::LogLevelWarning, "Could not find any free logical channels, telling MS to wait");
        m_signalling_generator->createReplyCallDenied(csbk, srcId);

        if (!m_settings->headless_mode) {
            emit updateRejectedCallsList(srcId, dstId, local);
        }

        return;
    } else {
        m_signalling_generator->createPrivateVoiceGrant(csbk, logical_channel, srcId, dstId);
        channel_grant = true;
        logical_channel->allocateChannel(srcId, dstId, CallType::CALL_TYPE_MS, local);
        logical_channel->setCallType(CallType::CALL_TYPE_MS);
        enableLogicalChannel(logical_channel);

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }

        return;
    }
}

void Controller::handleGroupCallRequest(CDMRCSBK& csbk, LogicalChannel*& logical_channel, unsigned int slotNo,
                                        unsigned int srcId, unsigned int dstId, bool& channel_grant, bool local)
{
    unsigned int dmrDstId = (local) ? TrunkingUtils::convertBase11GroupNumberToBase10(dstId) : dstId;
    m_logger->log(Logger::LogLevelInfo, QString("TSCC: DMR Slot %1, received group call request from %2 to TG %3")
                  .arg(slotNo).arg(srcId).arg(dmrDstId));

    // First try to find an existing busy channel with the same TG

    logical_channel = findCallChannel(dmrDstId, srcId);

    if (logical_channel != nullptr) {
        m_signalling_generator->createGroupVoiceGrant(csbk, logical_channel, srcId, dstId);

        if (srcId == logical_channel->getSource())
            channel_grant = true;

        logical_channel->setDestination(dmrDstId);
        logical_channel->setSource(srcId);
        logical_channel->startTimeoutTimer();
        return;
    }

    // Next try to find a free payload channel to allocate
    logical_channel = findNextFreePayloadChannel(dmrDstId, srcId, local);

    if (logical_channel == nullptr) {
        m_logger->log(Logger::LogLevelWarning, "Could not find any free logical channels, telling MS to wait");
        m_signalling_generator->createReplyCallDenied(csbk, srcId);

        if (!m_settings->headless_mode) {
            emit updateRejectedCallsList(srcId, dmrDstId, local);
        }

        return;
    } else {
        m_signalling_generator->createGroupVoiceGrant(csbk, logical_channel, srcId, dstId);
        channel_grant = true;
        logical_channel->allocateChannel(srcId, dmrDstId, CallType::CALL_TYPE_GROUP, local);
        logical_channel->setCallType(CallType::CALL_TYPE_GROUP);
        enableLogicalChannel(logical_channel);

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }

        return;
    }
}

void Controller::handlePrivatePacketDataCallRequest(CDMRCSBK& csbk, LogicalChannel*& logical_channel, unsigned int slotNo,
                                                    unsigned int srcId, unsigned int dstId, bool& channel_grant, bool local)
{
    m_logger->log(Logger::LogLevelInfo, QString("TSCC: DMR Slot %1, received private packet data call request from %2 to destination %3")
                  .arg(slotNo).arg(srcId).arg(dstId));

    logical_channel = findCallChannel(dstId, srcId, true);

    if (logical_channel != nullptr) {
        m_signalling_generator->createPrivatePacketDataGrant(csbk, logical_channel, srcId, dstId);

        if (srcId == logical_channel->getSource())
            channel_grant = true;

        logical_channel->setDestination(dstId);
        logical_channel->setSource(srcId);
        logical_channel->startTimeoutTimer();
        return;
    }

    // Next try to find a free payload channel to allocate
    logical_channel = findNextFreePayloadChannel(dstId, srcId, local);

    if (logical_channel == nullptr) {
        m_logger->log(Logger::LogLevelWarning, "Could not find any free logical channels, telling MS to wait");
        m_signalling_generator->createReplyCallDenied(csbk, srcId);

        if (!m_settings->headless_mode) {
            emit updateRejectedCallsList(srcId, dstId, local);
        }

        return;
    } else {
        m_signalling_generator->createPrivatePacketDataGrant(csbk, logical_channel, srcId, dstId);
        channel_grant = true;
        logical_channel->allocateChannel(srcId, dstId, CallType::CALL_TYPE_INDIV_PACKET, local);
        enableLogicalChannel(logical_channel);

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }

        return;
    }
}

void Controller::handleGroupPacketDataCallRequest(CDMRCSBK& csbk, LogicalChannel*& logical_channel, unsigned int slotNo,
                                                  unsigned int srcId, unsigned int dstId, bool& channel_grant, bool local)
{
    m_logger->log(Logger::LogLevelInfo, QString("TSCC: DMR Slot %1, received group packet data call request from %2 to destination %3")
                  .arg(slotNo).arg(srcId).arg(dstId));

    logical_channel = findCallChannel(dstId, srcId);

    if (logical_channel != nullptr) {
        m_signalling_generator->createGroupPacketDataGrant(csbk, logical_channel, srcId, dstId);

        if (srcId == logical_channel->getSource())
            channel_grant = true;

        logical_channel->setDestination(dstId);
        logical_channel->setSource(srcId);
        logical_channel->startTimeoutTimer();
        return;
    }

    // Next try to find a free payload channel to allocate
    logical_channel = findNextFreePayloadChannel(dstId, srcId, local);

    if (logical_channel == nullptr) {
        m_logger->log(Logger::LogLevelWarning, "Could not find any free logical channels, telling MS to wait");
        m_signalling_generator->createReplyCallDenied(csbk, srcId);

        if (!m_settings->headless_mode) {
            emit updateRejectedCallsList(srcId, dstId, local);
        }

        return;
    } else {
        m_signalling_generator->createGroupPacketDataGrant(csbk, logical_channel, srcId, dstId);
        channel_grant = true;
        logical_channel->allocateChannel(srcId, dstId, CallType::CALL_TYPE_GROUP_PACKET, local);
        enableLogicalChannel(logical_channel);

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }

        return;
    }
}

void Controller::handleCallDisconnect(unsigned int udp_channel_id, bool group_call,
                                      unsigned int& srcId, unsigned int& dstId, unsigned int slotNo,
                                      LogicalChannel*& logical_channel, CDMRCSBK& csbk)
{
    logical_channel = findChannelByPhysicalIdAndSlot(udp_channel_id, slotNo);

    if (logical_channel == nullptr) {
        m_logger->log(Logger::LogLevelDebug, QString("Could not find active physical channel %1, with slot %2")
                      .arg(udp_channel_id).arg(slotNo));
        return;
    } else {
        m_logger->log(Logger::LogLevelInfo, QString("Channel %1 Slot %2, received call disconnect from %3 to destination %4")
                      .arg(logical_channel->getPhysicalChannel()).arg(slotNo).arg(srcId).arg(dstId));
        m_signalling_generator->createClearChannelUserInitiated(csbk, logical_channel, dstId, group_call);

        if (logical_channel->getBusy() && !logical_channel->getDisabled()) {
            logical_channel->deallocateChannel();
            updateLogicalChannels(&m_logical_channels);
        }

    }
}

void Controller::handleIdleChannelDeallocation(unsigned int channel_id)
{
    unsigned int call_type = m_logical_channels.at(channel_id)->getCallType();

    if (call_type == CallType::CALL_TYPE_MS) {
        if (m_private_calls.contains(m_logical_channels.at(channel_id)->getDestination()))
            m_private_calls.remove(m_logical_channels.at(channel_id)->getDestination());

        if (m_private_calls.contains(m_logical_channels.at(channel_id)->getSource()))
            m_private_calls.remove(m_logical_channels.at(channel_id)->getSource());
    }

    CDMRCSBK csbk;

    if (call_type == CallType::CALL_TYPE_INDIV_PACKET)
        call_type = CallType::CALL_TYPE_MS;

    if (call_type == CallType::CALL_TYPE_GROUP_PACKET)
        call_type = CallType::CALL_TYPE_GROUP;

    m_signalling_generator->createChannelIdleDeallocation(csbk, call_type);

    for (unsigned int i = 0U; i < 5U; i++) {
        transmitCSBK(csbk, m_logical_channels[channel_id], m_logical_channels[channel_id]->getSlot(),
                     m_logical_channels[channel_id]->getPhysicalChannel(), false);
    }

    disableLogicalChannel(m_logical_channels[channel_id]);

    if (!m_settings->headless_mode) {
        emit updateLogicalChannels(&m_logical_channels);
    }
}

void Controller::handleLocalVoiceOnUnallocatedChannel(unsigned int call_type, unsigned int slotNo, unsigned int udp_channel_id)
{
    CDMRCSBK csbk;
    m_signalling_generator->createClearChannelAll(csbk, call_type);
    transmitCSBK(csbk, nullptr, slotNo, udp_channel_id, false);
}

void Controller::processRegistration(unsigned int srcId, unsigned int dstId, CDMRCSBK& csbk)
{
    bool existing_user = m_registered_ms->contains(srcId);
    unsigned int uab = 0;
    LogicalChannel* logical_channel = nullptr;
    bool sub = handleRegistration(csbk, m_control_channel->getSlot(), srcId, dstId, uab);

    if (sub) {
        // FIXME: order is wrong
        m_ack_handler->addAck(srcId, ServiceAction::RegistrationWithAttachment);
        m_signalling_generator->createRequestToUploadTgAttachments(csbk, srcId, uab);
        m_short_data_messages.insert(srcId, uab);
        transmitCSBK(csbk, logical_channel, m_control_channel->getSlot(), m_control_channel->getPhysicalChannel(), false, false);
    } else {
        transmitCSBK(csbk, logical_channel, m_control_channel->getSlot(), m_control_channel->getPhysicalChannel(), false, true);

        if (!existing_user && m_settings->announce_system_message) {
            QVector<LogicalChannel*> active_calls = findActiveChannels();
            QList<QString> messages;
            messages.append(QString("Welcome %1").arg(m_id_lookup->getCallsign(srcId)));
            messages.append(QString("Users - %1, channels - %2, calls - %3,")
                            .arg(m_registered_ms->size())
                            .arg(m_settings->logical_physical_channels.size())
                            .arg(active_calls.size()));
            messages.append(QString("Send SMS to %1 for command help")
                            .arg(m_settings->service_ids.value("help", 0)));
            (void)QtConcurrent::run(&Controller::sendUDTMultipartMessage, this,  messages, srcId, StandardAddreses::DISPATI, false, 1);
        }
    }
}

void Controller::processSignalling(CDMRData& dmr_data, unsigned int udp_channel_id)
{
    unsigned int srcId = dmr_data.getSrcId();
    unsigned int dstId = dmr_data.getDstId();
    unsigned int slotNo = dmr_data.getSlotNo();
    CDMRCSBK csbk;
    unsigned char buf[DMR_FRAME_LENGTH_BYTES];
    dmr_data.getData(buf);
    bool valid = csbk.put(buf);

    if (!valid) {
        m_logger->log(Logger::LogLevelDebug, QString("Received invalid CSBK from %1, slot %2 to destination %3")
                      .arg(srcId).arg(slotNo).arg(dstId));
        return;
    }

    CSBKO csbko = csbk.getCSBKO();

    if (csbko == CSBKO_BSDWNACT)
        return;

    bool channel_grant = false;
    LogicalChannel* logical_channel = nullptr; // for TV_GRANT and PV_GRANT
    bool group_call = dmr_data.getFLCO() == FLCO_GROUP;

    /// Registration or deregistration request
    if (csbko == CSBKO_RAND && csbk.getServiceKind() == ServiceKind::RegiAuthMSCheck) {
        if (m_settings->authentication_required && !m_registered_ms->contains(srcId)) {
            m_logger->log(Logger::LogLevelInfo, QString("User %1 is required to authenticate")
                          .arg(srcId));
            bool key_valid = sendAuthCheck(srcId);

            if (!key_valid) {
                m_logger->log(Logger::LogLevelInfo, QString("User %1 does not have a registration key stored in config."
                                                            " Registration denied.").arg(srcId));
                m_signalling_generator->createReplyRegistrationDenied(csbk, srcId);
                transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
            } else {
                m_auth_user.insert(srcId, csbk);
            }
        } else {
            processRegistration(srcId, dstId, csbk);
            m_control_channel->setText(QString("Registration / deregistration: %1").arg(srcId));

            if (!m_settings->headless_mode) {
                emit updateLogicalChannels(&m_logical_channels);
            }
        }
    }
    /// MS authentication response
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x90) &&
             m_ack_handler->hasAck(srcId, ServiceAction::ActionAuthCheck)) {
        m_ack_handler->removeAck(srcId, ServiceAction::ActionAuthCheck);

        if (m_auth_responses->contains(srcId)) {
            if ((dstId == m_auth_responses->value(srcId))) {
                if (m_auth_user.contains(srcId)) {
                    CDMRCSBK csbk = m_auth_user[srcId];
                    processRegistration(srcId, dstId, csbk);
                    m_auth_user.remove(srcId);
                }

                m_logger->log(Logger::LogLevelInfo, QString("Received authentication reply (SUCCESS) from %1, slot %2")
                              .arg(srcId).arg(slotNo));

                if (!m_settings->headless_mode)
                    emit authSuccess(true);

                m_control_channel->setText(QString("Successful authentication reply: %1").arg(srcId));

                if (!m_settings->headless_mode) {
                    emit updateLogicalChannels(&m_logical_channels);
                }
            } else {
                if (m_auth_user.contains(srcId)) {
                    m_signalling_generator->createReplyRegistrationDenied(csbk, srcId);
                    transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
                    m_auth_user.remove(srcId);
                }

                m_logger->log(Logger::LogLevelInfo, QString("Received authentication reply (FAILED) from %1, slot %2")
                              .arg(srcId).arg(slotNo));

                if (!m_settings->headless_mode)
                    emit authSuccess(false);

                m_control_channel->setText(QString("Failed authentication reply: %1").arg(srcId));

                if (!m_settings->headless_mode) {
                    emit updateLogicalChannels(&m_logical_channels);
                }
            }

            m_auth_responses->remove(srcId);
            emit stopAuthTimer();
        }
    }
    /// Service requested while not registered
    else if (!validateLocalSourceId(srcId)) {
        m_signalling_generator->createReplyNotRegistered(csbk, srcId);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
        m_logger->log(Logger::LogLevelInfo, QString("Received service request while not registered from %1, slot %2 to destination %3")
                      .arg(srcId).arg(slotNo).arg(dstId));
        m_control_channel->setText(QString("Unregistered service request: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
    ///
    /// All below signaling needs the MS to be registered
    ///
    /// MS ping reply
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x88) &&
             m_ack_handler->hasAck(srcId, ServiceAction::ActionPingRequest)) {
        std::chrono::high_resolution_clock::time_point t2_ping_ms = std::chrono::high_resolution_clock::now();
        uint64_t msec = std::chrono::duration_cast<std::chrono::nanoseconds>(t2_ping_ms - t1_ping_ms).count() / 1000000U;
        emit stopPingTimer();
        m_ack_handler->removeAck(srcId, ServiceAction::ActionPingRequest);
        emit pingResponse(srcId, msec);
        m_control_channel->setText(QString("Presence check response: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
    /// Group call request
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::GroupVoiceCall)
             && !csbk.getSuplimentaryData()) {
        bool broadcast_call = false;

        if (csbk.getBroadcast())
            broadcast_call = true;

        handleGroupCallRequest(csbk, logical_channel, slotNo, srcId, dstId, channel_grant, true);

        if (broadcast_call)
            csbk.setCSBKO(CSBKO_BTV_GRANT);

        CDMRCSBK csbk2;
        bool valid = false;

        if ((csbk.getCSBKO() == CSBKO_TV_GRANT) || (csbk.getCSBKO() == CSBKO_BTV_GRANT))
            valid = m_signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);

        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);

        if (valid) {
            transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        }

        m_control_channel->setText(QString("Talkgroup voice request: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
    /// Group call with suplimentary data
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::GroupVoiceCall) && csbk.getSuplimentaryData()) {
        m_signalling_generator->createRequestToSendGroupCallSupplimentaryData(csbk, csbk.getSrcId());
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
        m_control_channel->setText(QString("Talkgroup voice request with SUPLI: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
    /// Direct MS to MS call request
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::IndivVoiceCall)) {
        if (m_settings->call_diverts.contains(dstId)) {
            sendUDTCallDivertInfo(srcId, m_settings->call_diverts.value(dstId), 0);
            m_logger->log(Logger::LogLevelInfo, QString("Received radio FOACSU call request (diverted) from %1, slot %2 to destination %3")
                          .arg(srcId).arg(slotNo).arg(dstId));
            return;
        }

        if (dstId == StandardAddreses::PABXI) {
            m_signalling_generator->createRequestToSendPABXDigits(csbk, csbk.getSrcId());
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
            m_control_channel->setText(QString("PABX call request: %1").arg(srcId));

            if (!m_settings->headless_mode) {
                emit updateLogicalChannels(&m_logical_channels);
            }

            return;
        }

        if (m_registered_ms->contains(dstId)) {
            /** FIXME: The standard call procedure does not include a wait notification */
            CDMRCSBK csbk_wait;
            m_signalling_generator->createReplyWaitForSignalling(csbk_wait, csbk.getSrcId());
            transmitCSBK(csbk_wait, logical_channel, slotNo, udp_channel_id, channel_grant, false);
            /** **/
            contactMSForVoiceCall(csbk, slotNo, srcId, dstId, true);
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
            m_logger->log(Logger::LogLevelInfo, QString("Received radio FOACSU call request from %1, slot %2 to destination %3")
                          .arg(srcId).arg(slotNo).arg(dstId));
        } else {
            handlePrivateCallRequest(csbk, logical_channel, slotNo, srcId, dstId, channel_grant, false);

            CDMRCSBK csbk2;
            bool valid = false;

            if (csbk.getCSBKO() == CSBKO_PV_GRANT)
                valid = m_signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);

            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);

            if (valid) {
                transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, channel_grant, false);
            }

            m_logger->log(Logger::LogLevelInfo, QString("Received radio OACSU call request (not registered radio)"
                                                        " from %1, slot %2 to destination %3")
                          .arg(srcId).arg(slotNo).arg(dstId));
        }

        m_control_channel->setText(QString("Private voice request: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
    /// MS acknowledgement of OACSU call
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x88) &&
             m_ack_handler->hasAck(srcId, ServiceAction::ActionPrivateVoiceCallRequest)) {

        if (m_private_calls.contains(srcId))
            m_private_calls.remove(srcId);

        m_ack_handler->removeAck(srcId, ServiceAction::ActionPrivateVoiceCallRequest);
        csbk.setCSBKO(CSBKO_ACKD);
        csbk.setDstId(dstId);
        csbk.setSrcId(srcId);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        handlePrivateCallRequest(csbk, logical_channel, slotNo, srcId, dstId, channel_grant, true);

        CDMRCSBK csbk2;
        bool valid = false;

        if (csbk.getCSBKO() == CSBKO_PV_GRANT)
            valid = m_signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);

        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);

        if (valid) {
            transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        }

        csbk.setDstId(srcId);
        csbk.setSrcId(dstId);

        if (csbk.getCSBKO() == CSBKO_PV_GRANT)
            valid = m_signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);

        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);

        if (valid) {
            transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        }

        m_logger->log(Logger::LogLevelInfo, QString("Received acknowledgement for OACSU call from %1, slot %2 to destination %3")
                      .arg(srcId).arg(slotNo).arg(dstId));
        m_control_channel->setText(QString("OACSU call acknowledgemet: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
    /// MS acknowledgement of FOACSU call
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x8C)) {
        if (m_private_calls.contains(srcId))
            m_private_calls.remove(srcId);

        if (m_ack_handler->hasAck(srcId, ServiceAction::ActionPrivateVoiceCallRequest)) {
            m_ack_handler->removeAck(srcId, ServiceAction::ActionPrivateVoiceCallRequest);
        }

        csbk.setCSBKO(CSBKO_ACKD);
        csbk.setDstId(dstId);
        csbk.setSrcId(srcId);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        m_logger->log(Logger::LogLevelDebug, QString("Received acknowledgement for FOACSU call from %1, slot %2 to destination %3")
                      .arg(srcId).arg(slotNo).arg(dstId));
        m_control_channel->setText(QString("FOACSU call acknowledgemet: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
    /// MS FOACSU call answer
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::CallAnswer) && ((csbk.getCBF() & 0xF0) == 0)) {
        if (m_private_calls.contains(srcId))
            m_private_calls.remove(srcId);

        handlePrivateCallRequest(csbk, logical_channel, slotNo, srcId, dstId, channel_grant, true);

        CDMRCSBK csbk2;
        bool valid = false;

        if (csbk.getCSBKO() == CSBKO_PV_GRANT)
            valid = m_signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);

        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);

        if (valid) {
            transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        }

        m_logger->log(Logger::LogLevelInfo, QString("Received radio FOACSU call answer from %1, slot %2 to destination %3")
                      .arg(srcId).arg(slotNo).arg(dstId));
        m_control_channel->setText(QString("FOACSU call answer: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
    /// call reject
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::CallAnswer) && ((csbk.getCBF() & 0xF0) == 0x20)) {
        if (m_private_calls.contains(srcId))
            m_private_calls.remove(srcId);

        m_signalling_generator->createReplyCallRejected(csbk, srcId, dstId);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        m_control_channel->setText(QString("FOACSU call reject: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
    /// cancel private call
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::CancelCall) && (csbk.getDstId() > 0)) {
        if (m_private_calls.contains(srcId))
            m_private_calls.remove(srcId);

        if (m_private_calls.contains(dstId))
            m_private_calls.remove(dstId);

        m_signalling_generator->createCancelPrivateCallAhoy(csbk, csbk.getDstId());
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        m_control_channel->setText(QString("FOACSU call cancelled: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
    /// cancel call
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::CancelCall) && (csbk.getDstId() == 0)) {
        handleCallDisconnect(udp_channel_id, group_call, srcId, dstId, slotNo, logical_channel, csbk);

        for (unsigned int i = 0U; i < 3U; i++)
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);

        m_control_channel->setText(QString("Call cancelled: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }

        disableLogicalChannel(logical_channel);
    }
    /// Call disconnect
    else if ((csbko == CSBKO_MAINT) && (csbk.getServiceKind() == ServiceKind::IndivVoiceCall)) {
        handleCallDisconnect(udp_channel_id, group_call, srcId, dstId, slotNo, logical_channel, csbk);

        for (unsigned int i = 0U; i < 3U; i++)
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);

        CDMRCSBK csbk_receiver;
        m_signalling_generator->createCallDisconnect(csbk_receiver, srcId, group_call);

        for (unsigned int i = 0U; i < 3U; i++)
            transmitCSBK(csbk_receiver, logical_channel, slotNo, udp_channel_id, channel_grant, false);

        m_control_channel->setText(QString("Call disconnect: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }

        disableLogicalChannel(logical_channel);
    }
    /// MS acknowledgement of short data message
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x88) &&
             m_ack_handler->hasAck(srcId, ServiceAction::ActionMessageRequest)) {
        m_ack_handler->removeAck(srcId, ServiceAction::ActionMessageRequest);
        csbk.setCSBKO(CSBKO_ACKD);
        csbk.setDstId(dstId);
        csbk.setSrcId(srcId);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        csbk.setDstId(srcId);
        csbk.setSrcId(StandardAddreses::SDMI);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        m_logger->log(Logger::LogLevelInfo, QString("Received read receipt for message request from %1, slot %2 to destination %3")
                      .arg(srcId).arg(slotNo).arg(dstId));
        m_control_channel->setText(QString("Short message acknowledge: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
    /// MS acknowledgement of DGNA request
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x88) &&
             m_ack_handler->hasAck(srcId, ServiceAction::ActionDGNARequest)) {
        m_ack_handler->removeAck(srcId, ServiceAction::ActionDGNARequest);
        csbk.setCSBKO(CSBKO_ACKD);
        csbk.setDstId(dstId);
        csbk.setSrcId(srcId);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        m_logger->log(Logger::LogLevelInfo, QString("Received ACK for DGNA request from %1, slot %2 to destination %3")
                      .arg(srcId).arg(slotNo).arg(dstId));
        m_control_channel->setText(QString("DGNA assignement acknowledge: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
    /// Short data service MS to MS
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::IndivUDTDataCall)) {
        if (m_settings->call_diverts.contains(dstId)) {
            sendUDTCallDivertInfo(srcId, m_settings->call_diverts.value(dstId), 4); // FIXME: SAP 0100 for UDT causes radio to transmit with ID set to 0???
            return;
        }

        m_ack_handler->addAck(dstId, ServiceAction::ActionMessageRequest);
        unsigned int number_of_blocks = m_signalling_generator->createRequestToUploadMessage(csbk, csbk.getSrcId());
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
        m_short_data_messages.insert(srcId, number_of_blocks);
        m_logger->log(Logger::LogLevelInfo, QString("Received private short data message request from %1, slot %2 to destination %3")
                      .arg(srcId).arg(slotNo).arg(dstId));
        m_control_channel->setText(QString("Short private message request: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
    /// Short data service MS to TG
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::GroupUDTDataCall)) {
        unsigned int number_of_blocks = m_signalling_generator->createRequestToUploadMessage(csbk, csbk.getSrcId());
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
        m_short_data_messages.insert(srcId, number_of_blocks);
        m_logger->log(Logger::LogLevelInfo, QString("Received group short data message request to TG from %1, slot %2 to destination %3")
                      .arg(srcId).arg(slotNo).arg(dstId));
        m_control_channel->setText(QString("Short talkgroup message request: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
    /// Status transport message MS to MS and MS toTG
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::StatusTransport)) {
        uint8_t status = (csbk.getCBF() >> 4) & 0x03;
        status |= ((csbk.getData1() >> 1) & 0x1F) << 2;

        if ((csbk.getData1() & 0x80) == 0x80) {
            CDMRCSBK csbk2;
            m_signalling_generator->createReplyMessageAccepted(csbk2, srcId, StandardAddreses::TSI);
            transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, false, false);
            m_signalling_generator->createStatusTransportAhoy(csbk, srcId, dstId, true);
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
            m_logger->log(Logger::LogLevelInfo, QString("Received status transport request to TG from %1, slot %2 to destination %3")
                          .arg(srcId).arg(slotNo).arg(TrunkingUtils::convertBase11GroupNumberToBase10(dstId)));
            m_logger->log(Logger::LogLevelInfo, QString("Status of radio %1, for talkgroup %2 is %3")
                          .arg(srcId).arg(TrunkingUtils::convertBase11GroupNumberToBase10(dstId)).arg(status));
            m_control_channel->setText(QString("Status transfer to talkgroup: %1").arg(srcId));

            if (!m_settings->headless_mode) {
                emit updateLogicalChannels(&m_logical_channels);
            }
        } else {
            m_ack_handler->addAck(dstId, ServiceAction::ActionStatusMsg);
            m_signalling_generator->createStatusTransportAhoy(csbk, srcId, dstId, false);
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
            m_logger->log(Logger::LogLevelInfo, QString("Received status transport request to MS from %1, slot %2 to destination %3")
                          .arg(srcId).arg(slotNo).arg(dstId));
            m_logger->log(Logger::LogLevelInfo, QString("Status of radio %1, for radio %2 is %3")
                          .arg(srcId).arg(dstId).arg(status));
            m_control_channel->setText(QString("Status transfer to radio: %1").arg(srcId));

            if (!m_settings->headless_mode) {
                emit updateLogicalChannels(&m_logical_channels);
            }
        }
    }
    /// MS acknowledgement of status transport message
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x88) &&
             m_ack_handler->hasAck(srcId, ServiceAction::ActionStatusMsg)) {
        m_ack_handler->removeAck(srcId, ServiceAction::ActionStatusMsg);
        csbk.setCSBKO(CSBKO_ACKD);
        csbk.setDstId(dstId);
        csbk.setSrcId(srcId);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        m_logger->log(Logger::LogLevelInfo, QString("Received read receipt for status transport from %1, slot %2 to destination %3")
                      .arg(srcId).arg(slotNo).arg(dstId));
        m_control_channel->setText(QString("Acknowledge status transfer: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
    /// MS status poll reply
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x8E) &&
             m_ack_handler->hasAck(srcId, ServiceAction::ActionStatusPoll)) {
        m_ack_handler->removeAck(srcId, ServiceAction::ActionStatusPoll);
        unsigned int status = csbk.getData1() >> 1;
        m_logger->log(Logger::LogLevelInfo, QString("Received status poll reply %4 from %1, slot %2 to destination %3")
                      .arg(srcId).arg(slotNo).arg(dstId).arg(status));
        m_control_channel->setText(QString("Status poll reply: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
    /// MS status poll reply, service not supported
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x00) && ((csbk.getData1() & 0x01) == 0x00) &&
             m_ack_handler->hasAck(srcId, ServiceAction::ActionStatusPoll)) {
        m_ack_handler->removeAck(srcId, ServiceAction::ActionStatusPoll);
        m_logger->log(Logger::LogLevelInfo, QString("Received status poll reply UNSUPPORTED SERVICE from %1, slot %2 to destination %3")
                      .arg(srcId).arg(slotNo).arg(dstId));
        m_control_channel->setText(QString("Status poll reply UNSUPPORTED: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
    /// MS UDT data poll reply, service not supported
    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x00) && ((csbk.getData1() & 0x01) == 0x00) &&
             m_ack_handler->hasAck(srcId, ServiceAction::UDTPoll)) {
        m_ack_handler->removeAck(srcId, ServiceAction::UDTPoll);
        m_short_data_messages.remove(srcId);
        m_logger->log(Logger::LogLevelInfo, QString("Received UDT data poll reply UNSUPPORTED SERVICE from %1, slot %2 to destination %3")
                      .arg(srcId).arg(slotNo).arg(dstId));
        m_control_channel->setText(QString("UDT poll reply UNSUPPORTED: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
    /// Call diversion request
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::CallDiversion)) {
        unsigned int service_options = csbk.getServiceOptions();
        bool divert = ((service_options >> 4) & 0x01) == 1;

        if (divert) {
            unsigned int number_of_blocks = m_signalling_generator->createRequestToUploadDivertInfo(csbk, csbk.getSrcId());
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, false, false);
            m_short_data_messages.insert(srcId, number_of_blocks);
            m_ack_handler->addAck(srcId, ServiceAction::CallDivert);
            m_logger->log(Logger::LogLevelInfo, QString("Received call diversion request request from %1, slot %2 to destination %3")
                          .arg(srcId).arg(slotNo).arg(dstId));
            m_control_channel->setText(QString("Call diversion request: %1").arg(srcId));

            if (!m_settings->headless_mode) {
                emit updateLogicalChannels(&m_logical_channels);
            }
        } else {
            m_settings->call_diverts.remove(srcId);
            m_signalling_generator->createReplyCallDivertAccepted(csbk, srcId);
            transmitCSBK(csbk, nullptr, slotNo, udp_channel_id, false, false);
            m_logger->log(Logger::LogLevelInfo, QString("Received cancel call diversion request request from %1, slot %2 to destination %3")
                          .arg(srcId).arg(slotNo).arg(dstId));
            m_control_channel->setText(QString("Cancel call diversion request: %1").arg(srcId));

            if (!m_settings->headless_mode) {
                emit updateLogicalChannels(&m_logical_channels);
            }
        }
    }
    /// Individual packet data call
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::IndivPacketDataCall)) {
        if (m_settings->call_diverts.contains(dstId)) {
            sendUDTCallDivertInfo(srcId, m_settings->call_diverts.value(dstId), 0);
            m_logger->log(Logger::LogLevelInfo, QString("Received radio packet data call request (diverted) from %1, slot %2 to destination %3")
                          .arg(srcId).arg(slotNo).arg(dstId));
            return;
        }

        if (m_registered_ms->contains(dstId)) {
            qDebug() << "FID: " << csbk.getFID() << " data1: " << csbk.getData1() << " data2: " << csbk.getCBF();
            uint8_t service_options = csbk.getServiceOptions();
            m_simi = service_options;
            contactMSForPacketCall(csbk, slotNo, srcId, dstId);
            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
            m_logger->log(Logger::LogLevelInfo, QString("Received private packet data call request from %1, slot %2 to destination %3")
                          .arg(srcId).arg(slotNo).arg(dstId));
        } else {
            handlePrivatePacketDataCallRequest(csbk, logical_channel, slotNo, srcId, dstId, channel_grant, true);
            CDMRCSBK csbk2;
            bool valid = false;

            if ((csbk.getCSBKO() == CSBKO_PD_GRANT) || (csbk.getCSBKO() == CSBKO_PD_GRANT_MI))
                valid = m_signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);

            transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);

            if (valid) {
                transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, channel_grant, false);
            }

            m_logger->log(Logger::LogLevelInfo, QString("Received private packet data call request"
                                                        " from %1, slot %2 to destination %3 (not registered ID)")
                          .arg(srcId).arg(slotNo).arg(dstId));
        }

        m_control_channel->setText(QString("Individual packet data call: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
    /// Group packet data call
    else if ((csbko == CSBKO_RAND) && (csbk.getServiceKind() == ServiceKind::GroupPacketDataCall)) {

        handleGroupPacketDataCallRequest(csbk, logical_channel, slotNo, srcId, dstId, channel_grant, true);
        CDMRCSBK csbk2;
        bool valid = false;

        if ((csbk.getCSBKO() == CSBKO_TD_GRANT) || (csbk.getCSBKO() == CSBKO_TD_GRANT_MI))
            valid = m_signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);

        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);

        if (valid) {
            transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        }

        m_logger->log(Logger::LogLevelInfo, QString("Received group packet data call request"
                                                    " from %1, slot %2 to destination %3")
                      .arg(srcId).arg(slotNo).arg(dstId));

        m_control_channel->setText(QString("Group packet data call: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
    /// MS acknowledgement of private packet data call
    else if ((csbko == CSBKO_ACKU) &&
             m_ack_handler->hasAck(srcId, ServiceAction::ActionPrivatePacketCallRequest)) {
        m_ack_handler->removeAck(srcId, ServiceAction::ActionPrivatePacketCallRequest);
        csbk.setCSBKO(CSBKO_ACKD);
        csbk.setDstId(dstId);
        csbk.setSrcId(srcId);
        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        m_logger->log(Logger::LogLevelInfo, QString("Received packet data acknowledgement from %1, slot %2 to destination %3")
                      .arg(srcId).arg(slotNo).arg(dstId));
        // FIXME
        csbk.setData1((unsigned char)(m_simi << 1));
        handlePrivatePacketDataCallRequest(csbk, logical_channel, slotNo, dstId, srcId, channel_grant, true);
        CDMRCSBK csbk2;
        bool valid = false;

        if ((csbk.getCSBKO() == CSBKO_PD_GRANT) || (csbk.getCSBKO() == CSBKO_PD_GRANT_MI))
            valid = m_signalling_generator->createAbsoluteParameters(csbk, csbk2, logical_channel);

        transmitCSBK(csbk, logical_channel, slotNo, udp_channel_id, channel_grant, false);

        if (valid) {
            transmitCSBK(csbk2, logical_channel, slotNo, udp_channel_id, channel_grant, false);
        }

        m_control_channel->setText(QString("Individual packet data call response: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }

    }

    else if ((csbko == CSBKO_ACKU) && (csbk.getCBF() == 0x88)) {
        m_logger->log(Logger::LogLevelDebug, QString("Received unhandled ACKU from %1, slot %2 to destination %3")
                      .arg(srcId).arg(slotNo).arg(dstId));
        m_control_channel->setText(QString("Unhandled radio reply: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }
    }
    /// Not implemeted yet
    else {
        qDebug() << "CSBKO: " << QString::number(csbk.getCSBKO(), 16) <<
                 " FID " << csbk.getFID() <<
                 " data1: " << csbk.getData1() << " data2: " << csbk.getCBF() <<
                 " dst: " << dstId << " src: " << srcId;
        m_logger->log(Logger::LogLevelDebug, QString("Unhandled CSBK type slot %1, channel %2").arg(slotNo).arg(udp_channel_id));
        m_control_channel->setText(QString("Unknown service request: %1").arg(srcId));

        if (!m_settings->headless_mode) {
            emit updateLogicalChannels(&m_logical_channels);
        }

        return;
    }
}

void Controller::processNetworkCSBK(CDMRData& dmr_data, unsigned int udp_channel_id)
{
    (void)udp_channel_id;
    unsigned int srcId = dmr_data.getSrcId();
    unsigned int dstId = dmr_data.getDstId();
    unsigned int slotNo = dmr_data.getSlotNo();
    m_logger->log(Logger::LogLevelDebug, QString("Received network CSBK from %1, slot %2 to destination %3")
                  .arg(srcId).arg(slotNo).arg(dstId));

    CDMRCSBK csbk;
    unsigned char buf[DMR_FRAME_LENGTH_BYTES];
    dmr_data.getData(buf);
    bool valid = csbk.put(buf);

    if (!valid) {
        m_logger->log(Logger::LogLevelDebug, QString("Received invalid CSBK from %1, slot %2 to destination %3")
                      .arg(srcId).arg(slotNo).arg(dstId));
        return;
    }

    qDebug() << "CSBKO: " << QString::number(csbk.getCSBKO(), 16) <<
             " FID " << csbk.getFID() <<
             " data1: " << csbk.getData1() << " data2: " << csbk.getCBF() <<
             " dst: " << dstId << " src: " << srcId;
}


void Controller::transmitCSBK(CDMRCSBK& csbk, LogicalChannel* logical_channel, unsigned int slotNo,
                              unsigned int udp_channel_id, bool channel_grant, bool priority_queue, bool announce_priority)
{
    CDMRData dmr_data_wait = m_signalling_generator->createWaitForSignallingAnswer(slotNo, csbk, channel_grant);
    CDMRData dmr_data = m_signalling_generator->createDataFromCSBK(slotNo, csbk);
    LogicalChannel* main_channel = findChannelByPhysicalIdAndSlot(udp_channel_id, slotNo);

    if (channel_grant) {
        // Wake up payload channel, allow time for the chain latency
        CDMRData payload_channel_data = dmr_data;
        payload_channel_data.setSlotNo(logical_channel->getSlot());
        logical_channel->putRFQueue(payload_channel_data);

        if (m_settings->use_absolute_channel_grants) {
            main_channel->putRFQueue(dmr_data, priority_queue);
        } else {
            for (unsigned int i = 0; i < 2U; i++)
                main_channel->putRFQueue(dmr_data, priority_queue);
        }

        for (unsigned int i = 0; i < 4U; i++)
            logical_channel->putRFQueue(payload_channel_data);

        if (m_settings->announce_priority &&
            (m_subscribed_talkgroups->contains(logical_channel->getDestination())
             || (m_registered_ms->contains(logical_channel->getDestination())))) {

            QVector<LogicalChannel*> active_channels = findActiveChannels();

            for (unsigned int i = 0; i < active_channels.size(); i++) {
                CDMRData announce_data = payload_channel_data;
                announce_data.setSlotNo(active_channels[i]->getSlot());
                active_channels[i]->putRFQueue(announce_data);
            }
        }
    } else {
        main_channel->putRFQueue(dmr_data, priority_queue);

        if (announce_priority && m_settings->announce_priority &&
            (m_subscribed_talkgroups->contains(logical_channel->getDestination())
             || (m_registered_ms->contains(logical_channel->getDestination())))) {
            QVector<LogicalChannel*> active_channels = findActiveChannels();

            for (unsigned int i = 0; i < active_channels.size(); i++) {
                CDMRData announce_data = dmr_data;
                announce_data.setSlotNo(active_channels[i]->getSlot());
                active_channels[i]->putRFQueue(announce_data);
            }
        }
    }
}

void Controller::processDMRNetworkMessage(unsigned char* payload, unsigned int size)
{
    if (!m_settings->use_trunking_protocol) {
        delete[] payload;
        return;
    }

    if (!m_network_signalling->validateNetMessage(payload, size)) {
        delete[] payload;
        return;
    }

    uint8_t opcode = payload[4U];

    if (opcode == NetworkSignalling::OpCode::MasterLogin) {
        subscribeStaticTalkgroups();
        m_logger->log(Logger::LogLevelInfo, QString("Network gateway logged in, will subscribe talkgroups"));
    } else if (opcode == NetworkSignalling::OpCode::UDTMessage) {
        unsigned int srcId = 0, dstId = 0;
        QString message;
        bool group = false;
        unsigned char format = 4;
        unsigned char uuid[16U];

        if (m_network_signalling->parseUDTTransferMessage(payload, size, srcId, dstId,
                                                          message, format, group, uuid)) {
            sendUDTShortMessage(message, dstId, srcId, group);

            if (group) {
                m_logger->log(Logger::LogLevelInfo, QString("Received from network group UDT short data message from %1 to %2: %3")
                              .arg(srcId)
                              .arg(dstId)
                              .arg(message));
            } else {
                m_logger->log(Logger::LogLevelInfo, QString("Received from network private UDT short data message from %1 to %2: %3")
                              .arg(srcId)
                              .arg(dstId)
                              .arg(message));
            }

            CDMRData text_message_reply;
            m_network_signalling->createUDTAcceptMessage(text_message_reply, srcId, dstId, uuid);
            m_control_channel->putNetQueue(text_message_reply);
            m_logger->log(Logger::LogLevelDebug, QString("Sending UDT accept reply to network"));

            if (!m_settings->headless_mode) {
                if (group) {
                    emit updateMessageLog(srcId, dstId, message, true);
                } else {
                    emit updateMessageLog(srcId, dstId, message, false);
                }
            }

            m_control_channel->setText(QString("Short text message: %1").arg(srcId));

            if (!m_settings->headless_mode) {
                emit updateLogicalChannels(&m_logical_channels);
            }
        } else {
            m_logger->log(Logger::LogLevelWarning, QString("Could not parse network UDT short data message"));
        }
    } else if (opcode == NetworkSignalling::OpCode::UDTAccept) {
        unsigned int srcId = 0U, dstId = 0U;
        unsigned char uuid[16U];

        if (m_network_signalling->parseUDTAcceptMessage(payload, size, srcId, dstId,
                                                        uuid)) {
            m_logger->log(Logger::LogLevelInfo, QString("Received network accept UDT short data message from %1 to %2")
                          .arg(srcId)
                          .arg(dstId));
        } else {
            m_logger->log(Logger::LogLevelWarning, QString("Could not parse network accept UDT short data message"));
        }
    } else if (opcode == NetworkSignalling::OpCode::RegistrationConfirmation) {
        unsigned int srcId = 0U;
        bool accept = false;

        if (m_network_signalling->parseRegistrationConfirmationMessage(payload, size, srcId, accept)) {
            if (accept)
                m_logger->log(Logger::LogLevelInfo, QString("Network accepted registration for MS %1")
                              .arg(srcId));
            else
                m_logger->log(Logger::LogLevelInfo, QString("Network rejected registration for MS %1")
                              .arg(srcId));
        } else {
            m_logger->log(Logger::LogLevelWarning, QString("Could not parse registration confirmation message"));
        }
    } else if (opcode == NetworkSignalling::OpCode::NetDeRegistration) {
        unsigned int srcId = 0U;

        if (m_network_signalling->parseDeRegistrationConfirmationMessage(payload, size, srcId)) {
            m_logger->log(Logger::LogLevelInfo, QString("Network has de-registered MS %1")
                          .arg(srcId));

            if (m_settings->send_network_registrations) {
                userDeRegister(srcId);
            }
        } else {
            m_logger->log(Logger::LogLevelWarning, QString("Could not parse de-registration confirmation message"));
        }
    } else if (opcode == NetworkSignalling::OpCode::GroupSubscriptionConfirmation) {
        QList<unsigned int> confirmed_tgs;

        if (m_network_signalling->parseSubscriptionConfirmationMessage(payload, size, confirmed_tgs)) {
            for (unsigned int i = 0U; i < confirmed_tgs.size(); i++) {
                m_logger->log(Logger::LogLevelInfo, QString("Network accepted subscription for TG %1")
                              .arg(confirmed_tgs.at(i)));
            }
        } else {
            m_logger->log(Logger::LogLevelWarning, QString("Could not parse talkgroup subscription confirmation message"));
        }
    } else if (opcode == NetworkSignalling::OpCode::GroupUnSubscriptionConfirmation) {
        QList<unsigned int> confirmed_tgs;

        if (m_network_signalling->parseUnSubscriptionConfirmationMessage(payload, size, confirmed_tgs)) {
            for (unsigned int i = 0U; i < confirmed_tgs.size(); i++) {
                m_logger->log(Logger::LogLevelInfo, QString("Network accepted unsubscription for TG %1")
                              .arg(confirmed_tgs.at(i)));
            }
        } else {
            m_logger->log(Logger::LogLevelWarning, QString("Could not parse talkgroup unsubscription confirmation message"));
        }
    } else {
        m_logger->log(Logger::LogLevelDebug, QString("Received unknown network message with opcode %1")
                      .arg(QString::number(opcode, 16)));
    }

    delete[] payload;
}

QVector<LogicalChannel*>* Controller::getLogicalChannels()
{
    return &m_logical_channels;
}

void Controller::setChannelEnabled(unsigned int index, bool state)
{
    if (index < (unsigned int)m_logical_channels.size()) {
        if (!m_logical_channels[index]->isControlChannel()) {
            if (state) {
                m_settings->channel_disable_bitmask &= ~(1 << index);
            } else {
                m_settings->channel_disable_bitmask |= 1 << index;
            }

            m_logical_channels[index]->setDisabled(!state);
        }
    }

    if (!m_settings->headless_mode) {
        emit updateLogicalChannels(&m_logical_channels);
    }
}

bool Controller::validateLocalSourceId(unsigned int srcId)
{
    if (m_registered_ms->contains(srcId) || !m_settings->registration_required)
        return true;

    m_logger->log(Logger::LogLevelWarning, QString("Rejected local id %1, not registered with the site").arg(srcId));
    return false;
}

void Controller::setCallStats(unsigned int srcId, unsigned int dstId,
                              float rssi, float ber, float max_ber, unsigned int call_time, bool private_call)
{
    if (!m_settings->headless_mode) {
        emit updateCallLog(srcId, dstId, rssi, ber, max_ber, call_time, private_call);
    }

    if (dstId == (unsigned int)m_settings->service_ids.value("signal_report", 0)) {
        (void)QtConcurrent::run(&Controller::sendRSSIInfo, this, rssi, ber, srcId);
    }
}


