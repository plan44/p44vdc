//
//  Copyright (c) 2016-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__eldatcomm__
#define __p44vdc__eldatcomm__

#include "p44vdc_common.hpp"

#if ENABLE_ELDAT

#include "serialqueue.hpp"

using namespace std;

namespace p44 {

  class EldatCommError : public Error
  {
  public:
    typedef enum {
      OK,
      CmdTimeout,
      CmdError,
      Compatibility,
      numErrorCodes
    } ErrorCodes;

    static const char *domain() { return "EldatComm"; }
    virtual const char *getErrorDomain() const P44_OVERRIDE { return EldatCommError::domain(); };
    EldatCommError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
    #if ENABLE_NAMED_ERRORS
  protected:
    virtual const char* errorName() const P44_OVERRIDE { return errNames[getErrorCode()]; };
  private:
    static constexpr const char* const errNames[numErrorCodes] = {
      "OK",
      "CmdTimeout",
      "CmdError",
      "Compatibility"
    };
    #endif // ENABLE_NAMED_ERRORS
  };


  typedef uint32_t EldatAddress;
  typedef char EldatFunction;
  typedef uint8_t EldatMode;

  class EldatComm;

  typedef boost::function<void (string aEldatAnswer, ErrorPtr aError)> EldatMessageCB;

  typedef boost::intrusive_ptr<EldatComm> EldatCommPtr;
	// Eldat communication
	class EldatComm : public SerialOperationQueue
	{
		typedef SerialOperationQueue inherited;
		
    EldatMessageCB receivedMessageHandler;

    MLTicket aliveCheckTicket; ///< checking for interface being alive

    // Eldat module product identification and version
    uint16_t usbPid;
    uint16_t appVersion;

	public:
		
		EldatComm(MainLoop &aMainLoop);
		virtual ~EldatComm();
		
    /// set the connection parameters to connect to the ELDAT modem
    /// @param aConnectionSpec serial device path (/dev/...) or host name/address[:port] (1.2.3.4 or xxx.yy)
    /// @param aDefaultPort default port number for TCP connection (irrelevant for direct serial device connection)
    void setConnectionSpecification(const char *aConnectionSpec, uint16_t aDefaultPort);

    /// set handler for receiving messages from device which are not answers
    /// @param aMessageHandler the handler will be called when a command from the device arrives
    ///   which is not an expected response to a command sent
    void setReceivedMessageHandler(EldatMessageCB aMessageHandler);

    /// start the ELDAT modem watchdog (regular version commands, hard reset if no answer in time)
    void initialize(StatusCB aCompletedCB);

    /// called to process extra bytes after all pending operations have processed their bytes
    /// @param aNumBytes number of bytes ready to accept
    /// @param aBytes bytes ready to accept
    /// @return number of extra bytes that could be accepted, 0 if none, NOT_ENOUGH_BYTES if extra bytes would be accepted,
    ///   but not enough of them are ready. Note that NOT_ENOUGH_BYTES may only be used when the SerialQueue has a
    ///   buffer for re-assembling messages (see setAcceptBuffer())
    virtual ssize_t acceptExtraBytes(size_t aNumBytes, uint8_t *aBytes) P44_OVERRIDE;

    /// send a command and await response
    /// @param aCommand a ELDAT command
    /// @param aResponseCB callback to deliver command response to
    void sendCommand(string aCommand, EldatMessageCB aResponseCB);


  protected:

    /// dispatch received Eldat interface string to appropriate receiver
    void dispatchMessage(string aMessage);

  private:

    void initializeInternal(StatusCB aCompletedCB, int aRetriesLeft);
    void versionReceived(StatusCB aCompletedCB, int aRetriesLeft, string aAnswer, ErrorPtr aError);
    void initError(StatusCB aCompletedCB, int aRetriesLeft, ErrorPtr aError);

    void aliveCheck();
    void aliveCheckResponse(string aAnswer, ErrorPtr aError);

    void resetDone();
    void reopenConnection();

    void commandResponseHandler(EldatMessageCB aResponseCB, SerialOperationPtr aResponse, ErrorPtr aError);

	};



} // namespace p44

#endif // ENABLE_ELDAT
#endif // __p44vdc__eldatcomm__
