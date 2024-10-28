//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2022 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__bridgedevice__
#define __p44vdc__bridgedevice__

#include "device.hpp"
#include "p44script.hpp"
#include "httpcomm.hpp"

#include <math.h>

#if ENABLE_JSONBRIDGEAPI

using namespace std;

namespace p44 {

  class BridgeVdc;
  class BridgeDevice;

  class BridgeDeviceSettings : public DeviceSettings
  {
    typedef DeviceSettings inherited;
    friend class BridgeDevice;

  protected:

    BridgeDeviceSettings(BridgeDevice &aBridgeDevice);

    // persistence implementation
    virtual const char *tableName();
    virtual size_t numFieldDefs();
    virtual const FieldDefinition *getFieldDef(size_t aIndex);
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP);
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags);

  };
  typedef boost::intrusive_ptr<BridgeDeviceSettings> BridgeDeviceSettingsPtr;


  class BridgeDevice : public Device
  {
    typedef Device inherited;
    friend class BridgeVdc;
    friend class BridgeDeviceSettings;

    long long mBridgeDeviceRowID; ///< the ROWID this device was created from (0=none)
    string mBridgeDeviceId; ///< the base for generating the dSUID

    typedef enum {
      bridgedevice_unknown,
      bridgedevice_onoff, ///< mirrors preset1/off scenes to bridged pseudo-onoff device
      bridgedevice_fivelevel, ///< mirrors standard scenes for bridged pseudo-levelcontrol device (according to scene values)
      bridgedevice_sceneresponder, ///<  forwards specific scene call as a button click to bridge
      bridgedevice_scenecaller, ///< emits a specific scene when bridged on-off device is turned on
      bridgedevice_dimmerdial, ///< acts as room or area dimmer
    } BridgeDeviceType;

    BridgeDeviceType mBridgeDeviceType;
    SceneNo mActivateScene; ///< scene No for scene-specific bridges (detected or called)
    SceneNo mResetScene; ///< scene No for scene-specific bridges (detected or called)

    bool mProcessingBridgeNotification; ///< set when processing state update sent by bridge
    double mPreviousV; ///< value to compare to for deciding about issuing scene calls
    MLTicket mResetSignalTicket; ///< delayed reset signal timer

  public:

    BridgeDevice(BridgeVdc *aVdcP, const string &aBridgeDeviceId, const string &aBridgeDeviceConfig, DsGroup aGroup, bool aAllowBridging);

    virtual ~BridgeDevice();

    /// identify a device up to the point that it knows its dSUID and internal structure. Possibly swap device object for a more specialized subclass.
    virtual bool identifyDevice(IdentifyDeviceCB aIdentifyCB) P44_OVERRIDE;

    /// device type identifier
		/// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const P44_OVERRIDE { return "bridge"; };

    BridgeVdc &getBridgeVdc();

    /// check if device can be disconnected by software (i.e. Web-UI)
    /// @return true if device might be disconnectable (deletable) by the user via software (i.e. web UI)
    /// @note devices returning true here might still refuse disconnection on a case by case basis when
    ///   operational state does not allow disconnection.
    /// @note devices returning false here might still be disconnectable using disconnect() triggered
    ///   by vDC API "remove" method.
    virtual bool isSoftwareDisconnectable() P44_OVERRIDE { return true; };

    /// disconnect device. For static device, this means removing the config from the container's DB. Note that command line
    /// static devices cannot be disconnected.
    /// @param aForgetParams if set, not only the connection to the device is removed, but also all parameters related to it
    ///   such that in case the same device is re-connected later, it will not use previous configuration settings, but defaults.
    /// @param aDisconnectResultHandler will be called to report true if device could be disconnected,
    ///   false in case it is certain that the device is still connected to this and only this vDC
    virtual void disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler) P44_OVERRIDE;

    /// description of object, mainly for debug and logging
    /// @return textual description of object
    virtual string description() P44_OVERRIDE;

    /// This string may help the bridge to determine how to bridge this device.
    /// @return non-empty string if there is a bridging hint keyword that will be exposed as x-p44-bridgeAs.
    virtual string bridgeAsHint() P44_OVERRIDE;

    /// @name identification of the addressable entity
    /// @{

    /// @return human readable model name/short description
    virtual string modelName() P44_OVERRIDE;

    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

    /// @}

    /// initializes the physical device for being used
    /// @param aFactoryReset if set, the device will be inititalized as thoroughly as possible (factory reset, default settings etc.)
    /// @note this is called before interaction with dS system starts
    /// @note implementation should call inherited when complete, so superclasses could chain further activity
    virtual void initializeDevice(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

  protected:

    /// called before start examining (usually: handling) a notification
    /// @param aApiConnection null for internally generated notifications, or API connection notification originates from
    virtual void willExamineNotificationFromConnection(VdcApiConnectionPtr aApiConnection) P44_OVERRIDE;

    /// called after notification is examined (and either done, or needed operations queued)
    /// @param aApiConnection null for internally generated notifications, or API connection notification originates from
    virtual void didExamineNotificationFromConnection(VdcApiConnectionPtr aApiConnection) P44_OVERRIDE;

    /// prepare for calling a scene on the device level
    virtual bool prepareSceneCall(DsScenePtr aScene) P44_OVERRIDE;

    /// apply all pending channel value updates to the device's hardware
    /// @param aDoneCB will called when values are actually applied, or hardware reports an error/timeout
    /// @param aForDimming hint for implementations to optimize dimming, indicating that change is only an increment/decrement
    ///   in a single channel (and not switching between color modes etc.)
    /// @note this is the only routine that should trigger actual changes in output values. It must consult all of the device's
    ///   ChannelBehaviours and check isChannelUpdatePending(), and send new values to the device hardware. After successfully
    ///   updating the device hardware, channelValueApplied() must be called on the channels that had isChannelUpdatePending().
    ///   In addition, if the device output hardware has distinct disabled/enabled states, output->isEnabled() must be checked and applied.
    /// @note the implementation must capture the channel values to be applied before returning from this method call,
    ///   because channel values might change again before a delayed apply mechanism calls aDoneCB.
    /// @note this method will NOT be called again until aDoneCB is called, even if that takes a long time.
    ///   Device::requestApplyingChannels() provides an implementation that serializes calls to applyChannelValues and syncChannelValues
    virtual void applyChannelValues(SimpleCB aDoneCB, bool aForDimming) P44_OVERRIDE;

    void deriveDsUid();

  private:

    void resetSignalChannel();
    void updateDimmer(double aNewValue);

  };
  typedef boost::intrusive_ptr<BridgeDevice> BridgeDevicePtr;

} // namespace p44

#endif // ENABLE_JSONBRIDGEAPI
#endif // __p44vdc__bridgedevice__
