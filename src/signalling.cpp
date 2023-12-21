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

#include "signalling.h"

Signalling::Signalling(Settings *settings)
{
    _settings = settings;
    _pad_nibbles_mapping[0] = 18;
    _pad_nibbles_mapping[1] = 16;
    _pad_nibbles_mapping[2] = 14;
    _pad_nibbles_mapping[3] = 12;
    _pad_nibbles_mapping[4] = 10;
    _pad_nibbles_mapping[5] = 8;
    _pad_nibbles_mapping[6] = 6;
    _pad_nibbles_mapping[7] = 4;
    _pad_nibbles_mapping[8] = 2;
    _pad_nibbles_mapping[9] = 0;
    _pad_nibbles_mapping[10] = 22;
    _pad_nibbles_mapping[12] = 20;
    _pad_nibbles_mapping[12] = 18;
    _pad_nibbles_mapping[13] = 16;
    _pad_nibbles_mapping[14] = 14;
    _pad_nibbles_mapping[15] = 12;
    _pad_nibbles_mapping[16] = 10;
    _pad_nibbles_mapping[17] = 8;
    _pad_nibbles_mapping[18] = 6;
    _pad_nibbles_mapping[19] = 4;
    _pad_nibbles_mapping[20] = 2;
    _pad_nibbles_mapping[21] = 0;
    _pad_nibbles_mapping[22] = 22;
    _pad_nibbles_mapping[23] = 20;
    _pad_nibbles_mapping[24] = 18;
    _pad_nibbles_mapping[25] = 16;
    _pad_nibbles_mapping[26] = 14;
    _pad_nibbles_mapping[27] = 12;
    _pad_nibbles_mapping[28] = 10;
    _pad_nibbles_mapping[29] = 8;
    _pad_nibbles_mapping[30] = 6;
    _pad_nibbles_mapping[31] = 4;
    _pad_nibbles_mapping[32] = 2;
    _pad_nibbles_mapping[33] = 0;
    _pad_nibbles_mapping[34] = 22;
    _pad_nibbles_mapping[35] = 20;
    _pad_nibbles_mapping[36] = 18;
    _pad_nibbles_mapping[37] = 16;
    _pad_nibbles_mapping[38] = 14;
    _pad_nibbles_mapping[39] = 12;
    _pad_nibbles_mapping[40] = 10;
    _pad_nibbles_mapping[41] = 8;
    _pad_nibbles_mapping[42] = 6;
    _pad_nibbles_mapping[43] = 4;
    _pad_nibbles_mapping[44] = 2;
    _pad_nibbles_mapping[45] = 0;

    _uab_mapping[0] = 1;
    _uab_mapping[1] = 1;
    _uab_mapping[2] = 1;
    _uab_mapping[3] = 1;
    _uab_mapping[4] = 1;
    _uab_mapping[5] = 1;
    _uab_mapping[6] = 1;
    _uab_mapping[7] = 1;
    _uab_mapping[8] = 1;
    _uab_mapping[9] = 1;
    _uab_mapping[10] = 2;
    _uab_mapping[12] = 2;
    _uab_mapping[12] = 2;
    _uab_mapping[13] = 2;
    _uab_mapping[14] = 2;
    _uab_mapping[15] = 2;
    _uab_mapping[16] = 2;
    _uab_mapping[17] = 2;
    _uab_mapping[18] = 2;
    _uab_mapping[19] = 2;
    _uab_mapping[20] = 2;
    _uab_mapping[21] = 2;
    _uab_mapping[22] = 3;
    _uab_mapping[23] = 3;
    _uab_mapping[24] = 3;
    _uab_mapping[25] = 3;
    _uab_mapping[26] = 3;
    _uab_mapping[27] = 3;
    _uab_mapping[28] = 3;
    _uab_mapping[29] = 3;
    _uab_mapping[30] = 3;
    _uab_mapping[31] = 3;
    _uab_mapping[32] = 3;
    _uab_mapping[33] = 3;
    _uab_mapping[34] = 4;
    _uab_mapping[35] = 4;
    _uab_mapping[36] = 4;
    _uab_mapping[37] = 4;
    _uab_mapping[38] = 4;
    _uab_mapping[39] = 4;
    _uab_mapping[40] = 4;
    _uab_mapping[41] = 4;
    _uab_mapping[42] = 4;
    _uab_mapping[43] = 4;
    _uab_mapping[44] = 4;
    _uab_mapping[45] = 4;
}

void Signalling::getUABPadNibble(unsigned int msg_size, unsigned int &UAB, unsigned int &pad_nibble)
{
    if(msg_size > 46)
        return;
    pad_nibble = _pad_nibbles_mapping[msg_size - 1];
    UAB = _uab_mapping[msg_size - 1];
}

void Signalling::createLateEntryAnnouncement(LogicalChannel *logical_channel, CDMRCSBK &csbk)
{
    uint8_t emergency_call = 0; // TODO
    uint8_t late_entry = 1; // TODO
    if(logical_channel->getCallType() == CallType::CALL_TYPE_GROUP)
    {
        csbk.setCSBKO(CSBKO_TV_GRANT);
    }
    else
    {
        csbk.setCSBKO(CSBKO_PV_GRANT);
    }
    uint16_t phys_chan = logical_channel->getPhysicalChannel() + 1;
    uint8_t c1 = phys_chan >> 4;
    csbk.setData1(c1);
    uint8_t aligned_timing = 0; //TODO
    uint8_t c2 = (phys_chan << 4) & 0xFF;
    uint8_t data2 = c2;
    data2 |= ((logical_channel->getSlot() - 1) << 3);
    data2 |= late_entry << 2;
    data2 |= emergency_call << 1;
    data2 |= aligned_timing;
    csbk.setCBF(data2);  // 0x18
    if(logical_channel->getCallType() == CallType::CALL_TYPE_GROUP)
    {
        csbk.setDstId(Utils::convertBase10ToBase11GroupNumber(logical_channel->getDestination()));
    }
    else
    {
        csbk.setDstId(logical_channel->getDestination());
    }
    csbk.setSrcId(logical_channel->getSource());
}

bool Signalling::createAbsoluteParameters(CDMRCSBK &csbk1, CDMRCSBK &csbk2,
                                               LogicalChannel *&logical_channel)
{
    if(!_settings->use_absolute_channel_grants)
        return false;
    if(logical_channel == nullptr)
    {
        return false;
    }
    uint64_t params;
    uint8_t colour_code;
    bool valid = logical_channel->getChannelParams(params, colour_code);
    if(!valid)
    {
        return false;
    }
    // Continuation CSBK
    csbk2.setCSBKO(csbk1.getCSBKO());
    csbk2.setFID((unsigned char)colour_code);
    csbk2.setData1((params >> 56) & 0xFF);
    csbk2.setCBF((params >> 48) & 0xFF);
    csbk2.setDstId((params >> 24) & 0xFFFFFF);
    csbk2.setSrcId(params & 0xFFFFFF);
    csbk2.setDataType(DT_MBC_CONTINUATION);

    // rewrite Header
    csbk1.setLB(false);
    // erase logical channel
    csbk1.setData1(0xFF);
    unsigned char cbf = csbk1.getCBF() | 0xF0;
    csbk1.setCBF(cbf);
    csbk1.setDataType(DT_MBC_HEADER);
    return true;
}

void Signalling::createRegistrationRequest(CDMRCSBK &csbk)
{
    uint8_t announcement_type = 0x04 << 3; // MassReg
    uint8_t reg = 1;
    uint8_t par = 3; // PAR AB
    uint16_t system_id = (uint16_t)_settings->system_identity_code << 2;
    system_id = system_id | par;
    uint16_t bcast_parms1 = 8 << 2; // Reg Window (100 sec) + ALOHA mask
    uint32_t bcast_parms2 = 0;
    csbk.setCSBKO(CSBKO_C_BCAST);
    csbk.setFID(0x00);
    unsigned char data1 = (unsigned char) announcement_type;
    csbk.setData1(data1);
    unsigned char data2 = bcast_parms1;
    csbk.setCBF(data2);
    unsigned int data3 = (reg << 4 ) << 16;
    data3 |= 8 << 16; // random backoff
    data3 |= system_id;
    csbk.setDstId(data3);
    csbk.setSrcId(bcast_parms2);
}

void Signalling::createLogicalPhysicalChannelsAnnouncement(CDMRCSBK &csbk1, CDMRCSBK &csbk_cont, QMap<QString, uint64_t> channel)
{
    uint8_t announcement_type = 0x05 << 3;
    uint32_t reg = 1;
    uint16_t system_id = (uint16_t)_settings->system_identity_code << 2;
    system_id = system_id | 0x03; // AB MSs
    uint32_t bcast_parms2 = channel.value("logical_channel");
    csbk1.setCSBKO(CSBKO_C_BCAST, false, false);
    csbk1.setFID(0x00);
    unsigned char data1 = (unsigned char) announcement_type;
    csbk1.setData1(data1);
    csbk1.setCBF(0x00);
    unsigned int data3 = 0;

    data3 |= system_id;
    data3 |= 2 << 16; // backoff
    data3 |= reg << 20;
    csbk1.setDstId(data3);
    csbk1.setSrcId(bcast_parms2);
    csbk1.setDataType(DT_MBC_HEADER);

    csbk_cont.setCSBKO(CSBKO_C_BCAST);
    csbk_cont.setFID(0x00);

    uint64_t lcn = channel.value("logical_channel");
    uint64_t tx_value_khz = channel.value("tx_freq") % 1000000 / 125;
    uint64_t rx_value_khz = channel.value("rx_freq") % 1000000 / 125;
    uint64_t tx_value_Mhz = channel.value("tx_freq") / 1000000;
    uint64_t rx_value_Mhz = channel.value("rx_freq") / 1000000;
    uint64_t params = 0;
    params |= rx_value_khz;
    params |= rx_value_Mhz << 13;
    params |= tx_value_khz << 23;
    params |= tx_value_Mhz << 36;
    params |= lcn << 46;
    csbk_cont.setData1((params >> 56) & 0xFF);
    csbk_cont.setCBF((params >> 48) & 0xFF);
    csbk_cont.setDstId((params >> 24) & 0xFFFFFF);
    csbk_cont.setSrcId(params & 0xFFFFFF);
    csbk_cont.setDataType(DT_MBC_CONTINUATION);
}

void Signalling::createLocalTimeAnnouncement(CDMRCSBK &csbk, QDateTime date_time)
{
    uint32_t reg = 1;
    uint16_t system_id = (uint16_t)_settings->system_identity_code << 2;
    system_id = system_id | 0x03; // AB MSs
    uint8_t announcement_type = 0x03 << 3;
    unsigned int broadcast_parms1 = 0;
    broadcast_parms1 |= date_time.date().day() << 9;
    broadcast_parms1 |= date_time.date().month() << 5;
    broadcast_parms1 |= date_time.offsetFromUtc() << 1;
    broadcast_parms1 |= (uint8_t)(date_time.offsetFromUtc() < 0);
    uint32_t bcast_parms2 = 0;
    bcast_parms2 |= date_time.time().hour() << 19;
    bcast_parms2 |= date_time.time().minute() << 13;
    bcast_parms2 |= date_time.time().second() << 7;
    bcast_parms2 |= date_time.date().dayOfWeek() << 4;
    csbk.setCSBKO(CSBKO_C_BCAST);
    csbk.setFID(0x00);
    unsigned char data1 = 0;
    data1 |= (unsigned char) announcement_type;
    data1 |= (broadcast_parms1 >> 11) & 0x07;
    csbk.setData1(data1);
    csbk.setCBF((broadcast_parms1 >> 3) & 0xFF);
    unsigned int data3 = 0;
    data3 |= ((broadcast_parms1 << 5) & 0xFF) << 21;
    data3 |= system_id;
    data3 |= 2 << 16; // backoff
    data3 |= reg << 20;
    csbk.setDstId(data3);
    csbk.setSrcId(bcast_parms2);
}

void Signalling::createPresenceCheckAhoy(CDMRCSBK &csbk, unsigned int target_id, bool group)
{
    csbk.setCSBKO(CSBKO_AHOY);
    csbk.setFID(0x00);
    csbk.setData1(0x00);
    unsigned char data2 = ServiceKind::RegiAuthMSCheck;
    data2 |= (group) ? (1 << 6) : (0 << 6);
    csbk.setCBF(data2);
    csbk.setDstId(target_id & 0xFFFFFF);
    csbk.setSrcId(StandardAddreses::TSI);
}

void Signalling::createReplyMessageAccepted(CDMRCSBK &csbk, unsigned int dstId, unsigned int srcId, bool from_ts)
{
    csbk.setCSBKO(CSBKO_ACKD);
    csbk.setFID(0x00);
    unsigned int response_info = 0x00;
    unsigned int reason = 0x60; // reason: message_accepted
    if(!from_ts)
        reason = 0x44;
    unsigned int data1 = (response_info << 1) | reason >> 7;
    unsigned int data2 = (reason << 1);
    csbk.setData1(data1);
    csbk.setCBF(data2);
    csbk.setDstId(dstId);
    csbk.setSrcId(srcId);
}

void Signalling::createReplyRegistrationAccepted(CDMRCSBK &csbk, unsigned int dstId)
{
    uint8_t accepted_registrations_mask = 0xFE; // be generous in case the manufacturer isn't
    csbk.setCSBKO(CSBKO_ACKD);
    unsigned char data1 = (unsigned char) accepted_registrations_mask;
    csbk.setData1(data1);
    csbk.setCBF(0xC4);
    csbk.setDstId(dstId);
    csbk.setSrcId(StandardAddreses::REGI);
}

void Signalling::createPrivateCallRequest(CDMRCSBK &csbk, bool local, unsigned int srcId, unsigned int dstId)
{
    csbk.setCSBKO(CSBKO_AHOY);
    unsigned int service_kind_flag = 1; // FOACSU: 1, OACSU: 0
    if(!local)
        service_kind_flag = 0;
    csbk.setFID(0x00);
    csbk.setData1(service_kind_flag);
    csbk.setCBF(0x00);  // 0x18
    csbk.setDstId(dstId);
    csbk.setSrcId(srcId);
}

void Signalling::createPrivateVoiceGrant(CDMRCSBK &csbk, LogicalChannel *logical_channel, unsigned int srcId, unsigned int dstId)
{
    uint8_t emergency_call = 0; // TODO
    uint8_t late_entry = 0; // TODO
    csbk.setCSBKO(CSBKO_PV_GRANT);
    csbk.setFID(0x00);
    unsigned int phys_chan = logical_channel->getPhysicalChannel() + 1; // our phys channel numbering starts at 0, MS starts at 1
    unsigned char c1 = (phys_chan >> 4) & 0xFF;
    csbk.setData1(c1);
    unsigned char aligned_timing = 0x00; //TODO
    unsigned char c2 = (phys_chan & 0xFF) << 4;
    unsigned char data2 = c2;
    data2 |= ((logical_channel->getSlot() - 1) << 3) & 0x08;
    data2 |= (late_entry << 2) & 0x04;
    data2 |= (emergency_call << 1) & 0x02;
    data2 |= aligned_timing;
    csbk.setCBF(data2);  // 0x18
    csbk.setDstId(dstId);
    csbk.setSrcId(srcId);
}

void Signalling::createGroupVoiceGrant(CDMRCSBK &csbk, LogicalChannel *logical_channel, unsigned int srcId, unsigned int dstId)
{
    uint8_t emergency_call = 0; // TODO
    uint8_t late_entry = 0;
    csbk.setCSBKO(CSBKO_TV_GRANT);
    csbk.setFID(0x00);
    unsigned int phys_chan = logical_channel->getPhysicalChannel() + 1; // our phys channel numbering starts at 0, MS starts at 1
    unsigned char c1 = (phys_chan >> 4) & 0xFF;
    csbk.setData1(c1);
    unsigned char aligned_timing = 0x00; //TODO
    unsigned char c2 = (phys_chan & 0xFF) << 4;
    unsigned char data2 = c2;
    data2 |= ((logical_channel->getSlot() - 1) << 3) & 0x08;
    data2 |= (late_entry << 2) & 0x04;
    data2 |= (emergency_call << 1) & 0x02;
    data2 |= aligned_timing;
    csbk.setCBF(data2);  // 0x18
    csbk.setDstId(dstId);
    csbk.setSrcId(srcId);
}

void Signalling::createClearChannelUserInitiated(CDMRCSBK &csbk, LogicalChannel *logical_channel, unsigned int dstId, bool group_call)
{
    csbk.setCSBKO(CSBKO_P_CLEAR);
    uint16_t channel = 0;
    uint8_t c1 = channel >> 4;
    csbk.setData1(c1);
    uint8_t c2 = (channel << 4) & 0xFF;
    c2 |= (uint8_t)group_call;
    csbk.setCBF(c2);
    dstId = logical_channel->getDestination();
    csbk.setDstId(dstId);
    csbk.setSrcId(StandardAddreses::TSI);
}

void Signalling::createChannelIdleDeallocation(CDMRCSBK &csbk, unsigned int call_type)
{
    csbk.setCSBKO(CSBKO_P_CLEAR);
    csbk.setFID(0x00);
    uint16_t channel = 0;
    uint8_t c1 = channel >> 4;
    csbk.setData1(c1);
    uint8_t c2 = (channel << 4) & 0xFF;
    c2 |= (uint8_t)call_type;
    csbk.setCBF(c2);
    // All clear
    //csbk.setDstId(_logical_channels.at(channel_id)->getDestination());
    csbk.setDstId(StandardAddreses::ALLMSI);
    csbk.setSrcId(StandardAddreses::TSI);
}

void Signalling::createRequestToUploadTgAttachments(CDMRCSBK &csbk, unsigned int dstId, unsigned int UAB)
{
    unsigned int data1 = csbk.getServiceOptions() << 1;
    csbk.setCSBKO(CSBKO_AHOY);
    csbk.setFID(0x00);
    data1 |= 1;
    unsigned int data2 = (UAB << 4) | csbk.getServiceKind();
    csbk.setData1(data1);
    csbk.setCBF(data2);
    csbk.setDstId(dstId);
    csbk.setSrcId(StandardAddreses::TATTSI);
}

void Signalling::createReplyCallRejected(CDMRCSBK &csbk, unsigned int srcId, unsigned int dstId)
{
    unsigned char recipient_refused = 0x14;
    csbk.setCSBKO(CSBKO_ACKD);
    csbk.setFID(0x00);
    csbk.setData1(0x00);
    csbk.setCBF(recipient_refused << 1);
    csbk.setDstId(dstId);
    csbk.setSrcId(srcId);
}

void Signalling::createCancelPrivateCallAhoy(CDMRCSBK &csbk, unsigned int dstId)
{
    unsigned char cancel_call = 0x0F;
    csbk.setCSBKO(CSBKO_AHOY);
    csbk.setFID(0x00);
    csbk.setData1(0x00); // Reponse info (not set) + 1 bit reason code 1010 0000
    csbk.setCBF(cancel_call);
    csbk.setDstId(dstId);
    csbk.setSrcId(StandardAddreses::TSI);
}

void Signalling::createCallDisconnect(CDMRCSBK &csbk, unsigned int dstId, bool group_call)
{
    csbk.setCSBKO(CSBKO_P_CLEAR);
    uint16_t channel = 0;
    uint8_t c1 = channel >> 4;
    csbk.setData1(c1);
    uint8_t c2 = (channel << 4) & 0xFF;
    c2 |= (uint8_t)group_call;
    csbk.setCBF(c2);
    csbk.setDstId(dstId);
    csbk.setSrcId(StandardAddreses::TSI);
}

unsigned int Signalling::createRequestToUploadMessage(CDMRCSBK &csbk, unsigned int dstId)
{
    unsigned int number_of_blocks = csbk.getCBF() >> 4;
    csbk.setCSBKO(CSBKO_AHOY);
    csbk.setFID(0x00);
    unsigned int data2 = (number_of_blocks << 4) | csbk.getServiceKind();
    csbk.setData1(0x00);
    csbk.setCBF(data2);
    csbk.setDstId(dstId);
    csbk.setSrcId(StandardAddreses::SDMI);
    return number_of_blocks;
}

void Signalling::createRequestToSendGroupCallSupplimentaryData(CDMRCSBK &csbk, unsigned int dstId)
{
    csbk.setCSBKO(CSBKO_AHOY);
    csbk.setFID(0x00);
    unsigned char data1 = csbk.getServiceOptions() << 1;
    unsigned int data2 = csbk.getServiceKind();
    csbk.setData1(data1);
    csbk.setCBF(data2);
    csbk.setDstId(dstId);
    csbk.setSrcId(StandardAddreses::TSI);
}

void Signalling::createReplyWaitForSignalling(CDMRCSBK &csbk, unsigned int dstId)
{
    csbk.setCSBKO(CSBKO_ACKD);
    csbk.setFID(0x00);
    csbk.setData1(0x01); // Reponse info (not set) + 1 bit reason code 1010 0000
    csbk.setCBF(0x40);
    csbk.setDstId(dstId);
    csbk.setSrcId(StandardAddreses::TSI);
}

void Signalling::createReplyCallQueued(CDMRCSBK &csbk, unsigned int dstId)
{
    csbk.setCSBKO(CSBKO_ACKD);
    unsigned int response_info = 0;
    unsigned int reason_code = 160; // reason code 1010 0000 queued
    unsigned char data1 = 0x00;
    data1 |= response_info << 1;
    data1 |= (reason_code >> 7) & 0xFF;
    csbk.setData1(data1); // Reponse info (not set) + 1 bit reason code
    unsigned char data2 = (reason_code << 1) & 0xFF;
    csbk.setCBF(data2);
    csbk.setDstId(dstId);
    csbk.setSrcId(StandardAddreses::TSI);
}

void Signalling::createReplyCallDenied(CDMRCSBK &csbk, unsigned int dstId)
{
    csbk.setCSBKO(CSBKO_ACKD);
    unsigned int response_info = 0;
    unsigned int reason_code = 39; // reason code 0010 0111 busy
    unsigned char data1 = 0x00;
    data1 |= response_info << 1;
    data1 |= (reason_code >> 7) & 0xFF;
    csbk.setData1(data1);
    unsigned char data2 = (reason_code << 1) & 0xFF;
    csbk.setCBF(data2);
    csbk.setDstId(dstId);
    csbk.setSrcId(StandardAddreses::TSI);
}

void Signalling::createReplyNotRegistered(CDMRCSBK &csbk, unsigned int dstId)
{
    csbk.setCSBKO(CSBKO_ACKD);
    unsigned int response_info = 0;
    unsigned int reason_code = 45;
    unsigned char data1 = 0x00;
    data1 |= response_info << 1;
    data1 |= (reason_code >> 7) & 0xFF;
    csbk.setData1(data1);
    unsigned char data2 = (reason_code << 1) & 0xFF;
    csbk.setCBF(data2);
    csbk.setDstId(dstId);
    csbk.setSrcId(StandardAddreses::TSI);
}

void Signalling::createReplyDeregistrationAccepted(CDMRCSBK &csbk, unsigned int dstId)
{
    csbk.setCSBKO(CSBKO_ACKD);
    unsigned int response_info = 0;
    unsigned int reason_code = 98;
    unsigned char data1 = 0x00;
    data1 |= response_info << 1;
    data1 |= (reason_code >> 7) & 0xFF;
    csbk.setData1(data1);
    unsigned char data2 = (reason_code << 1) & 0xFF;
    csbk.setCBF(data2);
    csbk.setDstId(dstId);
    csbk.setSrcId(StandardAddreses::REGI);
}

void Signalling::createReplyUDTCRCError(CDMRCSBK &csbk, unsigned int dstId)
{
    csbk.setCSBKO(CSBKO_ACKD);
    unsigned int response_info = 0;
    unsigned int reason_code = 48; // reason code 0010 0111 busy
    unsigned char data1 = 0x00;
    data1 |= response_info << 1;
    data1 |= (reason_code >> 7) & 0xFF;
    csbk.setData1(data1);
    unsigned char data2 = (reason_code << 1) & 0xFF;
    csbk.setCBF(data2);
    csbk.setDstId(dstId);
    csbk.setSrcId(StandardAddreses::TSI);
}

void Signalling::createClearChannelAll(CDMRCSBK &csbk, unsigned int call_type)
{
    csbk.setCSBKO(CSBKO_P_CLEAR);
    csbk.setFID(0x00);
    uint16_t channel = 0;
    uint8_t c1 = channel >> 4;
    csbk.setData1(c1);
    uint8_t c2 = channel << 4;
    c2 |= (uint8_t)call_type;
    csbk.setCBF(c2);
    // All clear
    csbk.setDstId(StandardAddreses::ALLMSI);
    csbk.setSrcId(StandardAddreses::TSI);
}

