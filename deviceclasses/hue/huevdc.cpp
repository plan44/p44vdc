//
//  Copyright (c) 2013-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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


HueVdc::HueVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  inherited(aInstanceNumber, aVdcHostP, aTag),
  hueComm(),
  bridgeMacAddress(0)
{
  hueComm.isMemberVariable();
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
  return string_format("hue api%s: %s", fixedURL ? " (fixed)" : "", hueComm.baseURL.c_str());
}


string HueVdc::hardwareGUID()
{
  string s;
  if (bridgeMacAddress) {
    s = "macaddress:";
    s += macAddressToString(bridgeMacAddress,':');
  }
  return s;
};







// MARK: ===== DB and initialisation

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

void HueVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
	string databaseName = getPersistentDataDir();
	string_format_append(databaseName, "%s_%d.sqlite3", vdcClassIdentifier(), getInstanceNumber());
  ErrorPtr error = db.connectAndInitialize(databaseName.c_str(), HUE_SCHEMA_VERSION, HUE_SCHEMA_MIN_VERSION, aFactoryReset);
	aCompletedCB(error); // return status of DB init
  // schedule rescans
  setPeriodicRecollection(HUE_RECOLLECT_INTERVAL, rescanmode_incremental);
}



// MARK: ===== collect devices


int HueVdc::getRescanModes() const
{
  // normal and incremental make sense, no exhaustive mode
  return rescanmode_incremental+rescanmode_normal;
}


void HueVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  if (!(aRescanFlags & rescanmode_incremental)) {
    // full collect, remove all devices
    removeDevices(aRescanFlags & rescanmode_clearsettings);
  }
  // load hue bridge uuid and token
  sqlite3pp::query qry(db);
  if (qry.prepare("SELECT hueBridgeUUID, hueBridgeUser, hueApiURL, fixedURL FROM globs")==SQLITE_OK) {
    sqlite3pp::query::iterator i = qry.begin();
    if (i!=qry.end()) {
      bridgeUuid = nonNullCStr(i->get<const char *>(0));
      bridgeUserName = nonNullCStr(i->get<const char *>(1));
      bridgeApiURL = nonNullCStr(i->get<const char *>(2));
      fixedURL = i->get<bool>(3);
    }
  }
  if (bridgeUuid.length()>0 || bridgeApiURL.length()>0) {
    // we know a bridge by UUID, try to refind it
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
    collectDevices(NULL, rescanmode_incremental);
  }
}



#define REFIND_RETRY_DELAY (30*Second)

void HueVdc::refindBridge(StatusCB aCompletedCB)
{
  if (!getVdcHost().isNetworkConnected()) {
    // TODO: checking IPv4 only at this time, need to add IPv6 later
    ALOG(LOG_WARNING, "hue: device has no IP yet -> must wait ");
    MainLoop::currentMainLoop().executeOnce(boost::bind(&HueVdc::refindBridge, this, aCompletedCB), REFIND_RETRY_DELAY);
    return;
  }
  // actually refind
  hueComm.uuid = bridgeUuid;
  hueComm.userName = bridgeUserName;
  hueComm.fixedBaseURL = bridgeApiURL;
  hueComm.refindBridge(boost::bind(&HueVdc::refindResultHandler, this, aCompletedCB, _1));
}



void HueVdc::refindResultHandler(StatusCB aCompletedCB, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    // found already registered bridge again
    ALOG(LOG_NOTICE,
      "Hue bridge %s found again:\n"
      "- userName = %s\n"
      "- API base URL = %s",
      hueComm.uuid.c_str(),
      hueComm.userName.c_str(),
      hueComm.baseURL.c_str()
    );
    // save the current URL
    if (!fixedURL && hueComm.baseURL!=bridgeApiURL) {
      bridgeApiURL = hueComm.baseURL;
      // save back into database
      db.executef(
        "UPDATE globs SET hueBridgeUUID='%q', hueApiURL='%q', fixedURL=0",
        bridgeUuid.c_str(),
        bridgeApiURL.c_str()
      );
    }
    // collect existing lights
    // Note: for now we don't search for new lights, this is left to the Hue App, so users have control
    //   if they want new lights added or not
    queryBridgeAndLights(aCompletedCB);
  }
  else {
    // not found (usually timeout)
    // - if URL does not work, clear cached IP and try again (unless IP is user-provided)
    if (!bridgeApiURL.empty() && !fixedURL) {
      // forget the cached IP
      ALOG(LOG_NOTICE, "Could not access bridge API at %s - revert to finding bridge by UUID", bridgeApiURL.c_str());
      bridgeApiURL.clear();
      // retry searching by uuid
      MainLoop::currentMainLoop().executeOnce(boost::bind(&HueVdc::refindBridge, this, aCompletedCB), 500*MilliSecond);
      return;
    }
    else {
      ALOG(LOG_NOTICE, "Error refinding hue bridge uuid %s, error = %s", hueComm.uuid.c_str(), aError->description().c_str());
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
      bridgeUserName.clear();
      bridgeUuid.clear();
      bridgeApiURL = a->stringValue();
      fixedURL = false;
      if (!bridgeApiURL.empty()) {
        // make full API URL if it's just a IP or host name
        if (bridgeApiURL.substr(0,4)!="http") {
          bridgeApiURL = "http://" + bridgeApiURL + ":80/api";
        }
        // register
        bridgeUuid = PSEUDO_UUID_FOR_FIXED_API;
        fixedURL = true;
        // save the bridge parameters
        db.executef(
          "UPDATE globs SET hueBridgeUUID='%q', hueBridgeUser='', hueApiURL='%q', fixedURL=1",
          bridgeUuid.c_str(),
          bridgeApiURL.c_str()
        );
      }
      else {
        // unregister
        fixedURL = false;
        db.executef("UPDATE globs SET hueBridgeUUID='', hueBridgeUser='', hueApiURL='', fixedURL=0");
      }
      // done (separate learn-in required, because button press at the bridge is required)
      respErr = Error::ok();
    }
    else {
      // register by uuid/username (for migration)
      respErr = checkStringParam(aParams, "bridgeUuid", bridgeUuid);
      if (!Error::isOK(respErr)) return respErr;
      respErr = checkStringParam(aParams, "bridgeUsername", bridgeUserName);
      if (!Error::isOK(respErr)) return respErr;
      // save the bridge parameters
      db.executef(
        "UPDATE globs SET hueBridgeUUID='%q', hueBridgeUser='%q', hueApiURL='', fixedURL=0",
        bridgeUuid.c_str(),
        bridgeUserName.c_str()
      );
      // now collect the lights from the new bridge, remove all settings from previous bridge
      collectDevices(boost::bind(&DsAddressable::methodCompleted, this, aRequest, _1), rescanmode_clearsettings);
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
    if (!fixedURL || bridgeUserName.empty()) {
      hueComm.findNewBridge(
        string_format("%s#%s", getVdcHost().modelName().c_str(), getVdcHost().getDeviceHardwareId().c_str()).c_str(),
        15*Second, // try to login for 15 secs
        boost::bind(&HueVdc::searchResultHandler, this, aOnlyEstablish, _1)
      );
    }
  }
  else {
    // stop learning
    hueComm.stopFind();
  }
}


void HueVdc::searchResultHandler(Tristate aOnlyEstablish, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    // found and authenticated bridge
    ALOG(LOG_NOTICE,
      "Hue bridge found and logged in:\n"
      "- uuid = %s\n"
      "- userName = %s\n"
      "- API base URL = %s",
      hueComm.uuid.c_str(),
      hueComm.userName.c_str(),
      hueComm.baseURL.c_str()
    );
    // check if we found the already learned-in bridge
    Tristate learnedIn = undefined;
    if (hueComm.uuid==bridgeUuid && !fixedURL) {
      // this is the bridge that was learned in previously. Learn it out
      if (aOnlyEstablish!=yes) {
        learnedIn = no;
        // - delete it from the whitelist
        string url = "/config/whitelist/" + hueComm.userName;
        hueComm.apiAction(httpMethodDELETE, url.c_str(), JsonObjectPtr(), NULL);
        // - forget uuid + user name
        bridgeUuid.clear();
        bridgeUserName.clear();
        // - also clear base URL
        hueComm.baseURL.clear();
      }
    }
    else {
      // new bridge found
      if (aOnlyEstablish!=no) {
        learnedIn = yes;
        bridgeUuid = hueComm.uuid;
        bridgeUserName = hueComm.userName;
        if (!fixedURL) {
          bridgeApiURL = hueComm.baseURL;
        }
      }
    }
    if (learnedIn!=undefined) {
      // learning in or out requires all devices to be removed first
      // (on learn-in, the bridge's devices will be added afterwards)
      removeDevices(false);
      // actual learn-in or -out has happened
      if (learnedIn==no && !fixedURL) {
        // forget cached URL (but keep fixed ones!)
        bridgeApiURL.clear();
      }
      // save the bridge parameters
      db.executef(
        "UPDATE globs SET hueBridgeUUID='%q', hueBridgeUser='%q', hueApiURL='%q', fixedURL=0",
        bridgeUuid.c_str(),
        bridgeUserName.c_str(),
        bridgeApiURL.c_str()
      );
      // now process the learn in/out
      if (learnedIn==yes) {
        // now get lights
        queryBridgeAndLights(boost::bind(&HueVdc::learnedInComplete, this, _1));
        return;
      }
      if (learnedIn!=undefined) {
        // learn out clears MAC
        bridgeMacAddress = 0;
      }
      // report successful learn event
      getVdcHost().reportLearnEvent(learnedIn==yes, ErrorPtr());
    }
  }
  else {
    // not found (usually timeout)
    ALOG(LOG_NOTICE, "No hue bridge found to register, error = %s", aError->description().c_str());
  }
}


void HueVdc::learnedInComplete(ErrorPtr aError)
{
  getVdcHost().reportLearnEvent(true, aError);
}




void HueVdc::queryBridgeAndLights(StatusCB aCollectedHandler)
{
  // query bridge config
  ALOG(LOG_INFO, "Querying hue bridge for config...");
  hueComm.apiQuery("/config", boost::bind(&HueVdc::gotBridgeConfig, this, aCollectedHandler, _1, _2));
}


void HueVdc::gotBridgeConfig(StatusCB aCollectedHandler, JsonObjectPtr aResult, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    JsonObjectPtr o;
    // get mac address
    if (aResult->get("mac", o)) {
      bridgeMacAddress = stringToMacAddress(o->stringValue().c_str());
    }
    // get name
    if (aResult->get("name", o)) {
      if (getAssignedName().empty()) {
        // only if no name already assigned, show bridge name
        initializeName(o->stringValue());
      }
    }
  }
  // Note: can be used to incrementally search additional lights
  // issue lights query
  ALOG(LOG_INFO, "Querying hue bridge for available lights...");
  hueComm.apiQuery("/lights", boost::bind(&HueVdc::collectedLightsHandler, this, aCollectedHandler, _1, _2));
}


void HueVdc::collectedLightsHandler(StatusCB aCollectedHandler, JsonObjectPtr aResult, ErrorPtr aError)
{
  ALOG(LOG_INFO, "hue bridge reports lights = \n%s", aResult ? aResult->c_strValue() : "<none>");
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
        // 1.3 and later bridges do have "state", and if "state" contains "colormode", it's a color light
        bool hasColor = true; // assume color (default if no "state" delivered in answer)
        JsonObjectPtr o = lightInfo->get("state");
        if (o) {
          JsonObjectPtr cm = o->get("colormode");
          if (!cm) hasColor = false; // lamp without color mode -> just brightness (hue lux)
        }
        // 1.4 and later FINALLY have a "uniqueid"!
        string uniqueID;
        o = lightInfo->get("uniqueid");
        if (o) uniqueID = o->stringValue();
        // create device now
        HueDevicePtr newDev = HueDevicePtr(new HueDevice(this, lightID, hasColor, uniqueID));
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



#endif // ENABLE_HUE
