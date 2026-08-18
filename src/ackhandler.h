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

#ifndef ACKHANDLER_H
#define ACKHANDLER_H

#include <QObject>
#include <QMap>
#include <QList>
#include <QDebug>

class AckHandler : public QObject
{
    Q_OBJECT
public:
    explicit AckHandler(QObject* parent = nullptr);
    ~AckHandler();
public slots:
    void removeId(unsigned int srcId);
    void addAck(unsigned int srcId, unsigned int type);
    void removeAck(unsigned int srcId, unsigned int type);
    void removeAckType(unsigned int type);
    bool hasAck(unsigned int srcId, unsigned int type);

signals:


private:
    QMap<unsigned int, QList<unsigned int>>* m_uplink_acks;
};

#endif // ACKHANDLER_H
