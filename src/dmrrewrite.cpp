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

DMRRewrite::DMRRewrite(Settings *settings, QList<unsigned int> *registered_ms)
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
        dmr_data.setSlotNo(2);
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
        dmr_data.setSlotNo(2);
        return true;
    }
    if(_settings->slot_rewrite_table.contains(dstId))
    {
        dmr_data.setSlotNo(_settings->slot_rewrite_table.value(dstId));
        return true;
    }
    else
        return false;
}

bool DMRRewrite::rewriteSource(CDMRData &data)
{
    unsigned int srcId = data.getSrcId();
    if((_registered_ms->size() > 0) && _registered_ms->contains(srcId))
    {
        data.setSrcId(1);
        return true;
    }
    else
        return false;
}



