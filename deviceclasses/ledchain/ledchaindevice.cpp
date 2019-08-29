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
#include "lightspotview.hpp"
#include "lightbehaviour.hpp"
#include "movinglightbehaviour.hpp"


using namespace p44;


// MARK: - LightSegment

class LightSegment : public P44View
{
  int startSoftEdge;
  int endSoftEdge;

protected:

  /// get content pixel color
  /// @param aPt content coordinate
  /// @note aPt is NOT guaranteed to be within actual content as defined by contentSize
  ///   implementation must check this!
  virtual PixelColor contentColorAt(PixelCoord aPt)
  {
    if (isInContentSize(aPt)) return foregroundColor;
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


// MARK: - FeatureLightSpot

class FeatureLightSpot : public LightSpotView
{

public:

  FeatureLightSpot(int aX, int aDx, int aY, int aDy)
  {
    PixelRect f = { aX, aY, aDx, aDy };
    setFrame(f);
    setFullFrameContent();
    setPosition(50, 50); // middle
    setZoom(50, 50); // filling larger dimension of frame
  }


  void setPosition(double aRelX, double aRelY)
  {
    // movement area is 2 times the frame, so light can be moved out completely out of the frame in both directions
    // This means light center appears at left/bottom edge at 0.25 and disappears at right/top edge at 0.75
    setCenter({
      (int)(-frame.dx/2+frame.dx*aRelX*0.02),
      (int)(-frame.dy/2+frame.dy*aRelY*0.02),
    });
  }

  void setZoom(double aRelDx, double aRelDy)
  {
    // zoom area is (like position) twice the size of the larger dimension of the frame
    // This means the light fully fits the frame in the larger direction at zoom==50
    int maxD = max(frame.dx, frame.dy);
    setExtent({
      (int)(maxD*aRelDx*0.01),
      (int)(maxD*aRelDy*0.01)
    });
  }

};
typedef boost::intrusive_ptr<FeatureLightSpot> FeatureLightSpotPtr;






// MARK: - LedChainDevice

LedChainDevice::LedChainDevice(LedChainVdc *aVdcP, int aX, int aDx, int aY, int aDy, const string &aDeviceConfig) :
  inherited(aVdcP)
{
  RGBColorLightBehaviourPtr behaviour;
  // aDeviceConfig syntax:
  //   [#uniqueid:]lighttype:config_for_lighttype
  //   Note: uniqueid can be any unique string to derive a dSUID from, or a valid dSUID to be used as-is
  // where:
  // - with lighttype=segment
  //   - aX,aDx,aY,aDY determine the size of the segment (view)
  //   - config=b:e
  //     - id: optional unique id identifying the light, must start with non-numeric, if set, determines the dSUID
  //     - b:0..n size of softedge at beginning
  //     - e:0..n size of softedge at end
  // - with lighttype=lightspot
  //   - aX,aY determine the (initial) center of the light field
  //   - aDx,aDy determine the (initial) diameter of the light field
  //   - config=<none yet>
  // evaluate config
  string config = aDeviceConfig;
  string lt, s;
  const char* p = config.c_str();
  bool configOK = false;
  // check for optional unique id
  const char* p2 = p;
  if (nextPart(p2, s, ':')) {
    if (s[0]=='#') {
      // this is the unique ID
      uniqueId = s.substr(1); // save it
      p = p2; // skip it
    }
  }
  if (nextPart(p, lt, ':')) {
    // found light type
    lightType = lighttype_unknown;
    if (lt=="segment" || lt=="area") {
      if (lt=="segment" && aDy==0) aDy = 1; // backwards compatibility, old DB entries have null Y/dY and return 0 for it
      // simple segment (area) of the matrix/chain
      lightType = lighttype_simplearea;
      int startSoftEdge = 0;
      int endSoftEdge = 0;
      if (nextPart(p, s, ':')) {
        if (sscanf(s.c_str(), "%d", &startSoftEdge)==1) {
          if (nextPart(p, s, ':')) {
            sscanf(config.c_str(), "%d", &endSoftEdge);
          }
        }
      }
      // install the view
      lightView = P44ViewPtr(new LightSegment(aX, aDx, aY, aDy, startSoftEdge, endSoftEdge));
      // complete config
      configOK = true;
    }
    else if (lt=="lightspot") {
      // light spot
      lightType = lighttype_lightspot;
      installSettings(DeviceSettingsPtr(new FeatureLightDeviceSettings(*this)));
      behaviour = FeatureLightBehaviourPtr(new FeatureLightBehaviour(*this, false));
      lightView = FeatureLightSpotPtr(new FeatureLightSpot(aX, aDx, aY, aDy));
      configOK = true;
    }
  }
  if (!configOK) {
    ALOG(LOG_ERR, "invalid LedChain device config: %s", aDeviceConfig.c_str());
  }
  if (!behaviour) {
    // default to simple color light (we can't have nothing even with invalid config)
    installSettings(DeviceSettingsPtr(new ColorLightDeviceSettings(*this)));
    // - add multi-channel color light behaviour (which adds a number of auxiliary channels)
    behaviour = RGBColorLightBehaviourPtr(new RGBColorLightBehaviour(*this, false));
  }
  // - is RGB
  colorClass = class_yellow_light;
  // - set minimum brightness
  behaviour->initMinBrightness(getLedChainVdc().getMinBrightness());
  addBehaviour(behaviour);
  // - create dSUID
  if (uniqueId.empty()) {
    // no unique id, use type and position to form dSUID (backwards compatibility)
    ALOG(LOG_WARNING, "Legacy LED chain device, should specify unique ID to get stable dSUID");
    uniqueId = string_format("%d:%d:%d", lightType, aX, aDx);
  }
  // if uniqueId is a valid dSUID/UUID, use it as-is
  if (!dSUID.setAsString(uniqueId)) {
    // generate vDC implementation specific UUID:
    //   UUIDv5 with name = <classcontainerinstanceid><uniqueid> (separator missing for backwards compatibility)
    //   Note: for backwards compatibility, when no uniqueid ist set, <ledchainType>:<firstLED>:<lastLED> is used
    DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
    string ii = vdcP->vdcInstanceIdentifier();
    ii += uniqueId;
    dSUID.setNameInSpace(ii, vdcNamespace);
  }
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
  FeatureLightBehaviourPtr fl = getOutput<FeatureLightBehaviour>();
  if (cl) {
    if (needsToApplyChannels(&transitionTime)) {
      // needs update
      // - derive (possibly new) color mode from changed channels
      cl->deriveColorMode();
      // - calculate and start transition
      cl->brightnessTransitionStep(); // init
      cl->colorTransitionStep(); // init
      if (fl) {
        // also apply extra channels
        fl->positionTransitionStep();
        fl->featureTransitionStep();
      }
      applyChannelValueSteps(aForDimming, transitionTime==0 ? 1 : (double)TRANSITION_STEP_TIME/transitionTime);
    }
    // consider applied
    cl->appliedColorValues();
    if (fl) {
      fl->appliedPosition();
      fl->appliedFeatures();
    }
  }
  inherited::applyChannelValues(aDoneCB, aForDimming);
}


void LedChainDevice::applyChannelValueSteps(bool aForDimming, double aStepSize)
{
  // RGB or RGBW dimmer
  RGBColorLightBehaviourPtr cl = getOutput<RGBColorLightBehaviour>();
  FeatureLightBehaviourPtr fl = getOutput<FeatureLightBehaviour>();
  bool moreSteps = cl->colorTransitionStep(aStepSize);
  if (cl->brightnessTransitionStep(aStepSize)) moreSteps = true;
  if (fl) {
    if (fl->positionTransitionStep(aStepSize)) moreSteps = true;
    if (fl->featureTransitionStep(aStepSize)) moreSteps = true;
  }
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
  // the view's foreground color is the color
  PixelColor pix;
  pix.r = r;
  pix.g = g;
  pix.b = b;
  pix.a = cl->brightnessForHardware()*getLedChainVdc().ledArrangement.getMaxOutValue()/100; // alpha is brightness, scaled down to maxOutValue
  // possibly with extended features
  if (fl) {
    FeatureLightSpotPtr fls = boost::dynamic_pointer_cast<FeatureLightSpot>(lightView);
    if (fls) {
      fls->setPosition(fl->horizontalPosition->getTransitionalValue(), fl->verticalPosition->getTransitionalValue());
      fls->setZoom(fl->horizontalZoom->getTransitionalValue(), fl->verticalZoom->getTransitionalValue());
      fls->setRotation(fl->rotation->getTransitionalValue());
      uint32_t mode = fl->featureMode->getChannelValue();
      fls->setColoringParameters(
        pix,
        fl->brightnessGradient->getTransitionalValue()/100, mode & 0xFF,
        fl->hueGradient->getTransitionalValue()/100, (mode>>8) & 0xFF,
        fl->saturationGradient->getTransitionalValue()/100, (mode>>16) & 0xFF,
        (mode & 0x01000000)==0 // not radial
      );
      fls->setWrapMode((mode & 0x02000000)==0 ? P44View::clipXY : 0);
    }
  }
  else {
    lightView->setForegroundColor(pix);
  }
  getLedChainVdc().ledArrangement.render(); // update
  // next step
  if (moreSteps) {
    ALOG(LOG_DEBUG, "LED chain transitional values R=%d, G=%d, B=%d, dim=%d", (int)r, (int)g, (int)b, pix.a);
    // not yet complete, schedule next step
    transitionTicket.executeOnce(
      boost::bind(&LedChainDevice::applyChannelValueSteps, this, aForDimming, aStepSize),
      TRANSITION_STEP_TIME
    );
    return; // will be called later again
  }
  if (!aForDimming) {
    ALOG(LOG_INFO, "LED chain final values R=%d, G=%d, B=%d, dim=%d", (int)r, (int)g, (int)b, pix.a);
  }
}



string LedChainDevice::modelName()
{
  if (lightType==lighttype_simplearea)
    return "Static LED Matrix Area";
  else if (lightType==lighttype_lightspot)
    return "Moving Light on LED Matrix";
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
  s = string_format("LED matrix light in rectangle (%d,%d,%d,%d), id='%s'", r.x, r.y, r.dx, r.dy, uniqueId.c_str());
  return s;
}



string LedChainDevice::description()
{
  string s = inherited::description();
  PixelRect r = lightView->getFrame();
  string_format_append(s, "\n- LED matrix light in rectangle (%d,%d,%d,%d)\n  unique ID='%s'", r.x, r.y, r.dx, r.dy, uniqueId.c_str());
  return s;
}


#endif // ENABLE_LEDCHAIN



