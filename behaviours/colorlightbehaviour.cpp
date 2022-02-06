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

#include "colorlightbehaviour.hpp"

#include "colorutils.hpp"

using namespace p44;


// MARK: - ColorChannel

double ColorChannel::getChannelValueCalculated()
{
  // check with behaviour first
  ColorLightBehaviour *cl = dynamic_cast<ColorLightBehaviour *>(&mOutput);
  if (cl) {
    if (cl->colorMode!=colorMode()) {
      // asking for a color channel that is not native -> have it calculated
      cl->deriveMissingColorChannels();
    }
  }
  // now return it
  return getChannelValue();
}


// MARK: - ColorLightScene


ColorLightScene::ColorLightScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo) :
  inherited(aSceneDeviceSettings, aSceneNo),
  colorMode(colorLightModeNone),
  XOrHueOrCt(0),
  YOrSat(0)
{
}


// MARK: - color scene values/channels


double ColorLightScene::sceneValue(int aChannelIndex)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  switch (cb->getChannelType()) {
    case channeltype_hue: return colorMode==colorLightModeHueSaturation ? XOrHueOrCt : 0;
    case channeltype_saturation: return colorMode==colorLightModeHueSaturation ? YOrSat : 0;
    case channeltype_colortemp: return colorMode==colorLightModeCt ? XOrHueOrCt : 0;
    case channeltype_cie_x: return colorMode==colorLightModeXY ? XOrHueOrCt : 0;
    case channeltype_cie_y: return colorMode==colorLightModeXY ? YOrSat : 0;
    default: return inherited::sceneValue(aChannelIndex);
  }
  return 0;
}


void ColorLightScene::setSceneValue(int aChannelIndex, double aValue)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  switch (cb->getChannelType()) {
    case channeltype_hue: setPVar(XOrHueOrCt, aValue); setPVar(colorMode, colorLightModeHueSaturation); break;
    case channeltype_saturation: setPVar(YOrSat, aValue); setPVar(colorMode, colorLightModeHueSaturation); break;
    case channeltype_colortemp: setPVar(XOrHueOrCt, aValue); setPVar(colorMode, colorLightModeCt); break;
    case channeltype_cie_x: setPVar(XOrHueOrCt, aValue); setPVar(colorMode, colorLightModeXY); break;
    case channeltype_cie_y: setPVar(YOrSat, aValue); setPVar(colorMode, colorLightModeXY); break;
    default: inherited::setSceneValue(aChannelIndex, aValue); break;
  }
}


// MARK: - Color Light Scene persistence

const char *ColorLightScene::tableName()
{
  return "ColorLightScenes";
}

// data field definitions

static const size_t numColorSceneFields = 3;

size_t ColorLightScene::numFieldDefs()
{
  return inherited::numFieldDefs()+numColorSceneFields;
}


const FieldDefinition *ColorLightScene::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numColorSceneFields] = {
    { "colorMode", SQLITE_INTEGER },
    { "XOrHueOrCt", SQLITE_FLOAT },
    { "YOrSat", SQLITE_FLOAT }
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numColorSceneFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void ColorLightScene::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inherited::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get the fields
  colorMode = (ColorLightMode)aRow->get<int>(aIndex++);
  XOrHueOrCt = aRow->get<double>(aIndex++);
  YOrSat = aRow->get<double>(aIndex++);
}


/// bind values to passed statement
void ColorLightScene::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, (int)colorMode);
  aStatement.bind(aIndex++, XOrHueOrCt);
  aStatement.bind(aIndex++, YOrSat);
}



// MARK: - default color scene

void ColorLightScene::setDefaultSceneValues(SceneNo aSceneNo)
{
  // set the common light scene defaults
  inherited::setDefaultSceneValues(aSceneNo);
  // Add special color lamp behaviour
  switch (aSceneNo) {
    case ROOM_OFF:
    case AREA_1_OFF:
    case AREA_2_OFF:
    case AREA_3_OFF:
    case AREA_4_OFF:
    case PRESET_OFF_10:
    case PRESET_OFF_20:
    case PRESET_OFF_30:
    case PRESET_OFF_40:
    case AUTO_OFF:
    case LOCAL_OFF:
    case DEEP_OFF:
      // no color for off
      colorMode = colorLightModeNone;
      break;
    case PANIC:
    case FIRE:
      // Alert - use cold white
      colorMode = colorLightModeCt;
      XOrHueOrCt = 153; // = 1E6/153 = 6535K = cold white
      YOrSat = 0;
      break;
    default:
      // default color is warm white
      colorMode = colorLightModeCt;
      XOrHueOrCt = 370; // = 1E6/370 = 2700k = warm white
      YOrSat = 0;
  }
  ColorLightBehaviourPtr cb = boost::dynamic_pointer_cast<ColorLightBehaviour>(getOutputBehaviour());
  if (cb) {
    cb->adjustChannelDontCareToColorMode(ColorLightScenePtr(this));
  }
  markClean(); // default values are always clean
}


// MARK: - ColorLightDeviceSettings with default light scenes factory


ColorLightDeviceSettings::ColorLightDeviceSettings(Device &aDevice) :
  inherited(aDevice)
{
};


DsScenePtr ColorLightDeviceSettings::newDefaultScene(SceneNo aSceneNo)
{
  ColorLightScenePtr colorLightScene = ColorLightScenePtr(new ColorLightScene(*this, aSceneNo));
  colorLightScene->setDefaultSceneValues(aSceneNo);
  // return it
  return colorLightScene;
}



// MARK: - ColorLightBehaviour


ColorLightBehaviour::ColorLightBehaviour(Device &aDevice, bool aCtOnly) :
  inherited(aDevice),
  ctOnly(aCtOnly),
  colorMode(colorLightModeNone),
  derivedValuesComplete(false)
{
  // primary channel of a color light is always a dimmer controlling the brightness
  setHardwareOutputConfig(ctOnly ? outputFunction_ctdimmer : outputFunction_colordimmer, outputmode_gradual, usage_undefined, true, -1);
  // Create and add auxiliary channels to the device for Hue, Saturation, Color Temperature and CIE x,y
  // Note: all channels always exists, but for CT only lights, only CT is exposed in the API
  // - hue
  hue = ChannelBehaviourPtr(new HueChannel(*this));
  if (!aCtOnly) addChannel(hue);
  // - saturation
  saturation = ChannelBehaviourPtr(new SaturationChannel(*this));
  if (!aCtOnly) addChannel(saturation);
  // - color temperature
  ct = ChannelBehaviourPtr(new ColorTempChannel(*this));
  addChannel(ct);
  // - CIE x and y
  cieX = ChannelBehaviourPtr(new CieXChannel(*this));
  if (!aCtOnly) addChannel(cieX);
  cieY = ChannelBehaviourPtr(new CieYChannel(*this));
  if (!aCtOnly) addChannel(cieY);
}


Tristate ColorLightBehaviour::hasModelFeature(DsModelFeatures aFeatureIndex)
{
  // now check for light behaviour level features
  switch (aFeatureIndex) {
    case modelFeature_outputchannels:
      // Assumption: all color light output devices need the multi-channel color lamp UI
      return yes;
    default:
      // not available at this level, ask base class
      return inherited::hasModelFeature(aFeatureIndex);
  }
}



void ColorLightBehaviour::loadChannelsFromScene(DsScenePtr aScene)
{
  // load basic light scene info
  inherited::loadChannelsFromScene(aScene);
  // now load color specific scene information
  ColorLightScenePtr colorLightScene = boost::dynamic_pointer_cast<ColorLightScene>(aScene);
  if (colorLightScene) {
    MLMicroSeconds ttUp = transitionTimeFromScene(colorLightScene, true);
    MLMicroSeconds ttDown = transitionTimeFromScene(colorLightScene, false);
    // prepare next color values in channels
    bool colorInfoSet = false;
    ColorLightMode loadedMode = colorLightScene->colorMode;
    switch (loadedMode) {
      case colorLightModeHueSaturation: {
        if (hue->setChannelValueIfNotDontCare(colorLightScene, colorLightScene->XOrHueOrCt, ttUp, ttDown, true)) colorInfoSet = true;
        if (saturation->setChannelValueIfNotDontCare(colorLightScene, colorLightScene->YOrSat, ttUp, ttDown, true)) colorInfoSet = true;
        break;
      }
      case colorLightModeXY: {
        if (cieX->setChannelValueIfNotDontCare(colorLightScene, colorLightScene->XOrHueOrCt, ttUp, ttDown, true)) colorInfoSet = true;
        if (cieY->setChannelValueIfNotDontCare(colorLightScene, colorLightScene->YOrSat, ttUp, ttDown, true)) colorInfoSet = true;
        break;
      }
      case colorLightModeCt: {
        if (ct->setChannelValueIfNotDontCare(colorLightScene, colorLightScene->XOrHueOrCt, ttUp, ttDown, true)) colorInfoSet = true;
        break;
      }
      default:
        loadedMode = colorLightModeNone;
    }
    if (brightnessForHardware(true)>0 && colorInfoSet) {
      // change current color mode only if final brightness is not zero and any color channels have actually changed
      colorMode = loadedMode;
      // Don't cares should be correct at this point, but scenes saved long ago might have values that should NOT be
      // applied but don't have a dontCare. The following call will repair these incorrect scenes:
      adjustChannelDontCareToColorMode(colorLightScene, true); // only SET dontCares, but do not remove any
    }
  }
  // need recalculation of values
  derivedValuesComplete = false;
}


void ColorLightBehaviour::saveChannelsToScene(DsScenePtr aScene)
{
  // save basic light scene info
  inherited::saveChannelsToScene(aScene);
  // now save color specific scene information
  ColorLightScenePtr colorLightScene = boost::dynamic_pointer_cast<ColorLightScene>(aScene);
  if (colorLightScene) {
    colorLightScene->colorMode = colorMode;
    // save the values and adjust don't cares according to color mode
    switch (colorMode) {
      case colorLightModeHueSaturation: {
        colorLightScene->setPVar(colorLightScene->XOrHueOrCt, hue->getChannelValue());
        colorLightScene->setPVar(colorLightScene->YOrSat, saturation->getChannelValue());
        break;
      }
      case colorLightModeXY: {
        colorLightScene->setPVar(colorLightScene->XOrHueOrCt, cieX->getChannelValue());
        colorLightScene->setPVar(colorLightScene->YOrSat, cieY->getChannelValue());
        break;
      }
      case colorLightModeCt: {
        colorLightScene->setPVar(colorLightScene->XOrHueOrCt, ct->getChannelValue());
        break;
      }
      default: {
        break;
      }
    }
    // adjust value dontCare flags
    adjustChannelDontCareToColorMode(colorLightScene);
  }
}


void ColorLightBehaviour::adjustChannelDontCareToColorMode(ColorLightScenePtr aColorLightScene, bool aSetOnly)
{
  // save the values and adjust don't cares according to color mode
  switch (aColorLightScene->colorMode) {
    case colorLightModeHueSaturation: {
      // don't care unused ones
      if (!ctOnly) aColorLightScene->setSceneValueFlags(cieX->getChannelIndex(), valueflags_dontCare, true);
      if (!ctOnly) aColorLightScene->setSceneValueFlags(cieY->getChannelIndex(), valueflags_dontCare, true);
      aColorLightScene->setSceneValueFlags(ct->getChannelIndex(), valueflags_dontCare, true);
      if (!aSetOnly) {
        // enable the used values
        if (!ctOnly) aColorLightScene->setSceneValueFlags(hue->getChannelIndex(), valueflags_dontCare, false);
        if (!ctOnly) aColorLightScene->setSceneValueFlags(saturation->getChannelIndex(), valueflags_dontCare, false);
      }
      break;
    }
    case colorLightModeXY: {
      // don't care unused ones
      if (!ctOnly) aColorLightScene->setSceneValueFlags(hue->getChannelIndex(), valueflags_dontCare, true);
      if (!ctOnly) aColorLightScene->setSceneValueFlags(saturation->getChannelIndex(), valueflags_dontCare, true);
      aColorLightScene->setSceneValueFlags(ct->getChannelIndex(), valueflags_dontCare, true);
      if (!aSetOnly) {
        // enable the used values
        if (!ctOnly) aColorLightScene->setSceneValueFlags(cieX->getChannelIndex(), valueflags_dontCare, false);
        if (!ctOnly) aColorLightScene->setSceneValueFlags(cieY->getChannelIndex(), valueflags_dontCare, false);
      }
      break;
    }
    case colorLightModeCt: {
      // don't care unused ones
      if (!ctOnly) aColorLightScene->setSceneValueFlags(cieX->getChannelIndex(), valueflags_dontCare, true);
      if (!ctOnly) aColorLightScene->setSceneValueFlags(cieY->getChannelIndex(), valueflags_dontCare, true);
      if (!ctOnly) aColorLightScene->setSceneValueFlags(hue->getChannelIndex(), valueflags_dontCare, true);
      if (!ctOnly) aColorLightScene->setSceneValueFlags(saturation->getChannelIndex(), valueflags_dontCare, true);
      if (!aSetOnly) {
        // enable the used values
        aColorLightScene->setSceneValueFlags(ct->getChannelIndex(), valueflags_dontCare, false);
      }
      break;
    }
    default: {
      // all color related information is dontCare
      if (!ctOnly) aColorLightScene->setSceneValueFlags(hue->getChannelIndex(), valueflags_dontCare, true);
      if (!ctOnly) aColorLightScene->setSceneValueFlags(saturation->getChannelIndex(), valueflags_dontCare, true);
      if (!ctOnly) aColorLightScene->setSceneValueFlags(cieX->getChannelIndex(), valueflags_dontCare, true);
      if (!ctOnly) aColorLightScene->setSceneValueFlags(cieY->getChannelIndex(), valueflags_dontCare, true);
      aColorLightScene->setSceneValueFlags(ct->getChannelIndex(), valueflags_dontCare, true);
      break;
    }
  }
}




// MARK: - color services for implementing color lights


bool ColorLightBehaviour::deriveColorMode()
{
  // the need to derive the color modes only arises when
  // colors (may) have changed, so this invalidates the derived channel values
  derivedValuesComplete = false;
  // Note: actual calculation of derived values might not be carried out at all if none of the
  //   derived channel values is queried. However, we must mark the derived channel values
  //   volatile here to make sure these don't get persisted
  // check changed channels
  if (!ctOnly) {
    if (hue->needsApplying() || saturation->needsApplying()) {
      colorMode = colorLightModeHueSaturation;
      hue->setVolatile(false);
      saturation->setVolatile(false);
      cieX->setVolatile(true);
      cieY->setVolatile(true);
      ct->setVolatile(true);
      return true;
    }
    else if (cieX->needsApplying() || cieY->needsApplying()) {
      colorMode = colorLightModeXY;
      cieX->setVolatile(false);
      cieY->setVolatile(false);
      hue->setVolatile(true);
      saturation->setVolatile(true);
      ct->setVolatile(true);
      return true;
    }
  }
  if (ct->needsApplying()) {
    colorMode = colorLightModeCt;
    ct->setVolatile(false);
    cieX->setVolatile(true);
    cieY->setVolatile(true);
    hue->setVolatile(true);
    saturation->setVolatile(true);
    return true;
  }
  // could not determine new color mode (assuming old is still ok)
  return false;
}


bool ColorLightBehaviour::getCIExy(double &aCieX, double &aCieY, bool aTransitional)
{
  Row3 HSV;
  Row3 xyV;
  switch (colorMode) {
    case colorLightModeHueSaturation:
      HSV[0] = hue->getChannelValue(aTransitional); // 0..360
      HSV[1] = saturation->getChannelValue(aTransitional)/100; // 0..1
      HSV[2] = 1;
      HSVtoxyV(HSV, xyV);
      aCieX = xyV[0];
      aCieY = xyV[1];
      break;
    case colorLightModeXY:
      aCieX = cieX->getChannelValue(aTransitional);
      aCieY = cieY->getChannelValue(aTransitional);
      break;
    case colorLightModeCt:
      CTtoxyV(ct->getChannelValue(aTransitional), xyV);
      aCieX = xyV[0];
      aCieY = xyV[1];
      break;
    default:
      return false; // unknown color mode
  }
  return true;
}


bool ColorLightBehaviour::getCT(double &aCT, bool aTransitional)
{
  Row3 HSV;
  Row3 xyV;
  switch (colorMode) {
    case colorLightModeHueSaturation:
      HSV[0] = hue->getChannelValue(aTransitional); // 0..360
      HSV[1] = saturation->getChannelValue(aTransitional)/100; // 0..1
      HSV[2] = 1;
      HSVtoxyV(HSV, xyV);
      xyVtoCT(xyV, aCT);
      break;
    case colorLightModeXY:
      // missing HSV and ct
      xyV[0] = cieX->getChannelValue(aTransitional);
      xyV[1] = cieY->getChannelValue(aTransitional);
      xyV[2] = 1;
      xyVtoCT(xyV, aCT);
      break;
    case colorLightModeCt:
      aCT = ct->getChannelValue(aTransitional);
      break;
    default:
      return false; // unknown color mode
  }
  return true;
}


bool ColorLightBehaviour::getHueSaturation(double &aHue, double &aSaturation, bool aTransitional)
{
  Row3 HSV;
  Row3 xyV;
  switch (colorMode) {
    case colorLightModeHueSaturation:
      aHue = hue->getChannelValue(aTransitional); // 0..360
      aSaturation = saturation->getChannelValue(aTransitional);
      break;
    case colorLightModeXY:
      xyV[0] = cieX->getChannelValue(aTransitional);
      xyV[1] = cieY->getChannelValue(aTransitional);
      xyV[2] = 1;
    xyVtoHSV:
      xyVtoHSV(xyV, HSV);
      aHue = HSV[0];
      aSaturation = HSV[1]*100; // 0..100%
      break;
    case colorLightModeCt:
      CTtoxyV(ct->getChannelValue(aTransitional), xyV);
      goto xyVtoHSV;
    default:
      return false; // unknown color mode
  }
  return true;
}



// Note: altough very similar to the getXXX() above, this one does not operate on transitional values and also
//       can share some code when calculating two missing modes at once
void ColorLightBehaviour::deriveMissingColorChannels()
{
  if (!derivedValuesComplete) {
    Row3 HSV;
    Row3 xyV;
    double mired;
    switch (colorMode) {
      case colorLightModeHueSaturation:
        // missing CIE and ct
        HSV[0] = hue->getChannelValue(); // 0..360
        HSV[1] = saturation->getChannelValue()/100; // 0..1
        HSV[2] = 1;
        HSVtoxyV(HSV, xyV);
        cieX->syncChannelValue(xyV[0], false, true); // derived values are always volatile
        cieY->syncChannelValue(xyV[1], false, true);
        xyVtoCT(xyV, mired);
        ct->syncChannelValue(mired, false, true);
        break;
      case colorLightModeXY:
        // missing HSV and ct
        xyV[0] = cieX->getChannelValue();
        xyV[1] = cieY->getChannelValue();
        xyV[2] = 1;
        xyVtoCT(xyV, mired);
        ct->syncChannelValue(mired, false, true);
      xyVtoHSV:
        xyVtoHSV(xyV, HSV);
        hue->syncChannelValue(HSV[0], false, true);
        saturation->syncChannelValue(HSV[1]*100, false, true); // 0..100%
        break;
      case colorLightModeCt:
        // missing HSV and xy
        // - xy
        CTtoxyV(ct->getChannelValue(), xyV);
        cieX->syncChannelValue(xyV[0], false, true);
        cieY->syncChannelValue(xyV[1], false, true);
        // - also create HSV
        goto xyVtoHSV;
      default:
        break;
    }
    derivedValuesComplete = true;
    if (DBGLOGENABLED(LOG_DEBUG)) {
      // show all values, plus RGB
      DBGLOG(LOG_DEBUG, "Color mode = %s, actual and derived channel settings:", colorMode==colorLightModeHueSaturation ? "HSV" : (colorMode==colorLightModeXY ? "CIExy" : (colorMode==colorLightModeCt ? "CT" : "none")));
      DBGLOG(LOG_DEBUG, "- HSV : %6.1f, %6.1f, %6.1f [%%, %%, %%]", hue->getChannelValue(), saturation->getChannelValue(), brightness->getChannelValue());
      DBGLOG(LOG_DEBUG, "- xyV : %6.4f, %6.4f, %6.4f [0..1, 0..1, %%]", cieX->getChannelValue(), cieY->getChannelValue(), brightness->getChannelValue());
      DBGLOG(LOG_DEBUG, "- CT  : %6.0f, %6.0f [mired, K]", ct->getChannelValue(), 1E6/ct->getChannelValue());
    }
  }
}


void ColorLightBehaviour::appliedColorValues()
{
  brightness->channelValueApplied(true);
  switch (colorMode) {
    case colorLightModeHueSaturation:
      hue->channelValueApplied(true);
      saturation->channelValueApplied(true);
      // reset others in case these were falsely triggered for update
      ct->makeApplyPending(false);
      cieX->makeApplyPending(false);
      cieY->makeApplyPending(false);
      break;
    case colorLightModeCt:
      ct->channelValueApplied(true);
      // reset others in case these were falsely triggered for update
      hue->makeApplyPending(false);
      saturation->makeApplyPending(false);
      cieX->makeApplyPending(false);
      cieY->makeApplyPending(false);
      break;
    case colorLightModeXY:
      cieX->channelValueApplied(true);
      cieY->channelValueApplied(true);
      // reset others in case these were falsely triggered for update
      hue->makeApplyPending(false);
      saturation->makeApplyPending(false);
      ct->makeApplyPending(false);
      break;
    default:
      // no color
      break;
  }
}


bool ColorLightBehaviour::colorTransitionStep(double aStepSize)
{
  bool moreSteps = false;
  switch (colorMode) {
    case colorLightModeHueSaturation:
      if (hue->transitionStep(aStepSize)) moreSteps = true;
      if (saturation->transitionStep(aStepSize)) moreSteps = true;
      break;
    case colorLightModeCt:
      if (ct->transitionStep(aStepSize)) moreSteps = true;
      break;
    case colorLightModeXY:
      if (cieX->transitionStep(aStepSize)) moreSteps = true;
      if (cieY->transitionStep(aStepSize)) moreSteps = true;
      break;
    default:
      // no color
      break;
  }
  return moreSteps;
}



// MARK: - description/shortDesc


string ColorLightBehaviour::shortDesc()
{
  return string("ColorLight");
}


string ColorLightBehaviour::description()
{
  string s = string_format("%s behaviour", shortDesc().c_str());
  string_format_append(s, "\n- color mode = %s", colorMode==colorLightModeHueSaturation ? "HSB" : (colorMode==colorLightModeXY ? "CIExy" : (colorMode==colorLightModeCt ? "CT" : "none")));
  // TODO: add color specific info here
  s.append(inherited::description());
  return s;
}



// MARK: - RGBColorLightBehaviour

#define DUMP_CONVERSION_TABLE 0

RGBColorLightBehaviour::RGBColorLightBehaviour(Device &aDevice, bool aCtOnly) :
  inherited(aDevice, aCtOnly)
{
  // default to sRGB with D65 white point
  matrix3x3_copy(sRGB_d65_calibration, calibration);
  // default white assumed to contribute equally to R,G,B with 35% each
  whiteRGB[0] = 0.35; whiteRGB[1] = 0.35; whiteRGB[2] = 0.35;
  // default amber assumed to be AMBER web color #FFBE00 = 100%, 75%, 0% contributing 50% intensity
  amberRGB[0] = 0.5; amberRGB[1] = 0.375; amberRGB[2] = 0;

  #if DUMP_CONVERSION_TABLE
  // dump a conversion table for HSV -> RGBWA and then back -> HSV, with deltas (dH,dS,dV)
  printf("H;S;V;R;G;B;W;A;H2;S2;V2;dH;dS;dV\n");
  for (int v = 0; v<=100; v += 20) {
    for (int s = 0; s<=100; s += 20) {
      for (int h = 0; h<360; h += 60) {
        // set
        hue->setChannelValue(h);
        saturation->setChannelValue(s);
        brightness->setChannelValue(v);
        double r,g,b,w,a;
        // convert TO RGBWA
        getRGBWA(r, g, b, w, a, 100);
        // destroy values to make sure they get recalculated
        hue->setChannelValue(99);
        saturation->setChannelValue(66);
        brightness->setChannelValue(33);
        hue->channelValueApplied(true);
        saturation->channelValueApplied(true);
        brightness->channelValueApplied(true);
        // convert back FROM RGBWA
        setRGBWA(r, g, b, w, a, 100);
        // dump
        printf(
          "%d;%d;%d;%d;%d;%d;%d;%d;%d;%d;%d;%d;%d;%d\n",
          h,s,v,
          (int)r, (int)g, (int)b, (int)w, (int)a,
          (int)hue->getChannelValue(),
          (int)saturation->getChannelValue(),
          (int)brightness->getChannelValue(),
          h-(int)hue->getChannelValue(),
          s-(int)saturation->getChannelValue(),
          v-(int)brightness->getChannelValue()
        );
      }
    }
  }
  #endif // DUMP_CONVERSION_TABLE
}


static double colorCompScaled(double aColorComp, double aMax)
{
  if (aColorComp<0) aColorComp = 0; // limit to >=0
  aColorComp *= aMax;
  if (aColorComp>aMax) aColorComp = aMax;
  return aColorComp;
}

void RGBColorLightBehaviour::getRGB(double &aRed, double &aGreen, double &aBlue, double aMax, bool aNoBrightness, bool aTransitional)
{
  Row3 RGB;
  Row3 xyV;
  Row3 XYZ;
  Row3 HSV;
  double scale = 1;
  switch (colorMode) {
    case colorLightModeHueSaturation: {
      HSV[0] = hue->getChannelValue(aTransitional); // 0..360
      HSV[1] = saturation->getChannelValue(aTransitional)/100; // 0..1
      HSV[2] = aNoBrightness ? 1 : brightness->getChannelValue(aTransitional)/100; // 0..1
      HSVtoRGB(HSV, RGB);
      break;
    }
    case colorLightModeCt: {
      // Note: for some reason, passing brightness to V gives bad results,
      // so for now we always assume 1 and scale resulting RGB
      CTtoxyV(ct->getChannelValue(aTransitional), xyV);
      xyVtoXYZ(xyV, XYZ);
      XYZtoRGB(calibration, XYZ, RGB);
      // get maximum component brightness -> gives 100% brightness point, will be scaled down according to actual brightness
      double m = 0;
      if (RGB[0]>m) m = RGB[0];
      if (RGB[1]>m) m = RGB[1];
      if (RGB[2]>m) m = RGB[2];
      // include actual brightness into scale calculation
      if (!aNoBrightness) scale = brightness->getChannelValue(aTransitional)/100/m;
      break;
    }
    case colorLightModeXY: {
      // Note: for some reason, passing brightness to V gives bad results,
      // so for now we always assume 1 and scale resulting RGB
      xyV[0] = cieX->getChannelValue(aTransitional);
      xyV[1] = cieY->getChannelValue(aTransitional);
      xyV[2] = 1;
      xyVtoXYZ(xyV, XYZ);
      // convert using calibration for this lamp
      XYZtoRGB(calibration, XYZ, RGB);
      if (!aNoBrightness) scale = brightness->getChannelValue(aTransitional)/100; // 0..1
      break;
    }
    default: {
      // no color, just set R=G=B=brightness
      RGB[0] = aNoBrightness ? 1 : brightness->getChannelValue(aTransitional)/100;
      RGB[1] = RGB[0];
      RGB[2] = RGB[0];
      break;
    }
  }
  aRed = colorCompScaled(RGB[0]*scale, aMax);
  aGreen = colorCompScaled(RGB[1]*scale, aMax);
  aBlue = colorCompScaled(RGB[2]*scale, aMax);
}


void RGBColorLightBehaviour::setRGB(double aRed, double aGreen, double aBlue, double aMax, bool aNoBrightness)
{
  Row3 RGB;
  RGB[0] = aRed/aMax;
  RGB[1] = aGreen/aMax;
  RGB[2] = aBlue/aMax;
  // always convert to HSV, as this can actually represent the values seen on the light
  Row3 HSV;
  RGBtoHSV(RGB, HSV);
  // set the channels
  hue->syncChannelValue(HSV[0]);
  saturation->syncChannelValue(HSV[1]*100);
  if (!aNoBrightness) brightness->syncChannelValue(HSV[2]*100);
  // change the mode if needed
  if (colorMode!=colorLightModeHueSaturation) {
    colorMode = colorLightModeHueSaturation;
    // force recalculation of derived color value
    derivedValuesComplete = false;
  }
}


void RGBColorLightBehaviour::getRGBW(double &aRed, double &aGreen, double &aBlue, double &aWhite, double aMax, bool aNoBrightness, bool aTransitional)
{
  // first get 0..1 RGB
  double r,g,b;
  getRGB(r, g, b, 1, aNoBrightness, aTransitional);
  // transfer as much as possible to the white channel
  double w = transferToColor(whiteRGB, r, g, b);
  // Finally scale as requested
  aWhite = colorCompScaled(w, aMax);
  aRed = colorCompScaled(r, aMax);
  aGreen = colorCompScaled(g, aMax);
  aBlue = colorCompScaled(b, aMax);
}


void RGBColorLightBehaviour::getRGBWA(double &aRed, double &aGreen, double &aBlue, double &aWhite, double &aAmber, double aMax, bool aNoBrightness, bool aTransitional)
{
  // first get RGBW
  double r,g,b;
  getRGB(r, g, b, 1, aNoBrightness, aTransitional);
  // transfer as much as possible to the white channel
  double w = transferToColor(whiteRGB, r, g, b);
  // then transfer as much as possible to the amber channel
  double a = transferToColor(amberRGB, r, g, b);
  // Finally scale as requested
  aAmber = colorCompScaled(a, aMax);
  aWhite = colorCompScaled(w, aMax);
  aRed = colorCompScaled(r, aMax);
  aGreen = colorCompScaled(g, aMax);
  aBlue = colorCompScaled(b, aMax);
}


void RGBColorLightBehaviour::setRGBW(double aRed, double aGreen, double aBlue, double aWhite, double aMax, bool aNoBrightness)
{
  Row3 RGB;
  RGB[0] = aRed/aMax;
  RGB[1] = aGreen/aMax;
  RGB[2] = aBlue/aMax;
  transferFromColor(whiteRGB, aWhite/aMax, RGB[0], RGB[1], RGB[2]);
  // always convert to HSV, as this can actually represent the values seen on the light
  Row3 HSV;
  RGBtoHSV(RGB, HSV);
  // set the channels
  hue->syncChannelValue(HSV[0]);
  saturation->syncChannelValue(HSV[1]*100);
  if (!aNoBrightness) brightness->syncChannelValue(HSV[2]*100);
  // change the mode if needed
  if (colorMode!=colorLightModeHueSaturation) {
    colorMode = colorLightModeHueSaturation;
    // force recalculation of derived color value
    derivedValuesComplete = false;
  }
}


void RGBColorLightBehaviour::setRGBWA(double aRed, double aGreen, double aBlue, double aWhite, double aAmber, double aMax, bool aNoBrightness)
{
  Row3 RGB;
  RGB[0] = aRed/aMax;
  RGB[1] = aGreen/aMax;
  RGB[2] = aBlue/aMax;
  // transfer the amber amount into RGB
  transferFromColor(amberRGB, aAmber/aMax, RGB[0], RGB[1], RGB[2]);
  // transfer the white amount into RGB
  transferFromColor(whiteRGB, aWhite/aMax, RGB[0], RGB[1], RGB[2]);
  // always convert to HSV, as this can actually represent the values seen on the light
  Row3 HSV;
  RGBtoHSV(RGB, HSV);
  // set the channels
  hue->syncChannelValue(HSV[0]);
  saturation->syncChannelValue(HSV[1]*100);
  if (!aNoBrightness) brightness->syncChannelValue(HSV[2]*100);
  // change the mode if needed
  if (colorMode!=colorLightModeHueSaturation) {
    colorMode = colorLightModeHueSaturation;
    // force recalculation of derived color value
    derivedValuesComplete = false;
  }
}




// Simplistic mired to CW/WW conversion
// - turn up WW from 0 to 100 over 100..1000 mired
// - turn down CW from 100 to CW_MIN over 100..1000 mired

#define CW_MIN (0.5)


void RGBColorLightBehaviour::getCWWW(double &aCW, double &aWW, double aMax, bool aTransitional)
{
  Row3 xyV;
  Row3 HSV;
  double mired;
  switch (colorMode) {
    case colorLightModeCt: {
      // we have mired, use it
      mired = ct->getChannelValue(aTransitional);
      break;
    }
    case colorLightModeXY: {
      // get mired from x,y
      xyV[0] = cieX->getChannelValue(aTransitional);
      xyV[1] = cieY->getChannelValue(aTransitional);
      xyV[2] = 1;
      xyVtoCT(xyV, mired);
      break;
    }
    case colorLightModeHueSaturation: {
      // get mired from HS
      HSV[0] = hue->getChannelValue(aTransitional); // 0..360
      HSV[1] = saturation->getChannelValue(aTransitional)/100; // 0..1
      HSV[2] = 1;
      HSVtoxyV(HSV,xyV);
      xyVtoCT(xyV, mired);
      break;
    }
    default: {
      mired = 333; // default to 3000k
    }
  }
  // mired to CW/WW
  double b = brightness->getChannelValue(aTransitional)/100; // 0..1
  double t = (mired-ct->getMin()) / (ct->getMax()-ct->getMin()); // 0..1 scale of possible mireds, 0=coldest, 1=warmest
  // Equations:
  aWW = t * b;
  aCW = ((1-t)*(1-CW_MIN)+CW_MIN) * b;
  // scale
  aWW *= aMax;
  aCW *= aMax;
}


void RGBColorLightBehaviour::setCWWW(double aCW, double aWW, double aMax)
{
  double t;
  double b;
  // descale
  aCW /= aMax;
  aWW /= aMax;
  // Reverse Equations
  if (aWW==0) {
    t = 233; // 3000k default
    b = 0;
  }
  else {
    t = 1 / ((aCW/aWW) - CW_MIN + 1);
    if (t>0) {
      b = aWW / t;
    }
    else {
      b = 1;
    }
  }
  // back to mired and brightness
  ct->syncChannelValue(t*(ct->getMax()-ct->getMin())+ct->getMin());
  brightness->syncChannelValue(b*100);
}


void RGBColorLightBehaviour::getBriCool(double &aBri, double &aCool, double aMax, bool aTransitional)
{
  double b = brightness->getChannelValue(aTransitional)/100; // 0..1
  aBri = b*aMax;
  double ctval;
  getCT(ctval, aTransitional);
  // assume cool 1..0 goes over min..max of CT channel
  double cool = 1-(ctval-ct->getMin())/(ct->getMax()-ct->getMin());
  if (cool<0) cool = 0;
  else if (cool>1) cool = 1;
  aBri = aMax*b;
  aCool = aMax*cool;
}


void RGBColorLightBehaviour::setBriCool(double aBri, double aCool, double aMax)
{
  // assume cool 1..0 goes over min..max of CT channel
  brightness->syncChannelValue(aBri/aMax*100);
  ct->syncChannelValue((1-aCool/aMax)*(ct->getMax()-ct->getMin())+ct->getMin());
}



// MARK: - persistence implementation


const char *RGBColorLightBehaviour::tableName()
{
  return "RGBLightSettings";
}


// data field definitions

static const size_t numFields = 9;

size_t RGBColorLightBehaviour::numFieldDefs()
{
  return inherited::numFieldDefs()+numFields;
}


const FieldDefinition *RGBColorLightBehaviour::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numFields] = {
    { "Xr", SQLITE_FLOAT },
    { "Yr", SQLITE_FLOAT },
    { "Zr", SQLITE_FLOAT },
    { "Xg", SQLITE_FLOAT },
    { "Yg", SQLITE_FLOAT },
    { "Zg", SQLITE_FLOAT },
    { "Xb", SQLITE_FLOAT },
    { "Yb", SQLITE_FLOAT },
    { "Zb", SQLITE_FLOAT },
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void RGBColorLightBehaviour::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inherited::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get the fields
  //  [[Xr,Xg,Xb],[Yr,Yg,Yb],[Zr,Zg,Zb]]
  for (int i=0; i<3; i++) {
    for (int j=0; j<3; j++) {
      calibration[j][i] = aRow->get<double>(aIndex++);
    }
  }
}


// bind values to passed statement
void RGBColorLightBehaviour::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  //  [[Xr,Xg,Xb],[Yr,Yg,Yb],[Zr,Zg,Zb]]
  for (int i=0; i<3; i++) {
    for (int j=0; j<3; j++) {
      aStatement.bind(aIndex++, calibration[j][i]);
    }
  }
}



// MARK: - property access


static char rgblight_key;

// settings properties

enum {
  Xr_key,
  Yr_key,
  Zr_key,
  Xg_key,
  Yg_key,
  Zg_key,
  Xb_key,
  Yb_key,
  Zb_key,
  numSettingsProperties
};


int RGBColorLightBehaviour::numSettingsProps() { return inherited::numSettingsProps()+numSettingsProperties; }
const PropertyDescriptorPtr RGBColorLightBehaviour::getSettingsDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numSettingsProperties] = {
    { "Xr", apivalue_double, Xr_key+settings_key_offset, OKEY(rgblight_key) },
    { "Yr", apivalue_double, Yr_key+settings_key_offset, OKEY(rgblight_key) },
    { "Zr", apivalue_double, Zr_key+settings_key_offset, OKEY(rgblight_key) },
    { "Xg", apivalue_double, Xg_key+settings_key_offset, OKEY(rgblight_key) },
    { "Yg", apivalue_double, Yg_key+settings_key_offset, OKEY(rgblight_key) },
    { "Zg", apivalue_double, Zg_key+settings_key_offset, OKEY(rgblight_key) },
    { "Xb", apivalue_double, Xb_key+settings_key_offset, OKEY(rgblight_key) },
    { "Yb", apivalue_double, Yb_key+settings_key_offset, OKEY(rgblight_key) },
    { "Zb", apivalue_double, Zb_key+settings_key_offset, OKEY(rgblight_key) },
  };
  int n = inherited::numSettingsProps();
  if (aPropIndex<n)
    return inherited::getSettingsDescriptorByIndex(aPropIndex, aParentDescriptor);
  aPropIndex -= n;
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}


// access to all fields

bool RGBColorLightBehaviour::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(rgblight_key)) {
    int ix = (int)aPropertyDescriptor->fieldKey()-settings_key_offset;
    if (ix>=Xr_key && ix<=Zb_key) {
      if (aMode==access_read) {
        // read properties
        aPropValue->setDoubleValue(calibration[ix/3][ix%3]);
      }
      else {
        // write properties
        setPVar(calibration[ix/3][ix%3], aPropValue->doubleValue());
      }
      return true;
    }
    else {
      // check other props
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


// MARK: - description/shortDesc


string RGBColorLightBehaviour::shortDesc()
{
  return string("RGBLight");
}






