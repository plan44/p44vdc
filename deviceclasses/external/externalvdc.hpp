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

#ifndef __p44vdc__externalvdc__
#define __p44vdc__externalvdc__

#include "p44vdc_common.hpp"

#if ENABLE_EXTERNAL

#ifndef ENABLE_EXTERNAL_SINGLEDEVICE
  #define ENABLE_EXTERNAL_SINGLEDEVICE 1
#endif

#include "vdc.hpp"
#include "device.hpp"
#include "jsoncomm.hpp"

#include "buttonbehaviour.hpp"
#if ENABLE_EXTERNAL_SINGLEDEVICE
#include "singledevice.hpp"
#endif

using namespace std;

namespace p44 {

  class ExternalDeviceConnector;
  class ExternalVdc;
  class ExternalDevice;

  #if ENABLE_EXTERNAL_SINGLEDEVICE

  class ExternalDeviceAction : public DeviceAction
  {
    typedef DeviceAction inherited;

    StatusCB callback;

  public:

    /// create the action
    /// @param aSingleDevice the single device this action belongs to
    /// @param aName the name of the action.
    /// @param aDescription a description string for the action.
    ExternalDeviceAction(SingleDevice &aSingleDevice, const string aName, const string aDescription, const string aTitle);

    virtual ~ExternalDeviceAction();

    ExternalDevice &getExternalDevice();

    /// implementation of action
    virtual void performCall(ApiValuePtr aParams, StatusCB aCompletedCB) P44_OVERRIDE;

    /// process action call confirmation message from external device
    void callPerformed(JsonObjectPtr aStatusInfo);

  private:

  };
  typedef boost::intrusive_ptr<ExternalDeviceAction> ExternalDeviceActionPtr;

  #endif // ENABLE_EXTERNAL_SINGLEDEVICE



  typedef boost::intrusive_ptr<ExternalDeviceConnector> ExternalDeviceConnectorPtr;

  typedef boost::intrusive_ptr<ExternalDevice> ExternalDevicePtr;
  class ExternalDevice :
    #if ENABLE_EXTERNAL_SINGLEDEVICE
    public SingleDevice
    #else
    public Device
    #endif
  {
    #if ENABLE_EXTERNAL_SINGLEDEVICE
    typedef SingleDevice inherited;
    friend class ExternalDeviceAction;
    #else
    typedef Device inherited;
    #endif
    friend class ExternalVdc;
    friend class ExternalDeviceConnector;

    ExternalDeviceConnectorPtr deviceConnector;
    string tag; ///< the tag to address the device within the devices on the same connection

    string iconBaseName; ///< the base icon name
    string modelNameString; ///< the string to be returned by modelName()
    string vendorNameString; ///< the vendor name
    string oemModelGUIDString; ///< the OEM model GUID, which is used to match devices with dS database
    string typeIdentifier; ///< the type identifier
    string devClass; ///< device class
    string configUrl; ///< custom value for configURL if not empty
    uint32_t devClassVersion; ///< device class version


    bool configured; ///< set when device is configured (init message received and device added to vdc)
    bool useMovement; ///< if set, device communication uses MV/move command for dimming and shadow device operation
    bool controlValues; ///< if set, device communication uses CTRL/control command to forward system control values such as "heatingLevel" and "TemperatureZone"
    bool querySync; ///< if set, device is asked for synchronizing actual values of channels when needed (e.g. before saveScene)
    bool sceneCommands; ///< if set, scene commands are forwarded to the external device

    string configurationId; ///< current configuration's id
    DeviceConfigurationsVector configurations; ///< the device's possible configurations

    #if ENABLE_EXTERNAL_SINGLEDEVICE
    bool noConfirmAction; ///< if set, device implementation is not expected to use
    #endif


    SimpleCB syncedCB; ///< will be called when device confirms "SYNC" message with "SYNCED" response

  public:

    ExternalDevice(Vdc *aVdcP, ExternalDeviceConnectorPtr aDeviceConnector, string aTag);
    virtual ~ExternalDevice();

    /// identify a device up to the point that it knows its dSUID and internal structure. Possibly swap device object for a more specialized subclass.
    virtual bool identifyDevice(IdentifyDeviceCB aIdentifyCB) P44_OVERRIDE;

    ExternalVdc &getExternalVdc();

    /// device type identifier
		/// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const  P44_OVERRIDE { return typeIdentifier; };

    /// @return human readable model name/short description
    virtual string modelName() P44_OVERRIDE;

    /// @return Vendor name if known
    virtual string vendorName() P44_OVERRIDE;

    /// @return OEM model GUID in URN format to identify the OEM product MODEL hardware as uniquely as possible
    virtual string oemModelGUID() P44_OVERRIDE;

    /// device class (for grouping functionally equivalent single devices)
    /// @note usually, only single devices do have a deviceClass
    /// @return name of the device class, such as "washingmachine" or "kettle" or "oven". Empty string if no device class exists.
    virtual string deviceClass() P44_OVERRIDE { return devClass; }

    /// device class version number.
    /// @note This allows different versions of the functional representation of the device class
    ///   to coexist in a system.
    /// @return version or 0 if no version exists
    virtual uint32_t deviceClassVersion() P44_OVERRIDE { return devClassVersion; }

    /// @return URL for Web-UI (for access from local LAN)
    virtual string webuiURLString() P44_OVERRIDE;

    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

  protected:

    /// prepare for calling a scene on the device level
    /// @param aScene the scene that is to be called
    /// @return true if scene preparation is ok and call can continue. If false, no further action will be taken
    /// @note this is called BEFORE scene values are recalled
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
    virtual void dimChannel(ChannelBehaviourPtr aChannel, VdcDimMode aDimMode) P44_OVERRIDE;

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

    /// device configurations implementation
    virtual string getDeviceConfigurationId() P44_OVERRIDE;
    virtual ErrorPtr switchConfiguration(const string aConfigurationId) P44_OVERRIDE;
    virtual void getDeviceConfigurations(DeviceConfigurationsVector &aConfigurations, StatusCB aStatusCB) P44_OVERRIDE;

    #if ENABLE_EXTERNAL_SINGLEDEVICE

    /// @name factory methods for elements configured via dynamic JSON config
    /// @{

    virtual ErrorPtr actionFromJSON(DeviceActionPtr &aAction, JsonObjectPtr aJSONConfig, const string aActionId, const string aDescription) P44_OVERRIDE;
    virtual ErrorPtr dynamicActionFromJSON(DeviceActionPtr &aAction, JsonObjectPtr aJSONConfig, const string aActionId, const string aDescription, const string aTitle) P44_OVERRIDE;

    /// @}

    #endif


  private:

    void handleDeviceApiJsonMessage(JsonObjectPtr aMessage);
    void handleDeviceApiSimpleMessage(string aMessage);
    void sendDeviceApiJsonMessage(JsonObjectPtr aMessage);
    void sendDeviceApiSimpleMessage(string aMessage);
    void sendDeviceApiStatusMessage(ErrorPtr aError);

    ErrorPtr configureDevice(JsonObjectPtr aInitParams);
    ErrorPtr processJsonMessage(string aMessageType, JsonObjectPtr aMessage);
    ErrorPtr processSimpleMessage(string aMessageType, string aValue);
    ErrorPtr processInputJson(char aInputType, JsonObjectPtr aParams);
    ErrorPtr processInput(char aInputType, uint32_t aIndex, double aValue);

    #if ENABLE_EXTERNAL_SINGLEDEVICE
    ErrorPtr parseParam(const string aParamName, JsonObjectPtr aParamDetails, ValueDescriptorPtr &aParam);
    void propertyChanged(ValueDescriptorPtr aChangedProperty);
    #endif

    void changeChannelMovement(int aChannelIndex, SimpleCB aDoneCB, int aNewDirection);
    void releaseButton(ButtonBehaviourPtr aButtonBehaviour);

  };


  typedef map<string,ExternalDevicePtr> ExternalDevicesMap;

  class ExternalDeviceConnector : public P44Obj
  {
    friend class ExternalDevice;

    ExternalVdc &externalVdc;

    bool simpletext; ///< if set, device communication uses very simple text messages rather than JSON

    JsonCommPtr deviceConnection;
    ExternalDevicesMap externalDevices;

  public:

    ExternalDeviceConnector(ExternalVdc &aExternalVdc, JsonCommPtr aDeviceConnection);
    virtual ~ExternalDeviceConnector();

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
  class ExternalVdc : public Vdc
  {
    typedef Vdc inherited;
    friend class ExternalDevice;
    friend class ExternalDeviceConnector;

    SocketCommPtr externalDeviceApiServer;

    string iconBaseName; ///< the base icon name
    string modelNameString; ///< the string to be returned by modelName()
    string configUrl; ///< custom value for configURL if not empty

  public:
    ExternalVdc(int aInstanceNumber, const string &aSocketPathOrPort, bool aNonLocal, VdcHost *aVdcHostP, int aTag);

    void initialize(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    virtual const char *vdcClassIdentifier() const P44_OVERRIDE;

    /// scan for (collect) devices and add them to the vdc
    virtual void scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags) P44_OVERRIDE;

    /// @return human readable, language independent suffix to explain vdc functionality.
    ///   Will be appended to product name to create modelName() for vdcs
    virtual string vdcModelSuffix() const P44_OVERRIDE { return "external"; }

    /// External device container should not be announced when it has no devices
    /// @return if true, this vDC should not be announced towards the dS system when it has no devices
    virtual bool invisibleWhenEmpty() P44_OVERRIDE { return true; }

    /// get supported rescan modes for this vDC. This indicates (usually to a web-UI) which
    /// of the flags to collectDevices() make sense for this vDC.
    /// @return a combination of rescanmode_xxx bits
    virtual int getRescanModes() const P44_OVERRIDE { return rescanmode_exhaustive; }; // only exhaustive makes sense

    /// Custom identification for external vDCs
    /// @{

    /// @return human readable, language independent model name/short description
    /// @note base class will construct this from global product name and vdcModelSuffix()
    virtual string modelName() P44_OVERRIDE;

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


  private:

    SocketCommPtr deviceApiConnectionHandler(SocketCommPtr aServerSocketCommP);

  };

} // namespace p44


#endif // ENABLE_EXTERNAL
#endif // __p44vdc__externalvdc__
