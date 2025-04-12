//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__ds485comm__
#define __p44vdc__ds485comm__

#include "p44vdc_common.hpp"

#if ENABLE_DS485DEVICES

#include "dsuid.hpp"

#include "dsuid.h"
//#include "dsm-api.h"
#include "dsm-api-const.h"
#include "ds485-client.h"

using namespace std;

namespace p44 {

  class Ds485CommError : public Error
  {
  public:
    typedef int ErrorCodes;

    static const char *domain() { return "Ds485Comm"; }
    virtual const char *getErrorDomain() const P44_OVERRIDE { return Ds485CommError::domain(); };
    Ds485CommError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
    #if ENABLE_NAMED_ERRORS
  protected:
    virtual const char* errorName() const P44_OVERRIDE;
    #endif // ENABLE_NAMED_ERRORS
  };





  class Ds485Comm;
  class Ds485Vdc;

  typedef boost::intrusive_ptr<Ds485Comm> Ds485CommPtr;
  class Ds485Comm final : public P44LoggingObj
  {
    typedef P44LoggingObj inherited;
    friend class Ds485Vdc;

    ChildThreadWrapperPtr mDs485ClientThread;
    MLTicket mDs485ThreadRestarter;
    MLTicket mConnectDelay;

    ds485ClientHandle_t mDs485Client;
    ds485c_callbacks mDs485Callbacks;

    string mApiHost; ///< ds485 host IP (or "tunnel")
    uint16_t mApiPort; ///< ds485 host port

    DsUidPtr mMyDsuid; ///< the dSUID of my role as a client

    string mDs485HostIP;
    string mTunnelCommandTemplate;
    MLTicket mTunnelRestarter;
    pid_t mTunnelPid;

  public:

    Ds485Comm();
    virtual ~Ds485Comm();

    virtual string contextType() const P44_OVERRIDE { return "DS485"; }

    /// @param aConnectionSpec in host[:port] format
    /// @param aDefaultPort port number that is used when aConnectionSpec does not specify a port
    /// @param aTunnelCommandTemplate template for shell command to establish tunnel if not empty.
    ///   %HOST% will be replaced with vDSM IP if known, otherwise with host in aConnectionSpec,
    ///   %PORT% will be replaced with port in aConnectionSpec.
    void setConnectionSpecification(const char *aConnectionSpec, uint16_t aDefaultPort, const char* aTunnelCommandTemplate);

    void start(StatusCB aCompletedCB);
    void stop();

    /// @name callback receivers
    /// @{
    int linkStateChanged(bool aActive);
    int busMemberChanged(DsUidPtr aDsUid, bool aJoined);
    int containerReceived(const ds485_container_t *container);
    /// @}

    /// @name payload manipulation helpers
    /// @{
    static void payload_append8(string &aPayload, uint8_t aByte);
    static void payload_append16(string &aPayload, uint16_t aWord);
    static void payload_append32(string &aPayload, uint32_t aLongWord);
    static void payload_appendString(string &aPayload, size_t aFieldSize, const string aString);
    static size_t payload_get8(string &aPayload, size_t aAtIndex, uint8_t &aByte);
    static size_t payload_get16(string &aPayload, size_t aAtIndex, uint16_t &aWord);
    static size_t payload_get32(string &aPayload, size_t aAtIndex, uint32_t &aLongWord);
    static size_t payload_get64(string &aPayload, size_t aAtIndex, uint64_t &aLongLongWord);
    static size_t payload_getString(string &aPayload, size_t aAtIndex, size_t aFieldSize, string &aString);
    /// @}


  private:

    void connect(StatusCB aCompletedCB);
    void establishTunnel();
    void tunnelCollapsed(ErrorPtr aError, const string &aOutputString);


    void logContainer(int aLevel, const ds485_container_t& container, const char *aLabel);
    void setupRequestContainer(ds485_container& aContainer, DsUidPtr aDestination, DsUidPtr aSource, const string aPayload);
    void setupRequestCommand(ds485_container& aContainer, DsUidPtr aDestination, uint8_t aCommand, uint8_t aModifier, const string& aPayload = "");

    /// @name synchronously executing, blocking calls, only to use from mDs485ClientThread
    /// @{

    ErrorPtr executeQuerySync(string& aResponse, MLMicroSeconds aTimeout, DsUidPtr aDestination, uint8_t aCommand, uint8_t aModifier = 0, const string& aPayload = "");

    /// @}

    void ds485ClientThread(ChildThreadWrapper &aThread);
    void ds485ClientThreadSignal(ChildThreadWrapper &aChildThread, ThreadSignals aSignalCode);

  };

} // namespace p44


#endif // ENABLE_DS485DEVICES
#endif // __p44vdc__ds485comm__
