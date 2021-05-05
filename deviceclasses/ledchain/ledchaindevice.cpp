//
//  Copyright (c) 2015-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#include "movinglightbehaviour.hpp"
#include "coloreffectview.hpp"
#include "lightspotview.hpp"

#if ENABLE_VIEWCONFIG
#include "viewfactory.hpp"
#endif

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
  virtual PixelColor contentColorAt(PixelPoint aPt)
  {
    if (isInContentSize(aPt)) return foregroundColor;
    else return transparent;
  }

public:

  LightSegment(int aX, int aDx, int aY, int aDy, int aStartSoftEdge, int aEndSoftEdge) :
    startSoftEdge(aStartSoftEdge),
    endSoftEdge(aEndSoftEdge)
  {
    setForegroundColor(black);
    setBackgroundColor(transparent);
    PixelRect f = { aX, aY, aDx, aDy };
    setFrame(f);
    setFullFrameContent();
  }

};


// MARK: - LedChainDevice

LedChainDevice::LedChainDevice(LedChainVdc *aVdcP, int aX, int aDx, int aY, int aDy, const string &aDeviceConfig) :
  inherited(aVdcP)
  #if P44SCRIPT_FULL_SUPPORT
  ,ledChainDeviceLookup(*this)
  #endif
{
  #if P44SCRIPT_FULL_SUPPORT
  ledChainDeviceLookup.isMemberVariable();
  #endif
  RGBColorLightBehaviourPtr behaviour;
  // aDeviceConfig syntax:
  //   [#uniqueid:]lighttype:config_for_lighttype
  //   Note: uniqueid can be any unique string to derive a dSUID from, or a valid dSUID to be used as-is
  // where:
  // - with lighttype=segment
  //   - aX,aDx,aY,aDY determine the position and size (=frame) of the segment (view)
  //   - config=b:e
  //     - b:0..n size of softedge at beginning
  //     - e:0..n size of softedge at end
  // - with lighttype=featurelight
  //   - aX,aDx,aY,aDY determine the position and size (=frame) of the light spot
  //   - config:string|filepath|JSON
  //     - empty: lightspot view
  //     - string: name of a view type, which will be instantiated with full frame
  //     - JSON object beginning with '{': config for root view
  //     - filename (string containing a period) or path: name of a JSON file to load
  // evaluate config
  string config = aDeviceConfig;
  string lt, s;
  const char* p = config.c_str();
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
    }
    #if ENABLE_VIEWCONFIG
    else if (lt=="feature") {
      // light spot
      lightType = lighttype_feature;
      installSettings(DeviceSettingsPtr(new FeatureLightDeviceSettings(*this)));
      behaviour = FeatureLightBehaviourPtr(new FeatureLightBehaviour(*this, false));
      // create the root view (not necessarily a ColorEffectView, but likely so
      JsonObjectPtr cfg;
      ErrorPtr err;
      if (*p=='{') {
        // JSON config
        cfg = JsonObject::objFromText(p, -1, &err);
      }
      else if (strchr(p, '.')) {
        // filename
        cfg = JsonObject::objFromFile(Application::sharedApplication()->resourcePath(p).c_str(), &err);
      }
      else {
        string vty = "lightspot"; // default to lightspot
        if (*p) vty = p;
        cfg = JsonObject::newObj();
        cfg->add("type", cfg->newString(vty));
      }
      if (Error::isOK(err)) {
        // - override frame
        cfg->add("x", cfg->newInt32(aX));
        cfg->add("y", cfg->newInt32(aY));
        cfg->add("dx", cfg->newInt32(aDx));
        cfg->add("dy", cfg->newInt32(aDy));
        if (!cfg->get("fullframe")) cfg->add("fullframe", cfg->newBool(true));
        if (!cfg->get("type")) cfg->add("type", cfg->newString("stack"));
        // - create
        err = createViewFromConfig(cfg, lightView, P44ViewPtr());
      }
      if (Error::notOK(err)) {
        OLOG(LOG_WARNING, "Invalid feature light config: %s", err->text());
      }
    }
    #endif
  }
  if (!lightView) {
    lightView = P44ViewPtr(new P44View()); // dummy to avoid crashes
    OLOG(LOG_WARNING, "No light view found");
  }
  if (!behaviour) {
    // default to simple color light (we can't have nothing even with invalid config)
    installSettings(DeviceSettingsPtr(new ColorLightDeviceSettings(*this)));
    // - add multi-channel color light behaviour (which adds a number of auxiliary channels)
    behaviour = RGBColorLightBehaviourPtr(new RGBColorLightBehaviour(*this, false));
  }
  if (lightView) {
    // make sure it is invisible at the beginning
    lightView->hide();
  }
  // - is RGB
  colorClass = class_yellow_light;
  // - set minimum brightness
  behaviour->initMinBrightness(getLedChainVdc().getMinBrightness());
  addBehaviour(behaviour);
  // - create dSUID
  if (uniqueId.empty()) {
    // no unique id, use type and position to form dSUID (backwards compatibility)
    OLOG(LOG_WARNING, "Legacy LED chain device, should specify unique ID to get stable dSUID");
    uniqueId = string_format("%d:%d:%d", lightType, aX, aDx);
  }
  // set uniqueid as view label if view does not have a label
  if (lightView) lightView->setDefaultLabel(uniqueId);
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


#if P44SCRIPT_FULL_SUPPORT

class LedChainDeviceObj : public DeviceObj
{
  typedef DeviceObj inherited;
public:
  LedChainDeviceObj(DevicePtr aDevice);
  P44ViewPtr getLightView() { return boost::dynamic_pointer_cast<LedChainDevice>(mDevice)->getLightView(); }
};


static ScriptObjPtr view_accessor(BuiltInMemberLookup& aMemberLookup, ScriptObjPtr aParentObj, ScriptObjPtr aObjToWrite)
{
  LedChainDeviceObj* d = dynamic_cast<LedChainDeviceObj*>(aParentObj.get());
  return d->getLightView()->newViewObj();
}


static const BuiltinMemberDescriptor ledChainMembers[] = {
  { "view", builtinmember, 0, NULL, (BuiltinFunctionImplementation)&view_accessor }, // Note: correct '.accessor=&lrg_accessor' form does not work with OpenWrt g++, so need ugly cast here
  { NULL } // terminator
};

LedChainDeviceLookup::LedChainDeviceLookup(LedChainDevice& aLedChainDevice) :
  inherited(ledChainMembers),
  mLedChainDevice(aLedChainDevice)
{
}

static BuiltInMemberLookup* sharedLedChainDeviceMemberLookupP = NULL;

LedChainDeviceObj::LedChainDeviceObj(DevicePtr aDevice) : inherited(aDevice)
{
  if (sharedLedChainDeviceMemberLookupP==NULL) {
    sharedLedChainDeviceMemberLookupP = new BuiltInMemberLookup(ledChainMembers);
    sharedLedChainDeviceMemberLookupP->isMemberVariable(); // disable refcounting
  }
  registerMemberLookup(sharedLedChainDeviceMemberLookupP);
};

ScriptObjPtr LedChainDevice::newDeviceObj()
{
  return new LedChainDeviceObj(this);
}

#endif // P44SCRIPT_FULL_SUPPORT


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
      OLOG(LOG_ERR, "Error deleting led chain device: %s", getLedChainVdc().db.error()->description().c_str());
    }
  }
  // disconnection is immediate, so we can call inherited right now
  inherited::disconnect(aForgetParams, aDisconnectResultHandler);
}


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
      applyChannelValueSteps(aForDimming, transitionTime==0 ? 1 : (double)(getLedChainVdc().ledArrangement->minUpdateInterval)/transitionTime);
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
  MovingLightBehaviourPtr ml = getOutput<MovingLightBehaviour>();
  FeatureLightBehaviourPtr fl = getOutput<FeatureLightBehaviour>();
  bool moreSteps = cl->colorTransitionStep(aStepSize);
  if (cl->brightnessTransitionStep(aStepSize)) moreSteps = true;
  if (ml) {
    if (ml->positionTransitionStep(aStepSize)) moreSteps = true;
    if (fl) {
      if (fl->featureTransitionStep(aStepSize)) moreSteps = true;
    }
  }
  // RGB light, get basic color
  FOCUSLOG("Ledchain: brightness = %f, hue=%f, saturation=%f", cl->brightness->getChannelValue(true), cl->hue->getChannelValue(true), cl->saturation->getChannelValue(true));
  double r, g, b;
  cl->getRGB(r, g, b, 255, true, true); // get R,G,B at full brightness
  // the view's foreground color is the color
  PixelColor pix;
  pix.r = r;
  pix.g = g;
  pix.b = b;
  pix.a = 255;
  lightView->setAlpha(cl->brightnessForHardware()*getLedChainVdc().ledArrangement->getMaxOutValue()/100); // alpha is brightness, scaled down to maxOutValue
  P44ViewPtr targetView = lightView->getView("LIGHT"); // where to direct extras to
  if (!targetView) targetView = lightView;
  if (ml) {
    // moving light, has position, common to all views
    targetView->setRelativeContentOrigin(
      (fl->horizontalPosition->getChannelValue(true)-50)/50,
      (fl->verticalPosition->getChannelValue(true)-50)/50,
      true // centered
    );
    if (fl) {
      // feature light with extra channels
      // - rotation is common to all views
      targetView->setContentRotation(fl->rotation->getChannelValue(true));
      ColorEffectViewPtr cev = boost::dynamic_pointer_cast<ColorEffectView>(targetView);
      if (cev) {
        // features available only in ColorEffectView
        // - zoom area is (like position) twice the size of the larger dimension of the frame
        //   This means the light fully fits the frame in the larger direction at zoom==50
        PixelPoint sz = cev->getFrameSize();
        int maxD = max(sz.x, sz.y);
        cev->setExtent({
          (int)(maxD*fl->horizontalZoom->getChannelValue(true)*0.01),
          (int)(maxD*fl->verticalZoom->getChannelValue(true)*0.01)
        });
        uint32_t mode = fl->featureMode->getChannelValue();
        cev->setColoringParameters(
          pix,
          fl->brightnessGradient->getChannelValue(true)/100, mode & 0xFF,
          fl->hueGradient->getChannelValue(true)/100, (mode>>8) & 0xFF,
          fl->saturationGradient->getChannelValue(true)/100, (mode>>16) & 0xFF,
          (mode & 0x01000000)==0 // not radial
        );
        cev->setWrapMode((cev->getWrapMode()&~P44View::clipMask) | ((mode & 0x02000000)==0 ? P44View::clipXY : 0));
      }
      else {
        // not a ColorEffectView, just set foreground color
        targetView->setForegroundColor(pix);
      }
    }
  }
  else {
    // simple area, just set foreground color
    lightView->setForegroundColor(pix);
  }
  getLedChainVdc().ledArrangement->render(); // update
  // next step
  if (moreSteps) {
    OLOG(LOG_DEBUG, "LED chain transitional values R=%d, G=%d, B=%d, dim=%d", (int)r, (int)g, (int)b, lightView->getAlpha());
    // not yet complete, schedule next step
    transitionTicket.executeOnce(
      boost::bind(&LedChainDevice::applyChannelValueSteps, this, aForDimming, aStepSize),
      getLedChainVdc().ledArrangement->minUpdateInterval
    );
    return; // will be called later again
  }
  if (!aForDimming) {
    OLOG(LOG_INFO, "LED chain final values R=%d, G=%d, B=%d, dim=%d", (int)r, (int)g, (int)b, lightView->getAlpha());
  }
}



string LedChainDevice::modelName()
{
  if (lightType==lighttype_simplearea)
    return "Static LED Matrix Area";
  else if (lightType==lighttype_feature)
    return "Moving Feature Light on LED Matrix";
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



