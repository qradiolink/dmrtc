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


#ifndef TRUNKING_UTILS_H
#define TRUNKING_UTILS_H

#include <QSysInfo>
#include <QString>
#include <QList>
#include <QByteArray>
#include <string.h>

class TrunkingUtils
{
public:
    TrunkingUtils();
    static void parseUTF16(QString& text_message, unsigned int size, unsigned char* msg);
    static void parseISO7bitToISO8bit(unsigned char* msg, unsigned char* converted, unsigned int bit7_size, unsigned int size);
    static unsigned int convertP3GroupNumberToCAI(unsigned int group_number);
    static unsigned int convertCAIToP3GroupNumber(unsigned int gid);
    static unsigned int convertBase11GroupNumberToBase10(unsigned int group_number);
    static unsigned int convertBase10ToBase11GroupNumber(unsigned int gid);
    static unsigned int base11(unsigned int value);
    static QList<QString> readNMEA(unsigned char* msg, unsigned int dsize);
    static unsigned int parseBCDDigits(unsigned char* message_payload, unsigned int message_size, unsigned int pad_nibble);
};

#endif // TRUNKING_UTILS_H
