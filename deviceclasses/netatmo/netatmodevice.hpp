//
//  Copyright (c) 2017 digitalSTROM.org, Zurich, Switzerland
//
//  Author: Krystian Heberlein <krystian.heberlein@digitalstrom.com>
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

#ifndef __p44vdc__netatmodevice__
#define __p44vdc__netatmodevice__

#include "netatmovdc.hpp"

#if ENABLE_NETATMO

#include "singledevice.hpp"
#include "simplescene.hpp"
#include "sensorbehaviour.hpp"
#include "binaryinputbehaviour.hpp"
#include "netatmocomm.hpp"

using namespace std;

namespace p44 {

  class NetatmoVdc;
  class INetatmoComm;
  class NetatmoDevice;


  // MARK: ===== NetatmoDeviceSettings


  class NetatmoDeviceSettings : public CmdSceneDeviceSettings
  {
    using inherited = CmdSceneDeviceSettings;

  public:

    NetatmoDeviceSettings(Device &aDevice) : inherited(aDevice) {};


  };
  using NetatmoDeviceSettingsPtr = boost::intrusive_ptr<NetatmoDeviceSettings>;


  // MARK: ===== NetatmoScene


  /// A concrete class implementing the Scene object for a audio device, having a volume channel plus a index value (for specific song/sound effects)
  /// @note subclasses can implement more parameters
  class NetatmoScene : public SimpleCmdScene
  {
    using inherited = SimpleCmdScene;

  public:

    NetatmoScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo) : inherited(aSceneDeviceSettings, aSceneNo) {};

  };
  using NetatmoScenePtr = boost::intrusive_ptr<NetatmoScene>;


  // MARK: ===== NetatmoDevice


  using NetatmoDevicePtr = boost::intrusive_ptr<NetatmoDevice>;

  class NetatmoDevice : public SingleDevice
  {
    using inherited = SingleDevice;

    protected:

    static const MLMicroSeconds SENSOR_UPDATE_INTERVAL = 30*Second;
    static const MLMicroSeconds SENSOR_ALIVESIGN_INTERVAL = 10*Minute;
    static const int LOW_BATTERY_THRESHOLD_INDOOR = 4920;
    static const int LOW_BATTERY_THRESHOLD_OUTDOOR = 4500;

    /*device properties*/
    ValueDescriptorPtr swVersion;
    ValueDescriptorPtr measurementTimestamp;

    /*device sensors*/
    SensorBehaviourPtr sensorTemperature;
    SensorBehaviourPtr sensorHumidity;

    /*device states*/
    DeviceStatePtr statusTempTrend;

    enum class StatusTrend;

    string netatmoId;
    string netatmoName;
    string netatmoFw;
    string baseStationId;

    boost::signals2::connection cbConnection;

    VdcUsageHint usageArea;

    protected:
    NetatmoDevice(NetatmoVdc *aVdcP, INetatmoComm& aINetatmoComm, JsonObjectPtr aDeviceData, VdcUsageHint aUsageArea, const string& aBaseStationId={});
    virtual ~NetatmoDevice();

    public:
    /// identify a device up to the point that it knows its dSUID and internal structure. Possibly swap device object for a more specialized subclass.
    virtual bool identifyDevice(IdentifyDeviceCB aIdentifyCB) P44_OVERRIDE;

    /// device type identifier
		/// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const P44_OVERRIDE { return "netatmo"; };

    NetatmoVdc &netatmoVdc();

    /// description of object, mainly for debug and logging
    /// @return textual description of object
    virtual string description() P44_OVERRIDE;

    /// @name interaction with subclasses, actually representing physical I/O
    /// @{

    /// initializes the physical device for being used
    /// @param aFactoryReset if set, the device will be initialized as thoroughly as possible (factory reset, default settings etc.)
    /// @note this is called before interaction with dS system starts (usually just after collecting devices)
    /// @note implementation should call inherited when complete, so superclasses could chain further activity
    virtual void initializeDevice(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    /// check presence of this addressable
    /// @param aPresenceResultHandler will be called to report presence status
    virtual void checkPresence(PresenceCB aPresenceResultHandler) P44_OVERRIDE;

    /// disconnect device
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

//    /// @return model GUID in URN format to identify model of the connected hardware device as uniquely as possible
//    virtual string hardwareModelGUID() P44_OVERRIDE;

    /// @return human readable version string
    /// @note base class implementation returns version string of vdc host by default
    virtual string modelVersion() P44_OVERRIDE;

    /// @return Vendor name if known
    virtual string vendorName() P44_OVERRIDE;



    /// @}

    void deriveDsUid();

    /*Device sensors factory*/
    SensorBehaviourPtr createSensorCO2();
    SensorBehaviourPtr createSensorNoise();

    /*Device status factory*/
    EnumValueDescriptorPtr createTrendEnum(const string& aName);
    BinaryInputBehaviourPtr createStatusBattery();


    /// Callbacks for state ans property changes
    void stateChanged(DeviceStatePtr aChangedState, DeviceEventsList &aEventsToPush);

    protected:
    /// Configure device before initialization
    virtual void configureDevice();
    void setIdentificationData(JsonObjectPtr aJson);
    virtual void updateData(JsonObjectPtr aJson);
    JsonObjectPtr findDeviceJson(JsonObjectPtr aJsonArray, const string& aDeviceId);
    virtual JsonObjectPtr findModuleJson(JsonObjectPtr aJsonArray);

  };
  

} // namespace p44


#endif // ENABLE_NETATMO
#endif // __p44vdc__netatmodevice__
