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

#ifndef RC4_H
#define RC4_H
void swap(unsigned char* a, unsigned char* b);
void KSA(unsigned char* key, unsigned char* S, unsigned int key_length);
void PRGA(unsigned char* S, unsigned char* in, unsigned char* keystream, int text_length);
void arc4_get_challenge_response(unsigned char* key, unsigned int key_length, unsigned int& challenge, unsigned int& response);
#endif // RC4_H
