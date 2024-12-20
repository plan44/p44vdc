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

#include "colorlightbehaviour.hpp"

#include "colorutils.hpp"
#include <math.h>

using namespace p44;


// MARK: - ColorChannel

double ColorChannel::getChannelValueCalculated(bool aTransitional)
{
  // check with behaviour first
  ColorLightBehaviour *cl = dynamic_cast<ColorLightBehaviour *>(&mOutput);
  if (cl) {
    if (cl->mColorMode!=colorMode()) {
      // asking for a color channel that is not native -> have it calculated
      cl->deriveMissingColorChannels(aTransitional);
    }
  }
  // now return it
  return getChannelValue(aTransitional);
}


// MARK: - ColorLightScene


ColorLightScene::ColorLightScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo) :
  inherited(aSceneDeviceSettings, aSceneNo),
  mColorMode(colorLightModeNone),
  mXOrHueOrCt(0),
  mYOrSat(0)
{
}


// MARK: - color scene values/channels


double ColorLightScene::sceneValue(int aChannelIndex)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  switch (cb->getChannelType()) {
    case channeltype_hue: return mColorMode==colorLightModeHueSaturation ? mXOrHueOrCt : 0;
    case channeltype_saturation: return mColorMode==colorLightModeHueSaturation ? mYOrSat : 0;
    case channeltype_colortemp: return mColorMode==colorLightModeCt ? mXOrHueOrCt : 0;
    case channeltype_cie_x: return mColorMode==colorLightModeXY ? mXOrHueOrCt : 0;
    case channeltype_cie_y: return mColorMode==colorLightModeXY ? mYOrSat : 0;
    default: return inherited::sceneValue(aChannelIndex);
  }
  return 0;
}


void ColorLightScene::setSceneValue(int aChannelIndex, double aValue)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  switch (cb->getChannelType()) {
    case channeltype_hue: setPVar(mXOrHueOrCt, aValue); setPVar(mColorMode, colorLightModeHueSaturation); break;
    case channeltype_saturation: setPVar(mYOrSat, aValue); setPVar(mColorMode, colorLightModeHueSaturation); break;
    case channeltype_colortemp: setPVar(mXOrHueOrCt, aValue); setPVar(mColorMode, colorLightModeCt); break;
    case channeltype_cie_x: setPVar(mXOrHueOrCt, aValue); setPVar(mColorMode, colorLightModeXY); break;
    case channeltype_cie_y: setPVar(mYOrSat, aValue); setPVar(mColorMode, colorLightModeXY); break;
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
  mColorMode = (ColorLightMode)aRow->get<int>(aIndex++);
  mXOrHueOrCt = aRow->get<double>(aIndex++);
  mYOrSat = aRow->get<double>(aIndex++);
}


/// bind values to passed statement
void ColorLightScene::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, (int)mColorMode);
  aStatement.bind(aIndex++, mXOrHueOrCt);
  aStatement.bind(aIndex++, mYOrSat);
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
      mColorMode = colorLightModeNone;
      break;
    case PANIC:
    case FIRE:
      // Alert - use cold white
      mColorMode = colorLightModeCt;
      mXOrHueOrCt = 153; // = 1E6/153 = 6535K = cold white
      mYOrSat = 0;
      break;
    default:
      // default color is warm white
      mColorMode = colorLightModeCt;
      mXOrHueOrCt = 370; // = 1E6/370 = 2700k = warm white
      mYOrSat = 0;
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
  mCtOnly(aCtOnly),
  mColorMode(colorLightModeNone),
  mDerivedValuesComplete(false),
  mChannelCouplingMode(channelcoupling_none),
  mChannelCouplingParam(1.0) // "normal"
{
  // primary channel of a color light is always a dimmer controlling the brightness
  setHardwareOutputConfig(mCtOnly ? outputFunction_ctdimmer : outputFunction_colordimmer, outputmode_gradual, usage_undefined, true, -1);
  // Create and add auxiliary channels to the device for Hue, Saturation, Color Temperature and CIE x,y
  // Note: all channels always exists, but for CT only lights, only CT is exposed in the API
  // - hue
  mHue = ChannelBehaviourPtr(new HueChannel(*this));
  if (!aCtOnly) addChannel(mHue);
  // - saturation
  mSaturation = ChannelBehaviourPtr(new SaturationChannel(*this));
  if (!aCtOnly) addChannel(mSaturation);
  // - color temperature
  mCt = ChannelBehaviourPtr(new ColorTempChannel(*this));
  addChannel(mCt);
  // - CIE x and y
  mCIEx = ChannelBehaviourPtr(new CieXChannel(*this));
  if (!aCtOnly) addChannel(mCIEx);
  mCIEy = ChannelBehaviourPtr(new CieYChannel(*this));
  if (!aCtOnly) addChannel(mCIEy);
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
    ColorLightMode loadedMode = colorLightScene->mColorMode;
    switch (loadedMode) {
      case colorLightModeHueSaturation: {
        if (mHue->setChannelValueIfNotDontCare(colorLightScene, colorLightScene->mXOrHueOrCt, ttUp, ttDown, true)) colorInfoSet = true;
        if (mSaturation->setChannelValueIfNotDontCare(colorLightScene, colorLightScene->mYOrSat, ttUp, ttDown, true)) colorInfoSet = true;
        break;
      }
      case colorLightModeXY: {
        if (mCIEx->setChannelValueIfNotDontCare(colorLightScene, colorLightScene->mXOrHueOrCt, ttUp, ttDown, true)) colorInfoSet = true;
        if (mCIEy->setChannelValueIfNotDontCare(colorLightScene, colorLightScene->mYOrSat, ttUp, ttDown, true)) colorInfoSet = true;
        break;
      }
      case colorLightModeCt: {
        if (mCt->setChannelValueIfNotDontCare(colorLightScene, colorLightScene->mXOrHueOrCt, ttUp, ttDown, true)) colorInfoSet = true;
        break;
      }
      default:
        loadedMode = colorLightModeNone;
    }
    if (brightnessForHardware(true)>0 && colorInfoSet) {
      // change current color mode only if final brightness is not zero and any color channels have actually changed
      mColorMode = loadedMode;
      // Don't cares should be correct at this point, but scenes saved long ago might have values that should NOT be
      // applied but don't have a dontCare. The following call will repair these incorrect scenes:
      adjustChannelDontCareToColorMode(colorLightScene, true); // only SET dontCares, but do not remove any
    }
  }
  // need recalculation of values
  mDerivedValuesComplete = false;
}


void ColorLightBehaviour::saveChannelsToScene(DsScenePtr aScene)
{
  // save basic light scene info
  inherited::saveChannelsToScene(aScene);
  // now save color specific scene information
  ColorLightScenePtr colorLightScene = boost::dynamic_pointer_cast<ColorLightScene>(aScene);
  if (colorLightScene) {
    colorLightScene->mColorMode = mColorMode;
    // save the values and adjust don't cares according to color mode
    switch (mColorMode) {
      case colorLightModeHueSaturation: {
        colorLightScene->setPVar(colorLightScene->mXOrHueOrCt, mHue->getChannelValue());
        colorLightScene->setPVar(colorLightScene->mYOrSat, mSaturation->getChannelValue());
        break;
      }
      case colorLightModeXY: {
        colorLightScene->setPVar(colorLightScene->mXOrHueOrCt, mCIEx->getChannelValue());
        colorLightScene->setPVar(colorLightScene->mYOrSat, mCIEy->getChannelValue());
        break;
      }
      case colorLightModeCt: {
        colorLightScene->setPVar(colorLightScene->mXOrHueOrCt, mCt->getChannelValue());
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
  switch (aColorLightScene->mColorMode) {
    case colorLightModeHueSaturation: {
      // don't care unused ones
      if (!mCtOnly) aColorLightScene->setSceneValueFlags(mCIEx->getChannelIndex(), valueflags_dontCare, true);
      if (!mCtOnly) aColorLightScene->setSceneValueFlags(mCIEy->getChannelIndex(), valueflags_dontCare, true);
      aColorLightScene->setSceneValueFlags(mCt->getChannelIndex(), valueflags_dontCare, true);
      if (!aSetOnly) {
        // enable the used values
        if (!mCtOnly) aColorLightScene->setSceneValueFlags(mHue->getChannelIndex(), valueflags_dontCare, false);
        if (!mCtOnly) aColorLightScene->setSceneValueFlags(mSaturation->getChannelIndex(), valueflags_dontCare, false);
      }
      break;
    }
    case colorLightModeXY: {
      // don't care unused ones
      if (!mCtOnly) aColorLightScene->setSceneValueFlags(mHue->getChannelIndex(), valueflags_dontCare, true);
      if (!mCtOnly) aColorLightScene->setSceneValueFlags(mSaturation->getChannelIndex(), valueflags_dontCare, true);
      aColorLightScene->setSceneValueFlags(mCt->getChannelIndex(), valueflags_dontCare, true);
      if (!aSetOnly) {
        // enable the used values
        if (!mCtOnly) aColorLightScene->setSceneValueFlags(mCIEx->getChannelIndex(), valueflags_dontCare, false);
        if (!mCtOnly) aColorLightScene->setSceneValueFlags(mCIEy->getChannelIndex(), valueflags_dontCare, false);
      }
      break;
    }
    case colorLightModeCt: {
      // don't care unused ones
      if (!mCtOnly) aColorLightScene->setSceneValueFlags(mCIEx->getChannelIndex(), valueflags_dontCare, true);
      if (!mCtOnly) aColorLightScene->setSceneValueFlags(mCIEy->getChannelIndex(), valueflags_dontCare, true);
      if (!mCtOnly) aColorLightScene->setSceneValueFlags(mHue->getChannelIndex(), valueflags_dontCare, true);
      if (!mCtOnly) aColorLightScene->setSceneValueFlags(mSaturation->getChannelIndex(), valueflags_dontCare, true);
      if (!aSetOnly) {
        // enable the used values
        aColorLightScene->setSceneValueFlags(mCt->getChannelIndex(), valueflags_dontCare, false);
      }
      break;
    }
    default: {
      // all color related information is dontCare
      if (!mCtOnly) aColorLightScene->setSceneValueFlags(mHue->getChannelIndex(), valueflags_dontCare, true);
      if (!mCtOnly) aColorLightScene->setSceneValueFlags(mSaturation->getChannelIndex(), valueflags_dontCare, true);
      if (!mCtOnly) aColorLightScene->setSceneValueFlags(mCIEx->getChannelIndex(), valueflags_dontCare, true);
      if (!mCtOnly) aColorLightScene->setSceneValueFlags(mCIEy->getChannelIndex(), valueflags_dontCare, true);
      aColorLightScene->setSceneValueFlags(mCt->getChannelIndex(), valueflags_dontCare, true);
      break;
    }
  }
}




// MARK: - color services for implementing color lights


bool ColorLightBehaviour::deriveColorMode()
{
  // the need to derive the color modes only arises when
  // colors (may) have changed, so this invalidates the derived channel values
  mDerivedValuesComplete = false;
  // Note: actual calculation of derived values might not be carried out at all if none of the
  //   derived channel values is queried. However, we must mark the derived channel values
  //   volatile here to make sure these don't get persisted
  // check changed channels
  if (!mCtOnly) {
    if (mHue->needsApplying() || mSaturation->needsApplying()) {
      mColorMode = colorLightModeHueSaturation;
      mHue->setVolatile(false);
      mSaturation->setVolatile(false);
      mCIEx->setVolatile(true);
      mCIEy->setVolatile(true);
      mCt->setVolatile(true);
      return true;
    }
    else if (mCIEx->needsApplying() || mCIEy->needsApplying()) {
      mColorMode = colorLightModeXY;
      mCIEx->setVolatile(false);
      mCIEy->setVolatile(false);
      mHue->setVolatile(true);
      mSaturation->setVolatile(true);
      mCt->setVolatile(true);
      return true;
    }
  }
  if (mCt->needsApplying()) {
    mColorMode = colorLightModeCt;
    mCt->setVolatile(false);
    mCIEx->setVolatile(true);
    mCIEy->setVolatile(true);
    mHue->setVolatile(true);
    mSaturation->setVolatile(true);
    return true;
  }
  // could not determine new color mode (assuming old is still ok)
  return false;
}


bool ColorLightBehaviour::setColorMode(ColorLightMode aColorMode)
{
  if (aColorMode!=mColorMode) {
    deriveMissingColorChannels(false);
    mColorMode = aColorMode;
    return true;
  }
  return false;
}



bool ColorLightBehaviour::getCIExy(double &aCieX, double &aCieY, bool aTransitional)
{
  Row3 HSV;
  Row3 xyV;
  switch (mColorMode) {
    case colorLightModeHueSaturation:
      HSV[0] = mHue->getChannelValue(aTransitional); // 0..360
      HSV[1] = mSaturation->getChannelValue(aTransitional)/100; // 0..1
      HSV[2] = 1;
      HSVtoxyV(HSV, xyV);
      aCieX = xyV[0];
      aCieY = xyV[1];
      break;
    case colorLightModeXY:
      aCieX = mCIEx->getChannelValue(aTransitional);
      aCieY = mCIEy->getChannelValue(aTransitional);
      break;
    case colorLightModeCt:
      CTtoxyV(mCt->getChannelValue(aTransitional), xyV);
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
  switch (mColorMode) {
    case colorLightModeHueSaturation:
      HSV[0] = mHue->getChannelValue(aTransitional); // 0..360
      HSV[1] = mSaturation->getChannelValue(aTransitional)/100; // 0..1
      HSV[2] = 1;
      HSVtoxyV(HSV, xyV);
      xyVtoCT(xyV, aCT);
      break;
    case colorLightModeXY:
      // missing HSV and ct
      xyV[0] = mCIEx->getChannelValue(aTransitional);
      xyV[1] = mCIEy->getChannelValue(aTransitional);
      xyV[2] = 1;
      xyVtoCT(xyV, aCT);
      break;
    case colorLightModeCt:
      aCT = mCt->getChannelValue(aTransitional);
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
  switch (mColorMode) {
    case colorLightModeHueSaturation:
      aHue = mHue->getChannelValue(aTransitional); // 0..360
      aSaturation = mSaturation->getChannelValue(aTransitional);
      break;
    case colorLightModeXY:
      xyV[0] = mCIEx->getChannelValue(aTransitional);
      xyV[1] = mCIEy->getChannelValue(aTransitional);
      xyV[2] = 1;
    xyVtoHSV:
      xyVtoHSV(xyV, HSV);
      aHue = HSV[0];
      aSaturation = HSV[1]*100; // 0..100%
      break;
    case colorLightModeCt:
      CTtoxyV(mCt->getChannelValue(aTransitional), xyV);
      goto xyVtoHSV;
    default:
      return false; // unknown color mode
  }
  return true;
}



void ColorLightBehaviour::deriveMissingColorChannels(bool aTransitional)
{
  if (!mDerivedValuesComplete) {
    Row3 HSV;
    Row3 xyV;
    double mired;
    switch (mColorMode) {
      case colorLightModeHueSaturation:
        // missing CIE and ct
        HSV[0] = mHue->getChannelValue(aTransitional); // 0..360
        HSV[1] = mSaturation->getChannelValue(aTransitional)/100; // 0..1
        HSV[2] = 1;
        HSVtoxyV(HSV, xyV);
        mCIEx->syncChannelValue(xyV[0], false, true); // derived values are always volatile
        mCIEy->syncChannelValue(xyV[1], false, true);
        xyVtoCT(xyV, mired);
        mCt->syncChannelValue(mired, false, true);
        break;
      case colorLightModeXY:
        // missing HSV and ct
        xyV[0] = mCIEx->getChannelValue(aTransitional);
        xyV[1] = mCIEy->getChannelValue(aTransitional);
        xyV[2] = 1;
        xyVtoCT(xyV, mired);
        mCt->syncChannelValue(mired, false, true);
      xyVtoHSV:
        xyVtoHSV(xyV, HSV);
        mHue->syncChannelValue(HSV[0], false, true);
        mSaturation->syncChannelValue(HSV[1]*100, false, true); // 0..100%
        break;
      case colorLightModeCt:
        // missing HSV and xy
        // - xy
        CTtoxyV(mCt->getChannelValue(aTransitional), xyV);
        mCIEx->syncChannelValue(xyV[0], false, true);
        mCIEy->syncChannelValue(xyV[1], false, true);
        // - also create HSV
        goto xyVtoHSV;
      default:
        break;
    }
    mDerivedValuesComplete = true;
    if (DBGLOGENABLED(LOG_DEBUG)) {
      // show all values, plus RGB
      DBGLOG(LOG_DEBUG, "Color mode = %s, actual and derived channel settings:", mColorMode==colorLightModeHueSaturation ? "HSV" : (mColorMode==colorLightModeXY ? "CIExy" : (mColorMode==colorLightModeCt ? "CT" : "none")));
      DBGLOG(LOG_DEBUG, "- HSV : %6.1f, %6.1f, %6.1f [%%, %%, %%]", mHue->getChannelValue(aTransitional), mSaturation->getChannelValue(aTransitional), mBrightness->getChannelValue(aTransitional));
      DBGLOG(LOG_DEBUG, "- xyV : %6.4f, %6.4f, %6.4f [0..1, 0..1, %%]", mCIEx->getChannelValue(aTransitional), mCIEy->getChannelValue(aTransitional), mBrightness->getChannelValue(aTransitional));
      DBGLOG(LOG_DEBUG, "- CT  : %6.0f, %6.0f [mired, K]", mCt->getChannelValue(aTransitional), 1E6/mCt->getChannelValue(aTransitional));
    }
  }
}


void ColorLightBehaviour::appliedColorValues()
{
  mBrightness->channelValueApplied(true);
  switch (mColorMode) {
    case colorLightModeHueSaturation:
      mHue->channelValueApplied(true);
      mSaturation->channelValueApplied(true);
      // reset others in case these were falsely triggered for update
      mCt->makeApplyPending(false);
      mCIEx->makeApplyPending(false);
      mCIEy->makeApplyPending(false);
      break;
    case colorLightModeCt:
      mCt->channelValueApplied(true);
      // reset others in case these were falsely triggered for update
      mHue->makeApplyPending(false);
      mSaturation->makeApplyPending(false);
      mCIEx->makeApplyPending(false);
      mCIEy->makeApplyPending(false);
      break;
    case colorLightModeXY:
      mCIEx->channelValueApplied(true);
      mCIEy->channelValueApplied(true);
      // reset others in case these were falsely triggered for update
      mHue->makeApplyPending(false);
      mSaturation->makeApplyPending(false);
      mCt->makeApplyPending(false);
      break;
    default:
      // no color
      break;
  }
}


bool ColorLightBehaviour::updateColorTransition(MLMicroSeconds aNow)
{
  bool moreSteps = false;
  switch (mColorMode) {
    case colorLightModeHueSaturation:
      if (mHue->updateTimedTransition(aNow)) moreSteps = true;
      if (mSaturation->updateTimedTransition(aNow)) moreSteps = true;
      break;
    case colorLightModeCt:
      if (mCt->updateTimedTransition(aNow)) moreSteps = true;
      break;
    case colorLightModeXY:
      if (mCIEx->updateTimedTransition(aNow)) moreSteps = true;
      if (mCIEy->updateTimedTransition(aNow)) moreSteps = true;
      break;
    default:
      // no color
      break;
  }
  return moreSteps;
}


void ColorLightBehaviour::adjustChannelsCoupledTo(ChannelBehaviourPtr aChannel)
{
  if (mChannelCouplingMode==channelcoupling_none) return;
  if (mChannelCouplingMode==channelcoupling_glowdim && aChannel->getChannelType()==channeltype_brightness) {
    // glow dim
    ChannelBehaviourPtr ct = getChannelByType(channeltype_colortemp);
    if (!ct) return;
    double mired = ct->getMax()+(ct->getMin()-ct->getMax())*::pow(aChannel->getChannelValue()/aChannel->getMax(), mChannelCouplingParam);
    ct->setChannelValue(mired, aChannel->transitionTimeToNewValue());
  }
  #if P44SCRIPT_FULL_SUPPORT
  else if (
    (mChannelCouplingMode==channelcoupling_brightness_script && aChannel->getChannelType()==channeltype_brightness) ||
    (mChannelCouplingMode==channelcoupling_all_script)
  ) {
    // run channel coupling script
    OLOG(LOG_INFO, "Starting channel coupling script: '%s'", singleLine(mChannelCouplingScript.getSource().c_str(), true, 80).c_str() );
    mChannelCouplingScript.setSharedMainContext(mDevice.getDeviceScriptContext(true));
    ScriptObjPtr threadLocals = new SimpleVarContainer();
    threadLocals->setMemberByName("value", new NumericValue(aChannel->getChannelValue()));
    threadLocals->setMemberByName("transition", new NumericValue((double)aChannel->transitionTimeToNewValue()/Second));
    if (mChannelCouplingMode==channelcoupling_all_script) threadLocals->setMemberByName("channelid", new StringValue(aChannel->getId()));
    mChannelCouplingScript.run(inherit, boost::bind(&ColorLightBehaviour::channelCouplingScriptDone, this, _1), threadLocals, 1*Second);
    return;
  }
  #endif
}


#if P44SCRIPT_FULL_SUPPORT
void ColorLightBehaviour::channelCouplingScriptDone(ScriptObjPtr aResult)
{
  if (!aResult || !aResult->isErr()) return;
  OLOG(LOG_ERR, "channel coupling script error: %s", ScriptObj::describe(aResult).c_str());
}
#endif



// MARK: - persistence implementation


const char *ColorLightBehaviour::tableName()
{
  // Important note: we MUST use the inherited (LightBehaviour) table, because we
  //   added ColorLightBehaviour specific fields at a time where ColorLightBehaviours
  //   already existed in the field and had their base fields saved in the
  //   LightOutputSettings table.
  //   So the only compatible way (without extra DB migration) is to add these new color
  //   specific field to the base table.
  return inherited::tableName();
}


// data field definitions

#if P44SCRIPT_FULL_SUPPORT && !P44SCRIPT_REGISTERED_SOURCE
static const size_t numLCFields = 3;
#else
static const size_t numLCFields = 2;
#endif

size_t ColorLightBehaviour::numFieldDefs()
{
  return inherited::numFieldDefs()+numLCFields;
}


const FieldDefinition *ColorLightBehaviour::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numLCFields] = {
    { "channelCoupling", SQLITE_INTEGER },
    { "couplingParam", SQLITE_FLOAT },
    #if P44SCRIPT_FULL_SUPPORT && !P44SCRIPT_REGISTERED_SOURCE
    { "couplingScript", SQLITE_TEXT },
    #endif
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numLCFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void ColorLightBehaviour::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inherited::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get the fields
  mChannelCouplingMode = aRow->getCastedWithDefault<VdcChannelCoupling, int>(aIndex++, channelcoupling_none);
  mChannelCouplingParam = aRow->getWithDefault(aIndex++, 1); // default to 1 = normal
  #if P44SCRIPT_FULL_SUPPORT
  if (mChannelCouplingScript.loadAndActivate(
    string_format("dev_%s.channelcoupling", getDevice().getDsUid().getString().c_str()),
    scriptbody+regular+synchronously,
    "channelcoupling",
    "%C (%O)", // title
    &mDevice,
    nullptr, // standard scripting domain
    #if P44SCRIPT_REGISTERED_SOURCE
    nullptr // no DB field any more for this script
    #else
    aRow->get<const char *>(aIndex++)
    #endif
  )) {
    // set up script command handler 
    // TODO: we don't seem to need a special handler here, remove later
    //mChannelCouplingScript.setScriptCommandHandler(boost::bind(&ColorLightBehaviour::runSceneScriptCommand, this, _1));
  }
  #endif
}


// bind values to passed statement
void ColorLightBehaviour::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, static_cast<int>(mChannelCouplingMode));
  aStatement.bind(aIndex++, mChannelCouplingParam);
  #if P44SCRIPT_FULL_SUPPORT
  mChannelCouplingScript.storeSource();
  #if !P44SCRIPT_REGISTERED_SOURCE
  aStatement.bind(aIndex++, mSceneScript.getSourceToStoreLocally().c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  #endif
  #endif
}



// MARK: - property access


static char colorlight_key;

// settings properties

enum {
  couplingMode_key,
  couplingParam_key,
  #if P44SCRIPT_FULL_SUPPORT
  couplingScript_key,
  couplingScriptId_key,
  #endif
  numCLSettingsProperties
};


int ColorLightBehaviour::numSettingsProps() { return inherited::numSettingsProps()+numCLSettingsProperties; }
const PropertyDescriptorPtr ColorLightBehaviour::getSettingsDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numCLSettingsProperties] = {
    { "x-p44-couplingMode", apivalue_uint64, couplingMode_key+settings_key_offset, OKEY(colorlight_key) },
    { "x-p44-couplingParam", apivalue_double, couplingParam_key+settings_key_offset, OKEY(colorlight_key) },
    #if P44SCRIPT_FULL_SUPPORT
    { "x-p44-couplingScript", apivalue_string, couplingScript_key+settings_key_offset, OKEY(colorlight_key) },
    { "x-p44-couplingScriptId", apivalue_string, couplingScriptId_key+settings_key_offset, OKEY(colorlight_key) },
    #endif
  };
  int n = inherited::numSettingsProps();
  if (aPropIndex<n)
    return inherited::getSettingsDescriptorByIndex(aPropIndex, aParentDescriptor);
  aPropIndex -= n;
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}


// access to all fields

bool ColorLightBehaviour::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(colorlight_key)) {
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Settings properties
        case couplingMode_key+settings_key_offset:
          aPropValue->setUint8Value(mChannelCouplingMode);
          return true;
        case couplingParam_key+settings_key_offset:
          aPropValue->setDoubleValue(mChannelCouplingParam);
          return true;
        #if P44SCRIPT_FULL_SUPPORT
        case couplingScript_key+settings_key_offset:
          aPropValue->setStringValue(mChannelCouplingScript.getSource());
          return true;
        case couplingScriptId_key+settings_key_offset:
          if (!mChannelCouplingScript.active()) return false; // no ID yet
          aPropValue->setStringValue(mChannelCouplingScript.getSourceUid());
          return true;
        #endif
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Settings properties
        case couplingMode_key+settings_key_offset:
          setPVar(mChannelCouplingMode, static_cast<VdcChannelCoupling>(aPropValue->uint8Value()));
          return true;
        case couplingParam_key+settings_key_offset:
          setPVar(mChannelCouplingParam, aPropValue->doubleValue());
          return true;
        #if P44SCRIPT_FULL_SUPPORT
        case couplingScript_key+settings_key_offset:
          // lazy activation when setting a non-empty coupling script
          if (mChannelCouplingScript.setSourceAndActivate(
            aPropValue->stringValue(),
            string_format("dev_%s.channelcoupling", getDevice().getDsUid().getString().c_str()),
            scriptbody+regular+synchronously,
            "channelcoupling",
            "%C (%O)", // title
            &mDevice,
            nullptr // standard scripting domain
          )) {
            markDirty();
            // set up script command handler
            // TODO: we don't seem to need a special handler here, remove later
            //mChannelCouplingScript.setScriptCommandHandler(boost::bind(&ColorLightBehaviour::runSceneScriptCommand, this, _1));
          }
          return true;
        #endif
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}

// MARK: - description/shortDesc


string ColorLightBehaviour::shortDesc()
{
  return string("ColorLight");
}


string ColorLightBehaviour::description()
{
  string s = string_format("%s behaviour", shortDesc().c_str());
  string_format_append(s, "\n- color mode = %s", mColorMode==colorLightModeHueSaturation ? "HSB" : (mColorMode==colorLightModeXY ? "CIExy" : (mColorMode==colorLightModeCt ? "CT" : "none")));
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
  matrix3x3_copy(sRGB_d65_calibration, mCalibration);
  // default white assumed to contribute equally to R,G,B with 35% each
  mWhiteRGB[0] = 0.35; mWhiteRGB[1] = 0.35; mWhiteRGB[2] = 0.35;
  // default amber assumed to be AMBER web color #FFBE00 = 100%, 75%, 0% contributing 50% intensity
  mAmberRGB[0] = 0.5; mAmberRGB[1] = 0.375; mAmberRGB[2] = 0;

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
  switch (mColorMode) {
    case colorLightModeHueSaturation: {
      HSV[0] = mHue->getChannelValue(aTransitional); // 0..360
      HSV[1] = mSaturation->getChannelValue(aTransitional)/100; // 0..1
      HSV[2] = aNoBrightness ? 1 : mBrightness->getChannelValue(aTransitional)/100; // 0..1
      HSVtoRGB(HSV, RGB);
      break;
    }
    case colorLightModeCt: {
      // Note: for some reason, passing brightness to V gives bad results,
      // so for now we always assume 1 and scale resulting RGB
      CTtoxyV(mCt->getChannelValue(aTransitional), xyV);
      xyVtoXYZ(xyV, XYZ);
      XYZtoRGB(mCalibration, XYZ, RGB);
      // get maximum component brightness -> gives 100% brightness point, will be scaled down according to actual brightness
      double m = 0;
      if (RGB[0]>m) m = RGB[0];
      if (RGB[1]>m) m = RGB[1];
      if (RGB[2]>m) m = RGB[2];
      // include actual brightness into scale calculation
      if (!aNoBrightness) scale = mBrightness->getChannelValue(aTransitional)/100/m;
      break;
    }
    case colorLightModeXY: {
      // Note: for some reason, passing brightness to V gives bad results,
      // so for now we always assume 1 and scale resulting RGB
      xyV[0] = mCIEx->getChannelValue(aTransitional);
      xyV[1] = mCIEy->getChannelValue(aTransitional);
      xyV[2] = 1;
      xyVtoXYZ(xyV, XYZ);
      // convert using calibration for this lamp
      XYZtoRGB(mCalibration, XYZ, RGB);
      if (!aNoBrightness) scale = mBrightness->getChannelValue(aTransitional)/100; // 0..1
      break;
    }
    default: {
      // no color, just set R=G=B=brightness
      RGB[0] = aNoBrightness ? 1 : mBrightness->getChannelValue(aTransitional)/100;
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
  mHue->syncChannelValue(HSV[0]);
  mSaturation->syncChannelValue(HSV[1]*100);
  if (!aNoBrightness) syncBrightnessFromHardware(HSV[2]*100);
  // change the mode if needed
  if (mColorMode!=colorLightModeHueSaturation) {
    mColorMode = colorLightModeHueSaturation;
    // force recalculation of derived color value
    mDerivedValuesComplete = false;
  }
}


void RGBColorLightBehaviour::getRGBW(double &aRed, double &aGreen, double &aBlue, double &aWhite, double aMax, bool aNoBrightness, bool aTransitional)
{
  // first get 0..1 RGB
  double r,g,b;
  getRGB(r, g, b, 1, aNoBrightness, aTransitional);
  // transfer as much as possible to the white channel
  double w = transferToColor(mWhiteRGB, r, g, b);
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
  double w = transferToColor(mWhiteRGB, r, g, b);
  // then transfer as much as possible to the amber channel
  double a = transferToColor(mAmberRGB, r, g, b);
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
  transferFromColor(mWhiteRGB, aWhite/aMax, RGB[0], RGB[1], RGB[2]);
  // always convert to HSV, as this can actually represent the values seen on the light
  Row3 HSV;
  RGBtoHSV(RGB, HSV);
  // set the channels
  mHue->syncChannelValue(HSV[0]);
  mSaturation->syncChannelValue(HSV[1]*100);
  if (!aNoBrightness) syncBrightnessFromHardware(HSV[2]*100);
  // change the mode if needed
  if (mColorMode!=colorLightModeHueSaturation) {
    mColorMode = colorLightModeHueSaturation;
    // force recalculation of derived color value
    mDerivedValuesComplete = false;
  }
}


void RGBColorLightBehaviour::setRGBWA(double aRed, double aGreen, double aBlue, double aWhite, double aAmber, double aMax, bool aNoBrightness)
{
  Row3 RGB;
  RGB[0] = aRed/aMax;
  RGB[1] = aGreen/aMax;
  RGB[2] = aBlue/aMax;
  // transfer the amber amount into RGB
  transferFromColor(mAmberRGB, aAmber/aMax, RGB[0], RGB[1], RGB[2]);
  // transfer the white amount into RGB
  transferFromColor(mWhiteRGB, aWhite/aMax, RGB[0], RGB[1], RGB[2]);
  // always convert to HSV, as this can actually represent the values seen on the light
  Row3 HSV;
  RGBtoHSV(RGB, HSV);
  // set the channels
  mHue->syncChannelValue(HSV[0]);
  mSaturation->syncChannelValue(HSV[1]*100);
  if (!aNoBrightness) syncBrightnessFromHardware(HSV[2]*100);
  // change the mode if needed
  if (mColorMode!=colorLightModeHueSaturation) {
    mColorMode = colorLightModeHueSaturation;
    // force recalculation of derived color value
    mDerivedValuesComplete = false;
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
  switch (mColorMode) {
    case colorLightModeCt: {
      // we have mired, use it
      mired = mCt->getChannelValue(aTransitional);
      break;
    }
    case colorLightModeXY: {
      // get mired from x,y
      xyV[0] = mCIEx->getChannelValue(aTransitional);
      xyV[1] = mCIEy->getChannelValue(aTransitional);
      xyV[2] = 1;
      xyVtoCT(xyV, mired);
      break;
    }
    case colorLightModeHueSaturation: {
      // get mired from HS
      HSV[0] = mHue->getChannelValue(aTransitional); // 0..360
      HSV[1] = mSaturation->getChannelValue(aTransitional)/100; // 0..1
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
  double b = mBrightness->getChannelValue(aTransitional)/100; // 0..1
  double t = (mired-mCt->getMin()) / (mCt->getMax()-mCt->getMin()); // 0..1 scale of possible mireds, 0=coldest, 1=warmest
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
  mCt->syncChannelValue(t*(mCt->getMax()-mCt->getMin())+mCt->getMin());
  syncBrightnessFromHardware(b*100);
}


void RGBColorLightBehaviour::getBriCool(double &aBri, double &aCool, double aMax, bool aTransitional)
{
  double b = mBrightness->getChannelValue(aTransitional)/100; // 0..1
  aBri = b*aMax;
  double ctval;
  getCT(ctval, aTransitional);
  // assume cool 1..0 goes over min..max of CT channel
  double cool = 1-(ctval-mCt->getMin())/(mCt->getMax()-mCt->getMin());
  if (cool<0) cool = 0;
  else if (cool>1) cool = 1;
  aBri = aMax*b;
  aCool = aMax*cool;
}


void RGBColorLightBehaviour::setBriCool(double aBri, double aCool, double aMax)
{
  // assume cool 1..0 goes over min..max of CT channel
  syncBrightnessFromHardware(aBri/aMax*100);
  mCt->syncChannelValue((1-aCool/aMax)*(mCt->getMax()-mCt->getMin())+mCt->getMin());
}



// MARK: - persistence implementation


const char *RGBColorLightBehaviour::tableName()
{
  return "RGBLightSettings";
}


// data field definitions

static const size_t numRGBFields = 11;

size_t RGBColorLightBehaviour::numFieldDefs()
{
  return inherited::numFieldDefs()+numRGBFields;
}


const FieldDefinition *RGBColorLightBehaviour::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numRGBFields] = {
    { "Xr", SQLITE_FLOAT },
    { "Yr", SQLITE_FLOAT },
    { "Zr", SQLITE_FLOAT },
    { "Xg", SQLITE_FLOAT },
    { "Yg", SQLITE_FLOAT },
    { "Zg", SQLITE_FLOAT },
    { "Xb", SQLITE_FLOAT },
    { "Yb", SQLITE_FLOAT },
    { "Zb", SQLITE_FLOAT },
    { "whiteRGB", SQLITE_TEXT },
    { "amberRGB", SQLITE_TEXT },
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numRGBFields)
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
      mCalibration[j][i] = aRow->get<double>(aIndex++);
    }
  }
  string c;
  if (aRow->getIfNotNull(aIndex++, c)) pixelToRGB(webColorToPixel(c), mWhiteRGB);
  if (aRow->getIfNotNull(aIndex++, c)) pixelToRGB(webColorToPixel(c), mAmberRGB);
}


// bind values to passed statement
void RGBColorLightBehaviour::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  //  [[Xr,Xg,Xb],[Yr,Yg,Yb],[Zr,Zg,Zb]]
  for (int i=0; i<3; i++) {
    for (int j=0; j<3; j++) {
      aStatement.bind(aIndex++, mCalibration[j][i]);
    }
  }
  aStatement.bind(aIndex++, pixelToWebColor(rgbToPixel(mWhiteRGB), true).c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, pixelToWebColor(rgbToPixel(mAmberRGB), true).c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
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
  whiteRGB_key,
  amberRGB_key,
  numRGBSettingsProperties
};


int RGBColorLightBehaviour::numSettingsProps() { return inherited::numSettingsProps()+numRGBSettingsProperties; }
const PropertyDescriptorPtr RGBColorLightBehaviour::getSettingsDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numRGBSettingsProperties] = {
    { "Xr", apivalue_double, Xr_key+settings_key_offset, OKEY(rgblight_key) },
    { "Yr", apivalue_double, Yr_key+settings_key_offset, OKEY(rgblight_key) },
    { "Zr", apivalue_double, Zr_key+settings_key_offset, OKEY(rgblight_key) },
    { "Xg", apivalue_double, Xg_key+settings_key_offset, OKEY(rgblight_key) },
    { "Yg", apivalue_double, Yg_key+settings_key_offset, OKEY(rgblight_key) },
    { "Zg", apivalue_double, Zg_key+settings_key_offset, OKEY(rgblight_key) },
    { "Xb", apivalue_double, Xb_key+settings_key_offset, OKEY(rgblight_key) },
    { "Yb", apivalue_double, Yb_key+settings_key_offset, OKEY(rgblight_key) },
    { "Zb", apivalue_double, Zb_key+settings_key_offset, OKEY(rgblight_key) },
    { "whiteRGB", apivalue_string, whiteRGB_key+settings_key_offset, OKEY(rgblight_key) },
    { "amberRGB", apivalue_string, amberRGB_key+settings_key_offset, OKEY(rgblight_key) },
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
      // read or write matrix components
      if (aMode==access_read) {
        // read properties
        aPropValue->setDoubleValue(mCalibration[ix/3][ix%3]);
      }
      else {
        // write properties
        setPVar(mCalibration[ix/3][ix%3], aPropValue->doubleValue());
      }
      return true;
    }
    else if (aMode==access_read) {
      // read
      switch (ix) {
        case whiteRGB_key:
          aPropValue->setStringValue(pixelToWebColor(rgbToPixel(mWhiteRGB), true));
          return true;
        case amberRGB_key:
          aPropValue->setStringValue(pixelToWebColor(rgbToPixel(mAmberRGB), true));
          return true;
      }
    }
    else {
      // write
      switch (ix) {
        case whiteRGB_key:
          pixelToRGB(webColorToPixel(aPropValue->stringValue()), mWhiteRGB);
          markDirty(); // TODO: only mark if actually changed?
          return true;
        case amberRGB_key:
          pixelToRGB(webColorToPixel(aPropValue->stringValue()), mAmberRGB);
          markDirty(); // TODO: only mark if actually changed?
          return true;
      }
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






