//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#define FOCUSLOGLEVEL 7

#include "dalicomm.hpp"

#if ENABLE_DALI

using namespace p44;

#if P44_BUILD_DIGI
#define DALIBRIDGE_COMMPARAMS "9600,8,N,1" // pseudo baudrate for old Digi ESP dali bridge must be 9600bd
#else
#define DALIBRIDGE_COMMPARAMS "none" // modern DALI bridge kernel module does not want termios settings (and will return error if tried)
#endif

// default sending and sampling adjustment values
#define DEFAULT_SENDING_EDGE_ADJUSTMENT 16 // one step (1/16th = 16/256th DALI bit time) delay of rising edge by default is probably better
#define DEFAULT_SAMPLING_POINT_ADJUSTMENT 0


DaliComm::DaliComm(MainLoop &aMainLoop) :
  mMultiMaster(false),
	inherited(aMainLoop),
  mRunningProcedures(0),
  mDali2ScanLock(false),
  mDali2LUNLock(false),
  mRetriedReads(0),
  mRetriedWrites(0),
  mCloseAfterIdleTime(Never),
  mResponsesInSequence(false),
  mExpectedBridgeResponses(0),
  mSendEdgeAdj(DEFAULT_SENDING_EDGE_ADJUSTMENT),
  mSamplePointAdj(DEFAULT_SAMPLING_POINT_ADJUSTMENT)
{
  // serialqueue needs a buffer as we use NOT_ENOUGH_BYTES mechanism
  setAcceptBuffer(21); // actually min 3 bytes for EVENT_CODE_FOREIGN_FRAME
}


DaliComm::~DaliComm()
{
}


// MARK: - GTIN blacklist for ill-behaving devices


const long long DALI_GTIN_blacklist[] = {
  4052899919433ll, // OTi DALI 50/220…240/1A4 LT2 FAN - has garbage serial no, many duplicates!
  79462533185379ll, // Finnor GAC616 or similar LED rail spot, unknown driver, reports this GTIN, zero hits for it in google -> unlikely to block something real
  0 // terminator
};



// MARK: - procedure management

void DaliComm::startProcedure()
{
  ++mRunningProcedures;
}

void DaliComm::endProcedure()
{
  if (mRunningProcedures>0)
    --mRunningProcedures;
}


bool DaliComm::isBusy()
{
  return mRunningProcedures>0;
}




// MARK: - DALI bridge low level communication


static const char *bridgeCmdName(uint8_t aBridgeCmd)
{
  switch (aBridgeCmd) {
    case CMD_CODE_RESET:       return "RESETBRIDGE    ";
    case CMD_CODE_SEND16:      return "SEND16         ";
    case CMD_CODE_2SEND16:     return "DOUBLESEND16   ";
    case CMD_CODE_SEND16_REC8: return "SEND16_REC8    ";
    case CMD_CODE_OVLRESET:    return "OVLRESET       ";
    case CMD_CODE_EDGEADJ:     return "EDGEADJ        ";
    default: return "???";
  }
}


static const char *bridgeAckText(uint8_t aResp1, uint8_t aResp2)
{
  if (aResp1==RESP_CODE_ACK || aResp1==RESP_CODE_ACK_RETRIED) {
    switch (aResp2) {
      case ACK_OK:         return "OK             ";
      case ACK_TIMEOUT:    return "TIMEOUT        ";
      case ACK_FRAME_ERR:  return "FRAME_ERROR    ";
      case ACK_OVERLOAD:   return "BUS_OVERLOAD   ";
      case ACK_INVALIDCMD: return "INVALID_COMMAND";
      default:             return "UNKNOWN_ACKCODE";
    }
  }
  return "NOT_ACK_CODE   ";
}



void DaliComm::setConnectionSpecification(const char *aConnectionSpec, uint16_t aDefaultPort, MLMicroSeconds aCloseAfterIdleTime)
{
  mCloseAfterIdleTime = aCloseAfterIdleTime;
  mSerialComm->setConnectionSpecification(aConnectionSpec, aDefaultPort, DALIBRIDGE_COMMPARAMS);
}


void DaliComm::bridgeResponseHandler(DaliBridgeResultCB aBridgeResultHandler, SerialOperationReceivePtr aOperation, ErrorPtr aError)
{
  // check for operation timeout
  if (Error::isError(aError, OQError::domain(), OQError::TimedOut)) {
    // receive operation (answer from bridge, not from DALI!) has timed out
    aError = Error::err<DaliCommError>(DaliCommError::BridgeComm, "DALI bridge receive operation timed out - indicates problem with bridge");
    OLOG(LOG_ERR, "While still expecting %d responses: %s", mExpectedBridgeResponses, aError->text()); // make sure it is in the log, even if aBridgeResultHandler does not handle the error
    // otherwise, treat like having received an answer (count it, to avoid stalls)
  }
  if (mExpectedBridgeResponses>0) mExpectedBridgeResponses--;
  if (mExpectedBridgeResponses<BUFFERED_BRIDGE_RESPONSES_LOW) {
    mResponsesInSequence = false; // allow buffered sends without waiting for answers again
  }
  // get received data
  if (Error::isOK(aError) && aOperation && aOperation->getDataSize()>=2) {
    uint8_t resp1 = aOperation->getDataP()[0];
    uint8_t resp2 = aOperation->getDataP()[1];
    if (resp1==RESP_CODE_DATA || resp1==RESP_CODE_DATA_RETRIED) {
      FOCUSOLOG("bridge response: DATA            (%02X)      %02X    - %d pending responses%s", resp1, resp2, mExpectedBridgeResponses, resp1==RESP_CODE_DATA_RETRIED ? ", RETRIED" : "");
      if (resp1==RESP_CODE_DATA_RETRIED) mRetriedReads++;
    }
    else {
      FOCUSOLOG("bridge response: %s (%02X %02X)         - %d pending responses%s", bridgeAckText(resp1, resp2), resp1, resp2, mExpectedBridgeResponses, resp1==RESP_CODE_ACK_RETRIED ? ", RETRIED" : "");
      if (resp1==RESP_CODE_ACK_RETRIED) {
        if (resp2==ACK_TIMEOUT || resp2==ACK_FRAME_ERR)
          mRetriedReads++; // read ACKs
        else
          mRetriedWrites++; // count others as write ACKs
      }
    }
    if (aBridgeResultHandler) {
      aBridgeResultHandler(resp1, resp2, aError);
    }
  }
  else {
    // error
    if (Error::isOK(aError))
      aError = ErrorPtr(new DaliCommError(DaliCommError::MissingData));
    if (aBridgeResultHandler) {
      aBridgeResultHandler(0, 0, aError);
    }
  }
}


void DaliComm::sendBridgeCommand(uint8_t aCmd, uint8_t aDali1, uint8_t aDali2, DaliBridgeResultCB aResultCB, int aWithDelay)
{
  // reset connection closing timeout
  mConnectionTimeoutTicket.cancel();
  if (mCloseAfterIdleTime!=Never) {
    mConnectionTimeoutTicket.executeOnce(boost::bind(&DaliComm::connectionTimeout, this), mCloseAfterIdleTime);
  }
  // create sending operation
  SerialOperationSendPtr sendOp = SerialOperationSendPtr(new SerialOperationSend);
  if (aCmd<8) {
    // single byte command
    sendOp->setDataSize(1);
    sendOp->appendByte(aCmd);
  }
  else {
    // 3 byte command
    sendOp->setDataSize(3);
    sendOp->appendByte(aCmd);
    sendOp->appendByte(aDali1);
    sendOp->appendByte(aDali2);
  }
  // prepare response reading operation
  SerialOperationReceivePtr recOp = SerialOperationReceivePtr(new SerialOperationReceive);
  recOp->setExpectedBytes(2); // expected 2 response bytes
  mExpectedBridgeResponses++;
  if (aWithDelay>0) {
    // delayed sends must always be in sequence (always leave recOp->inSequence at its default, true)
    sendOp->setInitiationDelay(aWithDelay);
    FOCUSOLOG("bridge command:  %s (%02X)      %02X %02X - %d pending responses - to be sent in %d µS after no response pending", bridgeCmdName(aCmd), aCmd, aDali1, aDali2, mExpectedBridgeResponses, aWithDelay);
  }
  else {
    // non-delayed sends may be sent before answer of previous commands have arrived as long as Rx (9210) or Tx (p44dbr) buf in bridge does not overflow
    if (mExpectedBridgeResponses>BUFFERED_BRIDGE_RESPONSES_HIGH) {
      mResponsesInSequence = true; // prevent further sends without answers
    }
    recOp->mInSequence = mResponsesInSequence;
    FOCUSOLOG("bridge command:  %s (%02X)      %02X %02X - %d pending responses - %s", bridgeCmdName(aCmd), aCmd, aDali1, aDali2, mExpectedBridgeResponses, mResponsesInSequence ? "sent when no more responses pending" : "sent as soon as possible");
  }
  recOp->setTimeout(120*Second); // large timeout, because it can really take time until all expected answers are received, or DEH2 network/serial load might disturb timing for a longer while
  // set callback
  // - for recOp to obtain result or get error
  recOp->setCompletionCallback(boost::bind(&DaliComm::bridgeResponseHandler, this, aResultCB, recOp, _1));
  // chain response op
  sendOp->setChainedOperation(recOp);
  // queue op
  queueSerialOperation(sendOp);
  // process operations
  processOperations();
}


void DaliComm::connectionTimeout()
{
  mSerialComm->closeConnection();
}

// MARK: - DALI bus communication basics


static ErrorPtr checkBridgeResponse(uint8_t aResp1, uint8_t aResp2, ErrorPtr aError, bool &aNoOrTimeout, bool &aRetried)
{
  aNoOrTimeout = false;
  aRetried = false;
  if (aError) {
    return aError;
  }
  switch(aResp1) {
    case RESP_CODE_ACK_RETRIED:
      // command acknowledged with retry
      aRetried = true;
    case RESP_CODE_ACK:
      // command acknowledged
      switch (aResp2) {
        case ACK_TIMEOUT:
          aNoOrTimeout = true;  // only DALI timeout, which is no real error
          // otherwise like OK
        case ACK_OK:
          return ErrorPtr(); // no error
        case ACK_FRAME_ERR:
          return ErrorPtr(new DaliCommError(DaliCommError::DALIFrame));
        case ACK_INVALIDCMD:
          return ErrorPtr(new DaliCommError(DaliCommError::BridgeCmd));
        case ACK_OVERLOAD:
          return Error::err<DaliCommError>(DaliCommError::BusOverload, "DALI bus overload or short-circuit -> power down and check installation!");
      }
      break;
    case RESP_CODE_DATA_RETRIED:
      // data reading acknowledged with retry
      aRetried = true;
    case RESP_CODE_DATA:
      return ErrorPtr(); // no error
  }
  // other, uncatched error
  return ErrorPtr(new DaliCommError(DaliCommError::BridgeUnknown));
}


void DaliComm::daliCommandStatusHandler(DaliCommandStatusCB aResultCB, uint8_t aResp1, uint8_t aResp2, ErrorPtr aError)
{
  bool noOrTimeout;
  bool retried;
  ErrorPtr err = checkBridgeResponse(aResp1, aResp2, aError, noOrTimeout, retried);
  if (!err && noOrTimeout) {
    // timeout for a send-only command -> out of sync, bridge communication error
    err = ErrorPtr(new DaliCommError(DaliCommError::BridgeComm));
  }
  // execute callback if any
  if (aResultCB)
    aResultCB(err, retried);
}



void DaliComm::daliQueryResponseHandler(DaliQueryResultCB aResultCB, uint8_t aResp1, uint8_t aResp2, ErrorPtr aError)
{
  bool noOrTimeout;
  bool retried;
  ErrorPtr err = checkBridgeResponse(aResp1, aResp2, aError, noOrTimeout, retried);
  // execute callback if any
  if (aResultCB)
    aResultCB(noOrTimeout, aResp2, err, retried);
}


ssize_t DaliComm::acceptExtraBytes(size_t aNumBytes, uint8_t *aBytes)
{
  // before bridge V6, no data is expected except answers for commands
  // from bridge V6 onwards, bridge may send event data using EVENT_CODE_FOREIGN_FRAME
  #if ENABLE_DALI_INPUTS
  if (aBytes[0]==EVENT_CODE_FOREIGN_FRAME) {
    if (aNumBytes<3) return NOT_ENOUGH_BYTES;
    // detected forward frame on the bus from another master
    OLOG(LOG_INFO, "bridge event: 0x%02X 0x%02X 0x%02X from other master on bus", aBytes[0], aBytes[1], aBytes[2]);
    // invoke handler
    if (mBridgeEventHandler) {
      mBridgeEventHandler(aBytes[0], aBytes[1], aBytes[2]);
    }
    return 3; // 3 bytes of event message consumed, but no more
  }
  else
  #endif
  {
    // no forward frame event and no bridge answers expected -> consume any extra bytes.
    // extra bytes while no response expected are always sign of desynchronisation
    if (FOCUSLOGENABLED) {
      string b;
      b.assign((char *)aBytes, aNumBytes);
      FOCUSOLOG("bridge: received extra bytes (%s) -> bridge was apparently out of sync", binaryToHexString(b, ' ').c_str());
    }
    else {
      OLOG(LOG_WARNING,"bridge: received %zu extra bytes -> bridge was apparently out of sync", aNumBytes);
    }
    return aNumBytes;
  }
}





// reset the bridge

#define DALI_SINGLE_MASTER_PING_INTERVAL (10*Minute) // Single master controllers must issue a PING command in this interval

void DaliComm::reset(DaliCommandStatusCB aStatusCB)
{
  // this first reset command should also consume extra bytes left over from previous use
  // use delay to make sure commands are NOT buffered and extra bytes from unsynced bridge will be catched here
  FOCUSOLOG("Before reset: retriedWrites=%ld, retriedReads=%ld, expectedBridgeResponses=%d (will be cleared to 0 now)", mRetriedWrites, mRetriedReads, mExpectedBridgeResponses);
  mRetriedWrites = 0;
  mRetriedReads = 0;
  mExpectedBridgeResponses = 0;
  sendBridgeCommand(CMD_CODE_RESET, 0, 0, boost::bind(&DaliComm::resetIssued, this, 0, aStatusCB, _1, _2, _3), 100*MilliSecond);
}


#define MAX_RESET_RETRIES 20

void DaliComm::resetIssued(int aCount, DaliCommandStatusCB aStatusCB, uint8_t aResp1, uint8_t aResp2, ErrorPtr aError)
{
  // repeat resets until we get a correct answer
  if (Error::notOK(aError) || aResp1!=RESP_CODE_ACK || aResp2!=ACK_OK) {
    OLOG(LOG_WARNING, "bridge: Incorrect answer (%02X %02X) or error (%s) from reset command -> repeating", aResp1, aResp2, aError ? aError->text() : "none");
    if (aCount>=MAX_RESET_RETRIES) {
      if (Error::isOK(aError)) aError = Error::err<DaliCommError>(DaliCommError::BadData, "Bridge reset failed");
      if (aStatusCB) aStatusCB(aError, false);
      return;
    }
    // issue another reset
    mExpectedBridgeResponses = 0;
    sendBridgeCommand(CMD_CODE_RESET, 0, 0, boost::bind(&DaliComm::resetIssued, this, aCount+1, aStatusCB, _1, _2, _3), 500*MilliSecond);
    return;
  }
  // send next reset command with a longer delay, to give bridge time to process possibly buffered commands
  // (p44dbr does not execute next command until return code for previous command has been read from /dev/daliX)
  sendBridgeCommand(CMD_CODE_RESET, 0, 0, NoOP, 1*Second);
  // another reset to make sure
  sendBridgeCommand(CMD_CODE_RESET, 0, 0, NoOP, 100*MilliSecond);
  // make sure bus overload protection is active, autoreset enabled, reset to operating
  sendBridgeCommand(CMD_CODE_OVLRESET, 0, 0, NoOP);
  // set DALI signal edge adjustments (available from fim_dali v3 onwards)
  sendBridgeCommand(CMD_CODE_EDGEADJ, mSendEdgeAdj, mSamplePointAdj, NoOP);
  // terminate any special commands on the DALI bus
  daliSend(DALICMD_TERMINATE, 0, aStatusCB);
  // re-start PING if single master
  mPingTicket.cancel();
  if (!mMultiMaster) {
    mPingTicket.executeOnce(boost::bind(&DaliComm::singleMasterPing, this, _1), DALI_SINGLE_MASTER_PING_INTERVAL);
  }
}

// From DALI 103:
//   In order to make itself known as a possibly anonymous transmitting bus unit, a single-master
//   application controller shall transmit a PING message at regular intervals of 10 ± 1 min. The
//   first such PING message shall appear at a random time between 5 min and 10 min after
//   completion of the power-on procedure.

void DaliComm::singleMasterPing(MLTimer &aMLTimer)
{
  if (!isBusy()) daliSend(DALICMD_PING, 0, NoOP);
  MainLoop::currentMainLoop().retriggerTimer(aMLTimer, DALI_SINGLE_MASTER_PING_INTERVAL);
}




// Regular DALI bus commands

void DaliComm::daliSend(uint8_t aDali1, uint8_t aDali2, DaliCommandStatusCB aStatusCB, int aWithDelay)
{
  sendBridgeCommand(CMD_CODE_SEND16, aDali1, aDali2, boost::bind(&DaliComm::daliCommandStatusHandler, this, aStatusCB, _1, _2, _3), aWithDelay);
}

void DaliComm::daliSendDirectPower(DaliAddress aAddress, uint8_t aPower, DaliCommandStatusCB aStatusCB, int aWithDelay)
{
  daliSend(dali1FromAddress(aAddress), aPower, aStatusCB, aWithDelay);
}


void DaliComm::daliPrepareForCommand(DaliCommand &aCommand, int &aWithDelay)
{
  if (aCommand & 0xFF00) {
    // command has a device type
    uint8_t dt = aCommand>>8;
    if (dt==0xFF) dt=0; // 0xFF is used to code DT0, to allow 0 meaning "no DT prefix" (DT0 is not in frequent use anyway)
    daliSend(DALICMD_ENABLE_DEVICE_TYPE, dt, NoOP, aWithDelay); // apply delay to prefix command!
    aWithDelay = 0; // no further delay for actual command after prefix
    aCommand &= 0xFF; // mask out device type, is now consumed
  }
}



void DaliComm::daliSendCommand(DaliAddress aAddress, DaliCommand aCommand, DaliCommandStatusCB aStatusCB, int aWithDelay)
{
  daliPrepareForCommand(aCommand, aWithDelay);
  daliSend(dali1FromAddress(aAddress)+1, aCommand, aStatusCB, aWithDelay);
}


void DaliComm::daliSendDtrAndCommand(DaliAddress aAddress, DaliCommand aCommand, uint8_t aDTRValue, DaliCommandStatusCB aStatusCB, int aWithDelay)
{
  daliSend(DALICMD_SET_DTR, aDTRValue, NoOP, aWithDelay); // apply delay to DTR setting command
  aWithDelay = 0; // delay already consumed for setting DTR
  daliPrepareForCommand(aCommand, aWithDelay);
  daliSendCommand(aAddress, aCommand, aStatusCB);
}



// DALI config commands (send twice within 100ms)

void DaliComm::daliSendTwice(uint8_t aDali1, uint8_t aDali2, DaliCommandStatusCB aStatusCB, int aWithDelay)
{
  sendBridgeCommand(CMD_CODE_2SEND16, aDali1, aDali2, boost::bind(&DaliComm::daliCommandStatusHandler, this, aStatusCB, _1, _2, _3), aWithDelay);
}

void DaliComm::daliSendConfigCommand(DaliAddress aAddress, DaliCommand aCommand, DaliCommandStatusCB aStatusCB, int aWithDelay)
{
  daliPrepareForCommand(aCommand, aWithDelay);
  daliSendTwice(dali1FromAddress(aAddress)+1, aCommand, aStatusCB, aWithDelay);
}


void DaliComm::daliSendDtrAndConfigCommand(DaliAddress aAddress, DaliCommand aCommand, uint8_t aDTRValue, DaliCommandStatusCB aStatusCB, int aWithDelay)
{
  daliSend(DALICMD_SET_DTR, aDTRValue, NoOP, aWithDelay);
  aWithDelay = 0; // delay already consumed for setting DTR
  daliPrepareForCommand(aCommand, aWithDelay);
  daliSendConfigCommand(aAddress, aCommand, aStatusCB, aWithDelay);
}


void DaliComm::daliSend16BitValueAndCommand(DaliAddress aAddress, DaliCommand aCommand, uint16_t aValue16, DaliCommandStatusCB aStatusCB, int aWithDelay)
{
  daliSend(DALICMD_SET_DTR1, aValue16>>8, NoOP, aWithDelay); // MSB->DTR1 - apply delay to DTR1 setting command
  daliSend(DALICMD_SET_DTR, aValue16&0xFF); // LSB->DTR
  aWithDelay = 0; // delay already consumed for setting DTR1
  daliPrepareForCommand(aCommand, aWithDelay);
  daliSendCommand(aAddress, aCommand, aStatusCB);
}


void DaliComm::daliSend3x8BitValueAndCommand(DaliAddress aAddress, DaliCommand aCommand, uint8_t aValue0, uint8_t aValue1, uint8_t aValue2, DaliCommandStatusCB aStatusCB, int aWithDelay)
{
  daliSend(DALICMD_SET_DTR, aValue0, NoOP, aWithDelay);
  daliSend(DALICMD_SET_DTR1, aValue1);
  daliSend(DALICMD_SET_DTR2, aValue2);
  aWithDelay = 0; // delay already consumed for setting DTR
  daliPrepareForCommand(aCommand, aWithDelay);
  daliSendCommand(aAddress, aCommand, aStatusCB);
}



// DALI Query commands (expect answer byte)

void DaliComm::daliSendAndReceive(uint8_t aDali1, uint8_t aDali2, DaliQueryResultCB aResultCB, int aWithDelay)
{
  sendBridgeCommand(CMD_CODE_SEND16_REC8, aDali1, aDali2, boost::bind(&DaliComm::daliQueryResponseHandler, this, aResultCB, _1, _2, _3), aWithDelay);
}


void DaliComm::daliSendQuery(DaliAddress aAddress, DaliCommand aQueryCommand, DaliQueryResultCB aResultCB, int aWithDelay)
{
  if (aAddress==NoDaliAddress) {
    if (aResultCB) aResultCB(true, 0, Error::err<DaliCommError>(DaliCommError::NoAddress, "no valid DALI address"), false);
    return;
  }
  daliPrepareForCommand(aQueryCommand, aWithDelay);
  daliSendAndReceive(dali1FromAddress(aAddress)+1, aQueryCommand, aResultCB, aWithDelay);
}


void DaliComm::daliSendDtrAndQuery(DaliAddress aAddress, DaliCommand aQueryCommand, uint8_t aDTRValue, DaliQueryResultCB aResultCB, int aWithDelay)
{
  daliSend(DALICMD_SET_DTR, aDTRValue, NoOP, aWithDelay);
  daliSendQuery(aAddress, aQueryCommand, aResultCB, 0); // delay already consumed for setting DTR
}


void DaliComm::daliSend16BitQuery(DaliAddress aAddress, DaliCommand aQueryCommand, Dali16BitValueQueryResultCB aResult16CB, int aWithDelay)
{
  daliPrepareForCommand(aQueryCommand, aWithDelay);
  daliSendQuery(aAddress, aQueryCommand, boost::bind(&DaliComm::msbOf16BitQueryReceived, this, aAddress, aResult16CB, _1, _2, _3), aWithDelay);
}


void DaliComm::msbOf16BitQueryReceived(DaliAddress aAddress, Dali16BitValueQueryResultCB aResult16CB, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    if (aNoOrTimeout) {
      aError = ErrorPtr(new DaliCommError(DaliCommError::MissingData));
    }
    else {
      // this is the MSB, now query the DTR to get the LSB
      uint16_t result16 = aResponse<<8;
      daliSendQuery(aAddress, DALICMD_QUERY_CONTENT_DTR, boost::bind(&DaliComm::lsbOf16BitQueryReceived, this, result16, aResult16CB, _1, _2, _3));
      return;
    }
  }
  if (aResult16CB) aResult16CB(0, aError);
}


void DaliComm::lsbOf16BitQueryReceived(uint16_t aResult16, Dali16BitValueQueryResultCB aResult16CB, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    if (aNoOrTimeout) {
      aError = ErrorPtr(new DaliCommError(DaliCommError::MissingData));
    }
    else {
      // this is the LSB, combine with MSB and return
      aResult16 |= aResponse;
    }
  }
  if (aResult16CB) aResult16CB(aResult16, aError);
}


void DaliComm::daliSendDtrAnd16BitQuery(DaliAddress aAddress, DaliCommand aQueryCommand, uint8_t aDTRValue, Dali16BitValueQueryResultCB aResultCB, int aWithDelay)
{
  daliSend(DALICMD_SET_DTR, aDTRValue, NoOP, aWithDelay);
  daliSend16BitQuery(aAddress, aQueryCommand, aResultCB, 0); // delay already consumed for setting DTR
}




bool DaliComm::isYes(bool aNoOrTimeout, uint8_t aResponse, ErrorPtr &aError, bool aCollisionIsYes)
{
  bool isYes = !aNoOrTimeout;
  if (aError && aCollisionIsYes && aError->isError(DaliCommError::domain(), DaliCommError::DALIFrame)) {
    // framing error -> consider this a YES
    isYes = true;
    aError.reset(); // not considered an error when aCollisionIsYes is set
  }
  else if (isYes && !aCollisionIsYes) {
    // regular answer, must be DALIANSWER_YES to be a regular YES
    if (aResponse!=DALIANSWER_YES) {
      // invalid YES response
      aError.reset(new DaliCommError(DaliCommError::InvalidAnswer));
    }
  }
  if (aError)
    return false; // real error, consider NO
  // return YES/NO
  return isYes;
}



// DALI address byte:
// 0AAA AAAS : device short address (0..63)
// 100A AAAS : group address (0..15)
// 1111 111S : broadcast
// S : 0=direct arc power, 1=command

uint8_t DaliComm::dali1FromAddress(DaliAddress aAddress)
{
  if (aAddress==DaliBroadcast) {
    return 0xFE; // broadcast
  }
  else if (aAddress & DaliGroup) {
    return 0x80 + ((aAddress & DaliAddressMask)<<1); // group address
  }
  else {
    return ((aAddress & DaliAddressMask)<<1); // device short address
  }
}


DaliAddress DaliComm::addressFromDaliResponse(uint8_t aResponse)
{
  // 1111111x = broadcast
  // 0AAAAAAx = short address
  // 100AAAAx = group address
  aResponse &= 0xFE; // mask out direct arc bit
  if (aResponse==0xFE) {
    return DaliBroadcast; // broadcast
  }
  else if ((aResponse & 0x80)==0x00) {
    return (aResponse>>1) & DaliAddressMask; // device short address
  }
  else if ((aResponse & 0xE0)==0x80) {
    return ((aResponse>>1) & DaliGroupMask) + DaliGroup;
  }
  else {
    return NoDaliAddress; // is not a DALI address
  }
}


string DaliComm::formatDaliAddress(DaliAddress aAddress)
{
  if (aAddress==NoDaliAddress) {
    return "<NoDaliAddress>";
  }
  else if (aAddress==DaliBroadcast) {
    return "broadcast";
  }
  else if (aAddress & DaliGroup) {
    return string_format("group address %d", aAddress & DaliGroupMask); // group address
  }
  else if (aAddress & DaliScene) {
    return string_format("scene number %d", aAddress & DaliSceneMask); // scene number (not really an address...)
  }
  else {
    return string_format("short address %d", aAddress & DaliAddressMask); // single device address
  }
}




// MARK: - DALI bus data R/W test

class DaliBusDataTester : public P44Obj
{
  DaliComm &daliComm;
  StatusCB callback;
  DaliAddress busAddress;
  int numCycles;
  int cycle;
  uint8_t dtrValue;
  int numErrors;
public:
  static void daliBusTestData(DaliComm &aDaliComm, StatusCB aResultCB, DaliAddress aAddress, uint8_t aNumCycles)
  {
    // create new instance, deletes itself when finished
    new DaliBusDataTester(aDaliComm, aResultCB, aAddress, aNumCycles);
  };
private:
  DaliBusDataTester(DaliComm &aDaliComm, StatusCB aResultCB, DaliAddress aAddress, uint8_t aNumCycles) :
    daliComm(aDaliComm),
    callback(aResultCB),
    busAddress(aAddress),
    numCycles(aNumCycles)
  {
    daliComm.startProcedure();
    FOCUSSOLOG(daliComm, "Before R/W test: retriedWrites=%ld, retriedReads=%ld", daliComm.mRetriedWrites, daliComm.mRetriedReads);
    SOLOG(daliComm, LOG_DEBUG, "bus address %d - doing %d R/W tests to DTR...", busAddress, aNumCycles);
    // start with 0x55 pattern
    dtrValue = 0;
    numErrors = 0;
    cycle = 0;
    testNextByte();
  };

  // handle scan result
  void handleResponse(bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
  {
    if (Error::notOK(aError)) {
      numErrors++;
      SOLOG(daliComm, LOG_DEBUG, "- written 0x%02X, got error %s", dtrValue, aError->text());
    }
    else {
      if(!aNoOrTimeout) {
        // byte received
        if (aResponse!=dtrValue) {
          numErrors++;
          SOLOG(daliComm, LOG_DEBUG, "- written 0x%02X, read back 0x%02X -> error", dtrValue, aResponse);
        }
      }
      else {
        numErrors++;
        SOLOG(daliComm, LOG_DEBUG, "- written 0x%02X, got no answer (timeout) -> error", dtrValue);
      }
    }
    // prepare next test value
    dtrValue += 0x55; // gives 0x00, 0x55, 0xAA, 0xFF, 0x54... sequence (all values tested after 256 cycles)
    cycle++;
    if (cycle<numCycles) {
      // test next
      testNextByte();
      return;
    }
    // all cycles done, return result
    daliComm.endProcedure();
    if (numErrors>0) {
      SOLOG(daliComm, LOG_ERR, "Unreliable data access for bus address %d - %d of %d R/W tests have failed!", busAddress, numErrors, numCycles);
      FOCUSSOLOG(daliComm, "After failed R/W test: retriedWrites=%ld, retriedReads=%ld", daliComm.mRetriedWrites, daliComm.mRetriedReads);
      if (callback) callback(Error::err<DaliCommError>(numErrors>=numCycles ? DaliCommError::DataMissing : DaliCommError::DataUnreliable, "DALI R/W tests: %d of %d failed", numErrors, numCycles));
    }
    else {
      // everything is fine
      SOLOG(daliComm, LOG_DEBUG, "bus address %d - all %d test cycles OK", busAddress, numCycles);
      FOCUSSOLOG(daliComm, "After succesful R/W test: retriedWrites=%ld, retriedReads=%ld", daliComm.mRetriedWrites, daliComm.mRetriedReads);
      if (callback) callback(ErrorPtr());
    }
    // done, delete myself
    delete this;
  };


  void testNextByte()
  {
    daliComm.daliSend(DALICMD_SET_DTR, dtrValue);
    daliComm.daliSendQuery(busAddress, DALICMD_QUERY_CONTENT_DTR, boost::bind(&DaliBusDataTester::handleResponse, this, _1, _2, _3));
  }
};


void DaliComm::daliBusTestData(StatusCB aResultCB, DaliAddress aAddress, uint8_t aNumCycles)
{
  if (isBusy()) { aResultCB(DaliComm::busyError()); return; }
  DaliBusDataTester::daliBusTestData(*this, aResultCB, aAddress, aNumCycles);
}



// MARK: - DALI bus scanning

// Scan bus for active devices (returns list of short addresses)

class DaliBusScanner : public P44Obj
{
  DaliComm &mDaliComm;
  DaliComm::DaliBusScanCB mCallback;
  DaliAddress mShortAddress;
  DaliComm::ShortAddressListPtr mActiveDevicesPtr;
  DaliComm::ShortAddressListPtr mUnreliableDevicesPtr;
  bool mProbablyCollision;
  bool mUnconfiguredDevices;
public:
  static void scanBus(DaliComm &aDaliComm, DaliComm::DaliBusScanCB aResultCB)
  {
    // create new instance, deletes itself when finished
    new DaliBusScanner(aDaliComm, aResultCB);
  };
private:
  DaliBusScanner(DaliComm &aDaliComm, DaliComm::DaliBusScanCB aResultCB) :
    mCallback(aResultCB),
    mDaliComm(aDaliComm),
    mProbablyCollision(false),
    mUnconfiguredDevices(false),
    mActiveDevicesPtr(new DaliComm::ShortAddressList),
    mUnreliableDevicesPtr(new DaliComm::ShortAddressList)
  {
    mDaliComm.startProcedure();
    SOLOG(mDaliComm, LOG_INFO, "starting quick bus scan (short address poll)");
    // reset the bus first
    mDaliComm.reset(boost::bind(&DaliBusScanner::resetComplete, this, _1));
  }

  void resetComplete(ErrorPtr aError)
  {
    // check for overload condition
    if (Error::isError(aError, DaliCommError::domain(), DaliCommError::BusOverload)) {
      SOLOG(mDaliComm, LOG_ERR, "bus has overload - possibly due to short circuit, defective ballasts or more than 64 devices connected");
      SOLOG(mDaliComm, LOG_ERR, "-> Please power down installation, check bus and try again");
    }
    if (aError)
      return completed(aError);
    // check if there are devices without short address
    mDaliComm.daliSendQuery(DaliBroadcast, DALICMD_QUERY_MISSING_SHORT_ADDRESS, boost::bind(&DaliBusScanner::handleMissingShortAddressResponse, this, _1, _2, _3));
  }


  typedef enum {
    dqs_controlgear,
    dqs_random_h,
    dqs_random_m,
    dqs_random_l
  } DeviceQueryState;


  void handleMissingShortAddressResponse(bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
  {
    if (DaliComm::isYes(aNoOrTimeout, aResponse, aError, true)) {
      // we have devices without short addresses
      SOLOG(mDaliComm, LOG_NOTICE, "Detected devices without short address on the bus (-> will trigger full scan later)");
      mUnconfiguredDevices = true;
    }
    // start the scan
    mShortAddress = 0;
    nextQuery(dqs_controlgear);
  };


  // handle scan result
  void handleScanResponse(DeviceQueryState aQueryState, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
  {
    bool isYes = false;
    if (Error::isError(aError, DaliCommError::domain(), DaliCommError::DALIFrame)) {
      // framing error, indicates that we might have duplicates
      SOLOG(mDaliComm, LOG_NOTICE, "Detected framing error for %d-th response from short address %d - probably short address collision", (int)aQueryState, mShortAddress);
      mProbablyCollision = true;
      isYes = true; // still count as YES
      aError.reset(); // do not count as error aborting the search
      aQueryState=dqs_random_l; // one error is enough, no need to check other bytes
    }
    else if (Error::isOK(aError) && !aNoOrTimeout) {
      // no error, no timeout
      SOLOG(mDaliComm, LOG_DEBUG, "Short address %d returns 0x%02X for queryState %d (0=controlgear, 1..3=random address H/M/L)", mShortAddress, aResponse, (int)aQueryState);
      isYes = true;
      if (aQueryState==dqs_controlgear && aResponse!=DALIANSWER_YES) {
        // not entirely correct answer, also indicates collision
        SOLOG(mDaliComm, LOG_NOTICE, "Detected incorrect YES answer 0x%02X from short address %d - probably short address collision", aResponse, mShortAddress);
        mProbablyCollision = true;
      }
    }
    if (aQueryState==dqs_random_l || aNoOrTimeout) {
      // We get here in two cases:
      // a) isYes==true: querying this short address existence and collisions on it is complete
      //    (all queries up to dqs_random_l done or skipped because of a frame error)
      // b) isYes==false: the current query had a timeout
      // This can mean:
      // - if the current query is one of the random address readouts:
      //   - isYes==true: there is at least one device at this short address -> do R/W tests
      //     Note: do R/W tests even in case of possible collision, because we can't be sure
      //     it's REALLY a collision.
      //   - isYes==false, but we have no collision found yet on the bus in this scan -> do R/W tests
      //     Note: this means we had a timeout on one of the random address readouts, which might be
      //     caused by not fully compatible devices with no random address at all, such as some DMX/DALI
      //     converters. Doing the R/W test in this case will reveal if this could still be a usable device.
      //     Note that because we don't do R/W-tests when we think there are bus collision, such a
      //     nonconforming device might only be detected when the bus is free of collisions.
      // - otherwise:
      //   - no device at this short address (or a severely broken one)
      if (aQueryState!=dqs_controlgear && (isYes || !mProbablyCollision)) {
        // Device at this shortaddress: do a data reliability test now (quick 3 byte 0,0x55,0xAA only, unless loglevel>=6)
        DaliBusDataTester::daliBusTestData(mDaliComm, boost::bind(&DaliBusScanner::nextDevice ,this, true, _1), mShortAddress, LOGLEVEL>=LOG_INFO ? 9 : 3);
        return;
      }
      // No device found at this shortaddress -> just test next address
      nextDevice(false, ErrorPtr());
    }
    else {
      // more to check from same device
      nextQuery((DeviceQueryState)((int)aQueryState+1));
    }
  };


  void nextDevice(bool aDeviceAtThisAddress, ErrorPtr aError)
  {
    if (aDeviceAtThisAddress) {
      if (Error::isOK(aError)) {
        // this short address has a device which has passed the test
        mActiveDevicesPtr->push_back(mShortAddress);
        SOLOG(mDaliComm, LOG_INFO, "- detected reliably communicating DALI device at short address %d", mShortAddress);
      }
      else {
        mUnreliableDevicesPtr->push_back(mShortAddress);
        SOLOG(mDaliComm, LOG_ERR, "Detected device at short address %d, but it FAILED R/W TEST: %s -> ignoring", mShortAddress, aError->text());
      }
    }
    // check if more short addresses to test
    mShortAddress++;
    if (mShortAddress<DALI_MAXDEVICES) {
      // more devices to scan
      nextQuery(dqs_controlgear);
    }
    else {
      return completed(ErrorPtr());
    }
  }


  // query next device
  void nextQuery(DeviceQueryState aQueryState)
  {
    uint8_t q;
    switch (aQueryState) {
      default: q = DALICMD_QUERY_CONTROL_GEAR; break;
      case dqs_random_h: q = DALICMD_QUERY_RANDOM_ADDRESS_H; break;
      case dqs_random_m: q = DALICMD_QUERY_RANDOM_ADDRESS_M; break;
      case dqs_random_l: q = DALICMD_QUERY_RANDOM_ADDRESS_L; break;
    }
    mDaliComm.daliSendQuery(mShortAddress, q, boost::bind(&DaliBusScanner::handleScanResponse, this, aQueryState, _1, _2, _3));
  }



  void completed(ErrorPtr aError)
  {
    // scan done or error, return list to callback
    if (mProbablyCollision || mUnconfiguredDevices) {
      if (Error::notOK(aError)) {
        SOLOG(mDaliComm, LOG_WARNING, "Error (%s) in quick scan ignored because we need to do a full scan anyway", aError->text());
      }
      if (mProbablyCollision) {
        aError = Error::err<DaliCommError>(DaliCommError::AddressCollisions, "Address collision -> need full bus scan");
      }
      else {
        aError = Error::err<DaliCommError>(DaliCommError::AddressesMissing, "Devices with no short address -> need scan for those");
      }
    }
    FOCUSSOLOG(mDaliComm, "After scanBus complete: retriedWrites=%ld, retriedReads=%ld", mDaliComm.mRetriedWrites, mDaliComm.mRetriedReads);
    mDaliComm.endProcedure();
    // call back
    mCallback(mActiveDevicesPtr, mUnreliableDevicesPtr, aError);
    // done, delete myself
    delete this;
  }

};


void DaliComm::daliBusScan(DaliBusScanCB aResultCB)
{
  if (isBusy()) { aResultCB(ShortAddressListPtr(), ShortAddressListPtr(), DaliComm::busyError()); return; }
  DaliBusScanner::scanBus(*this, aResultCB);
}




// Scan DALI bus by random address

#define MAX_RESTARTS 3
#define MAX_COMPARE_REPEATS 1
#define MAX_SHORTADDR_READ_REPEATS 2
#define RESCAN_RETRY_DELAY (10*Second)
#define READ_SHORT_ADDR_SEND_DELAY 0

class DaliFullBusScanner : public P44Obj
{
  DaliComm &mDaliComm;
  DaliComm::DaliBusScanCB mCallback;
  bool mFullScanOnlyIfNeeded;
  uint32_t mSearchMax;
  uint32_t mSearchMin;
  uint32_t mSearchAddr;
  uint8_t mSearchL, mSearchM, mSearchH;
  uint32_t mLastSearchMin; // for re-starting
  int mRestarts;
  int mCompareRepeat;
  int mReadShortAddrRepeat;
  bool mSetLMH;
  DaliComm::ShortAddressListPtr mFoundDevicesPtr;
  DaliComm::ShortAddressListPtr mUsedShortAddrsPtr;
  DaliComm::ShortAddressListPtr mConflictedShortAddrsPtr;
  DaliAddress mNewAddress;
  MLTicket mDelayTicket;
public:
  static void fullBusScan(DaliComm &aDaliComm, DaliComm::DaliBusScanCB aResultCB, bool aFullScanOnlyIfNeeded)
  {
    // create new instance, deletes itself when finished
    new DaliFullBusScanner(aDaliComm, aResultCB, aFullScanOnlyIfNeeded);
  };
private:
  DaliFullBusScanner(DaliComm &aDaliComm, DaliComm::DaliBusScanCB aResultCB, bool aFullScanOnlyIfNeeded) :
    mDaliComm(aDaliComm),
    mCallback(aResultCB),
    mFullScanOnlyIfNeeded(aFullScanOnlyIfNeeded),
    mFoundDevicesPtr(new DaliComm::ShortAddressList)
  {
    mDaliComm.startProcedure();
    // start a scan
    startScan();
  }


  void startScan()
  {
    // first scan for used short addresses
    mFoundDevicesPtr->clear(); // must be empty in case we do a restart
    DaliBusScanner::scanBus(mDaliComm,boost::bind(&DaliFullBusScanner::shortAddrListReceived, this, _1, _2, _3));
  }


  void shortAddrListReceived(DaliComm::ShortAddressListPtr aShortAddressListPtr, DaliComm::ShortAddressListPtr aUnreliableShortAddressListPtr, ErrorPtr aError)
  {
    bool missingAddrs = Error::isError(aError, DaliCommError::domain(), DaliCommError::AddressesMissing);
    SOLOG(mDaliComm, LOG_NOTICE, "found %zu responding short addresses, %zu unreliable, %s missing",
      aShortAddressListPtr ? aShortAddressListPtr->size() : 0,
      aUnreliableShortAddressListPtr ? aUnreliableShortAddressListPtr->size() : 0,
      missingAddrs ? "SOME" : "none"
    );
    // Strategy:
    // - when short scan reports devices with no short address, trigger a random binary serach FOR THOSE ONLY (case: new devices)
    // - when short scan reports another error: just report back, UNLESS unconditional full scan is requested
    if (!missingAddrs && mFullScanOnlyIfNeeded) {
      // not enough reason for triggering a random search, with or without error
      mFoundDevicesPtr = aShortAddressListPtr; // but return those that are reliable
      completed(aError);
      return;
    }
    // save the short address list
    mUsedShortAddrsPtr = aShortAddressListPtr;
    mConflictedShortAddrsPtr = aUnreliableShortAddressListPtr;
    if (!mFullScanOnlyIfNeeded) {
      SOLOG(mDaliComm, LOG_WARNING, "unconditional full bus scan (random address binary search) for ALL devices requested, will reassign conflicting short addresses.");
    }
    else {
      mFoundDevicesPtr = aShortAddressListPtr; // use the already addressed devices
      SOLOG(mDaliComm, LOG_WARNING, "bus scan (random address binary search) for devices without shortaddr - NO existing addresses will be reassigned.");
    }
    // Terminate any special modes first
    mDaliComm.daliSend(DALICMD_TERMINATE, 0x00);
    // initialize entire system for random address selection process. Unless full scan explicitly requested, only unaddressed devices will be included
    mDaliComm.daliSendTwice(DALICMD_INITIALISE, mFullScanOnlyIfNeeded ? 0xFF : 0x00, NoOP, 100*MilliSecond); // 0xFF = only those w/o short address
    mDaliComm.daliSendTwice(DALICMD_RANDOMISE, 0x00, NoOP, 100*MilliSecond);
    // start search at lowest address
    mRestarts = 0;
    // - as specs say DALICMD_RANDOMISE might need 100mS until new random addresses are ready, wait a little before actually starting
    mDelayTicket.executeOnce(boost::bind(&DaliFullBusScanner::newSearchUpFrom, this, 0), 150*MilliSecond);
  };


  bool isShortAddressInList(DaliAddress aShortAddress, DaliComm::ShortAddressListPtr aShortAddressList)
  {
    if (!aShortAddressList)
      return true; // no info, consider all used as we don't know
    for (DaliComm::ShortAddressList::iterator pos = aShortAddressList->begin(); pos!=aShortAddressList->end(); ++pos) {
      if (aShortAddress==(*pos))
        return true;
    }
    return false;
  }


  /// get new unused short address (but does NOT yet add it to any address list)
  /// Note: do not forget to EXPLICITLY add newly programmed addresses to
  ///   mUsedShortAddrsPtr and mFoundDevicesPtr) after a new address is actually
  ///   programmed and verified
  DaliAddress newShortAddress()
  {
    DaliAddress newAddr = DALI_MAXDEVICES;
    while (newAddr>0) {
      newAddr--;
      if (!isShortAddressInList(newAddr,mUsedShortAddrsPtr) && !isShortAddressInList(newAddr, mConflictedShortAddrsPtr)) {
        // this one is free and COULD be used
        return newAddr;
      }
    }
    // return NoDaliAddress if none available
    return NoDaliAddress;
  }


  void newSearchUpFrom(uint32_t aMinSearch)
  {
    // init search range
    mSearchMax = 0xFFFFFF;
    mSearchMin = aMinSearch;
    mLastSearchMin = aMinSearch;
    mSearchAddr = (mSearchMax-aMinSearch)/2+aMinSearch; // start in the middle
    // no search address currently set
    mSetLMH = true;
    mCompareRepeat = 0;
    compareNext();
  }


  void compareNext()
  {
    // issue next compare command
    // - update address bytes as needed (only those that have changed)
    uint8_t by = (mSearchAddr>>16) & 0xFF;
    if (by!=mSearchH || mSetLMH) {
      mSearchH = by;
      mDaliComm.daliSend(DALICMD_SEARCHADDRH, mSearchH);
    }
    // - searchM
    by = (mSearchAddr>>8) & 0xFF;
    if (by!=mSearchM || mSetLMH) {
      mSearchM = by;
      mDaliComm.daliSend(DALICMD_SEARCHADDRM, mSearchM);
    }
    // - searchL
    by = (mSearchAddr) & 0xFF;
    if (by!=mSearchL || mSetLMH) {
      mSearchL = by;
      mDaliComm.daliSend(DALICMD_SEARCHADDRL, mSearchL);
    }
    mSetLMH = false; // incremental from now on until flag is set again
    // - issue the compare command
    mDaliComm.daliSendAndReceive(DALICMD_COMPARE, 0x00, boost::bind(&DaliFullBusScanner::handleCompareResult, this, _1, _2, _3));
  }


  void handleCompareResult(bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
  {
    // Anything received but timeout is considered a yes
    bool isYes = DaliComm::isYes(aNoOrTimeout, aResponse, aError, true);
    if (aError) {
      SOLOG(mDaliComm, LOG_ERR, "compare result error: %s -> aborted scan", aError->text());
      completed(aError); // other error, abort
      return;
    }
    mCompareRepeat++;
    SOLOG(mDaliComm, LOG_DEBUG, "DALICMD_COMPARE result #%d = %s, search=0x%06X, searchMin=0x%06X, searchMax=0x%06X", mCompareRepeat, isYes ? "Yes" : "No ", mSearchAddr, mSearchMin, mSearchMax);
    // repeat to make sure
    if (!isYes && mCompareRepeat<=MAX_COMPARE_REPEATS) {
      SOLOG(mDaliComm, LOG_DEBUG, "- not trusting compare NO result yet, retrying...");
      compareNext();
      return;
    }
    // any ballast has smaller or equal random address?
    if (isYes) {
      if (mCompareRepeat>1) {
        SOLOG(mDaliComm, LOG_DEBUG, "- got a NO in first attempt but now a YES in %d attempt! -> unreliable answers", mCompareRepeat);
      }
      // yes, there is at least one, max address is what we searched so far
      mSearchMax = mSearchAddr;
    }
    else {
      // none at or below current search
      if (mSearchMin==0xFFFFFF) {
        // already at max possible -> no more devices found
        SOLOG(mDaliComm, LOG_INFO, "No more devices");
        completed(ErrorPtr()); return;
      }
      mSearchMin = mSearchAddr+1; // new min
    }
    if (mSearchMin==mSearchMax && mSearchAddr==mSearchMin) {
      // found!
      SOLOG(mDaliComm, LOG_NOTICE, "- Found device at 0x%06X", mSearchAddr);
      // read current short address
      mReadShortAddrRepeat = 0;
      mDaliComm.daliSendAndReceive(DALICMD_QUERY_SHORT_ADDRESS, 0x00, boost::bind(&DaliFullBusScanner::handleShortAddressQuery, this, _1, _2, _3), READ_SHORT_ADDR_SEND_DELAY);
    }
    else {
      // not yet - continue
      mSearchAddr = mSearchMin + (mSearchMax-mSearchMin)/2;
      SOLOG(mDaliComm, LOG_DEBUG, "                            Next search=0x%06X, searchMin=0x%06X, searchMax=0x%06X", mSearchAddr, mSearchMin, mSearchMax);
      if (mSearchAddr>0xFFFFFF) {
        SOLOG(mDaliComm, LOG_WARNING, "- failed search");
        if (mRestarts<MAX_RESTARTS) {
          mRestarts++;
          SOLOG(mDaliComm, LOG_NOTICE, "- restarting search at address of last found device + 1 (%d/%d)", mRestarts, MAX_RESTARTS);
          newSearchUpFrom(mLastSearchMin);
          return;
        }
        else {
          return completed(Error::err<DaliCommError>(DaliCommError::DeviceSearch, "Binary search got out of range"));
        }
      }
      // issue next address' compare
      mCompareRepeat = 0;
      compareNext();
    }
  }


  void handleShortAddressQuery(bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
  {
    if (aError)
      return completed(aError);
    if (aNoOrTimeout) {
      // should not happen, but just retry
      SOLOG(mDaliComm, LOG_WARNING, "- Device at 0x%06X does not respond to DALICMD_QUERY_SHORT_ADDRESS", mSearchAddr);
      mReadShortAddrRepeat++;
      if (mReadShortAddrRepeat<=MAX_SHORTADDR_READ_REPEATS) {
        mDaliComm.daliSendAndReceive(DALICMD_QUERY_SHORT_ADDRESS, 0x00, boost::bind(&DaliFullBusScanner::handleShortAddressQuery, this, _1, _2, _3), READ_SHORT_ADDR_SEND_DELAY);
        return;
      }
      // should definitely not happen, probably bus error led to false device detection -> restart search after a while
      SOLOG(mDaliComm, LOG_WARNING, "- Device at 0x%06X did not respond to %d attempts of DALICMD_QUERY_SHORT_ADDRESS", mSearchAddr, MAX_SHORTADDR_READ_REPEATS+1);
      if (mRestarts<MAX_RESTARTS) {
        mRestarts++;
        SOLOG(mDaliComm, LOG_NOTICE, "- restarting complete scan after a delay (%d/%d)", mRestarts, MAX_RESTARTS);
        mDelayTicket.executeOnce(boost::bind(&DaliFullBusScanner::startScan, this), RESCAN_RETRY_DELAY);
        return;
      }
      else {
        return completed(Error::err<DaliCommError>(DaliCommError::DeviceSearch,  "Detected device does not respond to QUERY_SHORT_ADDRESS"));
      }
    }
    else {
      // response is short address in 0AAAAAA1 format or DALIVALUE_MASK (no adress)
      mNewAddress = NoDaliAddress; // none
      DaliAddress shortAddress = mNewAddress; // none
      bool needsNewAddress = false;
      if (aResponse==DALIVALUE_MASK) {
        // device has no short address yet, assign one
        needsNewAddress = true;
        mNewAddress = newShortAddress();
        SOLOG(mDaliComm, LOG_NOTICE, "- Device at 0x%06X has NO short address -> assigning new short address", mSearchAddr);
      }
      else {
        shortAddress = DaliComm::addressFromDaliResponse(aResponse);
        SOLOG(mDaliComm, LOG_INFO, "- Device at 0x%06X has short address: %d", mSearchAddr, shortAddress);
        // check for collisions
        if (isShortAddressInList(shortAddress, mFoundDevicesPtr)) {
          mNewAddress = newShortAddress();
          needsNewAddress = true;
          SOLOG(mDaliComm, LOG_NOTICE, "- Collision on short address %d -> assigning new short address", shortAddress);
        }
      }
      // check if we need to re-assign the short address
      if (needsNewAddress) {
        uint8_t addrProg;
        if (mNewAddress==NoDaliAddress) {
          // no more short addresses available
          SOLOG(mDaliComm, LOG_ERR, "Bus has too many devices, device 0x%06X cannot be assigned a new short address and will not be usable", mSearchAddr);
          addrProg = 0xFF; // programming 0xFF means NO address
        }
        else {
          SOLOG(mDaliComm, LOG_NOTICE, "- assigning new free short address %d to device", mNewAddress);
          addrProg = DaliComm::dali1FromAddress(mNewAddress)+1;
        }
        // new short address (or explicit no-address==DaliBroadcast) must be assigned
        mDaliComm.daliSend(DALICMD_PROGRAM_SHORT_ADDRESS, addrProg);
        mDaliComm.daliSendAndReceive(
          DALICMD_VERIFY_SHORT_ADDRESS, addrProg,
          boost::bind(&DaliFullBusScanner::handleNewShortAddressVerify, this, _1, _2, _3),
          1*Second // delay one second before querying for new short address
        );
      }
      else {
        // short address is ok as-is
        deviceFound(shortAddress);
      }
    }
  }


  void handleNewShortAddressVerify(bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
  {
    if (mNewAddress==NoDaliAddress || DaliComm::isYes(aNoOrTimeout, aResponse, aError, false)) {
      // address was deleted, not added in the first place (more than 64 devices)
      // OR real clean YES - new short address verified
      deviceFound(mNewAddress);
    }
    else {
      // short address verification failed
      SOLOG(mDaliComm, LOG_ERR, "Error - could not assign new short address %d", mNewAddress);
      deviceFound(NoDaliAddress); // not really a usable device, but withdraw it and continue searching
    }
  }


  void deviceFound(DaliAddress aShortAddress)
  {
    // store short address if real address
    // (if broadcast, means that this device is w/o short address because >64 devices are on the bus, or short address could not be programmed)
    if (aShortAddress!=NoDaliAddress) {
      if (!isShortAddressInList(aShortAddress, mFoundDevicesPtr)) {
        // prevent registering the same address twice (did happen before fixing bug with newShortAddress() 2024-06-27)
        SOLOG(mDaliComm, LOG_NOTICE, "- new short address %d programming verified", aShortAddress);
        mFoundDevicesPtr->push_back(aShortAddress);
      }
      // now is definitely in use - must alse be in mUsedShortAddrs
      if (!isShortAddressInList(aShortAddress, mUsedShortAddrsPtr)) {
        mUsedShortAddrsPtr->push_back(aShortAddress);
      }
    }
    // withdraw this device from further searches
    mDaliComm.daliSend(DALICMD_WITHDRAW, 0x00);
    // continue searching devices
    newSearchUpFrom(mSearchAddr+1);
  }


  void completed(ErrorPtr aError)
  {
    // terminate
    mDaliComm.daliSend(DALICMD_TERMINATE, 0x00);
    mDaliComm.endProcedure();
    FOCUSSOLOG(mDaliComm, "After scanBus complete: retriedWrites=%ld, retriedReads=%ld", mDaliComm.mRetriedWrites, mDaliComm.mRetriedReads);
    // callback
    mCallback(mFoundDevicesPtr, DaliComm::ShortAddressListPtr(), aError);
    // done, delete myself
    delete this;
  }
};


void DaliComm::daliFullBusScan(DaliBusScanCB aResultCB, bool aFullScanOnlyIfNeeded)
{
  if (isBusy()) { aResultCB(ShortAddressListPtr(), ShortAddressListPtr(), DaliComm::busyError()); return; }
  DaliFullBusScanner::fullBusScan(*this, aResultCB, aFullScanOnlyIfNeeded);
}



// MARK: - DALI memory access / device info reading

#define DALI_MAX_MEMREAD_RETRIES 3

class DaliMemoryReader : public P44Obj
{
  DaliComm &mDaliComm;
  DaliComm::DaliReadMemoryCB mCallback;
  DaliAddress mBusAddress;
  DaliComm::MemoryVectorPtr mMemory;
  int mBytesToRead;
  int mRetries;
  uint8_t mCurrentOffset;
public:
  static void readMemory(DaliComm &aDaliComm, DaliComm::DaliReadMemoryCB aResultCB, DaliAddress aAddress, uint8_t aBank, uint8_t aOffset, uint16_t aNumBytes, DaliComm::MemoryVectorPtr aMemory)
  {
    // create new instance, deletes itself when finished
    new DaliMemoryReader(aDaliComm, aResultCB, aAddress, aBank, aOffset, aNumBytes, aMemory);
  };
private:
  DaliMemoryReader(DaliComm &aDaliComm, DaliComm::DaliReadMemoryCB aResultCB, DaliAddress aAddress, uint8_t aBank, uint8_t aOffset, uint16_t aNumBytes, DaliComm::MemoryVectorPtr aMemory) :
    mDaliComm(aDaliComm),
    mCallback(aResultCB),
    mBusAddress(aAddress),
    mMemory(aMemory)
  {
    if (!mMemory) mMemory = DaliComm::MemoryVectorPtr(new DaliComm::MemoryVector);
    mDaliComm.startProcedure();
    SOLOG(mDaliComm, LOG_INFO, "bus address %d - reading %d bytes from bank %d at offset %d:", mBusAddress, aNumBytes, aBank, aOffset);
    // set DTR1 = bank
    mDaliComm.daliSend(DALICMD_SET_DTR1, aBank);
    // set initial offset and bytes
    mCurrentOffset = aOffset;
    mBytesToRead = aNumBytes;
    mRetries = 0;
    startReading();
  }


  void startReading()
  {
    // set DTR = offset within bank
    mDaliComm.daliSend(DALICMD_SET_DTR, mCurrentOffset);
    // start reading
    readNextByte();
  };


  // handle scan result
  void handleResponse(bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError, bool aRetried)
  {
    if (Error::notOK(aError)) {
      // error
      if (mRetries++<DALI_MAX_MEMREAD_RETRIES) {
        // restart reading explicitly at current offset
        startReading();
        return;
      }
      // report the error
    }
    else if (aNoOrTimeout) {
      // no response, means NO DATA
      mMemory->push_back(DaliComm::MemoryCell(0xFF, true));
      // restart at next location
      mCurrentOffset++;
      if (--mBytesToRead>0) {
        startReading();
        return;
      }
    }
    else {
      // even ok result must be retry-free, because retrying at bridge level might
      // have incremented the read position.
      // If we have a retry, we need to re-set the DTR to the correct byte position first
      if (aRetried) {
        if (mRetries++<DALI_MAX_MEMREAD_RETRIES) {
          // restart reading explicitly at current offset
          startReading();
          return;
        }
        // multiple retries because of frame errors in a row must count as frame error
        // (even if bridge retry mechanism DID manage to get a error free answer in a retried read)
        aError = Error::err<DaliCommError>(DaliCommError::DALIFrame, "repeated frame error (bridge retry) reading byte at offset %u", (unsigned int)mCurrentOffset);
        // report the error
      }
      // byte received, append to vector
      mRetries = 0;
      mMemory->push_back(DaliComm::MemoryCell(aResponse));
      mCurrentOffset++;
      if (--mBytesToRead>0) {
        // more bytes to read
        readNextByte();
        return;
      }
    }
    // read done or error, return memory to callback
    mDaliComm.endProcedure();
    if (LOGENABLED(LOG_INFO)) {
      // dump data
      int o=0;
      for (DaliComm::MemoryVector::iterator pos = mMemory->begin(); pos!=mMemory->end(); ++pos, ++o) {
        if (pos->no) {
          SOLOG(mDaliComm, LOG_INFO, "- %03d/0x%02X : NO (timeout)", o, o);
        }
        else {
          SOLOG(mDaliComm, LOG_INFO, "- %03d/0x%02X : 0x%02X/%03d", o, o, pos->b, pos->b);
        }
      }
    }
    mCallback(mMemory, aError);
    // done, delete myself
    delete this;
  };

  void readNextByte()
  {
    mDaliComm.daliSendQuery(mBusAddress, DALICMD_READ_MEMORY_LOCATION, boost::bind(&DaliMemoryReader::handleResponse, this, _1, _2, _3, _4));
  }
};


void DaliComm::daliReadMemory(DaliReadMemoryCB aResultCB, DaliAddress aAddress, uint8_t aBank, uint8_t aOffset, uint16_t aNumBytes, DaliComm::MemoryVectorPtr aMemory)
{
  if (isBusy()) { aResultCB(MemoryVectorPtr(), DaliComm::busyError()); return; }
  DaliMemoryReader::readMemory(*this, aResultCB, aAddress, aBank, aOffset, aNumBytes, aMemory);
}


// MARK: - DALI device info reading

#define DALI_MAX_BANKREAD_RETRIES 3 // how many times reading bank will be tried in case of checksum error
#define DALI2_MAX_BANK0_REREADS 3 // re-read 3 times at most

class DaliDeviceInfoReader : public P44Obj
{
  DaliComm &mDaliComm;
  DaliComm::DaliDeviceInfoCB mCallback;
  DaliAddress mBusAddress;
  DaliDeviceInfoPtr mDeviceInfo;
  uint8_t mBankChecksum;
  uint8_t mMaxBank;
  bool mDali2;
  int mRetries;
public:
  static void readDeviceInfo(DaliComm &aDaliComm, DaliComm::DaliDeviceInfoCB aResultCB, DaliAddress aAddress)
  {
    // create new instance, deletes itself when finished
    new DaliDeviceInfoReader(aDaliComm, aResultCB, aAddress);
  };
private:
  DaliDeviceInfoReader(DaliComm &aDaliComm, DaliComm::DaliDeviceInfoCB aResultCB, DaliAddress aAddress) :
    mDaliComm(aDaliComm),
    mCallback(aResultCB),
    mBusAddress(aAddress)
  {
    mDaliComm.startProcedure();
    mRetries = 0;
    readVersion();
  }

  void readVersion()
  {
    mDali2 = false;
    mDeviceInfo.reset(new DaliDeviceInfo);
    mDeviceInfo->mShortAddress = mBusAddress;
    mDeviceInfo->mDevInfStatus = DaliDeviceInfo::devinf_none; // no info yet
    mDaliComm.daliSendQuery(mBusAddress, DALICMD_QUERY_VERSION_NUMBER, boost::bind(&DaliDeviceInfoReader::handleVersion, this, _1, _2, _3));
    return;
  }

  void handleVersion(bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
  {
    if (Error::isOK(aError) && !aNoOrTimeout) {
      if (aResponse==1) {
        // IEC 62386-102:2009 says the response to QUERY_VERSION_NUMBER is 1 (meaning DALI 1.0)
        mDeviceInfo->mVers_102 = DALI_STD_VERS_BYTE(1, 0);
      }
      else {
        // IEC 62386-102:2014 says the response is content of bank0 offset 0x16 (6 bits major, 2 bits minor version)
        mDeviceInfo->mVers_102 = DALI_STD_VERS_NONEIS0(aResponse);
      }
      SOLOG(mDaliComm, LOG_INFO,"device at short address %d has DALI Version %d.%d", mBusAddress, DALI_STD_VERS_MAJOR(mDeviceInfo->mVers_102), DALI_STD_VERS_MINOR(mDeviceInfo->mVers_102));
      mDali2 = mDeviceInfo->mVers_102>=DALI_STD_VERS_BYTE(2, 0);
      if (mDali2 && mDaliComm.mDali2ScanLock) {
        // this looks like a DALI 2 device, but scanning is locked to ensure backwards compatibility
        mDali2 = false;
        SOLOG(mDaliComm, LOG_WARNING, "short address %d is a DALI 2 device, but DALI 2 serialno scanning is disabled for backwards compatibility. Force-Rescan to enable DALI 2 scanning", mBusAddress);
      }
    }
    else {
      SOLOG(mDaliComm, LOG_WARNING, "short address %d did not respond to QUERY_VERSION_NUMBER -> probably bad", mBusAddress);
    }
    readBank0();
  }


  void readBank0()
  {
    // read the memory
    mMaxBank = 0; // all devices must have a bank 0, rest is optional
    // Note: official checksum algorithm is: 0-byte2-byte3...byteLast, check with checksum+byte2+byte3...byteLast==0
    mBankChecksum = 0;
    DaliMemoryReader::readMemory(mDaliComm, boost::bind(&DaliDeviceInfoReader::handleBank0Header, this, _1, _2), mBusAddress, 0, 0, DALIMEM_BANK_HDRBYTES, DaliComm::MemoryVectorPtr());
  };


  void handleBank0Header(DaliComm::MemoryVectorPtr aBank0Data, ErrorPtr aError)
  {
    if (aError) {
      return complete(aError);
    }
    if (!(*aBank0Data)[2].no) {
      mMaxBank = (*aBank0Data)[2].b; // this is the highest bank number implemented in this device
      SOLOG(mDaliComm, LOG_INFO, "- highest available DALI memory bank = %d", mMaxBank);
    }
    if ((*aBank0Data)[0].no) {
      return complete(Error::err<DaliCommError>(DaliCommError::MissingData, "bank0 at short address %d does not exist", mBusAddress));
    }
    uint16_t n = (*aBank0Data)[0].b+1; // last available location + 1 = number of bytes in bank 0
    SOLOG(mDaliComm, LOG_INFO, "- number of bytes available in bank0 = %d/0x%x", n, n);
    if (n<DALIMEM_BANK0_MINBYTES || (mDali2 && n<DALIMEM_BANK0_MINBYTES_v2_0)) {
      return complete(Error::err<DaliCommError>(DaliCommError::MissingData, "bank0 at short address %d has not enough bytes (%d, min=%d)", mBusAddress, n, DALIMEM_BANK0_MINBYTES));
    }
    // no need to read more than what we need for dali2, because there's no checksum, anyway
    if (mDali2 && n>DALIMEM_BANK0_MINBYTES_v2_0) n = DALIMEM_BANK0_MINBYTES_v2_0;
    // append actual bank contents
    DaliMemoryReader::readMemory(mDaliComm, boost::bind(&DaliDeviceInfoReader::handleBank0Data, this, _1, _2), mBusAddress, 0, DALIMEM_BANK_HDRBYTES, n-DALIMEM_BANK_HDRBYTES, aBank0Data);
    return;
  }


  void handleBank0Verify(DaliComm::MemoryVectorPtr aOriginalBank0Data, DaliComm::MemoryVectorPtr aGTINSerialAgain, ErrorPtr aError)
  {
    if (Error::isOK(aError)) {
      for (int i=DALIMEM_BANK_HDRBYTES; i<=0x12; i++) {
        if ((*aOriginalBank0Data)[i].b!=(*aGTINSerialAgain)[i-DALIMEM_BANK_HDRBYTES].b) {
          if (mRetries++<DALI2_MAX_BANK0_REREADS) {
            // try again
            SOLOG(mDaliComm, LOG_WARNING, "DALI2 bank 0 GTIN/Serial bytes differ when re-read at offset 0x%02x at bus address %d -> retrying", i, mBusAddress);
            readBank0();
            return;
          }
          // to many retries
          SOLOG(mDaliComm, LOG_ERR, "short address %d Bank 0 cannot be read reliably", mBusAddress);
          return complete(Error::err<DaliCommError>(DaliCommError::BadData, "DALI memory bank 0 cannot be reliably read at short address %d", mBusAddress));
        }
      }
      // verified, make sure we don't re-read
      mRetries = DALI2_MAX_BANK0_REREADS; // no more retries
    }
    handleBank0Data(aOriginalBank0Data, aError);
  }


  void handleBank0Data(DaliComm::MemoryVectorPtr aBank0Data, ErrorPtr aError)
  {
    if (aError) {
      return complete(aError);
    }
    if (mDali2) {
      if (mRetries<DALI2_MAX_BANK0_REREADS) {
        // DALI2: no checksum, so read crucial bytes (GTIN, serial) again to make sure
        DaliMemoryReader::readMemory(mDaliComm, boost::bind(&DaliDeviceInfoReader::handleBank0Verify, this, aBank0Data, _1, _2), mBusAddress, 0, DALIMEM_BANK_HDRBYTES, 0x12+1-DALIMEM_BANK_HDRBYTES, DaliComm::MemoryVectorPtr());
        return;
      }
      // offset 1 should be NO
      if (!(*aBank0Data)[1].no) {
        SOLOG(mDaliComm, LOG_WARNING, "DALI2 bank 0 should return NO for offset 1, but returned 0x%x at bus address %d", (*aBank0Data)[1].b, mBusAddress);
      }
    }
    else {
      // DALI 1: sum up starting with checksum (offset 0x01) itself, result must be 0x00 in the end
      for (int i=0x01; i<aBank0Data->size(); i++) {
        mBankChecksum += (*aBank0Data)[i].b;
      }
      if (mBankChecksum!=0x00) {
        // - check retries
        if (mRetries++<DALI_MAX_BANKREAD_RETRIES) {
          // retry reading bank 0 info
          SOLOG(mDaliComm, LOG_INFO, "Checksum wrong (0x%02X!=0x00) in %d. attempt to read bank0 info from short address %d -> retrying", mBankChecksum, mRetries, mBusAddress);
          readBank0();
          return;
        }
        // - report error
        SOLOG(mDaliComm, LOG_ERR, "short address %d Bank 0 checksum is wrong - should sum up to 0x00, actual sum is 0x%02X", mBusAddress, mBankChecksum);
        return complete(Error::err<DaliCommError>(DaliCommError::BadData, "bad DALI memory bank 0 checksum at short address %d", mBusAddress));
      }
    }
    // we have verified (re-read or checksummed) data
    mDeviceInfo->mDevInfStatus = DaliDeviceInfo::devinf_solid; // assume solid info present
    // get standard version if available
    if (mDali2 && aBank0Data->size()>=DALIMEM_BANK0_MINBYTES_v2_0) {
      // we have DALI2 with standard version numbers
      // Note: vers_102 decides about scanning for DALI2 infos at all and is retrieved with QUERY_VERSION_NUMBER above, not saved again here!
      mDeviceInfo->mVers_101 = DALI_STD_VERS_NONEIS0((*aBank0Data)[0x15].b);
      mDeviceInfo->mVers_103 = DALI_STD_VERS_NONEIS0((*aBank0Data)[0x17].b);
      // save logical unit index within single device
      mDeviceInfo->mLunIndex = (*aBank0Data)[0x1A].b;
    }
    // check plausibility of GTIN/Version/SN data
    // Know bad signatures we must catch:
    // - Meanwell:
    //   all 01 or 05
    // - linealight.com/i-LÈD/eral LED-FGI332:
    //   71 01 01 FF 02 FF FF FF 01 4B 00 00 FF FF (6*FF, 3 of them consecutive, unfortunately gtin checkdigit by accident ok)
    uint8_t refByte = 0;
    uint8_t numSame = 1; // we always have one "consecutive" number of bytes
    uint8_t numFFs = 0;
    uint8_t maxSame = 0;
    uint8_t sameByte = 0;
    int idLastByte = mDali2 ? 0x12 : 0x0E; // >=2.0 has longer ID field
    for (int i=0x03; i<=idLastByte; i++) {
      if ((*aBank0Data)[i].no) {
        SOLOG(mDaliComm, LOG_ERR, "short address %d Bank 0 has missing byte (timeout) at offset 0x%02X -> invalid GTIN/Serial data", mBusAddress, i);
        mDeviceInfo->mDevInfStatus = DaliDeviceInfo::devinf_none; // consider invalid
        break;
      }
      uint8_t b = (*aBank0Data)[i].b;
      if(b==0xFF) {
        numFFs++; // count 0xFFs as suspect values
      }
      if(b==refByte) {
        numSame++; // count consecutively equal bytes
        if (numSame>maxSame) {
          maxSame = numSame;
          sameByte = b;
        }
      }
      else {
        refByte = b;
        numSame = 1; // first byte in a possible row of "consecutive" ones = the reference byte
      }
    }
    if (maxSame>=10 || (numFFs>=6 && maxSame>=3)) {
      // this is tuned heuristics: >=6 FFs total plus >=3 consecutive equal non-zeros are considered suspect (because linealight.com/i-LÈD/eral LED-FGI332 has that)
      SOLOG(mDaliComm, LOG_ERR, "short address %d Bank 0 has %d consecutive bytes of 0x%02X and %d bytes of 0xFF  - indicates invalid GTIN/Serial data -> ignoring", mBusAddress, maxSame, sameByte, numFFs);
      mDeviceInfo->mDevInfStatus = DaliDeviceInfo::devinf_none; // consider invalid
    }
    // GTIN: bytes 0x03..0x08, MSB first
    // All 0xFF is official DALI for "no GTIN"
    mDeviceInfo->mGtin = 0;
    bool allFF = true;
    uint8_t b;
    for (int i=0x03; i<=0x08; i++) {
      b = (*aBank0Data)[i].b;
      if (b!=0xFF) allFF = false;
      if ((*aBank0Data)[i].no) b = 0; // consider missing bytes as zeroes
      mDeviceInfo->mGtin = (mDeviceInfo->mGtin << 8) + b;
    }
    if (allFF) mDeviceInfo->mGtin = 0; // all FF means no GTIN
    // Firmware version
    mDeviceInfo->mFwVersionMajor = (*aBank0Data)[0x09].b;
    mDeviceInfo->mFwVersionMinor = (*aBank0Data)[0x0A].b;
    // Serial: bytes 0x0B..0x0E for <DALI 2.0
    //         bytes 0x0B..0x12 for >=DALI 2.0
    mDeviceInfo->mSerialNo = 0;
    allFF = true;
    int serialLastByte = mDali2 ? 0x12 : 0x0E; // 2.0 has longer ID field
    for (int i=0x0B; i<=serialLastByte; i++) {
      b = (*aBank0Data)[i].b;
      if (b!=0xFF) allFF = false;
      if ((*aBank0Data)[i].no) b = 0; // consider missing bytes as zeroes
      mDeviceInfo->mSerialNo = (mDeviceInfo->mSerialNo << 8) + b;
    }
    if (allFF) mDeviceInfo->mSerialNo = 0; // all FF means no serial number
    // now some more plausibility checks at the GTIN/serial level
    if (mDeviceInfo->mGtin==0) {
      mDeviceInfo->mDevInfStatus = DaliDeviceInfo::devinf_none; // no usable GTIN, consider devinf invalid
    }
    else if (gtinCheckDigit(mDeviceInfo->mGtin)!=0) {
      // invalid GTIN
      SOLOG(mDaliComm, LOG_ERR, "short address %d has invalid GTIN=%lld/0x%llX (wrong check digit) -> ignoring", mBusAddress, mDeviceInfo->mGtin, mDeviceInfo->mGtin);
      mDeviceInfo->mDevInfStatus = DaliDeviceInfo::devinf_none; // consider invalid
    }
    else {
      // GTIN by itself looks ok
      if (mDeviceInfo->mDevInfStatus==DaliDeviceInfo::devinf_solid) {
        // GTIN has not been ruled out before -> seems valid
        int i=0;
        while (DALI_GTIN_blacklist[i]!=0) {
          if (mDeviceInfo->mGtin==DALI_GTIN_blacklist[i]) {
            // found in blacklist, invalidate serial
            SOLOG(mDaliComm, LOG_ERR, "GTIN %lld of DALI short address %d is blacklisted because it is known to have invalid serial -> invalidating serial", mDeviceInfo->mGtin, mBusAddress);
            mDeviceInfo->mSerialNo = 0; // reset, make invalid for check below
            break;
          }
          i++;
        }
      }
      if (mDeviceInfo->mSerialNo==0) {
        if (mDeviceInfo->mDevInfStatus==DaliDeviceInfo::devinf_solid) {
          // if everything else is ok, except for a missing serial number, consider GTIN valid
          mDeviceInfo->mDevInfStatus = DaliDeviceInfo::devinf_only_gtin;
          SOLOG(mDaliComm, LOG_WARNING, "short address %d has no serial number but valid GTIN -> just using GTIN", mBusAddress);
        }
        else {
          // was not solid before, consider completely invalid
          mDeviceInfo->mDevInfStatus = DaliDeviceInfo::devinf_none;
        }
      }
    }
    // Note: before 2015-02-27, we had a bug which caused the last extra byte not being read, so the checksum reached zero
    // only if the last byte was 0. We also passed the if checksum was 0xFF, because our reference devices always had 0x01 in
    // the last byte, and I assumed missing by 1 was the result of not precise enough specs or a bug in the device.
    #if OLD_BUGGY_CHKSUM_COMPATIBLE
    if (!dali2 && bankChecksum==0 && deviceInfo->mDevInfStatus==DaliDeviceInfo::devinf_solid) {
      // by specs, this is a correct checksum, and a seemingly solid device info
      // - now check if the buggy checker would have passed it, too (which is when last byte is 0x01 or 0x00)
      uint8_t lastByte = (*aBank0Data)[aBank0Data->size()-1].b;
      if (lastByte!=0x00 && lastByte!=0x01) {
        // this bank 0 data would not have passed the buggy checker
        deviceInfo->mDevInfStatus = DaliDeviceInfo::devinf_maybe; // might be usable to identify device, but needs backwards compatibility checking
      }
    }
    #endif
    // done with bank0
    if (mMaxBank>0) {
      // now read OEM info from bank1
      mRetries = 0;
      readBank1();
    }
    else {
      // device does not have bank1, so we are complete
      return complete(ErrorPtr());
    }
  };



  void readBank1()
  {
    mBankChecksum = 0;
    DaliMemoryReader::readMemory(mDaliComm, boost::bind(&DaliDeviceInfoReader::handleBank1Header, this, _1, _2), mBusAddress, 1, 0, DALIMEM_BANK_HDRBYTES, DaliComm::MemoryVectorPtr());
  }


  void handleBank1Header(DaliComm::MemoryVectorPtr aBank1Data, ErrorPtr aError)
  {
    if (aError) {
      return complete(aError);
    }
    if ((*aBank1Data)[0].no) {
      return complete(Error::err<DaliCommError>(DaliCommError::MissingData, "bank1 at short address %d has not enough header bytes", mBusAddress));
    }
    // valid byte count
    uint16_t n = (*aBank1Data)[0].b+1; // last available location + 1 = number of bytes in bank 1
    SOLOG(mDaliComm, LOG_INFO, "- number of bytes available in bank1 = %d/0x%X", n, n);
    if (n<DALIMEM_BANK1_MINBYTES) {
      return complete(Error::err<DaliCommError>(DaliCommError::MissingData, "bank1 at short address %d has not enough bytes (%d, min=%d)", mBusAddress, n, DALIMEM_BANK1_MINBYTES));
    }
    // append actual bank contents
    DaliMemoryReader::readMemory(mDaliComm, boost::bind(&DaliDeviceInfoReader::handleBank1Data, this, _1, _2), mBusAddress, 1, DALIMEM_BANK_HDRBYTES, n-DALIMEM_BANK_HDRBYTES, aBank1Data);
    return;
  }


  void handleBank1Data(DaliComm::MemoryVectorPtr aBank1Data, ErrorPtr aError)
  {
    if (aError) {
      return complete(aError);
    }
    if (aBank1Data->size()>=DALIMEM_BANK1_MINBYTES) {
      // sum up starting with checksum itself, result must be 0x00 in the end
      if (!mDali2) {
        // only DALI 1 does have checksums
        for (int i=0x01; i<aBank1Data->size(); i++) {
          mBankChecksum += (*aBank1Data)[i].b;
        }
        if (mBankChecksum!=0x00) {
          // - check retries
          if (mRetries++<DALI_MAX_BANKREAD_RETRIES) {
            // retry reading bank 1 info
            SOLOG(mDaliComm, LOG_INFO, "Checksum wrong (0x%02X!=0x00) in %d. attempt to read bank1 info from short address %d -> retrying", mBankChecksum, mRetries, mBusAddress);
            readBank1();
            return;
          }
          // - report error
          SOLOG(mDaliComm, LOG_ERR, "short address %d Bank 1 checksum is wrong - should sum up to 0x00, actual sum is 0x%02X", mBusAddress, mBankChecksum);
          return complete(Error::err<DaliCommError>(DaliCommError::BadData, "bad DALI memory bank 1 checksum at short address %d", mBusAddress));
        }
      }
      // OEM GTIN: bytes 0x03..0x08, MSB first
      mDeviceInfo->mOemGtin = 0;
      bool allFF = true;
      uint8_t b;
      for (int i=0x03; i<=0x08; i++) {
        b = (*aBank1Data)[i].b;
        if (b!=0xFF) allFF = false;
        if ((*aBank1Data)[i].no) b = 0; // consider missing bytes as zeroes
        mDeviceInfo->mOemGtin = (mDeviceInfo->mOemGtin << 8) + b;
      }
      if (allFF) mDeviceInfo->mOemGtin = 0; // all FF means no OEM GTIN
      // OEM Serial: bytes 0x09..0x0C for <DALI 2.0
      //             bytes 0x09..0x10 for >=DALI 2.0
      allFF = true;
      int serialLastByte = mDali2 ? 0x10 : 0x0C; // >=2.0 has longer ID field
      for (int i=0x0B; i<=serialLastByte; i++) {
        b = (*aBank1Data)[i].b;
        if (b!=0xFF) allFF = false;
        if ((*aBank1Data)[i].no) b = 0; // consider missing bytes as zeroes
        mDeviceInfo->mOemSerialNo = (mDeviceInfo->mOemSerialNo << 8) + b;
      }
      if (allFF) mDeviceInfo->mOemSerialNo = 0; // all FF means no OEM GTIN
      // done with bank1
      return complete(aError);
    }
  };


  void complete(ErrorPtr aError)
  {
    mDaliComm.endProcedure();
    if (Error::isOK(aError)) {
      SOLOG(mDaliComm, LOG_NOTICE,
        "Successfully read DALI%d device info %sfrom short address %d - %s data: GTIN=%llu, Serial=%llu, LUN=%d",
        mDeviceInfo->mVers_102>=DALI_STD_VERS_BYTE(2, 0) ? 2 : 1,
        mDali2 ? "" : "in DALI1 mode ",
        mBusAddress,
        mDeviceInfo->mDevInfStatus==DaliDeviceInfo::devinf_solid
        #if OLD_BUGGY_CHKSUM_COMPATIBLE
        || deviceInfo->mDevInfStatus==DaliDeviceInfo::devinf_maybe
        #endif
        ? "valid" : "UNRELIABLE",
        mDeviceInfo->mGtin,
        mDeviceInfo->mSerialNo,
        mDeviceInfo->mLunIndex
      );
    }
    // clean device info in case it has been detected invalid by now
    if (mDeviceInfo->mDevInfStatus==DaliDeviceInfo::devinf_none) {
      mDeviceInfo->clear(); // clear everything except shortaddress
    }
    else if (mDeviceInfo->mDevInfStatus==DaliDeviceInfo::devinf_only_gtin) {
      // consider serial numbers invalid, but GTIN and version ok
      mDeviceInfo->mSerialNo = 0;
      mDeviceInfo->mOemSerialNo = 0;
    }
    // report
    mCallback(mDeviceInfo, aError);
    // done, delete myself
    delete this;
  }
};


void DaliComm::daliReadDeviceInfo(DaliDeviceInfoCB aResultCB, DaliAddress aAddress)
{
  if (isBusy()) { aResultCB(DaliDeviceInfoPtr(), DaliComm::busyError()); return; }
  DaliDeviceInfoReader::readDeviceInfo(*this, aResultCB, aAddress);
}


// MARK: - DALI device info


DaliDeviceInfo::DaliDeviceInfo()
{
  clear();
  mVers_102 = 0; // clear() does not reset this, as it is not really part of devInf (but separately retrieved with QUERY_VERSION_NUMBER)
  mShortAddress = NoDaliAddress; // undefined short address
}


void DaliDeviceInfo::clear()
{
  // clear everything except short address
  mVers_101 = 0; // unknown
  // do not clear vers_102, because this is retrieved with QUERY_VERSION_NUMBER, and should be ok even if devInf is not
  mVers_103 = 0; // unknown
  mGtin = 0;
  mFwVersionMajor = 0;
  mFwVersionMinor = 0;
  mSerialNo = 0;
  mLunIndex = 0;
  mOemGtin = 0;
  mOemSerialNo = 0;
  mDevInfStatus = devinf_none;
}


void DaliDeviceInfo::invalidateSerial()
{
  // reduce devinf to state that does not allow to base a dSUID on
  // - assume serials are garbage/not unique -> nobody should ever see them
  mSerialNo = 0;
  mOemSerialNo = 0;
  // - make sure status gets downgraded (but keep other info such as GTIN which is likely valid)
  if (mDevInfStatus>=devinf_only_gtin) {
    mDevInfStatus = devinf_only_gtin;
  }
}



string DaliDeviceInfo::description()
{
  string s = string_format("\n- DaliDeviceInfo for %s", DaliComm::formatDaliAddress(mShortAddress).c_str());
  string_format_append(s, "\n  - is %suniquely defining the device", mDevInfStatus==devinf_solid ? "" : "NOT ");
  string_format_append(s, "\n  - GTIN       : %llu", mGtin);
  string_format_append(s, "\n  - Serial     : %llu", mSerialNo);
  string_format_append(s, "\n  - LUN index  : %d", mLunIndex);
  string_format_append(s, "\n  - OEM GTIN   : %llu", mOemGtin);
  string_format_append(s, "\n  - OEM Serial : %llu", mOemSerialNo);
  string_format_append(s, "\n  - Firmware   : %d.%d", mFwVersionMajor, mFwVersionMinor);
  string_format_append(s, "\n  - DALI vers  : 101:%d.%d, 102:%d.%d, 103:%d.%d",
    DALI_STD_VERS_MAJOR(mVers_101), DALI_STD_VERS_MINOR(mVers_101),
    DALI_STD_VERS_MAJOR(mVers_102), DALI_STD_VERS_MINOR(mVers_102),
    DALI_STD_VERS_MAJOR(mVers_103), DALI_STD_VERS_MINOR(mVers_103)
  );
  return s;
}

#endif // ENABLE_DALI






