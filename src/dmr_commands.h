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

#ifndef DMR_COMMANDS_H
#define DMR_COMMANDS_H

namespace DMRCommand
{
    enum DMRCommand {
        ChannelEnableDisable = 1,
        RCCeaseTransmission = 2,
        RCRequestCeaseTransmission = 3,
        RCPowerIncreaseOneStep = 4,
        RCPowerDecreaseOneStep = 5,
        RCMaximumPower = 6,
        RCMinimumPower = 7,
        RCNoCommand = 0,
    };
}

#endif // DMR_COMMANDS_H
