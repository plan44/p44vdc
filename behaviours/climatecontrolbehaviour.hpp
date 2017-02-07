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

#include "device.hpp"
#include "outputbehaviour.hpp"
#include "simplescene.hpp"

using namespace std;

namespace p44 {

  class HeatingLevelChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;

  public:
    HeatingLevelChannel(OutputBehaviour &aOutput) : inherited(aOutput) { resolution = 1; /* 1% of full scale */ };

    virtual DsChannelType getChannelType() { return channeltype_heatingLevel; };
    virtual const char *getName() { return "heatingLevel"; };
    virtual double getMin() { return 0; }; // heating is 0..100 (cooling would be -100..0)
    virtual double getMax() { return 100; };
    virtual double getDimPerMS() { return 100/FULL_SCALE_DIM_TIME_MS; }; // 7 seconds full scale
    
  };


  class AirflowLevelChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;

  public:
    AirflowLevelChannel(OutputBehaviour &aOutput) : inherited(aOutput) { resolution = 1; /* 1% of full scale */ };

    virtual DsChannelType getChannelType() { return channeltype_airflowLevel; };
    virtual const char *getName() { return "airflowLevel"; };
    virtual double getMin() { return 0; }; // fan level is 0..100 (automatic mode with upper limit is -100..0)
    virtual double getMax() { return 100; };
    virtual double getDimPerMS() { return 100/FULL_SCALE_DIM_TIME_MS; }; // 7 seconds full scale

  };





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




  typedef enum {
    climatedevice_heatingvalve,
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

    /// defines how "heatingLevel" is applied to the output
    VdcHeatingSystemCapability heatingSystemCapability;

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


  public:


    ClimateControlBehaviour(Device &aDevice, ClimateDeviceKind aKind);

    /// device type identifier
    /// @return constant identifier for this type of behaviour
    virtual const char *behaviourTypeIdentifier() { return "climatecontrol"; };

    /// @name interface towards actual device hardware (or simulation)
    /// @{

    /// @return true if device should be in idle mode
    bool isClimateControlIdle() { return climateControlIdle; };

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
    virtual Tristate hasModelFeature(DsModelFeatures aFeatureIndex);

    /// Process a named control value. The type, group membership and settings of the device determine if at all,
    /// and if, how the value affects physical outputs of the device or general device operation
    /// @note if this method adjusts channel values, it must not directly update the hardware, but just
    ///   prepare channel values such that these can be applied using requestApplyingChannels().
    /// @param aName the name of the control value, which describes the purpose
    /// @param aValue the control value to process
    /// @note base class by default forwards the control value to all of its output behaviours.
    /// @return true if value processing caused channel changes so channel values should be applied
    virtual bool processControlValue(const string &aName, double aValue);

    /// apply scene to output channels
    /// @param aScene the scene to apply to output channels
    /// @return true if apply is complete, i.e. everything ready to apply to hardware outputs.
    ///   false if scene cannot yet be applied to hardware, and/or will be performed later/separately
    /// @note this derived class' applyScene only implements special hard-wired behaviour specific scenes,
    ///   basic scene apply functionality is provided by base class' implementation already.
    virtual bool applyScene(DsScenePtr aScene);

    /// @}

    /// description of object, mainly for debug and logging
    /// @return textual description of object, may contain LFs
    virtual string description();

    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc();

  protected:

    // property access implementation for descriptor/settings/states
    virtual int numSettingsProps();
    virtual const PropertyDescriptorPtr getSettingsDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor);
    // combined field access for all types of properties
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);

    // persistence implementation
    enum {
      outputflag_climateControlIdle = inherited::outputflag_nextflag<<0,
      outputflag_nextflag = inherited::outputflag_nextflag<<1
    };
    virtual const char *tableName();
    virtual size_t numFieldDefs();
    virtual const FieldDefinition *getFieldDef(size_t aIndex);
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP);
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags);


  };

  typedef boost::intrusive_ptr<ClimateControlBehaviour> ClimateControlBehaviourPtr;

} // namespace p44

#endif /* defined(__p44vdc__climatecontrolbehaviour__) */
