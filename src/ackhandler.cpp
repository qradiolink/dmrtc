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

#include "ackhandler.h"

AckHandler::AckHandler(QObject* parent)
    : QObject{parent}
{
    m_uplink_acks = new QMap<unsigned int, QList<unsigned int>>;
}

AckHandler::~AckHandler()
{
    QMapIterator<unsigned int, QList<unsigned int>> it(*m_uplink_acks);

    while (it.hasNext()) {
        it.next();

        if (it.value().size() > 0) {
            (*m_uplink_acks)[it.key()].clear();
        }

        m_uplink_acks->remove(it.key());
    }

    delete m_uplink_acks;
}

void AckHandler::removeId(unsigned int srcId)
{
    if (m_uplink_acks->contains(srcId)) {
        (*m_uplink_acks)[srcId].clear();
        m_uplink_acks->remove(srcId);
    }
}

void AckHandler::addAck(unsigned int srcId, unsigned int type)
{
    if (m_uplink_acks->contains(srcId)) {
        (*m_uplink_acks)[srcId].append(type);
    } else {
        QList<unsigned int> acks;
        acks.append(type);
        m_uplink_acks->insert(srcId, acks);
    }
}

void AckHandler::removeAck(unsigned int srcId, unsigned int type)
{
    if (m_uplink_acks->contains(srcId)) {
        if ((*m_uplink_acks)[srcId].contains(type)) {
            (*m_uplink_acks)[srcId].removeOne(type);
        }
    }
}

void AckHandler::removeAckType(unsigned int type)
{
    QMapIterator<unsigned int, QList<unsigned int>> it(*m_uplink_acks);

    while (it.hasNext()) {
        it.next();

        if (it.value().size() > 0) {
            if ((*m_uplink_acks)[it.key()].contains(type)) {
                (*m_uplink_acks)[it.key()].removeAll(type);
            }
        }
    }
}

bool AckHandler::hasAck(unsigned int srcId, unsigned int type)
{
    if (m_uplink_acks->contains(srcId)) {
        if ((*m_uplink_acks)[srcId].contains(type)) {
            return true;
        }
    }

    return false;
}
