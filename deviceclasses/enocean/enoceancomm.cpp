//
//  Copyright (c) 2013-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
//
//  This file is part of p44vdc.
//
//  p44vdc is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  p44vdc is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with p44vdc. If not, see <http://www.gnu.org/licenses/>.
//

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL IFNOTREDUCEDFOOTPRINT(7)

#include "enoceancomm.hpp"

#if ENABLE_ENOCEAN

using namespace p44;


// MARK: - ESP3 packet object

// enoceansender hex up:
// 55 00 07 07 01 7A F6 30 00 86 B8 1A 30 03 FF FF FF FF FF 00 C0

Esp3Packet::Esp3Packet() :
  payloadP(NULL)
{
  clear();
}


Esp3Packet::~Esp3Packet()
{
  clear();
}


void Esp3Packet::clear()
{
  clearData();
  memset(header, 0, sizeof(header));
  state = ps_syncwait;
}


void Esp3Packet::clearData()
{
  if (payloadP) {
    if (payloadP) delete [] payloadP;
    payloadP = NULL;
  }
  payloadSize = 0;
}



// ESP3 Header
//  0 : 0x55 sync byte
//  1 : data length MSB
//  2 : data length LSB
//  3 : optional data length
//  4 : packet type
//  5 : CRC over bytes 1..4

#define ESP3_HEADERBYTES 6



size_t Esp3Packet::dataLength()
{
  return (header[1]<<8) + header[2];
}

void Esp3Packet::setDataLength(size_t aNumBytes)
{
  header[1] = (aNumBytes>>8) & 0xFF;
  header[2] = (aNumBytes) & 0xFF;
}


size_t Esp3Packet::optDataLength()
{
  return header[3];
}

void Esp3Packet::setOptDataLength(size_t aNumBytes)
{
  header[3] = aNumBytes;
}


PacketType Esp3Packet::packetType()
{
  return (PacketType)header[4];
}


void Esp3Packet::setPacketType(PacketType aPacketType)
{
  header[4] = (uint8_t)aPacketType;
}


uint8_t Esp3Packet::headerCRC()
{
  return crc8(header+1, ESP3_HEADERBYTES-2);
}


uint8_t Esp3Packet::payloadCRC()
{
  if (!payloadP) return 0;
  return crc8(payloadP, payloadSize-1); // last byte of payload is CRC itself
}


bool Esp3Packet::isComplete()
{
  return state==ps_complete;
}



size_t Esp3Packet::acceptBytes(size_t aNumBytes, const uint8_t *aBytes, bool aNoChecks)
{
  size_t replayBytes = 0;
  size_t acceptedBytes = 0;
  uint8_t *replayP;
  // completed packets do not accept any more bytes
  if (state==ps_complete) return 0;
  // process bytes
  while (acceptedBytes<aNumBytes || replayBytes>0) {
    uint8_t byte;
    if (replayBytes>0) {
      // reconsider already stored byte
      byte = *replayP++;
      replayBytes--;
    }
    else {
      // process a new byte
      byte = *aBytes;
      // next
      aBytes++;
      acceptedBytes++;
    }
    switch (state) {
      case ps_syncwait:
        // waiting for 0x55 sync byte
        if (byte==0x55) {
          // potential start of packet
          header[0] = byte;
          // - start reading header
          state = ps_headerread;
          dataIndex = 1;
        }
        break;
      case ps_headerread:
        // collecting header bytes 1..5
        header[dataIndex] = byte;
        ++dataIndex;
        if (dataIndex==ESP3_HEADERBYTES) {
          // header including CRC received
          // - check header CRC now (unless disabled)
          if (!aNoChecks && header[ESP3_HEADERBYTES-1]!=headerCRC()) {
            // CRC mismatch
            // - replay from byte 1 (which could be a sync byte again)
            replayP = header+1; // consider 2nd byte of already received and stored header as potential start
            replayBytes = ESP3_HEADERBYTES-1;
            // - back to syncwait
            state = ps_syncwait;
          }
          else {
            // CRC matches, now read data
            // - make sure we have a buffer according to dataLength() and optDataLength()
            data();
            dataIndex = 0; // start of data read
            // - enter payload read state
            state = ps_dataread;
          }
        }
        break;
      case ps_dataread:
        // collecting payload
        payloadP[dataIndex] = byte;
        ++dataIndex;
        if (dataIndex==payloadSize) {
          // payload including CRC received
          // - check payload CRC now (unless disabled)
          if (!aNoChecks && payloadP[payloadSize-1]!=payloadCRC()) {
            // payload CRC mismatch, discard packet, start scanning for packet at next byte
            clear();
          }
          else {
            // packet is complete,
            state = ps_complete;
            // just return number of bytes accepted to complete it
            return acceptedBytes;
          }
        }
        break;
      default:
        // something's wrong, reset the packet
        clear();
        break;
    }
  }
  // number of bytes accepted (but packet not complete)
  return acceptedBytes;
}


uint8_t *Esp3Packet::data()
{
  size_t s = dataLength()+optDataLength()+1; // one byte extra for CRC
  if (s!=payloadSize || !payloadP) {
    if (payloadSize>300) {
      // safety - prevent huge telegrams
      clearData();
      return NULL;
    }
    payloadSize = s;
    if (payloadP) delete [] payloadP;
    payloadP = new uint8_t[payloadSize];
    memset(payloadP, 0, payloadSize); // zero out
  }
  return payloadP;
}


uint8_t *Esp3Packet::optData()
{
  uint8_t *o = data();
  if (o) {
    o += dataLength();
  }
  return o;
}



void Esp3Packet::finalize()
{
  if (state!=ps_complete) {
    // force creation of payload (usually already done, but to make sure to avoid crashes)
    data();
    // set sync byte
    header[0] = 0x55;
    // assign header CRC
    header[ESP3_HEADERBYTES-1] = headerCRC();
    // assign payload CRC
    if (payloadP) {
      payloadP[payloadSize-1] = payloadCRC();
    }
    // packet is complete now
    state = ps_complete;
  }
}



// MARK: - common commands


ErrorPtr Esp3Packet::responseStatus()
{
  if (packetType()!=pt_response || dataLength()<1) {
    return ErrorPtr(new EnoceanCommError(EnoceanCommError::WrongPacket));
  }
  else {
    // is response, check
    uint8_t respCode = data()[0];
    EnoceanCommError::ErrorCodes errorCode;
    switch (respCode) {
      case RET_OK: return (ErrorPtr()); // is ok
      case RET_NOT_SUPPORTED: errorCode = EnoceanCommError::Unsupported; break;
      case RET_WRONG_PARAM: errorCode = EnoceanCommError::BadParam; break;
      case RET_OPERATION_DENIED: errorCode = EnoceanCommError::Denied; break;
      default: errorCode = EnoceanCommError::CmdError; break;
    }
    return ErrorPtr(new EnoceanCommError(errorCode));
  }
}


// MARK: - radio telegram specifics


// Radio telegram optional data
//  0    : Subtelegram Number, 3 for set, 1..n for receive
//  1..4 : destination address, FFFFFFFF = broadcast
//  5    : dBm, send: set to FF, receive: best RSSI value of all subtelegrams
//  6    : security level: 0 = unencrypted, 1..F = type of encryption


uint8_t Esp3Packet::radioSubtelegrams()
{
  uint8_t *o = optData();
  if (!o || optDataLength()<7) return 0;
  return o[0];
}


EnoceanAddress Esp3Packet::radioDestination()
{
  uint8_t *o = optData();
  if (!o || optDataLength()<7) return 0;
  return
    (o[1]<<24) +
    (o[2]<<16) +
    (o[3]<<8) +
    (o[4]);
}


void Esp3Packet::setRadioDestination(EnoceanAddress aEnoceanAddress)
{
  uint8_t *o = optData();
  if (!o || optDataLength()<7) return;
  o[1] = (aEnoceanAddress>>24) & 0xFF;
  o[2] = (aEnoceanAddress>>16) & 0xFF;
  o[3] = (aEnoceanAddress>>8) & 0xFF;
  o[4] = aEnoceanAddress & 0xFF;
}



int Esp3Packet::radioDBm()
{
  uint8_t *o = optData();
  if (!o || optDataLength()<7) return 0;
  return -o[5];
}


uint8_t Esp3Packet::radioSecurityLevel()
{
  uint8_t *o = optData();
  if (!o || optDataLength()<7) return 0;
  return o[6];
}


void Esp3Packet::setRadioSecurityLevel(uint8_t aSecLevel)
{
  uint8_t *o = optData();
  if (!o || optDataLength()<7) return;
  o[6] = aSecLevel;
}


uint8_t Esp3Packet::radioStatus()
{
  RadioOrg rorg = eepRorg();
  int statusoffset = 0;
  if (rorg!=rorg_invalid) {
    statusoffset = (int)dataLength()-1; // last byte is status...
  }
  if (statusoffset<0) return 0;
  return data()[statusoffset]; // this is the status byte
}


uint8_t Esp3Packet::radioRepeaterCount()
{
  return radioStatus() & status_repeaterCount_mask;
}



size_t Esp3Packet::radioUserDataLength()
{
  if (packetType()!=pt_radio_erp1) return 0; // no data
  int bytes = (int)dataLength(); // start with actual length
  bytes -= 1; // RORG byte
  bytes -= 1; // one status byte
  bytes -= 4; // 4 bytes for sender
  return bytes<0 ? 0 : bytes;
}


void Esp3Packet::setRadioUserDataLength(size_t aSize)
{
  if (packetType()!=pt_radio_erp1) return; // is not radio packet
  // add extra length needed for fixed fields in radio packet
  aSize += 1; // RORG byte
  aSize += 1; // one status byte
  aSize += 4; // 4 bytes for sender
  // this is the complete data length
  setDataLength(aSize);
}



uint8_t *Esp3Packet::radioUserData()
{
  if (radioUserDataLength()==0) return NULL;
  uint8_t *d = data();
  return d+1;
}


EnoceanAddress Esp3Packet::radioSender()
{
  size_t l = radioUserDataLength(); // returns 0 for non-radio packets
  if (l>0) {
    uint8_t *d = data()+1+l; // skip RORG and userdata
    return
      (d[0]<<24) +
      (d[1]<<16) +
      (d[2]<<8) +
      d[3];
  }
  else
    return 0;
}


void Esp3Packet::setRadioSender(EnoceanAddress aEnoceanAddress)
{
  size_t l = radioUserDataLength(); // returns 0 for non-radio packets
  if (l>0) {
    uint8_t *d = data()+1+l; // skip RORG and userdata
    d[0] = (aEnoceanAddress>>24) & 0xFF;
    d[1] = (aEnoceanAddress>>16) & 0xFF;
    d[2] = (aEnoceanAddress>>8) & 0xFF;
    d[3] = aEnoceanAddress & 0xFF;
  }
}


void Esp3Packet::setRadioStatus(uint8_t aStatus)
{
  size_t l = radioUserDataLength(); // returns 0 for non-radio packets
  if (l>0) {
    uint8_t *d = data()+1+l+4; // skip RORG, userdata and sender address to reach status
    d[0] = aStatus;
  }
}




void Esp3Packet::initForRorg(RadioOrg aRadioOrg, size_t aVLDsize)
{
  clear(); // init
  // set as radio telegram
  setPacketType(pt_radio_erp1);
  // radio telegrams always have 7 fields of optional data
  setOptDataLength(7);
  // depending on radio org, set payload size
  switch (aRadioOrg) {
    case rorg_RPS:
    case rorg_1BS:
      setRadioUserDataLength(1);
      break;
    case rorg_4BS:
      setRadioUserDataLength(4);
      break;
    case rorg_VLD:
    case rorg_SEC_TEACHIN:
      if (aVLDsize>14) aVLDsize=14; // limit to max
      else if (aVLDsize<1) aVLDsize=1; // limit to min
      setRadioUserDataLength(aVLDsize);
      break;
    case rorg_UTE:
      setRadioUserDataLength(7);
      break;
    default:
      break;
  }
  // set the radio org
  data()[0] = aRadioOrg;
  // now set optional data defaults
  uint8_t *o = optData();
  // - subTelegramNo for sending is always 3
  o[0] = 3;
  // - dBm for sending is always 0xFF
  o[5] = 0xFF;
  // default to no security
  setRadioSecurityLevel(0);
}



// MARK: - Enocean Eqipment Profile (EEP) information extraction


// Radio telegram data (in ESP3, does not contain data checksum crc, e.g. for VLD)
//  0        : RORG
//  1..n     : user data, n bytes
//  n+1..n+4 : sender address
//  n+5      : status

RadioOrg Esp3Packet::eepRorg()
{
  if (packetType()!=pt_radio_erp1) return rorg_invalid; // no radio
  uint8_t *d = data();
  if (!d || dataLength()<1) return rorg_invalid; // no RORG
  return (RadioOrg)d[0]; // this is the RORG byte
}



//  RPS Signatures and conflicts (generated by rpsclash)
//  ====================================================
//
//             Status  Data      EEP        Function Description
//             T21 NU  76543210  Profile        (conflicting function)
//             --- --  --------  --------   -----------
//
//  Signature: 1   0   00010000  F6-01-01   single button pressed
//
//  Signature: 1   1   0xx10000  F6-02-xx   2-Rocker single action
//           - 1   1   01110000  F6-04-01       (Key Card inserted)
//           - 1   1   00x10000  F6-05-00       (Wind Alarm teach-in when repeated 3 times withing 2 seconds)
//           - 1   1   00x10000  F6-05-02       (Smoke Alarm teach-in when repeated 3 times withing 2 seconds)
//           - 1   1   00010000  F6-05-00       (Wind Alarm on (resent every minute) - status bits not documented, but probably as shown from observation of FRW smoke sensor)
//           - 1   1   00110000  F6-05-00       (Wind Sensor Energy low (resent every hour) - status bits not documented, but probably as shown from observation of FRW smoke sensor)
//           - 1   1   00010000  F6-05-02       (Smoke Alarm on (resent every minute) - P44 implementation existed before profile, implemented as F6-05-C0 - status bits not documented, but observed in FRW smoke sensor)
//           - 1   1   00110000  F6-05-02       (Smoke Sensor Energy low (resent every hour) - P44 implementation existed before profile, implemented as F6-05-C0 - status bits not documented, but observed in FRW smoke sensor)
//
//  Signature: 0   1   xxx10000  F6-03-xx   4-Rocker pressed single action
//
//  Signature: 1   1   00010001  F6-05-01   Water Leakage detected (or test switch on)
//           - 1   1   0xx10xx1  F6-02-xx       (2-Rocker two actions)
//
//  Signature: 1   0   11xxxxxx  F6-10-00   Window handle
//
//
//  Ignore:
//  =======
//
//  Signature: 1   0   00000000  F6-01-01   single button released
//           - 1   0   00000000  F6-02-xx       (2-Rocker all released)
//           - 1   0   0xx00000  F6-02-xx       (theoretically only: 2-Rocker single button release of multiple pressed (not mechanically possible with standard caps))
//           - 1   0   00000000  F6-04-01       (Key Card taken out)
//           - 1   0   00000000  F6-05-00       (Wind Alarm off+Energy ok (resent every 20 minutes) - status bits not documented, but probably as shown from observation of FRW smoke sensor)
//           - 1   0   00000000  F6-05-02       (Smoke Alarm off (resent every 20 minutes) - P44 implementation existed before profile, implemented as F6-05-C0 - status bits not documented, but observed in FRW smoke sensor)
//
//
//  Signature: 1   0   01100000  F6-02-xx   theoretically only: 2-Rocker 3 or 4 button release (not mechanically possible with standard caps)
//           - 1   0   0xx00000  F6-02-xx       (theoretically only: 2-Rocker single button release of multiple pressed (not mechanically possible with standard caps))
//
//  Signature: 1   0   01110000  F6-02-xx   2-Rocker 3 or 4 buttons pressed
//
//  Signature: 0   1   xxx1xxx1  F6-03-xx   4-Rocker pressed (2 actions)
//
//  Signature: 0   0   xxx10000  F6-03-xx   4-Rocker pressed (2-8 simultaneously)
//
//  Signature: 0   1   xxx0xxxx  F6-03-xx   4-Rocker released (1 or 2 actions)
//
//  Signature: 0   0   xxx00000  F6-03-xx   4-Rocker released (2-8 simultaneously)
//           - 0   0   xxx00000  F6-05-02   Apparently (evidence of HPZ 2021-12-03): F6-05-02 teach-in
//
//  Signature: 1   0   00010001  F6-05-01   Water Leakage reset (or test switch off)



// 1BS Telegrams
//
//                       D[0]
// T21 NU    7   6   5   4   3   2   1   0    RORG FUNC TYPE   Desc       Notes
// --- --   --- --- --- --- --- --- --- ---   ---- ---- ----   ---------- -------------------
//  x   x    x   x   x   x  LRN  x   x   c    D5   00   01     1 Contact  c:0=open,1=closed


// 4BS teach-in telegram (note: byte numbering is in radioUserData() buffer order, actual 4BS byte numbering is reversed!)
//
//       D[0]      |       D[1]      |       D[2]      |              D[3]
// 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 |  7   6   5   4   3   2   1   0
//
// f f f f f f t t   t t t t t m m m   m m m m m m m m   LRN EEP LRN LRN LRN  x   x   x
//    FUNC    |     TYPE      |      MANUFACTURER      | typ res res sta bit


// SA_LEARN_REQUEST (note: byte numbering is in radioUserData() buffer order, actual 4BS byte numbering is reversed!)
//
//    D[0]     D[1]   D[2] D[3] D[4] D[5] D[6] D[7] D[8] D[9] D[10] D[11] D[12] D[13] D[14] D[15]
//  rrrrrmmm mmmmmmmm RORG FUNC TYPE RSSI ID3  ID2  ID1  ID0   ID3   ID2   ID1   ID0  STAT  CHECK
//  Req  Manufacturer|   EEP No.    |dBm |    Repeater ID    |       Sender ID       |     |


// UTE Teach-In Query (note: byte numbering is in radioUserData() buffer order, actual VLD byte numbering is reversed!)
//
//           D[0]              |    D[1]   |    D[2]   |   D[3]    |    D[4]   |    D[5]   |    D[6]
//  7    6     5  4    3 2 1 0 | 765432310 | 765432310 | 76543 210 | 765432310 | 765432310 | 765432310
// BiDi NoRP TeachRQ  TeachCmd |  Channel  |  MID LSB  | resvd MID |   TYPE    |   FUNC    |    RORG



EnoceanProfile Esp3Packet::eepProfile()
{
  // default: unknown signature
  EnoceanProfile profile = eep_profile_unknown;
  RadioOrg rorg = eepRorg();
  if (rorg!=rorg_invalid) {
    // valid rorg
    if (rorg==rorg_RPS) {
      // RPS has no learn bit, some EEP signatures can be derived from bits
      uint8_t rpsStatus = radioStatus() & status_rps_mask;
      uint8_t rpsData = radioUserData()[0];
      if (rpsStatus==status_T21) {
        // T21/NU = 1/0
        if (rpsData==0x10) {
          // F6-01-01 : single button
          profile = ((EnoceanProfile)rorg<<16) | ((EnoceanProfile)0x01<<8) | (0x01);
        }
        else if ((rpsData & 0xC0) == 0xC0) {
          // F6-10-00 : Window handle
          profile = ((EnoceanProfile)rorg<<16) | ((EnoceanProfile)0x10<<8) | (0x00);
        }
      }
      else if (rpsStatus==status_NU) {
        // T21/NU = 0/1
        if ((rpsData & 0x1F)==0x10) {
          // F6-03-xx : quad rocker (one button pressed, single action)
          profile = ((EnoceanProfile)rorg<<16) | ((EnoceanProfile)0x03<<8) | (eep_func_unknown);
        }
      }
      else if (rpsStatus==status_T21+status_NU) {
        // T21/NU = 1/1
        if ((rpsData & 0x9F)==0x10) {
          // F6-02-xx : dual rocker (one button pressed, single action)
          // (has a lot of overlapping variants, which must be manually configured)
          profile = ((EnoceanProfile)rorg<<16) | ((EnoceanProfile)0x02<<8) | (eep_func_unknown);
        }
        else if (rpsData==0x11) {
          // F6-05-01 : Water Leakage detected (or test switch on)
          profile = ((EnoceanProfile)rorg<<16) | ((EnoceanProfile)0x05<<8) | (0x01);
        }
      }
      else if (rpsStatus==0) {
        // T21/NU = 0/0
        if ((rpsData & 0x9F)==0x00) {
          // F6-05-02 : smoke detector (Afriso ASD20, by example)
          profile = ((EnoceanProfile)rorg<<16) | ((EnoceanProfile)0x05<<8) | (0x02);
        }
      }
    }
    else if (rorg==rorg_1BS) {
      // 1BS has a learn bit
      if (radioHasTeachInfo()) {
        // As per March 2013, only one EEP is defined for 1BS: single contact
        profile = ((EnoceanProfile)rorg<<16) | ((EnoceanProfile)0x00<<8) | (0x01); // FUNC = contacts and switches, TYPE = single contact
      }
    }
    else if (rorg==rorg_4BS) {
      // 4BS has separate LRN telegrams
      if (radioHasTeachInfo()) {
        if ((radioUserData()[3] & LRN_EEP_INFO_VALID_MASK)!=0) {
          // teach-in has EEP info
          profile =
            (rorg<<16) |
            (((EnoceanProfile)(radioUserData()[0])<<6) & 0x3F00) | // 6 FUNC bits, shifted to bit 8..13
            (((EnoceanProfile)(radioUserData()[0])<<5) & 0x60) | // upper 2 TYPE bits, shifted to bit 5..6
            (((EnoceanProfile)(radioUserData()[1])>>3) & 0x1F); // lower 5 TYPE bits, shifted to bit 0..4
        }
        else {
          // unknown
          profile = (rorg<<16) | (eep_func_unknown<<8) | eep_type_unknown;
        }
      }
    }
    else if (rorg==rorg_SM_LRN_REQ) {
      // Smart Ack Learn Request
      profile =
        (((EnoceanProfile)radioUserData()[2])<<16) | // RORG field
        (((EnoceanProfile)radioUserData()[3])<<8) | // FUNC field
        radioUserData()[4]; // TYPE field
    }
    else if (rorg==rorg_UTE) {
      // UTE teach in request
      profile =
        (((EnoceanProfile)radioUserData()[6])<<16) | // RORG field
        (((EnoceanProfile)radioUserData()[5])<<8) | // FUNC field
        radioUserData()[4]; // TYPE field
    }
  } // valid rorg
  // return it
  return profile;
}


EnoceanManufacturer Esp3Packet::eepManufacturer()
{
  EnoceanManufacturer man = manufacturer_unknown;
  RadioOrg rorg = eepRorg();
  if (radioHasTeachInfo()) {
    if (rorg==rorg_4BS && ((radioUserData()[3] & LRN_EEP_INFO_VALID_MASK)!=0)) {
      man =
        ((((EnoceanManufacturer)radioUserData()[1])<<8) & 0x07) |
        radioUserData()[2];
    }
    else if (rorg==rorg_SM_LRN_REQ) {
      man =
        ((((EnoceanManufacturer)radioUserData()[0])&0x07)<<8) |
        radioUserData()[1];
    }
    else if (rorg==rorg_UTE) {
      man =
        ((((EnoceanManufacturer)radioUserData()[3])&0x07)<<8) |
        radioUserData()[2];
    }
  }
  // return it
  return man;
}



bool Esp3Packet::radioHasTeachInfo(int aMinLearnDBm, bool aMinDBmForAll)
{
  RadioOrg rorg = eepRorg();
  bool radioStrengthSufficient = aMinLearnDBm==0 || radioDBm()>aMinLearnDBm;
  bool explicitLearnOK = !aMinDBmForAll || radioStrengthSufficient; // ok if no restriction on radio strength OR strength sufficient
  switch (rorg) {
    case rorg_RPS:
      return radioStrengthSufficient; // RPS telegrams always have (somewhat limited) signature that can be used for teach-in
    case rorg_1BS:
      return ((radioUserData()[0] & LRN_BIT_MASK)==0) && explicitLearnOK; // 1BS telegrams have teach-in info if LRN bit is *cleared*
    case rorg_4BS:
      return ((radioUserData()[3] & LRN_BIT_MASK)==0) && explicitLearnOK; // 4BS telegrams have teach-in info if LRN bit is *cleared*
    case rorg_SM_LRN_REQ:
      return explicitLearnOK; // smart ack learn requests are by definition teach-in commands and have full EEP signature
    case rorg_UTE:
      return ((radioUserData()[0] & 0xF) == 0x00); // UTE if CMD identifier is 0 (teach-in)
    default:
      return false; // no or unknown radio telegram -> no teach-in info
  }
}


Tristate Esp3Packet::teachInfoType()
{
  if (eepRorg()==rorg_UTE) {
    uint8_t teachCmd = (radioUserData()[0]>>4)&0x03;
    switch (teachCmd) {
      case 0 : return yes; // request is specifically for teach-in
      case 1 : return no; // request is specifically for teach-out
      default: return undefined; // request is not specific, can be teach in or out
    }
  }
  return undefined;
}


// MARK: - 4BS comminication specifics


uint32_t Esp3Packet::get4BSdata()
{
  if (eepRorg()==rorg_4BS) {
    return
      (radioUserData()[0]<<24) |
      (radioUserData()[1]<<16) |
      (radioUserData()[2]<<8) |
      radioUserData()[3];
  }
  return 0;
}


void Esp3Packet::set4BSdata(uint32_t a4BSdata)
{
  if (eepRorg()==rorg_4BS) {
    radioUserData()[0] = (a4BSdata>>24) & 0xFF;
    radioUserData()[1] = (a4BSdata>>16) & 0xFF;
    radioUserData()[2] = (a4BSdata>>8) & 0xFF;
    radioUserData()[3] = a4BSdata & 0xFF;
  }
}


// 4BS teach-in telegram
//
//       D[0]      |       D[1]      |       D[2]      |              D[3]
// 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 |  7   6   5   4   3   2   1   0
//
// f f f f f f t t   t t t t t m m m   m m m m m m m m   LRN EEP LRN LRN LRN  x   x   x
//    FUNC    |     TYPE      |      MANUFACTURER      | typ res res sta bit


void Esp3Packet::set4BSTeachInEEP(EnoceanProfile aEEProfile, EnoceanManufacturer aManufacturer)
{
  if (eepRorg()==rorg_4BS && EEP_RORG(aEEProfile)==rorg_4BS) {
    radioUserData()[0] =
      ((aEEProfile>>6) & 0xFC) | // 6 FUNC bits
      ((aEEProfile>>5) & 0x03); // upper 2 TYPE bits
    radioUserData()[1] =
      ((aEEProfile<<3) & 0xF8) | // lower 5 TYPE bits
      ((aManufacturer>>8) & 0x07); // upper 3 manufacturer bits
    radioUserData()[2] =
      (aManufacturer & 0xFF); // lower 8 manufacturer bits
  }
}


// MARK: - packet Factory methods


Esp3PacketPtr Esp3Packet::newEsp3Message(PacketType aPacketType, uint8_t aCode, uint8_t aNumParamBytes, uint8_t *aParamBytesInitializerP)
{
  Esp3PacketPtr cmdPacket = Esp3PacketPtr(new Esp3Packet);
  cmdPacket->setPacketType(aPacketType);
  // command data is command byte plus params (if any)
  cmdPacket->setDataLength(1+aNumParamBytes); // command code + parameters
  // set the first byte (command, event, response code)
  cmdPacket->data()[0] = aCode;
  for (int i=0; i<aNumParamBytes; i++) {
    cmdPacket->data()[1+i] = aParamBytesInitializerP ? aParamBytesInitializerP[i] : 0; // copy byte from initializer or zero it
  }
  return cmdPacket;
}



// MARK: - Description

static const int numRespCodes = 5;
static const char *respCodeNames[numRespCodes] = {
  "OK",
  "ERROR",
  "NOT SUPPORTED",
  "WRONG PARAM",
  "OPERATION DENIED"
};


string Esp3Packet::description()
{
  if (isComplete()) {
    string t;
    if (packetType()==pt_radio_erp1) {
      // ESP3 radio packet
      t = string_format(
        "ESP3 RADIO rorg=0x%02X,  sender=0x%08X, status=0x%02X\n"
        "- subtelegrams=%d, destination=0x%08X, dBm=%d, repeated=%d, secLevel=%d",
        eepRorg(),
        radioSender(),
        radioStatus(),
        radioSubtelegrams(),
        radioDestination(),
        radioDBm(),
        radioRepeaterCount(),
        radioSecurityLevel()
      );
      // EEP info if any
      if (radioHasTeachInfo()) {
        const char *mn = EnoceanComm::manufacturerName(eepManufacturer());
        string_format_append(t,
          "\n- Is Learn-In packet: EEP RORG/FUNC/TYPE: %02X %02X %02X, Manufacturer = %s (%03X)",
          EEP_RORG(eepProfile()),
          EEP_FUNC(eepProfile()),
          EEP_TYPE(eepProfile()),
          mn ? mn : "<unknown>",
          eepManufacturer()
        );
      }
    }
    else if (packetType()==pt_response) {
      // ESP3 response packet
      uint8_t sta = data()[0];
      t = string_format("ESP3 response packet, return code = %d (%s)", sta, sta<numRespCodes ? respCodeNames[sta] : "<unknown>");
    }
    else if (packetType()==pt_common_cmd) {
      // ESP3 common command packet
      t = string_format("ESP3 common command (%d)", data()[0]);
    }
    else if (packetType()==pt_smart_ack_command) {
      // ESP3 smart ack command
      t = string_format("ESP3 SmartAck command (%d)", data()[0]);
    }
    else if (packetType()==pt_event_message) {
      // ESP3 event packet
      t = string_format("ESP3 event message (%d)", data()[0]);
    }
    else {
      // other, unknown packet type
      t = string_format("Unknown ESP3 packet type (%d)", packetType());
    }
    // raw data
    string_format_append(t, "\n- %3zu data bytes: ", dataLength());
    for (int i=0; i<dataLength(); i++)
      string_format_append(t, "%02X ", data()[i]);
    if (packetType()==pt_radio_erp1) {
      string_format_append(t, "\n- %3zu opt  bytes: ", optDataLength());
      for (int i=0; i<optDataLength(); i++)
        string_format_append(t, "%02X ", optData()[i]);
    }
    return t;
  }
  else {
    return string_format("\nIncomplete ESP3 packet in state = %d", (int)state);
  }
}


// MARK: - CRC8 calculation

static u_int8_t CRC8Table[256] = {
  0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15,
  0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d,
  0x70, 0x77, 0x7e, 0x79, 0x6c, 0x6b, 0x62, 0x65,
  0x48, 0x4f, 0x46, 0x41, 0x54, 0x53, 0x5a, 0x5d,
  0xe0, 0xe7, 0xee, 0xe9, 0xfc, 0xfb, 0xf2, 0xf5,
  0xd8, 0xdf, 0xd6, 0xd1, 0xc4, 0xc3, 0xca, 0xcd,
  0x90, 0x97, 0x9e, 0x99, 0x8c, 0x8b, 0x82, 0x85,
  0xa8, 0xaf, 0xa6, 0xa1, 0xb4, 0xb3, 0xba, 0xbd,
  0xc7, 0xc0, 0xc9, 0xce, 0xdb, 0xdc, 0xd5, 0xd2,
  0xff, 0xf8, 0xf1, 0xf6, 0xe3, 0xe4, 0xed, 0xea,
  0xb7, 0xb0, 0xb9, 0xbe, 0xab, 0xac, 0xa5, 0xa2,
  0x8f, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9d, 0x9a,
  0x27, 0x20, 0x29, 0x2e, 0x3b, 0x3c, 0x35, 0x32,
  0x1f, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0d, 0x0a,
  0x57, 0x50, 0x59, 0x5e, 0x4b, 0x4c, 0x45, 0x42,
  0x6f, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7d, 0x7a,
  0x89, 0x8e, 0x87, 0x80, 0x95, 0x92, 0x9b, 0x9c,
  0xb1, 0xb6, 0xbf, 0xb8, 0xad, 0xaa, 0xa3, 0xa4,
  0xf9, 0xfe, 0xf7, 0xf0, 0xe5, 0xe2, 0xeb, 0xec,
  0xc1, 0xc6, 0xcf, 0xc8, 0xdd, 0xda, 0xd3, 0xd4,
  0x69, 0x6e, 0x67, 0x60, 0x75, 0x72, 0x7b, 0x7c,
  0x51, 0x56, 0x5f, 0x58, 0x4d, 0x4a, 0x43, 0x44,
  0x19, 0x1e, 0x17, 0x10, 0x05, 0x02, 0x0b, 0x0c,
  0x21, 0x26, 0x2f, 0x28, 0x3d, 0x3a, 0x33, 0x34,
  0x4e, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5c, 0x5b,
  0x76, 0x71, 0x78, 0x7f, 0x6A, 0x6d, 0x64, 0x63,
  0x3e, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2c, 0x2b,
  0x06, 0x01, 0x08, 0x0f, 0x1a, 0x1d, 0x14, 0x13,
  0xae, 0xa9, 0xa0, 0xa7, 0xb2, 0xb5, 0xbc, 0xbb,
  0x96, 0x91, 0x98, 0x9f, 0x8a, 0x8D, 0x84, 0x83,
  0xde, 0xd9, 0xd0, 0xd7, 0xc2, 0xc5, 0xcc, 0xcb,
  0xe6, 0xe1, 0xe8, 0xef, 0xfa, 0xfd, 0xf4, 0xf3
};


uint8_t Esp3Packet::addToCrc8(uint8_t aByte, uint8_t aCRCValue)
{
  return CRC8Table[aCRCValue ^ aByte];
}


uint8_t Esp3Packet::crc8(uint8_t *aDataP, size_t aNumBytes, uint8_t aCRCValue)
{
  int i;
  for (i = 0; i<aNumBytes; i++) {
    aCRCValue = addToCrc8(aCRCValue, aDataP[i]);
  }
  return aCRCValue;
}



// MARK: - Manufacturer names

typedef struct {
  EnoceanManufacturer manufacturerID;
  const char *name;
} EnoceanManufacturerDesc;


static const EnoceanManufacturerDesc manufacturerDescriptions[] = {
  { 0x000, "Manufacturer Reserved" },
  { 0x001, "Peha" },
  { 0x002, "Thermokon" },
  { 0x003, "Servodan" },
  { 0x004, "EchoFlex Solutions" },
  { 0x005, "Omnio / AWAG" },
  { 0x006, "Hardmeier electronics" },
  { 0x007, "Regulvar" },
  { 0x008, "Ad Hoc Electronics" },
  { 0x009, "Distech Controls" },
  { 0x00A, "Kieback + Peter" },
  { 0x00B, "EnOcean" },
  { 0x00C, "Probare / Vicos" },
  { 0x00D, "Eltako" },
  { 0x00E, "Leviton" },
  { 0x00F, "Honeywell" },
  { 0x010, "Spartan Peripheral Devices" },
  { 0x011, "Siemens" },
  { 0x012, "T-Mac" },
  { 0x013, "Reliable Controls" },
  { 0x014, "Elsner Elektronik" },
  { 0x015, "Diehl Controls" },
  { 0x016, "BSC Computer" },
  { 0x017, "S+S Regeltechnik" },
  { 0x018, "Masco / Zeno" },
  { 0x019, "Intesis Software" },
  { 0x01A, "Viessmann" },
  { 0x01B, "Lutuo Technology" },
  { 0x01C, "CAN2GO" },
  { 0x01D, "Sauter" },
  { 0x01E, "Boot-Up"  },
  { 0x01F, "Osram Sylvania"  },
  { 0x020, "Unotech"  },
  { 0x021, "Delta Controls"  },
  { 0x022, "Unitronic" },
  { 0x023, "NanoSense" },
  { 0x024, "The S4 Ggroup" },
  { 0x025, "MSR Solutions / Veissmann" },
  { 0x026, "GE" },
  { 0x027, "Maico" },
  { 0x028, "Ruskin" },
  { 0x029, "Magnum Energy Solutions" },
  { 0x02A, "KM Controls" },
  { 0x02B, "Ecologix Controls" },
  { 0x02C, "Trio 2 Sys" },
  { 0x02D, "Afriso Euro Index" },
  { 0x02E, "Waldmann" },
  // { 0x02F, "not assigmed" },
  { 0x030, "NEC Platforms" },
  { 0x031, "ITEC Corporation" },
  { 0X032, "Simicx" },
  { 0X033, "Permundo" },
  { 0X034, "Eurotronic Technology" },
  { 0X035, "Art Japan" },
  { 0X036, "Tiansu Automation Control System" },
  { 0X037, "Weinzierl Engineering" },
  { 0X038, "Gruppo Giordano Idea Spa" },
  { 0X039, "Alphaeos" },
  { 0X03A, "Tag Technologies" },
  { 0X03B, "Wattstopper" },
  { 0X03C, "Pressac Communications" },
  { 0X03E, "Giga Concept" },
  { 0X03F, "Sensortec" },
  { 0X040, "Jaeger Direkt" },
  { 0X041, "Air System Components" },
  { 0X042, "Ermine Corp" },
  { 0X043, "Soda" },
  { 0X044, "Eke Automation" },
  { 0X045, "Holter Regelarmaturen" },
  { 0X046, "Id Rf" },
  { 0X047, "Deuta Controls" },
  { 0X048, "Ewatch" },
  { 0X049, "Micropelt" },
  { 0X04A, "Caleffi Spa" },
  { 0X04B, "Digital Concepts" },
  { 0X04C, "Emerson Climate Technologies" },
  { 0X04D, "Adee Electronic" },
  { 0X04E, "Altecon" },
  { 0X04F, "Nanjing Putian Telecommunications" },
  { 0X050, "Terralux" },
  { 0X051, "Menred" },
  { 0X052, "Iexergy" },
  { 0X053, "Oventrop" },
  { 0X054, "Building Automation Products" },
  { 0X055, "Functional Devices" },
  { 0X056, "Ogga" },
  { 0X057, "Itho Daalderop" },
  { 0X058, "Resol" },
  { 0X059, "Advanced Devices" },
  { 0X05A, "Autani" },
  { 0X05B, "Dr Riegel" },
  { 0X05C, "Hoppe Holding" },
  { 0X05D, "Siegenia Aubi" },
  { 0X05E, "Adeo Services" },
  { 0x05F, "EiMSIG" },
  { 0x060, "Vimar Spa" },
  { 0x061, "Glen Dimlax" },
  { 0x062, "MinebeaMitsumi / PM DM" },
  { 0x063, "Hubbel_Lighting" },
  { 0x064, "Debflex" },
  { 0x065, "Perifactory Sensorsystems" },
  { 0X066, "Watty" },
  { 0X067, "Wago Kontakttechnik" },
  { 0X068, "Kessel" },
  { 0X069, "Aug Winkhaus" },
  { 0X06A, "Decelect" },
  { 0X06B, "MST Industries" },
  { 0X06C, "Becker Antriebe" },
  { 0X06D, "Nexelec" },
  { 0X06E, "Wieland Electric" },
  { 0X06F, "Avidsen" },
  { 0X070, "CWS Boco International" },
  { 0X071, "Roto Frank" },
  { 0X072, "ALM Controls" },
  { 0X073, "Tommaso Technologies" },
  { 0X074, "Rehau" },
  { 0x7FF, "Multi user Manufacturer ID" },
  { 0, NULL /* NULL string terminates list */ }
};



const char *EnoceanComm::manufacturerName(EnoceanManufacturer aManufacturerCode)
{
  const EnoceanManufacturerDesc *manP = manufacturerDescriptions;
  while (manP->name) {
    if (manP->manufacturerID==aManufacturerCode) {
      return manP->name;
    }
    manP++;
  }
  // none found
  return NULL;
}


// MARK: - EnOcean Security

#if ENABLE_ENOCEAN_SECURE

EnOceanSecurity::EnOceanSecurity() :
  securityLevelFormat(0),
  teachInInfo(0),
  rollingCounter(0),
  lastSavedRLC(0),
  lastSave(Never),
  rlcVerified(false),
  established(false),
  teachInP(NULL)
{
  memset(&privateKey, 0, AES128BlockLen);
  memset(&subKey1, 0, AES128BlockLen);
  memset(&subKey2, 0, AES128BlockLen);
}

EnOceanSecurity::~EnOceanSecurity()
{
  if (teachInP) {
    delete teachInP;
    teachInP = NULL;
  }
}


string EnOceanSecurity::logContextPrefix()
{
  return "EnOceanSecurity";
}


void EnOceanSecurity::deriveSubkeysFromPrivateKey()
{
  deriveSubkeys(privateKey, subKey1, subKey2);
}


uint8_t EnOceanSecurity::rlcSize()
{
  uint8_t rlcAlgo = (securityLevelFormat>>6) & 0x03;
  uint8_t rlcBytes = 0;
  if (rlcAlgo==1) rlcBytes = 2; // 16 bit RLC
  else if (rlcAlgo==2) rlcBytes = 3; // 24 bit RLC
  return rlcBytes;
}


void EnOceanSecurity::incrementRlc(int32_t aIncrement)
{
  rollingCounter += (uint32_t)aIncrement;
  // mask
  rollingCounter &= (0xFFFFFFFF>>((4-rlcSize())*8));
}


uint32_t EnOceanSecurity::rlcDistance(uint32_t aNewRLC, uint32_t aOldRLC)
{
  return (aNewRLC-aOldRLC) & (0xFFFFFFFF>>((4-rlcSize())*8));
}


bool EnOceanSecurity::rlcInWindow(uint32_t aRLC)
{
  return rlcDistance(aRLC, rollingCounter)<=RLC_WINDOW_SIZE;
}



uint8_t EnOceanSecurity::macSize()
{
  uint8_t macAlgo = (securityLevelFormat>>3) & 0x03;
  uint8_t macBytes = 0;
  if (macAlgo==1) macBytes = 3; // 24 bit MAC
  else if (macAlgo==2) macBytes = 4; // 32 bit MAC
  return macBytes;
}


#define KEY_BYTES_IN_SEGMENT0 4

Esp3PacketPtr EnOceanSecurity::teachInMessage(int aSegment)
{
  Esp3PacketPtr tim = Esp3PacketPtr(new Esp3Packet);
  if (aSegment==0) {
    int rlcSz = rlcSize();
    tim->initForRorg(rorg_SEC_TEACHIN, 2+rlcSz+KEY_BYTES_IN_SEGMENT0);
    uint8_t *d = tim->radioUserData();
    int i=0;
    // R-ORG TS | TEACH_IN_INFO | SLF | RLC | KEY (KEY_BYTES_IN_SEGMENT0 bytes)
    d[i++] = teachInInfo;
    d[i++] = securityLevelFormat;
    while (rlcSz>0) {
      rlcSz--;
      d[i++] = (rollingCounter>>(8*rlcSz))&0xFF;
    }
    // KEY_BYTES_IN_SEGMENT0 bytes of key
    for (int j=0; j<KEY_BYTES_IN_SEGMENT0; j++) {
      d[i++] = privateKey[j];
    }
  }
  else if (aSegment==1) {
    tim->initForRorg(rorg_SEC_TEACHIN, 1+AES128BlockLen-KEY_BYTES_IN_SEGMENT0);
    uint8_t *d = tim->radioUserData();
    int i=0;
    // R-ORG TS | TEACH_IN_INFO | KEY (rest of bytes)
    d[i++] = 0x40; // TEACH_IN_INFO for segment 1
    // rest of key
    for (int j=KEY_BYTES_IN_SEGMENT0; j<AES128BlockLen; j++) {
      d[i++] = privateKey[j];
    }
  }
  else {
    return Esp3PacketPtr(); // no other segments
  }
  // ready
  return tim;
}


Tristate EnOceanSecurity::processTeachInMsg(Esp3PacketPtr aTeachInMsg, AES128Block *aPskP, bool aLearning)
{
  if (aTeachInMsg->eepRorg()!=rorg_SEC_TEACHIN) return no; // not a secure teach-in message
  // R-ORG TS | TEACH_IN_INFO | SLF | RLC | KEY
  size_t l = aTeachInMsg->radioUserDataLength(); // length w/o R-ORG-TS
  uint8_t *dataP = aTeachInMsg->radioUserData();
  if (l<2) return no; // invalid message: need at least TEACH_IN_INFO and one byte of other info (SLF or message continuation)
  // - first byte is always TEACH_IN_INFO
  uint8_t ti = *dataP++; l--;
  uint8_t sidx = (ti>>6) & 0x03; // IDX
  if (sidx==0) {
    // IDX=0, new teach-in begins
    if (teachInP) delete teachInP;
    teachInP = new SecureTeachInData;
    teachInInfo = ti;
    teachInP->numTeachInBytes = 0;
    teachInP->segmentIndex = 0;
  }
  else {
    // IDX>0, is an additional segment
    if (!teachInP || sidx-teachInP->segmentIndex!=1) {
      // not started teach-in or segment out of order
      return no;
    }
    teachInP->segmentIndex = sidx;
  }
  // accumulate bytes
  while (l>0) {
    if (teachInP->numTeachInBytes>=maxTeachInDataSize) return no; // too much teach-in data
    teachInP->teachInData[teachInP->numTeachInBytes++] = *dataP++; l--;
  }
  // check if all segments received
  uint8_t numSegments = (teachInInfo>>4) & 0x03;
  //DBGOLOG(LOG_INFO, "%08X: teach-in segment %d/%d received", aTeachInMsg->radioSender(), teachInP->segmentIndex+1, numSegments);
  if (teachInP->segmentIndex+1>=numSegments) {
    // if learning, this replaces earlier established info
    if (aLearning) established = false;
    // all teach-in data accumulated
    // Note: if this is already an established security info at this point
    //       only RLC can be updated, if and only if private key matches
    int b = teachInP->numTeachInBytes;
    int idx = 0; // start with SLF
    // - get SLF
    if (!established) {
      securityLevelFormat = teachInP->teachInData[idx];
    }
    else if (securityLevelFormat!=teachInP->teachInData[idx]) {
      OLOG(LOG_WARNING, "%08X: RLC update attempt with non-matching security level -> ignored", aTeachInMsg->radioSender());
      return no; // not a valid security info update
    }
    idx++; b--;
    // - RLC and key
    if (teachInInfo & 0x08) {
      // teach-in data is encrypted by preshared key (PSK)
      if (!aPskP) return no; // we don't have a PSK, cannot decrypt
      uint8_t di[maxTeachInDataSize];
      memcpy(di, &teachInP->teachInData[idx], b); // copy encrypted version
      VAEScrypt(*aPskP, 0x0000, 2, di, &teachInP->teachInData[idx], b); // decrypt
    }
    // - RLC if set
    uint32_t newRollingCounter = 0; // init with zero
    for (int i=0; i<rlcSize(); i++) {
      if (b<=0) return no; // not enough bytes
      newRollingCounter = (newRollingCounter<<8) + teachInP->teachInData[idx++]; b--;
    }
    // - private key
    for (int i=0; i<AES128BlockLen; i++) {
      if (b<=0) return no; // not enough bytes
      if (!established) {
        privateKey[i] = teachInP->teachInData[idx];
      }
      else {
        if (privateKey[i]!=teachInP->teachInData[idx]) {
          OLOG(LOG_ERR, "%08X: RLC update attempt with wrong private key -> ignored", aTeachInMsg->radioSender());
          return no; // not a valid security info update -> abort
        }
      }
      idx++; b--;
    }
    if (!established) {
      // - now established
      established = true;
      // - store RLC
      rollingCounter = newRollingCounter;
      // - derive subkeys
      deriveSubkeysFromPrivateKey();
    }
    else if (rlcSize()>0) {
      // was already established, only update RLC (matching key was checked above)
      rollingCounter = newRollingCounter;
    }
    // Security info is now complete
    delete teachInP; teachInP = NULL;
    return yes;
  }
  return undefined; // not yet complete
}


// D2-03-00 pseudo-profile data mapping to RPS data/status

static uint16_t ptmMapping[16] = {
  // format is 0xDDSS (DD=data, SS=status)
  0, 0, 0, 0, 0, // 0..4 are undefined
  0x1730, // 5: A1+B0 pressed
  0x7020, // 6: 3 or 4 buttony pressed
  0x3730, // 7: A0+B0 pressed
  0x1020, // 8: no buttons pressed but energy bow pressed
  0x1530, // 9: A1+B1 pressed
  0x3530, // 10: A0+B1 pressed
  0x5030, // 11: B1 pressed
  0x7030, // 12: B0 pressed
  0x1030, // 13: A1 pressed
  0x3030, // 14: A0 pressed
  0x0020  // 15: released
};


Esp3PacketPtr EnOceanSecurity::unpackSecureMessage(Esp3PacketPtr aSecureMsg)
{
  RadioOrg org = aSecureMsg->eepRorg();
  if (org!=rorg_SEC_ENCAPS && org!=rorg_SEC) {
    OLOG(LOG_WARNING, "%08X: Non-secure radio packet, but device is secure -> ignored", aSecureMsg->radioSender());
    return Esp3PacketPtr(); // none
  }
  if (!established) {
    OLOG(LOG_NOTICE, "%08X: Incomplete security info -> packet ignored", aSecureMsg->radioSender());
    return Esp3PacketPtr(); // none
  }
  // something to decrypt
  size_t n = aSecureMsg->radioUserDataLength();
  uint8_t *d = aSecureMsg->radioUserData();
  // check for CMAC
  uint32_t cmac_sent = 0;
  uint8_t macsz = macSize();
  if (macsz>0) {
    // there is a MAC
    if (macsz>n) {
      return Esp3PacketPtr(); // not enough data
    }
    n -= macsz;
    for (int i=0; i<macsz; i++) {
      cmac_sent = (cmac_sent<<8) | d[n+i];
    }
  }
  // check for transmitted RLC
  uint8_t rlcsz = rlcSize();
  bool transmittedRlc = securityLevelFormat & 0x20;
  if (transmittedRlc) {
    uint32_t rlc = 0;
    // RLC_TX set -> RLC is in the message
    if (rlcsz>n) {
      return Esp3PacketPtr(); // not enough data
    }
    n -= rlcsz;
    for (int i=0; i<rlcsz; i++) {
      rlc = (rlc<<8) | d[n+i];
    }
    // transmitted RLC must be higher than last known
    if (!rlcInWindow(rlc)) {
      LOG(LOG_NOTICE, "%08X: Transmitted RLC is not within allowed window of %d", aSecureMsg->radioSender(), RLC_WINDOW_SIZE);
      return Esp3PacketPtr(); // invalid CMAC
    }
    // update RLC
    rollingCounter = rlc;
  }
  // verify CMAC
  if (macsz) {
    int rlcRetries = 0;
    uint32_t origRLC = rollingCounter;
    // Note: allow for more retries when we might have lost RLC increments because of lazy persistence
    int maxRetries = rlcVerified ? RLC_WINDOW_SIZE : RLC_WINDOW_SIZE+MIN_RLC_DISTANCE_FOR_SAVE;
    while(true) {
      if (rlcRetries>=maxRetries) {
        OLOG(LOG_NOTICE, "%08X: No matching CMAC %X found within window of current RLC + %d", aSecureMsg->radioSender(), cmac_sent, maxRetries);
        rollingCounter = origRLC; // do not change RLC
        return Esp3PacketPtr(); // invalid CMAC
      }
      // calc CMAC
      uint32_t cmac_calc = calcCMAC(privateKey, subKey1, subKey2, rollingCounter, rlcsz, macsz, org, d, n);
      if (cmac_calc==cmac_sent) {
        // CMAC matches
        rlcVerified = true; // this RLC matches
        if (rlcRetries>0) {
          OLOG(LOG_NOTICE, "%08X: RLC increment of %d required to match CMAC %X (indicates missing packets)", aSecureMsg->radioSender(), rlcRetries, cmac_sent);
        }
        break;
      }
      // no match
      if (transmittedRlc) {
        OLOG(LOG_NOTICE, "%08X: No CMAC %X match with transmitted RLC %X", aSecureMsg->radioSender(), cmac_sent, rollingCounter);
        return Esp3PacketPtr(); // invalid CMAC
      }
      OLOG(LOG_DEBUG, "- No matching CMAC %X for current RLC, check next RLC in window", cmac_sent);
      incrementRlc();
      rlcRetries++;
    }
  }
  // check decryption: n bytes at d
  if (n==0) {
    OLOG(LOG_INFO, "%08X: packet has no payload", aSecureMsg->radioSender());
    return Esp3PacketPtr(); // invalid CMAC
  }
  uint8_t encMode = securityLevelFormat & 0x07;
  Esp3PacketPtr outMsg = Esp3PacketPtr(new Esp3Packet);
  uint8_t *outd = new uint8_t[n];
  if (encMode==0) {
    // plain data, just copy
    memcpy(outd, d, n);
  }
  else if (encMode==3) {
    VAEScrypt(privateKey, rollingCounter, rlcsz, d, outd, n);
  }
  else {
    // TODO: support other modes
    OLOG(LOG_WARNING, "%08X: encrypted radio package with unsupported encryption mode %d", aSecureMsg->radioSender(), encMode);
  }
  d = outd;
  // - now that we have decoded the payload: increment RLC for next packet
  incrementRlc();
  // - set radio org and data
  if (org==rorg_SEC_ENCAPS) {
    // use encapsulated org and 1:1 data
    outMsg->initForRorg((RadioOrg)d[0], n-1);
    d++; n--;
    if (n>outMsg->radioUserDataLength())
      n = outMsg->radioUserDataLength();
    for (int i=0; i<n; i++) {
      outMsg->radioUserData()[i] = *d++;
    }
    outMsg->setRadioStatus(aSecureMsg->radioStatus());
  }
  else {
    // must be implicit D2-03-00 PTM - map it to F6-02-01
    outMsg->initForRorg(rorg_RPS, 0);
    uint16_t ptmData = ptmMapping[*d & 0x0F];
    // - set data
    outMsg->radioUserData()[0] = (ptmData>>8) & 0xFF;
    // - set status
    outMsg->setRadioStatus(ptmData&0xFF);
  }
  // - copy sender
  outMsg->setRadioSender(aSecureMsg->radioSender());
  // - copy optdata (7 bytes)
  memcpy(outMsg->optData(), aSecureMsg->optData(), 7);
  // - update security level
  outMsg->setRadioSecurityLevel(1 + (macsz ? 2 : 0) + (encMode ? 1 : 0)); // 2=decrypted, 3=authenticated, 4=both
  // done, return the decrypted message
  delete[] outd;
  outMsg->finalize();
  return outMsg;
}



bool EnOceanSecurity::AES128(const AES128Block &aKey, const AES128Block &aData, AES128Block &aAES128)
{
  // single block AES128 (aes-128-ecb, "electronic code book")
  EVP_CIPHER_CTX* ctxP = EVP_CIPHER_CTX_new();
  if (ctxP) {
    if (EVP_EncryptInit_ex (ctxP, EVP_aes_128_ecb(), NULL, aKey, NULL)) {
      EVP_CIPHER_CTX_set_padding(ctxP, false); // no padding
      uint8_t *pointer = aAES128;
      int outlen;
      if (EVP_EncryptUpdate(ctxP, pointer, &outlen, aData, AES128BlockLen)) {
        pointer += outlen;
        if (EVP_EncryptFinal_ex(ctxP, pointer, &outlen)) {
          EVP_CIPHER_CTX_free(ctxP);
          return true;
        }
        else {
          DBGLOG(LOG_ERR, "EVP_EncryptFinal_ex failed");
        }
      }
      else {
        DBGLOG(LOG_ERR, "EVP_EncryptUpdate failed");
      }
    }
    else {
      DBGLOG(LOG_ERR, "EVP_EncryptInit_ex failed");
    }
    EVP_CIPHER_CTX_free(ctxP);
  }
  return false;
}


void EnOceanSecurity::VAEScrypt(const AES128Block &aKey, uint32_t aRLC, int aRLCSize, const uint8_t *aDataIn, uint8_t *aDataOut, size_t aDataSize)
{
  // VAES
  // - fixed publickey
  static const AES128Block publicKey = { 0x34, 0x10, 0xde, 0x8f, 0x1a, 0xba, 0x3e, 0xff, 0x9f, 0x5a, 0x11, 0x71, 0x72, 0xea, 0xca, 0xbd };
  // - start chain with zero crypt key
  AES128Block cryptKey;
  memset(&cryptKey, 0, AES128BlockLen);
  // - for every block
  while (aDataSize>0) {
    AES128Block aesInp;
    // AES input: publickey XOR rlc XOR last block's cryptKey
    int rlcbytes = aRLCSize;
    for (int i=0; i<AES128BlockLen; i++) {
      aesInp[i] = publicKey[i] ^ cryptKey[i]; // public key XOR last block's cryptKey
      if (--rlcbytes >= 0) {
        aesInp[i] ^= ((aRLC>>(rlcbytes*8))&0xFF); // .. XOR rlc
      }
    }
    // calculate (en/de)crypt key for next block
    AES128(aKey, aesInp, cryptKey);
    // actually en/decrypt now
    for (int i=0; i<AES128BlockLen; i++) {
      *aDataOut++ = cryptKey[i] ^ *aDataIn++;
      // count and end en/decryption
      if (--aDataSize==0) break;
    }
  }
}


void EnOceanSecurity::deriveSubkeys(const AES128Block &aKey, AES128Block &aSubkey1, AES128Block &aSubkey2)
{
  AES128Block zero;
  memset(&zero, 0, AES128BlockLen);
  AES128Block L;
  AES128(aKey, zero, L);
  // Subkey K1
  for (int i=0; i<AES128BlockLen; i++) {
    aSubkey1[i] = ((L[i]<<1)+(i<AES128BlockLen-1 && ((L[i+1]&0x80)!=0 ? 0x01 : 0)))&0xFF;
  }
  if ((L[0] & 0x80) != 0) aSubkey1[AES128BlockLen-1] ^= 0x87; // const_Rb
  // Subkey K2
  for (int i=0; i<AES128BlockLen; i++) {
    aSubkey2[i] = ((aSubkey1[i]<<1)+(i<AES128BlockLen-1 && ((aSubkey1[i+1]&0x80)!=0 ? 0x01 : 0)))&0xFF;
  }
  if ((aSubkey1[0] & 0x80) != 0) aSubkey2[AES128BlockLen-1] ^= 0x87; // const_Rb
}


uint32_t EnOceanSecurity::calcCMAC(const AES128Block &aKey, const AES128Block &aSubKey1, const AES128Block &aSubKey2, uint32_t aRLC, int aRLCBytes, int aMACBytes, uint8_t aFirstByte, const uint8_t *aData, size_t aDataSize)
{
  uint8_t db;
  // check for extra first byte (in addition to aData)
  if (aFirstByte) {
    // use given first byte
    db = aFirstByte;
    aDataSize++;
  }
  else {
    // fetch first byte from data
    db = *aData++;
  }
  // include RLC in aDataSize
  aDataSize += aRLCBytes;
  // aDataSize is now overall data size to process in CMAC, including optional extra first byte and including RLC
  AES128Block aesInp;
  AES128Block resBlock;
  memset(&resBlock, 0, AES128BlockLen);
  bool padded = false;
  while (aDataSize>0) {
    // AES input is result of previous block XOR data
    for (int i=0; i<AES128BlockLen; i++) {
      if (aDataSize>0) {
        // we still have data
        aesInp[i] = resBlock[i] ^ db;
        // get next byte
        aDataSize--; // processed this byte
        if (aDataSize>aRLCBytes) {
          // real data
          db = *aData++;
        }
        else if (aDataSize>0){
          // rlc
          db = ((aRLC>>((aDataSize-1)*8))&0xFF);
        }
      }
      else {
        // no more data, pad data with 0b1000...000
        aesInp[i] = resBlock[i]; // still include the result from previous block
        if (!padded) aesInp[i] ^= 0x80; // first padding byte, use 0x80 instead of 0x00 (which means NOP here)
        padded = true; // now we have started padding
      }
    }
    // now we have a full AES block
    // - if this is the last block, we need to add the subkey now
    if (aDataSize==0) {
      for (int i=0; i<AES128BlockLen; i++) {
        aesInp[i] ^= padded ? aSubKey2[i] : aSubKey1[i];
      }
    }
    // - do the AES now
    AES128(aKey, aesInp, resBlock);
  }
  // now resBlock contains the CMAC
  uint32_t cmac = 0;
  for (int i=0; i<aMACBytes; i++) {
    cmac <<= 8;
    cmac |= (uint8_t)resBlock[i]&0xFF;
  }
  return cmac;
}


#endif // ENABLE_ENOCEAN_SECURE



// MARK: - EnOcean communication handler

// baudrate for ESP3 on TCM310
#define ENOCEAN_ESP3_COMMAPARMS "57600,8,N,1"

#define ENOCEAN_ESP3_ALIVECHECK_INTERVAL (30*Second)
#define ENOCEAN_ESP3_ALIVECHECK_TIMEOUT (3*Second)

#define ENOCEAN_ESP3_COMMAND_TIMEOUT (3*Second)

#define ENOCEAN_INIT_RETRIES 5
#define ENOCEAN_INIT_RETRY_INTERVAL (5*Second)



EnoceanComm::EnoceanComm(MainLoop &aMainLoop) :
	inherited(aMainLoop),
  apiVersion(0),
  appVersion(0),
  myAddress(0),
  myIdBase(0)
{
}


EnoceanComm::~EnoceanComm()
{
}


string EnoceanComm::logContextPrefix()
{
  return "EnOcean";
}



void EnoceanComm::setConnectionSpecification(const char *aConnectionSpec, uint16_t aDefaultPort, const char *aEnoceanResetPinName)
{
  OLOG(LOG_DEBUG, "setConnectionSpecification: %s", aConnectionSpec);
  serialComm->setConnectionSpecification(aConnectionSpec, aDefaultPort, ENOCEAN_ESP3_COMMAPARMS);
  // create the EnOcean reset IO pin
  if (aEnoceanResetPinName) {
    // init, initially inactive = not reset
    enoceanResetPin = DigitalIoPtr(new DigitalIo(aEnoceanResetPinName, true, false));
  }
	// open connection so we can receive
	serialComm->requestConnection();
}


void EnoceanComm::initialize(StatusCB aCompletedCB)
{
  // start initializing
  initializeInternal(aCompletedCB, ENOCEAN_INIT_RETRIES);
}


void EnoceanComm::initializeInternal(StatusCB aCompletedCB, int aRetriesLeft)
{
  // get version
  serialComm->requestConnection();
  sendCommand(Esp3Packet::newEsp3Message(pt_common_cmd, CO_RD_VERSION), boost::bind(&EnoceanComm::versionReceived, this, aCompletedCB, aRetriesLeft, _1, _2));
}


void EnoceanComm::initError(StatusCB aCompletedCB, int aRetriesLeft, ErrorPtr aError)
{
  // error querying version
  aRetriesLeft--;
  if (aRetriesLeft>=0) {
    OLOG(LOG_WARNING, "Initialisation: command failed: %s -> retrying again", aError->text());
    // flush the line on the first half of attempts
    if (aRetriesLeft>ENOCEAN_INIT_RETRIES/2) {
      flushLine();
    }
    serialComm->closeConnection();
    // retry initializing later
    aliveCheckTicket.executeOnce(boost::bind(&EnoceanComm::initializeInternal, this, aCompletedCB, aRetriesLeft), ENOCEAN_INIT_RETRY_INTERVAL);
  }
  else {
    // no more retries, just return
    OLOG(LOG_ERR, "Initialisation: %d attempts failed to send commands -> initialisation failed", ENOCEAN_INIT_RETRIES);
    if (aCompletedCB) aCompletedCB(aError);
  }
  return; // done
}


void EnoceanComm::versionReceived(StatusCB aCompletedCB, int aRetriesLeft, Esp3PacketPtr aEsp3PacketPtr, ErrorPtr aError)
{
  // extract versions
  if (Error::isOK(aError)) {
    uint8_t *d = aEsp3PacketPtr->data();
    appVersion = (d[1]<<24)+(d[2]<<16)+(d[3]<<8)+d[4];
    apiVersion = (d[5]<<24)+(d[6]<<16)+(d[7]<<8)+d[8];
    myAddress = (d[9]<<24)+(d[10]<<16)+(d[11]<<8)+d[12];
    OLOG(LOG_INFO, "Modem info (CO_RD_VERSION): appVersion=0x%08X, apiVersion=0x%08X, modemAddress=0x%08X", appVersion, apiVersion, myAddress);
  }
  else {
    initError(aCompletedCB, aRetriesLeft, aError);
    return;
  }
  // query base ID
  sendCommand(Esp3Packet::newEsp3Message(pt_common_cmd, CO_RD_IDBASE), boost::bind(&EnoceanComm::idbaseReceived, this, aCompletedCB, aRetriesLeft, _1, _2));
}


void EnoceanComm::idbaseReceived(StatusCB aCompletedCB, int aRetriesLeft, Esp3PacketPtr aEsp3PacketPtr, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    uint8_t *d = aEsp3PacketPtr->data();
    myIdBase = (d[1]<<24)+(d[2]<<16)+(d[3]<<8)+d[4];
    OLOG(LOG_INFO, "Modem info (CO_RD_IDBASE): idBase=0x%08X", myIdBase);
  }
  else {
    initError(aCompletedCB, aRetriesLeft, aError);
    return;
  }
  // completed successfully
  if (aCompletedCB) aCompletedCB(aError);
  // schedule first alive check quickly
  aliveCheckTicket.executeOnce(boost::bind(&EnoceanComm::aliveCheck, this), 2*Second);
}



void EnoceanComm::aliveCheck()
{
  FOCUSOLOG("checking enocean module operation by sending CO_RD_VERSION command");
  // send a EPS3 command to the modem to check if it is alive
  Esp3PacketPtr checkPacket = Esp3Packet::newEsp3Message(pt_common_cmd, CO_RD_VERSION);
  // issue command
  sendCommand(checkPacket, boost::bind(&EnoceanComm::aliveCheckResponse, this, _1, _2));
}


void EnoceanComm::smartAckLearnMode(bool aEnabled, MLMicroSeconds aTimeout)
{
  OLOG(LOG_INFO, "%sabling smartAck learn mode in enocean module", aEnabled ? "en" : "dis");
  // send a EPS3 command to the modem to check if it is alive
  Esp3PacketPtr saPacket = Esp3Packet::newEsp3Message(pt_smart_ack_command, SA_WR_LEARNMODE, 6);
  // params
  saPacket->data()[1] = aEnabled ? 1 : 0;
  saPacket->data()[2] = 0; // simple learn mode
  uint32_t toMs = 0;
  if (aEnabled) toMs = (uint32_t)(aTimeout/MilliSecond);
  saPacket->data()[3] = (toMs>>24) & 0xFF;
  saPacket->data()[4] = (toMs>>16) & 0xFF;
  saPacket->data()[5] = (toMs>>8) & 0xFF;
  saPacket->data()[6] = toMs & 0xFF;
  // issue command
  sendCommand(saPacket, NoOP); // we don't need the response (but there is one)
}


void EnoceanComm::smartAckRespondToLearn(uint8_t aConfirmCode, MLMicroSeconds aResponseTime)
{
  OLOG(LOG_INFO, "responding to smartAck learn with code 0x%02X", aConfirmCode);
  // send a EPS3 command to the modem as response to SA_CONFIRM_LEARN
  Esp3PacketPtr respPacket = Esp3Packet::newEsp3Message(pt_response, RET_OK, 3);
  uint16_t respMs = 0;
  if (aConfirmCode==SA_RESPONSECODE_LEARNED) {
    // response time only if confirming successful learn-in
    respMs = (uint16_t)(aResponseTime/MilliSecond);
  }
  respPacket->data()[1] = (respMs>>8) & 0xFF;
  respPacket->data()[2] = respMs & 0xFF;
  respPacket->data()[3] = aConfirmCode;
  // Smartack response is immediate and does not respond back (not a regular "command")
  sendPacket(respPacket);
}


void EnoceanComm::confirmUTE(uint8_t aConfirmCode, Esp3PacketPtr aUTEPacket)
{
  uint8_t db6 = aUTEPacket->radioUserData()[0]; // DB6 (in EEP specs order)
  if ((db6 & 0x40)==0) {
    // UTE teach-in response expected
    Esp3PacketPtr uteRespPacket = Esp3PacketPtr(new Esp3Packet());
    uteRespPacket->initForRorg(rorg_UTE);
    // - is a echo of the request, except for first byte
    uteRespPacket->radioUserData()[0] =
      (db6 & 0x80) | // keep uni-/bidirectional bit
      (aConfirmCode<<4) | // response code
      0x01; // CMD EEP teach-in-response
    // - copy remaining bytes
    for (int i=1; i<7; i++) {
      uteRespPacket->radioUserData()[i] = aUTEPacket->radioUserData()[i];
    }
    // mirror back to sender
    uteRespPacket->setRadioDestination(aUTEPacket->radioSender());
    // now send
    OLOG(LOG_INFO, "Sending UTE teach-in response for EEP %06X", EEP_PURE(uteRespPacket->eepProfile()));
    sendCommand(uteRespPacket, NoOP);
  }
}




void EnoceanComm::aliveCheckResponse(Esp3PacketPtr aEsp3PacketPtr, ErrorPtr aError)
{
  if (Error::notOK(aError)) {
    // alive check failed, try to recover EnOcean interface
    OLOG(LOG_ERR, "alive check of EnOcean module failed -> restarting module");
    // - close the connection
    serialComm->closeConnection();
    // - do a hardware reset of the module if possible
    if (enoceanResetPin) enoceanResetPin->set(true); // reset
    // - using alive check ticket for reset sequence
    aliveCheckTicket.executeOnce(boost::bind(&EnoceanComm::resetDone, this), 2*Second);
  }
  else {
    // response received, should be answer to CO_RD_VERSION
    // check for version
    if (aEsp3PacketPtr->dataLength()!=33) {
      FOCUSOLOG("Alive check received packet after sending CO_RD_VERSION, but hat wrong data length (%zu instead of 33)", aEsp3PacketPtr->dataLength());
    }
    // also schedule the next alive check
    aliveCheckTicket.executeOnce(boost::bind(&EnoceanComm::aliveCheck, this), ENOCEAN_ESP3_ALIVECHECK_INTERVAL);
  }
}


void EnoceanComm::resetDone()
{
  OLOG(LOG_NOTICE, "releasing enocean reset");
  if (enoceanResetPin) enoceanResetPin->set(false); // release reset
  // wait a little, then re-open connection
  aliveCheckTicket.executeOnce(boost::bind(&EnoceanComm::reopenConnection, this), 2*Second);
}


void EnoceanComm::reopenConnection()
{
  OLOG(LOG_NOTICE, "re-opening connection");
	serialComm->requestConnection(); // re-open connection
  // restart alive checks, not too soon after reset
  aliveCheckTicket.executeOnce(boost::bind(&EnoceanComm::aliveCheck, this), 10*Second);
}


void EnoceanComm::setRadioPacketHandler(ESPPacketCB aRadioPacketCB)
{
  radioPacketHandler = aRadioPacketCB;
}


void EnoceanComm::setEventPacketHandler(ESPPacketCB aEventPacketCB)
{
  eventPacketHandler = aEventPacketCB;
}


size_t EnoceanComm::acceptBytes(size_t aNumBytes, uint8_t *aBytes)
{
  if (FOCUSLOGGING) {
    string d = string_format("accepting %zu bytes:", aNumBytes);
    for (size_t i = 0; i<aNumBytes; i++) {
      string_format_append(d, "%02X ", aBytes[i]);
    }
    FOCUSOLOG("%s",d.c_str());
  }
	size_t remainingBytes = aNumBytes;
	while (remainingBytes>0) {
		if (!currentIncomingPacket) {
			currentIncomingPacket = Esp3PacketPtr(new Esp3Packet);
		}
		// pass bytes to current telegram
		size_t consumedBytes = currentIncomingPacket->acceptBytes(remainingBytes, aBytes);
		if (currentIncomingPacket->isComplete()) {
      FOCUSOLOG("Received Packet:\n%s", currentIncomingPacket->description().c_str());
      dispatchPacket(currentIncomingPacket);
      // forget the packet, further incoming bytes will create new packet
			currentIncomingPacket = Esp3PacketPtr(); // forget
		}
		// continue with rest (if any)
		aBytes+=consumedBytes;
		remainingBytes-=consumedBytes;
	}
	return aNumBytes-remainingBytes;
}


void EnoceanComm::dispatchPacket(Esp3PacketPtr aPacket)
{
  // dispatch the packet
  PacketType pt = aPacket->packetType();
  if (pt==pt_radio_erp1) {
    // incoming radio packet
    if (radioPacketHandler) {
      // call the handler
      radioPacketHandler(aPacket, ErrorPtr());
    }
    else {
      OLOG(LOG_INFO, "Received radio packet, but no packet handler is installed -> ignored");
    }
  }
  else if (pt==pt_response) {
    // This is a command response
    // - stop timeout
    cmdTimeoutTicket.cancel();
    if (cmdQueue.empty() || cmdQueue.front().commandPacket) {
      // received unexpected answer
      OLOG(LOG_WARNING, "Received unexpected response packet of length %zu", aPacket->dataLength());
    }
    else {
      // must be response to first entry in queue
      // - deliver to waiting callback, if any
      ESPPacketCB callback = cmdQueue.front().responseCB;
      // - remove waiting marker from queue
      cmdQueue.pop_front();
      // - now call handler
      if (callback) {
        // pass packet and response status
        callback(aPacket, aPacket->responseStatus());
      }
    }
    // check if more commands in queue to be sent
    checkCmdQueue();
  }
  else if (pt==pt_event_message) {
    // This is a event
    if (eventPacketHandler) {
      // call the handler
      eventPacketHandler(aPacket, ErrorPtr());
    }
    else {
      OLOG(LOG_INFO, "Received event code %d, but no packet handler is installed -> ignored", aPacket->data()[0]);
    }
  }
  else {
    OLOG(LOG_INFO, "Received unknown packet type %d of length %zu", pt, aPacket->dataLength());
  }
}


void EnoceanComm::flushLine()
{
  ErrorPtr err;
  uint8_t zeroes[42];
  memset(zeroes, 0, sizeof(zeroes));
  serialComm->transmitBytes(sizeof(zeroes), zeroes, err);
  if (Error::notOK(err)) {
    OLOG(LOG_ERR, "flushLine: error sending flush bytes");
  }
}



void EnoceanComm::sendPacket(Esp3PacketPtr aPacket)
{
  // finalize, calc CRC
  aPacket->finalize();
  // transmit
  // - fixed header
  ErrorPtr err;
  serialComm->transmitBytes(ESP3_HEADERBYTES, aPacket->header, err);
  if (Error::isOK(err)) {
    // - payload
    serialComm->transmitBytes(aPacket->payloadSize, aPacket->payloadP, err);
  }
  if (Error::notOK(err)) {
    OLOG(LOG_ERR, "sendPacket: error sending packet over serial: %s", err->text());
  }
  else {
    FOCUSOLOG("Sent packet:\n%s", aPacket->description().c_str());
  }
}


EnoceanAddress EnoceanComm::makeSendAddress(EnoceanAddress aSendAddr)
{
  // Note: For migrated settings cases, addr might contain a base address different from this modem's (that of the original EnOcean modem).
  //   To facilitate migration (keeping the devices with current dSUIDs, derived from the original modem's base address),
  //   we ignore the base address in addr and always use the actual base address of this modem
  //   (otherwise the modem will not send any data at all).
  aSendAddr &= 0x7F; // only keep the offset to the base address
  aSendAddr += idBase(); // add-in the actual modem base address
  return aSendAddr;
}


void EnoceanComm::sendCommand(Esp3PacketPtr aCommandPacket, ESPPacketCB aResponsePacketCB)
{
  // queue command
  EnoceanCmd cmd;
  aCommandPacket->finalize();
  FOCUSOLOG("Queueing command packet to send: \n%s", aCommandPacket->description().c_str());
  cmd.commandPacket = aCommandPacket;
  cmd.responseCB = aResponsePacketCB;
  cmdQueue.push_back(cmd);
  checkCmdQueue();
}


void EnoceanComm::checkCmdQueue()
{
  if (cmdQueue.empty()) return; // queue empty
  EnoceanCmd cmd = cmdQueue.front();
  if (cmd.commandPacket) {
    // front is command to be sent -> send it
    sendPacket(cmd.commandPacket);
    // remove original entry, put waiting-for-response marker there instead
    cmd.commandPacket.reset(); // clear out, marks this entry for "waiting for response"
    cmdQueue.pop_front();
    cmdQueue.push_front(cmd);
    // schedule timeout
    cmdTimeoutTicket.executeOnce(boost::bind(&EnoceanComm::cmdTimeout, this), ENOCEAN_ESP3_COMMAND_TIMEOUT);
  }
}


void EnoceanComm::cmdTimeout()
{
  // currently waiting command has timed out
  if (cmdQueue.empty()) return; // queue empty -> NOP (should not happen, no timeout should be running when queue is empty!)
  FOCUSLOG("EnOcean Command timeout");
  EnoceanCmd cmd = cmdQueue.front();
  // Note: commandPacket should always be NULL here (because we are waiting for a response)
  if (!cmd.commandPacket) {
    // done with this command
    // - remove from queue
    cmdQueue.pop_front();
    // - now call handler with error
    if (cmd.responseCB) {
      cmd.responseCB(Esp3PacketPtr(), ErrorPtr(new EnoceanCommError(EnoceanCommError::CmdTimeout)));
    }
  }
  // check if more commands in queue to be sent
  checkCmdQueue();
}

#endif // ENABLE_ENOCEAN








