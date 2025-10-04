#include "ackhandler.h"

AckHandler::AckHandler(QObject *parent)
    : QObject{parent}
{
    _uplink_acks = new QMap<unsigned int, QList<unsigned int>>;
}

AckHandler::~AckHandler()
{
    QMapIterator<unsigned int, QList<unsigned int>> it(*_uplink_acks);
    while(it.hasNext())
    {
        it.next();
        if(it.value().size() > 0)
        {
            (*_uplink_acks)[it.key()].clear();
        }
        _uplink_acks->remove(it.key());
    }
    delete _uplink_acks;
}

void AckHandler::removeId(unsigned int srcId)
{
    if(_uplink_acks->contains(srcId))
    {
        (*_uplink_acks)[srcId].clear();
        _uplink_acks->remove(srcId);
    }
}

void AckHandler::addAck(unsigned int srcId, unsigned int type)
{
    if(_uplink_acks->contains(srcId))
    {
        (*_uplink_acks)[srcId].append(type);
    }
    else
    {
        QList<unsigned int> acks;
        acks.append(type);
        _uplink_acks->insert(srcId, acks);
    }
}

void AckHandler::removeAck(unsigned int srcId, unsigned int type)
{
    if(_uplink_acks->contains(srcId))
    {
        if((*_uplink_acks)[srcId].contains(type))
        {
            (*_uplink_acks)[srcId].removeOne(type);
        }
    }
}

void AckHandler::removeAckType(unsigned int type)
{
    QMapIterator<unsigned int, QList<unsigned int>> it(*_uplink_acks);
    while(it.hasNext())
    {
        it.next();
        if(it.value().size() > 0)
        {
            if((*_uplink_acks)[it.key()].contains(type))
            {
                (*_uplink_acks)[it.key()].removeAll(type);
            }
        }
    }
}

bool AckHandler::hasAck(unsigned int srcId, unsigned int type)
{
    if(_uplink_acks->contains(srcId))
    {
        if((*_uplink_acks)[srcId].contains(type))
        {
            return true;
        }
    }
    return false;
}
