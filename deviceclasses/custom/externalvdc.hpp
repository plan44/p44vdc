//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2015-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__externalvdc__
#define __p44vdc__externalvdc__

#include "p44vdc_common.hpp"

#if ENABLE_EXTERNAL

#include "customdevice.hpp"
#include "jsoncomm.hpp"

using namespace std;

namespace p44 {

  class ExternalDeviceConnector;
  class ExternalVdc;
  class ExternalDevice;

  typedef boost::intrusive_ptr<ExternalDeviceConnector> ExternalDeviceConnectorPtr;

  typedef boost::intrusive_ptr<ExternalDevice> ExternalDevicePtr;
  class ExternalDevice : public CustomDevice
  {
    typedef CustomDevice inherited;

    friend class ExternalVdc;
    friend class ExternalDeviceConnector;

    ExternalDeviceConnectorPtr mDeviceConnector;
    string mTag; ///< the tag to address the device within the devices on the same connection

  public:

    ExternalDevice(Vdc *aVdcP, ExternalDeviceConnectorPtr aDeviceConnector, string aTag, bool aSimpleText);
    virtual ~ExternalDevice();

    ExternalVdc &getExternalVdc();

  protected:

    /// disconnect device. If presence is represented by data stored in the vDC rather than
    /// detection of real physical presence on a bus, this call must clear the data that marks
    /// the device as connected to this vDC (such as a learned-in EnOcean button).
    /// For devices where the vDC can be *absolutely certain* that they are still connected
    /// to the vDC AND cannot possibly be connected to another vDC as well, this call should
    /// return false.
    /// @param aForgetParams if set, not only the connection to the device is removed, but also all parameters related to it
    ///   such that in case the same device is re-connected later, it will not use previous configuration settings, but defaults.
    /// @param aDisconnectResultHandler will be called to report true if device could be disconnected,
    ///   false in case it is certain that the device is still connected to this and only this vDC
    /// @note at the time aDisconnectResultHandler is called, the only owner left for the device object might be the
    ///   aDevice argument to the DisconnectCB handler.
    virtual void disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler) P44_OVERRIDE;

    virtual void sendDeviceApiJsonMessage(JsonObjectPtr aMessage) P44_OVERRIDE;
    virtual void sendDeviceApiSimpleMessage(string aMessage) P44_OVERRIDE;

  };


  typedef map<string,ExternalDevicePtr> ExternalDevicesMap;

  class ExternalDeviceConnector : public P44LoggingObj
  {
    friend class ExternalDevice;

    ExternalVdc &mExternalVdc;

    bool mSimpletext; ///< if set, device communication uses very simple text messages rather than JSON

    JsonCommPtr mDeviceConnection;
    ExternalDevicesMap mExternalDevices;

  public:

    ExternalDeviceConnector(ExternalVdc &aExternalVdc, JsonCommPtr aDeviceConnection);
    virtual ~ExternalDeviceConnector();

    /// @return the per-instance log level offset
    /// @note is virtual because some objects might want to use the log level offset of another object
    virtual int getLogLevelOffset() P44_OVERRIDE;

    /// @return type (such as: device, element, vdc, trigger) of the context object
    virtual string contextType() const P44_OVERRIDE;

    /// @return id identifying the context object
    virtual string contextId() const P44_OVERRIDE;


  private:

    void removeDevice(ExternalDevicePtr aExtDev);
    void closeConnection();
    void handleDeviceConnectionStatus(ErrorPtr aError);
    void handleDeviceApiJsonMessage(ErrorPtr aError, JsonObjectPtr aMessage);
    ErrorPtr handleDeviceApiJsonSubMessage(JsonObjectPtr aMessage);
    void handleDeviceApiSimpleMessage(ErrorPtr aError, string aMessage);

    ExternalDevicePtr findDeviceByTag(string aTag, bool aNoError);
    void sendDeviceApiJsonMessage(JsonObjectPtr aMessage, const char *aTag = NULL);
    void sendDeviceApiSimpleMessage(string aMessage, const char *aTag = NULL);
    void sendDeviceApiStatusMessage(ErrorPtr aError, const char *aTag = NULL);

  };




  typedef boost::intrusive_ptr<ExternalVdc> ExternalVdcPtr;
  class ExternalVdc : public CustomVdc
  {
    typedef CustomVdc inherited;
    friend class ExternalDevice;
    friend class ExternalDeviceConnector;

    SocketCommPtr mExternalDeviceApiServer;

  public:
    ExternalVdc(int aInstanceNumber, const string &aSocketPathOrPort, bool aNonLocal, VdcHost *aVdcHostP, int aTag);

    virtual void initialize(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    virtual const char *vdcClassIdentifier() const P44_OVERRIDE;

    /// scan for (collect) devices and add them to the vdc
    virtual void scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags) P44_OVERRIDE;

    /// @return human readable, language independent suffix to explain vdc functionality.
    ///   Will be appended to product name to create modelName() for vdcs
    virtual string vdcModelSuffix() const P44_OVERRIDE { return "external"; }

    /// get supported rescan modes for this vDC. This indicates (usually to a web-UI) which
    /// of the flags to collectDevices() make sense for this vDC.
    /// @return a combination of rescanmode_xxx bits
    virtual int getRescanModes() const P44_OVERRIDE { return rescanmode_exhaustive; }; // only exhaustive makes sense

    /// identify the vdc to the user in some way
    /// @param aDuration if !=Never, this is how long the identification should be recognizable. If this is \<0, the identification should stop
    /// @note usually, this would be a LED or buzzer in the vdc device (bridge, gateway etc.)
    virtual void identifyToUser(MLMicroSeconds aDuration) P44_OVERRIDE;

  private:

    SocketCommPtr deviceApiConnectionHandler(SocketCommPtr aServerSocketCommP);

  };

} // namespace p44


#endif // ENABLE_EXTERNAL
#endif // __p44vdc__externalvdc__
