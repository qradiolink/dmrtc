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

#include "dmrrewrite.h"

DMRRewrite::DMRRewrite(const Settings *settings, const QList<unsigned int> *registered_ms)
{
    _settings = settings;
    _registered_ms = registered_ms;
}

bool DMRRewrite::rewriteSlot(CDMRData &dmr_data)
{
    unsigned int dstId = dmr_data.getDstId();
    if(_private_call_stream_ids.contains(dmr_data.getStreamId()) &&
            dmr_data.getDataType() != DT_TERMINATOR_WITH_LC)
    {
        dmr_data.setSlotNo(_settings->private_call_timeslot);
        return true;
    }
    if(dmr_data.getFLCO() == FLCO_USER_USER)
    {
        if(dmr_data.getDataType() == DT_TERMINATOR_WITH_LC)
        {
            _private_call_stream_ids.removeAll(dmr_data.getStreamId());
        }
        else if(!_private_call_stream_ids.contains(dmr_data.getStreamId()))
        {
            _private_call_stream_ids.append(dmr_data.getStreamId());
        }
        dmr_data.setSlotNo(_settings->private_call_timeslot);
        return true;
    }
    if(_settings->slot_rewrite_table.contains(dstId))
    {
        dmr_data.setSlotNo(_settings->slot_rewrite_table.value(dstId));
        return true;
    }
    return false;
}

bool DMRRewrite::rewriteSource(CDMRData &data)
{
    if(data.getMessageFlag())
        return false;
    if(_registered_ms == nullptr)
        return false;
    unsigned int srcId = data.getSrcId();
    if((_registered_ms->size() > 0) && _registered_ms->contains(srcId))
    {
        data.setSrcId(StandardAddreses::DISPATI);
        return true;
    }
    return false;
}

bool DMRRewrite::removeTalkgroupPrefix(CDMRData &dmr_data, unsigned int gateway_id)
{
    if(dmr_data.getMessageFlag())
        return false;
    if(dmr_data.getFLCO() == FLCO_GROUP)
    {
        int dst = (int)dmr_data.getDstId();
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
            dmr_data.setDstId((unsigned int)real_tg_id);
            return true;
        }
        return false;
    }
    return false;
}

bool DMRRewrite::addTalkgroupPrefix(CDMRData &dmr_data, unsigned int gateway_id)
{
    if(dmr_data.getMessageFlag())
        return false;
    if(dmr_data.getFLCO() == FLCO_GROUP)
    {
        int dst = (int)dmr_data.getDstId();
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
            int tg_id = dst + prefix;
            if(tg_id <= 0)
                return false;
            dmr_data.setDstId((unsigned int)tg_id);
            return true;
        }
        return false;
    }
    return false;
}

bool DMRRewrite::getTrunkingGatewayTalkgroupId(unsigned int &tg_id)
{
    unsigned int prefix = (tg_id / _settings->tg_prefix_separation) * _settings->tg_prefix_separation;
    QListIterator<QMap<QString, QString>> it_gws(_settings->gateways);
    while(it_gws.hasNext())
    {
        QMap<QString, QString> gw = it_gws.next();
        unsigned int found_prefix = (unsigned int) gw.value("talkgroup_prefix").toInt();
        if(prefix != found_prefix)
            continue;
        if(gw.value("gateway_type").toInt() == 1)
        {
            tg_id -= prefix;
            return true;
        }
        return false;
    }
    return false;
}

bool DMRRewrite::getEmbeddedDataRewrite(CDMRData &dmr_data)
{
    int gw_id = 0;
    unsigned int dstId = dmr_data.getDstId();
    if(!_settings->talkgroup_routing_table.contains(dstId))
    {
        gw_id = _settings->talkgroup_routing_table.value(dstId);
        QListIterator<QMap<QString, QString>> it(_settings->gateways);
        while(it.hasNext())
        {
            QMap<QString, QString> gw_map = it.next();
            if((gw_map.value("gateway_id").toLong() == gw_id) &&
                    (gw_map.value("gateway_type").toLong() == 0))
            {
                return true;
            }
        }
        return false;
    }
    return true;
}

void DMRRewrite::rewriteLC(CDMRData &dmr_data)
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



