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

#include "oladevice.hpp"

#if ENABLE_OLA

#include "lightbehaviour.hpp"
#include "colorlightbehaviour.hpp"
#include "movinglightbehaviour.hpp"


using namespace p44;


// MARK: - OlaDevice


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


OlaDevice::OlaDevice(OlaVdc *aVdcP, const string &aDeviceConfig) :
  inherited(aVdcP),
  olaType(ola_unknown),
  whiteChannel(dmxNone),
  redChannel(dmxNone),
  greenChannel(dmxNone),
  blueChannel(dmxNone),
  amberChannel(dmxNone)
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
    olaType = ola_dimmer;
  else if (mode=="tunablewhite")
    olaType = ola_tunablewhitedimmer;
  else if (mode=="color")
    olaType = ola_fullcolordimmer;
  else {
    LOG(LOG_ERR, "unknown OLA device type: %s", mode.c_str());
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
      case 'W' : whiteChannel = channelNo; break;
      case 'R' : redChannel = channelNo; break;
      case 'G' : greenChannel = channelNo; break;
      case 'B' : blueChannel = channelNo; break;
      case 'A' : amberChannel = channelNo; break;
      case 'H' : hPosChannel = channelNo; break;
      case 'V' : vPosChannel = channelNo; break;
      default : break; // static channel, just set default once
    }
    // set initial default value (will stay in the buffer)
    setDMXChannel(channelNo, defaultValue);
  }
  // now create device according to type
  if (olaType==ola_dimmer) {
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
  else if (olaType==ola_fullcolordimmer) {
    // - is RGB
    setColorClass(class_yellow_light);
    if (redChannel!=dmxNone && greenChannel!=dmxNone && blueChannel!=dmxNone) {
      // Complete set of outputs to create RGB light
      if (hPosChannel!=dmxNone || vPosChannel!=dmxNone) {
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


bool OlaDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Nothing to do to identify for now
  return true; // simple identification, callback will not be called
}



bool OlaDevice::isSoftwareDisconnectable()
{
  return olaDeviceRowID>0; // disconnectable by software if it was created from DB entry (and not on the command line)
}

OlaVdc &OlaDevice::getOlaVdc()
{
  return *(static_cast<OlaVdc *>(vdcP));
}


void OlaDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  // clear learn-in data from DB
  if (olaDeviceRowID) {
    if(getOlaVdc().db.executef("DELETE FROM devConfigs WHERE rowid=%lld", olaDeviceRowID)!=SQLITE_OK) {
      OLOG(LOG_ERR, "Error deleting device: %s", getOlaVdc().db.error()->description().c_str());
    }
  }
  // disconnection is immediate, so we can call inherited right now
  inherited::disconnect(aForgetParams, aDisconnectResultHandler);
}


void OlaDevice::setDMXChannel(DmxChannel aChannel, DmxValue aChannelValue)
{
  getOlaVdc().setDMXChannel(aChannel, aChannelValue);
}


#define TRANSITION_STEP_TIME (10*MilliSecond)

void OlaDevice::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  MLMicroSeconds transitionTime = 0;
  // abort previous transition
  transitionTicket.cancel();
  // generic device, show changed channels
  if (olaType==ola_dimmer) {
    // single channel dimmer
    LightBehaviourPtr l = getOutput<LightBehaviour>();
    if (l && l->brightnessNeedsApplying()) {
      transitionTime = l->transitionTimeToNewBrightness();
      l->brightnessTransitionStep(); // init
      applyChannelValueSteps(aForDimming, transitionTime==0 ? 1 : (double)TRANSITION_STEP_TIME/transitionTime);
    }
    // consider applied
    l->brightnessApplied();
  }
  else if (olaType==ola_fullcolordimmer) {
    // RGB, RGBW or RGBWA dimmer
    RGBColorLightBehaviourPtr cl = getOutput<RGBColorLightBehaviour>();
    if (cl) {
      MovingLightBehaviourPtr ml = getOutput<MovingLightBehaviour>();
      if (needsToApplyChannels(&transitionTime)) {
        // needs update
        // - derive (possibly new) color mode from changed channels
        cl->deriveColorMode();
        // - calculate and start transition
        cl->brightnessTransitionStep(); // init
        cl->colorTransitionStep(); // init
        if (ml) ml->positionTransitionStep(); // init
        applyChannelValueSteps(aForDimming, transitionTime==0 ? 1 : (double)TRANSITION_STEP_TIME/transitionTime);
      }
      // consider applied
      if (ml) ml->appliedPosition();
      cl->appliedColorValues();
    }
  }
  inherited::applyChannelValues(aDoneCB, aForDimming);
}


void OlaDevice::applyChannelValueSteps(bool aForDimming, double aStepSize)
{
  // generic device, show changed channels
  if (olaType==ola_dimmer) {
    // single channel dimmer
    LightBehaviourPtr l = getOutput<LightBehaviour>();
    bool moreSteps = l->brightnessTransitionStep(aStepSize);
    double w = l->brightnessForHardware()*255/100;
    setDMXChannel(whiteChannel,(DmxValue)w);
    // next step
    if (moreSteps) {
      OLOG(LOG_DEBUG, "transitional DMX512 value %d=%d", whiteChannel, (int)w);
      // not yet complete, schedule next step
      transitionTicket.executeOnce(
        boost::bind(&OlaDevice::applyChannelValueSteps, this, aForDimming, aStepSize),
        TRANSITION_STEP_TIME
      );
      return; // will be called later again
    }
    if (!aForDimming) {
      OLOG(LOG_INFO, "final DMX512 channel %d=%d", whiteChannel, (int)w);
    }
    l->brightnessApplied(); // confirm having applied the new brightness
  }
  else if (olaType==ola_fullcolordimmer) {
    // RGB, RGBW or RGBWA dimmer
    RGBColorLightBehaviourPtr cl = getOutput<RGBColorLightBehaviour>();
    MovingLightBehaviourPtr ml = getOutput<MovingLightBehaviour>();
    bool moreSteps = cl->brightnessTransitionStep(aStepSize);
    if (cl->colorTransitionStep(aStepSize)) moreSteps = true;
    if (ml && ml->positionTransitionStep(aStepSize)) moreSteps = true;
    // RGB lamp, get components
    double r,g,b;
    double w = 0;
    double a = 0;
    if (whiteChannel!=dmxNone) {
      if (amberChannel!=dmxNone) {
        // RGBW
        cl->getRGBWA(r, g, b, w, a, 255, false, true);
        setDMXChannel(amberChannel,(DmxValue)a);
      }
      else {
        // RGBW
        cl->getRGBW(r, g, b, w, 255, false, true);
      }
      setDMXChannel(whiteChannel,(DmxValue)w);
    }
    else {
      // RGB
      cl->getRGB(r, g, b, 255, false, true); // get brightness per R,G,B channel
    }
    // There's always RGB
    setDMXChannel(redChannel,(DmxValue)r);
    setDMXChannel(greenChannel,(DmxValue)g);
    setDMXChannel(blueChannel,(DmxValue)b);
    // there might be position as well
    double h = 0;
    double v = 0;
    if (ml) {
      h = ml->horizontalPosition->getTransitionalValue()/100*255;
      setDMXChannel(hPosChannel,(DmxValue)h);
      v = ml->verticalPosition->getTransitionalValue()/100*255;
      setDMXChannel(vPosChannel,(DmxValue)v);
    }
    // next step
    if (moreSteps) {
      OLOG(LOG_DEBUG,
        "transitional DMX512 values R(%hd)=%d, G(%hd)=%d, B(%hd)=%d, W(%hd)=%d, A(%hd)=%d, H(%hd)=%d, V(%hd)=%d",
        redChannel, (int)r, greenChannel, (int)g, blueChannel, (int)b,
        whiteChannel, (int)w, amberChannel, (int)a,
        hPosChannel, (int)h, vPosChannel, (int)v
      );
      // not yet complete, schedule next step
      transitionTicket.executeOnce(
        boost::bind(&OlaDevice::applyChannelValueSteps, this, aForDimming, aStepSize),
        TRANSITION_STEP_TIME
      );
      return; // will be called later again
    }
    if (!aForDimming) {
      OLOG(LOG_INFO,
        "final DMX512 values R(%hd)=%d, G(%hd)=%d, B(%hd)=%d, W(%hd)=%d, A(%hd)=%d, H(%hd)=%d, V(%hd)=%d",
        redChannel, (int)r, greenChannel, (int)g, blueChannel, (int)b,
        whiteChannel, (int)w, amberChannel, (int)a,
        hPosChannel, (int)h, vPosChannel, (int)v
      );
    }
  }
}



void OlaDevice::deriveDsUid()
{
  // vDC implementation specific UUID:
  //   UUIDv5 with name = classcontainerinstanceid::olaType:white[:red:green:blue][:amber]
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  string s = vdcP->vdcInstanceIdentifier();
  string_format_append(s, ":%d:%d", (int)olaType, whiteChannel);
  if (olaType==ola_fullcolordimmer)
    string_format_append(s, ":%d:%d:%d", redChannel, greenChannel, blueChannel);
  if (amberChannel!=dmxNone)
    string_format_append(s, ":%d", amberChannel);
  dSUID.setNameInSpace(s, vdcNamespace);
}


string OlaDevice::modelName()
{
  if (olaType==ola_dimmer)
    return "DMX512 Dimmer";
  else if (olaType==ola_tunablewhitedimmer)
    return "DMX512 Tunable white";
  else if (olaType==ola_fullcolordimmer)
    return "DMX512 Full color";
  return "DMX512 device";
}



bool OlaDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  const char *iconName = NULL;
  switch (olaType) {
    case ola_dimmer: iconName = "ola_dimmer"; break;
    case ola_tunablewhitedimmer: iconName = "ola_ct"; break;
    case ola_fullcolordimmer: iconName = "ola_color"; break;
    default: break;
  }
  if (iconName && getIcon(iconName, aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


string OlaDevice::getExtraInfo()
{
  string s;
  if (olaType==ola_dimmer)
    s = string_format("DMX512 Dimmer: brightness=%d", whiteChannel);
  else if (olaType==ola_tunablewhitedimmer)
    s = string_format("DMX512 Tunable white dimmer: white=%d, amber=%d", whiteChannel, amberChannel);
  else if (olaType==ola_fullcolordimmer)
    s = string_format("DMX512 Full color dimmer: RGB=%d,%d,%d, white=%d, amber=%d", redChannel, greenChannel, blueChannel, whiteChannel, amberChannel);
  else
    s = "DMX512 device";
  if (hPosChannel!=dmxNone || vPosChannel!=dmxNone)
    string_format_append(s, " with position: h=%d, v=%d", hPosChannel, vPosChannel);
  return s;
}



string OlaDevice::description()
{
  string s = inherited::description();
  if (olaType==ola_dimmer)
    string_format_append(s, "\n- DMX512 Dimmer: brightness=%d", whiteChannel);
  else if (olaType==ola_tunablewhitedimmer)
    string_format_append(s, "\n- DMX512 Tunable white dimmer: white=%d, amber=%d", whiteChannel, amberChannel);
  else if (olaType==ola_fullcolordimmer)
    string_format_append(s, "\n- DMX512 Full color dimmer: RGB=%d,%d,%d, white=%d, amber=%d", redChannel, greenChannel, blueChannel, whiteChannel, amberChannel);
  if (hPosChannel!=dmxNone || vPosChannel!=dmxNone)
    string_format_append(s, "\n- With position: horizontal=%d, vertical=%d", hPosChannel, vPosChannel);
  return s;
}

#endif // ENABLE_OLA



