
DMRTC Operation Manual
====

## Settings


### General settings

#### Connectivity

- MMDVM send base port: ports on which MMDVMHost instances are listening are sequential and start with this number. Each RF channel is represented by one MMDVMHost instance. Default value: 44560.
MMDVMHost instances will listen on ports 44560, 44561, 44562, 44563 etc.
- MMDVM listen base port: same as above, but these are the pair ports on which DMRTC listens to traffic from MMDVMHosts. Default value: 44550. Incremented for all subsequent MMDVMHost instances: 44550, 44551, 44552, 44553 etc.
- DMRGateway listen base port: The ports on which DMRTC listens to traffic from DMRGateway. Default value 44660. Incremented for each subsequent DMRGateway instance
- DMRGateway send base port: The port numbers on which DMRGateway listens for traffic from DMRTC. Default value 44670. Incremented for each subsequent DMRGateway instance.
- Local IP address: the IP address of the machine on which DMRTC runs. If DMRTC runs on the same machine as MMDVMHost and DMRGateway, this IP can be 127.0.0.1
- MMDVMHost IP address: the IP address of the machine on which MMDVMHost instances are running. If on the same machine set to 127.0.0.1
- DMRGateway IP address: the IP address of the machine on which DMRGateway instances are running. If on the same machine set to 127.0.0.1

#### Other general settings

- Number of RF channels: how many MMDVMHost instances and therefore DMR RF channels this system is using. Default value: 4, maximum value: 7
- Number of DMRGateways: how many DMRGateway instances this site is using. Using multiple DMRGateway instances is necessary because of the way DMR reflectors work. For each network pair in DMRGateway, only two timeslots are available. To be able to receive all talkgroup traffic, each timeslot needs to be mapped only to a single static talkgroup. You will also need to set DMR rewrite rules in DMRGateway to ensure all outgoing traffic for a talkgroup is mapped to the correct timeslot.
- Control channel physical id: which MMDVMHost instance is running the control channel. Shoould generally be the first MMDVMHost instance. Default value: 0. Changing this is not recommended.
- Control channel timeslots: default value 1. If set to a value of 2, a TSCCAS will be used. 
- Payload channel idle timeout: how fast after a call has ended the payload channel is deallocated. Default value: 5 seconds
- Announce frequencies interval: how often should the control channel brodcast site frequencies. Default value: 120 seconds
- Announce late entry interval: how often should group voice late entry announcements be made on the control channel. Default value: 1 second.
- Announce adjacent BS interval: how often should the adjacent sites be advertised on the control channel. Default value: 30 seconds
- System identity code: the 14 bit value for system identity code which contains network type, network id, site id. Default value: 9 (Tiny, Network Id 1, Site Id 1). Refer to ETSI TS 102 361-4 section 6.3.2.2.1.1 Structure of the System Identity Code (C_SYScode) for further details.
- Base frequency: value in Hz for the base frequency of the fixed channel plan.
- Frequency separation: value in Hz for the separation between channels in the fixed channel plan. Default value: 25000
- Frequency duplex split: value in Hz for the offset between RX and TX of the base station, should be positive.

#### Optional values
- DMRGateway enabled: whether the site should be connected to the Internet using DMRGateway. Default value is on.
- Announce priority calls: whether to broadcast preferential call notifications on the payload channel for radios already engaged in a call. Default value is off. Toggling this on can cause short voice interruptions in a call.
- Announce system message: whether a text information will be sent to radios upon registration and every 30 minutes. Default is off.
- System announcement message: the text to be transmitted via SMS for the above option.
- Use absolute channel grants: if selected, voice channel grants will broadcast frequency and color code instead of logical channel number. Will not work if MS is set to hunt in fixed channel plan. Default: off
- Prevent MMDVM overflows: DMR voice data coming form the Internet will be buffered and sent at one timeslot intervals, preventing MMDVMHost overflow messages. Default value: on
- Use radio talkgroup attachments: if MS registration is enabled, the list of talkgroups to attach to will be sent by the MS and DMRTC will store it internally, enabling other actions. Default value: on
- Require registration: whether service should only be available once a MS has registered with the control channel. Default value: on. Disabling this option will cause several features like private calls and private messages to stop working.
- Use fixed channel plan: if the MS does not have the logical channel frequencies pre-programmed and is set to use the fixed plan hunt setting, the values for Base frequency, Frequency duplex split and Frequency separation will be used instead. Requires the MS to have the same values for above settings pre-programmed.
- Transmit subscribed talkgroups only: experimental, selecting this will maximize system call capacity by only transmitting TG traffic over RF for talkgroups which are requested by at least one MS registered on the system.

### Talkgroup gateway routing

If the DMR tier III site should be connected to the amateur radio networks on the Internet, at least one DMRGateway instance must be used. The number of DMRGateway instances is not limited.
DMR gateways used by the system will have at least 5 networks per gateway configured, and each network can support two talkgroups on timeslot 1 and timeslot 2. Due to limitations in DMR reflectors, which cause incoming traffic to be blocked if more than one talkgroup is configured per timeslot and at least one talkgroup is active, it is necessary to configure only one static talkgroup per timeslot. Since DMRTC can transmit a maximum of 13 talkgroups at the same time, you may want to allocate some static talkgroups to one DMRGateway instance, some others on a second DMRGateway instance and so on. All DMR logical networks configured in DMRGateway.ini can be the same DMR network (e.g. Brandmeister) which will give the appearance of having multiple repeaters connected.

On this tab, you can configure **outgoing** routing to the proper DMRGateway instance for each talkgroups. The numbering of the DMRGateway instance starts at zero, with the first instance having an id of 0, the next one an id of 1 and so on.

### Slot rewrites

On a DMR tier III site, the timeslot of the RF transmission is irrelevant and unknown to the user, since DMRTC will dynamically allocate an RF channel and a timeslot. However, on the internet network side, the timeslot is important for static talkgroup allocations (the only type of allocation which should be used with DMRTC). On this tab you can set the timeslot to be used for a certain talkgroup. However, this setting has been deprecated, and you should use DMRGateway.ini to set all timeslot rewrites instead of this tab.

### Call priorities

This tab allows to set talkgroup call priorities. In case the system is at full capacity and no more RF channels / timeslots are available, DMRTC will prioritize traffic for talkgroups with the highest priorities and will tear down traffic for lower priority talkgroups. Priorities range from 0 to 3, with 3 being highest priority. By default, if not listed on this tab, a talkgroup will have a priority of 0, the lowest.

### Logical physical channel map

This tab has mandatory settings (channel plan).

On this tab, you should create a list of used physical channels and their frequencies. This will be used in channel grants and information broadcasts of the system. List here all channels.
- Channel id starts from 1 and should be incremented for each channel.
- Logical channel starts from 1 and should be incremented for each channel. If the system is configured to use fixed channel plan, this value is not used and frequencies are used to compute the channel id instead.
- RX and TX frequencies should be added in Hz for each channel.
- Colour code is generally the same for all channels. Some radios will disregard the color code written here and use the color code of the control channel.

### Adjacent sites

A list of the control channel parameters and frequencies for neighbouring DMR tier III sites. These values will be used in information announcements and for roaming / handover from one site to another.

- System id: system identity code for the neighbouring station
- Logical channel: if the MS has pre-programmed frequencies in flexible channel plan, this is the channel number of the adjacent cell
- RX and TX frequency: if the MS and DMRTC is using the fixed channel plan, the logical channel will be computed from these values instead.
- Colour code: the colour code of the adjacent site. Will normally be different from current system colour code.

### Service ids

Special DMR ids used for requesting DMR services from DMRTC. The first column (service) should not be modified by the administrator.
- Service: is the specific service being requested. Should not be modified. Available services: DGNA, Help, Location, RSSI report.
- System id: DMR id used for the service. This value can be modified by the administrator and will be used by the radio user to request the service.

