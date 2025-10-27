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

#include "gatewayrouter.h"


GatewayRouter::GatewayRouter(const Settings *settings, Logger *logger, QObject *parent) : QObject(parent)
{
    _settings = settings;
    _logger = logger;
    _network_subscribed_talkgroups = new QSet<unsigned int>;
}

GatewayRouter::~GatewayRouter()
{
    _network_subscribed_talkgroups->clear();
    delete _network_subscribed_talkgroups;
}

bool GatewayRouter::findRoute(CDMRData &dmr_data, unsigned int &gateway_id)
{
    if(dmr_data.getMessageFlag())
    {
        if (getTrunkingGateway(gateway_id))
        {
            return true;
        }
        return false;
    }
    FLCO flco = dmr_data.getFLCO();
    if(flco == FLCO_USER_USER)
    {
        if (getPrivateCallGateway(gateway_id))
        {
            return true;
        }
        return false;
    }
    else
    {
        unsigned int dstId = dmr_data.getDstId();
        if((_settings->local_tg_ids.size() > 0) && _settings->local_tg_ids.contains(dstId))
            return false;
        if(_settings->talkgroup_routing_table.contains(dstId))
        {
            gateway_id = _settings->talkgroup_routing_table.value(dstId);
            return true;
        }
        if(getPrefixRoute(dstId, gateway_id))
        {
            return true;
        }
        // no gateway or prefix mismatch
        gateway_id = 0; // default route
        return true;
    }
    return false;
}

bool GatewayRouter::getPrivateCallGateway(unsigned int &id)
{
    /// TODO: multiple gateways with private call
    QListIterator<QMap<QString, QString>> it_gws(_settings->gateways);
    while(it_gws.hasNext())
    {
        QMap<QString, QString> gw = it_gws.next();
        if(bool(gw.value("enable_private_calls").toInt()))
        {
            id = (unsigned int)(gw.value("gateway_id").toInt());
            return true;
        }
    }
    return false;
}

bool GatewayRouter::getTrunkingGateway(unsigned int &id)
{
    QList<unsigned int> found_ids;
    QListIterator<QMap<QString, QString>> it_gws(_settings->gateways);
    while(it_gws.hasNext())
    {
        QMap<QString, QString> gw = it_gws.next();
        if(uint8_t(gw.value("gateway_type").toInt()) == 1) // TODO: proto version
        {
            unsigned int found_id = (unsigned int)(gw.value("gateway_id").toInt());
            found_ids.append(found_id);
        }
    }
    if(found_ids.size() == 1)
    {
        id = found_ids.at(0);
        return true;
    }
    else if(found_ids.size() == 0)
    {
        return false;
    }
    else
    {
        _logger->log(Logger::LogLevelWarning, QString("Found more than 1 trunking gateway, could not decide on route."));
    }
    return false;
}

bool GatewayRouter::getPrefixRoute(unsigned int dstId, unsigned int &id)
{
    int dst = (int) dstId;
    QList<unsigned int> found_ids;
    QListIterator<QMap<QString, QString>> it_gws(_settings->gateways);
    while(it_gws.hasNext())
    {
        QMap<QString, QString> gw = it_gws.next();
        int prefix = gw.value("talkgroup_prefix").toInt();
        int real_tg_id = dst - prefix;
        if(real_tg_id <= 0)
            continue;
        if(real_tg_id >= _settings->tg_prefix_separation)
            continue;
        unsigned int found_id = (unsigned int)(gw.value("gateway_id").toInt());
        found_ids.append(found_id);
    }
    if(found_ids.size() == 1)
    {
        id = found_ids.at(0);
        return true;
    }
    return false;
}

bool GatewayRouter::getStaticTgList(QList<unsigned int> &static_tg_list)
{
    unsigned int trunking_gw_id = 0;
    bool result = getTrunkingGateway(trunking_gw_id);
    if(!result)
        return false;
    QMapIterator<unsigned int, unsigned int> it_static_tg(_settings->static_talkgroups_requested);
    while(it_static_tg.hasNext())
    {
        it_static_tg.next();
        if(it_static_tg.value() == trunking_gw_id)
            static_tg_list.append(it_static_tg.key());
    }
    if(static_tg_list.size() > 0)
    {
        return true;
    }
    return false;
}

bool GatewayRouter::getTrunkingSubscriptions(QList<unsigned int> &requested_tg_ids,
                                                QList<unsigned int> &new_tg_ids)
{
    unsigned int trunking_gw_id = 0;
    bool result = getTrunkingGateway(trunking_gw_id);
    if(!result)
        return false;
    for(int i=0;i<requested_tg_ids.size();i++)
    {
        unsigned int req_tg_id = requested_tg_ids.at(i);
        unsigned int gw_id = 0;
        bool result = getPrefixRoute(req_tg_id, gw_id);
        if(result && (gw_id == trunking_gw_id))
        {
            if(!_network_subscribed_talkgroups->contains(req_tg_id))
            {
                if(removeTalkgroupPrefix(req_tg_id, gw_id))
                {
                    new_tg_ids.append(req_tg_id);
                    _network_subscribed_talkgroups->insert(req_tg_id);
                }
            }
        }
    }
    if(new_tg_ids.size() > 0)
        return true;
    return false;
}

bool GatewayRouter::getTrunkingUnSubscriptions(QList<unsigned int> &requested_tg_ids,
                                                QList<unsigned int> &new_tg_ids)
{
    unsigned int trunking_gw_id = 0;
    bool result = getTrunkingGateway(trunking_gw_id);
    if(!result)
        return false;
    for(int i=0;i<requested_tg_ids.size();i++)
    {
        unsigned int req_tg_id = requested_tg_ids.at(i);
        unsigned int gw_id = 0;
        bool result = getPrefixRoute(req_tg_id, gw_id);
        if(result && (gw_id == trunking_gw_id))
        {
            if(_network_subscribed_talkgroups->contains(req_tg_id))
            {
                if(removeTalkgroupPrefix(req_tg_id, gw_id))
                {
                    new_tg_ids.append(req_tg_id);
                    _network_subscribed_talkgroups->remove(req_tg_id);
                }
            }
        }
    }
    if(new_tg_ids.size() > 0)
        return true;
    return false;
}

bool GatewayRouter::removeTalkgroupPrefix(unsigned int &tg_id, unsigned int gateway_id)
{
    int dst = (int)tg_id;
    QListIterator<QMap<QString, QString>> it_gws(_settings->gateways);
    while(it_gws.hasNext())
    {
        QMap<QString, QString> gw = it_gws.next();
        unsigned int found_gw_id = (unsigned int) gw.value("gateway_id").toInt();
        if(found_gw_id != gateway_id)
            continue;
        if(gw.value("gateway_type").toInt() != 1)
            return false;
        int prefix = gw.value("talkgroup_prefix").toInt();
        int real_tg_id = dst - prefix;
        if(real_tg_id <= 0)
            return false;
        if(real_tg_id >= _settings->tg_prefix_separation)
            return false;
        tg_id = (unsigned int)real_tg_id;
        return true;
    }
    return false;
}

bool GatewayRouter::addTalkgroupPrefix(unsigned int &tg_id, unsigned int gateway_id)
{
    int dst = (int)tg_id;
    QListIterator<QMap<QString, QString>> it_gws(_settings->gateways);
    while(it_gws.hasNext())
    {
        QMap<QString, QString> gw = it_gws.next();
        unsigned int found_gw_id = (unsigned int) gw.value("gateway_id").toInt();
        if(found_gw_id != gateway_id)
            continue;
        if(gw.value("gateway_type").toInt() != 1)
            return false;
        int prefix = gw.value("talkgroup_prefix").toInt();
        int prefixed_tg_id = dst + prefix;
        if(prefixed_tg_id <= 0)
            return false;
        tg_id = (unsigned int)prefixed_tg_id;
        return true;
    }
    return false;
}
