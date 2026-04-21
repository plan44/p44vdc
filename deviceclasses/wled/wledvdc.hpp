//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Michael Troß <digitalstrom@tross.org>
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

#ifndef __p44vdc__wledvdc__
#define __p44vdc__wledvdc__

#include "p44vdc_common.hpp"

#if ENABLE_WLED

#include "vdc.hpp"
#include "wledcomm.hpp"

using namespace std;

namespace p44 {

  class WledVdc;
  class WledDevice;

  typedef boost::intrusive_ptr<WledVdc> WledVdcPtr;
  typedef boost::intrusive_ptr<WledDevice> WledDevicePtr;


  /// persistence for WLED device container
  class WledPersistence : public SQLite3TableGroup
  {
    typedef SQLite3TableGroup inherited;
  protected:
    /// Get DB Schema creation/upgrade SQL statements
    virtual string schemaUpgradeSQL(int aFromVersion, int &aToVersion);
  };


  /// WLED virtual device container
  class WledVdc : public Vdc
  {
    typedef Vdc inherited;
    friend class WledDevice;

    WledPersistence mDb;

    /// @name persistent parameters
    /// @{

    bool mAutoDiscovery;              ///< enable/disable DNS-SD auto-discovery

    /// @}

    /// @name internal state
    /// @{

    typedef map<string, WledDevicePtr> WledDeviceMap;
    WledDeviceMap mWledDevices;       ///< known devices, keyed by MAC address

    MLTicket mRefreshTicket;          ///< for periodic state refresh

    /// @}

  public:

    WledVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag);
    virtual ~WledVdc();

    /// set the log level offset on this logging object (and possibly contained sub-objects)
    virtual void setLogLevelOffset(int aLogLevelOffset) P44_OVERRIDE;

    /// initialize the device container
    virtual void initialize(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    /// vdc class identifier
    virtual const char *vdcClassIdentifier() const P44_OVERRIDE;

    /// scan for (collect) devices and add them to the vdc
    virtual void scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags) P44_OVERRIDE;

    /// rescan modes supported
    virtual int getRescanModes() const P44_OVERRIDE { return rescanmode_incremental+rescanmode_normal+rescanmode_exhaustive; }

    /// remove a device
    void removeDevice(DevicePtr aDevice, bool aForget) P44_OVERRIDE;

    /// handle vdc level method calls
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams) P44_OVERRIDE;

    /// Get icon data or name
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

    /// Get extra info to be displayed for vdc
    virtual string getExtraInfo() P44_OVERRIDE;

    /// Get hardware GUID
    virtual string hardwareGUID() P44_OVERRIDE;

    /// @return human readable, language independent suffix to explain vdc functionality.
    virtual string vdcModelSuffix() const P44_OVERRIDE { return "wled"; }

    /// @return human readable model version specific to that vDC
    virtual string vdcModelVersion() const P44_OVERRIDE { return "0.0.0"; };

  private:

    /// Connect to a WLED device by hostname/IP, query its info, create or update the
    /// WledDevice object and persist the MAC+hostname mapping.
    /// @param aHostname  IP address or hostname of the WLED device
    /// @param aCompletedCB  called when done (may be NULL)
    void connectToDevice(const string &aHostname, StatusCB aCompletedCB);

    /// perform auto-discovery of WLED devices via DNS-SD
    void performDiscovery(StatusCB aCompletedCB);

    /// DNS-SD browse result callback — returns true to continue browsing
    bool handleDiscoveryHandler(ErrorPtr aError, DnsSdServiceInfoPtr aServiceInfo, StatusCB aCompletedCB);
  };

} // namespace p44

#endif // ENABLE_WLED

#endif // __p44vdc__wledvdc__
