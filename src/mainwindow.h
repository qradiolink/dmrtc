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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDateTime>
#include <QTableWidgetItem>
#include <QList>
#include <QSet>
#include "src/channelviewmodel.h"
#include "src/logicalchannel.h"
#include "src/settings.h"
#include "src/logger.h"
#include "src/dmridlookup.h"
#include "src/standard_PDU.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(Settings *settings, Logger *logger, DMRIdLookup *_id_lookup, QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    void setLogicalChannels(QVector<LogicalChannel *> *logical_channels);
    void channelStateChange(int row, int col, bool state);
    void updateRegisteredMSList(QList<uint>* registered_ms);
    void updateTalkgroupSubscriptionList(QSet<unsigned int>* subscribed_talkgroups);
    void updateRejectedCallsList(unsigned int srcId, unsigned int dstId, bool local_call);
    void requestRegistration();
    void sendSystemMessage();
    void sendMessageToRadio();
    void addDGNA();
    void updateCallLog(unsigned int srcId, unsigned int dstId, int rssi, float ber, bool private_call);
    void updateMessageLog(unsigned int srcId, unsigned int dstId, QString message, bool tg);
    void saveConfig();
    void saveTalkgroupRouting();
    void saveCallPriorities();
    void deleteTalkgroupRow();
    void addTalkgroupRow();
    void deleteCallPriorityRow();
    void addCallPriorityRow();
    void deleteSlotRewrite();
    void addSlotRewrite();
    void saveSlotRewrite();
    void saveLogicalPhysicalChannels();
    void addLogicalPhysicalChannel();
    void deleteLogicalPhysicalChannel();
    void saveAdjacentSites();
    void addAdjacentSite();
    void deleteAdjacentSite();
    void saveServiceIds();
    void deleteServiceId();
    void addServiceId();
    void sendPing();
    void sendNMEAPoll();
    void sendStatusPoll();
    void authCheck();
    void displayPingResponse(unsigned int srcId, unsigned int msec);
    void displayPositionResponse(unsigned int srcId, QString message);
    void pingTimeout();
    void authSuccess(bool successful);
    void sendLocalTimeBroadcast();
    void sendFrequenciesBroadcast();

signals:
    void displayInitialized();
    void channelEnable(unsigned int channel_index, bool state);
    void registrationRequested();
    void sendShortMessage(QString message, unsigned int target);
    void sendDGNA(QString tgids, unsigned int target);
    void pingRadio(unsigned int target_id, bool group);
    void pollData(unsigned int target_id, unsigned int location_format);
    void pollStatus(unsigned int target_id);
    void sendAuthCheck(unsigned int target_id);
    void broadcastLocalTime();
    void broadcastFrequencies();

private:
    Ui::MainWindow *ui;
    Settings *_settings;
    Logger *_logger;
    ChannelViewModel *_logical_channel_model;
    DMRIdLookup *_id_lookup;
    void setConfig();
    void loadTalkgroupRouting();
    void loadCallPriorities();
    void loadSlotRewrite();
    void loadLogicalPhysicalChannels();
    void loadAdjacentSites();
    void loadServiceIds();
    void deleteRegisteredMSList();
    void deleteSubscribedTalkgroupList();

};

#endif // MAINWINDOW_H
