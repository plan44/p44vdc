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
    virtual string schemaUpgradeSQL(int aFromVersion, int &aToVersion);
  };


  /// WLED virtual device container
  class WledVdc : public Vdc
  {
    typedef Vdc inherited;
    friend class WledDevice;

    WledPersistence mDb;

    /// persistent parameters
    bool mAutoDiscovery;              ///< enable/disable DNS-SD auto-discovery

    /// internal state
    typedef map<string, WledDevicePtr> WledDeviceMap;
    WledDeviceMap mWledDevices;       ///< known dS virtual devices, keyed by dSUID-base string

    MLTicket mRefreshTicket;

    /// preset optimizer state
    /// Map from physical device MAC to the set of WLED preset IDs (1-250) in use by the optimizer.
    map<string, set<int>> mUsedPresetIds;
    MLTicket mDelayedPresetUpdateTicket;  ///< for delayed post-transition preset save

  public:

    WledVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag);
    virtual ~WledVdc();

    virtual void setLogLevelOffset(int aLogLevelOffset) P44_OVERRIDE;

    virtual void initialize(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;
    virtual const char *vdcClassIdentifier() const P44_OVERRIDE;
    virtual void scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags) P44_OVERRIDE;
    virtual int getRescanModes() const P44_OVERRIDE { return rescanmode_incremental+rescanmode_normal+rescanmode_exhaustive; }
    virtual void removeDevice(DevicePtr aDevice, bool aForget) P44_OVERRIDE;
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams) P44_OVERRIDE;
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;
    virtual string getExtraInfo() P44_OVERRIDE;
    virtual string hardwareGUID() P44_OVERRIDE;
    virtual string vdcModelSuffix() const P44_OVERRIDE { return "wled"; }
    virtual string vdcModelVersion() const P44_OVERRIDE { return "0.0.0"; };
    virtual void handleGlobalEvent(VdchostEvent aEvent) P44_OVERRIDE;

    // MARK: - Scene optimizer (native WLED presets)

    /// Allow optimizer for any ntfy_callscene regardless of device count
    virtual bool shouldUseOptimizerFor(NotificationDeliveryStatePtr aDeliveryState) P44_OVERRIDE;

    /// On startup: re-register preset IDs that were already in use
    virtual ErrorPtr announceNativeAction(const string aNativeActionId) P44_OVERRIDE;

    /// Recall a WLED preset
    virtual void callNativeAction(StatusCB aStatusCB, const string aNativeActionId, NotificationDeliveryStatePtr aDeliveryState) P44_OVERRIDE;

    /// Create a new WLED preset by saving the current device state
    virtual void createNativeAction(StatusCB aStatusCB, OptimizerEntryPtr aOptimizerEntry, NotificationDeliveryStatePtr aDeliveryState) P44_OVERRIDE;

    /// Update a WLED preset after a scene change (delayed to let transition finish)
    virtual void updateNativeAction(StatusCB aStatusCB, OptimizerEntryPtr aOptimizerEntry, NotificationDeliveryStatePtr aDeliveryState) P44_OVERRIDE;

    /// Cancel any pending delayed preset update
    virtual void cancelNativeActionUpdate() P44_OVERRIDE;

    /// Delete a WLED preset
    virtual void freeNativeAction(StatusCB aStatusCB, const string aNativeActionId) P44_OVERRIDE;

  private:

    // MARK: - Device management

    /// Connect to a WLED device, query its segments, and create one WledDevice per segment.
    void connectToDevice(const string &aHostname, StatusCB aCompletedCB);

    /// Internal: called once we have both /json/info and /json/state for a device.
    void createDevicesForPhysicalDevice(WledCommPtr aComm, const string &aHostname,
                                        JsonObjectPtr aInfo, JsonObjectPtr aState,
                                        StatusCB aCompletedCB);

    void performDiscovery(StatusCB aCompletedCB);
    bool handleDiscoveryHandler(ErrorPtr aError, DnsSdServiceInfoPtr aServiceInfo, StatusCB aCompletedCB);

    // MARK: - Preset helpers

    /// Format: "wled:<mac>:p:<N>"
    static string wledNativeActionId(const string &aMac, int aPresetId);

    /// Parse the above; returns false if format does not match
    static bool parseWledNativeActionId(const string &aId, string &aMac, int &aPresetId);

    /// Find first unused preset ID for the given device MAC; returns -1 when all 250 are used
    int allocatePresetId(const string &aMac);

    /// Release a preset ID back to the free pool
    void freePresetId(const string &aMac, int aPresetId);

    /// Find a WledDevice by its physical device MAC address (returns any segment)
    WledDevicePtr findDeviceByMac(const string &aMac);

    /// Save current device state as WLED preset aPresetId and call aStatusCB when done
    void savePreset(WledDevicePtr aDev, int aPresetId, int aSceneId, StatusCB aStatusCB);

    /// Perform the delayed preset update (called after transition finishes)
    void performPresetUpdate(uint64_t aNewHash, string aNativeActionId,
                             WledDevicePtr aDev, int aPresetId,
                             OptimizerEntryPtr aOptimizerEntry);
  };

} // namespace p44

#endif // ENABLE_WLED

#endif // __p44vdc__wledvdc__
