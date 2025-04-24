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

#ifndef __p44vdc__ds485vdc__
#define __p44vdc__ds485vdc__

#include "p44vdc_common.hpp"

#if ENABLE_DS485DEVICES

#include "vdc.hpp"
#include "ds485device.hpp"

#include "ds485comm.hpp"

using namespace std;

namespace p44 {

  class Ds485Vdc;
  class Ds485Device;


  /// persistence for dS485
  class Ds485Persistence : public SQLite3Persistence
  {
    typedef SQLite3Persistence inherited;
  protected:
    /// Get DB Schema creation/upgrade SQL statements
    virtual string dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion);
  };


  typedef boost::intrusive_ptr<Ds485Vdc> Ds485VdcPtr;
  class Ds485Vdc final : public Vdc
  {
    typedef Vdc inherited;
    friend class Ds485Device;

    typedef std::map<string, Ds485DevicePtr> Ds485DeviceMap;
    Ds485DeviceMap mDs485Devices;
    bool mDs485Started;
    bool mDs485HostKnown;
    MLTicket mRecollectTicket;

    Ds485Persistence mDb;

  public:

    Ds485Comm mDs485Comm;

    Ds485Vdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag);

    virtual ~Ds485Vdc();

    /// get logging object for a named topic
    virtual P44LoggingObj* getTopicLogObject(const string aTopic) P44_OVERRIDE;

    /// initialize vdc.
    /// @note this implementation will query the proxyied device and then change it's dSUID
    ///   which is allowed to happen before aCompletedCB is called. vdhost will re-map the
    ///   already registered vdc to its new dSUID.
    virtual void initialize(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    // dS485 vdc is NEVER to be shown as virtual device to a connecting vdsm!
    virtual bool isPublicDS() P44_OVERRIDE { return false; }

    virtual const char *vdcClassIdentifier() const P44_OVERRIDE;

    /// @return URL for Web-UI (for access from local LAN)
    virtual string webuiURLString() P44_OVERRIDE;

    /// get supported rescan modes for this vDC. This indicates (usually to a web-UI) which
    /// of the flags to collectDevices() make sense for this vDC.
    /// @return a combination of rescanmode_xxx bits
    virtual int getRescanModes() const P44_OVERRIDE;

    /// scan for (collect) devices and add them to the vdc
    virtual void scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags) P44_OVERRIDE;

    /// @return human readable, language independent suffix to explain vdc functionality.
    ///   Will be appended to product name to create modelName() for vdcs
    virtual string vdcModelSuffix() const P44_OVERRIDE { return "ds485"; }

    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

    /// deliver (forward) notifications to devices in one call instead of forwarding on device level
    virtual void deliverToDevicesAudience(DsAddressablesList aAudience, VdcApiConnectionPtr aApiConnection, const string &aNotification, ApiValuePtr aParams) P44_OVERRIDE;

    /// vdc level methods
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams) P44_OVERRIDE;

  protected:

    /// handle global events
    /// @param aEvent the event to handle
    virtual void handleGlobalEvent(VdchostEvent aEvent) P44_OVERRIDE;

    /// @return device matching dSM's dSUID and local deviceId
    Ds485DevicePtr deviceFor(const DsUid& aDsmDsUid, uint16_t aDevId);

  private:

    /// @name synchronously executing, blocking calls, only to use from mDs485ClientThread
    /// @{

    ErrorPtr scanDs485BusSync(ChildThreadWrapper &aThread);

    /// @}

    void ds485BusScanned(ErrorPtr aScanStatus, StatusCB aCompletedCB);
    void ds485MessageHandler(const DsUid& aSource, const DsUid& aTarget, const string aPayload);

    void recollect(RescanMode aRescanMode);

  };

} // namespace p44


#endif // ENABLE_DS485DEVICES
#endif // __p44vdc__ds485vdc__
