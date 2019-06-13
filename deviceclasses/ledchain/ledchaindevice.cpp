//
//  Copyright (c) 1-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "ledchaindevice.hpp"

#if ENABLE_LEDCHAIN


#include "lightbehaviour.hpp"
#include "colorlightbehaviour.hpp"


using namespace p44;


// MARK: - LedChainDevice


LedChainDevice::LedChainDevice(LedChainVdc *aVdcP, uint16_t aFirstLED, uint16_t aNumLEDs, const string &aDeviceConfig) :
  inherited(aVdcP),
  firstLED(aFirstLED),
  numLEDs(aNumLEDs),
  startSoftEdge(0),
  endSoftEdge(0),
  r(0), g(0), b(0)
{
  // type:config_for_type
  // Where:
  //  with type=segment
  //  config=b:e
  //   b:0..n size of softedge at beginning
  //   e:0..n size of softedge at end
  // evaluate config
  string config = aDeviceConfig;
  string mode, s;
  size_t i = config.find(":");
  ledchainType = ledchain_unknown;
  bool configOK = false;
  if (i!=string::npos) {
    mode = config.substr(0,i);
    config.erase(0,i+1);
  }
  if (mode=="segment") {
    ledchainType = ledchain_softsegment;
    i = config.find(":");
    if (i!=string::npos) {
      s = config.substr(0,i);
      config.erase(0,i+1);
      if (sscanf(s.c_str(), "%hd", &startSoftEdge)==1) {
        if (sscanf(config.c_str(), "%hd", &endSoftEdge)==1) {
          // complete config
          if (startSoftEdge+endSoftEdge<=numLEDs) {
            // correct config
            configOK = true;
          }
        }
      }
    }
  }
  if (!configOK) {
    LOG(LOG_ERR, "invalid LedChain device config: %s", aDeviceConfig.c_str());
  }
  // - is RGB
  colorClass = class_yellow_light;
  // just color light settings, which include a color scene table
  installSettings(DeviceSettingsPtr(new ColorLightDeviceSettings(*this)));
  // - add multi-channel color light behaviour (which adds a number of auxiliary channels)
  RGBColorLightBehaviourPtr l = RGBColorLightBehaviourPtr(new RGBColorLightBehaviour(*this, false));
  // - set minimum brightness
  l->initMinBrightness(getLedChainVdc().getMinBrightness());
  addBehaviour(l);
  // - create dSUID
  deriveDsUid();
}


bool LedChainDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Nothing to do to identify for now
  return true; // simple identification, callback will not be called
}


bool LedChainDevice::isSoftwareDisconnectable()
{
  return true; // these are always software disconnectable
}

LedChainVdc &LedChainDevice::getLedChainVdc()
{
  return *(static_cast<LedChainVdc *>(vdcP));
}


void LedChainDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  // clear learn-in data from DB
  if (ledChainDeviceRowID) {
    if(getLedChainVdc().db.executef("DELETE FROM devConfigs WHERE rowid=%lld", ledChainDeviceRowID)!=SQLITE_OK) {
      ALOG(LOG_ERR, "Error deleting led chain device: %s", getLedChainVdc().db.error()->description().c_str());
    }
  }
  // disconnection is immediate, so we can call inherited right now
  inherited::disconnect(aForgetParams, aDisconnectResultHandler);
}


#define TRANSITION_STEP_TIME (10*MilliSecond)

void LedChainDevice::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  MLMicroSeconds transitionTime = 0;
  // abort previous transition
  transitionTicket.cancel();
  // full color device
  RGBColorLightBehaviourPtr cl = getOutput<RGBColorLightBehaviour>();
  if (cl) {
    if (needsToApplyChannels()) {
      // needs update
      // - derive (possibly new) color mode from changed channels
      cl->deriveColorMode();
      // - calculate and start transition
      //   TODO: depending to what channel has changed, take transition time from that channel. For now always using brightness transition time
      transitionTime = cl->transitionTimeToNewBrightness();
      cl->brightnessTransitionStep(); // init
      cl->colorTransitionStep(); // init
      applyChannelValueSteps(aForDimming, transitionTime==0 ? 1 : (double)TRANSITION_STEP_TIME/transitionTime);
    }
    // consider applied
    cl->appliedColorValues();
  }
  inherited::applyChannelValues(aDoneCB, aForDimming);
}


void LedChainDevice::applyChannelValueSteps(bool aForDimming, double aStepSize)
{
  // RGB, RGBW or RGBWA dimmer
  RGBColorLightBehaviourPtr cl = getOutput<RGBColorLightBehaviour>();
  bool moreSteps = cl->colorTransitionStep(aStepSize);
  if (cl->brightnessTransitionStep(aStepSize)) moreSteps = true;
  // RGB lamp, get components for rendering loop
  if (getLedChainVdc().hasWhite()) {
    cl->getRGBW(r, g, b, w, 255); // get brightness per R,G,B,W channel
  }
  else {
    cl->getRGB(r, g, b, 255); // get brightness per R,G,B channel
    w = 0;
  }
  // trigger rendering the LEDs soon
  getLedChainVdc().triggerRenderingRange(firstLED, numLEDs);
  // next step
  if (moreSteps) {
    ALOG(LOG_DEBUG, "LED chain transitional values R=%d, G=%d, B=%d", (int)r, (int)g, (int)b);
    // not yet complete, schedule next step
    transitionTicket.executeOnce(
      boost::bind(&LedChainDevice::applyChannelValueSteps, this, aForDimming, aStepSize),
      TRANSITION_STEP_TIME
    );
    return; // will be called later again
  }
  if (!aForDimming) {
    ALOG(LOG_INFO, "LED chain final values R=%d, G=%d, B=%d", (int)r, (int)g, (int)b);
  }
}


double LedChainDevice::getLEDColor(uint16_t aLedNumber, uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue, uint8_t &aWhite)
{
  // index relative to beginning of my segment
  uint16_t i = aLedNumber-firstLED;
  if (i<0 || i>=numLEDs)
    return 0; // no color at this point
  // color at this point
  aRed = r; aGreen = g; aBlue = b; aWhite = w;
  // for soft edges
  if (i>=startSoftEdge && i<=numLEDs-endSoftEdge) {
    // not withing soft edge range, full opacity
    return 1;
  }
  else {
    if (i<startSoftEdge) {
      // zero point is LED *before* first LED!
      return 1.0/(startSoftEdge+1)*(i+1);
    }
    else {
      // zero point is LED *after* last LED!
      return 1.0/(endSoftEdge+1)*(numLEDs-i);
    }
  }
}


void LedChainDevice::deriveDsUid()
{
  // vDC implementation specific UUID:
  //   UUIDv5 with name = classcontainerinstanceid::ledchainType:firstLED:lastLED
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  string s = vdcP->vdcInstanceIdentifier();
  string_format_append(s, "%d:%d:%d", ledchainType, firstLED, numLEDs);
  dSUID.setNameInSpace(s, vdcNamespace);
}


string LedChainDevice::modelName()
{
  if (ledchainType==ledchain_softsegment)
    return "LED Chain Segment";
  return "LedChain device";
}



bool LedChainDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  const char *iconName = "rgbchain";
  if (iconName && getIcon(iconName, aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


string LedChainDevice::getExtraInfo()
{
  string s;
  s = string_format("Led Chain Color Light from LED #%d..%d", firstLED, firstLED+numLEDs-1);
  return s;
}



string LedChainDevice::description()
{
  string s = inherited::description();
  string_format_append(s, "\n- Led Chain Color Light from LED #%d..%d", firstLED, firstLED+numLEDs-1);
  return s;
}


#endif // ENABLE_LEDCHAIN



