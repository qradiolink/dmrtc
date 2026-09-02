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

#include "gatewayrouter.h"


GatewayRouter::GatewayRouter(const Settings* settings, Logger* logger, QObject* parent) : QObject(parent)
{
    m_settings = settings;
    m_logger = logger;
    m_network_subscribed_talkgroups = new QSet<unsigned int>;
}

GatewayRouter::~GatewayRouter()
{
    m_network_subscribed_talkgroups->clear();
    delete m_network_subscribed_talkgroups;
}

QList<unsigned int> GatewayRouter::getGatewayIds() const
{
    QListIterator<QMap<QString, QString>> it_gws(m_settings->gateways);
    QList<unsigned int> ids;
    while (it_gws.hasNext()) {
        QMap<QString, QString> gw = it_gws.next();
        bool ok = false;
        unsigned int id = (unsigned int)(gw.value("gateway_id").toInt(&ok));
        if (ok)
            ids.append(id);
    }
    return ids;
}

bool GatewayRouter::findRoute(CDMRData& dmr_data, unsigned int& gateway_id)
{
    if (dmr_data.getMessageFlag()) {
        if (getTrunkingGateway(gateway_id)) {
            return true;
        }

        return false;
    }

    FLCO flco = dmr_data.getFLCO();

    if (flco == FLCO_USER_USER) {
        if (getPrivateCallGateway(gateway_id)) {
            return true;
        }

        return false;
    } else {
        unsigned int dstId = dmr_data.getDstId();

        if ((m_settings->local_tg_ids.size() > 0) && m_settings->local_tg_ids.contains(dstId))
            return false;

        if (getPrefixRoute(dstId, gateway_id)) {
            return true;
        }

        if (m_settings->talkgroup_routing_table.contains(dstId)) {
            gateway_id = m_settings->talkgroup_routing_table.value(dstId);
            return true;
        }

        // no gateway or prefix mismatch
        return false;
    }

    return false;
}

bool GatewayRouter::getPrivateCallGateway(unsigned int& id)
{
    /// TODO: multiple gateways with private call
    QListIterator<QMap<QString, QString>> it_gws(m_settings->gateways);

    while (it_gws.hasNext()) {
        QMap<QString, QString> gw = it_gws.next();
        bool ok = false;
        int enable = gw.value("enable_private_calls").toInt(&ok);

        if (ok && bool(enable)) {
            ok = false;
            id = (unsigned int)(gw.value("gateway_id").toInt(&ok));

            if (ok)
                return true;
        }
    }

    return false;
}

bool GatewayRouter::getTrunkingGateway(unsigned int& id)
{
    QList<unsigned int> found_ids;
    QListIterator<QMap<QString, QString>> it_gws(m_settings->gateways);

    while (it_gws.hasNext()) {
        QMap<QString, QString> gw = it_gws.next();

        if (uint8_t(gw.value("gateway_type").toInt()) == 1) { // TODO: proto version
            bool ok = false;
            unsigned int found_id = (unsigned int)(gw.value("gateway_id").toInt(&ok));

            if (!ok)
                continue;

            found_ids.append(found_id);
        }
    }

    if (found_ids.size() == 1) {
        id = found_ids.at(0);
        return true;
    } else if (found_ids.size() == 0) {
        return false;
    } else {
        m_logger->log(Logger::LogLevelWarning, QString("Found more than 1 trunking gateway, could not decide on route."));
    }

    return false;
}

bool GatewayRouter::getPrefixRoute(unsigned int dstId, unsigned int& id)
{
    int dst = (int) dstId;
    QList<unsigned int> found_ids;
    QListIterator<QMap<QString, QString>> it_gws(m_settings->gateways);

    while (it_gws.hasNext()) {
        QMap<QString, QString> gw = it_gws.next();
        bool ok = false;
        int prefix = gw.value("talkgroup_prefix").toInt(&ok);

        if (!ok)
            continue;

        int real_tg_id = dst - prefix;

        if (real_tg_id <= 0)
            continue;

        if (real_tg_id >= m_settings->tg_prefix_separation)
            continue;

        ok = false;
        unsigned int found_id = (unsigned int)(gw.value("gateway_id").toInt(&ok));

        if (!ok)
            continue;

        found_ids.append(found_id);
    }

    if (found_ids.size() == 1) {
        id = found_ids.at(0);
        return true;
    } else if (found_ids.size() > 1) {
        QString all_ids;
        for(unsigned int i=0;i<found_ids.size();i++) {
            all_ids.append(QString::number(found_ids.at(i)) + " ");
        }
        m_logger->log(Logger::LogLevelWarning,
                      QString("Found more than 1 route to gateway, could not decide on route. Gateways: %1").arg(all_ids));
    }

    return false;
}

bool GatewayRouter::getStaticTgList(QList<unsigned int>& static_tg_list)
{
    unsigned int trunking_gw_id = 0;
    bool result = getTrunkingGateway(trunking_gw_id);

    if (!result)
        return false;

    QMapIterator<unsigned int, unsigned int> it_static_tg(m_settings->static_talkgroups_requested);

    while (it_static_tg.hasNext()) {
        it_static_tg.next();

        if (it_static_tg.value() == trunking_gw_id)
            static_tg_list.append(it_static_tg.key());
    }

    if (static_tg_list.size() > 0) {
        return true;
    }

    return false;
}

bool GatewayRouter::getNetSubscriptions(QList<unsigned int>& tg_list)
{
    if (m_network_subscribed_talkgroups->size() > 0) {
        QSetIterator<unsigned int> it(*m_network_subscribed_talkgroups);

        while (it.hasNext()) {
            tg_list.append(it.next());
        }

        return true;
    }

    return false;
}

bool GatewayRouter::getTrunkingSubscriptions(QList<unsigned int>& requested_tg_ids,
                                             QList<unsigned int>& new_tg_ids)
{
    unsigned int trunking_gw_id = 0;
    bool result = getTrunkingGateway(trunking_gw_id);

    if (!result)
        return false;

    QList<unsigned int> static_tgs;
    getStaticTgList(static_tgs);

    for (int i = 0; i < requested_tg_ids.size(); i++) {
        unsigned int req_tg_id = requested_tg_ids.at(i);
        unsigned int gw_id = 0;
        bool result = getPrefixRoute(req_tg_id, gw_id);

        if (result && (gw_id == trunking_gw_id)) {
            if (!m_network_subscribed_talkgroups->contains(req_tg_id)
                && !static_tgs.contains(req_tg_id)
                && !m_settings->local_tg_ids.contains(req_tg_id)) {
                unsigned int prefix_removed_tg = req_tg_id;

                if (removeTalkgroupPrefix(prefix_removed_tg, gw_id)) {
                    new_tg_ids.append(prefix_removed_tg);
                    m_network_subscribed_talkgroups->insert(req_tg_id);
                }
            }
        }
    }

    if (new_tg_ids.size() > 0)
        return true;

    return false;
}

bool GatewayRouter::getTrunkingUnSubscriptions(QList<unsigned int>& requested_tg_ids,
                                               QList<unsigned int>& new_tg_ids)
{
    unsigned int trunking_gw_id = 0;
    bool result = getTrunkingGateway(trunking_gw_id);

    if (!result)
        return false;

    QList<unsigned int> static_tgs;
    getStaticTgList(static_tgs);

    for (int i = 0; i < requested_tg_ids.size(); i++) {
        unsigned int req_tg_id = requested_tg_ids.at(i);
        unsigned int gw_id = 0;
        bool result = getPrefixRoute(req_tg_id, gw_id);

        if (result && (gw_id == trunking_gw_id)) {
            if (m_network_subscribed_talkgroups->contains(req_tg_id)
                && !static_tgs.contains(req_tg_id)
                && !m_settings->local_tg_ids.contains(req_tg_id)) {
                unsigned int prefix_removed_tg = req_tg_id;

                if (removeTalkgroupPrefix(prefix_removed_tg, gw_id)) {
                    new_tg_ids.append(prefix_removed_tg);
                    m_network_subscribed_talkgroups->remove(req_tg_id);
                }
            }
        }
    }

    if (new_tg_ids.size() > 0)
        return true;

    return false;
}

bool GatewayRouter::removeTalkgroupPrefix(unsigned int& tg_id, unsigned int gateway_id)
{
    int dst = (int)tg_id;
    QListIterator<QMap<QString, QString>> it_gws(m_settings->gateways);

    while (it_gws.hasNext()) {
        QMap<QString, QString> gw = it_gws.next();
        bool ok = false;
        unsigned int found_gw_id = (unsigned int) gw.value("gateway_id").toInt(&ok);

        if (!ok || (found_gw_id != gateway_id))
            continue;

        if (gw.value("gateway_type").toInt() != 1)
            return false;

        ok = false;
        int prefix = gw.value("talkgroup_prefix").toInt(&ok);

        if (!ok)
            return false;

        int real_tg_id = dst - prefix;

        if (real_tg_id <= 0)
            return false;

        if (real_tg_id >= m_settings->tg_prefix_separation)
            return false;

        tg_id = (unsigned int)real_tg_id;
        return true;
    }

    return false;
}

bool GatewayRouter::addTalkgroupPrefix(unsigned int& tg_id, unsigned int gateway_id)
{
    int dst = (int)tg_id;
    QListIterator<QMap<QString, QString>> it_gws(m_settings->gateways);

    while (it_gws.hasNext()) {
        QMap<QString, QString> gw = it_gws.next();
        bool ok = false;
        unsigned int found_gw_id = (unsigned int) gw.value("gateway_id").toInt(&ok);

        if (!ok || (found_gw_id != gateway_id))
            continue;

        if (gw.value("gateway_type").toInt() != 1)
            return false;

        ok = false;
        int prefix = gw.value("talkgroup_prefix").toInt(&ok);

        if (!ok)
            return false;

        int prefixed_tg_id = dst + prefix;

        if (prefixed_tg_id <= 0)
            return false;

        tg_id = (unsigned int)prefixed_tg_id;
        return true;
    }

    return false;
}
