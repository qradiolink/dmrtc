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

#ifndef SETTINGS_H
#define SETTINGS_H

#include <QObject>
#include <QFile>
#include <QFileDialog>
#include <QDir>
#include <QDebug>
#include <QMap>
#include <QMapIterator>
#include <QListIterator>
#include <QList>
#include <QFileInfo>
#include <libconfig.h++>
#include "logger.h"

class Settings
{
public:
    explicit Settings(Logger *logger);
    ~Settings();
    QFileInfo *setupConfig();
    void readConfig();
    void saveConfig();

    /// Saved to config file
    int control_port; // FIXME: this should be unsigned uint16
    int mmdvm_listen_port;
    int mmdvm_send_port;
    int gateway_listen_port;
    int gateway_send_port;
    int window_height;
    int window_width;
    int headless_mode;
    int channel_number;
    int gateway_number;
    int control_channel_physical_id;
    int control_channel_slot;
    int payload_channel_idle_timeout;
    int system_identity_code;
    int freq_base;
    int freq_separation;
    int freq_duplexsplit;
    QString udp_local_address;
    QString mmdvm_remote_address;
    QString gateway_remote_address;
    QString system_announcement_message;

    QMap<unsigned int, unsigned int> talkgroup_routing_table;
    QMap<unsigned int, unsigned int> slot_rewrite_table;
    QList<QMap<QString, uint64_t>> logical_physical_channels;
    QList<QMap<QString, uint64_t>> adjacent_sites;
    QMap<QString, unsigned int> service_ids;
    QMap<unsigned int, unsigned int> call_priorities;
    QMap<unsigned int, unsigned int> call_diverts;
    QMap<unsigned int, QString> auth_keys; // Auth keys are 32 character hex strings, traducing to 16 byte key

    int use_absolute_channel_grants;
    int use_fixed_channel_plan;
    int gateway_enabled;
    int announce_priority;
    int announce_system_message;
    int prevent_mmdvm_overflows;
    int receive_tg_attach;
    int registration_required;
    int transmit_subscribed_tg_only;
    int location_service_id;
    int announce_system_freqs_interval;
    int announce_adjacent_bs_interval;
    int announce_late_entry_interval;
    int channel_disable_bitmask;


private:
    QFileInfo *_config_file;
    Logger *_logger;
};

#endif // SETTINGS_H
