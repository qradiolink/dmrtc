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

#include "udpclient.h"

const unsigned int HOMEBREW_DATA_PACKET_LENGTH = 55U;


UDPClient::UDPClient(const Settings *settings, Logger *logger, unsigned int channel_id,
                     uint16_t local_port, uint16_t remote_port, QString remote_address, bool gateway, QObject *parent) : QObject(parent)
{
    _settings = settings;
    _logger = logger;
    _channel_id = channel_id;
    _gateway_connection = gateway;
    _listen_port = (local_port) ? local_port : _settings->mmdvm_listen_port + _channel_id;
    _send_port = (remote_port) ? remote_port : _settings->mmdvm_send_port + _channel_id;
    _remote_address = (!remote_address.isEmpty()) ? remote_address : _settings->mmdvm_remote_address;
    _started = false;
    _udp_socket_tx = new QUdpSocket();
}

UDPClient::~UDPClient()
{
    delete _udp_socket_tx;
}

bool UDPClient::isGatewayConnection()
{
    return _gateway_connection;
}

void UDPClient::start()
{
    if(_started)
        return;

    bool status;
    if(_listen_port != 0)
    {
        status = _udp_socket_tx->bind(QHostAddress(_settings->udp_local_address), _listen_port);

    }
    else
        status = false;
    if(!status)
    {
        _logger->log(Logger::LogLevelFatal, QString(
            "Server could not bind to port %1, another instance is probably listening already"
            ).arg(_listen_port));
        _started = false;
    }
    else
    {
        //_udp_socket_tx->connectToHost(QHostAddress(_remote_address), _send_port);
        _logger->log(Logger::LogLevelInfo, QString(
            "Listening for data on %1 port %2").arg(_settings->udp_local_address).arg(_listen_port));
        _logger->log(Logger::LogLevelInfo, QString(
            "Sending data to %1 port %2").arg(_remote_address).arg(_send_port));
        QObject::connect(_udp_socket_tx, SIGNAL(readyRead()), this, SLOT(readPendingDatagrams()));
        QObject::connect(_udp_socket_tx, SIGNAL(disconnected()), this, SLOT(handleDisconnect()));
        QObject::connect(_udp_socket_tx, SIGNAL(errorOccurred(QAbstractSocket::SocketError)),
                         this, SLOT(handleError(QAbstractSocket::SocketError)));
        QObject::connect(_udp_socket_tx, SIGNAL(stateChanged(QAbstractSocket::SocketState)),
                         this, SLOT(handleStateChange(QAbstractSocket::SocketState)));
        _started = true;
    }
}

void UDPClient::stop()
{
    if(!_started)
        return;
    _started = false;

     QObject::disconnect(_udp_socket_tx,SIGNAL(readyRead()),this,SLOT(readPendingDatagrams()));
     QObject::disconnect(_udp_socket_tx, SIGNAL(disconnected()), this, SLOT(handleDisconnect()));
     QObject::disconnect(_udp_socket_tx, SIGNAL(errorOccurred(QAbstractSocket::SocketError)),
                      this, SLOT(handleError(QAbstractSocket::SocketError)));
     QObject::disconnect(_udp_socket_tx, SIGNAL(stateChanged(QAbstractSocket::SocketState)),
                      this, SLOT(handleStateChange(QAbstractSocket::SocketState)));
     _udp_socket_tx->close();
     _logger->log(Logger::LogLevelInfo, QString("Stopped listening for data on port %1").arg(_listen_port));
     _logger->log(Logger::LogLevelInfo, QString("Stopped sending data on port %1").arg(_send_port));
}

void UDPClient::enable(bool value)
{
    if(value)
        start();
    else
        stop();
}

void UDPClient::handleDisconnect()
{
    _logger->log(Logger::LogLevelInfo, QString("Disconnected from host %1 port %2").
                 arg(_remote_address).arg(_send_port));
}

void UDPClient::handleError(QAbstractSocket::SocketError error)
{
    _logger->log(Logger::LogLevelInfo, QString("Socket error for %1 port %2: %3").
                 arg(_remote_address).arg(_send_port).arg(error));
}

void UDPClient::handleStateChange(QAbstractSocket::SocketState state)
{
    _logger->log(Logger::LogLevelInfo, QString("Socket state change for %1 port %2: %3").
                 arg(_remote_address).arg(_send_port).arg(state));
}

void UDPClient::readPendingDatagrams()
{
    while (_udp_socket_tx->hasPendingDatagrams())
    {
        QNetworkDatagram datagram = _udp_socket_tx->receiveDatagram();
        if(datagram.isValid())
        {
            QByteArray data = datagram.data();
            int size = data.size();
            unsigned char *payload = new unsigned char[size];
            memcpy(payload, (unsigned char *)data.data(), size * sizeof(unsigned char));
            bool ok = parseNetworkData(payload, size);
            if(!ok)
            {
                //_logger->log(Logger::LogLevelWarning, QString("Could not parse payload %1")
                //             .arg(QString::fromLocal8Bit((const char*)payload, size)));
            }
        }
    }
}

void UDPClient::writeDataToNetwork(unsigned char *data, int size)
{
    if(!_started)
    {
        return;
    }
    _udp_socket_tx->writeDatagram((const char*)data, size, QHostAddress(_remote_address), _send_port);
    //_udp_socket_tx->write((const char*)data, size);
}


bool UDPClient::parseNetworkData(unsigned char* payload, int size)
{
    if(size < 4)
        return false;
    if (memcmp(payload, "DMRD", 4U) == 0)
    {
        emit dmrData(payload, _channel_id, _gateway_connection);
        return true;
    }
    if (memcmp(payload, "DMRC", 4U) == 0)
    {
        if(size < 12)
        {
            return false;
        }
        emit newMMDVMConfig(payload, size);
        return true;
    }
    return false;
}

void UDPClient::writeDMRData(CDMRData &data)
{
    unsigned char buffer[HOMEBREW_DATA_PACKET_LENGTH];
    ::memset(buffer, 0x00U, HOMEBREW_DATA_PACKET_LENGTH);
    buffer[0U]  = 'D';
    buffer[1U]  = 'M';
    buffer[2U]  = 'R';
    buffer[3U]  = 'D';
    buffer[4U] = data.getSeqNo();
    unsigned int srcId = data.getSrcId();
    buffer[5U]  = srcId >> 16;
    buffer[6U]  = srcId >> 8;
    buffer[7U]  = srcId >> 0;

    unsigned int dstId = data.getDstId();
    buffer[8U]  = dstId >> 16;
    buffer[9U]  = dstId >> 8;
    buffer[10U] = dstId >> 0;
    int id = 1234567;
    ::memcpy(buffer + 11U, &id, 4U);

    unsigned int slotNo = data.getSlotNo();

    buffer[15U] = slotNo == 1U ? 0x00U : 0x80U;

    FLCO flco = data.getFLCO();
    buffer[15U] |= flco == FLCO_GROUP ? 0x00U : 0x40U;

    unsigned char dataType = data.getDataType();
    if (dataType == DT_VOICE_SYNC) {
        buffer[15U] |= 0x10U;
    } else if (dataType == DT_VOICE) {
        buffer[15U] |= data.getN();
    } else {
        buffer[15U] |= (0x20U | dataType);
    }

    unsigned int streamId = data.getStreamId();

    ::memcpy(buffer + 16U, &streamId, 4U);

    data.getData(buffer + 20U);

    buffer[53U] = data.getBER();

    buffer[54U] = data.getRSSI();

    writeDataToNetwork(buffer, HOMEBREW_DATA_PACKET_LENGTH);
}

void UDPClient::writeDMRConfig(QVector<unsigned char> &config)
{
    if(config.size() < 8)
    {
        return;
    }
    unsigned char buffer[config.size() + 7U];
    ::memset(buffer, 0x00U, config.size() + 7U);

    buffer[0U]  = 'D';
    buffer[1U]  = 'M';
    buffer[2U]  = 'R';
    buffer[3U]  = 'C';

    unsigned int srcId = 1234567;
    buffer[5U]  = srcId >> 16;
    buffer[6U]  = srcId >> 8;
    buffer[7U]  = srcId >> 0;

    for(int i=0;i<config.size();i++)
    {
        buffer[i + 8U]  = config[i];
    }
    writeDataToNetwork(buffer, config.size() + 7U);
}
