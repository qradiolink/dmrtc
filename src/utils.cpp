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

#include "utils.h"
#include "math.h"

Utils::Utils()
{

}


unsigned int Utils::convertCAIToP3GroupNumber(unsigned int gid)
{
    (void)gid;
    return 0; // Not implemented
}

unsigned int Utils::convertP3GroupNumberToCAI(unsigned int group_number)
{
    unsigned int NP = group_number / 100000;
    unsigned int FGN = (group_number - NP * 100000) / 10000;
    unsigned int GN = (group_number - NP * 100000) - (FGN * 1000);
    unsigned int CAI = (NP - 328) * 0x8000 + (FGN - 20) * 100 + (GN - 900) + 1048577;
    return CAI;
}

unsigned int Utils::convertBase11GroupNumberToBase10(unsigned int group_number)
{
    if(group_number < 1)
        return 0;
    unsigned int base_11 = Utils::base11(group_number);
    if(base_11 < 99999)
        return base_11;
    unsigned int gid = base_11;
    unsigned int digit[7];
    for(int i=0; i<7 ; i++)
    {
        digit[i] = gid % 10;
        gid = gid / 10;
    }
    unsigned int big_three = (digit[6] * 121U + digit[5] * 11U + digit[4]) * 10000;
    unsigned int small_four =  digit[3] * 1000 + digit[2] * 100 + digit[1] * 10  + digit[0];
    return big_three + small_four;
}

unsigned int Utils::base11(unsigned int value)
{
    if(value < 1)
        return 0;
    return (value % 11) + 10 * base11(value / 11);
}

unsigned int Utils::convertBase10ToBase11GroupNumber(unsigned int gid)
{
    if((gid > 9999999) || (gid < 1))
        return 0;
    unsigned int digit[7];
    for(int i=0; i<7 ; i++)
    {
        digit[i] = gid % 10;
        gid = gid / 10;
    }
    unsigned int group_number = digit[0] + digit[1] * 11U + digit[2] * 121U + digit[3] * 1331U + digit[4] * 14641U + digit[5] * 146410U + digit[6] * 1464100U;
    return group_number;
}
