//
//  Copyright (c) 2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__ventilationbehaviour__
#define __p44vdc__ventilationbehaviour__

#include "device.hpp"
#include "outputbehaviour.hpp"
#include "simplescene.hpp"

using namespace std;

namespace p44 {


  class AirflowIntensityChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;

  public:
    AirflowIntensityChannel(OutputBehaviour &aOutput) : inherited(aOutput) { resolution = 1; /* 1% of full scale */ };

    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_airflow_intensity; };
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_percent, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "airflow intensity"; };
    virtual double getMin() P44_OVERRIDE { return 0; }; // intensity level is 0..100 in % of device's available range
    virtual double getMax() P44_OVERRIDE { return 100; };
  };
  typedef boost::intrusive_ptr<AirflowIntensityChannel> AirflowIntensityChannelPtr;


  class AirflowDirectionChannel : public IndexChannel
  {
    typedef IndexChannel inherited;

  public:
    AirflowDirectionChannel(OutputBehaviour &aOutput) : inherited(aOutput) { setNumIndices(numDsVentilationDirectionStates); }; ///< see DsVentilationDirectionState enum

    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_airflow_direction; }; ///< the dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_none, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "airflow direction"; };
  };
  typedef boost::intrusive_ptr<AirflowDirectionChannel> AirflowDirectionChannelPtr;


  class LouverPositionChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;

  public:
    LouverPositionChannel(OutputBehaviour &aOutput) : inherited(aOutput) { resolution = 1; /* 1% of full scale */ };

    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_airflow_louver_position; };
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_percent, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "louver position"; };
    virtual double getMin() P44_OVERRIDE { return 0; }; // intensity level is 0..100 in % of device's available range
    virtual double getMax() P44_OVERRIDE { return 100; };
  };
  typedef boost::intrusive_ptr<LouverPositionChannel> LouverPositionChannelPtr;





  /// A ventilation scene
  class VentilationScene : public DsScene
  {
    typedef DsScene inherited;
    friend class VentilationBehaviour;

  public:
    VentilationScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo); ///< constructor, sets values according to dS specs' default values

    /// @name SimpleScene specific values
    /// @{

    double airflowIntensity; ///< main scene value, airflow
    DsVentilationAirflowDirection airflowDirection; ///< airflow direction
    double louverPosition; ///< louver position

    /// @}

    /// Set default scene values for a specified scene number
    /// @param aSceneNo the scene number to set default values
    virtual void setDefaultSceneValues(SceneNo aSceneNo) P44_OVERRIDE;

    // scene values implementation
    virtual double sceneValue(size_t aOutputIndex) P44_OVERRIDE;
    virtual void setSceneValue(size_t aOutputIndex, double aValue) P44_OVERRIDE;

  protected:

    // persistence implementation
    virtual const char *tableName() P44_OVERRIDE;
    virtual size_t numFieldDefs() P44_OVERRIDE;
    virtual const FieldDefinition *getFieldDef(size_t aIndex) P44_OVERRIDE;
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP) P44_OVERRIDE;
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags) P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<VentilationScene> VentilationScenePtr;



  /// the persistent parameters of a ventilation scene device (including scene table)
  class VentilationDeviceSettings : public SceneDeviceSettings
  {
    typedef SceneDeviceSettings inherited;

  public:
    VentilationDeviceSettings(Device &aDevice);

    /// factory method to create the correct subclass type of DsScene
    /// @param aSceneNo the scene number to create a scene object for.
    /// @note setDefaultSceneValues() must be called to set default scene values
    virtual DsScenePtr newDefaultScene(SceneNo aSceneNo);
    
  };


  typedef enum {
    ventilationdevice_ventilation,
    ventilationdevice_recirculation
  } VentilationDeviceKind;


  /// Implements the behaviour of ventilation control outputs
  class VentilationBehaviour : public OutputBehaviour
  {
    typedef OutputBehaviour inherited;
    friend class VentilationScene;

  protected:

    /// @name hardware derived parameters (constant during operation)
    /// @{

    /// kind of climate device
    VentilationDeviceKind ventilationDeviceKind;

    /// @}


    /// @name persistent settings
    /// @{

    /// @}


    /// @name internal volatile state
    /// @{

    bool airflowIntensity_automatic; ///< if set, airflow intensity is in automatic mode
    bool louverPosition_automatic; ///< if set, louver position is in automatic (swing) mode

    /// @}


  public:

    VentilationBehaviour(Device &aDevice, VentilationDeviceKind aKind);

    /// device type identifier
    /// @return constant identifier for this type of behaviour
    virtual const char *behaviourTypeIdentifier() P44_OVERRIDE { return "ventilation"; };

    /// the volume channel
    AirflowIntensityChannelPtr airflowIntensity;
    /// the power state channel
    AirflowDirectionChannelPtr airflowDirection;
    /// the content source channel
    LouverPositionChannelPtr louverPosition;



    /// @name interface towards actual device hardware (or simulation)
    /// @{

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

  };

  typedef boost::intrusive_ptr<VentilationBehaviour> VentilationBehaviourPtr;

} // namespace p44

#endif /* defined(__p44vdc__ventilationbehaviour__) */
