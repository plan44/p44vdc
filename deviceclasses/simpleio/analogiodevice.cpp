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

#include "analogiodevice.hpp"

#if ENABLE_STATIC

#include "lightbehaviour.hpp"
#include "sensorbehaviour.hpp"
#include "colorlightbehaviour.hpp"
#include "climatecontrolbehaviour.hpp"

using namespace p44;

#define STANDARD_PWM_GAMMA 4 // standard gamma that works ok for PWM for LEDs (roughly same as the old exp/log curve of before 2024-06-27)

AnalogIODevice::AnalogIODevice(StaticVdc *aVdcP, const string &aDeviceConfig) :
  StaticDevice((Vdc *)aVdcP),
  mAnalogIOType(analogio_unknown),
  mScaling(1),
  mOffset(0)
{
  // Config is:
  //  <pin(s) specification>:[<behaviour mode>]
  //  - where ! before the behaviour mode means inverted operation (in addition to possibly inverted pin specs)
  //  - where pin specification describes the actual I/Os to be used (see DigitialIO)
  string ioname = aDeviceConfig;
  string mode = "dimmer"; // default to dimmer
  size_t i = aDeviceConfig.find(":");
  if (i!=string::npos) {
    ioname = aDeviceConfig.substr(0,i);
    mode = aDeviceConfig.substr(i+1,string::npos);
  }
  if (mode=="dimmer")
    mAnalogIOType = analogio_dimmer;
  else if (mode=="rgbdimmer")
    mAnalogIOType = analogio_rgbdimmer;
  else if (mode=="cwwwdimmer")
    mAnalogIOType = analogio_cwwwdimmer;
  else if (mode=="valve")
    mAnalogIOType = analogio_valve;
  else if (mode.find("sensor")==0) // sensor can have further specification in mode string
    mAnalogIOType = analogio_sensor;
  else {
    LOG(LOG_ERR, "unknown analog IO type: %s", mode.c_str());
  }
  // by default, act as black device so we can configure colors
  mColorClass = class_black_joker;
  if (mAnalogIOType==analogio_dimmer) {
    // Analog output as dimmer
    mAnalogIO = AnalogIoPtr(new AnalogIo(ioname.c_str(), true, 0));
    // - is light
    mColorClass = class_yellow_light;
    // - use light settings, which include a scene table
    installSettings(DeviceSettingsPtr(new LightDeviceSettings(*this)));
    // - add simple single-channel light behaviour
    LightBehaviourPtr l = LightBehaviourPtr(new LightBehaviour(*this));
    l->setHardwareOutputConfig(outputFunction_dimmer, outputmode_gradual, usage_undefined, false, -1);
    l->setGamma(STANDARD_PWM_GAMMA); // default gamma, assuming analog output is a PWM
    addBehaviour(l);
  }
  else if (mAnalogIOType==analogio_rgbdimmer) {
    // - is light
    mColorClass = class_yellow_light;
    // - need 3 IO names for R,G,B, optional fourth for W
    size_t p;
    p = ioname.find("|");
    if (p!=string::npos) {
      // at least 2 pins specified
      // - create red output
      mAnalogIO = AnalogIoPtr(new AnalogIo(ioname.substr(0,p).c_str(), true, 0));
      ioname.erase(0,p+1);
      p = ioname.find("|");
      if (p!=string::npos) {
        // 3 pins specified
        // - create green output
        mAnalogIO2 = AnalogIoPtr(new AnalogIo(ioname.substr(0,p).c_str(), true, 0));
        ioname.erase(0,p+1);
        p = ioname.find("|");
        if (p!=string::npos) {
          // extra 4th pin for white specified
          // - create white output from rest
          mAnalogIO4 = AnalogIoPtr(new AnalogIo(ioname.substr(p+1).c_str(), true, 0));
          ioname.erase(p); // remove specification of white channel
        }
        // - create blue output from rest
        mAnalogIO3 = AnalogIoPtr(new AnalogIo(ioname.c_str(), true, 0));
        // Complete set of outputs, now create RGB light (with optional white channel)
        // - use color light settings, which include a color scene table
        installSettings(DeviceSettingsPtr(new ColorLightDeviceSettings(*this)));
        // - add multi-channel color light behaviour (which adds a number of auxiliary channels)
        RGBColorLightBehaviourPtr l = RGBColorLightBehaviourPtr(new RGBColorLightBehaviour(*this, false));
        l->setGamma(STANDARD_PWM_GAMMA); // default gamma, assuming analog output are PWMs
        addBehaviour(l);
      }
    }
  }
  else if (mAnalogIOType==analogio_cwwwdimmer) {
    // - is light
    mColorClass = class_yellow_light;
    // - need 2 IO names for CW and WW
    size_t p;
    p = ioname.find("|");
    if (p!=string::npos) {
      // 2 pins specified
      // - create CW output
      mAnalogIO = AnalogIoPtr(new AnalogIo(ioname.substr(0,p).c_str(), true, 0));
      ioname.erase(0,p+1);
      // - create WW output
      mAnalogIO2 = AnalogIoPtr(new AnalogIo(ioname.substr(0,p).c_str(), true, 0));
      // Complete set of outputs, now create CWWW light (with optional white channel)
      // - use color light settings, which include a color scene table
      installSettings(DeviceSettingsPtr(new ColorLightDeviceSettings(*this)));
      // - add CT only color light behaviour (which adds a number of auxiliary channels)
      RGBColorLightBehaviourPtr l = RGBColorLightBehaviourPtr(new RGBColorLightBehaviour(*this, true));
      l->setGamma(STANDARD_PWM_GAMMA); // default gamma, assuming analog outputs are PWMs
      addBehaviour(l);
    }
  }
  else if (mAnalogIOType==analogio_valve) {
    // Analog output as valve controlling output
    mAnalogIO = AnalogIoPtr(new AnalogIo(ioname.c_str(), true, 0));
    // - is heating
    mColorClass = class_blue_climate;
    // - valve needs climate control scene table (ClimateControlScene)
    installSettings(DeviceSettingsPtr(new ClimateDeviceSettings(*this)));
    // - create climate control outout
    OutputBehaviourPtr ob = OutputBehaviourPtr(new ClimateControlBehaviour(*this, climatedevice_simple, hscapability_heatingAndCooling));
    ob->setGroupMembership(group_roomtemperature_control, true); // put into room temperature control group by default, NOT into standard blue)
    ob->setHardwareOutputConfig(outputFunction_positional, outputmode_gradual, usage_room, false, 0);
    ob->setHardwareName("Valve, 0..100");
    addBehaviour(ob);
  }
  else if (mAnalogIOType==analogio_sensor) {
    int sensorType = sensorType_none;
    int sensorUsage = usage_undefined;
    double min = 0;
    double max = 100;
    double resolution = 1;
    double pollIntervalS = 30;
    double outResolution = 0;
    double outMin = NAN;
    double outMax = NAN;
    mScaling = 1;
    mOffset = 0;
    // optionally, sensor can specify details, sensor;<type>;<usage>;<interval>;<scale>;<offset>;<outresolution>;<outmin>;<outmax>
    sscanf(mode.c_str(), "sensor;%d;%d;%lf;%lf;%lf;%lf;%lf;%lf", &sensorType, &sensorUsage, &pollIntervalS, &mScaling, &mOffset, &outResolution, &outMin, &outMax);
    // Analog input as sensor
    mAnalogIO = AnalogIoPtr(new AnalogIo(ioname.c_str(), false, 0));
    // - query native range
    mAnalogIO->getRange(min, max, resolution);
    // custom or derived (from scale/offset) min/max/resolution
    bool definedOutRange = false;
    if (!isnan(outMin)) { min = outMin; definedOutRange = true; } else { min = min*mScaling+mOffset; }
    if (!isnan(outMax)) { max = outMax; definedOutRange = true; } else { max = max*mScaling+mOffset; }
    if (outResolution!=0) resolution = outResolution; else resolution = resolution*mScaling;
    // sensor only, standard settings without scene table
    installSettings();
    // single sensor behaviour
    SensorBehaviourPtr sb = SensorBehaviourPtr(new SensorBehaviour(*this, "")); // automatic
    sb->setHardwareSensorConfig(
      (VdcSensorType)sensorType, (VdcUsageHint)sensorUsage,
      min, max, resolution,
      pollIntervalS*Second, // we will deliver values at this rate
      (pollIntervalS*3>60 ? pollIntervalS*3 : 60)*Second, // push non-change after 60 seconds or 3*poll rate
      0, // no default ChangesOnlyInterval
      definedOutRange // when we set output min,max explicitly, we want actual value be clamped to that range
    );
    addBehaviour(sb);
    // install polling for it
    mTimerTicket.executeOnce(boost::bind(&AnalogIODevice::analogInputPoll, this, _1, _2));
  }
	deriveDsUid();
}

AnalogIODevice::~AnalogIODevice()
{
}



void AnalogIODevice::analogInputPoll(MLTimer &aTimer, MLMicroSeconds aNow)
{
  SensorBehaviourPtr sb = getSensor(0);
  if (sb) {
    // return value with scaling (default==1) and offset (default==0)
    sb->updateSensorValue(mAnalogIO->value()*mScaling+mOffset);
    MainLoop::currentMainLoop().retriggerTimer(aTimer, sb->getUpdateInterval());
  }
}




#define TRANSITION_STEP_TIME (10*MilliSecond)

void AnalogIODevice::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  MLMicroSeconds transitionTime = 0;
  // abort previous transition
  mTimerTicket.cancel();
  // generic device, show changed channels
  if (mAnalogIOType==analogio_dimmer) {
    // single channel PWM dimmer
    LightBehaviourPtr l = getOutput<LightBehaviour>();
    if (l && l->brightnessNeedsApplying()) {
      l->updateBrightnessTransition(); // init
      applyChannelValueSteps(aForDimming);
    }
    // consider applied
    l->brightnessApplied();
  }
  else if (mAnalogIOType==analogio_rgbdimmer || mAnalogIOType==analogio_cwwwdimmer) {
    // three channel RGB PWM dimmer or two channel CWWW PWM dimmer
    RGBColorLightBehaviourPtr cl = getOutput<RGBColorLightBehaviour>();
    if (cl) {
      if (needsToApplyChannels(&transitionTime)) {
        // needs update
        // - derive (possibly new) color mode from changed channels
        cl->deriveColorMode();
        // - calculate and start transition
        cl->updateBrightnessTransition(); // init
        cl->updateColorTransition(); // init
        applyChannelValueSteps(aForDimming);
      } // if needs update
      // consider applied
      cl->appliedColorValues();
    }
  }
  else {
    // direct single channel PWM output, no smooth transitions
    ChannelBehaviourPtr ch = getChannelByIndex(0);
    if (ch && ch->needsApplying()) {
      double chVal = ch->getChannelValue(true)-ch->getMin();
      double chSpan = ch->getMax()-ch->getMin();
      mAnalogIO->setValue(chVal/chSpan*100); // 0..100%
      ch->channelValueApplied(); // confirm having applied the value
    }
  }
  // always consider apply done, even if transition is still running
  inherited::applyChannelValues(aDoneCB, aForDimming);
}



void AnalogIODevice::applyChannelValueSteps(bool aForDimming)
{
  MLMicroSeconds now = MainLoop::now();
  // generic device, show changed channels
  if (mAnalogIOType==analogio_dimmer) {
    // single channel PWM dimmer
    LightBehaviourPtr l = getOutput<LightBehaviour>();
    bool moreSteps = l->updateBrightnessTransition(now);
    double pwm = l->brightnessForHardware(); // includes gamma, which is set to STANDARD_PWM_GAMMA by default
    mAnalogIO->setValue(pwm);
    // next step
    if (moreSteps) {
      OLOG(LOG_DEBUG, "AnalogIO transitional PWM value: %.2f", pwm);
      // not yet complete, schedule next step
      mTimerTicket.executeOnce(
        boost::bind(&AnalogIODevice::applyChannelValueSteps, this, aForDimming),
        TRANSITION_STEP_TIME
      );
      return; // will be called later again
    }
    if (!aForDimming) OLOG(LOG_INFO, "AnalogIO final PWM value: %.2f", pwm);
  }
  else if (mAnalogIOType==analogio_rgbdimmer) {
    // three channel RGB PWM dimmer
    RGBColorLightBehaviourPtr cl = getOutput<RGBColorLightBehaviour>();
    bool moreSteps = cl->updateBrightnessTransition(now);
    if (cl->updateColorTransition(now)) moreSteps = true;
    // RGB lamp, get components
    double r, g, b, pwm;
    double w = 0;
    if (mAnalogIO4) {
      // RGBW lamp
      cl->getRGBW(r, g, b, w, 100, false, true); // get brightness for R,G,B,W channels
      pwm = cl->brightnessToOutput(w, 100);
      mAnalogIO4->setValue(pwm);
    }
    else {
      // RGB only
      cl->getRGB(r, g, b, 100, false, true); // get brightness for R,G,B channels
    }
    // - red
    pwm = cl->brightnessToOutput(r, 100);
    mAnalogIO->setValue(pwm);
    // - green
    pwm = cl->brightnessToOutput(g, 100);
    mAnalogIO2->setValue(pwm);
    // - blue
    pwm = cl->brightnessToOutput(b, 100);
    mAnalogIO3->setValue(pwm);
    // next step
    if (moreSteps) {
      OLOG(LOG_DEBUG, "AnalogIO transitional RGBW values: R=%.2f G=%.2f, B=%.2f, W=%.2f", r, g, b, w);
      // not yet complete, schedule next step
      mTimerTicket.executeOnce(
        boost::bind(&AnalogIODevice::applyChannelValueSteps, this, aForDimming),
        TRANSITION_STEP_TIME
      );
      return; // will be called later again
    }
    if (!aForDimming) OLOG(LOG_INFO, "AnalogIO final RGBW values: R=%.2f G=%.2f, B=%.2f, W=%.2f", r, g, b, w);
  }
  else if (mAnalogIOType==analogio_cwwwdimmer) {
    // two channel RGB PWM dimmer
    RGBColorLightBehaviourPtr cl = getOutput<RGBColorLightBehaviour>();
    bool moreSteps = cl->updateBrightnessTransition(now);
    if (cl->updateColorTransition(now)) moreSteps = true;
    // CWWW lamp, get components
    double cw,ww, pwm;
    cl->getCWWW(cw, ww, 100, true);
    pwm = cl->brightnessToOutput(cw, 100);
    mAnalogIO->setValue(pwm);
    pwm = cl->brightnessToOutput(ww, 100);
    mAnalogIO2->setValue(pwm);
    // next step
    if (moreSteps) {
      OLOG(LOG_DEBUG, "AnalogIO transitional CWWW values: CW=%.2f WW=%.2f", cw, ww);
      // not yet complete, schedule next step
      mTimerTicket.executeOnce(
        boost::bind(&AnalogIODevice::applyChannelValueSteps, this, aForDimming),
        TRANSITION_STEP_TIME
      );
      return; // will be called later again
    }
    if (!aForDimming) OLOG(LOG_INFO, "AnalogIO final CWWW values: CW=%.2f, WW=%.2f", cw, ww);
  }
}




void AnalogIODevice::deriveDsUid()
{
  // vDC implementation specific UUID:
  //   UUIDv5 with name = classcontainerinstanceid::ioname[:ioname ...]
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  string s = mVdcP->vdcInstanceIdentifier();
  string_format_append(s, ":%d:", (int)mAnalogIOType);
  if (mAnalogIO) { s += ":"; s += mAnalogIO->getName(); }
  if (mAnalogIO2) { s += ":"; s += mAnalogIO2->getName(); }
  if (mAnalogIO3) { s += ":"; s += mAnalogIO3->getName(); }
  if (mAnalogIO4) { s += ":"; s += mAnalogIO4->getName(); }
  mDSUID.setNameInSpace(s, vdcNamespace);
}


string AnalogIODevice::modelName()
{
  if (mAnalogIOType==analogio_dimmer)
    return "Dimmer output";
  if (mAnalogIOType==analogio_cwwwdimmer)
    return "CW/WW dimmer outputs";
  if (mAnalogIOType==analogio_rgbdimmer)
    return "RGB(W) dimmer outputs";
  if (mAnalogIOType==analogio_valve)
    return "Heating Valve output";
  return "Analog I/O";
}


string AnalogIODevice::getExtraInfo()
{
  if (mAnalogIOType==analogio_rgbdimmer)
    return string_format("RGB Outputs:%s, %s, %s; White:%s", mAnalogIO->getName().c_str(), mAnalogIO2->getName().c_str(), mAnalogIO3->getName().c_str(), mAnalogIO4 ? mAnalogIO4->getName().c_str() : "none");
  if (mAnalogIOType==analogio_cwwwdimmer)
    return string_format("CW/WW Outputs:%s, %s", mAnalogIO->getName().c_str(), mAnalogIO2->getName().c_str());
  if (mAnalogIOType==analogio_dimmer || mAnalogIOType==analogio_valve)
    return string_format("Output: %s", mAnalogIO->getName().c_str());
  return "Analog I/O";
}



string AnalogIODevice::description()
{
  string s = inherited::description();
  if (mAnalogIOType==analogio_dimmer)
    string_format_append(s, "\n- Dimmer at Analog output '%s'", mAnalogIO->getName().c_str());
  else if (mAnalogIOType==analogio_cwwwdimmer)
    string_format_append(s, "\n- Tunable White Dimmer with CW/WW outputs '%s'/'%s'", mAnalogIO->getName().c_str(), mAnalogIO2->getName().c_str());
  else if (mAnalogIOType==analogio_rgbdimmer)
    string_format_append(s, "\n- Color Dimmer with RGB outputs '%s', '%s', '%s'; White: '%s'", mAnalogIO->getName().c_str(), mAnalogIO2->getName().c_str(), mAnalogIO3->getName().c_str(), mAnalogIO4 ? mAnalogIO4->getName().c_str() : "none");
  else if (mAnalogIOType==analogio_valve)
    string_format_append(s, "\nHeating Valve @ '%s'", mAnalogIO->getName().c_str());
  else if (mAnalogIOType==analogio_sensor)
    string_format_append(s, "\nSensor @ '%s', scaling=%.4f, offset=%.1f", mAnalogIO->getName().c_str(), mScaling, mOffset);
  return s;
}


#endif // ENABLE_STATIC
