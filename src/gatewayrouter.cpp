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
        if(_settings->talkgroup_routing_table.contains(dstId))
        {
            gateway_id = _settings->talkgroup_routing_table.value(dstId);
            return true;
        }
        gateway_id = 0; // default route
        return true;
    }
}

bool GatewayRouter::getPrivateCallGateway(unsigned int &id)
{
    /// TODO: multiple gateways with private call
    QListIterator<QMap<QString, QString>> it_gws(_settings->gateway_ids);
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
    QListIterator<QMap<QString, QString>> it_gws(_settings->gateway_ids);
    while(it_gws.hasNext())
    {
        QMap<QString, QString> gw = it_gws.next();
        if(uint8_t(gw.value("gateway_type").toInt()) == 1) // TODO: proto version
        {
            id = (unsigned int)(gw.value("gateway_id").toInt());
            return true;
        }
    }
    return false;
}
