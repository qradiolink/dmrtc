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

#ifndef DMRIDLOOKUP_H
#define DMRIDLOOKUP_H

#include <QObject>
#include <QByteArray>
#include <QMap>
#include "src/settings.h"
#include "src/logger.h"

class DMRIdLookup : public QObject
{
    Q_OBJECT
public:
    explicit DMRIdLookup(const Settings* settings, Logger* logger, QObject* parent = nullptr);
    ~DMRIdLookup();
    QString lookup(unsigned int id);
    QString getCallsign(unsigned int id);

signals:

private:
    const Settings* m_settings;
    Logger* m_logger;
    QMap<unsigned int, QString>* m_ids;

};

#endif // DMRIDLOOKUP_H
