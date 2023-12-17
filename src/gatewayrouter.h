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

#ifndef GATEWAYROUTER_H
#define GATEWAYROUTER_H

#include <QObject>
#include <QMap>
#include "src/settings.h"
#include "src/logger.h"
#include "MMDVM/DMRData.h"

class GatewayRouter : public QObject
{
    Q_OBJECT
public:
    explicit GatewayRouter(Settings *settings, Logger *logger, QObject *parent = nullptr);
    bool findRoute(CDMRData &dmr_data, unsigned int &gateway_id);

signals:

private:
    Settings *_settings;
    Logger *_logger;
    QMap<unsigned int, unsigned int> _routing_table;

};

#endif // GATEWAYROUTER_H
