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


#ifndef __p44vdc__dalicomm__
#define __p44vdc__dalicomm__

#include "p44vdc_common.hpp"

#if ENABLE_DALI

#include "serialqueue.hpp"

#include "dalidefs.h"


using namespace std;

// Note: before 2015-02-27, we had a bug which caused the last extra byte not being read, so the checksum reached zero
// only if the last byte was 0. We also passed the if checksum was 0xFF, because our reference devices always had 0x01 in
// the last byte, and I assumed missing by 1 was the result of not precise enough specs or a bug in the device.
// If OLD_BUGGY_CHKSUM_COMPATIBLE is defined, extra checks will be made to keep already-in-use devices identified by
// shortaddr to avoid messing up existing installations
#if P44_BUILD_DIGI
  // we need this only for the DIGI based devices as these existed in the field before 2015-02-27
  #define OLD_BUGGY_CHKSUM_COMPATIBLE 1
#endif


// MARK: - DALI Bridge commands and responses

// bridge commands
// - one byte commands 0..7
#define CMD_CODE_RESET 0  // reset
#define CMD_CODE_VERSION 1 // get version

// - three byte commands 8..n
#define CMD_CODE_SEND16 0x10 // send 16-bit DALI sequence, return RESP_ACK when done
#define CMD_CODE_2SEND16 0x11 // double send 16-bit DALI sequence with 10mS gap in between, return RESP_ACK when done
#define CMD_CODE_SEND16_REC8 0x12 // send 16-bit command, receive one 8-bit DALI response, return RESP_DATA when data received, RESP_ACK with error code
// - 3 byte debug commands
#define CMD_CODE_ECHO_DATA1 0x41 // returns RESP_DATA echoing DATA1
#define CMD_CODE_ECHO_DATA2 0x42 // returns RESP_DATA echoing DATA2
#define CMD_CODE_OVLRESET 0x43 // reset overload, DATA1 0=autoreset enabled, 1=autoreset disabled, 0x55=overload detector disabled
#define CMD_CODE_EDGEADJ 0x44 // set DALI sending edge adjustment, DATA1=sending delay for going-inactive edge, DATA2=delay of sampling point, in number of 1/256th periods (with actual resolution of 1/16th bit for now)

// bridge responses/events
// - responses
#define RESP_CODE_ACK 0x2A // * reponse for all commands that do not return data, second byte is status
#define RESP_CODE_ACK_RETRIED 0x2B // + command has executed with at least one retry
#define RESP_CODE_DATA 0x3D // = response for commands that return data, second byte is data
#define RESP_CODE_DATA_RETRIED 0x3E // > response with data, but has executed with at least one retry
// - events
#define EVENT_CODE_FOREIGN_FRAME 0x23 // # received DALI frame sent by another master on the bus (two bytes follow)
// - ACK status codes
#define ACK_OK 0x30 // ok status
#define ACK_TIMEOUT 0x31 // timeout receiving from DALI
#define ACK_FRAME_ERR 0x32 // rx frame error
#define ACK_OVERLOAD 0x33 // bus overload (max current for longer period = possibly shortened)
#define ACK_INVALIDCMD 0x39 // invalid command

//#define BUFFERED_BRIDGE_RESPONSES_HIGH 35 // Rx buf in bridge is 80 bytes = 40 answers, only use 35 to make sure
//#define BUFFERED_BRIDGE_RESPONSES_LOW 5 // low watermark to restart sending
#define BUFFERED_BRIDGE_RESPONSES_HIGH 5 // Conservative to prevent lockup
#define BUFFERED_BRIDGE_RESPONSES_LOW 2 // low watermark to restart sending



namespace p44 {

  // MARK: - DaliComm


  class DaliCommError : public Error
  {
  public:
    // Errors
    typedef enum {
      OK,
      Busy,
      BridgeComm,
      BridgeCmd,
      BridgeUnknown,
      DALIFrame,
      MissingData,
      BadData,
      InvalidAnswer,
      AddressesMissing,
      AddressCollisions,
      DeviceSearch,
      SetShortAddress,
      BusOverload,
      DataUnreliable,
      DataMissing,
      NoAddress,
      numErrorCodes
    } ErrorCodes;

    static const char *domain() { return "DaliComm"; }
    virtual const char *getErrorDomain() const P44_OVERRIDE { return DaliCommError::domain(); };
    DaliCommError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
    #if ENABLE_NAMED_ERRORS
  protected:
    virtual const char* errorName() const P44_OVERRIDE { return errNames[getErrorCode()]; };
  private:
    static constexpr const char* const errNames[numErrorCodes] = {
      "OK",
      "Busy",
      "BridgeComm",
      "BridgeCmd",
      "BridgeUnknown",
      "DALIFrame",
      "MissingData",
      "BadData",
      "InvalidAnswer",
      "AddressesMissing",
      "AddressCollisions",
      "DeviceSearch",
      "SetShortAddress",
      "BusOverload",
      "DataUnreliable",
      "DataMissing",
      "NoAddress"
    };
    #endif // ENABLE_NAMED_ERRORS
  };



  class DaliComm;


  /// abstracted DALI bus address
  typedef uint8_t DaliAddress;
  const DaliAddress DaliSingle = 0x00; // dali single address
  const DaliAddress DaliGroup = 0x80; // marks group address
  const DaliAddress DaliBroadcast = 0xFF; // all devices on the bus
  const DaliAddress DaliAddressMask = 0x3F; // address mask
  const DaliAddress DaliAddressTypeMask = 0xC0; // address type
  const DaliAddress DaliGroupMask = 0x0F; // group address mask
  // - pseudo-address for scene numbers
  const DaliAddress DaliScene = 0x40; // marks scene number
  const DaliAddress DaliSceneMask = 0x0F; // scene number mask
  // - pseudo-address for "no address"
  const DaliAddress NoDaliAddress = 0xC0; // not an address (but a command)

  /// abstracted DALI command (possibly including a DT prefix)
  typedef uint16_t DaliCommand;


  /// DALI device information record
  class DaliDeviceInfo : public P44Obj
  {
  public:
    typedef enum {
      devinf_needsquery, // only address for NOW, but we should try to load devinf
      devinf_none, // only address, we KNOW there's no devinf for this device
      devinf_only_gtin, // only GTIN is known, but serial number is invalid
      #if OLD_BUGGY_CHKSUM_COMPATIBLE
      devinf_maybe,
      #endif
      devinf_solid,
      #if OLD_BUGGY_CHKSUM_COMPATIBLE
      devinf_notForID
      #endif
    } DaliDevInfStatus;

    DaliDeviceInfo();
    /// clear everything except short address
    void clear();
    /// short address
    DaliAddress shortAddress;
    // DALI device information
    uint8_t vers_101; ///< version of the IEC 62386-101 standard used, byte encoded, see DALI_STD_VERS_* macros
    uint8_t vers_102; ///< version of the IEC 62386-102 standard used for all control gear in this bus unit, byte encoded, see DALI_STD_VERS_* macros
    uint8_t vers_103; ///< version of the IEC 62386-103 standard used for all control devices in this bus unit, byte encoded, see DALI_STD_VERS_* macros
    uint64_t gtin; /// < 48 bit global trade identification number (GTIN / EAN)
    uint8_t fw_version_major; /// < major firmware version
    uint8_t fw_version_minor; /// < minor firmware version
    uint64_t serialNo; /// < unique serial number
    // OEM product information
    uint64_t oem_gtin; /// < 48 bit global trade identification number of OEM product (GTIN / EAN)
    uint64_t oem_serialNo; /// < unique serial number
    /// text description
    string description();
    /// status of device info
    DaliDevInfStatus devInfStatus;
  };
  typedef boost::intrusive_ptr<DaliDeviceInfo> DaliDeviceInfoPtr;



  /// callback for DALI bridge events (not part of a command)
  typedef boost::function<void (uint8_t aEvent, uint8_t aData1, uint8_t aData2)> DaliBridgeEventCB;

  /// callback function for sendBridgeCommand
  typedef boost::function<void (uint8_t aResp1, uint8_t aResp2, ErrorPtr aError)> DaliBridgeResultCB;



  typedef boost::intrusive_ptr<DaliComm> DaliCommPtr;

  /// A class providing low level access to the DALI bus
  class DaliComm : public SerialOperationQueue
  {
    typedef SerialOperationQueue inherited;

    int runningProcedures;
    bool multiMaster;

    bool isBusy();
    static ErrorPtr busyError() { return ErrorPtr(new DaliCommError(DaliCommError::Busy)); };

    MLMicroSeconds closeAfterIdleTime;
    MLTicket connectionTimeoutTicket;

    MLTicket pingTicket; ///< timer for DALI single master PING

    int expectedBridgeResponses; ///< not yet received bridge responses
    bool responsesInSequence; ///< set when repsonses need to be in sequence with requests

    uint8_t sendEdgeAdj; ///< adjustment for sending rising edge - first param to CMD_CODE_EDGEADJ
    uint8_t samplePointAdj; ///< adjustment for sampling point - second param to CMD_CODE_EDGEADJ

    DaliBridgeEventCB bridgeEventHandler; ///< will be called for bridge events

  public:

    // statistics
    long retriedReads;
    long retriedWrites;

    bool dali2ScanLock; ///< if set, scanner will interpret memory bank 0 as DALI 1.0 (because there is no real backwards compatibility between 1.0 and 2.0)

    DaliComm(MainLoop &aMainLoop = MainLoop::currentMainLoop());
    virtual ~DaliComm();

    void startProcedure();
    void endProcedure();

    /// @name low level DALI bus communication
    /// @{

    /// set the connection parameters to connect to the DALI bridge
    /// @param aConnectionSpec serial device path (/dev/...) or host name/address[:port] (1.2.3.4 or xxx.yy)
    /// @param aDefaultPort default port number for TCP connection (irrelevant for direct serial device connection)
    /// @param aCloseAfterIdleTime if not Never, serial port will be closed after being idle for the specified time
    void setConnectionSpecification(const char *aConnectionSpec, uint16_t aDefaultPort, MLMicroSeconds aCloseAfterIdleTime);

    /// accept extra bytes to resynchronize bridge
    virtual ssize_t acceptExtraBytes(size_t aNumBytes, uint8_t *aBytes);

    /// set DALI edge adjustment
    /// @param how much (in 1/256th DALI bit time units) to delay the going inactive edge of the sending signal, to compensate for slow falling (going active) edge on the bus
    void setDaliSendAdj(uint8_t aSendEdgeDelay) { sendEdgeAdj = aSendEdgeDelay; };

    /// @param how much (in 1/256th DALI bit time units) to delay or advance the sample point when receiving DALI data
    void setDaliSampleAdj(int8_t aSamplePointDelay) { samplePointAdj = (uint8_t)aSamplePointDelay; };

    /// set event handler
    /// @param aEventHandler will be called when a dali bridge event is received
    void setBridgeEventHandler(DaliBridgeEventCB aEventHandler) { bridgeEventHandler = aEventHandler; };

    /// Send DALI command to bridge
    /// @param aCmd bridge command byte
    /// @param aDali1 first DALI byte
    /// @param aDali2 second DALI byte
    /// @param aResultCB callback executed when bridge response arrives
    /// @param aWithDelay if>0, time (in microseconds) to delay BEFORE sending the command
    void sendBridgeCommand(uint8_t aCmd, uint8_t aDali1, uint8_t aDali2, DaliBridgeResultCB aResultCB, int aWithDelay = -1);

    /// callback function for daliSendXXX methods
    typedef boost::function<void (ErrorPtr aError, bool aRetried)> DaliCommandStatusCB;

    /// reset the communication with the bridge
    void reset(DaliCommandStatusCB aStatusCB);

    /// Send two byte DALI bus command
    /// @param aDali1 first DALI byte
    /// @param aDali2 second DALI byte
    /// @param aStatusCB status callback
    /// @param aWithDelay if>0, time (in microseconds) to delay BEFORE sending the command
    void daliSend(uint8_t aDali1, uint8_t aDali2, DaliCommandStatusCB aStatusCB = NULL, int aWithDelay = -1);

    /// @param aAddress DALI address (device short address, or group address + DaliGroup, or DaliBroadcast)
    /// @param aPower Arc power
    /// @param aStatusCB status callback
    /// @param aWithDelay if>0, time (in microseconds) to delay BEFORE sending the command
    void daliSendDirectPower(DaliAddress aAddress, uint8_t aPower, DaliCommandStatusCB aStatusCB = NULL, int aWithDelay = -1);

    /// @param aAddress DALI address (device short address, or group address + DaliGroup, or DaliBroadcast)
    /// @param aCommand command (LSB=DALI command, MSB=device type to be selected first (0:no device type, 0xFF:DT0, other x=DTx)
    /// @param aStatusCB status callback
    /// @param aWithDelay if>0, time (in microseconds) to delay BEFORE sending the command
    void daliSendCommand(DaliAddress aAddress, DaliCommand aCommand, DaliCommandStatusCB aStatusCB = NULL, int aWithDelay = -1);

    /// @param aAddress DALI address (device short address, or group address + DaliGroup, or DaliBroadcast)
    /// @param aCommand command (LSB=DALI command, MSB=device type to be selected first (0:no device type, 0xFF:DT0, other x=DTx)
    /// @param aDTRValue the value to be sent to DTR before executing aCommand
    /// @param aStatusCB status callback
    /// @param aWithDelay if>0, time (in microseconds) to delay BEFORE sending the command
    void daliSendDtrAndCommand(DaliAddress aAddress, DaliCommand aCommand, uint8_t aDTRValue, DaliCommandStatusCB aStatusCB = NULL, int aWithDelay = -1);

    /// Send two byte DALI bus command twice within 100ms
    /// @param aDali1 first DALI byte
    /// @param aDali2 second DALI byte
    /// @param aStatusCB status callback
    /// @param aWithDelay if>0, time (in microseconds) to delay BEFORE sending the command
    void daliSendTwice(uint8_t aDali1, uint8_t aDali2, DaliCommandStatusCB aStatusCB = NULL, int aWithDelay = -1);

    /// Send DALI config command (send twice within 100ms)
    /// @param aAddress DALI address (device short address, or group address + DaliGroup, or DaliBroadcast)
    /// @param aCommand command (LSB=DALI command, MSB=device type to be selected first (0:no device type, 0xFF:DT0, other x=DTx)
    /// @param aStatusCB status callback
    /// @param aWithDelay if>0, time (in microseconds) to delay BEFORE sending the command
    void daliSendConfigCommand(DaliAddress aAddress, DaliCommand aCommand, DaliCommandStatusCB aStatusCB = NULL, int aWithDelay = -1);

    /// @param aAddress DALI address (device short address, or group address + DaliGroup, or DaliBroadcast)
    /// @param aCommand command (LSB=DALI command, MSB=device type to be selected first (0:no device type, 0xFF:DT0, other x=DTx)
    /// @param aDTRValue the value to be sent to DTR before executing aCommand
    /// @param aStatusCB status callback
    /// @param aWithDelay if>0, time (in microseconds) to delay BEFORE sending the command
    void daliSendDtrAndConfigCommand(DaliAddress aAddress, DaliCommand aCommand, uint8_t aDTRValue, DaliCommandStatusCB aStatusCB = NULL, int aWithDelay = -1);

    /// callback function for daliSendXXX methods returning data
    typedef boost::function<void (bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError, bool aRetried)> DaliQueryResultCB;

    /// Send DALI command and expect answer byte
    /// @param aDali1 first DALI byte
    /// @param aDali2 second DALI byte
    /// @param aResultCB result callback
    /// @param aWithDelay if>0, time (in microseconds) to delay BEFORE sending the command
    void daliSendAndReceive(uint8_t aDali1, uint8_t aDali2, DaliQueryResultCB aResultCB, int aWithDelay = -1);

    /// @param aAddress DALI address (device short address, or group address + DaliGroup, or DaliBroadcast)
    /// @param aQueryCommand command (LSB=DALI command, MSB=device type to be selected first (0:no device type, 0xFF:DT0, other x=DTx)
    /// @param aResultCB result callback
    /// @param aWithDelay if>0, time (in microseconds) to delay BEFORE sending the command
    void daliSendQuery(DaliAddress aAddress, DaliCommand aQueryCommand, DaliQueryResultCB aResultCB, int aWithDelay = -1);

    /// @param aAddress DALI address (device short address, or group address + DaliGroup, or DaliBroadcast)
    /// @param aQueryCommand command (LSB=DALI command, MSB=device type to be selected first (0:no device type, 0xFF:DT0, other x=DTx)
    /// @param aResultCB result callback
    /// @param aWithDelay if>0, time (in microseconds) to delay BEFORE sending the command
    void daliSendDtrAndQuery(DaliAddress aAddress, DaliCommand aQueryCommand, uint8_t aDTRValue, DaliQueryResultCB aResultCB, int aWithDelay = -1);

    /// callback function for daliSendDTXXX methods returning data
    typedef boost::function<void (uint16_t a16BitResult, ErrorPtr aError)> Dali16BitValueQueryResultCB;

    /// @param aAddress DALI address (device short address, or group address + DaliGroup, or DaliBroadcast)
    /// @param aCommand command (LSB=DALI command, MSB=device type to be selected first (0:no device type, 0xFF:DT0, other x=DTx)
    /// @param aValue16 a 16-bit value to be put into DTR1 (MSB) and DTR (LSB) before sending the command
    /// @param aStatusCB status callback
    /// @param aWithDelay if>0, time (in microseconds) to delay BEFORE sending the command
    void daliSend16BitValueAndCommand(DaliAddress aAddress, DaliCommand aCommand, uint16_t aValue16, DaliCommandStatusCB aStatusCB = NULL, int aWithDelay = -1);

    /// @param aAddress DALI address (device short address, or group address + DaliGroup, or DaliBroadcast)
    /// @param aCommand command (LSB=DALI command, MSB=device type to be selected first (0:no device type, 0xFF:DT0, other x=DTx)
    /// @param aValue0,1,2 3 8-bit values to be put into DTR, DTR1, DTR2 before sending the command
    /// @param aStatusCB status callback
    /// @param aWithDelay if>0, time (in microseconds) to delay BEFORE sending the command
    void daliSend3x8BitValueAndCommand(DaliAddress aAddress, DaliCommand aCommand, uint8_t aValue0, uint8_t aValue1, uint8_t aValue2, DaliCommandStatusCB aStatusCB = NULL, int aWithDelay = -1);

    /// Send DALI query for 16-bit value, when MSB is returned by query and LSB is returned in DTR
    /// @param aAddress DALI address (device short address, or group address + DaliGroup, or DaliBroadcast)
    /// @param aQueryCommand command (LSB=DALI command, MSB=device type to be selected first (0:no device type, 0xFF:DT0, other x=DTx)
    /// @param aResultCB result callback
    /// @param aWithDelay if>0, time (in microseconds) to delay BEFORE sending the command
    void daliSend16BitQuery(DaliAddress aAddress, DaliCommand aQueryCommand, Dali16BitValueQueryResultCB aResultCB, int aWithDelay = -1);

    /// Send DALI query for 16-bit value, when MSB is returned by query and LSB is returned in DTR
    /// @param aAddress DALI address (device short address, or group address + DaliGroup, or DaliBroadcast)
    /// @param aQueryCommand command (LSB=DALI command, MSB=device type to be selected first (0:no device type, 0xFF:DT0, other x=DTx)
    /// @param aDTRValue the value to be sent to DTR before executing aQueryCommand
    /// @param aResultCB result callback
    /// @param aWithDelay if>0, time (in microseconds) to delay BEFORE sending the command
    void daliSendDtrAnd16BitQuery(DaliAddress aAddress, DaliCommand aQueryCommand, uint8_t aDTRValue, Dali16BitValueQueryResultCB aResultCB, int aWithDelay = -1);

    /// helper to check daliSendQuery() callback response for a DALI YES answer
    static bool isYes(bool aNoOrTimeout, uint8_t aResponse, ErrorPtr &aError, bool aCollisionIsYes);

    /// utility function to create address byte
    /// @param aAddress DALI address (device short address, or group address + DaliGroup, or DaliBroadcast)
    /// @return first DALI byte for use in daliSend/daliSendTwice
    static uint8_t dali1FromAddress(DaliAddress aAddress);

    /// utility function to decode address byte
    /// @param DALI-style bAAAAAAx address byte, as returned by some query commands
    /// @return DALI address (device short address, or group address + DaliGroup, or DaliBroadcast)
    static DaliAddress addressFromDaliResponse(uint8_t aAnswer);

    /// utility function to format DaliAddress
    /// @param aAddress DALI address (device short address, or group address + DaliGroup, or scene number + DaliScene or DaliBroadcast)
    static string formatDaliAddress(DaliAddress aAddress);

    /// @}

    /// @name high level DALI bus services
    /// @{

    typedef std::list<DaliAddress> ShortAddressList;
    typedef boost::shared_ptr<ShortAddressList> ShortAddressListPtr;
    /// callback function for daliScanBus
    typedef boost::function<void (ShortAddressListPtr aShortAddressListPtr, ShortAddressListPtr aUnreliableShortAddressListPtr, ErrorPtr aError)> DaliBusScanCB;

    /// Scan the bus for active devices (short address)
    /// @param aResultCB callback receiving a list<int> of available short addresses on the bus
    void daliBusScan(DaliBusScanCB aResultCB);

    /// Scan the bus for devices by random address search
    /// @param aResultCB callback receiving a list<int> of available short addresses on the bus
    /// @param aFullScanOnlyIfNeeded if set, a full scan will only happen when address conflicts or missing
    ///   short addresses are detected during quick scan, and scanning will never cause existing short address
    ///   assignments to change (but new addresses might be assigned to devices w/o short address). This means
    ///   that addressing conflicts cannot be resolved in this mode, but also that short addresse based dSUIDs are
    ///   not in danger of getting changed/mixed up. Therefore, this is the recommended mode for most scan situations.
    /// @note detects short address conflicts and devices without short address, assigns new short addresses as needed
    void daliFullBusScan(DaliBusScanCB aResultCB, bool aFullScanOnlyIfNeeded);


    /// Test the reliability of writing and reading back data from a specific device
    /// @param aResultCB callback receiving ok or error
    /// @param aAddress short address of device to test
    /// @param aNumCycles number of R/W cycles to perform (on DTR of the device)
    /// @note does not check running procedure, so make sure no other procedure is running before calling this
    void daliBusTestData(StatusCB aResultCB, DaliAddress aAddress, uint8_t aNumCycles);


    /// a single DALI bank memory cell
    typedef struct MemoryCell {
      uint8_t b; ///< the byte value of the cell
      bool no; ///< if set, the value of the cell is NO (timeout when accessing)
      MemoryCell(uint8_t aB, bool aNo = false) : b(aB), no(aNo) {};
    } MemoryCell;
    /// a series of DALI memory cells
    typedef std::vector<MemoryCell> MemoryVector;
    typedef boost::shared_ptr<std::vector<MemoryCell> > MemoryVectorPtr;

    /// callback function for daliReadMemory
    typedef boost::function<void (MemoryVectorPtr aMemoryVectorPtr, ErrorPtr aError)> DaliReadMemoryCB;

    /// Read DALI memory
    /// @param aResultCB callback receiving the data read as a vector<uint8_t>
    /// @param aAddress short address of device to read
    /// @param aBank memory bank to read
    /// @param aOffset offset to start reading
    /// @param aNumBytes number of bytes to read
    /// @param aMemory optional existing memory vector to append to
    /// @note reading none or less data than requested is not considered an error - aMemoryVectorPtr param in callback will
    ///   just return the number of bytes that could be read; check its size to make sure expected result was returned
    void daliReadMemory(DaliReadMemoryCB aResultCB, DaliAddress aAddress, uint8_t aBank, uint8_t aOffset, uint16_t aNumBytes, DaliComm::MemoryVectorPtr aMemory = DaliComm::MemoryVectorPtr());

    /// callback function for daliReadDeviceInfo
    typedef boost::function<void (DaliDeviceInfoPtr aDaliDeviceInfoPtr, ErrorPtr aError)> DaliDeviceInfoCB;

    /// Read DALI device info
    /// @param aResultCB callback receiving the device info record
    /// @param aAddress short address of device to read device info from
    void daliReadDeviceInfo(DaliDeviceInfoCB aResultCB, DaliAddress aAddress);

    /// @}

  private:

    void resetIssued(int aCount, DaliCommandStatusCB aStatusCB, uint8_t aResp1, uint8_t aResp2, ErrorPtr aError);

    void singleMasterPing(MLTimer &aMLTimer);

    void daliPrepareForCommand(DaliCommand &aCommand, int &aWithDelay);
    void msbOf16BitQueryReceived(DaliAddress aAddress, Dali16BitValueQueryResultCB aResult16CB, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError);
    void lsbOf16BitQueryReceived(uint16_t aResult16, Dali16BitValueQueryResultCB aResult16CB, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError);

    void bridgeResponseHandler(DaliBridgeResultCB aBridgeResultHandler, SerialOperationReceivePtr aOperation, ErrorPtr aError);
    void daliCommandStatusHandler(DaliCommandStatusCB aResultCB, uint8_t aResp1, uint8_t aResp2, ErrorPtr aError);
    void daliQueryResponseHandler(DaliQueryResultCB aResultCB, uint8_t aResp1, uint8_t aResp2, ErrorPtr aError);
    void connectionTimeout();



  };

} // namespace p44

#endif // ENABLE_DALI
#endif // __p44vdc__dalicomm__
