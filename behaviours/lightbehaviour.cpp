//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2024 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "lightbehaviour.hpp"

#include <math.h>

using namespace p44;



// MARK: - LightDeviceSettings with default light scenes factory


LightDeviceSettings::LightDeviceSettings(Device &aDevice) :
  inherited(aDevice)
{
};


DsScenePtr LightDeviceSettings::newDefaultScene(SceneNo aSceneNo)
{
  LightScenePtr lightScene = LightScenePtr(new LightScene(*this, aSceneNo));
  lightScene->setDefaultSceneValues(aSceneNo);
  // return it
  return lightScene;
}



// MARK: - LightBehaviour

#define DUMP_CONVERSION_TABLE_FOR_GAMMA 0 // set to a gamma > 0 to get a brightness to Output conversion table on stdout


LightBehaviour::LightBehaviour(Device &aDevice) :
  inherited(aDevice),
  // hardware derived parameters
  // persistent settings
  mOnThreshold(50.0),
  mDefaultGamma(1), // hardware correction factor: linear by default
  mUserGamma(1), // user correction factor: linear by default
  mPreferLinearOutput(false), // no preference for linear!
  // volatile state
  mHardwareHasSetMinDim(false)
{
  // make it member of the light group
  setGroupMembership(group_yellow_light, true);
  // primary output controls brightness
  setHardwareName("brightness");
  // persistent settings
  mDimTimeUp[0] = 0x0F; // 100mS // smooth
  mDimTimeUp[1] = 0xA2; // 1min (60800mS) // slow
  mDimTimeUp[2] = 0x68; // 5sec // custom
  mDimTimeDown[0] = 0x0F; // 100mS // smooth
  mDimTimeDown[1] = 0xA2; // 1min (60800mS) // slow
  mDimTimeDown[2] = 0x68; // 5sec // custom
  // add the brightness channel (every light has brightness)
  mBrightness = BrightnessChannelPtr(new BrightnessChannel(*this));
  addChannel(mBrightness);
  #if DUMP_CONVERSION_TABLE_FOR_GAMMA>0
  #ifndef DEBUG
  #error "DUMP_CONVERSION_TABLE_FOR_GAMMA enabled in non-debug build"
  #endif
  // dump a conversion table for brightness -> output and then back, with gamma = DUMP_CONVERSION_TABLE_FOR_GAMMA
  double origGamma = mGamma;
  mGamma = DUMP_CONVERSION_TABLE_FOR_GAMMA;
  printf("B-in;PWM100-out;PWM-4096;B-back;gamma\n");
  for (double b = 0; b<=100; b += 0.05) {
    double pwm = brightnessToOutput(b, 100);
    uint16_t pwm4096 = (uint16_t)(pwm*40.96+0.5);
    double bb = OutputToBrightness(pwm, 100);
    // dump
    printf(
      "%.2f;%.4f;%d;%.2f;%.3f\n",
      b, pwm, pwm4096, bb, mGamma
    );
  }
  mGamma = origGamma; // restore
  #endif // DUMP_CONVERSION_TABLE
}


Tristate LightBehaviour::hasModelFeature(DsModelFeatures aFeatureIndex)
{
  // now check for light behaviour level features
  switch (aFeatureIndex) {
    case modelFeature_outmode:
      // Lights that support dimming (not only switched) should have this
      return getOutputFunction()!=outputFunction_switch ? yes : no;
    case modelFeature_outmodeswitch:
      // Lights with switch-only output (not dimmable) should have this
      return getOutputFunction()==outputFunction_switch ? yes : no;
    case modelFeature_outmodegeneric:
      // suppress generic output mode, we have switched/dimmer selection
      return no;
    case modelFeature_transt:
      // Assumption: all light output devices have transition times
      return yes;
    default:
      // not available at this level, ask base class
      return inherited::hasModelFeature(aFeatureIndex);
  }
}



void LightBehaviour::initMinBrightness(Brightness aMin)
{
  // save min
  mBrightness->setDimMin(aMin);
  mHardwareHasSetMinDim = true;
}


Brightness LightBehaviour::brightnessForHardware(bool aFinal)
{
  return outputValueAccordingToMode(mBrightness->getChannelValue(!aFinal), mBrightness->getChannelIndex());
}



void LightBehaviour::syncBrightnessFromHardware(Brightness aBrightness, bool aAlwaysSync, bool aVolatile)
{
  if (
    isDimmable() || // for dimmable lights: always update value
    ((aBrightness>=mOnThreshold) != (mBrightness->getChannelValue()>=mOnThreshold)) // for switched outputs: keep value if onThreshold condition is already met
  ) {
    mBrightness->syncChannelValue(outputToBrightness(aBrightness, 100), aAlwaysSync, aVolatile);
  }
}


double LightBehaviour::outputValueAccordingToMode(double aChannelValue, int aChannelIndex)
{
  // non-default channels and dimmable brightness are passed directly
  if (aChannelIndex!=0 || isDimmable()) {
    // apply behaviour-level dimming curve here
    return inherited::outputValueAccordingToMode(brightnessToOutput(aChannelValue, 100), aChannelIndex);
  }
  // switched light, check threshold
  return mBrightness->getChannelValue() >= mOnThreshold ? mBrightness->getMax() : mBrightness->getMin();
}


// MARK: - behaviour interaction with Digital Strom system


#define AUTO_OFF_FADE_TIME (60*Second)
#define AUTO_OFF_FADE_STEPSIZE 5

void LightBehaviour::loadChannelsFromScene(DsScenePtr aScene)
{
  LightScenePtr lightScene = boost::dynamic_pointer_cast<LightScene>(aScene);
  if (lightScene) {
    // load brightness channel from scene
    Brightness b = lightScene->value;
    mBrightness->setChannelValueIfNotDontCare(lightScene, b, transitionTimeFromScene(lightScene, true), transitionTimeFromScene(lightScene, false), true);
  }
  else {
    // only if not light scene, use default loader
    inherited::loadChannelsFromScene(aScene);
  }
}


void LightBehaviour::saveChannelsToScene(DsScenePtr aScene)
{
  LightScenePtr lightScene = boost::dynamic_pointer_cast<LightScene>(aScene);
  if (lightScene) {
    // save brightness channel from scene
    lightScene->setPVar(lightScene->value, mBrightness->getChannelValue());
    lightScene->setSceneValueFlags(mBrightness->getChannelIndex(), valueflags_dontCare, false);
  }
}



// aDimTime : transition time specification in dS format
//   T = 100ms*2^exp - (100ms*2^exp)* (15-lin)/32
//     = 100ms*2^exp * (1-(15-lin)/32)
//     = 100ms/32*2^exp * (1-(15-lin)/32)*32
//     = 100ms/32*2^exp * (32-(15-lin))
//     = 100ms/32*2^exp * (17+lin)
// Examples:
//   0x0F: 100 ms
//   0x1F: 200 ms
//   0x27: 300 ms
//   0x2F: 400 ms
//   0x37: 600 ms
//   0x68: 5000 ms
//   0xA2: 60800 ms
static MLMicroSeconds transitionTimeFromDimTime(uint8_t aDimTime)
{
  return
    (((100*MilliSecond)/32)<<((aDimTime>>4) & 0xF)) * // 100ms/32*2^exp *
    (17+(aDimTime & 0xF)); // (17+lin)
}


MLMicroSeconds LightBehaviour::transitionTimeFromScene(DsScenePtr aScene, bool aDimUp)
{
  uint8_t dimTimeIndex = 0; // default to smooth dimming
  SimpleScenePtr ssc = boost::dynamic_pointer_cast<SimpleScene>(aScene);
  if (ssc) {
    switch (ssc->mEffect) {
      case scene_effect_smooth :
        dimTimeIndex = 0; break;
      case scene_effect_slow :
        dimTimeIndex = 1; break;
      case scene_effect_custom :
        dimTimeIndex = 2; break;
      default:
        return inherited::transitionTimeFromScene(aScene, aDimUp);
    }
  }
  // dimTimeIndex derived from effect or no scene at all, look up actual time
  return transitionTimeFromDimTime(aDimUp ? mDimTimeUp[dimTimeIndex] : mDimTimeDown[dimTimeIndex]);
}


// dS Dimming rule for Light:
//  Rule 4 All devices which are turned on and not in local priority state take part in the dimming process.

bool LightBehaviour::canDim(ChannelBehaviourPtr aChannel)
{
  // to dim anything (not only brightness), brightness value must be >0
  return mBrightness->getChannelValue()>0;
}


void LightBehaviour::onAtMinBrightness(DsScenePtr aScene)
{
  if (mBrightness->getChannelValue()<=0) {
    // device is off and must be set to minimal logical brightness
    // but only if the brightness stored in the scene is not zero
    LightScenePtr lightScene = boost::dynamic_pointer_cast<LightScene>(aScene);
    if (lightScene && lightScene->sceneValue(mBrightness->getChannelIndex())>0) {
      // - load scene values for channels
      loadChannelsFromScene(lightScene); // Note: causes log message because channel is set to new value
      // - override brightness with minDim
      mBrightness->setChannelValue(mBrightness->getMinDim(), transitionTimeFromScene(lightScene, true));
    }
  }
}


void LightBehaviour::performSceneActions(DsScenePtr aScene, SimpleCB aDoneCB)
{
  // we can only handle light scenes
  LightScenePtr lightScene = boost::dynamic_pointer_cast<LightScene>(aScene);
  if (lightScene && lightScene->mEffect==scene_effect_alert) {
    // run blink effect
    // - set defaults
    int rep = 2;
    MLMicroSeconds period = 2*Second;
    int onratio = 50;
    // - can be parametrized: effectParam!=0 -> 0xrroopppp : rr=repetitions, oo=ontime ration, pppp=period in milliseconds
    uint32_t ep = lightScene->mEffectParam;
    if (ep!=0) {
      rep = (ep>>24) & 0xFF;
      onratio = (ep>>16) & 0xFF;
      period = (MLMicroSeconds)(ep & 0xFFFF)*MilliSecond;
    }
    blink(rep*period, lightScene, aDoneCB, period, onratio);
    return;
  }
  // none of my effects, let inherited check
  inherited::performSceneActions(aScene, aDoneCB);
}


void LightBehaviour::stopSceneActions()
{
  // stop blink
  if (mBlinkTicket) stopBlink();
  // let inherited stop as well
  inherited::stopSceneActions();
}



void LightBehaviour::identifyToUser(MLMicroSeconds aDuration)
{
  if (aDuration<0) {
    // stop identification
    stopBlink();
  }
  else {
    // simple, non-parametrized blink, 1.5 second period, 0.75 second on
    if (aDuration==Never) aDuration=6*Second; // default is 6 seconds
    blink(aDuration, LightScenePtr(), NoOP, 1.5*Second, 50);
  }
}


// MARK: - Dimming curve (Brightness -> hardware output relation)


double LightBehaviour::brightnessToOutput(Brightness aBrightness, double aMaxOutput)
{
  if (aBrightness<=0) return 0;
  double gamma = mUserGamma*mDefaultGamma;
  if (gamma==1 || gamma<=0) {
    // gamma==1 -> linear, only scale for output
    return aBrightness/100*aMaxOutput;
  }
  else {
    // gamma(x, g) = x^g  (in 0..1 ranges for both input and output)
    return ::pow(aBrightness/100, gamma)*aMaxOutput;
  }
}


Brightness LightBehaviour::outputToBrightness(double aOutValue, double aMaxOutput)
{
  if (aMaxOutput<=0) return 0;
  double gamma = mUserGamma*mDefaultGamma;
  if (gamma==1 || gamma<=0) {
    // gamma==1 -> linear, only scale output down to brightness
    return aOutValue/aMaxOutput*100;
  }
  else {
    // gamma(x, g) = x^g  (in 0..1 ranges for both input and output)
    return ::pow(aOutValue/aMaxOutput, 1/gamma)*100;
  }
}




// MARK: - blinking


void LightBehaviour::blink(MLMicroSeconds aDuration, LightScenePtr aParamScene, SimpleCB aDoneCB, MLMicroSeconds aBlinkPeriod, int aOnRatioPercent)
{
  // prevent current blink from going on further (but do not restore previous state)
  mBlinkTicket.cancel();
  // confirm end of previous blink if any handler was set for that
  if (mBlinkDoneHandler) {
    SimpleCB h = mBlinkDoneHandler;
    mBlinkDoneHandler = NoOP;
    h();
  }
  // save new handler now
  mBlinkDoneHandler = aDoneCB;
  // check for saving current before-blink state
  SceneDeviceSettingsPtr scenes = mDevice.getScenes();
  if (scenes && !mBlinkRestoreScene) {
    // device has scenes, and blink not in progress already -> capture current state
    mBlinkRestoreScene = boost::dynamic_pointer_cast<LightScene>(mDevice.getScenes()->newDefaultScene(ROOM_OFF)); // main off as template to store state
    captureScene(mBlinkRestoreScene, false, boost::bind(&LightBehaviour::beforeBlinkStateSavedHandler, this, aDuration, aParamScene, aBlinkPeriod, aOnRatioPercent));
  }
  else {
    // device has no scenes (some switch outputs don't have scenes), or blink already in progress -> just start blinking
    beforeBlinkStateSavedHandler(aDuration, aParamScene, aBlinkPeriod, aOnRatioPercent);
  }
}


void LightBehaviour::stopBlink()
{
  // immediately terminate (also kills ticket)
  blinkHandler(0 /* done */, false, 0, 0); // dummy params, only to immediately stop it
}



void LightBehaviour::beforeBlinkStateSavedHandler(MLMicroSeconds aDuration, LightScenePtr aParamScene, MLMicroSeconds aBlinkPeriod, int aOnRatioPercent)
{
  // apply the parameter scene if any
  if (aParamScene) loadChannelsFromScene(aParamScene);
  // start flashing
  MLMicroSeconds blinkOnTime = (aBlinkPeriod*aOnRatioPercent*10)/1000;
  aBlinkPeriod -= blinkOnTime; // blink off time
  // start off, so first action will be on
  blinkHandler(MainLoop::now()+aDuration, false, blinkOnTime, aBlinkPeriod);
}


void LightBehaviour::blinkHandler(MLMicroSeconds aEndTime, bool aState, MLMicroSeconds aOnTime, MLMicroSeconds aOffTime)
{
  if (MainLoop::now()>=aEndTime) {
    // kill scheduled execution, if any
    mBlinkTicket.cancel();
    // restore previous values if any
    if (mBlinkRestoreScene) {
      loadChannelsFromScene(mBlinkRestoreScene);
      mBlinkRestoreScene.reset();
      mDevice.requestApplyingChannels(NoOP, false); // apply to hardware, not dimming
    }
    // done, call end handler if any
    if (mBlinkDoneHandler) {
      SimpleCB h = mBlinkDoneHandler;
      mBlinkDoneHandler = NoOP;
      h();
    }
    return;
  }
  else if (!aState) {
    // turn on
    mBrightness->setChannelValue(mBrightness->getMax(), 0);
    mBrightness->markClean(); // do not save blink states
  }
  else {
    // turn off
    mBrightness->setChannelValue(mBrightness->getMinDim(), 0);
    mBrightness->markClean(); // do not save blink states
  }
  // apply to hardware
  mDevice.requestApplyingChannels(NoOP, false); // not dimming
  aState = !aState; // toggle
  // schedule next event
  mBlinkTicket.executeOnce(
    boost::bind(&LightBehaviour::blinkHandler, this, aEndTime, aState, aOnTime, aOffTime),
    aState ? aOnTime : aOffTime
  );
}


// MARK: - persistence implementation


const char *LightBehaviour::tableName()
{
  return "LightOutputSettings";
}


// data field definitions

static const size_t numFields = 5;

size_t LightBehaviour::numFieldDefs()
{
  return inherited::numFieldDefs()+numFields;
}


const FieldDefinition *LightBehaviour::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numFields] = {
    { "switchThreshold", SQLITE_FLOAT }, // formerly onThreshold, renamed because type changed
    { "minBrightness", SQLITE_FLOAT }, // formerly minBrightness, renamed because type changed
    { "dimUpTimes", SQLITE_INTEGER },
    { "dimDownTimes", SQLITE_INTEGER },
    { "dimCurveExp", SQLITE_FLOAT }, // actually, since 2024-06-27: gamma
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void LightBehaviour::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inherited::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // read onThreshold only if not NULL
  aRow->getCastedIfNotNull<Brightness, double>(aIndex++, mOnThreshold);
  // get the other fields
  Brightness md;
  if (aRow->getCastedIfNotNull<Brightness, double>(aIndex++, md)) {
    if (!mHardwareHasSetMinDim) mBrightness->setDimMin(md); // only apply if not set by hardware
  }
  uint32_t du;
  if (aRow->getCastedIfNotNull<uint32_t, int>(aIndex++, du)) {
    // dissect dimming times
    mDimTimeUp[0] = du & 0xFF;
    mDimTimeUp[1] = (du>>8) & 0xFF;
    mDimTimeUp[2] = (du>>16) & 0xFF;
  }
  uint32_t dd;
  if (aRow->getCastedIfNotNull<uint32_t, int>(aIndex++, dd)) {
    // dissect dimming times
    mDimTimeDown[0] = dd & 0xFF;
    mDimTimeDown[1] = (dd>>8) & 0xFF;
    mDimTimeDown[2] = (dd>>16) & 0xFF;
  }
  // read dim curve exponent only if not NULL
  aRow->getIfNotNull<double>(aIndex++, mUserGamma);
}


// bind values to passed statement
void LightBehaviour::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // create dimming time fields
  uint32_t du =
    mDimTimeUp[0] |
    (mDimTimeUp[1]<<8) |
    (mDimTimeUp[2]<<16);
  uint32_t dd =
    mDimTimeDown[0] |
    (mDimTimeDown[1]<<8) |
    (mDimTimeDown[2]<<16);
  // bind the fields
  aStatement.bind(aIndex++, mOnThreshold);
  aStatement.bind(aIndex++, mBrightness->getMinDim());
  aStatement.bind(aIndex++, (int)du);
  aStatement.bind(aIndex++, (int)dd);
  aStatement.bind(aIndex++, mUserGamma);
}



// MARK: - property access


static char light_key;

// description properties

enum {
  defaultGamma_key,
  numDescProperties
};


int LightBehaviour::numDescProps() { return inherited::numDescProps()+numDescProperties; }
const PropertyDescriptorPtr LightBehaviour::getDescDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numDescProperties] = {
    { "x-p44-defaultGamma", apivalue_double, defaultGamma_key+descriptions_key_offset, OKEY(light_key) },
  };
  int n = inherited::numDescProps();
  if (aPropIndex<n)
    return inherited::getDescDescriptorByIndex(aPropIndex, aParentDescriptor);
  aPropIndex -= n;
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}


// settings properties

enum {
  onThreshold_key,
  minBrightness_key,
  dimTimeUp_key, // upAlt1/2 must immediately follow (array index calculation in accessField below!)
  dimTimeUpAlt1_key,
  dimTimeUpAlt2_key,
  dimTimeDown_key, // downAlt1/2 must immediately follow (array index calculation in accessField below!)
  dimTimeDownAlt1_key,
  dimTimeDownAlt2_key,
  gamma_key, // formerly "dimCurveExp"
  numSettingsProperties
};


int LightBehaviour::numSettingsProps() { return inherited::numSettingsProps()+numSettingsProperties; }
const PropertyDescriptorPtr LightBehaviour::getSettingsDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numSettingsProperties] = {
    { "onThreshold", apivalue_double, onThreshold_key+settings_key_offset, OKEY(light_key) },
    { "minBrightness", apivalue_double, minBrightness_key+settings_key_offset, OKEY(light_key) },
    { "dimTimeUp", apivalue_uint64, dimTimeUp_key+settings_key_offset, OKEY(light_key) },
    { "dimTimeUpAlt1", apivalue_uint64, dimTimeUpAlt1_key+settings_key_offset, OKEY(light_key) },
    { "dimTimeUpAlt2", apivalue_uint64, dimTimeUpAlt2_key+settings_key_offset, OKEY(light_key) },
    { "dimTimeDown", apivalue_uint64, dimTimeDown_key+settings_key_offset, OKEY(light_key) },
    { "dimTimeDownAlt1", apivalue_uint64, dimTimeDownAlt1_key+settings_key_offset, OKEY(light_key) },
    { "dimTimeDownAlt2", apivalue_uint64, dimTimeDownAlt2_key+settings_key_offset, OKEY(light_key) },
    { "x-p44-gamma", apivalue_double, gamma_key+settings_key_offset, OKEY(light_key) },  // formerly "x-p44-dimCurveExp"
  };
  int n = inherited::numSettingsProps();
  if (aPropIndex<n)
    return inherited::getSettingsDescriptorByIndex(aPropIndex, aParentDescriptor);
  aPropIndex -= n;
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}


// access to all fields

bool LightBehaviour::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(light_key)) {
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Description properties
        case defaultGamma_key+descriptions_key_offset:
          aPropValue->setDoubleValue(mDefaultGamma);
          return true;
        // Settings properties
        case onThreshold_key+settings_key_offset:
          aPropValue->setDoubleValue(mOnThreshold);
          return true;
        case minBrightness_key+settings_key_offset:
          aPropValue->setDoubleValue(mBrightness->getMinDim());
          return true;
        case dimTimeUp_key+settings_key_offset:
        case dimTimeUpAlt1_key+settings_key_offset:
        case dimTimeUpAlt2_key+settings_key_offset:
          aPropValue->setUint8Value(mDimTimeUp[aPropertyDescriptor->fieldKey()-(dimTimeUp_key+settings_key_offset)]);
          return true;
        case dimTimeDown_key+settings_key_offset:
        case dimTimeDownAlt1_key+settings_key_offset:
        case dimTimeDownAlt2_key+settings_key_offset:
          aPropValue->setUint8Value(mDimTimeDown[aPropertyDescriptor->fieldKey()-(dimTimeDown_key+settings_key_offset)]);
          return true;
        case gamma_key+settings_key_offset:
          aPropValue->setDoubleValue(mUserGamma);
          return true;
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Settings properties
        case onThreshold_key+settings_key_offset:
          setPVar(mOnThreshold, aPropValue->doubleValue());
          return true;
        case minBrightness_key+settings_key_offset:
          mBrightness->setDimMin(aPropValue->doubleValue());
          if (!mHardwareHasSetMinDim) markDirty();
          return true;
        case dimTimeUp_key+settings_key_offset:
        case dimTimeUpAlt1_key+settings_key_offset:
        case dimTimeUpAlt2_key+settings_key_offset:
          setPVar(mDimTimeUp[aPropertyDescriptor->fieldKey()-(dimTimeUp_key+settings_key_offset)], (DimmingTime)aPropValue->int32Value());
          return true;
        case dimTimeDown_key+settings_key_offset:
        case dimTimeDownAlt1_key+settings_key_offset:
        case dimTimeDownAlt2_key+settings_key_offset:
          setPVar(mDimTimeDown[aPropertyDescriptor->fieldKey()-(dimTimeDown_key+settings_key_offset)], (DimmingTime)aPropValue->int32Value());
          return true;
        case gamma_key+settings_key_offset:
          setPVar(mUserGamma, aPropValue->doubleValue());
          return true;
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


// MARK: - description/shortDesc


string LightBehaviour::shortDesc()
{
  return string("Light");
}


string LightBehaviour::description()
{
  string s = string_format("%s behaviour", shortDesc().c_str());
  string_format_append(s, "\n- brightness = %.1f, localPriority = %d", mBrightness->getChannelValue(), hasLocalPriority());
  string_format_append(s, "\n- dimmable: %d, minBrightness=%.1f, onThreshold=%.1f", isDimmable(), mBrightness->getMinDim(), mOnThreshold);
  s.append(inherited::description());
  return s;
}







