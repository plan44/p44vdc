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

#ifndef __p44vdc__huedevice__
#define __p44vdc__huedevice__

#include "device.hpp"

#if ENABLE_HUE

#include "colorlightbehaviour.hpp"
#include "jsonobject.hpp"

using namespace std;

namespace p44 {

  class HueVdc;
  class HueDevice;
  class HueComm;



  typedef boost::intrusive_ptr<HueDevice> HueDevicePtr;
  class HueDevice : public Device
  {
    typedef Device inherited;
    friend class HueVdc;

    string lightID; ///< the ID as used in the hue bridge
    string uniqueID; ///< the unique light ID (which is available in v1.4 and later APIs)

    // information from the device itself
    string hueModel;

    // model software version
    string swVersion;

    // reapply mechanism for difficult situations
    typedef enum {
      reapply_none, ///< do not re-apply
      reapply_once, ///< re-apply once shortly after initial apply
      reapply_periodic ///< alse re-apply periodically later (for broken bulbs that go white after a while)
    } ReapplyMode;
    ReapplyMode reapplyMode;
    int reapplyCount;
    MLTicket reapplyTicket;

    MLTicket dimTicket;

  public:
    HueDevice(HueVdc *aVdcP, const string &aLightID, bool aIsColor, bool aCTOnly, const string &aUniqueID);

    /// identify a device up to the point that it knows its dSUID and internal structure. Possibly swap device object for a more specialized subclass.
    virtual bool identifyDevice(IdentifyDeviceCB aIdentifyCB) P44_OVERRIDE;

    /// device type identifier
		/// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const P44_OVERRIDE { return "hue"; };

    HueVdc &hueVdc();
    HueComm &hueComm();

    /// description of object, mainly for debug and logging
    /// @return textual description of object
    virtual string description() P44_OVERRIDE;

    /// set user assignable name
    /// @param aName new name of the hue device
    /// @note will propagate the name to the hue bridge to name the light itself
    virtual void setName(const string &aName) P44_OVERRIDE;


    /// @name interaction with subclasses, actually representing physical I/O
    /// @{

    /// initializes the physical device for being used
    /// @param aFactoryReset if set, the device will be inititalized as thoroughly as possible (factory reset, default settings etc.)
    /// @note this is called before interaction with dS system starts (usually just after collecting devices)
    /// @note implementation should call inherited when complete, so superclasses could chain further activity
    virtual void initializeDevice(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    /// check presence of this addressable
    /// @param aPresenceResultHandler will be called to report presence status
    virtual void checkPresence(PresenceCB aPresenceResultHandler) P44_OVERRIDE;

    /// disconnect device. For hue, we'll check if the device is still reachable via the bridge, and only if not
    /// we allow disconnection
    /// @param aForgetParams if set, not only the connection to the device is removed, but also all parameters related to it
    ///   such that in case the same device is re-connected later, it will not use previous configuration settings, but defaults.
    /// @param aDisconnectResultHandler will be called to report true if device could be disconnected,
    ///   false in case it is certain that the device is still connected to this and only this vDC
    virtual void disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler) P44_OVERRIDE;

    /// start or stop dimming (optimized hue version)
    /// @param aChannel the channel to start or stop dimming for
    /// @param aDimMode according to VdcDimMode: 1=start dimming up, -1=start dimming down, 0=stop dimming
    /// @param aDoApply only if set to true, dimming must be started/stopped in hardware. Otherwise
    ///   the actual operation has been done already by another means (such as native group/scene call on the harware level)
    ///   and must NOT be repeated.
    ///   However, in all cases internal state must be updated to reflect the finalized operation
    /// @note this method can rely on a clean start-stop sequence in all cases, which means it will be called once to
    ///   start a dimming process, and once again to stop it. There are no repeated start commands or missing stops - Device
    ///   class makes sure these cases (which may occur at the vDC API level) are not passed on to dimChannel()
    virtual void dimChannel(ChannelBehaviourPtr aChannel, VdcDimMode aDimMode, bool aDoApply) P44_OVERRIDE;

    /// apply all pending channel value updates to the device's hardware
    /// @param aDoneCB will called when values are applied
    /// @param aForDimming hint for implementations to optimize dimming, indicating that change is only an increment/decrement
    ///   in a single channel (and not switching between color modes etc.)
    /// @note this is the only routine that should trigger actual changes in output values. It must consult all of the device's
    ///   ChannelBehaviours and check isChannelUpdatePending(), and send new values to the device hardware. After successfully
    ///   updating the device hardware, channelValueApplied() must be called on the channels that had isChannelUpdatePending().
    virtual void applyChannelValues(SimpleCB aDoneCB, bool aForDimming) P44_OVERRIDE;

    /// synchronize channel values by reading them back from the device's hardware (if possible)
    /// @param aDoneCB will be called when values are updated with actual hardware values
    /// @note this method is only called at startup and before saving scenes to make sure changes done to the outputs directly (e.g. using
    ///   a direct remote control for a lamp) are included. Just reading a channel state does not call this method.
    /// @note implementation must use channel's syncChannelValue() method
    virtual void syncChannelValues(SimpleCB aDoneCB) P44_OVERRIDE;

    /// @}


    /// @name identification of the addressable entity
    /// @{

    /// @return human readable model name/short description
    virtual string modelName() P44_OVERRIDE;

    /// @return hardware GUID in URN format to identify hardware as uniquely as possible
    virtual string hardwareGUID() P44_OVERRIDE;

    /// @return human readable version string
    virtual string modelVersion() const P44_OVERRIDE;

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

    /// @}


  protected:

    /// let device implementation prepare for (and possibly reject) optimized set
    /// @param aDeliveryState can be inspected to see the scene or dim parameters
    ///   (optimizedType, actionParam, actionVariant are already set)
    /// @return true if device is ok with being part of optimized set. If false is returned, the call will be
    ///    executed without optimisation
    virtual bool prepareForOptimizedSet(NotificationDeliveryStatePtr aDeliveryState) P44_OVERRIDE;

  private:

    void deriveDsUid();
    void deviceStateReceived(StatusCB aCompletedCB, bool aFactoryReset, JsonObjectPtr aDeviceInfo, ErrorPtr aError);
    void presenceStateReceived(PresenceCB aPresenceResultHandler, JsonObjectPtr aDeviceInfo, ErrorPtr aError);
    void disconnectableHandler(bool aForgetParams, DisconnectCB aDisconnectResultHandler, bool aPresent);
    void channelValuesSent(LightBehaviourPtr aColorLightBehaviour, SimpleCB aDoneCB, JsonObjectPtr aResult, ErrorPtr aError);
    void channelValuesReceived(SimpleCB aDoneCB, JsonObjectPtr aDeviceInfo, ErrorPtr aError);
    bool applyLightState(SimpleCB aDoneCB, bool aForDimming, bool aAnyway);
    void reapplyTimerHandler();
    void parseLightState(JsonObjectPtr aDeviceInfo);

  };
  
} // namespace p44


#endif // ENABLE_HUE
#endif // __p44vdc__huedevice__
