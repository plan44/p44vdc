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

#include "huevdc.hpp"

#if ENABLE_HUE

#include "huedevice.hpp"
#include "macaddress.hpp"

using namespace p44;

#define DEFAULT_HUE_MAX_OPTIMIZER_SCENES 20
#define DEFAULT_HUE_MAX_OPTIMIZER_GROUPS 5


HueVdc::HueVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  inherited(aInstanceNumber, aVdcHostP, aTag),
  mHueComm(),
  mBridgeMacAddress(0),
  mNumOptimizerScenes(0),
  mNumOptimizerGroups(0),
  mHas_1_11_api(false)
{
  mHueComm.isMemberVariable();
  mHueComm.mUseHueCloudDiscovery = getVdcHost().cloudAllowed();
  mOptimizerMode = opt_disabled; // optimizer disabled by default, but available
  // defaults
  mMaxOptimizerScenes = DEFAULT_HUE_MAX_OPTIMIZER_SCENES;
  mMaxOptimizerGroups = DEFAULT_HUE_MAX_OPTIMIZER_GROUPS;
}


HueVdc::~HueVdc()
{
}


void HueVdc::setLogLevelOffset(int aLogLevelOffset)
{
  mHueComm.setLogLevelOffset(aLogLevelOffset);
  inherited::setLogLevelOffset(aLogLevelOffset);
}


const char *HueVdc::vdcClassIdentifier() const
{
  return "hue_Lights_Container";
}


bool HueVdc::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("vdc_hue", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


string HueVdc::getExtraInfo()
{
  return string_format("hue api%s: %s", mFixedURL ? " (fixed)" : "", mHueComm.mBaseURL.c_str());
}


string HueVdc::hardwareGUID()
{
  string s;
  if (mBridgeMacAddress) {
    s = "macaddress:";
    s += macAddressToString(mBridgeMacAddress,':');
  }
  return s;
};



string HueVdc::vendorName()
{
  return "Philips/Signify";
}






// MARK: - DB and initialisation

// Version history
//  1 : first version
//  2 : added hueApiURL and fixedURL
#define HUE_SCHEMA_MIN_VERSION 1 // minimally supported version, anything older will be deleted
#define HUE_SCHEMA_VERSION 2 // current version

string HuePersistence::dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion)
{
  string sql;
  if (aFromVersion==0) {
    // create DB from scratch
		// - use standard globs table for schema version
    sql = inherited::dbSchemaUpgradeSQL(aFromVersion, aToVersion);
		// - add fields to globs table
    sql.append(
      "ALTER TABLE globs ADD hueBridgeUUID TEXT;"
      "ALTER TABLE globs ADD hueBridgeUser TEXT;"
      "ALTER TABLE globs ADD hueApiURL TEXT;"
      "ALTER TABLE globs ADD fixedURL INTEGER;"
    );
    // reached final version in one step
    aToVersion = HUE_SCHEMA_VERSION;
  }
  else if (aFromVersion==1) {
    // V1->V2: stored API url added
    sql =
      "ALTER TABLE globs ADD hueApiURL TEXT;"
      "ALTER TABLE globs ADD fixedURL INTEGER;";
    // reached version 2
    aToVersion = 2;
  }
  return sql;
}


#define HUE_RECOLLECT_INTERVAL (30*Minute)
#define PSEUDO_UUID_FOR_FIXED_API "fixed_api_base_URL" // used in place of uuid in fixed-IP case

void HueVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  // load persistent params for dSUID
  load();
  // load private data
	string databaseName = getPersistentDataDir();
	string_format_append(databaseName, "%s_%d.sqlite3", vdcClassIdentifier(), getInstanceNumber());
  ErrorPtr error = mDb.connectAndInitialize(databaseName.c_str(), HUE_SCHEMA_VERSION, HUE_SCHEMA_MIN_VERSION, aFactoryReset);
	aCompletedCB(error); // return status of DB init
  // schedule rescans
  setPeriodicRecollection(HUE_RECOLLECT_INTERVAL, rescanmode_incremental);
}



// MARK: - collect devices


int HueVdc::getRescanModes() const
{
  // all modes make sense, exhaustive forces discovery instead of using cached API URL
  return rescanmode_incremental+rescanmode_normal+rescanmode_exhaustive;
}


void HueVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  if (!(aRescanFlags & rescanmode_incremental)) {
    // full collect, remove all devices
    removeDevices(aRescanFlags & rescanmode_clearsettings);
  }
  // load hue bridge uuid and token
  sqlite3pp::query qry(mDb);
  if (qry.prepare("SELECT hueBridgeUUID, hueBridgeUser, hueApiURL, fixedURL FROM globs")==SQLITE_OK) {
    sqlite3pp::query::iterator i = qry.begin();
    if (i!=qry.end()) {
      mBridgeIdentifier = nonNullCStr(i->get<const char *>(0));
      mBridgeUserName = nonNullCStr(i->get<const char *>(1));
      mBridgeApiURL = nonNullCStr(i->get<const char *>(2));
      mFixedURL = i->get<bool>(3);
    }
  }
  if ((aRescanFlags & rescanmode_exhaustive) && !mFixedURL) {
    // exhaustive rescan means we need to search for the bridge API
    mBridgeApiURL.clear();
  }
  if (mBridgeIdentifier.length()>0 || mBridgeApiURL.length()>0) {
    // we know a bridge by UUID or API URL, try to refind it
    refindBridge(aCompletedCB);
  }
  else {
    // no bridge known, can't collect anything at this time
    if (aCompletedCB) aCompletedCB(ErrorPtr());
  }
}


void HueVdc::handleGlobalEvent(VdchostEvent aEvent)
{
  if (aEvent==vdchost_network_reconnected) {
    // re-connecting to network should re-scan for hue bridge
    collectDevices(NoOP, rescanmode_incremental);
  }
  inherited::handleGlobalEvent(aEvent);
}



#define REFIND_RETRY_DELAY (30*Second)

void HueVdc::refindBridge(StatusCB aCompletedCB)
{
  if (!getVdcHost().isNetworkConnected()) {
    // TODO: checking IPv4 only at this time, need to add IPv6 later
    OLOG(LOG_WARNING, "hue: device has no IP yet -> must wait ");
    mRefindTicket.executeOnce(boost::bind(&HueVdc::refindBridge, this, aCompletedCB), REFIND_RETRY_DELAY);
    return;
  }
  // actually refind
  mHueComm.mBridgeIdentifier = mBridgeIdentifier;
  mHueComm.mUserName = mBridgeUserName;
  mHueComm.mFixedBaseURL = mBridgeApiURL;
  mHueComm.refindBridge(boost::bind(&HueVdc::refindResultHandler, this, aCompletedCB, _1));
}



void HueVdc::refindResultHandler(StatusCB aCompletedCB, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    // found already registered bridge again
    OLOG(LOG_INFO,
      "Hue bridge uuid '%s' found again:\n"
      "- userName = %s\n"
      "- API base URL = %s",
      mHueComm.mBridgeIdentifier.c_str(),
      mHueComm.mUserName.c_str(),
      mHueComm.mBaseURL.c_str()
    );
    // save the current URL and possibly upgraded bridge identifier
    if (
      !mFixedURL &&
      (mHueComm.mBaseURL!=mBridgeApiURL || mHueComm.mBridgeIdentifier!=mBridgeIdentifier)
    ) {
      mBridgeApiURL = mHueComm.mBaseURL;
      mBridgeIdentifier = mHueComm.mBridgeIdentifier;
      // save back into database
      if(mDb.executef(
        "UPDATE globs SET hueBridgeUUID='%q', hueApiURL='%q', fixedURL=0",
        mBridgeIdentifier.c_str(),
        mBridgeApiURL.c_str()
      )!=SQLITE_OK) {
        OLOG(LOG_ERR, "Error saving hue bridge url: %s", mDb.error()->description().c_str());
      }
    }
    // collect existing lights
    // Note: for now we don't search for new lights, this is left to the Hue App, so users have control
    //   if they want new lights added or not
    queryBridgeAndLights(aCompletedCB);
  }
  else {
    // not found (usually timeout)
    // - if URL does not work, clear cached IP and try again (unless IP is user-provided)
    if (!mBridgeApiURL.empty() && !mFixedURL) {
      // forget the cached IP
      OLOG(LOG_WARNING, "Could not access bridge API at %s - revert to finding bridge by UUID", mBridgeApiURL.c_str());
      mBridgeApiURL.clear();
      // retry searching by uuid
      mRefindTicket.executeOnce(boost::bind(&HueVdc::refindBridge, this, aCompletedCB), 500*MilliSecond);
      return;
    }
    else {
      OLOG(LOG_WARNING, "Error refinding hue bridge uuid '%s', error = %s", mHueComm.mBridgeIdentifier.c_str(), aError->text());
    }
    if (aCompletedCB) aCompletedCB(ErrorPtr()); // no hue bridge to collect lights from (but this is not a collect error)
  }
}


ErrorPtr HueVdc::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="registerHueBridge") {
    // hue specific addition, only via genericRequest
    ApiValuePtr a = aParams->get("bridgeApiURL");
    if (a) {
      // needs new pairing, forget current devices
      removeDevices(false);
      // register by bridge API URL (or remove with empty string)
      mBridgeUserName.clear();
      mBridgeIdentifier.clear();
      mBridgeApiURL = a->stringValue();
      mFixedURL = false;
      if (!mBridgeApiURL.empty()) {
        // make full API URL if it's just a IP or host name
        if (mBridgeApiURL.substr(0,4)!="http") {
          mBridgeApiURL = "http://" + mBridgeApiURL + ":80/api";
        }
        // register
        mBridgeIdentifier = PSEUDO_UUID_FOR_FIXED_API;
        mFixedURL = true;
        // save the bridge parameters
        if(mDb.executef(
          "UPDATE globs SET hueBridgeUUID='%q', hueBridgeUser='', hueApiURL='%q', fixedURL=1",
          mBridgeIdentifier.c_str(),
          mBridgeApiURL.c_str()
        )!=SQLITE_OK) {
          respErr = mDb.error("saving hue bridge params");
        }
        else {
          // done (separate learn-in required, because button press at the bridge is required)
          respErr = Error::ok();
        }
      }
      else {
        // unregister
        mFixedURL = false;
        if(mDb.executef("UPDATE globs SET hueBridgeUUID='', hueBridgeUser='', hueApiURL='', fixedURL=0")!=SQLITE_OK) {
          respErr = mDb.error("clearing hue bridge params");
        }
        else {
          // done
          respErr = Error::ok();
        }
      }
    }
    else {
      // register by uuid/username (for migration)
      respErr = checkStringParam(aParams, "bridgeUuid", mBridgeIdentifier);
      if (Error::notOK(respErr)) return respErr;
      respErr = checkStringParam(aParams, "bridgeUsername", mBridgeUserName);
      if (Error::notOK(respErr)) return respErr;
      // save the bridge parameters
      if(mDb.executef(
        "UPDATE globs SET hueBridgeUUID='%q', hueBridgeUser='%q', hueApiURL='', fixedURL=0",
        mBridgeIdentifier.c_str(),
        mBridgeUserName.c_str()
      )!=SQLITE_OK) {
        respErr = mDb.error("saving hue bridge migration params");
      }
      else {
        // now collect the lights from the new bridge, remove all settings from previous bridge
        collectDevices(boost::bind(&DsAddressable::methodCompleted, this, aRequest, _1), rescanmode_clearsettings);
      }
    }
  }
  else {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }
  return respErr;
}


void HueVdc::setLearnMode(bool aEnableLearning, bool aDisableProximityCheck, Tristate aOnlyEstablish)
{
  if (aEnableLearning) {
    if (!mFixedURL || mBridgeUserName.empty()) {
      // no IP known or not logged in: actually search for bridge to learn/unlearn
      if (mFixedURL) {
        // use the user-defined URL
        mHueComm.mFixedBaseURL = mBridgeApiURL;
      }
      else {
        // do not use a chached (but not explicitly user-configured) URL
        mHueComm.mFixedBaseURL.clear();
      }
      mHueComm.findNewBridge(
        string_format("%s#%s", getVdcHost().modelName().c_str(), getVdcHost().getDeviceHardwareId().c_str()).c_str(),
        15*Second, // try to login for 15 secs
        boost::bind(&HueVdc::searchResultHandler, this, aOnlyEstablish, _1)
      );
    }
  }
  else {
    // stop learning
    mHueComm.stopFind();
  }
}


void HueVdc::searchResultHandler(Tristate aOnlyEstablish, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    // found and authenticated bridge
    OLOG(LOG_NOTICE,
      "Hue bridge found and logged in:\n"
      "- uuid = %s\n"
      "- userName = %s\n"
      "- API base URL = %s",
      mHueComm.mBridgeIdentifier.c_str(),
      mHueComm.mUserName.c_str(),
      mHueComm.mBaseURL.c_str()
    );
    // check if we found the already learned-in bridge
    Tristate learnedIn = undefined;
    if (mHueComm.mBridgeIdentifier==mBridgeIdentifier && !mFixedURL) {
      // this is the bridge that was learned in previously. Learn it out
      if (aOnlyEstablish!=yes) {
        learnedIn = no;
        // - delete it from the whitelist
        string url = "/config/whitelist/" + mHueComm.mUserName;
        mHueComm.apiAction(httpMethodDELETE, url.c_str(), JsonObjectPtr(), NoOP);
        // - forget uuid + user name
        mBridgeIdentifier.clear();
        mBridgeUserName.clear();
        // - also clear base URL
        mHueComm.mBaseURL.clear();
      }
    }
    else {
      // new bridge found
      if (aOnlyEstablish!=no) {
        learnedIn = yes;
        if (mHueComm.mBridgeIdentifier!=PSEUDO_UUID_FOR_FIXED_API) {
          // only update if it is a real UUID.
          mBridgeIdentifier = mHueComm.mBridgeIdentifier;
        }
        mBridgeUserName = mHueComm.mUserName;
        if (!mFixedURL) {
          mBridgeApiURL = mHueComm.mBaseURL;
        }
      }
    }
    if (learnedIn!=undefined) {
      // learning in or out requires all devices to be removed first
      // (on learn-in, the bridge's devices will be added afterwards)
      removeDevices(false);
      // actual learn-in or -out has happened
      if (learnedIn==no && !mFixedURL) {
        // forget cached URL (but keep fixed ones!)
        mBridgeApiURL.clear();
      }
      // save the bridge parameters
      if(mDb.executef(
        "UPDATE globs SET hueBridgeUUID='%q', hueBridgeUser='%q', hueApiURL='%q', fixedURL=0",
        mBridgeIdentifier.c_str(),
        mBridgeUserName.c_str(),
        mBridgeApiURL.c_str()
      )!=SQLITE_OK) {
        OLOG(LOG_ERR, "Error saving hue bridge learn params: %s", mDb.error()->description().c_str());
      }
      // now process the learn in/out
      if (learnedIn==yes) {
        // now get lights
        queryBridgeAndLights(boost::bind(&HueVdc::learnedInComplete, this, _1));
        return;
      }
      if (learnedIn!=undefined) {
        // learn out clears MAC
        mBridgeMacAddress = 0;
      }
      // report successful learn event
      getVdcHost().reportLearnEvent(learnedIn==yes, ErrorPtr());
    }
  }
  else {
    // not found (usually timeout)
    OLOG(LOG_NOTICE, "No hue bridge found to register, error = %s", aError->text());
  }
}


void HueVdc::learnedInComplete(ErrorPtr aError)
{
  getVdcHost().reportLearnEvent(true, aError);
}




void HueVdc::queryBridgeAndLights(StatusCB aCollectedHandler)
{
  // query bridge config
  OLOG(LOG_INFO, "Querying hue bridge for config...");
  mHueComm.apiQuery("/config", boost::bind(&HueVdc::gotBridgeConfig, this, aCollectedHandler, _1, _2));
}


void HueVdc::gotBridgeConfig(StatusCB aCollectedHandler, JsonObjectPtr aResult, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    JsonObjectPtr o;
    // get mac address
    if (aResult->get("mac", o)) {
      mBridgeMacAddress = stringToMacAddress(o->stringValue().c_str());
    }
    if (aResult->get("swversion", o)) {
      mSwVersion = o->stringValue();
    }
    if (aResult->get("apiversion", o)) {
      mApiVersion = o->stringValue();
      // check features this version has
      int maj = 0, min = 0, patch = 0;
      if (sscanf(mApiVersion.c_str(), "%d.%d.%d", &maj, &min, &patch)==3) {
        mHas_1_11_api = maj>1 || (maj==1 && min>=11);
      }
    }
    // get name
    if (aResult->get("name", o)) {
      if (getAssignedName().empty()) {
        // only if no name already assigned, show bridge name
        initializeName(o->stringValue());
      }
    }
  }
  if (mHas_1_11_api) {
    // query scenes (in parallel to lights!)
    OLOG(LOG_INFO, "Querying hue bridge for available scenes...");
    mHueComm.apiQuery("/scenes", boost::bind(&HueVdc::collectedScenesHandler, this, aCollectedHandler, _1, _2));
  }
  // Note: can be used to incrementally search additional lights
  // - issue lights query
  OLOG(LOG_INFO, "Querying hue bridge for available lights...");
  mHueComm.apiQuery("/lights", boost::bind(&HueVdc::collectedLightsHandler, this, aCollectedHandler, _1, _2));
}


void HueVdc::collectedScenesHandler(StatusCB aCollectedHandler, JsonObjectPtr aResult, ErrorPtr aError)
{
  OLOG(LOG_INFO, "hue bridge reports scenes = \n%s", aResult ? aResult->c_strValue() : "<none>");
}


void HueVdc::collectedLightsHandler(StatusCB aCollectedHandler, JsonObjectPtr aResult, ErrorPtr aError)
{
  OLOG(LOG_INFO, "hue bridge reports lights = \n%s", aResult ? aResult->c_strValue() : "<none>");
  if (aResult) {
    // pre-v1.3 bridges: { "1": { "name": "Bedroom" }, "2": .... }
    // v1.3 and later bridges: { "1": { "name": "Bedroom", "state": {...}, "modelid":"LCT001", ... }, "2": .... }
    // v1.4 and later bridges: { "1": { "state": {...}, "type": "Dimmable light", "name": "lux demoboard", "modelid": "LWB004","uniqueid":"00:17:88:01:00:e5:a0:87-0b", "swversion": "66012040" }
    aResult->resetKeyIteration();
    string lightID;
    JsonObjectPtr lightInfo;
    while (aResult->nextKeyValue(lightID, lightInfo)) {
      // create hue device
      if (lightInfo) {
        // pre 1.3 bridges, which do not know yet hue Lux, don't have the "state" -> no state == all lights have color (hue or living color)
        // 1.3 and later bridges do have "state", and if "state" contains "colormode", it's a color light or a tunable white
        HueDevice::HueType hueType = HueDevice::fullcolor; // assume color (default if no "state" delivered in answer)
        JsonObjectPtr o = lightInfo->get("state");
        if (o) {
          JsonObjectPtr o2 = o->get("bri");
          if (!o2) {
            // not dimmable: must be on/off switch
            hueType = HueDevice::onoff;
          }
          else {
            // is at least dimmable
            o2 = o->get("colormode");
            if (!o2) {
              hueType = HueDevice::dimmable; // lamp without colormode -> just brightness (hue lux)
            }
            else {
              // has color mode, but might be CT only
              o2 = o->get("hue");
              if (!o2) hueType = HueDevice::colortemperature; // lamp with colormode, but without hue -> tunable white (hue ambiance)
            }
          }
        }
        // 1.4 and later FINALLY have a "uniqueid"!
        string uniqueID;
        o = lightInfo->get("uniqueid");
        if (o) uniqueID = o->stringValue();
        // create device now
        HueDevicePtr newDev = HueDevicePtr(new HueDevice(this, lightID, hueType, uniqueID));
        if (simpleIdentifyAndAddDevice(newDev)) {
          // actually added, no duplicate, set the name
          // (otherwise, this is an incremental collect and we knew this light already)
          JsonObjectPtr n = lightInfo->get("name");
          if (n) newDev->initializeName(n->stringValue());
        }
      }
    }
  }
  // collect phase done
  if (aCollectedHandler) aCollectedHandler(ErrorPtr());
}


// MARK: - Native actions (groups and scenes on vDC level)


static string hueSceneIdFromActionId(const string aNativeActionId)
{
  if (aNativeActionId.substr(0,10)=="hue_scene_") {
    return aNativeActionId.substr(10); // rest is hue bridge scene ID
  }
  return "";
}


static string hueGroupIdFromActionId(const string aNativeActionId)
{
  if (aNativeActionId.substr(0,10)=="hue_group_") {
    return aNativeActionId.substr(10); // rest is hue bridge group ID (number)
  }
  return "";
}


ErrorPtr HueVdc::announceNativeAction(const string aNativeActionId)
{
  if (!hueSceneIdFromActionId(aNativeActionId).empty()) {
    // just count to see how many
    mNumOptimizerScenes++;
  }
  else if (!hueGroupIdFromActionId(aNativeActionId).empty()) {
    // just count to see how many
    mNumOptimizerGroups++;
  }
  return ErrorPtr();
}


void HueVdc::callNativeAction(StatusCB aStatusCB, const string aNativeActionId, NotificationDeliveryStatePtr aDeliveryState)
{
  string hueActionId;
  if (aDeliveryState->mOptimizedType==ntfy_callscene) {
    hueActionId = hueSceneIdFromActionId(aNativeActionId);
    if (!hueActionId.empty()) {
      mGroupDimTicket.cancel(); // just safety, should be cancelled already
      JsonObjectPtr setGroupState = JsonObject::newObj();
      // PUT /api/<username>/groups/<groupid>/action
      // { "scene": "AB34EF5", "transitiontime":60 }
      setGroupState->add("scene", JsonObject::newString(hueActionId));
      // TODO: maybe uncomment later, but per hue API 1.33, "transitiontime" at this point does not have any effect; only the scene stored transition time is used
      //   once enabled, make sure we don't reject calls with transition time override any more in huedevice's prepareForOptimizedSet()
      // determine slowest transition time over all affected devices
      // MLMicroSeconds maxtt = 0;
      // for (DeviceList::iterator pos = aDeliveryState->affectedDevices.begin(); pos!=aDeliveryState->affectedDevices.end(); ++pos) {
      //   MLMicroSeconds devtt = (*pos)->transitionTimeForPreparedScene(true); // including override value
      //   if (devtt>maxtt) maxtt = devtt;
      // }
      // setGroupState->add("transitiontime", JsonObject::newInt64(maxtt*10/Second)); // 100mS resolution
      mHueComm.apiAction(httpMethodPUT, "/groups/0/action", setGroupState, boost::bind(&HueVdc::nativeActionDone, this, aStatusCB, _1, _2));
      return;
    }
  }
  else if (aDeliveryState->mOptimizedType==ntfy_dimchannel) {
    hueActionId = hueGroupIdFromActionId(aNativeActionId);
    if (!hueActionId.empty()) {
      // Dim group
      // - get params
      VdcDimMode dm = (VdcDimMode)aDeliveryState->mActionVariant;
      DsChannelType channelType = aDeliveryState->mActionParam;
      // - prepare call
      JsonObjectPtr setGroupState = JsonObject::newObj();
      // PUT /api/<username>/groups/<groupid>/action
      // { "bri_inc": 254, "transitiontime":70 }
      int tt = 0;
      switch (channelType) {
        case channeltype_brightness:
          setGroupState->add("bri_inc", JsonObject::newInt32(dm*254));
          tt = FULL_SCALE_DIM_TIME_MS/100; // unit is 100mS
          break;
        case channeltype_saturation:
          setGroupState->add("sat_inc", JsonObject::newInt32(dm*254));
          tt = FULL_SCALE_DIM_TIME_MS/100; // unit is 100mS
          break;
        case channeltype_hue:
          // hue must be done in smaller steps, otherwise color change is not along hue, but travels accross less saturated center of the HS wheel
          setGroupState->add("hue_inc", JsonObject::newInt32(dm*6553));
          tt = FULL_SCALE_DIM_TIME_MS/100/15; // 1/15 of full scale, unit is 100mS
          if (dm==dimmode_stop) {
            mGroupDimTicket.cancel();
          }
          else {
            setGroupState->add("transitiontime", JsonObject::newInt32(tt));
            mGroupDimTicket.executeOnce(boost::bind(&HueVdc::groupDimRepeater, this, setGroupState, tt, _1));
            aStatusCB(ErrorPtr());
          }
          break;
        default:
          aStatusCB(TextError::err("Channel type %d dimming not supported", channelType)); // causes normal execution
          return;
      }
      if (dm!=dimmode_stop) {
        setGroupState->add("transitiontime", JsonObject::newInt32(tt));
      }
      mHueComm.apiAction(httpMethodPUT, "/groups/0/action", setGroupState, boost::bind(&HueVdc::nativeActionDone, this, aStatusCB, _1, _2));
      return;
    }
  }
  aStatusCB(TextError::err("Native action '%s' not supported", aNativeActionId.c_str())); // causes normal execution
}


void HueVdc::groupDimRepeater(JsonObjectPtr aDimState, int aTransitionTime, MLTimer &aTimer)
{
  mHueComm.apiAction(httpMethodPUT, "/groups/0/action", aDimState, NoOP);
  mGroupDimTicket.executeOnce(boost::bind(&HueVdc::groupDimRepeater, this, aDimState, aTransitionTime, _1), aTransitionTime*Second/10);
}




void HueVdc::nativeActionDone(StatusCB aStatusCB, JsonObjectPtr aResult, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    // [{"success":{"/groups/1/action/scene", "value": "AB34EF5"}}]
    JsonObjectPtr s = HueComm::getSuccessItem(aResult,0);
    if (!s) {
      aError = TextError::err("call of scene (group set state) did not return a success item -> failed");
    }
  }
  FOCUSOLOG("hue Native action done with status: %s", Error::text(aError).c_str());
  if (aStatusCB) aStatusCB(aError);
}



void HueVdc::createNativeAction(StatusCB aStatusCB, OptimizerEntryPtr aOptimizerEntry, NotificationDeliveryStatePtr aDeliveryState)
{
  ErrorPtr err;
  if (aOptimizerEntry->mType==ntfy_callscene) {
    // need a free scene
    if (mNumOptimizerScenes>=mMaxOptimizerScenes) {
      // too many already
      err = Error::err<VdcError>(VdcError::NoMoreActions, "hue: max number of optimizer scenes (%d) already exist", mMaxOptimizerScenes);
    }
    else {
      // create a new scene
      JsonObjectPtr newScene = JsonObject::newObj();
      // POST /api/<username>/scenes
      // {"name":"thename", "lights":["1","2"], "recycle":false }
      string sceneName = string_format("dS-Scene_%d", aOptimizerEntry->mContentId);
      JsonObjectPtr lights = JsonObject::newArray();
      // transition time is per scene for hue. Use longest transition time among devices
      MLMicroSeconds maxtt = 0;
      for (DeviceList::iterator pos = aDeliveryState->mAffectedDevices.begin(); pos!=aDeliveryState->mAffectedDevices.end(); ++pos) {
        HueDevicePtr dev = boost::dynamic_pointer_cast<HueDevice>(*pos);
        lights->arrayAppend(JsonObject::newString(dev->mLightID));
        sceneName += ":" + dev->mLightID;
        if (sceneName.size()>32) {
          sceneName.erase(29);
          sceneName += "..."; // exactly 32
        }
        // find longest transition
        MLMicroSeconds devtt = dev->transitionTimeForPreparedScene(false); // without override value
        if (devtt>maxtt) maxtt = devtt;
      }
      newScene->add("transitiontime", JsonObject::newInt64(maxtt*10/Second));
      newScene->add("name", JsonObject::newString(sceneName)); // must be max 32 chars
      newScene->add("lights", lights);
      newScene->add("recycle", JsonObject::newBool(false));
      mHueComm.apiAction(httpMethodPOST, "/scenes", newScene, boost::bind(&HueVdc::nativeActionCreated, this, aStatusCB, aOptimizerEntry, aDeliveryState, _1, _2));
      return;
    }
  }
  else if (aOptimizerEntry->mType==ntfy_dimchannel) {
    // need a free group
    if (mNumOptimizerGroups>=mMaxOptimizerGroups) {
      // too many already
      err = Error::err<VdcError>(VdcError::NoMoreActions, "hue: max number of optimizer groups (%d) already exist", mMaxOptimizerGroups);
    }
    else {
      // create a new group
      JsonObjectPtr newGroup = JsonObject::newObj();
      // POST /api/<username>/scenes
      // {"name":"thename", "lights":["1","2"], "recycle":false }
      string groupName = "dS-Dim";
      JsonObjectPtr lights = JsonObject::newArray();
      for (DeviceList::iterator pos = aDeliveryState->mAffectedDevices.begin(); pos!=aDeliveryState->mAffectedDevices.end(); ++pos) {
        HueDevicePtr dev = boost::dynamic_pointer_cast<HueDevice>(*pos);
        lights->arrayAppend(JsonObject::newString(dev->mLightID));
        groupName += ":" + dev->mLightID;
        if (groupName.size()>32) {
          groupName.erase(29);
          groupName += "..."; // exactly 32
        }
      }
      newGroup->add("name", JsonObject::newString(groupName));
      newGroup->add("lights", lights);
      mHueComm.apiAction(httpMethodPOST, "/groups", newGroup, boost::bind(&HueVdc::nativeActionCreated, this, aStatusCB, aOptimizerEntry, aDeliveryState, _1, _2));
      return;
    }

  }
  else {
    err = TextError::err("cannot create new hue native action for type=%d", (int)aOptimizerEntry->mType);
  }
  aStatusCB(err);
}


void HueVdc::nativeActionCreated(StatusCB aStatusCB, OptimizerEntryPtr aOptimizerEntry, NotificationDeliveryStatePtr aDeliveryState, JsonObjectPtr aResult, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    // [{ "success": { "id": "Abc123Def456Ghi" } }]
    JsonObjectPtr s = HueComm::getSuccessItem(aResult, 0);
    if (s) {
      JsonObjectPtr i = s->get("id");
      if (i) {
        if (aOptimizerEntry->mType==ntfy_callscene) {
          // successfully created scene
          mNumOptimizerScenes++;
          aOptimizerEntry->mNativeActionId = "hue_scene_" + i->stringValue();
          OLOG(LOG_INFO,"created new hue scene '%s'", aOptimizerEntry->mNativeActionId.c_str());
          // TODO: if hue scene saves transitional values, we might need to call updateNativeAction() here
        }
        else if (aOptimizerEntry->mType==ntfy_dimchannel) {
          // successfully created group
          mNumOptimizerGroups++;
          aOptimizerEntry->mNativeActionId = "hue_group_" + i->stringValue();
          OLOG(LOG_INFO,"created new hue group '%s'", aOptimizerEntry->mNativeActionId.c_str());
        }
        aOptimizerEntry->mLastNativeChange = MainLoop::now();
        aStatusCB(ErrorPtr()); // success
        return;
      }
    }
    aError = TextError::err("creation of hue scene/group did not return a id -> failed");
  }
  aStatusCB(aError); // failure of some sort
}


void HueVdc::updateNativeAction(StatusCB aStatusCB, OptimizerEntryPtr aOptimizerEntry, NotificationDeliveryStatePtr aDeliveryState)
{
  ErrorPtr err;
  if (aOptimizerEntry->mType==ntfy_callscene) {
    string sceneId = hueSceneIdFromActionId(aOptimizerEntry->mNativeActionId);
    if (!sceneId.empty()) {
      string sceneId = aOptimizerEntry->mNativeActionId.substr(10); // rest is hue bridge scene ID
      // update all lights in the scene with current values
      JsonObjectPtr updatedScene = JsonObject::newObj();
      // PUT /api/<username>/scenes/<sceneid>
      // {"lights":["1","2"], "storelightstate":true }
      JsonObjectPtr lights = JsonObject::newArray();
      MLMicroSeconds maxtt = 0;
      for (DeviceList::iterator pos = aDeliveryState->mAffectedDevices.begin(); pos!=aDeliveryState->mAffectedDevices.end(); ++pos) {
        HueDevicePtr dev = boost::dynamic_pointer_cast<HueDevice>(*pos);
        // collect id to update
        lights->arrayAppend(JsonObject::newString(dev->mLightID));
        // find longest transition
        MLMicroSeconds devtt = dev->transitionTimeForPreparedScene(false); // without transition time override
        if (devtt>maxtt) maxtt = devtt;
      }
      updatedScene->add("transitiontime", JsonObject::newInt64(maxtt*10/Second));
      updatedScene->add("storelightstate", JsonObject::newBool(true));
      // actually perform scene update only after transitions are all complete (50% safety margin)
      uint64_t newHash = aOptimizerEntry->mContentsHash; // remember the correct hash for the case we can execute the delayed update
      aOptimizerEntry->mContentsHash = 0; // reset for now, scene is not up-to-date yet
      mDelayedSceneUpdateTicket.executeOnce(boost::bind(&HueVdc::performNativeSceneUpdate, this, newHash, sceneId, updatedScene, aDeliveryState->mAffectedDevices, aOptimizerEntry), maxtt*3/2);
      aStatusCB(ErrorPtr());
      return;
    }
  }
  aStatusCB(TextError::err("cannot update hue native action for type=%d", (int)aOptimizerEntry->mType));
}


void HueVdc::cancelNativeActionUpdate()
{
  // the lights will change, do not update the scene
  mDelayedSceneUpdateTicket.cancel();
}


void HueVdc::performNativeSceneUpdate(uint64_t aNewHash, string aSceneId, JsonObjectPtr aSceneUpdate, DeviceList aAffectedDevices, OptimizerEntryPtr aOptimizerEntry)
{
  // actually post update
  string url = "/scenes/" + aSceneId;
  mHueComm.apiAction(httpMethodPUT, url.c_str(), aSceneUpdate, boost::bind(&HueVdc::nativeActionUpdated, this, aNewHash, aOptimizerEntry, _1, _2));
  return;
}



void HueVdc::nativeActionUpdated(uint64_t aNewHash, OptimizerEntryPtr aOptimizerEntry, JsonObjectPtr aResult, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    // [{ "success": { "id": "Abc123Def456Ghi" } }]
    // TODO: details checks - for now just assume update has worked when request did not produce an error
    aOptimizerEntry->mLastNativeChange = MainLoop::now();
    OLOG(LOG_INFO,"updated hue scene, result: %s", JsonObject::text(aResult));
    // done, update entry
    aOptimizerEntry->mContentsHash = aNewHash;
    aOptimizerEntry->mLastNativeChange = MainLoop::now();
    aOptimizerEntry->markDirty();
  }
}


void HueVdc::freeNativeAction(StatusCB aStatusCB, const string aNativeActionId)
{
  string id;
  if (!(id = hueSceneIdFromActionId(aNativeActionId)).empty()) {
    // is a scene, delete it
    string url = "/scenes/" + id;
    mHueComm.apiAction(httpMethodDELETE, url.c_str(), NULL, boost::bind(&HueVdc::nativeActionFreed, this, aStatusCB, url, _1, _2));
  }
  else if (!(id = hueGroupIdFromActionId(aNativeActionId)).empty()) {
    string url = "/groups/" + id;
    mHueComm.apiAction(httpMethodDELETE, url.c_str(), NULL, boost::bind(&HueVdc::nativeActionFreed, this, aStatusCB, url, _1, _2));
  }
}


void HueVdc::nativeActionFreed(StatusCB aStatusCB, const string aUrl, JsonObjectPtr aResult, ErrorPtr aError)
{
  bool isScene = aUrl.find("/scenes/")!=string::npos;
  bool deleted = true; // assume deleted
  if (Error::isOK(aError)) {
    // [{"success":"/scenes/3T2SvsxvwteNNys deleted"}]
    JsonObjectPtr s = HueComm::getSuccessItem(aResult,0);
    if (!s || s->stringValue().find(aUrl)==string::npos) {
      OLOG(LOG_WARNING, "delete suceeded but did not confirm resource '%s'", aUrl.c_str());
    }
  }
  if (Error::notOK(aError)) {
    if (aError->isError(HueCommError::domain(), HueCommError::NotFound)) {
      // to be deleted item does not exist
      OLOG(LOG_WARNING, "to be deleted '%s' did not exist -> consider deleted", aUrl.c_str());
      aError.reset(); // consider deleted ok
    }
    else {
      deleted = false;
      OLOG(LOG_WARNING, "could not delete '%s': %s", aUrl.c_str(), Error::text(aError));
    }
  }
  if (deleted) {
    // action is considered gone (actually deleted or no longer existing), so count it
    if (isScene)
      mNumOptimizerScenes--;
    else
      mNumOptimizerGroups--;
  }
  if (aStatusCB) aStatusCB(aError);
}




#endif // ENABLE_HUE
