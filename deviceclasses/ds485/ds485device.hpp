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

#ifndef __p44vdc__ds485device__
#define __p44vdc__ds485device__

#include "device.hpp"
#include "ds485comm.hpp"
#include <bitset>

#if ENABLE_DS485DEVICES

using namespace std;

namespace p44 {

  class Ds485Vdc;
  class Ds485Device;

  #define ZG(mod) (0x5A00|(mod))
  #define DEV(mod) (0x4400|(mod))

  class Ds485Device final : public Device
  {
    typedef Device inherited;
    friend class Ds485Vdc;

    DsUid mDsmDsUid;
    uint16_t mDevId;
    bool mIsPresent;
    int mNumOPC;
    Ds485Vdc& mDs485Vdc;

    bool mUpdatingCache; ///< if this is set, channels must not be applied to dS, because we are only updating the cache FROM dS-side change
    MLTicket mTracingTimer;
    SceneNo mTracedScene;

    std::bitset<NUM_VALID_SCENES> mCachedScenes; ///< scenes we have already cached

  public:

    Ds485Device(Ds485Vdc *aVdcP, DsUid& aDsmDsUid, uint16_t aDevId);

    virtual ~Ds485Device();

    /// identify a device up to the point that it knows its dSUID and internal structure. Possibly swap device object for a more specialized subclass.
    virtual bool identifyDevice(IdentifyDeviceCB aIdentifyCB) P44_OVERRIDE;

    /// device type identifier
		/// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const P44_OVERRIDE;

    // dS485 devices are NEVER to be shown as virtual devices to a connecting vdsm!
    virtual bool isPublicDS() P44_OVERRIDE { return false; }

    Ds485Vdc &getDs485Vdc();

    /// check if device can be disconnected by software (i.e. Web-UI)
    /// @return true if device might be disconnectable (deletable) by the user via software (i.e. web UI)
    /// @note devices returning true here might still refuse disconnection on a case by case basis when
    ///   operational state does not allow disconnection.
    /// @note devices returning false here might still be disconnectable using disconnect() triggered
    ///   by vDC API "remove" method.
    virtual bool isSoftwareDisconnectable() P44_OVERRIDE { return false; };

    /// description of object, mainly for debug and logging
    /// @return textual description of object
    virtual string description() P44_OVERRIDE;

    /// @name identification of the addressable entity
    /// @{

    /// @return human readable model name/short description
    virtual string modelName() P44_OVERRIDE;

    /// @return hardware GUID in URN format to identify the hardware INSTANCE as uniquely as possible
    virtual string hardwareGUID() P44_OVERRIDE;

    /// @return vendor name
    virtual string vendorName() P44_OVERRIDE;

    /// @return URL for Web-UI (for access from local LAN)
    virtual string webuiURLString() P44_OVERRIDE;

    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

    /// @}

    virtual void addedAndInitialized() P44_OVERRIDE;

    /// overridden to handle method calls, depending on what it is
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams) P44_OVERRIDE;

    /// identify the device to the user
    /// @param aDuration if !=Never, this is how long the identification should be recognizable. If this is \<0, the identification should stop
    virtual void identifyToUser(MLMicroSeconds aDuration) P44_OVERRIDE;

    /// prepare for applying a scene or scene undo on the device level
    /// @param aScene the scene that is to be applied (or restored channel values from an undoScene, see below)
    /// @return true if channel values should be applied, false if not
    /// @note for a scene call, this is called AFTER scene values are already loaded and prepareSceneCall() has already been called,
    ///   but before channels are applied (or not, if method returns false). For a undoScene call, prepareSceneCall() is NOT
    ///   called (it's not a scene call), but prepareSceneApply() is called with a pseudo-scene having sceneCmd==scene_cmd_undo.
    virtual bool prepareSceneApply(DsScenePtr aScene) P44_OVERRIDE;

    /// apply all pending channel value updates to the device's hardware
    /// @note this is the only routine that should trigger actual changes in output values. It must consult all of the device's
    ///   ChannelBehaviours and check isChannelUpdatePending(), and send new values to the device hardware. After successfully
    ///   updating the device hardware, channelValueApplied() must be called on the channels that had isChannelUpdatePending().
    /// @param aDoneCB if not NULL, must be called when values are applied
    /// @param aForDimming hint for implementations to optimize dimming, indicating that change is only an increment/decrement
    ///   in a single channel (and not switching between color modes etc.)
    virtual void applyChannelValues(SimpleCB aDoneCB, bool aForDimming) P44_FINAL;

    /// overridden to handle notifications
    virtual void handleNotification(const string &aNotification, ApiValuePtr aParams, StatusCB aExaminedCB) P44_OVERRIDE;

    /// initializes the physical device for being used
    /// @param aFactoryReset if set, the device will be inititalized as thoroughly as possible (factory reset, default settings etc.)
    /// @note this is called before interaction with dS system starts
    /// @note implementation should call inherited when complete, so superclasses could chain further activity
    virtual void initializeDevice(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    /// handle message from dS485 bus related to this device
    void handleDeviceUpstreamMessage(bool aIsSensor, uint8_t aKeyNo, DsClickType aClickType);

    /// trace scene call to figure out how the device's current state now is
    void traceSceneCall(SceneNo aSceneNo);
    void traceChannelChange(DsChannelType aChannelType, uint8_t a8BitChannelValue);


  private:

    void processActionRequest(uint16_t aFlaggedModifier, const string aPayload, size_t aPli);

    void requestOutputValueUpdate();
    void startTracingFor(SceneNo aSceneNo);

    ErrorPtr issueDeviceRequest(uint8_t aCommand, uint8_t aModifier, const string& aMorePayload = "");
    ErrorPtr issueDsmRequest(uint8_t aCommand, uint8_t aModifier, const string& aPayload = "");

    void executeDsmQuery(QueryCB aQueryCB, MLMicroSeconds aTimeout, uint8_t aCommand, uint8_t aModifier, const string& aPayload = "");

  };
  typedef boost::intrusive_ptr<Ds485Device> Ds485DevicePtr;

} // namespace p44

#endif // ENABLE_DS485DEVICES
#endif // __p44vdc__ds485device__
