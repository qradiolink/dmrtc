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
#include "settings.h"
#include "MMDVM/DMRData.h"
#include "MMDVM/DMRFullLC.h"
#include "MMDVM/DMRLC.h"
#include "MMDVM/DMRSlotType.h"
#include "MMDVM/Sync.h"

class DMRRewrite
{
public:
    DMRRewrite(Settings *settings, QList<unsigned int> *registered_ms);
    bool rewriteSlot(CDMRData &dmr_data);
    bool rewriteSource(CDMRData &data);

private:
    Settings *_settings;
    QMap<int, int> _slot_rewrite_map;
    QList<unsigned int> *_registered_ms;
    QVector<int> _private_call_stream_ids;
};

#endif // DMRREWRITE_H
