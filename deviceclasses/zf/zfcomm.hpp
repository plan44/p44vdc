//
//  Copyright (c) 1-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__zfcomm__
#define __p44vdc__zfcomm__

#include "p44vdc_common.hpp"

#if ENABLE_ZF

#include "serialqueue.hpp"

using namespace std;

namespace p44 {

  // Errors
  typedef enum {
    ZfCommErrorOK,
    ZfCommErrorCmdTimeout,
    ZfCommErrorCmdError,
    ZfCommErrorCompatibility
  } ZfCommErrors;

  class ZfCommError : public Error
  {
  public:
    static const char *domain() { return "ZfComm"; }
    virtual const char *getErrorDomain() const { return ZfCommError::domain(); };
    ZfCommError(ZfCommErrors aError) : Error(ErrorCode(aError)) {};
    ZfCommError(ZfCommErrors aError, std::string aErrorMessage) : Error(ErrorCode(aError), aErrorMessage) {};
  };


  typedef uint32_t ZfAddress;
  typedef char ZfFunction;
  typedef uint8_t ZfMode;


  class ZfComm;

  class ZfPacket;
  typedef boost::intrusive_ptr<ZfPacket> ZfPacketPtr;
  class ZfPacket : public P44Obj
  {
    typedef P44Obj inherited;

    friend class ZfComm;

    uint8_t len;

  public:

    uint8_t opCode;
    ZfAddress uid;
    uint8_t data;
    int8_t rssi;

    ZfPacket();

    /// check data, return NOT_ENOUGH_BYTES if not enough bytes to decide about parsing, or number of bytes consumed by parsing packet
    static size_t getPacket(size_t aNumBytes, uint8_t *aBytes, ZfPacketPtr &aPacket);

    /// @return description of packet for logging
    string description();

  };




  typedef boost::function<void (ZfPacketPtr aZfPacket, ErrorPtr aError)> ZfPacketCB;

  typedef boost::intrusive_ptr<ZfComm> ZfCommPtr;
	// ZF communication
	class ZfComm : public SerialOperationQueue
	{
		typedef SerialOperationQueue inherited;
		
    ZfPacketCB receivedPacketHandler;

    MLTicket aliveCheckTicket; ///< checking for interface being alive

	public:
		
		ZfComm(MainLoop &aMainLoop);
		virtual ~ZfComm();
		
    /// set the connection parameters to connect to the ZF modem
    /// @param aConnectionSpec serial device path (/dev/...) or host name/address[:port] (1.2.3.4 or xxx.yy)
    /// @param aDefaultPort default port number for TCP connection (irrelevant for direct serial device connection)
    void setConnectionSpecification(const char *aConnectionSpec, uint16_t aDefaultPort);

    /// set handler for receiving packets from device which are not answers
    /// @param aPacketHandler the handler will be called when a packet from the device arrives
    ///   which is not an expected response to a command sent
    void setReceivedPacketHandler(ZfPacketCB aPacketHandler);

    /// start the EnOcean modem watchdog (regular version commands, hard reset if no answer in time)
    void initialize(StatusCB aCompletedCB);

    /// called to process extra bytes after all pending operations have processed their bytes
    /// @param aNumBytes number of bytes ready to accept
    /// @param aBytes bytes ready to accept
    /// @return number of extra bytes that could be accepted, 0 if none, NOT_ENOUGH_BYTES if extra bytes would be accepted,
    ///   but not enough of them are ready. Note that NOT_ENOUGH_BYTES may only be used when the SerialQueue has a
    ///   buffer for re-assembling messages (see setAcceptBuffer())
    virtual ssize_t acceptExtraBytes(size_t aNumBytes, uint8_t *aBytes) P44_OVERRIDE;

  protected:

    /// dispatch received ZF interface message to appropriate receiver
    void dispatchMessage(ZfPacket aPacket);

	};



} // namespace p44

#endif // ENABLE_ZF
#endif // __p44vdc__zfcomm__
