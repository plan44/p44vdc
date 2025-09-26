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

#ifndef __p44vdc__wbfvdc__
#define __p44vdc__wbfvdc__

#include "p44vdc_common.hpp"

#if ENABLE_WBF

#include "jsonwebclient.hpp"

#include "wbfcomm.hpp"
#include "vdc.hpp"
#include "dsbehaviour.hpp"
#include "wbfdevice.hpp"

using namespace std;

namespace p44 {


  class WbfVdc;
  typedef boost::intrusive_ptr<WbfVdc> WbfVdcPtr;
  class WbfDevice;
  typedef boost::intrusive_ptr<WbfDevice> WbfDevicePtr;

  /// persistence for Wbf device container
  class WbfPersistence : public SQLite3TableGroup
  {
    typedef SQLite3TableGroup inherited;
  protected:
    /// Get DB Schema creation/upgrade SQL statements
    virtual string schemaUpgradeSQL(int aFromVersion, int &aToVersion);
  };


  class WbfVdc : public Vdc
  {
    typedef Vdc inherited;
    friend class WbfDevice;

    WbfPersistence mDb;

    /// @name info retrieved from gateway
    /// @{

    string mSerialNo; ///< gateway serial number from /api/info
    string mSwVersion; ///< gateway software version
    string mApiVersion; ///< gateway API version

    /// @}

    /// @name internal state
    /// @{

    MLTicket mRefindTicket;
    PartIdToBehaviourMap mLoadsMap;
    PartIdToBehaviourMap mSensorsMap;
    PartIdToBehaviourMap mButtonsMap;
    MLTicket mButtonActivationTimeout;
    VdcApiRequestPtr mButtonActivationRequest;

    /// @}



  public:

    WbfVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag);
    virtual ~WbfVdc();

    /// set the log level offset on this logging object (and possibly contained sub-objects)
    /// @param aLogLevelOffset the new log level offset
    virtual void setLogLevelOffset(int aLogLevelOffset) P44_OVERRIDE;

    /// get logging object for a named topic
    virtual P44LoggingObj* getTopicLogObject(const string aTopic) P44_OVERRIDE;

    WbfComm mWbfComm;

		virtual void initialize(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    virtual const char *vdcClassIdentifier() const P44_OVERRIDE;

    /// get supported rescan modes for this vDC
    /// @return a combination of rescanmode_xxx bits
    virtual int getRescanModes() const P44_OVERRIDE;

    /// scan for (collect) devices and add them to the vdc
    virtual void scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags) P44_OVERRIDE;

    /// vdc level methods
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams) P44_OVERRIDE;

    /// set container learn mode
    /// @param aEnableLearning true to enable learning mode
    /// @param aDisableProximityCheck true to disable proximity check (e.g. minimal RSSI requirement for some radio devices)
    /// @param aOnlyEstablish set this to yes to only learn in, to no to only learn out or to undefined to allow both learn-in and out.
    /// @note learn events (new devices found or devices removed) must be reported by calling reportLearnEvent() on VdcHost.
    virtual void setLearnMode(bool aEnableLearning, bool aDisableProximityCheck, Tristate aOnlyEstablish) P44_OVERRIDE;

    /// @return human readable, language independent suffix to explain vdc functionality.
    ///   Will be appended to product name to create modelName() for vdcs
    virtual string vdcModelSuffix() const P44_OVERRIDE { return "wbf"; }

    /// @return human readable model version specific to that vDC
    virtual string vdcModelVersion() const P44_OVERRIDE { return mApiVersion.empty() ? mSwVersion : mSwVersion + "/" + mApiVersion; };

    /// @return hardware GUID in URN format to identify hardware as uniquely as possible
    /// - uuid:UUUUUUU = UUID
    /// - http:xxxx = API
    virtual string hardwareGUID() P44_OVERRIDE;

    /// @return Vendor name for display purposes
    /// @note if not empty, value will be used by vendorId() default implementation to create vendorname:xxx URN schema id
    virtual string vendorName() P44_OVERRIDE;

    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

    /// Get extra info (plan44 specific) to describe the addressable in more detail
    /// @return string, single line extra info describing aspects of the device not visible elsewhere
    virtual string getExtraInfo() P44_OVERRIDE;

    /// Remove device known no longer connected to the system (for example: explicitly unlearned EnOcean switch)
    /// @param aDevice a device object which has a valid dSUID
    /// @param aForget if set, all parameters stored for the device will be deleted. Note however that
    ///   the device is not disconnected (=unlearned) by this.
    virtual void removeDevice(DevicePtr aDevice, bool aForget = false) P44_OVERRIDE;

  protected:

    /// handle global events
    /// @param aEvent the event to handle
    virtual void handleGlobalEvent(VdchostEvent aEvent) P44_OVERRIDE;

  private:

    void connectGateway(StatusCB aCompletedCB);
    void refindResultHandler(StatusCB aCompletedCB, ErrorPtr aError);
    void startupGatewayApi(StatusCB aCompletedCB);
    void apiIsStopped(StatusCB aCompletedCB);
    void apiIsStarted(StatusCB aCompletedCB, ErrorPtr aError);
    void gatewayInfoHandler(StatusCB aCompletedCB, JsonObjectPtr aResult, ErrorPtr aError);

    void gatewayWebsocketHandler(const string aMessage, ErrorPtr aError);

    void pairResultHandler(Tristate aOnlyEstablish, bool aWasPaired, ErrorPtr aError);
    void learnedInComplete(ErrorPtr aError);

    void queryDevices(StatusCB aCompletedCB);
    void devicesListHandler(StatusCB aCompletedCB, JsonObjectPtr aResult, ErrorPtr aError);
    void loadsListHandler(StatusCB aCompletedCB, JsonObjectPtr aDevicesArray, JsonObjectPtr aLoadsArray, ErrorPtr aError);
    void loadsStateHandler(StatusCB aCompletedCB, JsonObjectPtr aDevicesArray, JsonObjectPtr aLoadsArray, JsonObjectPtr aStatesArray, ErrorPtr aError);
    void sensorsListHandler(StatusCB aCompletedCB, JsonObjectPtr aDevicesArray, JsonObjectPtr aLoadsArray, JsonObjectPtr aStatesArray, JsonObjectPtr aSensorsArray, ErrorPtr aError);
    void buttonsListHandler(StatusCB aCompletedCB, JsonObjectPtr aDevicesArray, JsonObjectPtr aLoadsArray, JsonObjectPtr aStatesArray, JsonObjectPtr aSensorsArray, JsonObjectPtr aButtonsArray, ErrorPtr aError);

    bool addWbfDevice(WbfDevicePtr aNewDev);

    void buttonActivationStarted(JsonObjectPtr aResult, ErrorPtr aError);
    void endButtonActivation();

    void requestButtonActivation(JsonObjectPtr aButtonInfo);
    void buttonActivationChange(JsonObjectPtr aResult, ErrorPtr aError, bool aActivated);
    void activatedAndRescanned(ErrorPtr aError);


    void wbfapicallResponse(VdcApiRequestPtr aRequest, JsonObjectPtr aResult, ErrorPtr aError);

    static void unregisterBehaviourMap(PartIdToBehaviourMap &aMap, DsBehaviourPtr aBehaviour);

  };

} // namespace p44

#endif // ENABLE_WBF
#endif // !__p44vdc__wbfvdc__
