//
//  Copyright (c) 2016 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__homeconnectdevice__
#define __p44vdc__homeconnectdevice__

#include "singledevice.hpp"

#if ENABLE_HOMECONNECT

#include "jsonobject.hpp"
#include "simplescene.hpp"
#include "homeconnectcomm.hpp"

using namespace std;

namespace p44 {

  class HomeConnectVdc;
  class HomeConnectDevice;
  class HomeConnectComm;

  class HomeConnectDeviceSettings : public CmdSceneDeviceSettings
  {
    typedef CmdSceneDeviceSettings inherited;

  public:

    HomeConnectDeviceSettings(Device &aDevice) : inherited(aDevice) {};

    /// factory method to create the correct subclass type of DsScene
    /// @param aSceneNo the scene number to create a scene object for.
    /// @note setDefaultSceneValues() must be called to set default scene values
    virtual DsScenePtr newDefaultScene(SceneNo aSceneNo);


  };
  typedef boost::intrusive_ptr<HomeConnectDeviceSettings> HomeConnectDeviceSettingsPtr;


  /// A concrete class implementing the Scene object for a audio device, having a volume channel plus a index value (for specific song/sound effects)
  /// @note subclasses can implement more parameters
  class HomeConnectScene : public SimpleCmdScene
  {
    typedef SimpleCmdScene inherited;

  public:

    HomeConnectScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo) : inherited(aSceneDeviceSettings, aSceneNo) {};

    /// Set default scene values for a specified scene number
    /// @param aSceneNo the scene number to set default values
    virtual void setDefaultSceneValues(SceneNo aSceneNo);

  };
  typedef boost::intrusive_ptr<HomeConnectScene> HomeConnectScenePtr;



  class HomeConnectAction : public DeviceAction
  {
    typedef DeviceAction inherited;

    string apiCommandTemplate;

  public:

    /// create the action
    /// @param aSingleDevice the single device this action belongs to
    /// @param aName the name of the action.
    /// @param aDescription a description string for the action.
    /// @param aApiCommandTemplate the API command template to use for this action
    HomeConnectAction(SingleDevice &aSingleDevice, const string& aName, const string& aDescription, const string& aApiCommandTemplate);

    HomeConnectDevice &getHomeConnectDevice();

    /// implementation of action
    virtual void performCall(ApiValuePtr aParams, StatusCB aCompletedCB) P44_OVERRIDE;

  private:

    ErrorPtr valueLookup(ApiValuePtr aParams, const string aName, string &aValue);
    void apiCommandSent(StatusCB aCompletedCB, JsonObjectPtr aResult, ErrorPtr aError);
    
  };
  typedef boost::intrusive_ptr<HomeConnectAction> HomeConnectActionPtr;
  

  class HomeConnectCommandBuilder
  {
  private:
    string programName;

    map<string, string> options;
  public:
    HomeConnectCommandBuilder(string aProgramName);

    void addOption(string key, string value) {  options[key] = value; }

    string build();

  };

  typedef boost::intrusive_ptr<HomeConnectDevice> HomeConnectDevicePtr;
  class HomeConnectDevice : public SingleDevice
  {
    typedef SingleDevice inherited;
    friend class HomeConnectAction;

  protected:

    string haId; ///< the home appliance ID
    string model; ///< the model name for the device
    string modelGuid; ///< the model guid for the device
    string vendor; ///< the vendor of this device

    HomeConnectEventMonitorPtr eventMonitor; ///< event monitor

    // common states, they can be NULL in case this state is not valid for device class
    DeviceStatePtr operationMode;
    DeviceStatePtr remoteControl;
    DeviceStatePtr doorState;
    DeviceStatePtr powerState;

    // Common Properties
    ValueDescriptorPtr programName;

    HomeConnectDevice(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord);
    virtual ~HomeConnectDevice();
  public:

    /// A factory method that create a device of proper type
    static HomeConnectDevicePtr createHomeConenctDevice(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord);

    /// identify a device up to the point that it knows its dSUID and internal structure. Possibly swap device object for a more specialized subclass.
    virtual bool identifyDevice(IdentifyDeviceCB aIdentifyCB) P44_OVERRIDE;

    /// device type identifier
		/// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const P44_OVERRIDE { return "homeConnect"; };

    HomeConnectVdc &homeConnectVdc();
    HomeConnectComm &homeConnectComm();

    /// description of object, mainly for debug and logging
    /// @return textual description of object
    virtual string description() P44_OVERRIDE;

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

    /// disconnect device. For homeConnect, we'll check if the device is still reachable, and only if not
    /// we allow disconnection
    /// @param aForgetParams if set, not only the connection to the device is removed, but also all parameters related to it
    ///   such that in case the same device is re-connected later, it will not use previous configuration settings, but defaults.
    /// @param aDisconnectResultHandler will be called to report true if device could be disconnected,
    ///   false in case it is certain that the device is still connected to this and only this vDC
    virtual void disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler) P44_OVERRIDE;

    /// @}


    /// @name identification of the addressable entity
    /// @{

    /// @return hardware GUID in URN format to identify the hardware INSTANCE as uniquely as possible
    /// @note Smarterdevices don't have anything but MAC address
    /// - macaddress:MM:MM:MM:MM:MM:MM = MAC Address in hex
    virtual string hardwareGUID() P44_OVERRIDE;

    /// @return model GUID in URN format to identify model of the connected hardware device as uniquely as possible
    virtual string hardwareModelGUID() P44_OVERRIDE;

    /// @return human readable model name/short description
    virtual string modelName() P44_OVERRIDE;

    /// @return Vendor name if known
    virtual string vendorName() P44_OVERRIDE;

    /// @return OEM model GUID in URN format to identify the OEM product MODEL hardware as uniquely as possible
    virtual string oemModelGUID() P44_OVERRIDE = 0;
    
    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

    /// @}

  protected:

    typedef struct {
      bool hasInactive : 1;
      bool hasReady : 1;
      bool hasDelayedStart : 1;
      bool hasRun : 1;
      bool hasPause : 1;
      bool hasActionrequired : 1;
      bool hasFinished : 1;
      bool hasError : 1;
      bool hasAborting : 1;
    } OperationModeConfiguration;

    typedef struct {
      bool hasControlInactive : 1;
      bool hasControlActive : 1;
      bool hasStartActive : 1;
    } RemoteControlConfiguration;

    typedef struct {
      bool hasOpen : 1;
      bool hasClosed : 1;
      bool hasLocked : 1;
    } DoorStateConfiguration;

    typedef struct {
      bool hasOff : 1;
      bool hasOn : 1;
      bool hasStandby : 1;
    } PowerStateConfiguration;

    // The following methods implement common behaviour and should be executed from subclasses overrides.
    virtual bool configureDevice();
    virtual void stateChanged(DeviceStatePtr aChangedState, DeviceEventsList &aEventsToPush);
    void handleEvent(EventType aEventType, JsonObjectPtr aEventData, ErrorPtr aError);
    virtual void handleEventTypeNotify(string aKey, JsonObjectPtr aValue);
    virtual void handleEventTypeEvent(string aKey);
    virtual void handleEventTypeStatus(string aKey, JsonObjectPtr aValue);

    // Pool the state of the device and process the responses
    void pollState();
    virtual void pollStateStatusDone(JsonObjectPtr aResult, ErrorPtr aError);
    virtual void pollStateSettingsDone(JsonObjectPtr aResult, ErrorPtr aError);
    virtual void pollStateProgramDone(JsonObjectPtr aResult, ErrorPtr aError);

    // The following methods are used to configure common states
    void configureOperationModeState(const OperationModeConfiguration& aConfiguration);
    void configureRemoteControlState(const RemoteControlConfiguration& aConfiguration);
    void configureDoorState(const DoorStateConfiguration& aConfiguration);
    void configurePowerState(const PowerStateConfiguration& aConfiguration);

    // Create dsuid based on device id
    void deriveDsUid();

    // utility function to remove the namespace from home connect values, keys and paths
    static string removeNamespace(const string& aString);

  private:

    void disconnectableHandler(bool aForgetParams, DisconnectCB aDisconnectResultHandler, bool aPresent);

  };
  
} // namespace p44


#endif // ENABLE_HOMECONNECT
#endif // __p44vdc__homeconnectdevice__
