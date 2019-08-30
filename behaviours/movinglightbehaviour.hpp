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

#ifndef __p44vdc__movinglightbehaviour__
#define __p44vdc__movinglightbehaviour__

#include "device.hpp"
#include "dsscene.hpp"
#include "colorlightbehaviour.hpp"

using namespace std;

namespace p44 {

  // MARK: - Moving color light

  class VPosChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;

  public:
    VPosChannel(OutputBehaviour &aOutput) : inherited(aOutput, "vPos") { resolution = 0.01; cachedChannelValue = 50; };

    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_p44_position_v; }; ///< the dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_percent, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "vertical position"; };
    virtual double getMin() P44_OVERRIDE { return 0; }; // position goes from 0 to 100%
    virtual double getMax() P44_OVERRIDE { return 100; };
  };


  class HPosChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;

  public:
    HPosChannel(OutputBehaviour &aOutput) : inherited(aOutput, "hPos") { resolution = 0.01; cachedChannelValue = 50; };

    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_p44_position_h; }; ///< the dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_percent, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "horizontal position"; };
    virtual double getMin() P44_OVERRIDE { return 0; }; // position goes from 0 to 100%
    virtual double getMax() P44_OVERRIDE { return 100; };
  };



  class MovingLightScene : public ColorLightScene
  {
    typedef ColorLightScene inherited;
    
  public:
    MovingLightScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo); ///< constructor, sets values according to dS specs' default values

    /// @name moving light scene specific values
    /// @{

    double hPos; ///< horizontal position
    double vPos; ///< vertical position

    /// @}

    /// Set default scene values for a specified scene number
    /// @param aSceneNo the scene number to set default values
    virtual void setDefaultSceneValues(SceneNo aSceneNo);

    // scene values implementation
    virtual double sceneValue(int aChannelIndex);
    virtual void setSceneValue(int aChannelIndex, double aValue);

  protected:

    // persistence implementation
    virtual const char *tableName();
    virtual size_t numFieldDefs();
    virtual const FieldDefinition *getFieldDef(size_t aIndex);
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP);
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags);

  };
  typedef boost::intrusive_ptr<MovingLightScene> MovingLightScenePtr;



  /// the persistent parameters of a light scene device (including scene table)
  class MovingLightDeviceSettings : public ColorLightDeviceSettings
  {
    typedef ColorLightDeviceSettings inherited;

  public:
    MovingLightDeviceSettings(Device &aDevice);

    /// factory method to create the correct subclass type of DsScene
    /// @param aSceneNo the scene number to create a scene object for.
    /// @note setDefaultSceneValues() must be called to set default scene values
    virtual DsScenePtr newDefaultScene(SceneNo aSceneNo);

  };


  class MovingLightBehaviour : public RGBColorLightBehaviour
  {
    typedef RGBColorLightBehaviour inherited;

  public:

    /// @name channels
    /// @{
    ChannelBehaviourPtr horizontalPosition;
    ChannelBehaviourPtr verticalPosition;
    /// @}


    MovingLightBehaviour(Device &aDevice, bool aCtOnly);

    /// check for presence of model feature (flag in dSS visibility matrix)
    /// @param aFeatureIndex the feature to check for
    /// @return yes if this output behaviour has the feature, no if (explicitly) not, undefined if asked entity does not know
    virtual Tristate hasModelFeature(DsModelFeatures aFeatureIndex);

    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc();

    /// step through transitions
    /// @param aStepSize how much to step. Default is zero and means starting transition
    /// @return true if there's another step to take, false if end of transition already reached
    bool positionTransitionStep(double aStepSize = 0);

    /// mark horizontal and vertical position values applied
    void appliedPosition();

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
  typedef boost::intrusive_ptr<MovingLightBehaviour> MovingLightBehaviourPtr;


  // MARK: - Feature spotlight with size, rotation, gradients

  #define DEFAULT_ZOOM 50 // half size = fits into area
  #define DEFAULT_BRIGHTNESS_GRADIENT -30  // dimming down a bit towards the edges
  #define DEFAULT_HUE_GRADIENT 0
  #define DEFAULT_SATURATION_GRADIENT 0
  #define DEFAULT_FEATURE_MODE 0x222222  // linear, oscillating, radial, clipped


  class VZoomChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;

  public:
    VZoomChannel(OutputBehaviour &aOutput) : inherited(aOutput, "vZoom") { resolution = 0.01; cachedChannelValue = DEFAULT_ZOOM; };

    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_p44_zoom_v; }; ///< the dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_percent, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "vertical size/zoom"; };
    virtual double getMin() P44_OVERRIDE { return 0; }; // zoom goes from 0 to 100%
    virtual double getMax() P44_OVERRIDE { return 100; };
  };


  class HZoomChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;

  public:
    HZoomChannel(OutputBehaviour &aOutput) : inherited(aOutput, "hZoom") { resolution = 0.01; cachedChannelValue = DEFAULT_ZOOM; };

    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_p44_zoom_h; }; ///< the dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_percent, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "horizonal size/zoom"; };
    virtual double getMin() P44_OVERRIDE { return 0; }; // zoom goes from 0 to 100%
    virtual double getMax() P44_OVERRIDE { return 100; };
  };


  class RotationChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;

  public:
    RotationChannel(OutputBehaviour &aOutput) : inherited(aOutput, "rotation") { resolution = 1; };

    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_p44_rotation; }; ///< the dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_degree, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "rotation"; };
    virtual double getMin() P44_OVERRIDE { return 0; }; ///< rotation goes from 0 to 360 degrees, with 0 and 360 meaning the same.
    virtual double getMax() P44_OVERRIDE { return 360; }; ///< Note the max value will never be actually reached, as it wraps around to min
    virtual bool wrapsAround() P44_OVERRIDE { return true; }; ///< rotation wraps around, meaning max is considered identical to min
  };


  class GradientChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;

  public:
    GradientChannel(OutputBehaviour &aOutput, const string aChannelId) : inherited(aOutput, aChannelId) { resolution = 0.1; /* arbitrary */ };
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_percent, unitScaling_1); };
    virtual double getMin() P44_OVERRIDE { return -100; }; ///< rotation goes from 0 to 360 degrees, with 0 and 360 meaning the same.
    virtual double getMax() P44_OVERRIDE { return 100; }; ///< Note the max value will never be actually reached, as it wraps around to min
  };


  class BrightnessGradientChannel : public GradientChannel
  {
    typedef GradientChannel inherited;

  public:
    BrightnessGradientChannel(OutputBehaviour &aOutput) : inherited(aOutput, "brightnessGradient") { cachedChannelValue = DEFAULT_BRIGHTNESS_GRADIENT; };
    virtual const char *getName() P44_OVERRIDE { return "brightness gradient"; };
    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_p44_brightness_gradient; }; ///< the dS channel type
  };

  class HueGradientChannel : public GradientChannel
  {
    typedef GradientChannel inherited;

  public:
    HueGradientChannel(OutputBehaviour &aOutput) : inherited(aOutput, "hueGradient") { cachedChannelValue = DEFAULT_HUE_GRADIENT; };
    virtual const char *getName() P44_OVERRIDE { return "hue gradient"; };
    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_p44_hue_gradient; }; ///< the dS channel type
  };

  class SaturationGradientChannel : public GradientChannel
  {
    typedef GradientChannel inherited;

  public:
    SaturationGradientChannel(OutputBehaviour &aOutput) : inherited(aOutput, "saturationGradient") { cachedChannelValue = DEFAULT_SATURATION_GRADIENT; };
    virtual const char *getName() P44_OVERRIDE { return "saturation gradient"; };
    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_p44_saturation_gradient; }; ///< the dS channel type
  };


  class FeatureModeChannel : public DialChannel
  {
    typedef DialChannel inherited;

  public:
    FeatureModeChannel(OutputBehaviour &aOutput) : inherited(aOutput, "featureMode") { setMax(0x3FFFFFF); cachedChannelValue = DEFAULT_FEATURE_MODE; };
    virtual const char *getName() P44_OVERRIDE { return "feature mode"; };
    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_p44_feature_mode; }; ///< the dS channel type
  };




  class FeatureLightScene : public MovingLightScene
  {
    typedef MovingLightScene inherited;

  public:
    FeatureLightScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo); ///< constructor, sets values according to dS specs' default values

    /// @name moving light scene specific values
    /// @{

    double hZoom; ///< horizontal zoom
    double vZoom; ///< vertical zoom
    double rotation; ///< vertical zoom
    double brightnessGradient; ///< brightness gradient
    double hueGradient; ///< hue gradient
    double saturationGradient; ///< saturation gradient
    uint32_t featureMode; ///< feature control (gradient curve modes etc.)

    /// @}

    /// Set default scene values for a specified scene number
    /// @param aSceneNo the scene number to set default values
    virtual void setDefaultSceneValues(SceneNo aSceneNo);

    // scene values implementation
    virtual double sceneValue(int aChannelIndex);
    virtual void setSceneValue(int aChannelIndex, double aValue);

  protected:

    // persistence implementation
    virtual const char *tableName();
    virtual size_t numFieldDefs();
    virtual const FieldDefinition *getFieldDef(size_t aIndex);
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP);
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags);

  };
  typedef boost::intrusive_ptr<FeatureLightScene> FeatureLightScenePtr;



  class FeatureLightDeviceSettings : public MovingLightDeviceSettings
  {
    typedef MovingLightDeviceSettings inherited;

  public:
    FeatureLightDeviceSettings(Device &aDevice);

    /// factory method to create the correct subclass type of DsScene
    /// @param aSceneNo the scene number to create a scene object for.
    /// @note setDefaultSceneValues() must be called to set default scene values
    virtual DsScenePtr newDefaultScene(SceneNo aSceneNo);

  };




  class FeatureLightBehaviour : public MovingLightBehaviour
  {
    typedef MovingLightBehaviour inherited;

  public:

    /// @name channels
    /// @{
    ChannelBehaviourPtr horizontalZoom;
    ChannelBehaviourPtr verticalZoom;
    ChannelBehaviourPtr rotation;
    ChannelBehaviourPtr brightnessGradient;
    ChannelBehaviourPtr hueGradient;
    ChannelBehaviourPtr saturationGradient;
    ChannelBehaviourPtr featureMode;
    /// @}


    FeatureLightBehaviour(Device &aDevice, bool aCtOnly);

    /// check for presence of model feature (flag in dSS visibility matrix)
    /// @param aFeatureIndex the feature to check for
    /// @return yes if this output behaviour has the feature, no if (explicitly) not, undefined if asked entity does not know
    virtual Tristate hasModelFeature(DsModelFeatures aFeatureIndex);

    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc();

    /// step through transitions
    /// @param aStepSize how much to step. Default is zero and means starting transition
    /// @return true if there's another step to take, false if end of transition already reached
    bool featureTransitionStep(double aStepSize = 0);

    /// mark horizontal and vertical position values applied
    void appliedFeatures();

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
  typedef boost::intrusive_ptr<FeatureLightBehaviour> FeatureLightBehaviourPtr;




} // namespace p44

#endif /* defined(__p44vdc__movinglightbehaviour__) */
