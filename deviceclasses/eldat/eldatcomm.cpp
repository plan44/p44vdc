//
//  Copyright (c) 2016-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "eldatcomm.hpp"

#if ENABLE_ELDAT

using namespace p44;


// MARK: ===== ELDAT SerialOperations


static size_t getMessage(size_t aNumBytes, uint8_t *aBytes, string &aMessage)
{
  size_t e = string::npos;
  for (size_t i=0; i<aNumBytes; i++) {
    if (aBytes[i]=='\t') {
      // TAB separates actual answer from OK
      e = i;
    }
    if (aBytes[i]=='\r' || aBytes[i]=='\n') {
      aMessage.assign((char *)aBytes, e==string::npos ? i : e);
      while (++i<aNumBytes) {
        if (aBytes[i]!='\r' && aBytes[i]!='\n')
          break;
      }
      return i; // answer including all trailing CRs and LFs
    }
  }
  // not yet a complete line
  return NOT_ENOUGH_BYTES;
}


class EldatResponse : public SerialOperation
{
  typedef SerialOperation inherited;

public:

  string response;

  EldatResponse() {};

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
typedef boost::intrusive_ptr<EldatResponse> EldatResponsePtr;


class EldatCommand : public SerialOperationSend
{
  typedef SerialOperationSend inherited;

public:

  /// send a command not expecting an answer
  EldatCommand(const string &aCommand)
  {
    string cmd = aCommand + '\r';
    setDataSize(cmd.size());
    appendData(cmd.size(), (uint8_t *)cmd.c_str());
  }
  
};



// MARK: ===== ELDAT communication handler

// baudrate for communication with ELDAT TX10 interface
#define ELDAT_COMMAPARMS "57600,8,N,1"
#define ELDAT_VID 0x155A
#define ELDAT_PID 0x1009

#define ELDAT_MAX_MESSAGE_SIZE 100

#define ELDAT_ALIVECHECK_INTERVAL (30*Second)
#define ELDAT_ALIVECHECK_TIMEOUT (3*Second)

#define ELDAT_COMMAND_TIMEOUT (3*Second)

#define ELDAT_INIT_RETRIES 5
#define ELDAT_INIT_RETRY_INTERVAL (5*Second)



EldatComm::EldatComm(MainLoop &aMainLoop) :
	inherited(aMainLoop),
  usbPid(0),
  appVersion(0)
{
  // serialqueue needs a buffer as we use NOT_ENOUGH_BYTES mechanism
  setAcceptBuffer(ELDAT_MAX_MESSAGE_SIZE);
}


EldatComm::~EldatComm()
{
}


void EldatComm::setConnectionSpecification(const char *aConnectionSpec, uint16_t aDefaultPort)
{
  FOCUSLOG("EldatComm::setConnectionSpecification: %s", aConnectionSpec);
  serialComm->setConnectionSpecification(aConnectionSpec, aDefaultPort, ELDAT_COMMAPARMS);
	// open connection so we can receive
	serialComm->requestConnection();
}


void EldatComm::initialize(StatusCB aCompletedCB)
{
  // start initializing
  initializeInternal(aCompletedCB, ELDAT_INIT_RETRIES);
}


void EldatComm::initializeInternal(StatusCB aCompletedCB, int aRetriesLeft)
{
  // get version
  serialComm->requestConnection();
  serialComm->setDTR(true);
  sendCommand("ID?", boost::bind(&EldatComm::versionReceived, this, aCompletedCB, aRetriesLeft, _1, _2));
}


void EldatComm::initError(StatusCB aCompletedCB, int aRetriesLeft, ErrorPtr aError)
{
  // error querying version
  aRetriesLeft--;
  if (aRetriesLeft>=0) {
    LOG(LOG_WARNING, "EldatComm: Initialisation: command failed: %s -> retrying again", aError->description().c_str());
    serialComm->setDTR(false); // should cause reset
    serialComm->closeConnection(); // also close and re-open later
    // retry initializing later
    aliveCheckTicket.executeOnce(boost::bind(&EldatComm::initializeInternal, this, aCompletedCB, aRetriesLeft), ELDAT_INIT_RETRY_INTERVAL);
  }
  else {
    // no more retries, just return
    LOG(LOG_ERR, "EldatComm: Initialisation: %d attempts failed to send commands -> initialisation failed", ELDAT_INIT_RETRIES);
    if (aCompletedCB) aCompletedCB(aError);
  }
  return; // done
}


void EldatComm::versionReceived(StatusCB aCompletedCB, int aRetriesLeft, string aAnswer, ErrorPtr aError)
{
  // extract versions
  if (Error::isOK(aError)) {
    uint16_t vid;
    if (sscanf(aAnswer.c_str(), "ID,%hX,%hX,%hX", &vid, &usbPid, &appVersion)==3) {
      LOG(LOG_INFO, "ELDAT module info (ID): vid=0x%04hX, usbPid=0x%04hX, version=0x%04hX", vid, usbPid, appVersion);
      if (vid!=ELDAT_VID) {
        initError(aCompletedCB, 0, ErrorPtr(new EldatCommError(EldatCommErrorCompatibility, string_format("Invalid Vendor ID 0x%04hX", vid))));
        return;
      }
      if (usbPid!=ELDAT_PID) {
        initError(aCompletedCB, 0, ErrorPtr(new EldatCommError(EldatCommErrorCompatibility, string_format("Unsupported Product ID 0x%04hX", usbPid))));
        return;
      }
    }
    FOCUSLOG("Received ID answer: %s", aAnswer.c_str());
  }
  else {
    initError(aCompletedCB, aRetriesLeft, aError);
    return;
  }
  // completed successfully
  if (aCompletedCB) aCompletedCB(aError);
  // schedule first alive check quickly
  aliveCheckTicket.executeOnce(boost::bind(&EldatComm::aliveCheck, this), 2*Second);
}



void EldatComm::aliveCheck()
{
  FOCUSLOG("EldatComm: checking ELDAT module operation by sending ID command");
  // issue command
  sendCommand("ID?", boost::bind(&EldatComm::aliveCheckResponse, this, _1, _2));
}


void EldatComm::aliveCheckResponse(string aAnswer, ErrorPtr aError)
{
  if (!Error::isOK(aError)) {
    // alive check failed, try to recover ELDAT interface
    LOG(LOG_ERR, "EldatComm: alive check of ELDAT module failed -> restarting module");
    serialComm->setDTR(false); // release DTR, this should reset the ELDAT interface
    // - using alive check ticket for reset sequence
    aliveCheckTicket.executeOnce(boost::bind(&EldatComm::resetDone, this), 2*Second);
  }
  else {
    // response received, should be ID
    if (!(aAnswer.substr(0,3)=="ID,")) {
      FOCUSLOG("Alive check received answer after sending 'ID?', but got unexpected answer '%s'", aAnswer.c_str());
    }
    // also schedule the next alive check
    aliveCheckTicket.executeOnce(boost::bind(&EldatComm::aliveCheck, this), ELDAT_ALIVECHECK_INTERVAL);
  }
}


void EldatComm::resetDone()
{
  LOG(LOG_NOTICE, "EldatComm: re-asserting DTR");
  serialComm->setDTR(true); // should restart the ELDAT interface
  // wait a little, then re-open connection
  aliveCheckTicket.executeOnce(boost::bind(&EldatComm::reopenConnection, this), 2*Second);
}


void EldatComm::reopenConnection()
{
  LOG(LOG_NOTICE, "EldatComm: re-opening connection");
	serialComm->requestConnection(); // re-open connection
  // restart alive checks, not too soon after reset
  aliveCheckTicket.executeOnce(boost::bind(&EldatComm::aliveCheck, this), 10*Second);
}


void EldatComm::setReceivedMessageHandler(EldatMessageCB aMessageHandler)
{
  receivedMessageHandler = aMessageHandler;
}


ssize_t EldatComm::acceptExtraBytes(size_t aNumBytes, uint8_t *aBytes)
{
  string msg;
  size_t ret = getMessage(aNumBytes, aBytes, msg);
  if (ret!=NOT_ENOUGH_BYTES) {
    FOCUSLOG("ELDAT: received message: %s", msg.c_str());
    if (receivedMessageHandler) {
      receivedMessageHandler(msg, ErrorPtr());
    }
  }
  return ret; // NOT_ENOUGH_BYTES or length of command consumed
}


void EldatComm::sendCommand(string aCommand, EldatMessageCB aResponseCB)
{
  // queue command
  FOCUSLOG("ELDAT: sending command: %s", aCommand.c_str());
  SerialOperationPtr req = SerialOperationPtr(new EldatCommand(aCommand));
  // all commands expect an answer
  SerialOperationPtr resp = SerialOperationPtr(new EldatResponse);
  req->setChainedOperation(resp);
  resp->setCompletionCallback(boost::bind(&EldatComm::commandResponseHandler, this, aResponseCB, resp, _1));
  resp->setTimeout(ELDAT_COMMAND_TIMEOUT);
  queueSerialOperation(req);
  processOperations();
}


void EldatComm::commandResponseHandler(EldatMessageCB aResponseCB, SerialOperationPtr aResponse, ErrorPtr aError)
{
  EldatResponsePtr r = boost::dynamic_pointer_cast<EldatResponse>(aResponse);
  if (r) {
    FOCUSLOG("ELDAT: received answer: %s", r->response.c_str());
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



#endif // ENABLE_ELDAT




