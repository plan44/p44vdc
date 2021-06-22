//
//  Copyright (c) 2015-2021 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__customdevice__
#define __p44vdc__customdevice__

#include "p44vdc_common.hpp"

#if ENABLE_EXTERNAL || ENABLE_SCRIPTED

#ifndef ENABLE_CUSTOM_SINGLEDEVICE
  #define ENABLE_CUSTOM_SINGLEDEVICE 1
#endif
#ifndef ENABLE_FCU_SUPPORT
  #define ENABLE_FCU_SUPPORT 1
#endif
#ifndef ENABLE_CUSTOM_EXOTIC
  #define ENABLE_CUSTOM_EXOTIC 1
#endif


#include "vdc.hpp"
#include "device.hpp"
#include "jsonobject.hpp"

#include "buttonbehaviour.hpp"
#if ENABLE_CUSTOM_SINGLEDEVICE
#include "singledevice.hpp"
#endif

using namespace std;

namespace p44 {

  class ExternalVdc;
  class CustomDevice;

  #if ENABLE_CUSTOM_SINGLEDEVICE

  class CustomDeviceAction : public DeviceAction
  {
    typedef DeviceAction inherited;

    StatusCB mCallback;

  public:

    /// create the action
    /// @param aSingleDevice the single device this action belongs to
    /// @param aName the name of the action.
    /// @param aDescription a description string for the action.
    CustomDeviceAction(SingleDevice &aSingleDevice, const string aName, const string aDescription, const string aTitle, const string aCategory);

    virtual ~CustomDeviceAction();

    CustomDevice &getCustomDevice();

    /// implementation of action
    virtual void performCall(ApiValuePtr aParams, StatusCB aCompletedCB) P44_OVERRIDE;

    /// process action call confirmation message from external device
    void callPerformed(JsonObjectPtr aStatusInfo);

  };
  typedef boost::intrusive_ptr<CustomDeviceAction> CustomDeviceActionPtr;

  #endif // ENABLE_CUSTOM_SINGLEDEVICE

  typedef boost::intrusive_ptr<CustomDevice> CustomDevicePtr;
  class CustomDevice :
    #if ENABLE_CUSTOM_SINGLEDEVICE
    public SingleDevice
    #else
    public Device
    #endif
  {
    #if ENABLE_CUSTOM_SINGLEDEVICE
    typedef SingleDevice inherited;
    friend class CustomDeviceAction;
    #else
    typedef Device inherited;
    #endif
    friend class ExternalVdc;
    friend class CustomDeviceConnector;

    string mIconBaseName; ///< the base icon name
    string mModelNameString; ///< the string to be returned by modelName()
    string mModelVersionString; ///< the string to be returned by modelVersion()
    string mVendorNameString; ///< the vendor name
    string mOemModelGUIDString; ///< the OEM model GUID, which is used to match devices with dS database
    string mDevClass; ///< device class
    string mConfigUrl; ///< custom value for configURL if not empty
    uint32_t mDevClassVersion; ///< device class version


    bool mSimpletext; ///< set when communication with this device is simple text
    bool mConfigured; ///< set when device is configured (init message received and device added to vdc)
    bool mUseMovement; ///< if set, device communication uses MV/move command for dimming and shadow device operation
    bool mControlValues; ///< if set, device communication uses CTRL/control command to forward system control values such as "heatingLevel" and "TemperatureZone"
    bool mQuerySync; ///< if set, device is asked for synchronizing actual values of channels when needed (e.g. before saveScene)
    bool mSceneCommands; ///< if set, scene commands are forwarded to the external device
    bool mForwardIdentify; ///< if set, "IDENTIFY" messages will be sent, and device will show the "identification" modelfeature in the vDC API

    #if ENABLE_CUSTOM_EXOTIC
    string mConfigurationId; ///< current configuration's id
    DeviceConfigurationsVector mConfigurations; ///< the device's possible configurations
    #endif

    #if ENABLE_CUSTOM_SINGLEDEVICE
    bool mNoConfirmAction; ///< if set, device implementation is not expected to use
    #endif

    SimpleCB mSyncedCB; ///< will be called when device confirms "SYNC" message with "SYNCED" response

    MLTicket mButtonReleaseTicket; ///< for automatically releasing buttons

  protected:

    string mTypeIdentifier; ///< the type identifier

  public:

    CustomDevice(Vdc *aVdcP, bool aSimpleText);
    virtual ~CustomDevice();

    /// identify a device up to the point that it knows its dSUID and internal structure. Possibly swap device object for a more specialized subclass.
    virtual bool identifyDevice(IdentifyDeviceCB aIdentifyCB) P44_OVERRIDE;

    ExternalVdc &getExternalVdc();

    /// device type identifier
    /// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const  P44_OVERRIDE { return mTypeIdentifier; };

    /// @return human readable model name/short description
    virtual string modelName() P44_OVERRIDE;

    /// @return human readable version string of the device model
    virtual string modelVersion() const P44_OVERRIDE;

    /// @return Vendor name if known
    virtual string vendorName() P44_OVERRIDE;

    /// @return OEM model GUID in URN format to identify the OEM product MODEL hardware as uniquely as possible
    virtual string oemModelGUID() P44_OVERRIDE;

    /// device class (for grouping functionally equivalent single devices)
    /// @note usually, only single devices do have a deviceClass
    /// @return name of the device class, such as "washingmachine" or "kettle" or "oven". Empty string if no device class exists.
    virtual string deviceClass() P44_OVERRIDE { return mDevClass; }

    /// device class version number.
    /// @note This allows different versions of the functional representation of the device class
    ///   to coexist in a system.
    /// @return version or 0 if no version exists
    virtual uint32_t deviceClassVersion() P44_OVERRIDE { return mDevClassVersion; }

    /// @return URL for Web-UI (for access from local LAN)
    virtual string webuiURLString() P44_OVERRIDE;

    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

    /// identify the external device to the user in some way
    /// @note for lights, this would be blinking, for sound devices a beep, for moving devices (blinds) a short movement
    virtual void identifyToUser() P44_OVERRIDE;

    /// check if identifyToUser() has an actual implementation
    virtual bool canIdentifyToUser() P44_OVERRIDE;

  protected:

    /// prepare for calling a scene on the device level
    /// @param aScene the scene that is to be called
    /// @return true if scene preparation is ok and call can continue. If false, no further action will be taken
    /// @note this is called BEFORE scene values are recalled
    virtual bool prepareSceneCall(DsScenePtr aScene) P44_OVERRIDE;

    /// prepare for applying a scene or scene undo on the device level
    virtual bool prepareSceneApply(DsScenePtr aScene) P44_OVERRIDE;

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
    /// @note this method will NOT be called again until aCompletedCB is called, even if that takes a long time.
    ///   Device::requestApplyingChannels() provides an implementation that serializes calls to applyChannelValues and syncChannelValues
    virtual void applyChannelValues(SimpleCB aDoneCB, bool aForDimming) P44_OVERRIDE;

    /// synchronize channel values by reading them back from the device's hardware (if possible)
    /// @param aDoneCB will be called when values are updated with actual hardware values
    /// @note this method is only called at startup and before saving scenes to make sure changes done to the outputs directly (e.g. using
    ///   a direct remote control for a lamp) are included. Just reading a channel state does not call this method.
    /// @note implementation must use channel's syncChannelValue() method
    virtual void syncChannelValues(SimpleCB aDoneCB) P44_OVERRIDE;

    /// start or stop dimming channel of this device. Usually implemented in device specific manner in subclasses.
    /// @param aChannel the channel to start or stop dimming for
    /// @param aDimMode according to VdcDimMode: 1=start dimming up, -1=start dimming down, 0=stop dimming
    /// @note unlike the vDC API "dimChannel" command, which must be repeated for dimming operations >5sec, this
    ///   method MUST NOT terminate dimming automatically except when reaching the minimum or maximum level
    ///   available for the device. Dimming timeouts are implemented at the device level and cause calling
    ///   dimChannel() with aDimMode=0 when timeout happens.
    /// @note this method can rely on a clean start-stop sequence in all cases, which means it will be called once to
    ///   start a dimming process, and once again to stop it. There are no repeated start commands or missing stops - Device
    ///   class makes sure these cases (which may occur at the vDC API level) are not passed on to dimChannel()
    virtual void dimChannel(ChannelBehaviourPtr aChannel, VdcDimMode aDimMode, bool aDoApply) P44_OVERRIDE;

    /// Process a named control value. The type, group membership and settings of the device determine if at all,
    /// and if, how the value affects physical outputs of the device or general device operation
    /// @note if this method adjusts channel values, it must not directly update the hardware, but just
    ///   prepare channel values such that these can be applied using requestApplyingChannels().
    /// @param aName the name of the control value, which describes the purpose
    /// @param aValue the control value to process
    /// @note base class by default forwards the control value to all of its output behaviours.
    /// @return true if value processing caused channel changes so channel values should be applied.
    virtual bool processControlValue(const string &aName, double aValue) P44_OVERRIDE;

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

    #if ENABLE_CUSTOM_EXOTIC
    /// device configurations implementation
    virtual string getDeviceConfigurationId() P44_OVERRIDE;
    virtual ErrorPtr switchConfiguration(const string aConfigurationId) P44_OVERRIDE;
    virtual void getDeviceConfigurations(DeviceConfigurationsVector &aConfigurations, StatusCB aStatusCB) P44_OVERRIDE;
    #endif

    #if ENABLE_CUSTOM_SINGLEDEVICE
    /// @name factory methods for elements configured via dynamic JSON config
    /// @{

    virtual ErrorPtr actionFromJSON(DeviceActionPtr &aAction, JsonObjectPtr aJSONConfig, const string aActionId, const string aDescription, const string aCategory) P44_OVERRIDE;
    virtual ErrorPtr dynamicActionFromJSON(DeviceActionPtr &aAction, JsonObjectPtr aJSONConfig, const string aActionId, const string aDescription, const string aTitle, const string aCategory) P44_OVERRIDE;

    /// @}
    #endif

    /// for vdc, to configure the device
    ErrorPtr configureDevice(JsonObjectPtr aInitParams);

    /// @return true if successfully configured
    bool isConfigured() { return mConfigured; }

    /// for vdc, to have JSON message processed
    ErrorPtr processJsonMessage(string aMessageType, JsonObjectPtr aMessage);

    /// for vdc, to have simple message processed
    ErrorPtr processSimpleMessage(string aMessageType, string aValue);

  protected:

    virtual void sendDeviceApiJsonMessage(JsonObjectPtr aMessage) = 0;
    virtual void sendDeviceApiSimpleMessage(string aMessage) = 0;
    virtual void sendDeviceApiFlagMessage(string aFlagWord) = 0;

  private:

    ErrorPtr processInputJson(char aInputType, JsonObjectPtr aParams);
    ErrorPtr processInput(char aInputType, uint32_t aIndex, double aValue);

    #if ENABLE_CUSTOM_SINGLEDEVICE
    ErrorPtr parseParam(const string aParamName, JsonObjectPtr aParamDetails, ValueDescriptorPtr &aParam);
    void propertyChanged(ValueDescriptorPtr aChangedProperty);
    #endif

    void changeChannelMovement(int aChannelIndex, SimpleCB aDoneCB, int aNewDirection);
    void releaseButton(ButtonBehaviourPtr aButtonBehaviour);

  };

} // namespace p44


#endif // ENABLE_EXTERNAL || ENABLE_SCRIPTED
#endif // __p44vdc__customdevice__
