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

#include "settings.h"

Settings::Settings(Logger *logger)
{
    _logger = logger;
    _config_file = setupConfig();

    /// not saved to config


    /// saved to config
    window_width = 1024;
    window_height = 800;
    headless_mode = 0;
    control_port = 4939;
    mmdvm_listen_port = 44550;
    mmdvm_send_port = 44560;
    gateway_listen_port = 44660;
    gateway_send_port = 44670;
    channel_number = 1;
    gateway_number = 1;
    udp_local_address = "127.0.0.1";
    mmdvm_remote_address = "127.0.0.1";
    gateway_remote_address = "127.0.0.1";
    control_channel_physical_id = 0;
    control_channel_slot = 1;
    gateway_enabled = 1;
    announce_priority = 0;
    payload_channel_idle_timeout = 5;
    system_identity_code = 1;
    use_absolute_channel_grants = 0;
    announce_system_message = 1;
    prevent_mmdvm_overflows = 1;
    receive_tg_attach = 0;
    announce_system_freqs_interval = 120;
    announce_late_entry_interval = 1;
    service_ids = {{"help", 1}, {"signal_report", 2}, {"location", 1048677}};
}

Settings::~Settings()
{
    delete _config_file;
}

QFileInfo *Settings::setupConfig()
{
    QDir files = QDir::homePath();
    if(!QDir(files.absolutePath()+"/.config/dmrtc").exists())
    {
        QDir().mkdir(files.absolutePath()+"/.config/dmrtc");
    }
    QFileInfo old_file = files.filePath(".config/dmrtc.cfg");
    if(old_file.exists())
    {
        QDir().rename(old_file.filePath(), files.filePath(".config/dmrtc/dmrtc.cfg"));
    }
    QFileInfo new_file = files.filePath(".config/dmrtc/dmrtc.cfg");
    if(!new_file.exists())
    {
        QString config = "// Automatically generated\n";
        QFile newfile(new_file.absoluteFilePath());

        if (newfile.open(QIODevice::ReadWrite))
        {
            newfile.write(config.toStdString().c_str());
            newfile.close();
        }

    }

    return new QFileInfo(new_file);
}


void Settings::readConfig()
{
    libconfig::Config cfg;
    try
    {
        cfg.readFile(_config_file->absoluteFilePath().toStdString().c_str());
    }
    catch(const libconfig::FileIOException &fioex)
    {
        _logger->log(Logger::LogLevelFatal, "I/O error while reading configuration file.");
        exit(EXIT_FAILURE); // a bit radical
    }
    catch(const libconfig::ParseException &pex)
    {
        _logger->log(Logger::LogLevelFatal,
                  QString("Configuration parse error at %1: %2 - %3").arg(pex.getFile()).arg(
                         pex.getLine()).arg(pex.getError()));
        exit(EXIT_FAILURE); // a bit radical
    }

    /// Read values

    try
    {
        control_port = cfg.lookup("control_port");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        control_port = 4939;
    }
    try
    {
        mmdvm_listen_port = cfg.lookup("mmdvm_listen_port");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        mmdvm_listen_port = 44550;
    }
    try
    {
        mmdvm_send_port = cfg.lookup("mmdvm_send_port");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        mmdvm_send_port = 44560;
    }
    try
    {
        gateway_listen_port = cfg.lookup("gateway_listen_port");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        gateway_listen_port = 44660;
    }
    try
    {
        gateway_send_port = cfg.lookup("gateway_send_port");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        gateway_send_port = 44670;
    }
    try
    {
        headless_mode = cfg.lookup("headless_mode");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        window_width = 1400;
    }
    try
    {
        headless_mode = cfg.lookup("headless_mode");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        headless_mode = 0;
    }
    try
    {
        window_width = cfg.lookup("window_width");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        window_width = 1400;
    }
    try
    {
        window_height = cfg.lookup("window_height");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        window_height = 700;
    }
    try
    {
        channel_number = cfg.lookup("channel_number");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        channel_number = 4;
    }
    try
    {
        gateway_number = cfg.lookup("gateway_number");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        gateway_number = 1;
    }
    try
    {
        udp_local_address = QString(cfg.lookup("udp_local_address"));
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        udp_local_address = "127.0.0.1";
    }
    try
    {
        mmdvm_remote_address = QString(cfg.lookup("mmdvm_remote_address"));
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        mmdvm_remote_address = "127.0.0.1";
    }
    try
    {
        gateway_remote_address = QString(cfg.lookup("gateway_remote_address"));
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        gateway_remote_address = "127.0.0.1";
    }
    try
    {
        control_channel_physical_id = cfg.lookup("control_channel_physical_id");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        control_channel_physical_id = 0;
    }
    try
    {
        control_channel_slot = cfg.lookup("control_channel_slot");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        control_channel_slot = 1;
    }
    try
    {
        gateway_enabled = cfg.lookup("gateway_enabled");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        gateway_enabled = 1;
    }
    try
    {
        announce_priority = cfg.lookup("announce_priority");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        announce_priority = 0;
    }
    try
    {
        system_announcement_message = QString(cfg.lookup("system_announcement_message"));
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        system_announcement_message = "DMR tier III trunked radio site";
    }
    try
    {
        payload_channel_idle_timeout = cfg.lookup("payload_channel_idle_timeout");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        payload_channel_idle_timeout = 5;
    }
    try
    {
        system_identity_code = cfg.lookup("system_identity_code");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        system_identity_code = 1;
    }
    try
    {
        use_absolute_channel_grants = cfg.lookup("use_absolute_channel_grants");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        use_absolute_channel_grants = 0;
    }
    try
    {
        announce_system_message = cfg.lookup("announce_system_message");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        announce_system_message = 1;
    }
    try
    {
        prevent_mmdvm_overflows = cfg.lookup("prevent_mmdvm_overflows");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        prevent_mmdvm_overflows = 1;
    }
    try
    {
        receive_tg_attach = cfg.lookup("receive_tg_attach");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        receive_tg_attach = 0;
    }
    try
    {
        announce_system_freqs_interval = cfg.lookup("announce_system_freqs_interval");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        announce_system_freqs_interval = 120;
    }
    try
    {
        announce_late_entry_interval = cfg.lookup("announce_late_entry_interval");
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        announce_late_entry_interval = 1;
    }
    try
    {
        const libconfig::Setting &talkgroup_routing = cfg.lookup("talkgroup_routing");
        for(int i = 0; i < talkgroup_routing.getLength(); ++i)
        {
          const libconfig::Setting &talkgroup = talkgroup_routing[i];
          unsigned int tg_id, gateway_id;

          if(!(talkgroup.lookupValue("tg_id", tg_id)
               && talkgroup.lookupValue("gateway_id", gateway_id)))
            continue;
          talkgroup_routing_table.insert(tg_id, gateway_id);
        }
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
    }
    try
    {
        const libconfig::Setting &slot_map = cfg.lookup("slot_rewrite");
        for(int i = 0; i < slot_map.getLength(); ++i)
        {
          const libconfig::Setting &slot_rewrite = slot_map[i];
          unsigned int tg_id, slot_no;

          if(!(slot_rewrite.lookupValue("tg_id", tg_id)
               && slot_rewrite.lookupValue("slot_no", slot_no)))
            continue;
          slot_rewrite_table.insert(tg_id, slot_no);
        }
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
    }
    try
    {
        const libconfig::Setting &channel_map = cfg.lookup("logical_physical_channels");
        for(int i = 0; i < channel_map.getLength(); ++i)
        {
          const libconfig::Setting &channel = channel_map[i];
          long long logical_channel, tx_freq, rx_freq, colour_code;

          if(!(channel.lookupValue("logical_channel", logical_channel)
               && channel.lookupValue("tx_freq", tx_freq) &&
               channel.lookupValue("rx_freq", rx_freq) &&
               channel.lookupValue("colour_code", colour_code)))
            continue;
          QMap<QString, uint64_t> channel_map{{"logical_channel", (uint64_t)logical_channel},
                                              {"tx_freq", (uint64_t)tx_freq},
                                              {"rx_freq", (uint64_t)rx_freq},
                                              {"colour_code", (uint64_t)colour_code}};
          logical_physical_channels.append(channel_map);
        }
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
    }
    try
    {
        const libconfig::Setting &service_map = cfg.lookup("service_ids");
        for(int i = 0; i < service_map.getLength(); ++i)
        {
          const libconfig::Setting &service = service_map[i];
          std::string service_name;
          unsigned int id;

          if(!(service.lookupValue("service_name", service_name)
               && service.lookupValue("id", id)))
            continue;
          service_ids.insert(QString::fromStdString(service_name), id);
        }
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        service_ids = {{"help", 1}, {"signal_report", 2}, {"location", 1048677}};
    }
    try
    {
        const libconfig::Setting &call_prio = cfg.lookup("call_priorities");
        for(int i = 0; i < call_prio.getLength(); ++i)
        {
          const libconfig::Setting &talkgroup = call_prio[i];
          unsigned int id, priority;

          if(!(talkgroup.lookupValue("id", id)
               && talkgroup.lookupValue("priority", priority)))
            continue;
          call_priorities.insert(id, priority);
        }
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
        call_priorities = {{112, 3}, {226, 2}, {9, 1}};
    }
    try
    {
        const libconfig::Setting &call_div = cfg.lookup("call_diverts");
        for(int i = 0; i < call_div.getLength(); ++i)
        {
          const libconfig::Setting &div = call_div[i];
          unsigned int id, divert;

          if(!(div.lookupValue("id", id)
               && div.lookupValue("divert", divert)))
            continue;
          call_diverts.insert(id, divert);
        }
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
    }
    try
    {
        const libconfig::Setting &auth_k = cfg.lookup("auth_keys");
        for(int i = 0; i < auth_k.getLength(); ++i)
        {
          const libconfig::Setting &ak = auth_k[i];
          unsigned int id;
          std::string key;

          if(!(ak.lookupValue("id", id)
               && ak.lookupValue("key", key)))
            continue;
          auth_keys.insert(id, QString::fromStdString(key));
        }
    }
    catch(const libconfig::SettingNotFoundException &nfex)
    {
    }

}

void Settings::saveConfig()
{
    libconfig::Config cfg;
    libconfig::Setting &root = cfg.getRoot();
    root.add("control_port",libconfig::Setting::TypeInt) = control_port;
    root.add("mmdvm_listen_port",libconfig::Setting::TypeInt) = mmdvm_listen_port;
    root.add("mmdvm_send_port",libconfig::Setting::TypeInt) = mmdvm_send_port;
    root.add("gateway_listen_port",libconfig::Setting::TypeInt) = gateway_listen_port;
    root.add("gateway_send_port",libconfig::Setting::TypeInt) = gateway_send_port;
    root.add("window_width",libconfig::Setting::TypeInt) = window_width;
    root.add("window_height",libconfig::Setting::TypeInt) = window_height;
    root.add("headless_mode",libconfig::Setting::TypeInt) = headless_mode;
    root.add("channel_number",libconfig::Setting::TypeInt) = channel_number;
    root.add("gateway_number",libconfig::Setting::TypeInt) = gateway_number;
    root.add("udp_local_address",libconfig::Setting::TypeString) = udp_local_address.toStdString();
    root.add("mmdvm_remote_address",libconfig::Setting::TypeString) = mmdvm_remote_address.toStdString();
    root.add("gateway_remote_address",libconfig::Setting::TypeString) = gateway_remote_address.toStdString();
    root.add("control_channel_physical_id",libconfig::Setting::TypeInt) = control_channel_physical_id;
    root.add("control_channel_slot",libconfig::Setting::TypeInt) = control_channel_slot;
    root.add("gateway_enabled",libconfig::Setting::TypeInt) = gateway_enabled;
    root.add("announce_priority",libconfig::Setting::TypeInt) = announce_priority;
    root.add("system_announcement_message",libconfig::Setting::TypeString) = system_announcement_message.toStdString();
    root.add("payload_channel_idle_timeout",libconfig::Setting::TypeInt) = payload_channel_idle_timeout;
    root.add("system_identity_code",libconfig::Setting::TypeInt) = system_identity_code;
    root.add("use_absolute_channel_grants",libconfig::Setting::TypeInt) = use_absolute_channel_grants;
    root.add("announce_system_message",libconfig::Setting::TypeInt) = announce_system_message;
    root.add("prevent_mmdvm_overflows",libconfig::Setting::TypeInt) = prevent_mmdvm_overflows;
    root.add("receive_tg_attach",libconfig::Setting::TypeInt) = receive_tg_attach;
    root.add("announce_system_freqs_interval",libconfig::Setting::TypeInt) = announce_system_freqs_interval;
    root.add("announce_late_entry_interval",libconfig::Setting::TypeInt) = announce_late_entry_interval;
    /// Talkgroup routing
    root.add("talkgroup_routing",libconfig::Setting::TypeList);
    libconfig::Setting &talkgroup_routing = root["talkgroup_routing"];
    QMapIterator<unsigned int, unsigned int> i(talkgroup_routing_table);
    while(i.hasNext())
    {
        i.next();
        libconfig::Setting &talkgroup = talkgroup_routing.add(libconfig::Setting::TypeGroup);
        talkgroup.add("tg_id", libconfig::Setting::TypeInt) = (int)i.key();
        talkgroup.add("gateway_id", libconfig::Setting::TypeInt) = (int)i.value();
    }
    /// SLot rewrites
    root.add("slot_rewrite",libconfig::Setting::TypeList);
    libconfig::Setting &slot_rewrite = root["slot_rewrite"];
    QMapIterator<unsigned int, unsigned int> it_slot(slot_rewrite_table);
    while(it_slot.hasNext())
    {
        it_slot.next();
        libconfig::Setting &talkgroup = slot_rewrite.add(libconfig::Setting::TypeGroup);
        talkgroup.add("tg_id", libconfig::Setting::TypeInt) = (int)it_slot.key();
        talkgroup.add("slot_no", libconfig::Setting::TypeInt) = (int)it_slot.value();
    }
    /// Logical physical channels
    root.add("logical_physical_channels",libconfig::Setting::TypeList);
    libconfig::Setting &lpc = root["logical_physical_channels"];
    QListIterator<QMap<QString, uint64_t>> it_lpc(logical_physical_channels);
    while(it_lpc.hasNext())
    {
        QMap<QString, uint64_t> channel_map = it_lpc.next();
        libconfig::Setting &channel = lpc.add(libconfig::Setting::TypeGroup);
        channel.add("logical_channel", libconfig::Setting::TypeInt64) = (int64_t)channel_map.value("logical_channel");
        channel.add("tx_freq", libconfig::Setting::TypeInt64) = (int64_t)channel_map.value("tx_freq");
        channel.add("rx_freq", libconfig::Setting::TypeInt64) = (int64_t)channel_map.value("rx_freq");
        channel.add("colour_code", libconfig::Setting::TypeInt64) = (int64_t)channel_map.value("colour_code");
    }

    /// Service ids
    root.add("service_ids",libconfig::Setting::TypeList);
    libconfig::Setting &service_ids_config = root["service_ids"];
    QMapIterator<QString, unsigned int> it_services(service_ids);
    while(it_services.hasNext())
    {
        it_services.next();
        libconfig::Setting &service = service_ids_config.add(libconfig::Setting::TypeGroup);
        service.add("service_name", libconfig::Setting::TypeString) = it_services.key().toStdString();
        service.add("id", libconfig::Setting::TypeInt) = (int)it_services.value();
    }
    /// Call priorities
    root.add("call_priorities",libconfig::Setting::TypeList);
    libconfig::Setting &call_prio = root["call_priorities"];
    QMapIterator<unsigned int, unsigned int> it_prio(call_priorities);
    while(it_prio.hasNext())
    {
        it_prio.next();
        libconfig::Setting &id = call_prio.add(libconfig::Setting::TypeGroup);
        id.add("id", libconfig::Setting::TypeInt) = (int)it_prio.key();
        id.add("priority", libconfig::Setting::TypeInt) = (int)it_prio.value();
    }

    /// Call diverts
    root.add("call_diverts",libconfig::Setting::TypeList);
    libconfig::Setting &call_div = root["call_diverts"];
    QMapIterator<unsigned int, unsigned int> it_div(call_diverts);
    while(it_div.hasNext())
    {
        it_div.next();
        libconfig::Setting &id = call_div.add(libconfig::Setting::TypeGroup);
        id.add("id", libconfig::Setting::TypeInt) = (int)it_div.key();
        id.add("divert", libconfig::Setting::TypeInt) = (int)it_div.value();
    }

    /// Auth keys
    root.add("auth_keys",libconfig::Setting::TypeList);
    libconfig::Setting &auth_k = root["auth_keys"];
    QMapIterator<unsigned int, QString> it_k(auth_keys);
    while(it_k.hasNext())
    {
        it_k.next();
        libconfig::Setting &id = auth_k.add(libconfig::Setting::TypeGroup);
        id.add("id", libconfig::Setting::TypeInt) = (int)it_k.key();
        id.add("key", libconfig::Setting::TypeString) = it_k.value().toStdString();
    }

    /// Write to file
    try
    {
        cfg.writeFile(_config_file->absoluteFilePath().toStdString().c_str());
    }
    catch(const libconfig::FileIOException &fioex)
    {
        _logger->log(Logger::LogLevelFatal, "I/O error while writing configuration file: " +
                     _config_file->absoluteFilePath());
        exit(EXIT_FAILURE);
    }
}

