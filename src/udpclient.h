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
#include "MMDVM/DMRDefines.h"
#include "MMDVM/DMRData.h"
#include "MMDVM/DMRCSBK.h"
#include "MMDVM/DMRSlotType.h"
#include "MMDVM/Sync.h"

#define EXTERNAL_NETWORK_CHANNEL 1000

class UDPClient : public QObject
{
    Q_OBJECT
public:
    explicit UDPClient(const Settings *settings, Logger *logger, unsigned int channel_id,
                       uint16_t local_port=0, uint16_t remote_port=0, QString remote_address="",
                       bool gateway=false, QObject *parent = nullptr);
    ~UDPClient();
    bool isGatewayConnection();

signals:
    void dmrData(unsigned char* dmr_data, int udp_channel_id, bool gateway_connection);
    void newMMDVMConfig(unsigned char* payload, int size);
    void writeToNetwork(unsigned char* payload, int size);

public slots:
    void readPendingDatagrams();
    void enable(bool);
    void handleDisconnect();
    void handleError(QAbstractSocket::SocketError error);
    void writeDataToNetwork(unsigned char *data, int size);
    void writeDMRData(CDMRData &data);
    void writeDMRConfig(QVector<unsigned char> &config);
    void writeDMRTrunkingParams(CDMRData &dmr_control_data);
    void handleStateChange(QAbstractSocket::SocketState state);


private:
    void start();
    void stop();
    bool parseNetworkData(unsigned char* payload, int size);
    QUdpSocket *_udp_socket_tx;
    const Settings *_settings;
    Logger *_logger;
    QMutex _mutex;
    bool _started;
    unsigned int _channel_id;
    bool _gateway_connection;
    unsigned short _listen_port;
    unsigned short _send_port;
    QString _remote_address;

};

#endif // UDPCLIENT_H
