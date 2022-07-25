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


// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 0


#include "dsaddressable.hpp"

#include "vdchost.hpp"

using namespace p44;



DsAddressable::DsAddressable(VdcHost *aVdcHostP) :
  vdcHostP(aVdcHostP),
  announced(Never),
  announcing(Never),
  present(true), // by default, consider addressable present
  lastPresenceUpdate(Never)
  #if ENABLE_JSONBRIDGEAPI
  , mBridged(false) // by default, addressable is not bridged
  #endif
{
}


DsAddressable::~DsAddressable()
{
}


string DsAddressable::modelVersion() const
{
  // Note: it is important to override this at vdchost level, because would loop otherwise
  return getVdcHost().modelVersion();
}


string DsAddressable::displayId()
{
  string schema, id;
  if (!keyAndValue(hardwareGUID(), schema, id, ':')) {
    id = hardwareGUID();
  }
  return id;
}


void DsAddressable::setName(const string &aName)
{
  // TODO: for now dsm API truncates names to 20 bytes. Therefore,
  //   we prevent replacing a long name with a truncated version
  if (name!=aName && (name.length()<20 || name.substr(0,20)!=aName)) {
    name = aName;
  }
}


void DsAddressable::initializeName(const string &aName)
{
  // just assign
  name = aName;
}


void DsAddressable::reportVanished()
{
  if (isAnnounced()) {
    // report to vDC API client that the device is now offline
    sendRequest(getVdcHost().getVdsmSessionConnection(), "vanish", ApiValuePtr());
    #if ENABLE_JSONBRIDGEAPI
    // also report to connected bridges
    sendRequest(getVdcHost().getBridgeApi(), "vanish", ApiValuePtr());
    #endif
  }
}


bool DsAddressable::isPublicDS()
{
  // public dS when vdchost has API enabled at all (i.e. not localcontroller-only mode)
  return vdcHostP && vdcHostP->vdcApiServer;
}


// MARK: - vDC API



ErrorPtr DsAddressable::checkParam(ApiValuePtr aParams, const char *aParamName, ApiValuePtr &aParam)
{
  ErrorPtr err;
  if (aParams)
    aParam = aParams->get(aParamName);
  else
    aParam.reset();
  if (!aParam)
    err = Error::err<VdcApiError>(400, "Invalid Parameters - missing '%s'",aParamName);
  return err;
}


ErrorPtr DsAddressable::checkStringParam(ApiValuePtr aParams, const char *aParamName, string &aString)
{
  ErrorPtr err;
  ApiValuePtr o;
  err = checkParam(aParams, aParamName, o);
  if (Error::isOK(err)) {
    aString = o->stringValue();
  }
  return err;
}


ErrorPtr DsAddressable::checkBoolParam(ApiValuePtr aParams, const char *aParamName, bool &aBool)
{
  ErrorPtr err;
  ApiValuePtr o;
  err = checkParam(aParams, aParamName, o);
  if (Error::isOK(err)) {
    aBool = o->boolValue();
  }
  return err;
}



ErrorPtr DsAddressable::checkDsuidParam(ApiValuePtr aParams, const char *aParamName, DsUid &aDsUid)
{
  ErrorPtr err;
  ApiValuePtr o;
  err = checkParam(aParams, aParamName, o);
  if (Error::isOK(err)) {
    aDsUid.setAsBinary(o->binaryValue());
  }
  return err;
}




ErrorPtr DsAddressable::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="getProperty") {
    // query must be present
    ApiValuePtr query;
    if (Error::isOK(respErr = checkParam(aParams, "query", query))) {
      // now read
      accessProperty(
        access_read, query, VDC_API_DOMAIN, aRequest->getApiVersion(),
        boost::bind(&DsAddressable::propertyAccessed, this, aRequest, _1, _2)
      );
    }
  }
  else if (aMethod=="setProperty") {
    // properties must be present
    ApiValuePtr value;
    if (Error::isOK(respErr = checkParam(aParams, "properties", value))) {
      // check preload flag
      bool preload = false;
      ApiValuePtr o = aParams->get("preload");
      if (o) {
        preload = o->boolValue();
      }
      accessProperty(
        preload ? access_write_preload : access_write, value, VDC_API_DOMAIN, aRequest->getApiVersion(),
        boost::bind(&DsAddressable::propertyAccessed, this, aRequest, _1, _2)
      );
    }
  }
  else if (aMethod=="genericRequest") {
    // the generic wrapper for calling methods from protobuf (allows new methods without expanding protobuf definitions)
    // method name must be present
    string methodName;
    if (Error::isOK(respErr = checkStringParam(aParams, "methodname", methodName))) {
      if (methodName=="genericRequest") {
        respErr = Error::err<VdcApiError>(415, "recursive call of genericRequest");
      }
      else {
        ApiValuePtr params = aParams->get("params");
        if (!params || !params->isType(apivalue_object)) {
          // no params or not object -> default to empty parameter list
          params = aRequest->newApiValue();
          params->setType(apivalue_object);
        }
        // recursively call method handler with unpacked params
        respErr = handleMethod(aRequest, methodName, params);
        if (Error::isError(respErr, VdcApiError::domain(), 405)) {
          // unknown method (or syntax error in params), but not actual failure of method operation: try as notification
          if (handleNotification(aRequest->connection(), methodName, params)) {
            // successful initiation of notification via genericRequest *method* call, confirm with simple OK
            respErr = Error::ok();
          }
        }
        return respErr;
      }
    }
  }
  else if (aMethod=="loglevel") {
    // via genericRequest
    ApiValuePtr o;
    if (Error::isOK(respErr = checkParam(aParams, "value", o))) {
      int newLevel = o->int32Value();
      if (newLevel==8) {
        // trigger statistics
        LOG(LOG_NOTICE, "\n========== requested showing statistics");
        getVdcHost().postEvent(vdchost_logstats);
        LOG(LOG_NOTICE, "\n%s", MainLoop::currentMainLoop().description().c_str());
        MainLoop::currentMainLoop().statistics_reset();
        LOG(LOG_NOTICE, "========== statistics shown\n");
        respErr = Error::ok(); // return OK as generic response
      }
      else if (newLevel>=0 && newLevel<=7) {
        int oldLevel = LOGLEVEL;
        SETLOGLEVEL(newLevel);
        LOG(newLevel, "\n\n========== changed log level from %d to %d ===============", oldLevel, newLevel);
        respErr = Error::ok(); // return OK as generic response
      }
      else {
        respErr = Error::err<VdcApiError>(405, "invalid log level %d", newLevel);
      }
    }
  }
  else {
    respErr = Error::err<VdcApiError>(405, "unknown method '%s'", aMethod.c_str());
  }
  return respErr;
}


void DsAddressable::propertyAccessed(VdcApiRequestPtr aRequest, ApiValuePtr aResultObject, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    // ok - return result object, which is property result tree for getProperty,
    // and usually null for writes except when a object was created
    aRequest->sendResult(aResultObject);
  }
  else {
    // just return status or error
    aRequest->sendStatus(aError);
  }
}




void DsAddressable::methodCompleted(VdcApiRequestPtr aRequest, ErrorPtr aError)
{
  // method completed, return status
  aRequest->sendStatus(aError);
}



bool DsAddressable::pushNotification(VdcApiConnectionPtr aApi, ApiValuePtr aPropertyQuery, ApiValuePtr aEvents, bool aDeletedProperty)
{
  if (!aApi) return false; // safety
  if (aApi->domain()!=VDC_API_DOMAIN || isAnnounced()) {
    // device is announced: push can take place
    if (aPropertyQuery) {
      if (aDeletedProperty) {
        // a property deletion is to be notified -> the query itself is what needs to be pushed
        pushPropertyReady(aApi, aEvents, aPropertyQuery, ErrorPtr());
      }
      else {
        // a property change is to be notified -> read the property
        accessProperty(
          access_read, aPropertyQuery, aApi->domain(), aApi->getApiVersion(),
          boost::bind(&DsAddressable::pushPropertyReady, this, aApi, aEvents, _1, _2)
        );
      }
    }
    else {
      // no property query, event-only -> send events right away
      pushPropertyReady(aApi, aEvents, ApiValuePtr(), ErrorPtr());
    }
    return true; // although possibly (when we have properties to push) not yet sent now, assume push will be ok
  }
  else {
    if (isPublicDS() && aApi->domain()==VDC_API_DOMAIN) {
      // is public, but not yet announced, show warning (non-public devices never push and must not log anything here)
      OLOG(LOG_WARNING, "pushNotification suppressed - is not yet announced");
    }
  }
  return false; // push not possible now
}


void DsAddressable::pushPropertyReady(VdcApiConnectionPtr aApi, ApiValuePtr aEvents, ApiValuePtr aResultObject, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    // - send pushNotification
    ApiValuePtr pushParams;
    if (aResultObject) {
      pushParams = aResultObject->newValue(apivalue_object);
      pushParams->add("changedproperties", aResultObject);
    }
    if (aEvents) {
      if (!pushParams) pushParams = aEvents->newValue(apivalue_object);
      pushParams->add("deviceevents", aEvents);
    }
    sendRequest(aApi, "pushNotification", pushParams);
  }
  else {
    OLOG(LOG_WARNING, "push failed because to-be-pushed property could not be accessed: %s", aError->text());
  }
}




bool DsAddressable::handleNotification(VdcApiConnectionPtr aApiConnection, const string &aNotification, ApiValuePtr aParams)
{
  if (aNotification=="ping") {
    // issue device ping (which will issue a pong when device is reachable)
    OLOG(LOG_INFO, "ping -> checking presence...");
    checkPresence(boost::bind(&DsAddressable::pingResultHandler, this, _1));
  }
  else if (aNotification=="identify") {
    // identify to user
    OLOG(LOG_NOTICE, "Identify");
    identifyToUser();
  }
  else {
    // unknown notification
    OLOG(LOG_WARNING, "unknown notification '%s'", aNotification.c_str());
    return false;
  }
  return true;
}


void DsAddressable::identifyToUser()
{
  OLOG(LOG_WARNING, "***** 'identify' called (but addressable does not have a hardware implementation for it)");
}


bool DsAddressable::sendRequest(VdcApiConnectionPtr aApi, const char *aMethod, ApiValuePtr aParams, VdcApiResponseCB aResponseHandler)
{
  if (aApi) {
    if (!aParams) {
      // create params object because we need it for the dSUID
      aParams = aApi->newApiValue();
      aParams->setType(apivalue_object);
    }
    aParams->add("dSUID", aParams->newBinary(getDsUid().getBinary()));
    ErrorPtr err = aApi->sendRequest(aMethod, aParams, aResponseHandler);
    if (Error::isOK(err)) return true;
    LOG(LOG_ERR, "Failed to send request to %s-API (domain 0x%x)", aApi->apiName(), aApi->domain());
  }
  return false; // no API/connection
}



void DsAddressable::pingResultHandler(bool aIsPresent)
{
  // update the state
  updatePresenceState(aIsPresent);
  // send pong
  if (aIsPresent) {
    // send back Pong notification
    OLOG(LOG_INFO, "is present -> sending pong");
    sendRequest(getVdcHost().getVdsmSessionConnection(), "pong", ApiValuePtr());
  }
  else {
    OLOG(LOG_NOTICE, "is NOT present -> no Pong sent");
  }
}



void DsAddressable::updatePresenceState(bool aPresent)
{
  bool first = lastPresenceUpdate==Never;
  lastPresenceUpdate = MainLoop::now();
  if (aPresent!=present || first) {
    // change in presence
    present = aPresent;
    OLOG(LOG_NOTICE, "changes to %s", aPresent ? "PRESENT" : "OFFLINE");
    // push change in presence to vdSM
    VdcApiConnectionPtr api = getVdcHost().getVdsmSessionConnection();
    if (api) {
      ApiValuePtr query = api->newApiValue();
      query->setType(apivalue_object);
      query->add("active", query->newValue(apivalue_null));
      pushNotification(api, query, ApiValuePtr(), VDC_API_DOMAIN);
    }
    // also push change in presence to bridgeAPI
    #if ENABLE_JSONBRIDGEAPI
    if (isBridged()) {
      VdcApiConnectionPtr api = getVdcHost().getBridgeApi();
      if (api) {
        ApiValuePtr query = api->newApiValue();
        query->setType(apivalue_object);
        query->add("active", query->newValue(apivalue_null));
        pushNotification(api, query, ApiValuePtr());
      }
    }
    #endif
  }
}



// MARK: - interaction with subclasses


void DsAddressable::checkPresence(PresenceCB aPresenceResultHandler)
{
  // base class just confirms current state
  aPresenceResultHandler(present);
}


// MARK: - property access

enum {
  type_key,
  dSUID_key,
  model_key,
  displayId_key,
  modelUID_key,
  modelVersion_key,
  hardwareVersion_key,
  hardwareGUID_key,
  hardwareModelGUID_key,
  oemGUID_key,
  oemModelGUID_key,
  vendorId_key,
  vendorName_key,
  webui_url_key,
  extraInfo_key,
  objectDescription_key,
  statusText_key,
  opStateLevel_key,
  opStateText_key,
  logLevelOffset_key,
  deviceIcon16_key,
  iconName_key,
  name_key,
  active_key,
  #if ENABLE_JSONBRIDGEAPI
  isBridged_key,
  bridgeable_key,
  #endif
  numDsAddressableProperties
};


static char dsAddressable_key;

int DsAddressable::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  return inherited::numProps(aDomain, aParentDescriptor)+numDsAddressableProperties;
}


PropertyDescriptorPtr DsAddressable::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numDsAddressableProperties] = {
    { "type", apivalue_string, type_key, OKEY(dsAddressable_key) },
    { "dSUID", apivalue_binary, dSUID_key, OKEY(dsAddressable_key) },
    { "model", apivalue_string, model_key, OKEY(dsAddressable_key) },
    { "displayId", apivalue_string, displayId_key, OKEY(dsAddressable_key) },
    { "modelUID", apivalue_string, modelUID_key, OKEY(dsAddressable_key) },
    { "modelVersion", apivalue_string, modelVersion_key, OKEY(dsAddressable_key) },
    { "hardwareVersion", apivalue_string, hardwareVersion_key, OKEY(dsAddressable_key) },
    { "hardwareGuid", apivalue_string, hardwareGUID_key, OKEY(dsAddressable_key) },
    { "hardwareModelGuid", apivalue_string, hardwareModelGUID_key, OKEY(dsAddressable_key) },
    { "oemGuid", apivalue_string, oemGUID_key, OKEY(dsAddressable_key) },
    { "oemModelGuid", apivalue_string, oemModelGUID_key, OKEY(dsAddressable_key) },
    { "vendorId", apivalue_string, vendorId_key, OKEY(dsAddressable_key) },
    { "vendorName", apivalue_string, vendorName_key, OKEY(dsAddressable_key) },
    { "configURL", apivalue_string, webui_url_key, OKEY(dsAddressable_key) },
    { "x-p44-extraInfo", apivalue_string, extraInfo_key, OKEY(dsAddressable_key) },
    { "x-p44-description", apivalue_string, objectDescription_key, OKEY(dsAddressable_key) },
    { "x-p44-statusText", apivalue_string, statusText_key, OKEY(dsAddressable_key) },
    { "x-p44-opStateLevel", apivalue_uint64, opStateLevel_key, OKEY(dsAddressable_key) },
    { "x-p44-opStateText", apivalue_string, opStateText_key, OKEY(dsAddressable_key) },
    { "x-p44-logLevelOffset", apivalue_int64, logLevelOffset_key, OKEY(dsAddressable_key) },
    { "deviceIcon16", apivalue_binary, deviceIcon16_key, OKEY(dsAddressable_key) },
    { "deviceIconName", apivalue_string, iconName_key, OKEY(dsAddressable_key) },
    { "name", apivalue_string, name_key, OKEY(dsAddressable_key) },
    { "active", apivalue_bool+propflag_needsreadprep, active_key, OKEY(dsAddressable_key) },
    #if ENABLE_JSONBRIDGEAPI
    { "x-p44-bridged", apivalue_bool, isBridged_key, OKEY(dsAddressable_key) },
    { "x-p44-bridgeable", apivalue_bool, bridgeable_key, OKEY(dsAddressable_key) },
    #endif
  };
  int n = inherited::numProps(aDomain, aParentDescriptor);
  if (aPropIndex<n)
    return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor); // base class' property
  aPropIndex -= n; // rebase to 0 for my own first property
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}


#define MIN_PRESENCE_SAMPLE_INTERVAL (120*Second)

void DsAddressable::prepareAccess(PropertyAccessMode aMode, PropertyDescriptorPtr aPropertyDescriptor, StatusCB aPreparedCB)
{
  if (aPropertyDescriptor->hasObjectKey(dsAddressable_key) && aPropertyDescriptor->fieldKey()==active_key) {
    // update status in case
    if (lastPresenceUpdate+MIN_PRESENCE_SAMPLE_INTERVAL<MainLoop::now()) {
      // request update from device
      checkPresence(boost::bind(&DsAddressable::presenceSampleHandler, this, aPreparedCB, _1));
      return;
    }
  }
  // nothing to do here, let inherited handle it
  inherited::prepareAccess(aMode, aPropertyDescriptor, aPreparedCB);
}


void DsAddressable::presenceSampleHandler(StatusCB aPreparedCB, bool aIsPresent)
{
  updatePresenceState(aIsPresent);
  aPreparedCB(ErrorPtr());
}



bool DsAddressable::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(dsAddressable_key)) {
    if (aMode!=access_read) {
      switch (aPropertyDescriptor->fieldKey()) {
        case name_key: setName(aPropValue->stringValue()); return true;
        case logLevelOffset_key: setLogLevelOffset(aPropValue->int32Value()); return true;
        #if ENABLE_JSONBRIDGEAPI
        case isBridged_key: if (bridgeable()) { mBridged = aPropValue->boolValue(); return true; } else return false;
        #endif
      }
    }
    else {
      switch (aPropertyDescriptor->fieldKey()) {
        case type_key: aPropValue->setStringValue(entityType()); return true; // the entity type
        case dSUID_key: aPropValue->setStringValue(dSUID.getString()); return true; // always the real dSUID
        case model_key: aPropValue->setStringValue(modelName()); return true; // human readable model identification
        case displayId_key: aPropValue->setStringValue(displayId()); return true; // human readable device instance identification
        case modelUID_key: aPropValue->setStringValue(modelUID()); return true; // unique model identification, same features = same model
        case modelVersion_key: if (modelVersion().size()>0) { aPropValue->setStringValue(modelVersion()); return true; } else return false;
        case hardwareVersion_key: if (hardwareVersion().size()>0) { aPropValue->setStringValue(hardwareVersion()); return true; } else return false;
        case hardwareGUID_key: if (hardwareGUID().size()>0) { aPropValue->setStringValue(hardwareGUID()); return true; } else return false;
        case hardwareModelGUID_key: if (hardwareModelGUID().size()>0) { aPropValue->setStringValue(hardwareModelGUID()); return true; } else return false;
        case oemGUID_key: if (oemGUID().size()>0) { aPropValue->setStringValue(oemGUID()); return true; } else return false;
        case oemModelGUID_key: if (oemModelGUID().size()>0) { aPropValue->setStringValue(oemModelGUID()); return true; } else return false;
        case vendorId_key: if (vendorId().size()>0) { aPropValue->setStringValue(vendorId()); return true; } else return false;
        case vendorName_key: if (vendorName().size()>0) { aPropValue->setStringValue(vendorName()); return true; } else return false;
        case webui_url_key: if (webuiURLString().size()>0) { aPropValue->setStringValue(webuiURLString()); return true; } else return false;
        case extraInfo_key: if (getExtraInfo().size()>0) { aPropValue->setStringValue(getExtraInfo()); return true; } else return false;
        case statusText_key: aPropValue->setStringValue(getStatusText()); return true;
        case opStateLevel_key: { int l=opStateLevel(); if (l<0) aPropValue->setNull(); else aPropValue->setUint16Value(l); return true; }
        case opStateText_key: aPropValue->setStringValue(getOpStateText()); return true;
        case logLevelOffset_key: { int o=getLocalLogLevelOffset(); if (o==0) return false; else aPropValue->setInt32Value(o); return true; }
        case objectDescription_key: aPropValue->setStringValue(description()); return true;
        case deviceIcon16_key: { string icon; if (getDeviceIcon(icon, true, "icon16")) { aPropValue->setBinaryValue(icon); return true; } else return false; }
        case iconName_key: { string iconName; if (getDeviceIcon(iconName, false, "icon16")) { aPropValue->setStringValue(iconName); return true; } else return false; }
        case name_key: aPropValue->setStringValue(getName()); return true;
        case active_key: aPropValue->setBoolValue(present); return true;
        #if ENABLE_JSONBRIDGEAPI
        case isBridged_key: aPropValue->setBoolValue(isBridged()); return true;
        case bridgeable_key: aPropValue->setBoolValue(bridgeable()); return true;
        #endif
      }
      return true;
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor); // let base class handle it
}


// MARK: - icon loading

bool DsAddressable::getIcon(const char *aIconName, string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  DBGLOG(LOG_DEBUG, "Trying to load icon named '%s/%s' for dSUID %s", aResolutionPrefix, aIconName, dSUID.getString().c_str());
  const char *iconDir = getVdcHost().getIconDir();
  if (iconDir && *iconDir) {
    string iconPath = string_format("%s%s/%s.png", iconDir, aResolutionPrefix, aIconName);
    // TODO: maybe add cache lookup here
    // try to access this file
    int fildes = open(iconPath.c_str(), O_RDONLY);
    if (fildes<0) {
      return false; // can't load from this location
    }
    // file seems to exist
    if (aWithData) {
      // load it
      ssize_t bytes = 0;
      const size_t bufsize = 4096; // usually a 16x16 png is 3.4kB
      char buffer[bufsize];
      aIcon.clear();
      while (true) {
        bytes = read(fildes, buffer, bufsize);
        if (bytes<=0)
          break; // done
        aIcon.append(buffer, bytes);
      }
      close(fildes);
      // done
      if (bytes<0) {
        // read error, do not return half-read icon
        aIcon.clear();
        return false;
      }
      DBGLOG(LOG_DEBUG, "- successfully loaded icon named '%s'", aIconName);
    }
    else {
      // just name
      close(fildes);
      aIcon = aIconName; // this is a name for which the file exists
    }
    return true;
  }
  else {
    // no icon dir
    if (aWithData) {
      // data requested but no icon dir -> cannot return data
      return false;
    }
    else {
      // name requested but no directory to check for file -> always just return name
      aIcon = aIconName;
      return true;
    }
  }
}


static const char *classColors[] = {
  "white", // undefined are shown as white
  "yellow", // Light
  "grey", // shadow
  "blue", // climate
  "cyan", // audio
  "magenta", // video
  "red", // security
  "green", // access
  "black", // joker
  "white" // singledevices
};

const int numKnownColors = sizeof(classColors)/sizeof(const char *);


bool DsAddressable::getClassColoredIcon(const char *aIconName, DsClass aClass, string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  string  iconName;
  bool found = false;
  if (aClass<numKnownColors) {
    // try first with color name
    found = getIcon(string_format("%s_%s", aIconName, classColors[aClass]).c_str(), aIcon, aWithData, aResolutionPrefix);
  }
  else {
    // for groups without known class color, use _n suffix (n=class no)
    found = getIcon(string_format("%s_%d", aIconName, (int)aClass).c_str(), aIcon, aWithData, aResolutionPrefix);
  }
  if (!found) {
    // no class-specific icon found, try with "other"
    found = getIcon(string_format("%s_other", aIconName).c_str(), aIcon, aWithData, aResolutionPrefix);
  }
  return found;
}


bool DsAddressable::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("unknown", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return false;
}


string DsAddressable::vendorId()
{
  // by default, use vendorname: URN schema
  if (!vendorName().empty())
    return string_format("vendorname:%s", vendorName().c_str());
  else
    return "";
}



string DsAddressable::webuiURLString()
{
  // Note: it is important to override this at vdchost level, because would loop otherwise
  return getVdcHost().webuiURLString(); // by default, return vDC host's config URL
}


// MARK: - load addressable settings from files

#if ENABLE_SETTINGS_FROM_FILES

bool DsAddressable::loadSettingsFromFile(const char *aCSVFilepath, bool aOnlyExplicitlyOverridden)
{
  bool anySettingsApplied = false;
  string line;
  int lineNo = 0;
  FILE *file = fopen(aCSVFilepath, "r");
  if (!file) {
    int syserr = errno;
    if (syserr!=ENOENT) {
      // file not existing is ok, all other errors must be reported
      LOG(LOG_ERR, "failed opening file %s - %s", aCSVFilepath, strerror(syserr));
    }
    // NOP
  }
  else {
    // file opened
    while (string_fgetline(file, line)) {
      lineNo++;
      const char *p = line.c_str();
      // process CSV line as property name/value pairs
      anySettingsApplied = readPropsFromCSV(VDC_API_DOMAIN, aOnlyExplicitlyOverridden, p, aCSVFilepath, lineNo) || anySettingsApplied;
    }
    fclose(file);
    if (anySettingsApplied) {
      OLOG(LOG_INFO, "Customized settings from config file %s", aCSVFilepath);
    }
  }
  return anySettingsApplied;
}

#endif // ENABLE_SETTINGS_FROM_FILES


// MARK: - description/shortDesc/logging

string DsAddressable::logContextPrefix()
{
  return string_format("%s %s", entityType(), shortDesc().c_str());
}


string DsAddressable::shortDesc()
{
  // short description is dSUID...
  string s = dSUID.getString();
  // ...and user-set name, if any
  if (!getName().empty())
    string_format_append(s, " (%s)", getName().c_str());
  return s;
}



string DsAddressable::description()
{
  string s = string_format("%s %s - %sannounced", entityType(), shortDesc().c_str(), announced==Never ? "NOT YET " : "");
  return s;
}
