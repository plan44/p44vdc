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

#include "dmxdevice.hpp"

#if ENABLE_OLA || ENABLE_DMX

#include "lightbehaviour.hpp"
#include "colorlightbehaviour.hpp"
#include "movinglightbehaviour.hpp"


using namespace p44;


// MARK: - DmxDevice


static bool nextChannelSpec(string &aConfig, size_t &aStartPos, char &aChannelType, DmxChannel &aChannelNo, DmxValue &aDefaultValue)
{
  // check for channel spec
  // syntax is: C=n[=v][,C=n[=v],...] where C=channel type character, n=channel number, v=default value (if missing, default value is 0)
  size_t i = aConfig.find("=", aStartPos);
  if (i==string::npos || i==0) return false;
  // first char before = is channel type
  aChannelType = aConfig[aStartPos];
  // after =, there must be a channel number
  int n;
  if (sscanf(aConfig.c_str()+i+1, "%d", &n)!=1) return false;
  aChannelNo = n;
  // find next comma
  size_t e = aConfig.find(",", i);
  // check for default value
  aDefaultValue = 0; // zero by default
  i = aConfig.find("=", i+1); // second equal sign?
  if (i!=string::npos && (e==string::npos || i<e)) {
    // default value must follow
    if (sscanf(aConfig.c_str()+i+1, "%d", &n)!=1) return false;
    aDefaultValue = n;
  }
  // skip to beginning of next item (or end)
  if (e==string::npos)
    aStartPos = aConfig.size();
  else
    aStartPos = e+1; // next item after ,
  return true;
}


DmxDevice::DmxDevice(DmxVdc *aVdcP, const string &aDeviceConfig) :
  inherited(aVdcP),
  mDmxType(dmx_unknown),
  mWhiteChannel(dmxNone),
  mRedChannel(dmxNone),
  mGreenChannel(dmxNone),
  mBlueChannel(dmxNone),
  mAmberChannel(dmxNone)
{
  // evaluate config
  string config = aDeviceConfig;
  string mode = "dimmer"; // default to dimmer
  size_t i = aDeviceConfig.find(":");
  if (i!=string::npos) {
    mode = aDeviceConfig.substr(0,i);
    config = aDeviceConfig.substr(i+1,string::npos);
  }
  if (mode=="dimmer")
    mDmxType = dmx_dimmer;
  else if (mode=="tunablewhite")
    mDmxType = dmx_tunablewhitedimmer;
  else if (mode=="color")
    mDmxType = dmx_fullcolordimmer;
  else {
    LOG(LOG_ERR, "unknown DMX device type: %s", mode.c_str());
  }
  // by default, act as black device so we can configure colors
  setColorClass(class_black_joker);
  // get DMX channels specifications
  char channelType;
  DmxChannel channelNo;
  DmxValue defaultValue;
  size_t p = 0;
  while (nextChannelSpec(config, p, channelType, channelNo, defaultValue)) {
    switch (channelType) {
      case 'W' : mWhiteChannel = channelNo; break;
      case 'R' : mRedChannel = channelNo; break;
      case 'G' : mGreenChannel = channelNo; break;
      case 'B' : mBlueChannel = channelNo; break;
      case 'A' : mAmberChannel = channelNo; break;
      case 'H' : mHPosChannel = channelNo; break;
      case 'V' : mVPosChannel = channelNo; break;
      default : break; // static channel, just set default once
    }
    // set initial default value (will stay in the buffer)
    setDMXChannel(channelNo, defaultValue);
  }
  // now create device according to type
  if (mDmxType==dmx_dimmer) {
    // Single channel DMX512 dimmer, only use white channel
    // - is light
    setColorClass(class_yellow_light);
    // - use light settings, which include a scene table
    installSettings(DeviceSettingsPtr(new LightDeviceSettings(*this)));
    // - add simple single-channel light behaviour
    LightBehaviourPtr l = LightBehaviourPtr(new LightBehaviour(*this));
    l->setHardwareOutputConfig(outputFunction_dimmer, outputmode_gradual, usage_undefined, false, -1);
    addBehaviour(l);
  }
  else if (mDmxType==dmx_fullcolordimmer) {
    // - is RGB
    setColorClass(class_yellow_light);
    if (mRedChannel!=dmxNone && mGreenChannel!=dmxNone && mBlueChannel!=dmxNone) {
      // Complete set of outputs to create RGB light
      if (mHPosChannel!=dmxNone || mVPosChannel!=dmxNone) {
        // also has position, use moving light behaviour
        installSettings(DeviceSettingsPtr(new MovingLightDeviceSettings(*this)));
        // - add moving color light behaviour
        MovingLightBehaviourPtr ml = MovingLightBehaviourPtr(new MovingLightBehaviour(*this, false));
        addBehaviour(ml);
      }
      else {
        // just color light settings, which include a color scene table
        installSettings(DeviceSettingsPtr(new ColorLightDeviceSettings(*this)));
        // - add multi-channel color light behaviour (which adds a number of auxiliary channels)
        RGBColorLightBehaviourPtr l = RGBColorLightBehaviourPtr(new RGBColorLightBehaviour(*this, false));
        addBehaviour(l);
      }
    }
  }
  deriveDsUid();
}


bool DmxDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Nothing to do to identify for now
  return true; // simple identification, callback will not be called
}



bool DmxDevice::isSoftwareDisconnectable()
{
  return mDmxDeviceRowID>0; // disconnectable by software if it was created from DB entry (and not on the command line)
}

DmxVdc &DmxDevice::getDmxVdc()
{
  return *(static_cast<DmxVdc *>(mVdcP));
}


void DmxDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  // clear learn-in data from DB
  if (mDmxDeviceRowID) {
    if(getDmxVdc().mDb.executef("DELETE FROM devConfigs WHERE rowid=%lld", mDmxDeviceRowID)!=SQLITE_OK) {
      OLOG(LOG_ERR, "Error deleting device: %s", getDmxVdc().mDb.error()->description().c_str());
    }
  }
  // disconnection is immediate, so we can call inherited right now
  inherited::disconnect(aForgetParams, aDisconnectResultHandler);
}


void DmxDevice::setDMXChannel(DmxChannel aChannel, DmxValue aChannelValue)
{
  getDmxVdc().setDMXChannel(aChannel, aChannelValue);
}


#define TRANSITION_STEP_TIME (10*MilliSecond)

void DmxDevice::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  // abort previous transition
  mTransitionTicket.cancel();
  // generic device, show changed channels
  if (mDmxType==dmx_dimmer) {
    // single channel dimmer
    LightBehaviourPtr l = getOutput<LightBehaviour>();
    if (l && l->brightnessNeedsApplying()) {
      l->updateBrightnessTransition(); // init
      applyChannelValueSteps(aForDimming);
    }
    // consider applied
    l->brightnessApplied();
  }
  else if (mDmxType==dmx_fullcolordimmer) {
    // RGB, RGBW or RGBWA dimmer
    RGBColorLightBehaviourPtr cl = getOutput<RGBColorLightBehaviour>();
    if (cl) {
      MovingLightBehaviourPtr ml = getOutput<MovingLightBehaviour>();
      if (needsToApplyChannels()) {
        // needs update
        // - derive (possibly new) color mode from changed channels
        cl->deriveColorMode();
        // - calculate and start transition
        cl->updateBrightnessTransition(); // init
        cl->updateColorTransition(); // init
        if (ml) ml->updatePositionTransition(); // init
        applyChannelValueSteps(aForDimming);
      }
      // consider applied
      if (ml) ml->appliedPosition();
      cl->appliedColorValues();
    }
  }
  inherited::applyChannelValues(aDoneCB, aForDimming);
}


void DmxDevice::applyChannelValueSteps(bool aForDimming)
{
  MLMicroSeconds now = MainLoop::now();
  // generic device, show changed channels
  if (mDmxType==dmx_dimmer) {
    // single channel dimmer
    LightBehaviourPtr l = getOutput<LightBehaviour>();
    bool moreSteps = l->updateBrightnessTransition(now);
    double w = l->brightnessForHardware()*255/100;
    setDMXChannel(mWhiteChannel,(DmxValue)w);
    // next step
    if (moreSteps) {
      OLOG(LOG_DEBUG, "transitional DMX512 value %d=%d", mWhiteChannel, (int)w);
      // not yet complete, schedule next step
      mTransitionTicket.executeOnce(
        boost::bind(&DmxDevice::applyChannelValueSteps, this, aForDimming),
        TRANSITION_STEP_TIME
      );
      return; // will be called later again
    }
    if (!aForDimming) {
      OLOG(LOG_INFO, "final DMX512 channel %d=%d", mWhiteChannel, (int)w);
    }
    l->brightnessApplied(); // confirm having applied the new brightness
  }
  else if (mDmxType==dmx_fullcolordimmer) {
    // RGB, RGBW or RGBWA dimmer
    RGBColorLightBehaviourPtr cl = getOutput<RGBColorLightBehaviour>();
    MovingLightBehaviourPtr ml = getOutput<MovingLightBehaviour>();
    bool moreSteps = cl->updateBrightnessTransition(now);
    if (cl->updateColorTransition(now)) moreSteps = true;
    if (ml && ml->updatePositionTransition(now)) moreSteps = true;
    // RGB lamp, get components
    double r,g,b;
    double w = 0;
    double a = 0;
    if (mWhiteChannel!=dmxNone) {
      if (mAmberChannel!=dmxNone) {
        // RGBW
        cl->getRGBWA(r, g, b, w, a, 255, false, true);
        setDMXChannel(mAmberChannel,(DmxValue)a);
      }
      else {
        // RGBW
        cl->getRGBW(r, g, b, w, 255, false, true);
      }
      setDMXChannel(mWhiteChannel,(DmxValue)w);
    }
    else {
      // RGB
      cl->getRGB(r, g, b, 255, false, true); // get brightness per R,G,B channel
    }
    // There's always RGB
    setDMXChannel(mRedChannel,(DmxValue)r);
    setDMXChannel(mGreenChannel,(DmxValue)g);
    setDMXChannel(mBlueChannel,(DmxValue)b);
    // there might be position as well
    double h = 0;
    double v = 0;
    if (ml) {
      h = ml->horizontalPosition->getChannelValue(true)/100*255;
      setDMXChannel(mHPosChannel,(DmxValue)h);
      v = ml->verticalPosition->getChannelValue(true)/100*255;
      setDMXChannel(mVPosChannel,(DmxValue)v);
    }
    // next step
    if (moreSteps) {
      OLOG(LOG_DEBUG,
        "transitional DMX512 values R(%hd)=%d, G(%hd)=%d, B(%hd)=%d, W(%hd)=%d, A(%hd)=%d, H(%hd)=%d, V(%hd)=%d",
        mRedChannel, (int)r, mGreenChannel, (int)g, mBlueChannel, (int)b,
        mWhiteChannel, (int)w, mAmberChannel, (int)a,
        mHPosChannel, (int)h, mVPosChannel, (int)v
      );
      // not yet complete, schedule next step
      mTransitionTicket.executeOnce(
        boost::bind(&DmxDevice::applyChannelValueSteps, this, aForDimming),
        TRANSITION_STEP_TIME
      );
      return; // will be called later again
    }
    if (!aForDimming) {
      OLOG(LOG_INFO,
        "final DMX512 values R(%hd)=%d, G(%hd)=%d, B(%hd)=%d, W(%hd)=%d, A(%hd)=%d, H(%hd)=%d, V(%hd)=%d",
        mRedChannel, (int)r, mGreenChannel, (int)g, mBlueChannel, (int)b,
        mWhiteChannel, (int)w, mAmberChannel, (int)a,
        mHPosChannel, (int)h, mVPosChannel, (int)v
      );
    }
  }
}



void DmxDevice::deriveDsUid()
{
  // vDC implementation specific UUID:
  //   UUIDv5 with name = classcontainerinstanceid::dmxType:white[:red:green:blue][:amber]
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  string s = mVdcP->vdcInstanceIdentifier();
  string_format_append(s, ":%d:%d", (int)mDmxType, mWhiteChannel);
  if (mDmxType==dmx_fullcolordimmer)
    string_format_append(s, ":%d:%d:%d", mRedChannel, mGreenChannel, mBlueChannel);
  if (mAmberChannel!=dmxNone)
    string_format_append(s, ":%d", mAmberChannel);
  mDSUID.setNameInSpace(s, vdcNamespace);
}


string DmxDevice::modelName()
{
  if (mDmxType==dmx_dimmer)
    return "DMX512 Dimmer";
  else if (mDmxType==dmx_tunablewhitedimmer)
    return "DMX512 Tunable white";
  else if (mDmxType==dmx_fullcolordimmer)
    return "DMX512 Full color";
  return "DMX512 device";
}



bool DmxDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  const char *iconName = NULL;
  switch (mDmxType) {
    case dmx_dimmer: iconName = "dmx_dimmer"; break;
    case dmx_tunablewhitedimmer: iconName = "dmx_ct"; break;
    case dmx_fullcolordimmer: iconName = "dmx_color"; break;
    default: break;
  }
  if (iconName && getIcon(iconName, aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


string DmxDevice::getExtraInfo()
{
  string s;
  if (mDmxType==dmx_dimmer)
    s = string_format("DMX512 Dimmer: brightness=%d", mWhiteChannel);
  else if (mDmxType==dmx_tunablewhitedimmer)
    s = string_format("DMX512 Tunable white dimmer: white=%d, amber=%d", mWhiteChannel, mAmberChannel);
  else if (mDmxType==dmx_fullcolordimmer)
    s = string_format("DMX512 Full color dimmer: RGB=%d,%d,%d, white=%d, amber=%d", mRedChannel, mGreenChannel, mBlueChannel, mWhiteChannel, mAmberChannel);
  else
    s = "DMX512 device";
  if (mHPosChannel!=dmxNone || mVPosChannel!=dmxNone)
    string_format_append(s, " with position: h=%d, v=%d", mHPosChannel, mVPosChannel);
  return s;
}



string DmxDevice::description()
{
  string s = inherited::description();
  if (mDmxType==dmx_dimmer)
    string_format_append(s, "\n- DMX512 Dimmer: brightness=%d", mWhiteChannel);
  else if (mDmxType==dmx_tunablewhitedimmer)
    string_format_append(s, "\n- DMX512 Tunable white dimmer: white=%d, amber=%d", mWhiteChannel, mAmberChannel);
  else if (mDmxType==dmx_fullcolordimmer)
    string_format_append(s, "\n- DMX512 Full color dimmer: RGB=%d,%d,%d, white=%d, amber=%d", mRedChannel, mGreenChannel, mBlueChannel, mWhiteChannel, mAmberChannel);
  if (mHPosChannel!=dmxNone || mVPosChannel!=dmxNone)
    string_format_append(s, "\n- With position: horizontal=%d, vertical=%d", mHPosChannel, mVPosChannel);
  return s;
}

#endif // ENABLE_OLA || ENABLE_DMX



