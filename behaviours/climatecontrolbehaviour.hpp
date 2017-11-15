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

#ifndef __p44vdc__climatecontrolbehaviour__
#define __p44vdc__climatecontrolbehaviour__

#ifndef ENABLE_FCU_SUPPORT
  #define ENABLE_FCU_SUPPORT 1
#endif


#include "device.hpp"
#include "outputbehaviour.hpp"
#include "simplescene.hpp"

using namespace std;

namespace p44 {

  class PowerLevelChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;

  public:
    PowerLevelChannel(OutputBehaviour &aOutput) : inherited(aOutput, "heatingPower") { resolution = 1; /* 1% of full scale */ };

    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_heating_power; };
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_percent, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "power level"; };
    virtual double getMin() P44_OVERRIDE { return 0; }; // 0..100 (cooling or heating)
    virtual double getMax() P44_OVERRIDE { return 100; };

  };

  #if ENABLE_FCU_SUPPORT

  /// ventilation airflow direction channel states
  typedef enum {
    fcuOperatingMode_off = 0,
    fcuOperatingMode_heat = 1,
    fcuOperatingMode_cool = 2,
    fcuOperatingMode_fan = 3,
    fcuOperatingMode_dry = 4,
    fcuOperatingMode_auto = 5,
    numFcuOperationModes
  } FcuOperationMode;


  class FcuOperationModeChannel : public IndexChannel
  {
    typedef IndexChannel inherited;

  public:
    FcuOperationModeChannel(OutputBehaviour &aOutput) : inherited(aOutput, "operationMode") { setNumIndices(numFcuOperationModes); }; ///< see FcuOperationModes enum

    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_fcu_operation_mode; }; ///< the dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_none, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "FCU operation mode"; };
  };


  class FcuPowerStateChannel : public FlagChannel
  {
    typedef FlagChannel inherited;

  public:
    FcuPowerStateChannel(OutputBehaviour &aOutput) : inherited(aOutput, "powerState") { };

    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_power_state; }; ///< the dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_none, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "FCU power state"; };
  };

  #endif // ENABLE_FCU_SUPPORT


  /// A climate scene
  class ClimateControlScene : public SimpleScene
  {
    typedef SimpleScene inherited;

  public:
    ClimateControlScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo); ///< constructor, sets values according to dS specs' default values

    /// Set default scene values for a specified scene number
    /// @param aSceneNo the scene number to set default values
    virtual void setDefaultSceneValues(SceneNo aSceneNo);

  };
  typedef boost::intrusive_ptr<ClimateControlScene> ClimateControlScenePtr;


  /// the persistent parameters of a climate scene device (including scene table)
  class ClimateDeviceSettings : public SceneDeviceSettings
  {
    typedef SceneDeviceSettings inherited;

  public:
    ClimateDeviceSettings(Device &aDevice);

    /// factory method to create the correct subclass type of DsScene
    /// @param aSceneNo the scene number to create a scene object for.
    /// @note setDefaultSceneValues() must be called to set default scene values
    virtual DsScenePtr newDefaultScene(SceneNo aSceneNo);

  };


  #if ENABLE_FCU_SUPPORT

  /// A FCU scene
  class FanCoilUnitScene : public DsScene
  {
    typedef DsScene inherited;
    friend class ClimateControlBehaviour;

  public:
    FanCoilUnitScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo); ///< constructor, sets values according to dS specs' default values

    /// @name FanCoilUnitScene specific values
    /// @{

    bool powerState; ///< power on/off
    FcuOperationMode operationMode; ///< operation mode
    //double targetTempOffset; ///< ??? maybe

    /// @}

    /// Set default scene values for a specified scene number
    /// @param aSceneNo the scene number to set default values
    virtual void setDefaultSceneValues(SceneNo aSceneNo) P44_OVERRIDE;

    // scene values implementation
    virtual double sceneValue(int aOutputIndex) P44_OVERRIDE;
    virtual void setSceneValue(int aOutputIndex, double aValue) P44_OVERRIDE;

  protected:

    // persistence implementation
    virtual const char *tableName() P44_OVERRIDE;
    virtual size_t numFieldDefs() P44_OVERRIDE;
    virtual const FieldDefinition *getFieldDef(size_t aIndex) P44_OVERRIDE;
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP) P44_OVERRIDE;
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags) P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<FanCoilUnitScene> FanCoilUnitScenePtr;


  /// the persistent parameters of a climate scene device (including scene table)
  class FanCoilUnitDeviceSettings : public SceneDeviceSettings
  {
    typedef SceneDeviceSettings inherited;

  public:
    FanCoilUnitDeviceSettings(Device &aDevice);

    /// factory method to create the correct subclass type of DsScene
    /// @param aSceneNo the scene number to create a scene object for.
    /// @note setDefaultSceneValues() must be called to set default scene values
    virtual DsScenePtr newDefaultScene(SceneNo aSceneNo);
    
  };

  #endif // ENABLE_FCU_SUPPORT


  typedef enum {
    climatedevice_simple,
    climatedevice_fancoilunit
  } ClimateDeviceKind;


  /// Implements the behaviour of climate control outputs, in particular evaluating
  /// control values with processControlValue()
  class ClimateControlBehaviour : public OutputBehaviour
  {
    typedef OutputBehaviour inherited;

  protected:

    /// @name hardware derived parameters (constant during operation)
    /// @{

    /// kind of climate device
    ClimateDeviceKind climateDeviceKind;

    /// @}


    /// @name persistent settings
    /// @{

    /// set if climate controlling output is in idle (summer) mode - uses less energy or is switched off
    /// @note this flag is not exposed as a property, but set/reset by callScene(29=wintermode) and callScene(30=summermode)
    bool climateControlIdle;

    /// set if climate controlling output is turn to heating (true) or active/passive cooling (false)
    /// @note this flag is not exposed as a property, but set/reset by callScene(<0,6>=heating) and callScene(<7,11>=cooling)
    bool climateModeHeating;

    /// defines how "heatingLevel" is applied to the output
    VdcHeatingSystemCapability heatingSystemCapability;
    VdcHeatingSystemType heatingSystemType;

    /// @}


    /// @name internal volatile state
    /// @{

    /// if set, a valve phrophylaxis run is performed on next occasion. Flag automatically resets afterwards.
    /// @note this flag is not exposed as a property, but can be set by callScene(31=prophylaxis)
    bool runProphylaxis;

    MLMicroSeconds zoneTemperatureUpdated; ///< time of when zoneTemperature was last updated from processControlValue
    double zoneTemperature; ///< current zone (room) temperature
    MLMicroSeconds zoneTemperatureSetPointUpdated; ///< time of when zoneSetPoint was last updated from processControlValue
    double zoneTemperatureSetPoint; ///< current zone(room) temperature set point

    /// @}

    /// channels
    ChannelBehaviourPtr powerLevel; // Valve only
    FlagChannelPtr powerState; // FCU only
    IndexChannelPtr operationMode; // FCU only

  public:


    ClimateControlBehaviour(Device &aDevice, ClimateDeviceKind aKind, VdcHeatingSystemCapability aDefaultHeatingSystemCapability);

    /// device type identifier
    /// @return constant identifier for this type of behaviour
    virtual const char *behaviourTypeIdentifier() P44_OVERRIDE P44_FINAL { return "climatecontrol"; };

    /// @name interface towards actual device hardware (or simulation)
    /// @{

    /// @return true if device should be in idle mode
    bool isClimateControlIdle() { return climateControlIdle; };

    /// @return true if device should be in heating mode
    bool isClimateModeHeating() { return climateModeHeating; };

    /// @return true if device should run a prophylaxis cycle
    /// @note automatically resets the internal flag when queried
    bool shouldRunProphylaxis() { if (runProphylaxis) { runProphylaxis=false; return true; } else return false; };


    /// get temperature information needed for regulation
    /// @param aCurrentTemperature will receive current zone (room) temperature
    /// @param aTemperatureSetpoint will receive current set point for zone (room) temperature
    /// @return true if values are available, false if no values could be returned
    bool getZoneTemperatures(double &aCurrentTemperature, double &aTemperatureSetpoint);

    /// @}


    /// @name interaction with digitalSTROM system
    /// @{

    /// check for presence of model feature (flag in dSS visibility matrix)
    /// @param aFeatureIndex the feature to check for
    /// @return yes if this output behaviour has the feature, no if (explicitly) not, undefined if asked entity does not know
    virtual Tristate hasModelFeature(DsModelFeatures aFeatureIndex) P44_OVERRIDE;

    /// Process a named control value. The type, group membership and settings of the device determine if at all,
    /// and if, how the value affects physical outputs of the device or general device operation
    /// @note if this method adjusts channel values, it must not directly update the hardware, but just
    ///   prepare channel values such that these can be applied using requestApplyingChannels().
    /// @param aName the name of the control value, which describes the purpose
    /// @param aValue the control value to process
    /// @note base class by default forwards the control value to all of its output behaviours.
    /// @return true if value processing caused channel changes so channel values should be applied
    virtual bool processControlValue(const string &aName, double aValue) P44_OVERRIDE;

    /// apply scene to output channels
    /// @param aScene the scene to apply to output channels
    /// @return true if apply is complete, i.e. everything ready to apply to hardware outputs.
    ///   false if scene cannot yet be applied to hardware, and/or will be performed later/separately
    /// @note this derived class' applyScene only implements special hard-wired behaviour specific scenes,
    ///   basic scene apply functionality is provided by base class' implementation already.
    virtual bool applyScene(DsScenePtr aScene) P44_OVERRIDE;

    /// @}

    /// description of object, mainly for debug and logging
    /// @return textual description of object, may contain LFs
    virtual string description() P44_OVERRIDE;

    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc() P44_OVERRIDE;

    VdcHeatingSystemType getHeatingSystemType()  { return heatingSystemType; }
    VdcHeatingSystemCapability getHeatingSystemCapability() { return heatingSystemCapability; }

  protected:

    /// called by applyScene to load channel values from a scene.
    /// @param aScene the scene to load channel values from
    /// @note Scenes don't have 1:1 representation of all channel values for footprint and logic reasons, so this method
    ///   is implemented in the specific behaviours according to the scene layout for that behaviour.
    virtual void loadChannelsFromScene(DsScenePtr aScene) P44_OVERRIDE;

    /// called by captureScene to save channel values to a scene.
    /// @param aScene the scene to save channel values to
    /// @note Scenes don't have 1:1 representation of all channel values for footprint and logic reasons, so this method
    ///   is implemented in the specific behaviours according to the scene layout for that behaviour.
    /// @note call markDirty on aScene in case it is changed (otherwise captured values will not be saved)
    virtual void saveChannelsToScene(DsScenePtr aScene) P44_OVERRIDE;

    // property access implementation for descriptor/settings/states
    virtual int numDescProps() P44_OVERRIDE;
    virtual const PropertyDescriptorPtr getDescDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual int numSettingsProps() P44_OVERRIDE;
    virtual const PropertyDescriptorPtr getSettingsDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    // combined field access for all types of properties
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

    // persistence implementation
    enum {
      outputflag_climateControlIdle = inherited::outputflag_nextflag<<0,
      outputflag_nextflag = inherited::outputflag_nextflag<<1
    };
    virtual const char *tableName() P44_OVERRIDE;
    virtual size_t numFieldDefs() P44_OVERRIDE;
    virtual const FieldDefinition *getFieldDef(size_t aIndex) P44_OVERRIDE;
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP) P44_OVERRIDE;
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags) P44_OVERRIDE;


  };

  typedef boost::intrusive_ptr<ClimateControlBehaviour> ClimateControlBehaviourPtr;

} // namespace p44

#endif /* defined(__p44vdc__climatecontrolbehaviour__) */
