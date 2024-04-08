/*
 *   Copyright (C) 2015,2016,2020,2021,2022 by Jonathan Naylor G4KLX
 *   Copyright (C) 2023 by Adrian Musceac YO8RZZ
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
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

#if !defined(DMRCSBK_H)
#define DMRCSBK_H

#include "DMRDefines.h"
#include <stdint.h>

enum CSBKO {
	CSBKO_NONE           = 0x00,
	CSBKO_UUVREQ         = 0x04,
	CSBKO_UUANSRSP       = 0x05,
	CSBKO_CTCSBK         = 0x07,
    CSBKO_RAND           = 0x1F,
    CSBKO_ACKD           = 0x20,
	CSBKO_RADIO_CHECK    = 0x24,
	CSBKO_NACKRSP        = 0x26,
	CSBKO_BSDWNACT       = 0x38,
	CSBKO_PRECCSBK       = 0x3D,
    CSBKO_MAINT          = 0x2A,
    CSBKO_TV_GRANT       = 0x31,
    CSBKO_PV_GRANT       = 0x30,
    CSBKO_BTV_GRANT      = 0x32,
    CSBKO_PD_GRANT       = 0x33,
    CSBKO_PD_GRANT_MI    = 0x37,
    CSBKO_TD_GRANT       = 0x34,
    CSBKO_TD_GRANT_MI    = 0x38,
    CSBKO_AHOY           = 0x1C,
    CSBKO_ACKU           = 0x21,
    CSBKO_P_CLEAR        = 0x2E,
    CSBKO_C_BCAST        = 0x28,
    CSBKO_PV_GRANT_DX    = 0x35,
    CSBKO_PD_GRANT_DX    = 0x36,
};


class CDMRCSBK
{
public:
	CDMRCSBK();
    CDMRCSBK(const CDMRCSBK& csbk);
	~CDMRCSBK();
    CDMRCSBK& operator=(const CDMRCSBK& data);

	bool put(const unsigned char* bytes);

	void get(unsigned char* bytes) const;

	// Generic fields
	CSBKO         getCSBKO() const;
    void          setCSBKO(unsigned char csbko, bool LB=true, bool PF=false);
	unsigned char getFID() const;

	// Set/Get the OVCM bit in the supported CSBKs
	bool getOVCM() const;
	void setOVCM(bool ovcm);

	// For BS Dwn Act
	unsigned int  getBSId() const;

	// For Pre
	bool getGI() const;

	unsigned int  getSrcId() const;
	unsigned int  getDstId() const;
    void          setSrcId(unsigned int srcId);
    void          setDstId(unsigned int dstId);
    unsigned int  getServiceKind() const;
    unsigned int  getServiceOptions() const;

	bool          getDataContent() const;
	unsigned char getCBF() const;

	void          setCBF(unsigned char cbf);
    void          setData1(unsigned char data1);
    unsigned char getData1();
    void          setLB(bool LB);
    void          setPF(bool PF);
    bool          getLB();
    bool          getPF();
    void          setFID(unsigned char FID);
    bool          getProxyFlag();
    unsigned int  getPriority();
    bool          getBroadcast();
    bool          getSuplimentaryData();
    void          setDataType(unsigned char dataType);
    unsigned char getDataType();

private:
	unsigned char* m_data;
	CSBKO          m_CSBKO;
	unsigned char  m_FID;
	bool           m_GI;
	unsigned int   m_bsId;
	unsigned int   m_srcId;
	unsigned int   m_dstId;
	bool           m_dataContent;
	unsigned char  m_CBF;
	bool           m_OVCM;
    unsigned int   m_service_kind;
    unsigned int   m_service_options;
    bool           m_LB;
    bool           m_PF;
    unsigned char  m_dataType;
};

#endif
