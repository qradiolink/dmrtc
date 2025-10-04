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
    explicit AckHandler(QObject *parent = nullptr);
    ~AckHandler();
public slots:
    void removeId(unsigned int srcId);
    void addAck(unsigned int srcId, unsigned int type);
    void removeAck(unsigned int srcId, unsigned int type);
    void removeAckType(unsigned int type);
    bool hasAck(unsigned int srcId, unsigned int type);

signals:


private:
    QMap<unsigned int, QList<unsigned int>> *_uplink_acks;
};

#endif // ACKHANDLER_H
