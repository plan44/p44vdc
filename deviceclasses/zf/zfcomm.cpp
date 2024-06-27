//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2017-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#define FOCUSLOGLEVEL 6

#include "zfcomm.hpp"

#if ENABLE_ZF

using namespace p44;


// MARK: - ZfPacket


ZfPacket::ZfPacket() :
  len(0),
  opCode(0),
  uid(0),
  data(0),
  rssi(0)
{
}


size_t ZfPacket::getPacket(size_t aNumBytes, uint8_t *aBytes, ZfPacketPtr &aPacket)
{
  // no packet yet
  aPacket.reset();
  // all packet must begin with a 0x53
  if (*aBytes!=0x53) {
    // consume single stray byte and return
    return 1;
  }
  // can be beginning of a packet
  if (aNumBytes<2) {
    // need to see length byte first
    return NOT_ENOUGH_BYTES;
  }
  // startbyte and length available
  size_t len = aBytes[1]+1; // one more than length byte indicates
  if (aNumBytes<len) {
    // need to see entire packet
    return NOT_ENOUGH_BYTES;
  }
  // now we have all packet bytes
  // - check XOR checksum first
  uint8_t xorsum = 0;
  for (size_t i=0; i<len-1; i++) {
    xorsum ^= aBytes[i];
  }
  if (xorsum!=aBytes[len-1]) {
    // invalid checksum -> consume bytes but do not deliver packet
    return len;
  }
  // packet ok
  aPacket = ZfPacketPtr(new ZfPacket);
  aPacket->len = len;
  aPacket->opCode = aBytes[3];
  aPacket->uid =
    (aBytes[4]<<24) |
    (aBytes[5]<<16) |
    (aBytes[6]<<8) |
    aBytes[7];
  aPacket->data = aBytes[8];
  aPacket->rssi = aBytes[9];
  // packet bytes consumed
  return len;
}


string ZfPacket::description()
{
  return string_format("ZFpacket from 0x%08lX, opcode=%d, data=%d, rssi=%d", (unsigned long)uid, (int)opCode, (int)data, (int)rssi);
}


/*
// MARK: - ZF SerialOperations


class ZfResponse : public SerialOperation
{
  typedef SerialOperation inherited;

public:

  string response;

  ZfResponse() {};

  virtual ssize_t acceptBytes(size_t aNumBytes, uint8_t *aBytes)
  {
    return getMessage(aNumBytes, aBytes, response);
  };

  virtual bool hasCompleted()
  {
    // completed when we've got a non-empty response
    return response.size()>0;
  };

};
typedef boost::intrusive_ptr<ZfResponse> ZfResponsePtr;


class ZfCommand : public SerialOperationSend
{
  typedef SerialOperationSend inherited;

public:

  /// send a command not expecting an answer
  ZfCommand(const string &aCommand)
  {
    string cmd = aCommand + '\r';
    setDataSize(cmd.size());
    appendData(cmd.size(), (uint8_t *)cmd.c_str());
  }
  
};

*/


// MARK: - ZF communication handler

// baudrate for communication with ZF modem
#define ZF_COMMAPARMS "57600,8,N,1"

#define ZF_MAX_MESSAGE_SIZE 100


ZfComm::ZfComm(MainLoop &aMainLoop) :
	inherited(aMainLoop)
{
  // serialqueue needs a buffer as we use NOT_ENOUGH_BYTES mechanism
  setAcceptBuffer(ZF_MAX_MESSAGE_SIZE);
}


ZfComm::~ZfComm()
{
}


void ZfComm::setConnectionSpecification(const char *aConnectionSpec, uint16_t aDefaultPort)
{
  FOCUSOLOG("setConnectionSpecification: %s", aConnectionSpec);
  mSerialComm->setConnectionSpecification(aConnectionSpec, aDefaultPort, ZF_COMMAPARMS);
	// open connection so we can receive
	mSerialComm->requestConnection();
}


void ZfComm::initialize(StatusCB aCompletedCB)
{
  // essentially NOP for now
  if (aCompletedCB) aCompletedCB(ErrorPtr());
}


void ZfComm::setReceivedPacketHandler(ZfPacketCB aPacketHandler)
{
  receivedPacketHandler = aPacketHandler;
}


ssize_t ZfComm::acceptExtraBytes(size_t aNumBytes, uint8_t *aBytes)
{
  ZfPacketPtr packet;
  size_t ret = ZfPacket::getPacket(aNumBytes, aBytes, packet);
  if (packet) {
    FOCUSOLOG("received message: %s", packet->description().c_str());
    if (receivedPacketHandler) {
      receivedPacketHandler(packet, ErrorPtr());
    }
  }
  return ret; // NOT_ENOUGH_BYTES or length of command consumed
}


/*

void ZfComm::commandResponseHandler(ZfMessageCB aResponseCB, SerialOperationPtr aResponse, ErrorPtr aError)
{
  ZfResponsePtr r = boost::dynamic_pointer_cast<ZfResponse>(aResponse);
  if (r) {
    FOCUSOLOG("received answer: %s", r->response.c_str());
  }
  if (aResponseCB) {
    if (Error::isOK(aError) && aResponse) {
      // ok with result
      if (r) {
        aResponseCB(r->response, aError);
        return;
      }
    }
    // error or command w/o result
    aResponseCB(string(), aError);
  }
}

*/


#endif // ENABLE_ZF




