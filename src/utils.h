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

#ifndef UTILS_H
#define UTILS_H


class Utils
{
public:
    Utils();
    static unsigned int convertP3GroupNumberToCAI(unsigned int group_number);
    static unsigned int convertCAIToP3GroupNumber(unsigned int gid);
    static unsigned int convertBase11GroupNumberToBase10(unsigned int group_number);
    static unsigned int convertBase10ToBase11GroupNumber(unsigned int gid);
    static unsigned int base11(unsigned int value);
};

#endif // UTILS_H
