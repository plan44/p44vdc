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

#define STANDARD_DIM_CURVE_EXPONENT 4 // standard exponent, usually ok for PWM for LEDs

#define DUMP_CONVERSION_TABLE 0 // set to get a brightness to PWM conversion table on stdout


LightBehaviour::LightBehaviour(Device &aDevice) :
  inherited(aDevice),
  // hardware derived parameters
  // persistent settings
  onThreshold(50.0),
  dimCurveExp(STANDARD_DIM_CURVE_EXPONENT),
  // volatile state
  hardwareHasSetMinDim(false)
{
  // make it member of the light group
  setGroupMembership(group_yellow_light, true);
  // primary output controls brightness
  setHardwareName("brightness");
  // persistent settings
  dimTimeUp[0] = 0x0F; // 100mS // smooth
  dimTimeUp[1] = 0xA2; // 1min (60800mS) // slow
  dimTimeUp[2] = 0x68; // 5sec // custom
  dimTimeDown[0] = 0x0F; // 100mS // smooth
  dimTimeDown[1] = 0xA2; // 1min (60800mS) // slow
  dimTimeDown[2] = 0x68; // 5sec // custom
  // add the brightness channel (every light has brightness)
  brightness = BrightnessChannelPtr(new BrightnessChannel(*this));
  addChannel(brightness);
  #if DUMP_CONVERSION_TABLE
  // dump a conversion table for HSV -> RGBWA and then back -> HSV, with deltas (dH,dS,dV)
  printf("B-in;PWM100-out;PWM-4096;B-back\n");
  for (double b = 0; b<=100; b += 0.05) {
    double pwm = brightnessToPWM(b, 100);
    uint16_t pwm4096 = (uint16_t)(pwm*40.96+0.5);
    double bb = PWMToBrightness(pwm, 100);
    // dump
    printf(
      "%.2f;%.4f;%d;%.2f\n",
      b, pwm, pwm4096, bb
    );
  }
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
  brightness->setDimMin(aMin);
  hardwareHasSetMinDim = true;
}


Brightness LightBehaviour::brightnessForHardware(bool aFinal)
{
  return outputValueAccordingToMode(brightness->getChannelValue(!aFinal), brightness->getChannelIndex());
}



void LightBehaviour::syncBrightnessFromHardware(Brightness aBrightness, bool aAlwaysSync, bool aVolatile)
{
  if (
    isDimmable() || // for dimmable lights: always update value
    ((aBrightness>=onThreshold) != (brightness->getChannelValue()>=onThreshold)) // for switched outputs: keep value if onThreshold condition is already met
  ) {
    brightness->syncChannelValue(aBrightness, aAlwaysSync, aVolatile);
  }
}


double LightBehaviour::outputValueAccordingToMode(double aChannelValue, int aChannelIndex)
{
  // non-default channels and dimmable brightness are passed directly
  if (aChannelIndex!=0 || isDimmable()) {
    return inherited::outputValueAccordingToMode(aChannelValue, aChannelIndex);
  }
  // switched light, check threshold
  return brightness->getChannelValue() >= onThreshold ? brightness->getMax() : brightness->getMin();
}


// MARK: - behaviour interaction with digitalSTROM system


#define AUTO_OFF_FADE_TIME (60*Second)
#define AUTO_OFF_FADE_STEPSIZE 5

void LightBehaviour::loadChannelsFromScene(DsScenePtr aScene)
{
  LightScenePtr lightScene = boost::dynamic_pointer_cast<LightScene>(aScene);
  if (lightScene) {
    // load brightness channel from scene
    Brightness b = lightScene->value;
    brightness->setChannelValueIfNotDontCare(lightScene, b, transitionTimeFromScene(lightScene, true), transitionTimeFromScene(lightScene, false), true);
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
    lightScene->setPVar(lightScene->value, brightness->getChannelValue());
    lightScene->setSceneValueFlags(brightness->getChannelIndex(), valueflags_dontCare, false);
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
  uint8_t dimTimeIndex = 0;
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
  // dimTimeIndex found, look up actual time
  return transitionTimeFromDimTime(aDimUp ? dimTimeUp[dimTimeIndex] : dimTimeDown[dimTimeIndex]);
}


// dS Dimming rule for Light:
//  Rule 4 All devices which are turned on and not in local priority state take part in the dimming process.

bool LightBehaviour::canDim(ChannelBehaviourPtr aChannel)
{
  // to dim anything (not only brightness), brightness value must be >0
  return brightness->getChannelValue()>0;
}


void LightBehaviour::onAtMinBrightness(DsScenePtr aScene)
{
  if (brightness->getChannelValue()<=0) {
    // device is off and must be set to minimal logical brightness
    // but only if the brightness stored in the scene is not zero
    LightScenePtr lightScene = boost::dynamic_pointer_cast<LightScene>(aScene);
    if (lightScene && lightScene->sceneValue(brightness->getChannelIndex())>0) {
      // - load scene values for channels
      loadChannelsFromScene(lightScene); // Note: causes log message because channel is set to new value
      // - override brightness with minDim
      brightness->setChannelValue(brightness->getMinDim(), transitionTimeFromScene(lightScene, true));
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
  if (blinkTicket) stopBlink();
  // let inherited stop as well
  inherited::stopSceneActions();
}



void LightBehaviour::identifyToUser()
{
  // simple, non-parametrized blink, 6 seconds, 1.5 second period, 0.75 second on
  blink(6*Second, LightScenePtr(), NoOP, 1.5*Second, 50);
}


// MARK: - PWM dim curve

// TODO: implement multi point adjustable curves, logarithmic curve with adjustable exponent for now

// PWM    = PWM value
// maxPWM = max PWM value
// B      = brightness
// maxB   = max brightness value
// S      = dim Curve Exponent (1=linear, 2=quadratic, ...)
//
//                   (B*S/maxB)
//                 e            - 1
// PWM =  maxPWM * ----------------
//                      S
//                    e   - 1
//
//                           S
//        maxB        P * (e   - 1)
// B   =  ---- * ln ( ------------- + 1)
//          S             maxP
//

double LightBehaviour::brightnessToPWM(Brightness aBrightness, double aMaxPWM)
{
  return aMaxPWM*((exp(aBrightness*dimCurveExp/100)-1)/(exp(dimCurveExp)-1));
}


Brightness LightBehaviour::PWMToBrightness(double aPWM, double aMaxPWM)
{
  return 100/dimCurveExp*::log(aPWM*(exp(dimCurveExp)-1)/aMaxPWM + 1);
}




// MARK: - blinking


void LightBehaviour::blink(MLMicroSeconds aDuration, LightScenePtr aParamScene, SimpleCB aDoneCB, MLMicroSeconds aBlinkPeriod, int aOnRatioPercent)
{
  // prevent current blink from going on further (but do not restore previous state)
  blinkTicket.cancel();
  // confirm end of previous blink if any handler was set for that
  if (blinkDoneHandler) {
    SimpleCB h = blinkDoneHandler;
    blinkDoneHandler = NoOP;
    h();
  }
  // save new handler now
  blinkDoneHandler = aDoneCB;
  // check for saving current before-blink state
  SceneDeviceSettingsPtr scenes = mDevice.getScenes();
  if (scenes && !blinkRestoreScene) {
    // device has scenes, and blink not in progress already -> capture current state
    blinkRestoreScene = boost::dynamic_pointer_cast<LightScene>(mDevice.getScenes()->newDefaultScene(ROOM_OFF)); // main off as template to store state
    captureScene(blinkRestoreScene, false, boost::bind(&LightBehaviour::beforeBlinkStateSavedHandler, this, aDuration, aParamScene, aBlinkPeriod, aOnRatioPercent));
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
    blinkTicket.cancel();
    // restore previous values if any
    if (blinkRestoreScene) {
      loadChannelsFromScene(blinkRestoreScene);
      blinkRestoreScene.reset();
      mDevice.requestApplyingChannels(NoOP, false); // apply to hardware, not dimming
    }
    // done, call end handler if any
    if (blinkDoneHandler) {
      SimpleCB h = blinkDoneHandler;
      blinkDoneHandler = NoOP;
      h();
    }
    return;
  }
  else if (!aState) {
    // turn on
    brightness->setChannelValue(brightness->getMax(), 0);
    brightness->markClean(); // do not save blink states
  }
  else {
    // turn off
    brightness->setChannelValue(brightness->getMinDim(), 0);
    brightness->markClean(); // do not save blink states
  }
  // apply to hardware
  mDevice.requestApplyingChannels(NoOP, false); // not dimming
  aState = !aState; // toggle
  // schedule next event
  blinkTicket.executeOnce(
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
    { "dimCurveExp", SQLITE_FLOAT },
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
  aRow->getCastedIfNotNull<Brightness, double>(aIndex++, onThreshold);
  // get the other fields
  Brightness md;
  if (aRow->getCastedIfNotNull<Brightness, double>(aIndex++, md)) {
    if (!hardwareHasSetMinDim) brightness->setDimMin(md); // only apply if not set by hardware
  }
  uint32_t du;
  if (aRow->getCastedIfNotNull<uint32_t, int>(aIndex++, du)) {
    // dissect dimming times
    dimTimeUp[0] = du & 0xFF;
    dimTimeUp[1] = (du>>8) & 0xFF;
    dimTimeUp[2] = (du>>16) & 0xFF;
  }
  uint32_t dd;
  if (aRow->getCastedIfNotNull<uint32_t, int>(aIndex++, dd)) {
    // dissect dimming times
    dimTimeDown[0] = dd & 0xFF;
    dimTimeDown[1] = (dd>>8) & 0xFF;
    dimTimeDown[2] = (dd>>16) & 0xFF;
  }
  // read dim curve exponent only if not NULL
  aRow->getIfNotNull<double>(aIndex++, dimCurveExp);
}


// bind values to passed statement
void LightBehaviour::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // create dimming time fields
  uint32_t du =
    dimTimeUp[0] |
    (dimTimeUp[1]<<8) |
    (dimTimeUp[2]<<16);
  uint32_t dd =
    dimTimeDown[0] |
    (dimTimeDown[1]<<8) |
    (dimTimeDown[2]<<16);
  // bind the fields
  aStatement.bind(aIndex++, onThreshold);
  aStatement.bind(aIndex++, brightness->getMinDim());
  aStatement.bind(aIndex++, (int)du);
  aStatement.bind(aIndex++, (int)dd);
  aStatement.bind(aIndex++, dimCurveExp);
}



// MARK: - property access


static char light_key;

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
  dimCurveExp_key,
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
    { "x-p44-dimCurveExp", apivalue_double, dimCurveExp_key+settings_key_offset, OKEY(light_key) },
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
        // Settings properties
        case onThreshold_key+settings_key_offset:
          aPropValue->setDoubleValue(onThreshold);
          return true;
        case minBrightness_key+settings_key_offset:
          aPropValue->setDoubleValue(brightness->getMinDim());
          return true;
        case dimTimeUp_key+settings_key_offset:
        case dimTimeUpAlt1_key+settings_key_offset:
        case dimTimeUpAlt2_key+settings_key_offset:
          aPropValue->setUint8Value(dimTimeUp[aPropertyDescriptor->fieldKey()-(dimTimeUp_key+settings_key_offset)]);
          return true;
        case dimTimeDown_key+settings_key_offset:
        case dimTimeDownAlt1_key+settings_key_offset:
        case dimTimeDownAlt2_key+settings_key_offset:
          aPropValue->setUint8Value(dimTimeDown[aPropertyDescriptor->fieldKey()-(dimTimeDown_key+settings_key_offset)]);
          return true;
        case dimCurveExp_key+settings_key_offset:
          aPropValue->setDoubleValue(dimCurveExp);
          return true;
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Settings properties
        case onThreshold_key+settings_key_offset:
          setPVar(onThreshold, aPropValue->doubleValue());
          return true;
        case minBrightness_key+settings_key_offset:
          brightness->setDimMin(aPropValue->doubleValue());
          if (!hardwareHasSetMinDim) markDirty();
          return true;
        case dimTimeUp_key+settings_key_offset:
        case dimTimeUpAlt1_key+settings_key_offset:
        case dimTimeUpAlt2_key+settings_key_offset:
          setPVar(dimTimeUp[aPropertyDescriptor->fieldKey()-(dimTimeUp_key+settings_key_offset)], (DimmingTime)aPropValue->int32Value());
          return true;
        case dimTimeDown_key+settings_key_offset:
        case dimTimeDownAlt1_key+settings_key_offset:
        case dimTimeDownAlt2_key+settings_key_offset:
          setPVar(dimTimeDown[aPropertyDescriptor->fieldKey()-(dimTimeDown_key+settings_key_offset)], (DimmingTime)aPropValue->int32Value());
          return true;
        case dimCurveExp_key+settings_key_offset:
          setPVar(dimCurveExp, aPropValue->doubleValue());
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
  string_format_append(s, "\n- brightness = %.1f, localPriority = %d", brightness->getChannelValue(), hasLocalPriority());
  string_format_append(s, "\n- dimmable: %d, minBrightness=%.1f, onThreshold=%.1f", isDimmable(), brightness->getMinDim(), onThreshold);
  s.append(inherited::description());
  return s;
}







