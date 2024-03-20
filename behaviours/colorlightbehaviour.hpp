//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__colorlightbehaviour__
#define __p44vdc__colorlightbehaviour__

#include "device.hpp"
#include "dsscene.hpp"
#include "lightbehaviour.hpp"
#include "colorutils.hpp"

using namespace std;

namespace p44 {


  typedef enum {
    colorLightModeNone, ///< no color information stored, only brightness
    colorLightModeHueSaturation, ///< "hs" - hue & saturation
    colorLightModeXY, ///< "xy" - CIE color space coordinates
    colorLightModeCt, ///< "ct" - Mired color temperature: 153 (6500K) to 500 (2000K)
    colorLightModeRGBWA, ///< direct RGBWA channels, not directly supported at colorlightbehaviour level, but internally in some devices
  } ColorLightMode;



  class ColorChannel : public ChannelBehaviour
  {
    typedef ChannelBehaviour inherited;

  public:

    ColorChannel(OutputBehaviour &aOutput, const string aChannelId) : inherited(aOutput, aChannelId) {};

    virtual ColorLightMode colorMode() = 0;

    /// get current value of this channel - and calculate it if it is not set in the device, but must be calculated from other channels
    virtual double getChannelValueCalculated(bool aTransitional);

  };


  class HueChannel : public ColorChannel
  {
    typedef ColorChannel inherited;

  public:
    explicit HueChannel(OutputBehaviour &aOutput) : inherited(aOutput, "hue") { mResolution = 0.1; /* 0.1 degree */ };

    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_hue; }; ///< the dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_degree, unitScaling_1); };
    virtual ColorLightMode colorMode() P44_OVERRIDE { return colorLightModeHueSaturation; };
    virtual const char* getName() const P44_OVERRIDE { return "hue"; };
    virtual double getMin() P44_OVERRIDE { return 0; }; ///< hue goes from 0 to 360 degrees, with 0 and 360 meaning the same.
    virtual double getMax() P44_OVERRIDE { return 360; }; ///< Note the max value will never be actually reached, as it wraps around to min
    virtual bool wrapsAround() P44_OVERRIDE { return true; }; ///< hue wraps around, meaning max is considered identical to min
  };


  class SaturationChannel : public ColorChannel
  {
    typedef ColorChannel inherited;

  public:
    SaturationChannel(OutputBehaviour &aOutput) : inherited(aOutput, "saturation") { mResolution = 0.1; /* 0.1 percent */ };

    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_saturation; }; ///< the dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_percent, unitScaling_1); };
    virtual ColorLightMode colorMode() P44_OVERRIDE { return colorLightModeHueSaturation; };
    virtual const char* getName() const P44_OVERRIDE { return "saturation"; };
    virtual double getMin() P44_OVERRIDE { return 0; }; // saturation goes from 0 to 100 percent
    virtual double getMax() P44_OVERRIDE { return 100; };
  };


  class ColorTempChannel : public ColorChannel
  {
    typedef ColorChannel inherited;

  public:
    ColorTempChannel(OutputBehaviour &aOutput) : inherited(aOutput, "colortemp") { mResolution = 1; /* 1 mired */ };

    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_colortemp; }; ///< the dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(ValueUnit_mired, unitScaling_1); };
    virtual ColorLightMode colorMode() P44_OVERRIDE { return colorLightModeCt; };
    virtual const char* getName() const P44_OVERRIDE { return "color temperature"; };
    virtual double getMin() P44_OVERRIDE { return 100; }; // CT goes from 100 to 1000 mired (10000K to 1000K)
    virtual double getMax() P44_OVERRIDE { return 1000; };
  };


  class CieXChannel : public ColorChannel
  {
    typedef ColorChannel inherited;

  public:
    CieXChannel(OutputBehaviour &aOutput) : inherited(aOutput, "x") { mResolution = 0.01; /* 1% of full scale */ };

    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_cie_x; }; ///< the dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_none, unitScaling_1); };
    virtual ColorLightMode colorMode() P44_OVERRIDE { return colorLightModeXY; };
    virtual const char* getName() const P44_OVERRIDE { return "CIE x"; };
    virtual double getMin() P44_OVERRIDE { return 0; }; // CIE x and y have 0..1 range
    virtual double getMax() P44_OVERRIDE { return 1; };
  };


  class CieYChannel : public ColorChannel
  {
    typedef ColorChannel inherited;

  public:
    CieYChannel(OutputBehaviour &aOutput) : inherited(aOutput, "y") { mResolution = 0.01; /* 1% of full scale */ };

    virtual DsChannelType getChannelType() P44_OVERRIDE { return channeltype_cie_y; }; ///< the dS channel type
    virtual ValueUnit getChannelUnit() P44_OVERRIDE { return VALUE_UNIT(valueUnit_none, unitScaling_1); };
    virtual ColorLightMode colorMode() P44_OVERRIDE { return colorLightModeXY; };
    virtual const char* getName() const P44_OVERRIDE { return "CIE y"; };
    virtual double getMin() P44_OVERRIDE { return 0; }; // CIE x and y have 0..1 range
    virtual double getMax() P44_OVERRIDE { return 1; };
  };




  class ColorLightScene : public LightScene
  {
    typedef LightScene inherited;
    
  public:
    ColorLightScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo); ///< constructor, sets values according to dS specs' default values

    /// @name color light scene specific values
    /// @{

    ColorLightMode mColorMode; ///< color mode (hue+Saturation or CIE xy or color temperature)
    double mXOrHueOrCt; ///< X or hue or ct, depending on colorMode
    double mYOrSat; ///< Y or saturation, depending on colorMode

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
  typedef boost::intrusive_ptr<ColorLightScene> ColorLightScenePtr;



  /// the persistent parameters of a light scene device (including scene table)
  class ColorLightDeviceSettings : public LightDeviceSettings
  {
    typedef LightDeviceSettings inherited;

  public:
    ColorLightDeviceSettings(Device &aDevice);

    /// factory method to create the correct subclass type of DsScene
    /// @param aSceneNo the scene number to create a scene object for.
    /// @note setDefaultSceneValues() must be called to set default scene values
    virtual DsScenePtr newDefaultScene(SceneNo aSceneNo);

  };


  class ColorLightBehaviour : public LightBehaviour
  {
    typedef LightBehaviour inherited;
    friend class ColorLightScene;

    bool mCtOnly; // if set, behaviour only exposes brightness and colortemperature channels

  public:

    /// @name internal volatile state
    /// @{
    ColorLightMode mColorMode;
    bool mDerivedValuesComplete;
    /// @}


    /// @name channels
    /// @{
    ChannelBehaviourPtr mHue;
    ChannelBehaviourPtr mSaturation;
    ChannelBehaviourPtr mCt;
    ChannelBehaviourPtr mCIEx;
    ChannelBehaviourPtr mCIEy;
    /// @}


    /// Constructor for color and tunable white lights
    /// @param aCtOnly if set, only color temperature is supported (no HSV or CieX/Y)
    ColorLightBehaviour(Device &aDevice, bool aCtOnly);

    /// @name color services for implementing color lights
    /// @{

    /// @return true if light is not full color, but color temperature only
    bool isCtOnly() { return mCtOnly; }

    /// derives the color mode from channel values that need to be applied to hardware
    /// @return true if new mode could be found (which also means that color needs to be applied to HW)
    bool deriveColorMode();

    /// set a specific color mode, if different from current mode missing channel values will be derived
    /// @param aColorMode new color mode requested
    /// @return true if mode actually changed, false if requested mode was already set
    bool setColorMode(ColorLightMode aColorMode);

    /// derives the values for the not-current color representations' channels
    /// by converting between representations
    /// @param aTransitional if set and involved channels are in transition, the transitional values are used for calculating derived values
    void deriveMissingColorChannels(bool aTransitional);

    /// mark Color Light values applied (flags channels applied depending on colormode)
    void appliedColorValues();

    /// initialize a transition or update its progress over time
    /// @param aNow current time, used to calculate progress. Default is 0 and means starting a new transition NOW
    /// @return true if the transition must be updated again, false if end of transition already reached
    bool updateColorTransition(MLMicroSeconds aNow = 0);

    /// @}

    /// check for presence of model feature (flag in dSS visibility matrix)
    /// @param aFeatureIndex the feature to check for
    /// @return yes if this output behaviour has the feature, no if (explicitly) not, undefined if asked entity does not know
    virtual Tristate hasModelFeature(DsModelFeatures aFeatureIndex);

    /// description of object, mainly for debug and logging
    /// @return textual description of object, may contain LFs
    virtual string description();

    /// short (text without LFs!) description of object, mainly for referencing it in log messages
    /// @return textual description of object
    virtual string shortDesc();

    /// @name color services for implementing color lights
    /// @{

    /// get CIEx,y from current color mode (possibly in transition)
    /// @param aCieX will receive CIE X component, 0..1
    /// @param aCieY will receive CIE Y component, 0..1
    /// @return false if values are not available
    bool getCIExy(double &aCieX, double &aCieY, bool aTransitional);

    /// get Color Temperature from current color mode
    /// @param aCT will receive color temperature in mired
    /// @return false if values are not available
    bool getCT(double &aCT, bool aTransitional);

    /// get Hue+Saturation from current color mode
    /// @param aHue will receive hue component, 0..360
    /// @param aSaturation will receive saturation component, 0..100%
    /// @return false if values are not available
    bool getHueSaturation(double &aHue, double &aSaturation, bool aTransitional);

    /// @}

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

    /// utility function, adjusts channel-level dontCare flags to current color mode
    /// @param aColorLightScene the scene to adjust
    /// @param aSetOnly only SET don't care for channels that are not native to the current colormode,
    ///    but do not touch dontCare for channels that are native.
    void adjustChannelDontCareToColorMode(ColorLightScenePtr aColorLightScene, bool aSetOnly = false);

  };

  typedef boost::intrusive_ptr<ColorLightBehaviour> ColorLightBehaviourPtr;



  class RGBColorLightBehaviour : public ColorLightBehaviour
  {
    typedef ColorLightBehaviour inherited;

  public:

    /// @name settings (color calibration)
    /// @{
    Matrix3x3 mCalibration; ///< calibration matrix: [[Xr,Xg,Xb],[Yr,Yg,Yb],[Zr,Zg,Zb]]
    Row3 mWhiteRGB; ///< R,G,B relative intensities that can be replaced by a extra (cold)white channel
    Row3 mAmberRGB; ///< R,G,B relative intensities that can be replaced by a extra amber (warm white) channel
    /// @}

    RGBColorLightBehaviour(Device &aDevice, bool aCtOnly);

    /// @name color services for implementing color lights
    /// @{

    /// get RGB colors (from current channel settings, HSV, CIE, CT + brightness) for applying to lamp
    /// @param aRed,aGreen,aBlue will receive the R,G,B values corresponding to current channels
    /// @param aMax max value for aRed,aGreen,aBlue
    /// @param aNoBrightness if set, RGB is calculated at full brightness
    void getRGB(double &aRed, double &aGreen, double &aBlue, double aMax, bool aNoBrightness, bool aTransitional);

    /// set RGB values from lamp (to update channel values from actual lamp setting)
    /// @param aRed,aGreen,aBlue current R,G,B values to be converted to color channel settings
    /// @param aMax max value for aRed,aGreen,aBlue
    /// @param aNoBrightness do not update brightness channel (when RGB are modulated by a separate brightness)
    void setRGB(double aRed, double aGreen, double aBlue, double aMax, bool aNoBrightness);

    /// get RGBW colors (from current channel settings, HSV, CIE, CT + brightness) for applying to lamp
    /// @param aRed,aGreen,aBlue,aWhite will receive the R,G,B,W values corresponding to current channels
    /// @param aMax max value for aRed,aGreen,aBlue,aWhite
    /// @param aNoBrightness if set, RGBW is calculated at full brightness
    void getRGBW(double &aRed, double &aGreen, double &aBlue, double &aWhite, double aMax, bool aNoBrightness, bool aTransitional);

    /// set RGBW values from lamp (to update channel values from actual lamp setting)
    /// @param aRed,aGreen,aBlue,aWhite current R,G,B,W values to be converted to color channel settings
    /// @param aMax max value for aRed,aGreen,aBlue,aWhite
    /// @param aNoBrightness do not update brightness channel (when RGB are modulated by a separate brightness)
    void setRGBW(double aRed, double aGreen, double aBlue, double aWhite, double aMax, bool aNoBrightness);

    /// get RGBWA colors (from current channel settings, HSV, CIE, CT + brightness) for applying to lamp
    /// @param aRed,aGreen,aBlue,aWhite,aAmber will receive the R,G,B,W,A values corresponding to current channels
    /// @param aMax max value for aRed,aGreen,aBlue,aWhite,aAmber
    /// @param aNoBrightness if set, RGBWA is calculated at full brightness
    void getRGBWA(double &aRed, double &aGreen, double &aBlue, double &aWhite, double &aAmber, double aMax, bool aNoBrightness, bool aTransitional);

    /// set RGBWA values from lamp (to update channel values from actual lamp setting)
    /// @param aRed,aGreen,aBlue,aWhite,aAmber current R,G,B,W,A values to be converted to color channel settings
    /// @param aMax max value for aRed,aGreen,aBlue,aWhite
    /// @param aNoBrightness do not update brightness channel (when RGB are modulated by a separate brightness)
    void setRGBWA(double aRed, double aGreen, double aBlue, double aWhite, double aAmber, double aMax, bool aNoBrightness);

    /// get Cool White and Warm White colors (from current CT + brightness) for applying to lamp
    /// @param aCW,aWW will receive the CW and WW values corresponding to current channels
    /// @param aMax max value for aCW,aWW
    void getCWWW(double &aCW, double &aWW, double aMax, bool aTransitional);

    /// set Cool White and Warm White values from lamp  (to update channel values from actual lamp setting)
    /// @param aCW,aWW current CW and WW values
    /// @param aMax max value for aCW,aWW
    void setCWWW(double aCW, double aWW, double aMax);

    /// get Brigthness and "coolness" for applying to lamp
    /// @param aBri,aCool will receive brightness and "coolness"
    /// @param aMax max value for aBri,aCool
    void getBriCool(double &aBri, double &aCool, double aMax, bool aTransitional);

    /// set Brigthness and "coolness" from lamp (to update channel values from actual lamp setting)
    /// @param aBri,aCool current brightness and "coolness"
    /// @param aMax max value for aBri,aCool
    void setBriCool(double aBri, double aCool, double aMax);


    /// @}

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
    virtual const char *tableName();
    virtual size_t numFieldDefs();
    virtual const FieldDefinition *getFieldDef(size_t aIndex);
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP);
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags);

  };

  typedef boost::intrusive_ptr<RGBColorLightBehaviour> RGBColorLightBehaviourPtr;

} // namespace p44

#endif /* defined(__p44vdc__colorlightbehaviour__) */
