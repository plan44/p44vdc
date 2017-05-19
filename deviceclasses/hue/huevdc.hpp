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

#ifndef __p44vdc__huevdc__
#define __p44vdc__huevdc__

#include "p44vdc_common.hpp"

#if ENABLE_HUE

#include "ssdpsearch.hpp"
#include "jsonwebclient.hpp"

#include "huecomm.hpp"
#include "vdc.hpp"

using namespace std;

namespace p44 {


  class HueVdc;
  class HueDevice;

  /// persistence for hue device container
  class HuePersistence : public SQLite3Persistence
  {
    typedef SQLite3Persistence inherited;
  protected:
    /// Get DB Schema creation/upgrade SQL statements
    virtual string dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion);
  };


  typedef boost::intrusive_ptr<HueVdc> HueVdcPtr;
  class HueVdc : public Vdc
  {
    typedef Vdc inherited;
    friend class HueDevice;

    HuePersistence db;

    /// @name persistent parameters
    /// @{

    string bridgeUuid; ///< the UUID for searching the hue bridge via SSDP
    string bridgeUserName; ///< the user name registered with the bridge
    string bridgeApiURL; ///< URL for the hue bridge (cache or user provided)
    bool fixedURL; ///< if set, bridgeApiURL is user provided and must not be updated

    /// @}

    /// @name other info retrieved from bridge
    /// @{

    uint64_t bridgeMacAddress; ///< the mac address of this hue bridge
    string swVersion;

    /// @}


  public:

    HueVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag);

    HueComm hueComm;

		void initialize(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

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
    virtual string vdcModelSuffix() const P44_OVERRIDE { return "hue"; }

    /// @return hardware GUID in URN format to identify hardware as uniquely as possible
    /// - uuid:UUUUUUU = UUID
    /// - http:xxxx = API
    virtual string hardwareGUID() P44_OVERRIDE;

    /// @return Vendor name for display purposes
    /// @note if not empty, value will be used by vendorId() default implementation to create vendorname:xxx URN schema id
    virtual string vendorName() P44_OVERRIDE;

    /// @return human readable version string
    virtual string modelVersion() P44_OVERRIDE;

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

  protected:

    /// handle global events
    /// @param aEvent the event to handle
    virtual void handleGlobalEvent(VdchostEvent aEvent) P44_OVERRIDE;

  private:

    void refindBridge(StatusCB aCompletedCB);
    void refindResultHandler(StatusCB aCompletedCB, ErrorPtr aError);
    void searchResultHandler(Tristate aOnlyEstablish, ErrorPtr aError);
    void learnedInComplete(ErrorPtr aError);
    void queryBridgeAndLights(StatusCB aCollectedHandler);
    void gotBridgeConfig(StatusCB aCollectedHandler, JsonObjectPtr aResult, ErrorPtr aError);
    void collectedLightsHandler(StatusCB aCollectedHandler, JsonObjectPtr aResult, ErrorPtr aError);

  };

} // namespace p44

#endif // ENABLE_HUE
#endif // __p44vdc__huevdc__
