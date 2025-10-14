/*
 *	Copyright (C) 2015,2016,2017 Jonathan Naylor, G4KLX
 *  Copyright (C) 2023 by Adrian Musceac, YO8RZZ
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; version 2 of the License.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 */

#include "DMRData.h"
#include "DMRDefines.h"
#include "Utils.h"
#include "Log.h"

#include <cstdio>
#include <cstring>
#include <cassert>


CDMRData::CDMRData(const CDMRData& data) :
m_slotNo(data.m_slotNo),
m_data(NULL),
m_srcId(data.m_srcId),
m_dstId(data.m_dstId),
m_flco(data.m_flco),
m_dataType(data.m_dataType),
m_seqNo(data.m_seqNo),
m_n(data.m_n),
m_ber(data.m_ber),
m_rssi(data.m_rssi),
m_streamId(data.m_streamId),
m_dummy(data.m_dummy),
m_control(data.m_control),
m_chanEnable(data.m_chanEnable),
m_command(data.m_command),
m_messageSize(data.m_messageSize),
m_messageFlag(data.m_messageFlag)
{
	m_data = new unsigned char[2U * DMR_FRAME_LENGTH_BYTES];
	::memcpy(m_data, data.m_data, 2U * DMR_FRAME_LENGTH_BYTES);
    ::memcpy(m_uuid, data.m_uuid, 16U);
    m_message = new unsigned char[1024U];
    ::memcpy(m_message, data.m_message, 1024U);
}

CDMRData::CDMRData() :
m_slotNo(1U),
m_data(NULL),
m_srcId(0U),
m_dstId(0U),
m_flco(FLCO_GROUP),
m_dataType(0U),
m_seqNo(0U),
m_n(0U),
m_ber(0U),
m_rssi(0U),
m_streamId(0U),
m_dummy(false),
m_control(false),
m_chanEnable(true),
m_command(0),
m_messageSize(0U),
m_messageFlag(false)
{
	m_data = new unsigned char[2U * DMR_FRAME_LENGTH_BYTES];
    memset(m_uuid, 0, 16U);
    m_message = new unsigned char[1024U];
}

CDMRData::~CDMRData()
{
	delete[] m_data;
    delete[] m_message;
}

CDMRData& CDMRData::operator=(const CDMRData& data)
{
	if (this != &data) {
		::memcpy(m_data, data.m_data, DMR_FRAME_LENGTH_BYTES);
        ::memcpy(m_uuid, data.m_uuid, 16U);
        ::memcpy(m_message, data.m_message, 1024U);

		m_slotNo   = data.m_slotNo;
		m_srcId    = data.m_srcId;
		m_dstId    = data.m_dstId;
		m_flco     = data.m_flco;
		m_dataType = data.m_dataType;
		m_seqNo    = data.m_seqNo;
		m_n        = data.m_n;
		m_ber      = data.m_ber;
		m_rssi     = data.m_rssi;
        m_streamId = data.m_streamId;
        m_dummy    = data.m_dummy;
        m_control  = data.m_control;
        m_chanEnable  = data.m_chanEnable;
        m_command  = data.m_command;
        m_messageFlag = data.m_messageFlag;
        m_messageSize = data.m_messageSize;
	}

	return *this;
}

unsigned int CDMRData::getSlotNo() const
{
	return m_slotNo;
}

void CDMRData::setSlotNo(unsigned int slotNo)
{
	assert(slotNo == 1U || slotNo == 2U);

	m_slotNo = slotNo;
}

unsigned char CDMRData::getDataType() const
{
	return m_dataType;
}

void CDMRData::setDataType(unsigned char dataType)
{
	m_dataType = dataType;
}

unsigned int CDMRData::getSrcId() const
{
	return m_srcId;
}

void CDMRData::setSrcId(unsigned int id)
{
	m_srcId = id;
}

unsigned int CDMRData::getDstId() const
{
	return m_dstId;
}

void CDMRData::setDstId(unsigned int id)
{
	m_dstId = id;
}

FLCO CDMRData::getFLCO() const
{
	return m_flco;
}

void CDMRData::setFLCO(FLCO flco)
{
	m_flco = flco;
}

unsigned char CDMRData::getSeqNo() const
{
	return m_seqNo;
}

void CDMRData::setSeqNo(unsigned char seqNo)
{
	m_seqNo = seqNo;
}

unsigned char CDMRData::getN() const
{
	return m_n;
}

void CDMRData::setN(unsigned char n)
{
	m_n = n;
}

unsigned char CDMRData::getBER() const
{
	return m_ber;
}

void CDMRData::setBER(unsigned char ber)
{
	m_ber = ber;
}

unsigned char CDMRData::getRSSI() const
{
	return m_rssi;
}

void CDMRData::setRSSI(unsigned char rssi)
{
	m_rssi = rssi;
}

unsigned int CDMRData::getData(unsigned char* buffer) const
{
	assert(buffer != NULL);

	::memcpy(buffer, m_data, DMR_FRAME_LENGTH_BYTES);

	return DMR_FRAME_LENGTH_BYTES;
}

void CDMRData::setData(const unsigned char* buffer)
{
	assert(buffer != NULL);

	::memcpy(m_data, buffer, DMR_FRAME_LENGTH_BYTES);
}

void CDMRData::setStreamId(unsigned int streamId)
{
    m_streamId = streamId;
}

unsigned int CDMRData::getStreamId() const
{
    return m_streamId;
}

void CDMRData::setDummy(bool dummy)
{
    m_dummy = dummy;
}

bool CDMRData::getDummy() const
{
    return m_dummy;
}

void CDMRData::setControl(bool control)
{
    m_control = control;
}

bool CDMRData::getControl() const
{
    return m_control;
}

void CDMRData::setChannelEnable(bool chanEnable)
{
    m_chanEnable = chanEnable;
}

bool CDMRData::getChannelEnable() const
{
    return m_chanEnable;
}

void CDMRData::setCommand(unsigned int command)
{
    m_command = command;
}

unsigned int CDMRData::getCommand() const
{
    return m_command;
}

void CDMRData::setUUID(unsigned char *uuid)
{
    ::memcpy(m_uuid, uuid, 16U);
}

void CDMRData::getUUID(unsigned char *uuid)
{
    ::memcpy(uuid, m_uuid, 16U);
}

void CDMRData::setMessageSize(unsigned int size)
{
    m_messageSize = size;
}

unsigned int CDMRData::getMessageSize() const
{
    return m_messageSize;
}

void CDMRData::setMessageFlag(bool is_message)
{
    m_messageFlag = is_message;
}

bool CDMRData::getMessageFlag() const
{
    return m_messageFlag;
}

void CDMRData::setMessage(const unsigned char* buffer, unsigned int size)
{
    if(size > 1024U)
        return;
    ::memcpy(m_message, buffer, size);
    m_messageSize = size;
    m_messageFlag = true;
}

unsigned int CDMRData::getMessage(unsigned char* buffer) const
{
    if(m_messageFlag)
    {
        ::memcpy(buffer, m_message, m_messageSize);
        return m_messageSize;
    }
    return 0U;
}
