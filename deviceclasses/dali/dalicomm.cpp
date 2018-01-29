//
//  Copyright (c) 2013-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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


// pseudo baudrate for dali bridge must be 9600bd
#define DALIBRIDGE_COMMPARAMS "9600,8,N,1"

// default sending and sampling adjustment values
#define DEFAULT_SENDING_EDGE_ADJUSTMENT 16 // one step (1/16th = 16/256th DALI bit time) delay of rising edge by default is probably better
#define DEFAULT_SAMPLING_POINT_ADJUSTMENT 0


DaliComm::DaliComm(MainLoop &aMainLoop) :
	inherited(aMainLoop),
  runningProcedures(0),
  closeAfterIdleTime(Never),
  connectionTimeoutTicket(0),
  expectedBridgeResponses(0),
  responsesInSequence(false),
  sendEdgeAdj(DEFAULT_SENDING_EDGE_ADJUSTMENT),
  samplePointAdj(DEFAULT_SAMPLING_POINT_ADJUSTMENT)
{
  // serialqueue needs a buffer as we use NOT_ENOUGH_BYTES mechanism
  setAcceptBuffer(21); // actually min 3 bytes for EVENT_CODE_FOREIGN_FRAME
}


DaliComm::~DaliComm()
{
}


// MARK: ===== GTIN blacklist for ill-behaving devices


const long long DALI_GTIN_blacklist[] = {
  4052899919433ll, // OTi DALI 50/220…240/1A4 LT2 FAN - has garbage serial no, many duplicates!
  0 // terminator
};



// MARK: ===== procedure management

void DaliComm::startProcedure()
{
  ++runningProcedures;
}

void DaliComm::endProcedure()
{
  if (runningProcedures>0)
    --runningProcedures;
}


bool DaliComm::isBusy()
{
  return runningProcedures>0;
}




// MARK: ===== DALI bridge low level communication


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
  closeAfterIdleTime = aCloseAfterIdleTime;
  serialComm->setConnectionSpecification(aConnectionSpec, aDefaultPort, DALIBRIDGE_COMMPARAMS);
}


void DaliComm::bridgeResponseHandler(DaliBridgeResultCB aBridgeResultHandler, SerialOperationReceivePtr aOperation, ErrorPtr aError)
{
  if (expectedBridgeResponses>0) expectedBridgeResponses--;
  if (expectedBridgeResponses<BUFFERED_BRIDGE_RESPONSES_LOW) {
    responsesInSequence = false; // allow buffered sends without waiting for answers again
  }
  // get received data
  if (Error::isOK(aError) && aOperation && aOperation->getDataSize()>=2) {
    uint8_t resp1 = aOperation->getDataP()[0];
    uint8_t resp2 = aOperation->getDataP()[1];
    if (resp1==RESP_CODE_DATA || resp1==RESP_CODE_DATA_RETRIED) {
      FOCUSLOG("DALI bridge response: DATA            (%02X)      %02X    - %d pending responses%s", resp1, resp2, expectedBridgeResponses, resp1==RESP_CODE_DATA_RETRIED ? ", RETRIED" : "");

    }
    else {
      FOCUSLOG("DALI bridge response: %s (%02X %02X)         - %d pending responses%s", bridgeAckText(resp1, resp2), resp1, resp2, expectedBridgeResponses, resp1==RESP_CODE_ACK_RETRIED ? ", RETRIED" : "");
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
  MainLoop::currentMainLoop().cancelExecutionTicket(connectionTimeoutTicket);
  if (closeAfterIdleTime!=Never) {
    connectionTimeoutTicket = MainLoop::currentMainLoop().executeOnce(boost::bind(&DaliComm::connectionTimeout, this), closeAfterIdleTime);
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
  expectedBridgeResponses++;
  if (aWithDelay>0) {
    // delayed sends must always be in sequence (always leave recOp->inSequence at its default, true)
    sendOp->setInitiationDelay(aWithDelay);
    FOCUSLOG("DALI bridge command:  %s (%02X)      %02X %02X - %d pending responses - to be sent in %d µS after no response pending", bridgeCmdName(aCmd), aCmd, aDali1, aDali2, expectedBridgeResponses, aWithDelay);
  }
  else {
    // non-delayed sends may be sent before answer of previous commands have arrived as long as Rx (9210) or Tx (p44dbr) buf in bridge does not overflow
    if (expectedBridgeResponses>BUFFERED_BRIDGE_RESPONSES_HIGH) {
      responsesInSequence = true; // prevent further sends without answers
    }
    recOp->inSequence = responsesInSequence;
    FOCUSLOG("DALI bridge command:  %s (%02X)      %02X %02X - %d pending responses - %s", bridgeCmdName(aCmd), aCmd, aDali1, aDali2, expectedBridgeResponses, responsesInSequence ? "sent when no more responses pending" : "sent as soon as possible");
  }
  recOp->setTimeout(20*Second); // large timeout, because it can really take time until all expected answers are received
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
  serialComm->closeConnection();
}

// MARK: ===== DALI bus communication basics


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
          return ErrorPtr(new DaliCommError(DaliCommError::BusOverload));
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
    LOG(LOG_INFO, "DALI bridge event: 0x%02X 0x%02X 0x%02X from other master on bus", aBytes[0], aBytes[1], aBytes[2]);
    // invoke handler
    if (bridgeEventHandler) {
      bridgeEventHandler(aBytes[0], aBytes[1], aBytes[2]);
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
      FOCUSLOG("DALI bridge: received extra bytes (%s) -> bridge was apparently out of sync", binaryToHexString(b, ' ').c_str());
    }
    else {
      LOG(LOG_WARNING,"DALI bridge: received %zu extra bytes -> bridge was apparently out of sync", aNumBytes);
    }
    return aNumBytes;
  }
}





// reset the bridge

void DaliComm::reset(DaliCommandStatusCB aStatusCB)
{
  // this first reset command should also consume extra bytes left over from previous use use delay to make sure commands are NOT buffered and extra bytes from unsynced bridge will be catched here
  sendBridgeCommand(CMD_CODE_RESET, 0, 0, boost::bind(&DaliComm::resetIssued, this, aStatusCB, _1, _2, _3), 100*MilliSecond);
}



void DaliComm::resetIssued(DaliCommandStatusCB aStatusCB, uint8_t aResp1, uint8_t aResp2, ErrorPtr aError)
{
  // repeat resets until we get a correct answer
  if (!Error::isOK(aError) || aResp1!=RESP_CODE_ACK || aResp2!=ACK_OK) {
    LOG(LOG_WARNING, "DALI bridge: Incorrect answer (%02X %02X) or error (%s) from reset command -> repeating", aResp1, aResp2, aError ? aError->description().c_str() : "none");
    // issue another reset
    reset(aStatusCB);
    return;
  }
  // send next reset command with a longer delay, to give bridge time to process possibly buffered commands
  // (p44dbr does not execute next command until return code for previous command has been read from /dev/daliX)
  sendBridgeCommand(CMD_CODE_RESET, 0, 0, NULL, 1*Second);
  // another reset to make sure
  sendBridgeCommand(CMD_CODE_RESET, 0, 0, NULL, 100*MilliSecond);
  // make sure bus overload protection is active, autoreset enabled, reset to operating
  sendBridgeCommand(CMD_CODE_OVLRESET, 0, 0, NULL);
  // set DALI signal edge adjustments (available from fim_dali v3 onwards)
  sendBridgeCommand(CMD_CODE_EDGEADJ, sendEdgeAdj, samplePointAdj, NULL);
  // terminate any special commands on the DALI bus
  daliSend(DALICMD_TERMINATE, 0, aStatusCB);
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


void DaliComm::daliPrepareForCommand(uint16_t &aCommand, int &aWithDelay)
{
  if (aCommand & 0xFF00) {
    // command has a device type
    uint8_t dt = aCommand>>8;
    if (dt==0xFF) dt=0; // 0xFF is used to code DT0, to allow 0 meaning "no DT prefix" (DT0 is not in frequent use anyway)
    daliSend(DALICMD_ENABLE_DEVICE_TYPE, dt, NULL, aWithDelay); // apply delay to prefix command!
    aWithDelay = 0; // no further delay for actual command after prefix
    aCommand &= 0xFF; // mask out device type, is now consumed
  }
}



void DaliComm::daliSendCommand(DaliAddress aAddress, uint16_t aCommand, DaliCommandStatusCB aStatusCB, int aWithDelay)
{
  daliPrepareForCommand(aCommand, aWithDelay);
  daliSend(dali1FromAddress(aAddress)+1, aCommand, aStatusCB, aWithDelay);
}


void DaliComm::daliSendDtrAndCommand(DaliAddress aAddress, uint16_t aCommand, uint8_t aDTRValue, DaliCommandStatusCB aStatusCB, int aWithDelay)
{
  daliSend(DALICMD_SET_DTR, aDTRValue, NULL, aWithDelay); // apply delay to DTR setting command
  aWithDelay = 0; // delay already consumed for setting DTR
  daliPrepareForCommand(aCommand, aWithDelay);
  daliSendCommand(aAddress, aCommand, aStatusCB);
}



// DALI config commands (send twice within 100ms)

void DaliComm::daliSendTwice(uint8_t aDali1, uint8_t aDali2, DaliCommandStatusCB aStatusCB, int aWithDelay)
{
  sendBridgeCommand(CMD_CODE_2SEND16, aDali1, aDali2, boost::bind(&DaliComm::daliCommandStatusHandler, this, aStatusCB, _1, _2, _3), aWithDelay);
}

void DaliComm::daliSendConfigCommand(DaliAddress aAddress, uint16_t aCommand, DaliCommandStatusCB aStatusCB, int aWithDelay)
{
  daliPrepareForCommand(aCommand, aWithDelay);
  daliSendTwice(dali1FromAddress(aAddress)+1, aCommand, aStatusCB, aWithDelay);
}


void DaliComm::daliSendDtrAndConfigCommand(DaliAddress aAddress, uint16_t aCommand, uint8_t aDTRValue, DaliCommandStatusCB aStatusCB, int aWithDelay)
{
  daliSend(DALICMD_SET_DTR, aDTRValue, NULL, aWithDelay);
  aWithDelay = 0; // delay already consumed for setting DTR
  daliPrepareForCommand(aCommand, aWithDelay);
  daliSendConfigCommand(aAddress, aCommand, aStatusCB, aWithDelay);
}


void DaliComm::daliSend16BitValueAndCommand(DaliAddress aAddress, uint16_t aCommand, uint16_t aValue16, DaliCommandStatusCB aStatusCB, int aWithDelay)
{
  daliSend(DALICMD_SET_DTR1, aValue16>>8, NULL, aWithDelay); // MSB->DTR1 - apply delay to DTR1 setting command
  daliSend(DALICMD_SET_DTR, aValue16&0xFF); // LSB->DTR
  aWithDelay = 0; // delay already consumed for setting DTR1
  daliPrepareForCommand(aCommand, aWithDelay);
  daliSendCommand(aAddress, aCommand, aStatusCB);
}


void DaliComm::daliSend3x8BitValueAndCommand(DaliAddress aAddress, uint16_t aCommand, uint8_t aValue0, uint8_t aValue1, uint8_t aValue2, DaliCommandStatusCB aStatusCB, int aWithDelay)
{
  daliSend(DALICMD_SET_DTR, aValue0, NULL, aWithDelay);
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


void DaliComm::daliSendQuery(DaliAddress aAddress, uint16_t aQueryCommand, DaliQueryResultCB aResultCB, int aWithDelay)
{
  daliPrepareForCommand(aQueryCommand, aWithDelay);
  daliSendAndReceive(dali1FromAddress(aAddress)+1, aQueryCommand, aResultCB, aWithDelay);
}


void DaliComm::daliSendDtrAndQuery(DaliAddress aAddress, uint16_t aQueryCommand, uint8_t aDTRValue, DaliQueryResultCB aResultCB, int aWithDelay)
{
  daliSend(DALICMD_SET_DTR, aDTRValue, NULL, aWithDelay);
  daliSendQuery(aAddress, aQueryCommand, aResultCB, 0); // delay already consumed for setting DTR
}


void DaliComm::daliSend16BitQuery(DaliAddress aAddress, uint16_t aQueryCommand, Dali16BitValueQueryResultCB aResult16CB, int aWithDelay)
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


void DaliComm::daliSendDtrAnd16BitQuery(DaliAddress aAddress, uint16_t aQueryCommand, uint8_t aDTRValue, Dali16BitValueQueryResultCB aResultCB, int aWithDelay)
{
  daliSend(DALICMD_SET_DTR, aDTRValue, NULL, aWithDelay);
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
  aResponse &= 0xFE; // mask out direct arc bit
  if (aResponse==0xFE) {
    return DaliBroadcast; // broadcast
  }
  else if ((aResponse & 0xC0)==0x80) {
    return ((aResponse>>1) & DaliGroupMask) + DaliGroup;
  }
  else if ((aResponse & 0xC0)==0x00) {
    return (aResponse>>1) & DaliAddressMask; // device short address
  }
  else {
    return NoDaliAddress; // is not a DALI address
  }
}


string DaliComm::formatDaliAddress(DaliAddress aAddress)
{
  if (aAddress==DaliBroadcast) {
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




// MARK: ===== DALI bus data R/W test

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
    LOG(LOG_DEBUG, "DALI bus address %d - doing %d R/W tests to DTR...", busAddress, aNumCycles);
    // start with 0x55 pattern
    dtrValue = 0;
    numErrors = 0;
    cycle = 0;
    testNextByte();
  };

  // handle scan result
  void handleResponse(bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
  {
    if (!Error::isOK(aError)) {
      numErrors++;
      LOG(LOG_DEBUG, "- written 0x%02X, got error %s", dtrValue, aError->description().c_str());
    }
    else {
      if(!aNoOrTimeout) {
        // byte received
        if (aResponse!=dtrValue) {
          numErrors++;
          LOG(LOG_DEBUG, "- written 0x%02X, read back 0x%02X -> error", dtrValue, aResponse);
        }
      }
      else {
        numErrors++;
        LOG(LOG_DEBUG, "- written 0x%02X, got no answer (timeout) -> error", dtrValue);
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
      LOG(LOG_ERR, "Unreliable data access for DALI bus address %d - %d of %d R/W tests have failed!", busAddress, numErrors, numCycles);
      if (callback) callback(Error::err<DaliCommError>(DaliCommError::DataUnreliable, "DALI R/W tests: %d of %d failed", numErrors, numCycles));
    }
    else {
      // everything is fine
      LOG(LOG_DEBUG, "DALI bus address %d - all %d test cycles OK", busAddress, numCycles);
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



// MARK: ===== DALI bus scanning

// Scan bus for active devices (returns list of short addresses)

class DaliBusScanner : public P44Obj
{
  DaliComm &daliComm;
  DaliComm::DaliBusScanCB callback;
  DaliAddress shortAddress;
  DaliComm::ShortAddressListPtr activeDevicesPtr;
  DaliComm::ShortAddressListPtr unreliableDevicesPtr;
  bool probablyCollision;
  bool unconfiguredDevices;
public:
  static void scanBus(DaliComm &aDaliComm, DaliComm::DaliBusScanCB aResultCB)
  {
    // create new instance, deletes itself when finished
    new DaliBusScanner(aDaliComm, aResultCB);
  };
private:
  DaliBusScanner(DaliComm &aDaliComm, DaliComm::DaliBusScanCB aResultCB) :
    callback(aResultCB),
    daliComm(aDaliComm),
    probablyCollision(false),
    unconfiguredDevices(false),
    activeDevicesPtr(new DaliComm::ShortAddressList),
    unreliableDevicesPtr(new DaliComm::ShortAddressList)
  {
    daliComm.startProcedure();
    LOG(LOG_INFO, "DaliComm: starting quick bus scan (short address poll)");
    // reset the bus first
    daliComm.reset(boost::bind(&DaliBusScanner::resetComplete, this, _1));
  }

  void resetComplete(ErrorPtr aError)
  {
    // check for overload condition
    if (Error::isError(aError, DaliCommError::domain(), DaliCommError::BusOverload)) {
      LOG(LOG_ERR, "DALI bus has overload - possibly due to short circuit, defective ballasts or more than 64 devices connected");
      LOG(LOG_ERR, "-> Please power down installation, check DALI bus and try again");
    }
    if (aError)
      return completed(aError);
    // check if there are devices without short address
    daliComm.daliSendQuery(DaliBroadcast, DALICMD_QUERY_MISSING_SHORT_ADDRESS, boost::bind(&DaliBusScanner::handleMissingShortAddressResponse, this, _1, _2, _3));
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
      LOG(LOG_NOTICE, "Detected devices without short address on the bus (-> will trigger full scan later)");
      unconfiguredDevices = true;
    }
    // start the scan
    shortAddress = 0;
    nextQuery(dqs_controlgear);
  };


  // handle scan result
  void handleScanResponse(DeviceQueryState aQueryState, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
  {
    bool isYes = false;
    if (Error::isError(aError, DaliCommError::domain(), DaliCommError::DALIFrame)) {
      // framing error, indicates that we might have duplicates
      LOG(LOG_NOTICE, "Detected framing error for %d-th response from short address %d - probably short address collision", (int)aQueryState, shortAddress);
      probablyCollision = true;
      isYes = true; // still count as YES
      aError.reset(); // do not count as error aborting the search
      aQueryState=dqs_random_l; // one error is enough, no need to check other bytes
    }
    else if (Error::isOK(aError) && !aNoOrTimeout) {
      // no error, no timeout
      isYes = true;
      if (aQueryState==dqs_controlgear && aResponse!=DALIANSWER_YES) {
        // not entirely correct answer, also indicates collision
        LOG(LOG_NOTICE, "Detected incorrect YES answer 0x%02X from short address %d - probably short address collision", aResponse, shortAddress);
        probablyCollision = true;
      }
    }
    if (aQueryState==dqs_random_l || aNoOrTimeout) {
      // - collision already detected (dqs_random_l set above) -> query complete for this short address
      // - or last byte of existing device checked (dqs_random_l reached sequentially) -> do data test when this check was ok (isYes)
      // - or timeout -> could be device without random address support, do data test unless collision detected
      if (aQueryState!=dqs_controlgear && (isYes || !probablyCollision)) {
        // do a data reliability test now (quick 3 byte 0,0x55,0xAA only, unless loglevel>=6)
        DaliBusDataTester::daliBusTestData(daliComm, boost::bind(&DaliBusScanner::nextDevice ,this, true, _1), shortAddress, LOGLEVEL>=LOG_INFO ? 9 : 3);
        return;
      }
      // none found here, just test next
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
        activeDevicesPtr->push_back(shortAddress);
        LOG(LOG_INFO, "- detected DALI device at short address %d", shortAddress);
      }
      else {
        unreliableDevicesPtr->push_back(shortAddress);
        LOG(LOG_ERR, "Detected DALI device at short address %d, but it FAILED R/W TEST: %s -> ignoring", shortAddress, aError->description().c_str());
      }
    }
    // check if more short addresses to test
    shortAddress++;
    if (shortAddress<DALI_MAXDEVICES) {
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
    daliComm.daliSendQuery(shortAddress, q, boost::bind(&DaliBusScanner::handleScanResponse, this, aQueryState, _1, _2, _3));
  }



  void completed(ErrorPtr aError)
  {
    // scan done or error, return list to callback
    if (probablyCollision || unconfiguredDevices) {
      if (!Error::isOK(aError)) {
        LOG(LOG_WARNING, "Error (%s) in quick scan ignored because we need to do a full scan anyway", aError->description().c_str());
      }
      if (probablyCollision) {
        aError = Error::err<DaliCommError>(DaliCommError::AddressCollisions, "Address collision -> need full bus scan");
      }
      else {
        aError = Error::err<DaliCommError>(DaliCommError::AddressesMissing, "Devices with no short address -> need scan for those");
      }
    }
    daliComm.endProcedure();
    callback(activeDevicesPtr, unreliableDevicesPtr, aError);
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
  DaliComm &daliComm;
  DaliComm::DaliBusScanCB callback;
  bool fullScanOnlyIfNeeded;
  uint32_t searchMax;
  uint32_t searchMin;
  uint32_t searchAddr;
  uint8_t searchL, searchM, searchH;
  uint32_t lastSearchMin; // for re-starting
  int restarts;
  int compareRepeat;
  int readShortAddrRepeat;
  bool setLMH;
  DaliComm::ShortAddressListPtr foundDevicesPtr;
  DaliComm::ShortAddressListPtr usedShortAddrsPtr;
  DaliComm::ShortAddressListPtr conflictedShortAddrsPtr;
  DaliAddress newAddress;
public:
  static void fullBusScan(DaliComm &aDaliComm, DaliComm::DaliBusScanCB aResultCB, bool aFullScanOnlyIfNeeded)
  {
    // create new instance, deletes itself when finished
    new DaliFullBusScanner(aDaliComm, aResultCB, aFullScanOnlyIfNeeded);
  };
private:
  DaliFullBusScanner(DaliComm &aDaliComm, DaliComm::DaliBusScanCB aResultCB, bool aFullScanOnlyIfNeeded) :
    daliComm(aDaliComm),
    callback(aResultCB),
    fullScanOnlyIfNeeded(aFullScanOnlyIfNeeded),
    foundDevicesPtr(new DaliComm::ShortAddressList)
  {
    daliComm.startProcedure();
    // start a scan
    startScan();
  }


  void startScan()
  {
    // first scan for used short addresses
    foundDevicesPtr->clear(); // must be empty in case we do a restart
    DaliBusScanner::scanBus(daliComm,boost::bind(&DaliFullBusScanner::shortAddrListReceived, this, _1, _2, _3));
  }


  void shortAddrListReceived(DaliComm::ShortAddressListPtr aShortAddressListPtr, DaliComm::ShortAddressListPtr aUnreliableShortAddressListPtr, ErrorPtr aError)
  {
    bool missingAddrs = aError && aError->isError(DaliCommError::domain(), DaliCommError::AddressesMissing);
    // Strategy:
    // - when short scan reports devices with no short address, trigger a random binary serach FOR THOSE ONLY (case: new devices)
    // - when short scan reports another error: just report back, UNLESS unconditional full scan is requested
    if (aError && !missingAddrs && fullScanOnlyIfNeeded) {
      // not enough reason for triggering a random search
      return completed(aError);
    }
    // exit now if full binary search is not explicitly requested (!fullScanOnlyIfNeeded) and no new devices to be given address
    if (!missingAddrs && fullScanOnlyIfNeeded) {
      // just use the short address scan result
      foundDevicesPtr = aShortAddressListPtr;
      completed(ErrorPtr()); return;
    }
    // save the short address list
    usedShortAddrsPtr = aShortAddressListPtr;
    conflictedShortAddrsPtr = aUnreliableShortAddressListPtr;
    if (!fullScanOnlyIfNeeded) {
      LOG(LOG_WARNING, "DaliComm: unconditional full bus scan (random address binary search) for ALL devices requested, will reassign conflicting short addresses.");
    }
    else {
      foundDevicesPtr = aShortAddressListPtr; // use the already addressed devices
      LOG(LOG_WARNING, "DaliComm: bus scan (random address binary search) for devices without shortaddr - NO existing addresses will be reassigned.");
    }
    // Terminate any special modes first
    daliComm.daliSend(DALICMD_TERMINATE, 0x00);
    // initialize entire system for random address selection process. Unless full scan explicitly requested, only unaddressed devices will be included
    daliComm.daliSendTwice(DALICMD_INITIALISE, fullScanOnlyIfNeeded ? 0xFF : 0x00, NULL, 100*MilliSecond); // 0xFF = only those w/o short address
    daliComm.daliSendTwice(DALICMD_RANDOMISE, 0x00, NULL, 100*MilliSecond);
    // start search at lowest address
    restarts = 0;
    // - as specs say DALICMD_RANDOMISE might need 100mS until new random addresses are ready, wait a little before actually starting
    MainLoop::currentMainLoop().executeOnce(boost::bind(&DaliFullBusScanner::newSearchUpFrom, this, 0), 150*MilliSecond);
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

  // get new unused short address
  DaliAddress newShortAddress()
  {
    DaliAddress newAddr = DALI_MAXDEVICES;
    while (newAddr>0) {
      newAddr--;
      if (!isShortAddressInList(newAddr,usedShortAddrsPtr) && !isShortAddressInList(newAddr, conflictedShortAddrsPtr)) {
        // this one is free, use it
        usedShortAddrsPtr->push_back(newAddr);
        return newAddr;
      }
    }
    // return broadcast if none available
    return DaliBroadcast;
  }


  void newSearchUpFrom(uint32_t aMinSearch)
  {
    // init search range
    searchMax = 0xFFFFFF;
    searchMin = aMinSearch;
    lastSearchMin = aMinSearch;
    searchAddr = (searchMax-aMinSearch)/2+aMinSearch; // start in the middle
    // no search address currently set
    setLMH = true;
    compareRepeat = 0;
    compareNext();
  }


  void compareNext()
  {
    // issue next compare command
    // - update address bytes as needed (only those that have changed)
    uint8_t by = (searchAddr>>16) & 0xFF;
    if (by!=searchH || setLMH) {
      searchH = by;
      daliComm.daliSend(DALICMD_SEARCHADDRH, searchH);
    }
    // - searchM
    by = (searchAddr>>8) & 0xFF;
    if (by!=searchM || setLMH) {
      searchM = by;
      daliComm.daliSend(DALICMD_SEARCHADDRM, searchM);
    }
    // - searchL
    by = (searchAddr) & 0xFF;
    if (by!=searchL || setLMH) {
      searchL = by;
      daliComm.daliSend(DALICMD_SEARCHADDRL, searchL);
    }
    setLMH = false; // incremental from now on until flag is set again
    // - issue the compare command
    daliComm.daliSendAndReceive(DALICMD_COMPARE, 0x00, boost::bind(&DaliFullBusScanner::handleCompareResult, this, _1, _2, _3));
  }

  void handleCompareResult(bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
  {
    // Anything received but timeout is considered a yes
    bool isYes = DaliComm::isYes(aNoOrTimeout, aResponse, aError, true);
    if (aError) {
      LOG(LOG_ERR, "compare result error: %s -> aborted scan", aError->description().c_str());
      completed(aError); // other error, abort
      return;
    }
    compareRepeat++;
    LOG(LOG_DEBUG, "DALICMD_COMPARE result #%d = %s, search=0x%06X, searchMin=0x%06X, searchMax=0x%06X", compareRepeat, isYes ? "Yes" : "No ", searchAddr, searchMin, searchMax);
    // repeat to make sure
    if (!isYes && compareRepeat<=MAX_COMPARE_REPEATS) {
      LOG(LOG_DEBUG, "- not trusting compare NO result yet, retrying...");
      compareNext();
      return;
    }
    // any ballast has smaller or equal random address?
    if (isYes) {
      if (compareRepeat>1) {
        LOG(LOG_DEBUG, "- got a NO in first attempt but now a YES in %d attempt! -> unreliable answers", compareRepeat);
      }
      // yes, there is at least one, max address is what we searched so far
      searchMax = searchAddr;
    }
    else {
      // none at or below current search
      if (searchMin==0xFFFFFF) {
        // already at max possible -> no more devices found
        LOG(LOG_INFO, "No more devices");
        completed(ErrorPtr()); return;
      }
      searchMin = searchAddr+1; // new min
    }
    if (searchMin==searchMax && searchAddr==searchMin) {
      // found!
      LOG(LOG_NOTICE, "- Found device at 0x%06X", searchAddr);
      // read current short address
      readShortAddrRepeat = 0;
      daliComm.daliSendAndReceive(DALICMD_QUERY_SHORT_ADDRESS, 0x00, boost::bind(&DaliFullBusScanner::handleShortAddressQuery, this, _1, _2, _3), READ_SHORT_ADDR_SEND_DELAY);
    }
    else {
      // not yet - continue
      searchAddr = searchMin + (searchMax-searchMin)/2;
      LOG(LOG_DEBUG, "                            Next search=0x%06X, searchMin=0x%06X, searchMax=0x%06X", searchAddr, searchMin, searchMax);
      if (searchAddr>0xFFFFFF) {
        LOG(LOG_WARNING, "- failed search");
        if (restarts<MAX_RESTARTS) {
          LOG(LOG_NOTICE, "- restarting search at address of last found device + 1");
          restarts++;
          newSearchUpFrom(lastSearchMin);
          return;
        }
        else {
          return completed(Error::err<DaliCommError>(DaliCommError::DeviceSearch, "Binary search got out of range"));
        }
      }
      // issue next address' compare
      compareRepeat = 0;
      compareNext();
    }
  }

  void handleShortAddressQuery(bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
  {
    if (aError)
      return completed(aError);
    if (aNoOrTimeout) {
      // should not happen, but just retry
      LOG(LOG_WARNING, "- Device at 0x%06X does not respond to DALICMD_QUERY_SHORT_ADDRESS", searchAddr);
      readShortAddrRepeat++;
      if (readShortAddrRepeat<=MAX_SHORTADDR_READ_REPEATS) {
        daliComm.daliSendAndReceive(DALICMD_QUERY_SHORT_ADDRESS, 0x00, boost::bind(&DaliFullBusScanner::handleShortAddressQuery, this, _1, _2, _3), READ_SHORT_ADDR_SEND_DELAY);
        return;
      }
      // should definitely not happen, probably bus error led to false device detection -> restart search after a while
      LOG(LOG_WARNING, "- Device at 0x%06X did not respond to %d attempts of DALICMD_QUERY_SHORT_ADDRESS", searchAddr, MAX_SHORTADDR_READ_REPEATS+1);
      if (restarts<MAX_RESTARTS) {
        LOG(LOG_NOTICE, "- restarting complete scan after a delay");
        restarts++;
        MainLoop::currentMainLoop().executeOnce(boost::bind(&DaliFullBusScanner::startScan, this), RESCAN_RETRY_DELAY);
        return;
      }
      else {
        return completed(Error::err<DaliCommError>(DaliCommError::DeviceSearch,  "Detected device does not respond to QUERY_SHORT_ADDRESS"));
      }
    }
    else {
      // response is short address in 0AAAAAA1 format or DALIVALUE_MASK (no adress)
      newAddress = DaliBroadcast; // none
      DaliAddress shortAddress = newAddress; // none
      bool needsNewAddress = false;
      if (aResponse==DALIVALUE_MASK) {
        // device has no short address yet, assign one
        needsNewAddress = true;
        newAddress = newShortAddress();
        LOG(LOG_NOTICE, "- Device at 0x%06X has NO short address -> assigning new short address = %d", searchAddr, newAddress);
      }
      else {
        shortAddress = DaliComm::addressFromDaliResponse(aResponse);
        LOG(LOG_INFO, "- Device at 0x%06X has short address: %d", searchAddr, shortAddress);
        // check for collisions
        if (isShortAddressInList(shortAddress, foundDevicesPtr)) {
          newAddress = newShortAddress();
          needsNewAddress = true;
          LOG(LOG_NOTICE, "- Collision on short address %d -> assigning new short address = %d", shortAddress, newAddress);
        }
      }
      // check if we need to re-assign the short address
      if (needsNewAddress) {
        if (newAddress==DaliBroadcast) {
          // no more short addresses available
          LOG(LOG_ERR, "Bus has too many devices, device 0x%06X cannot be assigned a short address and will not be usable", searchAddr);
        }
        // new address must be assigned (or in case none is available, a possibly
        // existing short address will be removed by assigning DaliBroadcast==0xFF)
        daliComm.daliSend(DALICMD_PROGRAM_SHORT_ADDRESS, DaliComm::dali1FromAddress(newAddress)+1);
        daliComm.daliSendAndReceive(
          DALICMD_VERIFY_SHORT_ADDRESS, DaliComm::dali1FromAddress(newAddress)+1,
          boost::bind(&DaliFullBusScanner::handleNewShortAddressVerify, this, _1, _2, _3),
          1000 // delay one second before querying for new short address
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
    if (newAddress==DaliBroadcast || DaliComm::isYes(aNoOrTimeout, aResponse, aError, false)) {
      // address was deleted, not added in the first place (more than 64 devices)
      // OR real clean YES - new short address verified
      deviceFound(newAddress);
    }
    else {
      // short address verification failed
      LOG(LOG_ERR, "Error - could not assign new short address %d", newAddress);
      deviceFound(DaliBroadcast); // not really a usable device, but withdraw it and continue searching
    }
  }

  void deviceFound(DaliAddress aShortAddress)
  {
    // store short address if real address
    // (if broadcast, means that this device is w/o short address because >64 devices are on the bus, or short address could not be programmed)
    if (aShortAddress!=DaliBroadcast) {
      foundDevicesPtr->push_back(aShortAddress);
    }
    // withdraw this device from further searches
    daliComm.daliSend(DALICMD_WITHDRAW, 0x00);
    // continue searching devices
    newSearchUpFrom(searchAddr+1);
  }

  void completed(ErrorPtr aError)
  {
    // terminate
    daliComm.daliSend(DALICMD_TERMINATE, 0x00);
    // callback
    daliComm.endProcedure();
    callback(foundDevicesPtr, DaliComm::ShortAddressListPtr(), aError);
    // done, delete myself
    delete this;
  }
};


void DaliComm::daliFullBusScan(DaliBusScanCB aResultCB, bool aFullScanOnlyIfNeeded)
{
  if (isBusy()) { aResultCB(ShortAddressListPtr(), ShortAddressListPtr(), DaliComm::busyError()); return; }
  DaliFullBusScanner::fullBusScan(*this, aResultCB, aFullScanOnlyIfNeeded);
}



// MARK: ===== DALI memory access / device info reading

#define DALI_MAX_MEMREAD_RETRIES 3

class DaliMemoryReader : public P44Obj
{
  DaliComm &daliComm;
  DaliComm::DaliReadMemoryCB callback;
  DaliAddress busAddress;
  DaliComm::MemoryVectorPtr memory;
  int bytesToRead;
  int retries;
  uint8_t currentOffset;
  typedef std::vector<uint8_t> MemoryVector;
public:
  static void readMemory(DaliComm &aDaliComm, DaliComm::DaliReadMemoryCB aResultCB, DaliAddress aAddress, uint8_t aBank, uint8_t aOffset, uint8_t aNumBytes)
  {
    // create new instance, deletes itself when finished
    new DaliMemoryReader(aDaliComm, aResultCB, aAddress, aBank, aOffset, aNumBytes);
  };
private:
  DaliMemoryReader(DaliComm &aDaliComm, DaliComm::DaliReadMemoryCB aResultCB, DaliAddress aAddress, uint8_t aBank, uint8_t aOffset, uint8_t aNumBytes) :
    daliComm(aDaliComm),
    callback(aResultCB),
    busAddress(aAddress),
    memory(new MemoryVector)
  {
    daliComm.startProcedure();
    LOG(LOG_INFO, "DALI bus address %d - reading %d bytes from bank %d at offset %d:", busAddress, aNumBytes, aBank, aOffset);
    // set DTR1 = bank
    daliComm.daliSend(DALICMD_SET_DTR1, aBank);
    // set initial offset and bytes
    currentOffset = aOffset;
    bytesToRead = aNumBytes;
    retries = 0;
    startReading();
  }


  void startReading()
  {
    // set DTR = offset within bank
    daliComm.daliSend(DALICMD_SET_DTR, currentOffset);
    // start reading
    readNextByte();
  };


  // handle scan result
  void handleResponse(bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError, bool aRetried)
  {
    if (!Error::isOK(aError) || aNoOrTimeout) {
      if (retries++<DALI_MAX_MEMREAD_RETRIES) {
        // restart reading explicitly at current offset
        startReading();
        return;
      }
    }
    else {
      // even ok result must be retry-free, otherwise we need to re-set the DTR
      if (aRetried) {
        // restart reading explicitly at current offset
        startReading();
        return;
      }
      // byte received, append to vector
      retries = 0;
      memory->push_back(aResponse);
      currentOffset++;
      if (--bytesToRead>0) {
        // more bytes to read
        readNextByte();
        return;
      }
    }
    // read done, timeout or error, return memory to callback
    daliComm.endProcedure();
    if (LOGENABLED(LOG_INFO)) {
      // dump data
      int o=0;
      for (MemoryVector::iterator pos = memory->begin(); pos!=memory->end(); ++pos, ++o) {
        LOG(LOG_INFO, "- %03d/0x%02X : 0x%02X/%03d", o, o, *pos, *pos);
      }
    }
    callback(memory, aError);
    // done, delete myself
    delete this;
  };

  void readNextByte()
  {
    daliComm.daliSendQuery(busAddress, DALICMD_READ_MEMORY_LOCATION, boost::bind(&DaliMemoryReader::handleResponse, this, _1, _2, _3, _4));
  }
};


void DaliComm::daliReadMemory(DaliReadMemoryCB aResultCB, DaliAddress aAddress, uint8_t aBank, uint8_t aOffset, uint8_t aNumBytes)
{
  if (isBusy()) { aResultCB(MemoryVectorPtr(), DaliComm::busyError()); return; }
  DaliMemoryReader::readMemory(*this, aResultCB, aAddress, aBank, aOffset, aNumBytes);
}


// MARK: ===== DALI device info reading

#define DALI_MAX_BANKREAD_RETRIES 3 // how many times reading bank will be tried in case of checksum error


class DaliDeviceInfoReader : public P44Obj
{
  DaliComm &daliComm;
  DaliComm::DaliDeviceInfoCB callback;
  DaliAddress busAddress;
  DaliDeviceInfoPtr deviceInfo;
  uint8_t bankChecksum;
  uint8_t maxBank;
  int retries;
public:
  static void readDeviceInfo(DaliComm &aDaliComm, DaliComm::DaliDeviceInfoCB aResultCB, DaliAddress aAddress)
  {
    // create new instance, deletes itself when finished
    new DaliDeviceInfoReader(aDaliComm, aResultCB, aAddress);
  };
private:
  DaliDeviceInfoReader(DaliComm &aDaliComm, DaliComm::DaliDeviceInfoCB aResultCB, DaliAddress aAddress) :
    daliComm(aDaliComm),
    callback(aResultCB),
    busAddress(aAddress)
  {
    daliComm.startProcedure();
    retries = 0;
    readBank0();
  }

  void readBank0() {
    // read the memory
    maxBank = 0; // all devices must have a bank 0, rest is optional
    // Note: official checksum algorithm is: 0-byte2-byte3...byteLast, check with checksum+byte2+byte3...byteLast==0
    bankChecksum = 0;
    DaliMemoryReader::readMemory(daliComm, boost::bind(&DaliDeviceInfoReader::handleBank0Data, this, _1, _2), busAddress, 0, 0, DALIMEM_BANK0_MINBYTES);
  };

  void handleBank0Data(DaliComm::MemoryVectorPtr aBank0Data, ErrorPtr aError)
  {
    deviceInfo.reset(new DaliDeviceInfo);
    deviceInfo->shortAddress = busAddress;
    deviceInfo->devInfStatus = DaliDeviceInfo::devinf_none; // no info yet
    if (aError)
      return complete(aError);
    if (aBank0Data->size()==DALIMEM_BANK0_MINBYTES) {
      deviceInfo->devInfStatus = DaliDeviceInfo::devinf_solid; // assume solid info present
      maxBank = (*aBank0Data)[2]; // this is the highest bank number implemented in this device
      LOG(LOG_INFO, "- highest available DALI memory bank = %d", maxBank);
      // sum up starting with checksum itself, result must be 0x00 in the end
      for (int i=0x01; i<DALIMEM_BANK0_MINBYTES; i++) {
        bankChecksum += (*aBank0Data)[i];
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
      for (int i=0x03; i<=0x0E; i++) {
        uint8_t b = (*aBank0Data)[i];
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
        LOG(LOG_ERR, "DALI shortaddress %d Bank 0 has %d consecutive bytes of 0x%02X and %d bytes of 0xFF  - indicates invalid GTIN/Serial data -> ignoring", busAddress, maxSame, sameByte, numFFs);
        deviceInfo->devInfStatus = DaliDeviceInfo::devinf_none; // consider invalid
      }
      // GTIN: bytes 0x03..0x08, MSB first
      deviceInfo->gtin = 0;
      for (int i=0x03; i<=0x08; i++) {
        deviceInfo->gtin = (deviceInfo->gtin << 8) + (*aBank0Data)[i];
      }
      // Firmware version
      deviceInfo->fw_version_major = (*aBank0Data)[0x09];
      deviceInfo->fw_version_minor = (*aBank0Data)[0x0A];
      // Serial: bytes 0x0B..0x0E
      deviceInfo->serialNo = 0;
      for (int i=0x0B; i<=0x0E; i++) {
        deviceInfo->serialNo = (deviceInfo->serialNo << 8) + (*aBank0Data)[i];
      }
      // now some more plausibility checks at the GTIN/serial level
      if (deviceInfo->gtin==0 || gtinCheckDigit(deviceInfo->gtin)!=0) {
        // invalid GTIN
        LOG(LOG_ERR, "DALI shortaddress %d has invalid GTIN=%lld/0x%llX -> ignoring", busAddress, deviceInfo->gtin, deviceInfo->gtin);
        deviceInfo->devInfStatus = DaliDeviceInfo::devinf_none; // consider invalid
      }
      else {
        if (deviceInfo->devInfStatus==DaliDeviceInfo::devinf_solid) {
          // we have a GTIN -> check blacklist
          int i=0;
          while (DALI_GTIN_blacklist[i]!=0) {
            if (deviceInfo->gtin==DALI_GTIN_blacklist[i]) {
              // found in blacklist, invalidate serial
              LOG(LOG_ERR, "GTIN %lld of DALI shortaddress %d is blacklisted because it is known to have invalid serial -> invalidating serial", deviceInfo->gtin, busAddress);
              deviceInfo->serialNo = 0; // reset, make invalid for check below
              break;
            }
            i++;
          }
        }
        if (deviceInfo->serialNo==0 || deviceInfo->serialNo==0xFFFFFFFF) {
          // all bits zero or all bits one is considered invalid serial,
          // as well as 2-byte and 3-byte all-one serials are
          LOG(LOG_ERR, "DALI shortaddress %d has suspect S/N=%lld/0x%llX -> ignoring", busAddress, deviceInfo->serialNo, deviceInfo->serialNo);
          if (deviceInfo->devInfStatus==DaliDeviceInfo::devinf_solid) {
            // if everything else is ok, except for a all zero or all 1 serial number, consider GTIN valid
            deviceInfo->devInfStatus = DaliDeviceInfo::devinf_only_gtin;
          }
          else {
            // was not solid before, consider completely invalid
            deviceInfo->devInfStatus = DaliDeviceInfo::devinf_none;
          }
        }
      }
      // check for extra data device may have
      // Note: aBank0Data[0] is address of highest byte, so NUMBER of bytes is one more!
      int extraBytes = (*aBank0Data)[0]+1-DALIMEM_BANK0_MINBYTES;
      if (extraBytes>0) {
        // issue read of extra bytes
        DaliMemoryReader::readMemory(daliComm, boost::bind(&DaliDeviceInfoReader::handleBank0ExtraData, this, _1, _2), busAddress, 0, DALIMEM_BANK0_MINBYTES, extraBytes);
      }
      else {
        // no extra bytes, bank 0 reading is complete
        bank0readcomplete();
      }
    }
    else {
      // not enough bytes
      return complete(Error::err<DaliCommError>(DaliCommError::MissingData, "Not enough bytes read from bank0 at shortAddress %d", busAddress));
    }
  };


  void handleBank0ExtraData(DaliComm::MemoryVectorPtr aBank0Data, ErrorPtr aError)
  {
    if (aError)
      return complete(aError);
    else {
      // add extra bytes to checksum, result must be 0x00 in the end
      for (int i=0; i<aBank0Data->size(); i++) {
        bankChecksum += (*aBank0Data)[i];
      }
      // Note: before 2015-02-27, we had a bug which caused the last extra byte not being read, so the checksum reached zero
      // only if the last byte was 0. We also passed the if checksum was 0xFF, because our reference devices always had 0x01 in
      // the last byte, and I assumed missing by 1 was the result of not precise enough specs or a bug in the device.
      #if OLD_BUGGY_CHKSUM_COMPATIBLE
      if (bankChecksum==0 && deviceInfo->devInfStatus==DaliDeviceInfo::devinf_solid) {
        // by specs, this is a correct checksum, and a seemingly solid device info
        // - now check if the buggy checker would have passed it, too (which is when last byte is 0x01 or 0x00)
        uint8_t lastByte = (*aBank0Data)[aBank0Data->size()-1];
        if (lastByte!=0x00 && lastByte!=0x01) {
          // this bank 0 data would not have passed the buggy checker
          deviceInfo->devInfStatus = DaliDeviceInfo::devinf_maybe; // might be usable to identify device, but needs backwards compatibility checking
        }
      }
      #endif
      // TODO: look at that data
      // now get OEM info
      bank0readcomplete();
    }
  };


  void bank0readcomplete()
  {
    // verify checksum of bank0 data first
    // - per specs, correct sum must be 0x00 here.
    if (bankChecksum!=0x00) {
      // checksum error
      deviceInfo->devInfStatus = DaliDeviceInfo::devinf_none; // device info is invalid
      // - invalidate gtin, serial and fw version
      deviceInfo->gtin = 0;
      deviceInfo->fw_version_major = 0;
      deviceInfo->fw_version_minor = 0;
      deviceInfo->serialNo = 0;
      // - check retries
      if (retries++<DALI_MAX_BANKREAD_RETRIES) {
        // retry reading bank 0 info
        LOG(LOG_INFO, "Checksum wrong (0x%02X!=0x00) in %d. attempt to read bank0 info from shortAddress %d -> retrying", bankChecksum, retries, busAddress);
        readBank0();
        return;
      }
      // - report error
      LOG(LOG_ERR, "DALI shortaddress %d Bank 0 checksum is wrong - should sum up to 0x00, actual sum is 0x%02X", busAddress, bankChecksum);
      return complete(Error::err<DaliCommError>(DaliCommError::BadChecksum, "bad DALI memory bank 0 checksum at shortAddress %d", busAddress));
    }
    if (maxBank>0) {
      // now read OEM info from bank1
      retries = 0;
      readBank1();
    }
    else {
      // device does not have bank1, so we are complete
      return complete(ErrorPtr());
    }
  };


  void readBank1()
  {
    bankChecksum = 0;
    DaliMemoryReader::readMemory(daliComm, boost::bind(&DaliDeviceInfoReader::handleBank1Data, this, _1, _2), busAddress, 1, 0, DALIMEM_BANK1_MINBYTES);
  }


  void handleBank1Data(DaliComm::MemoryVectorPtr aBank1Data, ErrorPtr aError)
  {
    if (aError)
      return complete(aError);
    if (aBank1Data->size()==DALIMEM_BANK1_MINBYTES) {
      // sum up starting with checksum itself, result must be 0x00 in the end
      for (int i=0x01; i<DALIMEM_BANK1_MINBYTES; i++) {
        bankChecksum += (*aBank1Data)[i];
      }
      // OEM GTIN: bytes 0x03..0x08, MSB first
      deviceInfo->oem_gtin = 0;
      for (int i=0x03; i<=0x08; i++) {
        deviceInfo->oem_gtin = (deviceInfo->oem_gtin << 8) + (*aBank1Data)[i];
      }
      // Serial: bytes 0x09..0x0C
      deviceInfo->oem_serialNo = 0;
      for (int i=0x09; i<=0x0C; i++) {
        deviceInfo->oem_serialNo = (deviceInfo->oem_serialNo << 8) + (*aBank1Data)[i];
      }
      // check for extra data device may have
      int extraBytes = (*aBank1Data)[0]+1-DALIMEM_BANK1_MINBYTES;
      if (extraBytes>0) {
        // issue read of extra bytes
        DaliMemoryReader::readMemory(daliComm, boost::bind(&DaliDeviceInfoReader::handleBank1ExtraData, this, _1, _2), busAddress, 0, DALIMEM_BANK1_MINBYTES, extraBytes);
      }
      else {
        // No extra bytes: device info is complete already
        return bank1readcomplete(aError);
      }
    }
    else {
      // No bank1 OEM info: device info is complete already (is not an error)
      return complete(aError);
    }
  };


  void handleBank1ExtraData(DaliComm::MemoryVectorPtr aBank1Data, ErrorPtr aError)
  {
    if (aError)
      return complete(aError);
    else {
      // add extra bytes to checksum, result must be 0x00 in the end
      for (int i=0; i<aBank1Data->size(); i++) {
        bankChecksum += (*aBank1Data)[i];
      }
      // TODO: look at that data
      // now get OEM info
      bank1readcomplete(aError);
    }
  };


  void bank1readcomplete(ErrorPtr aError)
  {
    if (Error::isOK(aError)) {
      // test checksum
      // - per specs, correct sum must be 0x00 here.
      if (bankChecksum!=0x00) {
        // checksum error
        // - invalidate OEM gtin and serial
        deviceInfo->oem_gtin = 0;
        deviceInfo->oem_serialNo = 0;
        // - check retries
        if (retries++<DALI_MAX_BANKREAD_RETRIES) {
          // retry reading bank 1 info
          LOG(LOG_INFO, "Checksum wrong (0x%02X!=0x00) in %d. attempt to read bank1 info from shortAddress %d -> retrying", bankChecksum, retries, busAddress);
          readBank1();
          return;
        }
        // - report error
        LOG(LOG_ERR, "DALI shortaddress %d Bank 1 checksum is wrong - should sum up to 0x00, actual sum is 0x%02X", busAddress, bankChecksum);
        aError = Error::err<DaliCommError>(DaliCommError::BadChecksum, "bad DALI memory bank 1 checksum at shortAddress %d", busAddress);
      }
    }
    complete(aError);
  }


  void complete(ErrorPtr aError)
  {
    daliComm.endProcedure();
    if (Error::isOK(aError)) {
      LOG(LOG_NOTICE,
        "Successfully read device info from shortAddress %d - %s data: GTIN=%lld, Serial=%lld",
        busAddress,
        deviceInfo->devInfStatus==DaliDeviceInfo::devinf_solid
        #if OLD_BUGGY_CHKSUM_COMPATIBLE
        || deviceInfo->devInfStatus==DaliDeviceInfo::devinf_maybe
        #endif
        ? "valid" : "GARBAGE",
        deviceInfo->gtin,
        deviceInfo->serialNo
      );
    }
    // clean device info in case it has been detected invalid by now
    if (deviceInfo->devInfStatus==DaliDeviceInfo::devinf_none) {
      deviceInfo->clear(); // clear everything except shortaddress
    }
    else if (deviceInfo->devInfStatus==DaliDeviceInfo::devinf_only_gtin) {
      // consider serial numbers invalid, but GTIN and version ok
      deviceInfo->serialNo = 0;
      deviceInfo->oem_serialNo = 0;
    }
    // report
    callback(deviceInfo, aError);
    // done, delete myself
    delete this;
  }
};


void DaliComm::daliReadDeviceInfo(DaliDeviceInfoCB aResultCB, DaliAddress aAddress)
{
  if (isBusy()) { aResultCB(DaliDeviceInfoPtr(), DaliComm::busyError()); return; }
  DaliDeviceInfoReader::readDeviceInfo(*this, aResultCB, aAddress);
}


// MARK: ===== DALI device info


DaliDeviceInfo::DaliDeviceInfo()
{
  clear();
  shortAddress = DaliBroadcast; // undefined short address
}


void DaliDeviceInfo::clear()
{
  // clear everything except short address
  gtin = 0;
  fw_version_major = 0;
  fw_version_minor = 0;
  serialNo = 0;
  oem_gtin = 0;
  oem_serialNo = 0;
  devInfStatus = devinf_none;
}


string DaliDeviceInfo::description()
{
  string s = string_format("\n- DaliDeviceInfo for shortAddress %d", shortAddress);
  string_format_append(s, "\n  - is %suniquely defining the device", devInfStatus==devinf_solid ? "" : "NOT ");
  string_format_append(s, "\n  - GTIN       : %lld", gtin);
  string_format_append(s, "\n  - Serial     : %lld", serialNo);
  string_format_append(s, "\n  - OEM GTIN   : %lld", oem_gtin);
  string_format_append(s, "\n  - OEM Serial : %lld", oem_serialNo);
  string_format_append(s, "\n  - Firmware   : %d.%d", fw_version_major, fw_version_minor);
  return s;
}

#endif // ENABLE_DALI






