//
//  Copyright (c) 2013-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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


#ifndef __p44vdc__sparkiodevice__
#define __p44vdc__sparkiodevice__

#include "device.hpp"

#if ENABLE_STATIC

#include "jsonwebclient.hpp"
#include "colorlightbehaviour.hpp"
#include "staticvdc.hpp"


using namespace std;

namespace p44 {

  enum {
    channeltype_sparkmode = channeltype_custom_first
  };


  class SparkModeChannel : public IndexChannel
  {
    typedef IndexChannel inherited;

  public:
    SparkModeChannel(OutputBehaviour &aOutput) : inherited(aOutput,"sparkMode") { setNumIndices(4); };

    virtual DsChannelType getChannelType() { return channeltype_sparkmode; }; ///< custom device-specific channel
    virtual const char *getName() { return "x-p44-sparkmode"; };
  };


  class StaticVdc;
  class SparkIoDevice;

  class SparkLightScene : public ColorLightScene
  {
    typedef ColorLightScene inherited;
  public:
    SparkLightScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo); ///< constructor, sets values according to dS specs' default values

    /// @name spark core based light scene specific values
    /// @{

    uint32_t extendedState; // extended state (beyond brightness+rgb) of the spark core light

    /// @}

    /// get scene value
    /// @param aChannelIndex the channel index (0=primary channel, 1..n other channels)
    /// @return the scene value
    virtual double sceneValue(int aChannelIndex);

    /// modify per-value scene flags
    /// @param aChannelIndex the channel index (0=primary channel, 1..n other channels)
    /// @param aValue the new scene value
    virtual void setSceneValue(int aChannelIndex, double aValue);


    /// Set default scene values for a specified scene number
    /// @param aSceneNo the scene number to set default values
    virtual void setDefaultSceneValues(SceneNo aSceneNo);

  protected:

    // persistence implementation
    virtual const char *tableName();
    virtual size_t numFieldDefs();
    virtual const FieldDefinition *getFieldDef(size_t aIndex);
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP);
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags);

  };
  typedef boost::intrusive_ptr<SparkLightScene> SparkLightScenePtr;


  class SparkLightBehaviour : public RGBColorLightBehaviour
  {
    typedef RGBColorLightBehaviour inherited;

  public:

    /// @name channels
    /// @{
    ChannelBehaviourPtr sparkmode;
    /// @}

    SparkLightBehaviour(Device &aDevice);

    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc();


  protected:

    /// called by performApplySceneToChannels() to load channel values from a scene.
    /// @param aScene the scene to load channel values from
    /// @note Scenes don't have 1:1 representation of all channel values for footprint and logic reasons, so this method
    ///   is implemented in the specific behaviours according to the scene layout for that behaviour.
    virtual void loadChannelsFromScene(DsScenePtr aScene);

    /// called by captureScene to save channel values to a scene.
    /// @param aScene the scene to save channel values to
    /// @note Scenes don't have 1:1 representation of all channel values for footprint and logic reasons, so this method
    ///   is implemented in the specific behaviours according to the scene layout for that behaviour.
    /// @note call markDirty on aScene in case it is changed (otherwise captured values will not be saved)
    virtual void saveChannelsToScene(DsScenePtr aScene);

  };
  typedef boost::intrusive_ptr<SparkLightBehaviour> SparkLightBehaviourPtr;



  /// the persistent parameters of a light scene device (including scene table)
  class SparkDeviceSettings : public ColorLightDeviceSettings
  {
    typedef ColorLightDeviceSettings inherited;

  public:
    SparkDeviceSettings(Device &aDevice);

    /// factory method to create the correct subclass type of DsScene with default values
    /// @param aSceneNo the scene number to create a scene object with proper default values for.
    virtual DsScenePtr newDefaultScene(SceneNo aSceneNo);
    
  };
  
  

  typedef boost::intrusive_ptr<SparkIoDevice> SparkIoDevicePtr;
  class SparkIoDevice : public StaticDevice
  {
    typedef StaticDevice inherited;

    friend class SparkLightBehaviour;

    string sparkCoreID;
    string sparkCoreToken;
    JsonWebClient sparkCloudComm;
    int apiVersion;
    MLTicket retryTicket;

  public:
    SparkIoDevice(StaticVdc *aVdcP, const string &aDeviceConfig);

    /// device type identifier
		/// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const { return "spark_io"; };

    /// description of object, mainly for debug and logging
    /// @return textual description of object
    virtual string description();


    /// @name interaction with subclasses, actually representing physical I/O
    /// @{

    /// initializes the physical device for being used
    /// @param aFactoryReset if set, the device will be inititalized as thoroughly as possible (factory reset, default settings etc.)
    /// @note this is called before interaction with dS system starts (usually just after collecting devices)
    /// @note implementation should call inherited when complete, so superclasses could chain further activity
    virtual void initializeDevice(StatusCB aCompletedCB, bool aFactoryReset);

    /// check presence of this addressable
    /// @param aPresenceResultHandler will be called to report presence status
    virtual void checkPresence(PresenceCB aPresenceResultHandler);

    /// apply all pending channel value updates to the device's hardware
    /// @note this is the only routine that should trigger actual changes in output values. It must consult all of the device's
    ///   ChannelBehaviours and check isChannelUpdatePending(), and send new values to the device hardware. After successfully
    ///   updating the device hardware, channelValueApplied() must be called on the channels that had isChannelUpdatePending().
    /// @param aDoneCB if not NULL, must be called when values are applied
    /// @param aForDimming hint for implementations to optimize dimming, indicating that change is only an increment/decrement
    ///   in a single channel (and not switching between color modes etc.)
    virtual void applyChannelValues(SimpleCB aDoneCB, bool aForDimming);

    /// synchronize channel values by reading them back from the device's hardware (if possible)
    /// @param aDoneCB will be called when values are updated with actual hardware values
    /// @note this method is only called at startup and before saving scenes to make sure changes done to the outputs directly (e.g. using
    ///   a direct remote control for a lamp) are included. Just reading a channel state does not call this method.
    /// @note implementation must use channel's syncChannelValue() method
    virtual void syncChannelValues(SimpleCB aDoneCB);

    /// @}


    /// @name identification of the addressable entity
    /// @{

    /// @return human readable model name/short description
    virtual string modelName() { return "SparkCore RGB light"; }

    /// @return hardware GUID in URN format to identify hardware as uniquely as possible
    virtual string hardwareGUID() { return string_format("sparkcoreid:%s", sparkCoreID.c_str()); }

    /// @return Vendor ID in URN format to identify vendor as uniquely as possible
    virtual string vendorName() { return "particle.io"; };

    /// @}

  protected:

    void deriveDsUid();

  private:

    bool sparkApiCall(JsonWebClientCB aResponseCB, string aArgs);

    void apiVersionReceived(StatusCB aCompletedCB, bool aFactoryReset, JsonObjectPtr aJsonResponse, ErrorPtr aError);
    void presenceStateReceived(PresenceCB aPresenceResultHandler, JsonObjectPtr aDeviceInfo, ErrorPtr aError);

    void channelValuesSent(SparkLightBehaviourPtr aSparkLightBehaviour, SimpleCB aDoneCB, JsonObjectPtr aJsonResponse, ErrorPtr aError);
    void channelValuesReceived(SimpleCB aDoneCB, JsonObjectPtr aJsonResponse, ErrorPtr aError);

  };
  
} // namespace p44


#endif // ENABLE_STATIC
#endif // __p44vdc__sparkiodevice__
