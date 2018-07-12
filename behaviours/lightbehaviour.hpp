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

#ifndef __p44vdc__lightbehaviour__
#define __p44vdc__lightbehaviour__

#include "device.hpp"
#include "simplescene.hpp"
#include "outputbehaviour.hpp"

using namespace std;

namespace p44 {

  typedef uint8_t DimmingTime; ///< dimming time with bits 0..3 = mantissa in 6.666mS, bits 4..7 = exponent (# of bits to shift left)
  typedef double Brightness; ///< 0..100% brightness

  #define DS_BRIGHTNESS_STEP (100.0/255.0) ///< default step size for brightness (from historical 0..255 8bit dS brightness)

  class BrightnessChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;
    double minDim;

  public:
    BrightnessChannel(OutputBehaviour &aOutput) : inherited(aOutput, "brightness")
    {
      resolution = DS_BRIGHTNESS_STEP; // light defaults to historic dS scale resolution
      minDim = getMin()+resolution; // min dimming level defaults to one resolution step above zero
    };

    void setDimMin(double aMinDim) { minDim = aMinDim; };

    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_brightness; }; ///< the dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_percent, unitScaling_1); };
    virtual const char *getName() P44_OVERRIDE { return "brightness"; };
    virtual double getMin() P44_OVERRIDE { return 0; }; // dS brightness goes from 0 to 100%
    virtual double getMax() P44_OVERRIDE { return 100; };
    virtual double getDimPerMS() P44_OVERRIDE { return 11.0/256*100/300; }; // dimming is 11 steps(1/256) per 300mS (as per ds-light.pdf specification) = 255/11*300 = 7 seconds full scale
    virtual double getMinDim() P44_OVERRIDE { return minDim; };

  };
  typedef boost::intrusive_ptr<BrightnessChannel> BrightnessChannelPtr;



  /// A concrete class implementing the Scene object for a simple (single channel = brightness) light device
  /// @note subclasses can implement more parameters, like for exampe ColorLightScene for color lights.
  class LightScene : public SimpleScene
  {
    typedef SimpleScene inherited;

  public:
    LightScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo) : inherited(aSceneDeviceSettings, aSceneNo) {}; ///< constructor, sets values according to dS specs' default values

  };
  typedef boost::intrusive_ptr<LightScene> LightScenePtr;



  /// the persistent parameters of a light scene device (including scene table)
  /// @note subclasses can implement more parameters, like for example ColorLightDeviceSettings for color lights.
  class LightDeviceSettings : public SceneDeviceSettings
  {
    typedef SceneDeviceSettings inherited;

  public:
    LightDeviceSettings(Device &aDevice);

    /// factory method to create the correct subclass type of DsScene
    /// @param aSceneNo the scene number to create a scene object for.
    /// @note setDefaultSceneValues() must be called to set default scene values
    virtual DsScenePtr newDefaultScene(SceneNo aSceneNo);

  };


  /// Implements the behaviour of a digitalSTROM Light device, such as maintaining the logical brightness,
  /// dimming and alert (blinking) functions.
  class LightBehaviour : public OutputBehaviour
  {
    typedef OutputBehaviour inherited;


    /// @name hardware derived parameters (constant during operation)
    /// @{
    /// @}


    /// @name persistent settings
    /// @{
    Brightness onThreshold; ///< if !isDimmable, output will be on when output value is >= the threshold
    DimmingTime dimTimeUp[3]; ///< dimming up time
    DimmingTime dimTimeDown[3]; ///< dimming down time
    double dimCurveExp; ///< exponent for logarithmic curve (1=linear, 2=quadratic, 3=cubic, ...)
    /// @}


    /// @name internal volatile state
    /// @{
    MLTicket blinkTicket; ///< when blinking
    SimpleCB blinkDoneHandler; ///< called when blinking done
    LightScenePtr blinkRestoreScene; ///< scene to restore
    MLTicket fadeDownTicket; ///< for slow fading operations
    bool hardwareHasSetMinDim; ///< if set, hardware has set minDim (prevents loading from DB)
    /// @}


  public:
    LightBehaviour(Device &aDevice);

    /// device type identifier
    /// @return constant identifier for this type of behaviour
    virtual const char *behaviourTypeIdentifier() P44_OVERRIDE P44_FINAL { return "light"; };

    /// the brightness channel
    BrightnessChannelPtr brightness;

    /// @name interface towards actual device hardware (or simulation)
    /// @{

    /// @return true if device is dimmable
    bool isDimmable() { return outputFunction!=outputFunction_switch && actualOutputMode()!=outputmode_binary; };

    /// initialize behaviour with actual device's brightness parameters
    /// @param aMin minimal brightness that can be set
    /// @note brightness: 0..100%, linear brightness as perceived by humans (half value = half brightness)
    void initMinBrightness(Brightness aMin);

    /// Apply output-mode specific output value transformation
    /// @param aChannelValue channel value
    /// @param aChannelIndex channel index (might be different transformation depending on type)
    /// @return output value limited/transformed according to outputMode
    /// @note subclasses might implement behaviour-specific output transformations
    virtual double outputValueAccordingToMode(double aChannelValue, int aChannelIndex) P44_OVERRIDE;

    /// return the brightness to be applied to hardware
    /// @param aFinal if set, the final value (not transitional) will be used
    /// @return brightness
    /// @note this is to allow lights to have switching behaviour - when brightness channel value is
    ///   above onThreshold, brightnessForHardware() will return the max channel value and 0 otherwise.
    Brightness brightnessForHardware(bool aFinal = false);

    /// sync channel brightness from actual hardware value
    /// @param aBrightness current brightness value read back from hardware
    /// @note this wraps the dimmable/switch functionality (does not change channel value when onThreshold
    ///   condition is already met to allow saving virtual brightness to scenes)
    void syncBrightnessFromHardware(Brightness aBrightness, bool aAlwaysSync=false);

    /// Check if brightness change needs to be applied to hardware
    /// @return true if brightness has pending change
    bool brightnessNeedsApplying() { return brightness->needsApplying(); };

    /// step through transitions
    /// @param aStepSize how much to step. Default is zero and means starting transition
    /// @return true if there's another step to take, false if end of transition already reached
    bool brightnessTransitionStep(double aStepSize = 0) { return brightness->transitionStep(aStepSize); };

    /// wrapper to confirm having applied brightness
    void brightnessApplied() { brightness->channelValueApplied(); };

    /// wrapper to get brightness' transition time
    MLMicroSeconds transitionTimeToNewBrightness() { return brightness->transitionTimeToNewValue(); };

    /// @}


    /// @name interaction with digitalSTROM system
    /// @{

    /// check for presence of model feature (flag in dSS visibility matrix)
    /// @param aFeatureIndex the feature to check for
    /// @return yes if this output behaviour has the feature, no if (explicitly) not, undefined if asked entity does not know
    virtual Tristate hasModelFeature(DsModelFeatures aFeatureIndex) P44_OVERRIDE;

    /// apply scene to output channels
    /// @param aScene the scene to apply to output channels
    /// @param aSceneCmd This will be used instead of the scenecommand stored in the scene.
    /// @return true if apply is complete, i.e. everything ready to apply to hardware outputs.
    ///   false if scene cannot yet be applied to hardware, and will be performed later
    /// @note this derived class' performApplySceneToChannels only implements special hard-wired behaviour specific scenes
    ///   basic scene apply functionality is provided by base class' implementation already.
    virtual bool performApplySceneToChannels(DsScenePtr aScene, SceneCmd aSceneCmd) P44_OVERRIDE;

    /// perform special scene actions (like flashing) which are independent of dontCare flag.
    /// @param aScene the scene that was called (if not dontCare, performApplySceneToChannels() has already been called)
    /// @param aDoneCB will be called when scene actions have completed (but not necessarily when stopped by stopSceneActions())
    virtual void performSceneActions(DsScenePtr aScene, SimpleCB aDoneCB) P44_OVERRIDE;

    /// will be called to stop all ongoing actions before next callScene etc. is issued.
    /// @note this must stop all ongoing actions such that applying another scene or action right afterwards
    ///   cannot mess up things.
    virtual void stopSceneActions() P44_OVERRIDE;

    /// switch on at minimum brightness if not already on (needed for callSceneMin), only relevant for lights
    /// @param aScene the scene to take all other channel values from, except brightness which is set to light's minDim
    virtual void onAtMinBrightness(DsScenePtr aScene) P44_OVERRIDE;

    /// check if this channel of this device is allowed to dim now (for lights, this will prevent dimming lights that are off)
    /// @param aChannel the channel to check
    virtual bool canDim(ChannelBehaviourPtr aChannel) P44_OVERRIDE;

    /// identify the device to the user in a behaviour-specific way
    /// @note implemented as blinking for LightBehaviour
    virtual void identifyToUser() P44_OVERRIDE;

    /// @}


    /// @name services for implementing functionality
    /// @{

    /// blink the light (for identifying it, or alerting special system states)
    /// @param aDuration how long the light should blink
    /// @param aParamScene if not NULL, this scene might provide parameters for blinking
    /// @param aDoneCB will be called when scene actions have completed
    /// @param aBlinkPeriod how fast the blinking should be
    /// @param aOnRatioPercent how many percents of aBlinkPeriod the indicator should be on
    void blink(MLMicroSeconds aDuration, LightScenePtr aParamScene, SimpleCB aDoneCB, MLMicroSeconds aBlinkPeriod = 600*MilliSecond, int aOnRatioPercent = 50);

    /// stop blinking immediately
    virtual void stopBlink();

    /// get transition time in microseconds from given scene effect
    /// @param aEffect the scene effect
    /// @param aEffectParam parameter for the effect (standard dS scenes do not have it)
    /// @param aDimUp true when dimming up, false when dimming down
    MLMicroSeconds transitionTimeFromSceneEffect(VdcSceneEffect aEffect, uint32_t aEffectParam, bool aDimUp);


    /// get PWM value for brightness (from brightness channel) according to dim curve
    /// @param aBrightness brightness to convert to PWM value
    /// @param aMaxPWM max PWM duty cycle value
    /// @return the PWM value (from 0..aMaxPWM) corresponding to aBrightness
    double brightnessToPWM(Brightness aBrightness, double aMaxPWM);

    /// set PWM value from lamp (to update brightness channel) according to dim curve
    /// @param aPWM PWM value to be converted back to brightness
    /// @param aMaxPWM max PWM duty cycle value
    /// @return the brightness value corresponding to aPWM (which must be 0..aMaxPWM)
    Brightness PWMToBrightness(double aPWM, double aMaxPWM);


    /// @}


    /// description of object, mainly for debug and logging
    /// @return textual description of object, may contain LFs
    virtual string description() P44_OVERRIDE;

    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc() P44_OVERRIDE;

  protected:

    /// called by performApplySceneToChannels() to load channel values from a scene.
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
    virtual int numSettingsProps() P44_OVERRIDE;
    virtual const PropertyDescriptorPtr getSettingsDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    // combined field access for all types of properties
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

    // persistence implementation
    virtual const char *tableName() P44_OVERRIDE;
    virtual size_t numFieldDefs() P44_OVERRIDE;
    virtual const FieldDefinition *getFieldDef(size_t aIndex) P44_OVERRIDE;
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP) P44_OVERRIDE;
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags) P44_OVERRIDE;

  private:

    void beforeBlinkStateSavedHandler(MLMicroSeconds aDuration, LightScenePtr aParamScene, MLMicroSeconds aBlinkPeriod, int aOnRatioPercent);
    void blinkHandler(MLMicroSeconds aEndTime, bool aState, MLMicroSeconds aOnTime, MLMicroSeconds aOffTime);

  };

  typedef boost::intrusive_ptr<LightBehaviour> LightBehaviourPtr;

} // namespace p44

#endif /* defined(__p44vdc__lightbehaviour__) */
