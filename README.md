
DMR tier III trunking controller GUI for MMDVM
====

This program is a proof-of-concept implementation of a DMR tier III trunked radio controller GUI. Only a subset of ETSI TS 102 361-4 is implemented currently, see list below.
This software is licenced under the GPL v3 and is primarily intended for amateur radio and educational use.
This software is intended to work together with DMRGateway, MMDVMHost-SDR, MMDVM-SDR and QRadioLink in order to create a DMR tier III radio site.


[![Image](https://qradiolink.org/images/dmrtc1.png)](https://qradiolink.org/images/dmrtc1.png?v=1)


DMR tier III services partly or completely implemented
----

1. Registration service
2. Talkgroup voice call service
3. Private voice call service: OACSU (to and from network subscribers), FOACSU (local subscribers only)
4. Broadcast talkgroup call service (local subscribers only)
5. UDT short data message service: private (local subscribers only), talkgroup (local and network)
6. Voice call late entry announcements
7. System frequencies and local time broadcasts
8. Network voice services
9. Talkgroup attachment on registration
10. Dynamic Group Numbering Assignment service
11. Radio presence check
12. Private voice / UDT short data call diversion service
13. MS authentication
14. Preferential call notification
15. Group call priorities, priority interrupt of network group calls
16. Adjacent sites announcements
17. Fixed and flexible hunt channel plans
18. Absolute channel grants (only when MS is set to hunt in flexible channel plan)
19. Status delivery service
20. Status polling service
21. NMEA location polling via UDT
22. Individual packet data call service (single item, confirmed data, long SMS, 8 bit ASCII text unit-to-unit, unit-to-group)


DMR tier III services NOT implemented
----

1. MS dynamic power control
2. MS pre-emption control
3. All-MS call service
4. Packet data call service (for unconfirmed data, raw data, defined data, group call, multi-item)
5. IP bearer services
6. Ambient listening service
7. Suplimentary user data transfer service (additional data sent as part of the primary call setup)
8. MS stun, kill and revive
9. Full duplex voice call service
10. PABX/PSTN call service
11. Call diversion service to PSTN/PABX/Gateway


Radio compatibility matrix
====

tier III compatible radios tested and confirmed to work with this software:

1. Hytera HP785: registration, talkgroup attachment, group voice calls, private voice calls (OACSU and FOACSU), short text messages, call divert, voice with location, DGNA, status transfer service, authentication, hunting in fixed or flexible channel plans, NMEA location polling via UDT
2. Hytera PD755: registration, talkgroup attachment, group voice calls, private voice calls (OACSU only, radio does not support FOACSU), short text messages, the rest of features are not 100% confirmed to work yet
3. Hytera PD785: registration, talkgroup attachment, group voice calls, private voice calls (OACSU and FOACSU), short text messages, call divert, voice with location, DGNA, status transfer service, authentication, hunting in fixed or flexible channel plans, NMEA location polling via UDT
4. Hytera PD685G: registration, talkgroup attachment, group voice calls, private voice calls (OACSU and FOACSU), short text messages, call divert, voice with location, DGNA, status transfer service, authentication, hunting in fixed or flexible channel plans, NMEA location polling via UDT
5. Motorola SL4000e: only fixed channel plan hunt mode, Open Radio or Open System mode in RadioManager CapMax system settings, otherwise no registration possible, talkgroup and private voice calls work, short text messages partly work, everything else does not at this moment, regardless of settings. Also, one timing parameter needs to be adjusted in RadioManager, otherwise in about 30 minutes radio deregisters from dmrtc.
6. Motorola DP4801e: same as Motorola SL4000e


Known issues
====

1. Private calls and private messages do not work when MS registration is disabled
2. Site to site handover in RX and TX does not work adequatly
3. MS power control and transmit interrupt do not work
4. RSSI indicated values are not always accurate and may have large errors at times
5. UDT short data polling other than NMEA location does not work
6. Status polling from TS does not work
7. Talkgroup call priorities are not respected for local calls, only for incoming network calls
8. Absolute channel grants do not work when the MS is set to hunt with fixed channel plan
9. TSCCAS support is incomplete / buggy


Requirements
====

- Qt 5.14 with GUI, network and widgets support.
- libconfig
- liblog4cpp
- Installing build dependencies on Debian 11 Bullseye: 

<pre>
$ sudo apt-get install liblog4cpp5v5 libconfig++9v5 libconfig++-dev qt5-qmake qtbase5-dev libqt5core5a libqt5gui5 libqt5network5
</pre>


Building the software
-----

- Clone the Github repository into a directory of your choice
- Change directory to where you have cloned or unzipped the code
- Run qmake to generate the Makefile
- Run make (with the optional -j flag)

<pre>
$ git clone https://codeberg.org/qradiolink/dmrtc
$ cd dmrtc/
$ git checkout master
$ mkdir -p build
$ cd build/
$ qmake ..
$ make
</pre>


Running the controller
----

- To run it with the GUI interface, simply execute drmtc.

<pre>
$ ./dmrtc
</pre>

- To run it in console, without the GUI interface, start it with the "-h" flag:

<pre>
$ ./dmrtc -h
</pre>

- The configuration file is located at $HOME/.config/dmrtc/dmrtc.cfg (see dmrtc.cfg.example)
- The list of DMR ids must be created at $HOME/.config/dmrtc/DMRIds.dat (CSV file with id, callsign and name, format by radioid.net )
- The log file is written at $HOME/.config/dmrtc/dmrtc.log 


Documentation
----

A complete HTML version of the MMDVM DMR trunking documentation can be found [here](https://qradiolink.org/docs/MMDVM/trunking.html)


Copyright and License
-----
- The program is released under the GNU General Public License version 3. Please see the COPYRIGHT and AUTHORS files for details.
- This program uses code (with modifications) from MMDVMHost copyright by Jonathan Naylor G4KLX and others, licensed under GPLv2, see the MMDVM directory and source files for details and license
- Graphical resources are licensed under LGPLv3 (icons from KDE Oxygen theme copyright KDE Foundation)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

