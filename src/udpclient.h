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

#ifndef UDPCLIENT_H
#define UDPCLIENT_H

#include <QObject>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QTimer>
#include <QVector>
#include <QMutex>
#include "src/settings.h"
#include "src/dmr_commands.h"
#include "src/logger.h"
#include "src/MMDVM/DMRDefines.h"
#include "src/MMDVM/DMRData.h"
#include "src/MMDVM/DMRCSBK.h"
#include "src/MMDVM/DMRSlotType.h"
#include "src/MMDVM/Sync.h"
#include "src/MMDVM/Utils.h"

#define EXTERNAL_NETWORK_CHANNEL 1000

class UDPClient : public QObject
{
    Q_OBJECT
public:
    explicit UDPClient(const Settings* settings, Logger* logger, unsigned int channel_id,
                       uint16_t local_port = 0, uint16_t remote_port = 0, QString remote_address = "",
                       bool gateway = false, QObject* parent = nullptr);
    ~UDPClient();
    bool isGatewayConnection();

signals:
    void dmrData(unsigned char* dmr_data, unsigned int size, unsigned int udp_channel_id, bool gateway_connection);
    void newDMRNetworkMessage(unsigned char* payload, unsigned int size);
    void writeToNetwork(unsigned char* payload, unsigned int size);

public slots:
    void readPendingDatagrams();
    void enable(bool);
    void handleDisconnect();
    void handleError(QAbstractSocket::SocketError error);
    void writeDataToNetwork(unsigned char* data, unsigned int size);
    void writeDMRData(CDMRData& data);
    void writeDMRConfig(QVector<unsigned char>& config);
    void writeDMRTrunkingParams(CDMRData& dmr_control_data);
    void writeDMRNetMessage(unsigned char* buffer, unsigned int size);
    void handleStateChange(QAbstractSocket::SocketState state);


private:
    void start();
    void stop();
    bool parseNetworkData(unsigned char* payload, unsigned int size);
    QUdpSocket* m_udp_socket_tx;
    const Settings* m_settings;
    Logger* m_logger;
    QMutex m_mutex;
    bool m_started;
    unsigned int m_channel_id;
    bool m_gateway_connection;
    unsigned short m_listen_port;
    unsigned short m_send_port;
    QString m_remote_address;

};

#endif // UDPCLIENT_H
