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

#ifndef DMRREWRITE_H
#define DMRREWRITE_H

#include <QMap>
#include <QList>
#include <QVector>
#include <QDebug>
#include "MMDVM/DMRData.h"
#include "MMDVM/DMRFullLC.h"
#include "MMDVM/DMRLC.h"
#include "MMDVM/DMRSlotType.h"
#include "MMDVM/Sync.h"
#include "settings.h"
#include "standard_PDU.h"


class DMRRewrite
{
public:
    DMRRewrite(const Settings *settings, const QList<unsigned int> *registered_ms);
    bool rewriteSlot(CDMRData &dmr_data);
    bool rewriteSource(CDMRData &data);
    bool removeTalkgroupPrefix(CDMRData &dmr_data, unsigned int gateway_id);
    bool addTalkgroupPrefix(CDMRData &dmr_data, unsigned int gateway_id);
    bool getTrunkingGatewayTalkgroupId(unsigned int &tg_id);
    bool getEmbeddedDataRewrite(CDMRData &dmr_data);
    void rewriteLC(CDMRData &dmr_data);

private:
    const Settings *_settings;
    const QList<unsigned int> *_registered_ms;
    QVector<int> _private_call_stream_ids;
};

#endif // DMRREWRITE_H
