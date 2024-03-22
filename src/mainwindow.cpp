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

#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(Settings *settings, Logger *logger, DMRIdLookup *id_lookup, QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle("DMR tier III Trunking controller");
    _settings = settings;
    _logger = logger;
    _id_lookup = id_lookup;
    _logical_channel_model = new ChannelViewModel;

    QObject::connect(_logical_channel_model, SIGNAL(channelStateChange(int,int,bool)),
                     this, SLOT(channelStateChange(int,int,bool)));
    QObject::connect(ui->requestRegistrationButton, SIGNAL(clicked(bool)), this, SLOT(requestRegistration()));
    QObject::connect(ui->pushButtonPingRadio, SIGNAL(clicked(bool)), this, SLOT(sendPing()));
    QObject::connect(ui->pushButtonSaveSettings, SIGNAL(clicked(bool)), this, SLOT(saveConfig()));
    QObject::connect(ui->pushButtonSendSystemMessage, SIGNAL(clicked(bool)), this, SLOT(sendSystemMessage()));
    QObject::connect(ui->pushButtonSendMessageToRadio, SIGNAL(clicked(bool)), this, SLOT(sendMessageToRadio()));
    QObject::connect(ui->pushButtonSendDGNA, SIGNAL(clicked(bool)), this, SLOT(addDGNA()));
    QObject::connect(ui->pushButtonUDTPoll, SIGNAL(clicked(bool)), this, SLOT(sendUDTPoll()));
    QObject::connect(ui->pushButtonStatusPoll, SIGNAL(clicked(bool)), this, SLOT(sendStatusPoll()));
    QObject::connect(ui->pushButtonAuthCheck, SIGNAL(clicked(bool)), this, SLOT(authCheck()));
    QObject::connect(ui->pushButtonBroadcastTime, SIGNAL(clicked(bool)), this, SLOT(sendLocalTimeBroadcast()));
    QObject::connect(ui->pushButtonBroadcastFrequencies, SIGNAL(clicked(bool)), this, SLOT(sendFrequenciesBroadcast()));
    QObject::connect(ui->pushButtonRemoveTalkgroupRoute, SIGNAL(clicked(bool)),
                     this, SLOT(deleteTalkgroupRow()));
    QObject::connect(ui->pushButtonAddTalkgroupRoute, SIGNAL(clicked(bool)),
                     this, SLOT(addTalkgroupRow()));
    QObject::connect(ui->pushButtonRemoveCallPriority, SIGNAL(clicked(bool)),
                     this, SLOT(deleteCallPriorityRow()));
    QObject::connect(ui->pushButtonAddCallPriority, SIGNAL(clicked(bool)),
                     this, SLOT(addCallPriorityRow()));
    QObject::connect(ui->pushButtonRemoveSlotRoute, SIGNAL(clicked(bool)),
                     this, SLOT(deleteSlotRewrite()));
    QObject::connect(ui->pushButtonAddSlotRoute, SIGNAL(clicked(bool)),
                     this, SLOT(addSlotRewrite()));
    QObject::connect(ui->pushButtonRemoveLogicalPhysicalChannel, SIGNAL(clicked(bool)),
                     this, SLOT(deleteLogicalPhysicalChannel()));
    QObject::connect(ui->pushButtonAddLogicalPhysicalChannel, SIGNAL(clicked(bool)),
                     this, SLOT(addLogicalPhysicalChannel()));
    QObject::connect(ui->pushButtonRemoveAdjacentSite, SIGNAL(clicked(bool)),
                     this, SLOT(deleteAdjacentSite()));
    QObject::connect(ui->pushButtonAddAjacentSite, SIGNAL(clicked(bool)),
                     this, SLOT(addAdjacentSite()));
    QObject::connect(ui->pushButtonRemoveServiceId, SIGNAL(clicked(bool)),
                     this, SLOT(deleteServiceId()));
    QObject::connect(ui->pushButtonAddServiceId, SIGNAL(clicked(bool)),
                     this, SLOT(addServiceId()));
    QObject::connect(ui->pushButtonGroupFirst, SIGNAL(clicked(bool)),
                     ui->groupCallsTableWidget, SLOT(scrollToTop()));
    QObject::connect(ui->pushButtonGroupLast, SIGNAL(clicked(bool)),
                     ui->groupCallsTableWidget, SLOT(scrollToBottom()));
    QObject::connect(ui->pushButtonPrivateFirst, SIGNAL(clicked(bool)),
                     ui->privateCallsTableWidget, SLOT(scrollToTop()));
    QObject::connect(ui->pushButtonPrivateLast, SIGNAL(clicked(bool)),
                     ui->privateCallsTableWidget, SLOT(scrollToBottom()));


    ui->tabWidgetSettings->setCurrentIndex(0);
    ui->tabWidgetDashboard->setCurrentIndex(0);
    ui->channelTableView->setModel(_logical_channel_model);
    ui->channelTableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->channelTableView->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->channelTableView->verticalHeader()->setVisible(true);
    ui->privateCallsTableWidget->setColumnCount(5);
    ui->groupCallsTableWidget->setColumnCount(5);
    ui->privateCallsTableWidget->horizontalHeader()->resizeSections(QHeaderView::ResizeMode::Stretch);
    ui->groupCallsTableWidget->horizontalHeader()->resizeSections(QHeaderView::ResizeMode::Stretch);
    ui->privateCallsTableWidget->horizontalHeader()->setStretchLastSection(true);
    ui->groupCallsTableWidget->horizontalHeader()->setStretchLastSection(true);
    ui->privateCallsTableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->groupCallsTableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidgetMessages->setColumnCount(4);
    ui->tableWidgetMessages->horizontalHeader()->resizeSections(QHeaderView::ResizeMode::Stretch);
    ui->tableWidgetMessages->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    QStringList header_private;
    header_private.append("Date and Time");
    header_private.append("Source Id");
    header_private.append("Destination Id");
    header_private.append("RSSI");
    header_private.append("BER");
    ui->privateCallsTableWidget->setHorizontalHeaderLabels(header_private);
    QStringList header_group;
    header_group.append("Date and Time");
    header_group.append("Source Id");
    header_group.append("Destination Id");
    header_group.append("RSSI");
    header_group.append("BER");
    ui->groupCallsTableWidget->setHorizontalHeaderLabels(header_group);
    QStringList header_messages;
    header_messages.append("Date and Time");
    header_messages.append("Source Id");
    header_messages.append("Destination Id");
    header_messages.append("Messages");
    ui->tableWidgetMessages->setHorizontalHeaderLabels(header_messages);
    setConfig();
}

MainWindow::~MainWindow()
{
    for(int i=0;i < ui->privateCallsTableWidget->rowCount();i++)
    {
        QTableWidgetItem *dateitem = ui->privateCallsTableWidget->item(i, 0);
        delete dateitem;
        QTableWidgetItem *srcitem = ui->privateCallsTableWidget->item(i, 1);
        delete srcitem;
        QTableWidgetItem *dstitem = ui->privateCallsTableWidget->item(i, 2);
        delete dstitem;
    }
    ui->privateCallsTableWidget->clear();
    for(int i=0;i < ui->groupCallsTableWidget->rowCount();i++)
    {
        QTableWidgetItem *dateitem = ui->groupCallsTableWidget->item(i, 0);
        delete dateitem;
        QTableWidgetItem *srcitem = ui->groupCallsTableWidget->item(i, 1);
        delete srcitem;
        QTableWidgetItem *dstitem = ui->groupCallsTableWidget->item(i, 2);
        delete dstitem;
    }
    ui->groupCallsTableWidget->clear();
    for(int i=0;i < ui->tableWidgetMessages->rowCount();i++)
    {
        QTableWidgetItem *dateitem = ui->tableWidgetMessages->item(i, 0);
        delete dateitem;
        QTableWidgetItem *srcitem = ui->tableWidgetMessages->item(i, 1);
        delete srcitem;
        QTableWidgetItem *dstitem = ui->tableWidgetMessages->item(i, 2);
        delete dstitem;
        QTableWidgetItem *msgitem = ui->tableWidgetMessages->item(i, 3);
        delete msgitem;
    }
    ui->tableWidgetMessages->clear();
    ui->channelTableView->reset();
    delete _logical_channel_model;
    delete ui;

}

void MainWindow::setConfig()
{
    ui->lineEditLocalIPAddress->setText(_settings->udp_local_address);
    ui->lineEditMMDVMAddress->setText(_settings->mmdvm_remote_address);
    ui->lineEditGatewayAddress->setText(_settings->gateway_remote_address);
    ui->lineEditMMDVMListenBasePort->setText(QString::number(_settings->mmdvm_listen_port));
    ui->lineEditMMDVMSendBasePort->setText(QString::number(_settings->mmdvm_send_port));
    ui->lineEditGatewayListenBasePort->setText(QString::number(_settings->gateway_listen_port));
    ui->lineEditGatewaySendBasePort->setText(QString::number(_settings->gateway_send_port));
    ui->lineEditNumberOfChannels->setText(QString::number(_settings->channel_number));
    ui->lineEditControlChannelPhysicalId->setText(QString::number(_settings->control_channel_physical_id));
    ui->spinBoxControlChannelSlot->setValue(_settings->control_channel_slot);
    ui->lineEditNumberOfGateways->setText(QString::number(_settings->gateway_number));
    ui->lineEditPayloadChannelTimeout->setText(QString::number(_settings->payload_channel_idle_timeout));
    ui->lineEditSystemCode->setText(QString::number(_settings->system_identity_code));
    ui->lineEditAnnounceSystemFreqsTime->setText(QString::number(_settings->announce_system_freqs_interval));
    ui->lineEditAnnounceLateEntryInterval->setText(QString::number(_settings->announce_late_entry_interval));
    ui->lineEditAnnounceAdjacentBSInterval->setText(QString::number(_settings->announce_adjacent_bs_interval));
    ui->lineEditBaseFrequency->setText(QString::number(_settings->freq_base));
    ui->lineEditFrequencySeparation->setText(QString::number(_settings->freq_separation));
    ui->lineEditDuplexSplit->setText(QString::number(_settings->freq_duplexsplit));
    ui->textEditSystemMessage->setText(_settings->system_announcement_message);
    ui->checkBoxAnnouncePriority->setChecked((bool)_settings->announce_priority);
    ui->checkBoxAbsoluteGrants->setChecked((bool)_settings->use_absolute_channel_grants);
    ui->checkBoxFixedChannelPlan->setChecked((bool)_settings->use_fixed_channel_plan);
    ui->checkBoxGatewayEnabled->setChecked((bool)_settings->gateway_enabled);
    ui->checkBoxAnnounceSystemMessage->setChecked((bool)_settings->announce_system_message);
    ui->checkBoxPreventMMDVMOverflows->setChecked((bool)_settings->prevent_mmdvm_overflows);
    ui->checkBoxReceiveAttachments->setChecked((bool)_settings->receive_tg_attach);
    ui->checkBoxTransmitSubscribedTGOnly->setChecked((bool)_settings->transmit_subscribed_tg_only);

    loadTalkgroupRouting();
    loadCallPriorities();
    loadSlotRewrite();
    loadLogicalPhysicalChannels();
    loadAdjacentSites();
    loadServiceIds();
}

void MainWindow::saveConfig()
{
    _settings->udp_local_address = ui->lineEditLocalIPAddress->text();
    _settings->mmdvm_remote_address = ui->lineEditMMDVMAddress->text();
    _settings->gateway_remote_address = ui->lineEditGatewayAddress->text();
    _settings->mmdvm_listen_port = ui->lineEditMMDVMListenBasePort->text().toInt();
    _settings->mmdvm_send_port = ui->lineEditMMDVMSendBasePort->text().toInt();
    _settings->gateway_listen_port = ui->lineEditGatewayListenBasePort->text().toInt();
    _settings->gateway_send_port = ui->lineEditGatewaySendBasePort->text().toInt();
    _settings->channel_number = ui->lineEditNumberOfChannels->text().toInt();
    _settings->control_channel_physical_id = ui->lineEditControlChannelPhysicalId->text().toInt();
    _settings->control_channel_slot = ui->spinBoxControlChannelSlot->value();
    _settings->gateway_number = ui->lineEditNumberOfGateways->text().toInt();
    _settings->payload_channel_idle_timeout = ui->lineEditPayloadChannelTimeout->text().toInt();
    _settings->system_identity_code = ui->lineEditSystemCode->text().toInt();
    _settings->announce_system_freqs_interval = ui->lineEditAnnounceSystemFreqsTime->text().toInt();
    _settings->announce_late_entry_interval = ui->lineEditAnnounceLateEntryInterval->text().toInt();
    _settings->announce_adjacent_bs_interval = ui->lineEditAnnounceAdjacentBSInterval->text().toInt();
    _settings->freq_base = ui->lineEditBaseFrequency->text().toInt();
    _settings->freq_separation = ui->lineEditFrequencySeparation->text().toInt();
    _settings->freq_duplexsplit = ui->lineEditDuplexSplit->text().toInt();
    _settings->system_announcement_message = ui->textEditSystemMessage->toPlainText();
    _settings->announce_priority = (int)ui->checkBoxAnnouncePriority->isChecked();
    _settings->use_absolute_channel_grants = (int)ui->checkBoxAbsoluteGrants->isChecked();
    _settings->use_fixed_channel_plan = (int)ui->checkBoxFixedChannelPlan->isChecked();
    _settings->gateway_enabled = (int)ui->checkBoxGatewayEnabled->isChecked();
    _settings->announce_system_message = (int)ui->checkBoxAnnounceSystemMessage->isChecked();
    _settings->prevent_mmdvm_overflows = (int)ui->checkBoxPreventMMDVMOverflows->isChecked();
    _settings->receive_tg_attach = (int)ui->checkBoxReceiveAttachments->isChecked();
    _settings->transmit_subscribed_tg_only = (int)ui->checkBoxTransmitSubscribedTGOnly->isChecked();

    saveTalkgroupRouting();
    saveCallPriorities();
    saveSlotRewrite();
    saveLogicalPhysicalChannels();
    saveAdjacentSites();
    saveServiceIds();
    _settings->saveConfig();
}

void MainWindow::loadTalkgroupRouting()
{
    QMapIterator<unsigned int, unsigned int> i(_settings->talkgroup_routing_table);
    QStringList header_tg_routing;
    header_tg_routing.append("Talkgroup");
    header_tg_routing.append("Gateway Id");
    ui->tableWidgetTalkgroupRouting->setRowCount(_settings->talkgroup_routing_table.size());
    ui->tableWidgetTalkgroupRouting->setColumnCount(2);
    ui->tableWidgetTalkgroupRouting->setHorizontalHeaderLabels(header_tg_routing);
    ui->tableWidgetTalkgroupRouting->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidgetTalkgroupRouting->horizontalHeader()->resizeSections(QHeaderView::ResizeMode::Stretch);
    int row = 0;
    while(i.hasNext())
    {
        i.next();
        QTableWidgetItem *tg = new QTableWidgetItem(QString::number(i.key()));
        QTableWidgetItem *gateway = new QTableWidgetItem(QString::number(i.value()));

        ui->tableWidgetTalkgroupRouting->setItem(row, 0, tg);
        ui->tableWidgetTalkgroupRouting->setItem(row, 1, gateway);
        row++;
    }
}

void MainWindow::saveTalkgroupRouting()
{
    _settings->talkgroup_routing_table.clear();
    int rows = ui->tableWidgetTalkgroupRouting->rowCount();
    for(int i=0;i<rows;i++)
    {
        QTableWidgetItem *item1 = ui->tableWidgetTalkgroupRouting->item(i, 0);
        QTableWidgetItem *item2 = ui->tableWidgetTalkgroupRouting->item(i, 1);
        bool ok1, ok2 = false;
        if(item1->text().size() > 0 && item2->text().size() > 0)
        {
            item1->text().toInt(&ok1);
            item2->text().toInt(&ok2);
        }
        if(ok1 && ok2)
        {
            _settings->talkgroup_routing_table.insert(item1->text().toInt(), item2->text().toInt());
        }
    }
}

void MainWindow::addTalkgroupRow()
{
    ui->tableWidgetTalkgroupRouting->setRowCount(ui->tableWidgetTalkgroupRouting->rowCount() + 1);
    QTableWidgetItem *tg = new QTableWidgetItem(QString(""));
    QTableWidgetItem *gateway = new QTableWidgetItem(QString(""));

    ui->tableWidgetTalkgroupRouting->setItem(ui->tableWidgetTalkgroupRouting->rowCount() - 1, 0, tg);
    ui->tableWidgetTalkgroupRouting->setItem(ui->tableWidgetTalkgroupRouting->rowCount() - 1, 1, gateway);
    ui->tableWidgetTalkgroupRouting->scrollToBottom();
}

void MainWindow::deleteTalkgroupRow()
{
    QList<QTableWidgetItem*> items = ui->tableWidgetTalkgroupRouting->selectedItems();
    QSet<int> rows;
    for(QTableWidgetItem* item : items)
    {
        int row = item->row();
        rows.insert(row);
    }
    for(int row : rows)
    {
        ui->tableWidgetTalkgroupRouting->removeRow(row);
    }
}

void MainWindow::loadCallPriorities()
{
    QMapIterator<unsigned int, unsigned int> i(_settings->call_priorities);
    QStringList header_call_priorities;
    header_call_priorities.append("Id");
    header_call_priorities.append("Priority");
    ui->tableWidgetCallPriorities->setRowCount(_settings->call_priorities.size());
    ui->tableWidgetCallPriorities->setColumnCount(2);
    ui->tableWidgetCallPriorities->setHorizontalHeaderLabels(header_call_priorities);
    ui->tableWidgetCallPriorities->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidgetCallPriorities->horizontalHeader()->resizeSections(QHeaderView::ResizeMode::Stretch);
    int row = 0;
    while(i.hasNext())
    {
        i.next();
        QTableWidgetItem *id = new QTableWidgetItem(QString::number(i.key()));
        QTableWidgetItem *priority = new QTableWidgetItem(QString::number(i.value()));

        ui->tableWidgetCallPriorities->setItem(row, 0, id);
        ui->tableWidgetCallPriorities->setItem(row, 1, priority);
        row++;
    }
}

void MainWindow::saveCallPriorities()
{
    _settings->call_priorities.clear();
    int rows = ui->tableWidgetCallPriorities->rowCount();
    for(int i=0;i<rows;i++)
    {
        QTableWidgetItem *item1 = ui->tableWidgetCallPriorities->item(i, 0);
        QTableWidgetItem *item2 = ui->tableWidgetCallPriorities->item(i, 1);
        bool ok1, ok2 = false;
        if(item1->text().size() > 0 && item2->text().size() > 0)
        {
            item1->text().toInt(&ok1);
            item2->text().toInt(&ok2);
        }
        if(ok1 && ok2)
        {
            _settings->call_priorities.insert(item1->text().toInt(), item2->text().toInt());
        }
    }
}

void MainWindow::addCallPriorityRow()
{
    ui->tableWidgetCallPriorities->setRowCount(ui->tableWidgetCallPriorities->rowCount() + 1);
    QTableWidgetItem *id = new QTableWidgetItem(QString(""));
    QTableWidgetItem *priority = new QTableWidgetItem(QString(""));

    ui->tableWidgetCallPriorities->setItem(ui->tableWidgetCallPriorities->rowCount() - 1, 0, id);
    ui->tableWidgetCallPriorities->setItem(ui->tableWidgetCallPriorities->rowCount() - 1, 1, priority);
    ui->tableWidgetCallPriorities->scrollToBottom();
}

void MainWindow::deleteCallPriorityRow()
{
    QList<QTableWidgetItem*> items = ui->tableWidgetCallPriorities->selectedItems();
    QSet<int> rows;
    for(QTableWidgetItem* item : items)
    {
        int row = item->row();
        rows.insert(row);
    }
    for(int row : rows)
    {
        ui->tableWidgetCallPriorities->removeRow(row);
    }
}

void MainWindow::loadSlotRewrite()
{
    QMapIterator<unsigned int, unsigned int> i(_settings->slot_rewrite_table);
    QStringList header_slot_rewrite;
    header_slot_rewrite.append("Talkgroup");
    header_slot_rewrite.append("Network Timeslot");
    ui->tableWidgetSlotRewrite->verticalHeader()->setVisible(false);
    ui->tableWidgetSlotRewrite->setRowCount(_settings->slot_rewrite_table.size());
    ui->tableWidgetSlotRewrite->setColumnCount(2);
    ui->tableWidgetSlotRewrite->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidgetSlotRewrite->horizontalHeader()->resizeSections(QHeaderView::ResizeMode::Stretch);
    ui->tableWidgetSlotRewrite->setHorizontalHeaderLabels(header_slot_rewrite);
    int row = 0;
    while(i.hasNext())
    {
        i.next();
        QTableWidgetItem *tg = new QTableWidgetItem(QString::number(i.key()));
        QTableWidgetItem *slot = new QTableWidgetItem(QString::number(i.value()));

        ui->tableWidgetSlotRewrite->setItem(row, 0, tg);
        ui->tableWidgetSlotRewrite->setItem(row, 1, slot);
        row++;
    }
}

void MainWindow::saveSlotRewrite()
{
    _settings->slot_rewrite_table.clear();
    int rows = ui->tableWidgetSlotRewrite->rowCount();
    for(int i=0;i<rows;i++)
    {
        QTableWidgetItem *item1 = ui->tableWidgetSlotRewrite->item(i, 0);
        QTableWidgetItem *item2 = ui->tableWidgetSlotRewrite->item(i, 1);
        bool ok1, ok2 = false;
        if(item1->text().size() > 0 && item2->text().size() > 0)
        {
            item1->text().toInt(&ok1);
            item2->text().toInt(&ok2);
        }
        if(ok1 && ok2)
        {
            _settings->slot_rewrite_table.insert(item1->text().toInt(), item2->text().toInt());
        }
    }
}

void MainWindow::addSlotRewrite()
{
    ui->tableWidgetSlotRewrite->setRowCount(ui->tableWidgetSlotRewrite->rowCount() + 1);
    QTableWidgetItem *tg = new QTableWidgetItem(QString(""));
    QTableWidgetItem *slot = new QTableWidgetItem(QString(""));

    ui->tableWidgetSlotRewrite->setItem(ui->tableWidgetSlotRewrite->rowCount() - 1, 0, tg);
    ui->tableWidgetSlotRewrite->setItem(ui->tableWidgetSlotRewrite->rowCount() - 1, 1, slot);
    ui->tableWidgetSlotRewrite->scrollToBottom();
}

void MainWindow::deleteSlotRewrite()
{
    QList<QTableWidgetItem*> items = ui->tableWidgetSlotRewrite->selectedItems();
    QSet<int> rows;
    for(QTableWidgetItem* item : items)
    {
        int row = item->row();
        rows.insert(row);
    }
    for(int row : rows)
    {
        ui->tableWidgetSlotRewrite->removeRow(row);
    }
}

void MainWindow::loadLogicalPhysicalChannels()
{
    QStringList header_lpc;
    header_lpc.append("Channel id");
    header_lpc.append("Logical channel");
    header_lpc.append("RX Frequency");
    header_lpc.append("TX Frequency");
    header_lpc.append("Colour code");
    ui->tableWidgetLogicalPhysicalChannels->setColumnCount(5);
    ui->tableWidgetLogicalPhysicalChannels->setRowCount(_settings->logical_physical_channels.size());
    ui->tableWidgetLogicalPhysicalChannels->verticalHeader()->setVisible(false);
    ui->tableWidgetLogicalPhysicalChannels->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidgetLogicalPhysicalChannels->horizontalHeader()->resizeSections(QHeaderView::ResizeMode::Stretch);
    ui->tableWidgetLogicalPhysicalChannels->setHorizontalHeaderLabels(header_lpc);

    QListIterator<QMap<QString, uint64_t>> it_lpc(_settings->logical_physical_channels);
    int row = 0;
    while(it_lpc.hasNext())
    {
        QMap<QString, uint64_t> channel_map = it_lpc.next();
        QTableWidgetItem *id = new QTableWidgetItem(QString::number(channel_map.value("channel_id")));
        QTableWidgetItem *lc = new QTableWidgetItem(QString::number(channel_map.value("logical_channel")));
        QTableWidgetItem *rx_freq = new QTableWidgetItem(QString::number(channel_map.value("rx_freq")));
        QTableWidgetItem *tx_freq = new QTableWidgetItem(QString::number(channel_map.value("tx_freq")));
        QTableWidgetItem *cc = new QTableWidgetItem(QString::number(channel_map.value("colour_code")));

        ui->tableWidgetLogicalPhysicalChannels->setItem(row, 0, id);
        ui->tableWidgetLogicalPhysicalChannels->setItem(row, 1, lc);
        ui->tableWidgetLogicalPhysicalChannels->setItem(row, 2, rx_freq);
        ui->tableWidgetLogicalPhysicalChannels->setItem(row, 3, tx_freq);
        ui->tableWidgetLogicalPhysicalChannels->setItem(row, 4, cc);
        row++;
    }
}

void MainWindow::saveLogicalPhysicalChannels()
{
    _settings->logical_physical_channels.clear();
    int rows = ui->tableWidgetLogicalPhysicalChannels->rowCount();
    for(int i=0;i<rows;i++)
    {
        QTableWidgetItem *item1 = ui->tableWidgetLogicalPhysicalChannels->item(i, 0);
        QTableWidgetItem *item2 = ui->tableWidgetLogicalPhysicalChannels->item(i, 1);
        QTableWidgetItem *item3 = ui->tableWidgetLogicalPhysicalChannels->item(i, 2);
        QTableWidgetItem *item4 = ui->tableWidgetLogicalPhysicalChannels->item(i, 3);
        QTableWidgetItem *item5 = ui->tableWidgetLogicalPhysicalChannels->item(i, 4);
        bool ok1, ok2, ok3, ok4, ok5 = false;
        if(item1->text().size() > 0 && item2->text().size() > 0 && item3->text().size() > 0
                && item4->text().size() > 0 && item5->text().size() > 0)
        {
            item1->text().toInt(&ok1);
            item2->text().toInt(&ok2);
            item3->text().toInt(&ok3);
            item4->text().toInt(&ok4);
            item5->text().toInt(&ok5);
        }
        if(ok1 && ok2 && ok3 && ok4)
        {
            QMap<QString, uint64_t> map;
            map.insert("channel_id", item1->text().toInt());
            map.insert("logical_channel", item2->text().toInt());
            map.insert("rx_freq", item3->text().toInt());
            map.insert("tx_freq", item4->text().toInt());
            map.insert("colour_code", item5->text().toInt());
            _settings->logical_physical_channels.append(map);
        }
    }
}

void MainWindow::addLogicalPhysicalChannel()
{
    ui->tableWidgetLogicalPhysicalChannels->setRowCount(ui->tableWidgetLogicalPhysicalChannels->rowCount() + 1);
    QTableWidgetItem *id = new QTableWidgetItem(QString(""));
    QTableWidgetItem *lc = new QTableWidgetItem(QString(""));
    QTableWidgetItem *rx_freq = new QTableWidgetItem(QString(""));
    QTableWidgetItem *tx_freq = new QTableWidgetItem(QString(""));
    QTableWidgetItem *cc = new QTableWidgetItem(QString(""));
    ui->tableWidgetLogicalPhysicalChannels->setItem(ui->tableWidgetLogicalPhysicalChannels->rowCount() - 1, 0, id);
    ui->tableWidgetLogicalPhysicalChannels->setItem(ui->tableWidgetLogicalPhysicalChannels->rowCount() - 1, 1, lc);
    ui->tableWidgetLogicalPhysicalChannels->setItem(ui->tableWidgetLogicalPhysicalChannels->rowCount() - 1, 2, rx_freq);
    ui->tableWidgetLogicalPhysicalChannels->setItem(ui->tableWidgetLogicalPhysicalChannels->rowCount() - 1, 3, tx_freq);
    ui->tableWidgetLogicalPhysicalChannels->setItem(ui->tableWidgetLogicalPhysicalChannels->rowCount() - 1, 4, cc);
    ui->tableWidgetLogicalPhysicalChannels->scrollToBottom();
}

void MainWindow::deleteLogicalPhysicalChannel()
{
    QList<QTableWidgetItem*> items = ui->tableWidgetLogicalPhysicalChannels->selectedItems();
    QSet<int> rows;
    for(QTableWidgetItem* item : items)
    {
        int row = item->row();
        rows.insert(row);
    }
    for(int row : rows)
    {
        ui->tableWidgetLogicalPhysicalChannels->removeRow(row);
    }
}

void MainWindow::loadAdjacentSites()
{
    QStringList header_lpc;
    header_lpc.append("System id");
    header_lpc.append("Logical channel");
    header_lpc.append("RX Frequency");
    header_lpc.append("TX Frequency");
    header_lpc.append("Colour code");
    ui->tableWidgetAjacentSites->setColumnCount(5);
    ui->tableWidgetAjacentSites->setRowCount(_settings->adjacent_sites.size());
    ui->tableWidgetAjacentSites->verticalHeader()->setVisible(false);
    ui->tableWidgetAjacentSites->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidgetAjacentSites->horizontalHeader()->resizeSections(QHeaderView::ResizeMode::Stretch);
    ui->tableWidgetAjacentSites->setHorizontalHeaderLabels(header_lpc);

    QListIterator<QMap<QString, uint64_t>> it_lpc(_settings->adjacent_sites);
    int row = 0;
    while(it_lpc.hasNext())
    {
        QMap<QString, uint64_t> channel_map = it_lpc.next();
        QTableWidgetItem *id = new QTableWidgetItem(QString::number(channel_map.value("system_id")));
        QTableWidgetItem *lc = new QTableWidgetItem(QString::number(channel_map.value("logical_channel")));
        QTableWidgetItem *rx_freq = new QTableWidgetItem(QString::number(channel_map.value("rx_freq")));
        QTableWidgetItem *tx_freq = new QTableWidgetItem(QString::number(channel_map.value("tx_freq")));
        QTableWidgetItem *cc = new QTableWidgetItem(QString::number(channel_map.value("colour_code")));

        ui->tableWidgetAjacentSites->setItem(row, 0, id);
        ui->tableWidgetAjacentSites->setItem(row, 1, lc);
        ui->tableWidgetAjacentSites->setItem(row, 2, rx_freq);
        ui->tableWidgetAjacentSites->setItem(row, 3, tx_freq);
        ui->tableWidgetAjacentSites->setItem(row, 4, cc);
        row++;
    }
}

void MainWindow::saveAdjacentSites()
{
    _settings->adjacent_sites.clear();
    int rows = ui->tableWidgetAjacentSites->rowCount();
    for(int i=0;i<rows;i++)
    {
        QTableWidgetItem *item1 = ui->tableWidgetAjacentSites->item(i, 0);
        QTableWidgetItem *item2 = ui->tableWidgetAjacentSites->item(i, 1);
        QTableWidgetItem *item3 = ui->tableWidgetAjacentSites->item(i, 2);
        QTableWidgetItem *item4 = ui->tableWidgetAjacentSites->item(i, 3);
        QTableWidgetItem *item5 = ui->tableWidgetAjacentSites->item(i, 4);
        bool ok1, ok2, ok3, ok4, ok5 = false;
        if(item1->text().size() > 0 && item2->text().size() > 0 && item3->text().size() > 0
                && item4->text().size() > 0 && item5->text().size() > 0)
        {
            item1->text().toInt(&ok1);
            item2->text().toInt(&ok2);
            item3->text().toInt(&ok3);
            item4->text().toInt(&ok4);
            item5->text().toInt(&ok5);
        }
        if(ok1 && ok2 && ok3 && ok4)
        {
            QMap<QString, uint64_t> map;
            map.insert("system_id", item1->text().toInt());
            map.insert("logical_channel", item2->text().toInt());
            map.insert("rx_freq", item3->text().toInt());
            map.insert("tx_freq", item4->text().toInt());
            map.insert("colour_code", item5->text().toInt());
            _settings->adjacent_sites.append(map);
        }
    }
}

void MainWindow::addAdjacentSite()
{
    ui->tableWidgetAjacentSites->setRowCount(ui->tableWidgetAjacentSites->rowCount() + 1);
    QTableWidgetItem *id = new QTableWidgetItem(QString(""));
    QTableWidgetItem *lc = new QTableWidgetItem(QString(""));
    QTableWidgetItem *rx_freq = new QTableWidgetItem(QString(""));
    QTableWidgetItem *tx_freq = new QTableWidgetItem(QString(""));
    QTableWidgetItem *cc = new QTableWidgetItem(QString(""));
    ui->tableWidgetAjacentSites->setItem(ui->tableWidgetAjacentSites->rowCount() - 1, 0, id);
    ui->tableWidgetAjacentSites->setItem(ui->tableWidgetAjacentSites->rowCount() - 1, 1, lc);
    ui->tableWidgetAjacentSites->setItem(ui->tableWidgetAjacentSites->rowCount() - 1, 2, rx_freq);
    ui->tableWidgetAjacentSites->setItem(ui->tableWidgetAjacentSites->rowCount() - 1, 3, tx_freq);
    ui->tableWidgetAjacentSites->setItem(ui->tableWidgetAjacentSites->rowCount() - 1, 4, cc);
    ui->tableWidgetAjacentSites->scrollToBottom();
}

void MainWindow::deleteAdjacentSite()
{
    QList<QTableWidgetItem*> items = ui->tableWidgetAjacentSites->selectedItems();
    QSet<int> rows;
    for(QTableWidgetItem* item : items)
    {
        int row = item->row();
        rows.insert(row);
    }
    for(int row : rows)
    {
        ui->tableWidgetAjacentSites->removeRow(row);
    }
}

void MainWindow::loadServiceIds()
{
    QMapIterator<QString, unsigned int> i(_settings->service_ids);
    QStringList header_service_ids;
    header_service_ids.append("Service");
    header_service_ids.append("System Id");
    ui->tableWidgetServiceIds->setRowCount(_settings->service_ids.size());
    ui->tableWidgetServiceIds->setColumnCount(2);
    ui->tableWidgetServiceIds->setHorizontalHeaderLabels(header_service_ids);
    ui->tableWidgetServiceIds->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidgetServiceIds->horizontalHeader()->resizeSections(QHeaderView::ResizeMode::Stretch);
    int row = 0;
    while(i.hasNext())
    {
        i.next();
        QTableWidgetItem *service = new QTableWidgetItem(i.key());
        QTableWidgetItem *id = new QTableWidgetItem(QString::number(i.value()));

        ui->tableWidgetServiceIds->setItem(row, 0, service);
        ui->tableWidgetServiceIds->setItem(row, 1, id);
        row++;
    }
}

void MainWindow::saveServiceIds()
{
    _settings->service_ids.clear();
    int rows = ui->tableWidgetServiceIds->rowCount();
    for(int i=0;i<rows;i++)
    {
        QTableWidgetItem *item1 = ui->tableWidgetServiceIds->item(i, 0);
        QTableWidgetItem *item2 = ui->tableWidgetServiceIds->item(i, 1);
        bool ok2 = false;
        if(item1->text().size() > 0 && item2->text().size() > 0)
        {
            item2->text().toInt(&ok2);
        }
        if(ok2)
        {
            _settings->service_ids.insert(item1->text(), item2->text().toInt());
        }
    }
}

void MainWindow::addServiceId()
{
    ui->tableWidgetServiceIds->setRowCount(ui->tableWidgetServiceIds->rowCount() + 1);
    QTableWidgetItem *service = new QTableWidgetItem(QString(""));
    QTableWidgetItem *id = new QTableWidgetItem(QString(""));

    ui->tableWidgetServiceIds->setItem(ui->tableWidgetServiceIds->rowCount() - 1, 0, service);
    ui->tableWidgetServiceIds->setItem(ui->tableWidgetServiceIds->rowCount() - 1, 1, id);
    ui->tableWidgetServiceIds->scrollToBottom();
}

void MainWindow::deleteServiceId()
{
    QList<QTableWidgetItem*> items = ui->tableWidgetServiceIds->selectedItems();
    QSet<int> rows;
    for(QTableWidgetItem* item : items)
    {
        int row = item->row();
        rows.insert(row);
    }
    for(int row : rows)
    {
        ui->tableWidgetServiceIds->removeRow(row);
    }
}

void MainWindow::setLogicalChannels(QVector<LogicalChannel *> *logical_channels)
{

    for(int i = 0, j =0;i < _settings->channel_number; i++,j+=2)
    {
        if(logical_channels->count() > 1)
        {
            QModelIndex index1 = _logical_channel_model->index(i, 0);
            QModelIndex index2 = _logical_channel_model->index(i, 1);
            QString usage1 = logical_channels->at(j)->getBusy() ? (logical_channels->at(j)->getLocalCall() ? "Local call" : "Network call") : "Free ";
            usage1 = logical_channels->at(j)->getDisabled() ? "Disabled " : usage1;
            _logical_channel_model->setData(index1, QString("%3  %1  -->  %2 \n %4 \n %5")
                                            .arg(_id_lookup->lookup(logical_channels->at(j)->getSource())).
                                            arg(logical_channels->at(j)->getDestination())
                                            .arg(usage1)
                                            .arg(logical_channels->at(j)->getText())
                                            .arg(logical_channels->at(j)->getGPSInfo()));
            QString color1 = (logical_channels->at(j)->getBusy() ? (logical_channels->at(j)->getLocalCall() ? "#004dFF" : "#004d99") : "#9cffab");
            color1 = (logical_channels->at(j)->getDisabled() ? "#FF7777" : color1);
            color1 = (logical_channels->at(j)->isControlChannel() ? "#BBBBBB" : color1);
            _logical_channel_model->setColor(index1, color1);
            int state1 = logical_channels->at(j)->getBusy() ?
                        ChannelState::ChannelBusy : ChannelState::ChannelFree;
            state1 = logical_channels->at(j)->isControlChannel() ? ChannelState::ChannelControl : state1;
            _logical_channel_model->setState(index1, state1, 0);
            _logical_channel_model->setChecked(index1, logical_channels->at(j)->getDisabled() ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);

            QString usage2 = logical_channels->at(j + 1)->getBusy() ? (logical_channels->at(j + 1)->getLocalCall() ? "Local call" : "Network call") : "Free ";
            usage2 = logical_channels->at(j + 1)->getDisabled() ? "Disabled " : usage2;
            _logical_channel_model->setData(index2, QString("%3  %1  -->  %2 \n%4 \n %5")
                                            .arg(_id_lookup->lookup(logical_channels->at(j + 1)->getSource()))
                                            .arg(logical_channels->at(j + 1)->getDestination())
                                            .arg(usage2)
                                            .arg(logical_channels->at(j + 1)->getText())
                                            .arg(logical_channels->at(j + 1)->getGPSInfo()));
            QString color2 = (logical_channels->at(j + 1)->getBusy() ? (logical_channels->at(j + 1)->getLocalCall() ? "#004dFF" : "#004d99") : "#9cffab");
            color2 = (logical_channels->at(j + 1)->getDisabled() ? "#FF7777" : color2);
            _logical_channel_model->setColor(index2, color2);
            int state2 = logical_channels->at(j + 1)->getBusy() ?
                        ChannelState::ChannelBusy : ChannelState::ChannelFree;
            state2 = logical_channels->at(j + 1)->isControlChannel() ? ChannelState::ChannelControl : state2;
            _logical_channel_model->setState(index2, state2, 0);
            _logical_channel_model->setChecked(index2, logical_channels->at(j+1)->getDisabled() ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
        }
    }
}

void MainWindow::channelStateChange(int row, int col, bool state)
{
    unsigned int channel_index = row * 2 + col;
    emit channelEnable(channel_index, state);
}

void MainWindow::updateRegisteredMSList(QList<unsigned int>* registered_ms)
{
    deleteRegisteredMSList();
    for(int i=0;i<registered_ms->size();i++)
    {
        QIcon icon = QIcon(":/res/preferences-desktop-user.png");
        QListWidgetItem *item = new QListWidgetItem(icon, QString("%1").arg(_id_lookup->lookup(registered_ms->at(i))));
        ui->registeredMSListWidget->addItem(item);
    }
    for(int i=0;i<registered_ms->size();i++)
    {
        QIcon icon = QIcon(":/res/preferences-desktop-user.png");
        ui->comboBoxRegisteredMS->addItem(icon, QString::number(registered_ms->at(i)));
    }
    registered_ms->clear();
    delete registered_ms;
}

void MainWindow::updateTalkgroupSubscriptionList(QSet<unsigned int> *subscribed_talkgroups)
{
    deleteSubscribedTalkgroupList();
    QSetIterator<unsigned int> it(*subscribed_talkgroups);
    while(it.hasNext())
    {
        QIcon icon = QIcon(":/res/system-users.png");
        QListWidgetItem *item = new QListWidgetItem(icon, QString("%1").arg(it.next()));
        ui->listWidgetSubscribedTalkgroups->addItem(item);
    }
    subscribed_talkgroups->clear();
    delete subscribed_talkgroups;
}

void MainWindow::updateRejectedCallsList(unsigned int srcId, unsigned int dstId, bool local_call)
{
    QIcon icon = QIcon(":/res/preferences-desktop-user.png");
    QDateTime datetime = QDateTime::currentDateTime();
    QListWidgetItem *item = new QListWidgetItem(icon, QString("%1: %2 --> %3 (%4)")
                                                .arg(datetime.toString(Qt::TextDate))
                                                .arg(_id_lookup->lookup(srcId))
                                                .arg(_id_lookup->lookup(dstId))
                                                .arg(local_call ? "local" : "remote"));
    ui->listWidgetRejectedCalls->addItem(item);
}

void MainWindow::deleteRegisteredMSList()
{
    for(int i=0;i < ui->registeredMSListWidget->count();i++)
    {
        QListWidgetItem *item = ui->registeredMSListWidget->item(i);
        delete item;
    }
    ui->registeredMSListWidget->clear();
    ui->comboBoxRegisteredMS->clear();
}

void MainWindow::deleteSubscribedTalkgroupList()
{
    for(int i=0;i < ui->listWidgetSubscribedTalkgroups->count();i++)
    {
        QListWidgetItem *item = ui->listWidgetSubscribedTalkgroups->item(i);
        delete item;
    }
    ui->listWidgetSubscribedTalkgroups->clear();
}

void MainWindow::updateCallLog(unsigned int srcId, unsigned int dstId, int rssi, float ber, bool private_call)
{
    QDateTime datetime = QDateTime::currentDateTime();
    if(private_call)
    {
        QIcon icon = QIcon(":/res/preferences-desktop-user.png");
        QTableWidgetItem *dateitem = new QTableWidgetItem(datetime.toString(Qt::TextDate));
        QTableWidgetItem *srcitem = new QTableWidgetItem(icon, QString("%1")
                                                    .arg(_id_lookup->lookup(srcId)));
        QTableWidgetItem *dstitem = new QTableWidgetItem(icon, QString("%1")
                                                    .arg(_id_lookup->lookup(dstId)));
        QTableWidgetItem *rssiitem = new QTableWidgetItem(QString("%1")
                                                    .arg(rssi));
        QTableWidgetItem *beritem = new QTableWidgetItem(QString("%1")
                                                    .arg(ber));
        uint32_t rows = ui->privateCallsTableWidget->rowCount();
        ui->privateCallsTableWidget->setRowCount(rows + 1);
        ui->privateCallsTableWidget->setItem(rows, 0, dateitem);
        ui->privateCallsTableWidget->setItem(rows, 1, srcitem);
        ui->privateCallsTableWidget->setItem(rows, 2, dstitem);
        ui->privateCallsTableWidget->setItem(rows, 3, rssiitem);
        ui->privateCallsTableWidget->setItem(rows, 4, beritem);
    }
    else
    {
        QIcon icon_group = QIcon(":/res/system-users.png");
        QIcon icon_user = QIcon(":/res/preferences-desktop-user.png");
        QTableWidgetItem *dateitem = new QTableWidgetItem(datetime.toString(Qt::TextDate));
        QTableWidgetItem *srcitem = new QTableWidgetItem(icon_user, QString("%1")
                                                    .arg(_id_lookup->lookup(srcId)));
        QTableWidgetItem *dstitem = new QTableWidgetItem(icon_group, QString("%1")
                                                    .arg(dstId));
        QTableWidgetItem *rssiitem = new QTableWidgetItem(QString("%1")
                                                    .arg(rssi));
        QTableWidgetItem *beritem = new QTableWidgetItem(QString("%1")
                                                    .arg(ber));
        uint32_t rows = ui->groupCallsTableWidget->rowCount();
        ui->groupCallsTableWidget->setRowCount(rows + 1);
        ui->groupCallsTableWidget->setItem(rows, 0, dateitem);
        ui->groupCallsTableWidget->setItem(rows, 1, srcitem);
        ui->groupCallsTableWidget->setItem(rows, 2, dstitem);
        ui->groupCallsTableWidget->setItem(rows, 3, rssiitem);
        ui->groupCallsTableWidget->setItem(rows, 4, beritem);
    }
}

void MainWindow::updateMessageLog(unsigned int srcId, unsigned int dstId, QString message, bool tg)
{
    QDateTime datetime = QDateTime::currentDateTime();
    if(!tg)
    {
        QIcon icon = QIcon(":/res/preferences-desktop-user.png");
        QTableWidgetItem *dateitem = new QTableWidgetItem(datetime.toString(Qt::TextDate));
        QTableWidgetItem *srcitem = new QTableWidgetItem(icon, QString("%1")
                                                    .arg(_id_lookup->lookup(srcId)));
        QTableWidgetItem *dstitem = new QTableWidgetItem(icon, QString("%1")
                                                    .arg(_id_lookup->lookup(dstId)));
        QTableWidgetItem *msg = new QTableWidgetItem(QString("%1")
                                                    .arg(message));
        uint32_t rows = ui->tableWidgetMessages->rowCount();
        ui->tableWidgetMessages->setRowCount(rows + 1);
        ui->tableWidgetMessages->setItem(rows, 0, dateitem);
        ui->tableWidgetMessages->setItem(rows, 1, srcitem);
        ui->tableWidgetMessages->setItem(rows, 2, dstitem);
        ui->tableWidgetMessages->setItem(rows, 3, msg);
    }
    else
    {
        QIcon icon_group = QIcon(":/res/system-users.png");
        QIcon icon_user = QIcon(":/res/preferences-desktop-user.png");
        QTableWidgetItem *dateitem = new QTableWidgetItem(datetime.toString(Qt::TextDate));
        QTableWidgetItem *srcitem = new QTableWidgetItem(icon_user, QString("%1")
                                                    .arg(_id_lookup->lookup(srcId)));
        QTableWidgetItem *dstitem = new QTableWidgetItem(icon_group, QString("%1")
                                                    .arg(dstId));
        QTableWidgetItem *msg = new QTableWidgetItem(QString("%1")
                                                    .arg(message));
        uint32_t rows = ui->tableWidgetMessages->rowCount();
        ui->tableWidgetMessages->setRowCount(rows + 1);
        ui->tableWidgetMessages->setItem(rows, 0, dateitem);
        ui->tableWidgetMessages->setItem(rows, 1, srcitem);
        ui->tableWidgetMessages->setItem(rows, 2, dstitem);
        ui->tableWidgetMessages->setItem(rows, 3, msg);
    }
}

void MainWindow::requestRegistration()
{
    deleteRegisteredMSList();
    emit registrationRequested();
}

void MainWindow::sendSystemMessage()
{
    emit sendShortMessage(ui->textEditSystemMessageOnce->toPlainText(), 0);
}

void MainWindow::sendMessageToRadio()
{
    unsigned int radio = ui->comboBoxRegisteredMS->currentText().toInt();
    emit sendShortMessage(ui->textEditSystemMessageOnce->toPlainText(), radio);
}

void MainWindow::addDGNA()
{
    unsigned int radio = ui->comboBoxRegisteredMS->currentText().toInt();
    emit sendDGNA(ui->textEditSystemMessageOnce->toPlainText(), radio);
}

void MainWindow::sendUDTPoll()
{
    unsigned int radio = ui->comboBoxRegisteredMS->currentText().toInt();
    emit pollData(radio);
}

void MainWindow::sendStatusPoll()
{
    unsigned int radio = ui->comboBoxRegisteredMS->currentText().toInt();
    emit pollStatus(radio);
}

void MainWindow::sendPing()
{
    QString radio = ui->comboBoxRegisteredMS->currentText();
    ui->labelPingInformation->setStyleSheet("background-color:#EEEEEE;color:#000000");
    emit pingRadio(radio.toInt(), false);
}

void MainWindow::pingTimeout()
{
    ui->labelPingInformation->setStyleSheet("background-color:#CC0000;color:#FFFFFF");
    ui->labelPingInformation->setText("Ping timeout");
}

void MainWindow::displayPingResponse(unsigned int srcId, unsigned int msec)
{
    ui->labelPingInformation->setStyleSheet("background-color:#009900;color:#FFFFFF");
    ui->labelPingInformation->setText(QString("Ping response from: %1, time: %2 ms").arg(srcId).arg(msec));
}

void MainWindow::authCheck()
{
    QString radio = ui->comboBoxRegisteredMS->currentText();
    ui->labelAuthSuccess->setStyleSheet("background-color:#FFFFFF;color:#000000");
    ui->labelAuthSuccess->setText("Checking");
    emit sendAuthCheck(radio.toInt());
}

void MainWindow::authSuccess(bool successful)
{
    if(successful)
    {
        ui->labelAuthSuccess->setStyleSheet("background-color:#009900;color:#FFFFFF");
        ui->labelAuthSuccess->setText("SUCCESS");
    }
    else
    {
        ui->labelAuthSuccess->setStyleSheet("background-color:#990000;color:#FFFFFF");
        ui->labelAuthSuccess->setText("FAILED");
    }
}

void MainWindow::sendLocalTimeBroadcast()
{
    emit broadcastLocalTime();
}

void MainWindow::sendFrequenciesBroadcast()
{
    emit broadcastFrequencies();
}
