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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 7


#include "ledchaindevice.hpp"

#if ENABLE_LEDCHAIN

#include "p44view.hpp"
#include "lightbehaviour.hpp"
#include "colorlightbehaviour.hpp"


using namespace p44;


// MARK: - LedChainDevice

class LightSegment : public P44View
{
  PixelColor lightColor;
  int startSoftEdge;
  int endSoftEdge;

protected:

  /// get content pixel color
  /// @param aPt content coordinate
  /// @note aPt is NOT guaranteed to be within actual content as defined by contentSize
  ///   implementation must check this!
  virtual PixelColor contentColorAt(PixelCoord aPt)
  {
    if (isInContentSize(aPt)) return backgroundColor;
    else return transparent;
  }

public:

  LightSegment(int aX, int aDx, int aY, int aDy, int aStartSoftEdge, int aEndSoftEdge) :
    startSoftEdge(aStartSoftEdge),
    endSoftEdge(aEndSoftEdge)
  {
    PixelRect f = { aX, aY, aDx, aDy };
    setFrame(f);
    setFullFrameContent();
  }

};



LedChainDevice::LedChainDevice(LedChainVdc *aVdcP, int aX, int aDx, int aY, int aDy, const string &aDeviceConfig) :
  inherited(aVdcP)
{
  // type:config_for_type
  // Where:
  // - with type=segment
  //   - aX,aDx,aY,aDY determine the size of the segment (view)
  //   - config=b:e
  //     - b:0..n size of softedge at beginning
  //     - e:0..n size of softedge at end
  // - with type=field
  //   - aX,aY determine the (initial) center of the light field
  //   - aDx,aDy determine the (initial) diameter of the light field
  //   - config=??? %%%tbd
  // evaluate config
  string config = aDeviceConfig;
  string mode, s;
  int startSoftEdge = 0;
  int endSoftEdge = 0;
  size_t i = config.find(":");
  bool configOK = false;
  if (i!=string::npos) {
    mode = config.substr(0,i);
    config.erase(0,i+1);
  }
  lightType = lighttype_unknown;
  if (mode=="segment" || mode=="area") {
    // simple segment (area) of the matrix/chain
    lightType = lighttype_simplearea;
    i = config.find(":");
    if (i!=string::npos) {
      s = config.substr(0,i);
      config.erase(0,i+1);
      if (sscanf(s.c_str(), "%d", &startSoftEdge)==1) {
        if (sscanf(config.c_str(), "%d", &endSoftEdge)==1) {
          // install the view
          lightView = P44ViewPtr(new LightSegment(aX, aDx, aY, aDy, startSoftEdge, endSoftEdge));
          // complete config
          configOK = true;
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
  // vDC implementation specific UUID:
  //   UUIDv5 with name = classcontainerinstanceid::ledchainType:firstLED:lastLED
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  string ii = vdcP->vdcInstanceIdentifier();
  if (lightType==lighttype_simplearea && aY==0 && aDy==1) {
    // make dSUID compatible with older simple ledchain implementation if it is just a single chain
    string_format_append(ii, "%d:%d:%d", lightType, aX, aDx);
  }
  else {
    // (initial) position and size determine the dSUID
    string_format_append(ii, "%d:%d:%d:%d:%d", lightType, aX, aDx, aY, aDy);
  }
  dSUID.setNameInSpace(ii, vdcNamespace);

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
    if (needsToApplyChannels(&transitionTime)) {
      // needs update
      // - derive (possibly new) color mode from changed channels
      cl->deriveColorMode();
      // - calculate and start transition
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
  // RGB or RGBW dimmer
  RGBColorLightBehaviourPtr cl = getOutput<RGBColorLightBehaviour>();
  bool moreSteps = cl->colorTransitionStep(aStepSize);
  if (cl->brightnessTransitionStep(aStepSize)) moreSteps = true;
  // RGB light, get basic color
  FOCUSLOG("Ledchain: brightness = %f, hue=%f, saturation=%f", cl->brightness->getTransitionalValue(), cl->hue->getTransitionalValue(), cl->saturation->getTransitionalValue());
  double r, g, b, w;
  if (getLedChainVdc().hasWhite()) {
    cl->getRGBW(r, g, b, w, 255, true); // get R,G,B,W values, but at full brightness
  }
  else {
    cl->getRGB(r, g, b, 255, true); // get R,G,B at full brightness
    w = 0;
  }
  // the view's background color is the color
  PixelColor pix;
  pix.r = r;
  pix.g = g;
  pix.b = b;
  pix.a = cl->brightnessForHardware()*255/100; // alpha is brightness
  lightView->setBackgroundColor(pix);
  getLedChainVdc().ledArrangement.render(); // update
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



string LedChainDevice::modelName()
{
  if (lightType==lighttype_simplearea)
    return "LED Matrix Area";
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
  PixelRect r = lightView->getFrame();
  s = string_format("LED matrix light in rectangle (%d,%d,%d,%d)", r.x, r.y, r.dx, r.dy);
  return s;
}



string LedChainDevice::description()
{
  string s = inherited::description();
  PixelRect r = lightView->getFrame();
  string_format_append(s, "\n- LED matrix light in rectangle (%d,%d,%d,%d)", r.x, r.y, r.dx, r.dy);
  return s;
}


#endif // ENABLE_LEDCHAIN



