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
  announcing(Never)
{
}


DsAddressable::~DsAddressable()
{
}


string DsAddressable::modelVersion()
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
  if (announced!=Never) {
    // report to vDC API client that the device is now offline
    sendRequest("vanish", ApiValuePtr());
  }
}


// MARK: ===== vDC API



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
        access_read, query, VDC_API_DOMAIN, PropertyDescriptorPtr(),
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
        preload ? access_write_preload : access_write, value, VDC_API_DOMAIN, PropertyDescriptorPtr(),
        boost::bind(&DsAddressable::propertyAccessed, this, aRequest, _1, _2)
      );
    }
  }
  else if (aMethod=="genericRequest") {
    // the generic wrapper for calling methods from protobuf (allows new methods without expanding protobuf definitions)
    // method name must be present
    string methodName;
    if (Error::isOK(respErr = checkStringParam(aParams, "methodname", methodName))) {
      ApiValuePtr params = aParams->get("params");
      if (!params || !params->isType(apivalue_object)) {
        // no params or not object -> default to empty parameter list
        params = aRequest->newApiValue();
        params->setType(apivalue_object);
      }
      // recursively call method handler with unpacked params
      return handleMethod(aRequest, methodName, params);
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
    // read - send back property result
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



bool DsAddressable::pushNotification(ApiValuePtr aPropertyQuery, ApiValuePtr aEvents, int aDomain)
{
  if (announced!=Never) {
    // device is announced: push can take place
    if (aPropertyQuery) {
      // a property change is to be notified
      accessProperty(
        access_read, aPropertyQuery, aDomain, PropertyDescriptorPtr(),
        boost::bind(&DsAddressable::pushPropertyReady, this, aEvents, _1, _2)
      );
    }
    else {
      // no property query, event-only -> send events right away
      pushPropertyReady(aEvents, ApiValuePtr(), ErrorPtr());
    }
    return true; // although possibly (when we have properties to push) not yet sent now, assume push will be ok
  }
  else {
    if (isPublicDS()) {
      // is public, but not yet announced, show warning (non-public devices never push and must not log anything here)
      ALOG(LOG_WARNING, "pushNotification suppressed - is not yet announced");
    }
  }
  return false; // push not possible now
}


void DsAddressable::pushPropertyReady(ApiValuePtr aEvents, ApiValuePtr aResultObject, ErrorPtr aError)
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
    sendRequest("pushNotification", pushParams);
  }
  else {
    ALOG(LOG_WARNING, "push failed because to-be-pushed property could not be accessed: %s", aError->description().c_str());
  }
}




void DsAddressable::handleNotification(const string &aMethod, ApiValuePtr aParams)
{
  if (aMethod=="ping") {
    // issue device ping (which will issue a pong when device is reachable)
    ALOG(LOG_INFO, "ping -> checking presence...");
    checkPresence(boost::bind(&DsAddressable::presenceResultHandler, this, _1));
  }
  else {
    // unknown notification
    ALOG(LOG_WARNING, "unknown notification '%s'", aMethod.c_str());
  }
}


bool DsAddressable::sendRequest(const char *aMethod, ApiValuePtr aParams, VdcApiResponseCB aResponseHandler)
{
  VdcApiConnectionPtr api = getVdcHost().getSessionConnection();
  if (api) {
    if (!aParams) {
      // create params object because we need it for the dSUID
      aParams = api->newApiValue();
      aParams->setType(apivalue_object);
    }
    aParams->add("dSUID", aParams->newBinary(getDsUid().getBinary()));
    return getVdcHost().sendApiRequest(aMethod, aParams, aResponseHandler);
  }
  return false; // no connection
}



void DsAddressable::presenceResultHandler(bool aIsPresent)
{
  if (aIsPresent) {
    // send back Pong notification
    ALOG(LOG_INFO, "is present -> sending pong");
    sendRequest("pong", ApiValuePtr());
  }
  else {
    ALOG(LOG_NOTICE, "is NOT present -> no Pong sent");
  }
}



// MARK: ===== interaction with subclasses


void DsAddressable::checkPresence(PresenceCB aPresenceResultHandler)
{
  // base class just assumes being present
  aPresenceResultHandler(true);
}


// MARK: ===== property access

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
  deviceIcon16_key,
  iconName_key,
  name_key,
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
    { "deviceIcon16", apivalue_binary, deviceIcon16_key, OKEY(dsAddressable_key) },
    { "deviceIconName", apivalue_string, iconName_key, OKEY(dsAddressable_key) },
    { "name", apivalue_string, name_key, OKEY(dsAddressable_key) }
  };
  int n = inherited::numProps(aDomain, aParentDescriptor);
  if (aPropIndex<n)
    return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor); // base class' property
  aPropIndex -= n; // rebase to 0 for my own first property
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}


bool DsAddressable::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(dsAddressable_key)) {
    if (aMode!=access_read) {
      switch (aPropertyDescriptor->fieldKey()) {
        case name_key: setName(aPropValue->stringValue()); return true;
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
        case objectDescription_key: aPropValue->setStringValue(description()); return true;
        case deviceIcon16_key: { string icon; if (getDeviceIcon(icon, true, "icon16")) { aPropValue->setBinaryValue(icon); return true; } else return false; }
        case iconName_key: { string iconName; if (getDeviceIcon(iconName, false, "icon16")) { aPropValue->setStringValue(iconName); return true; } else return false; }
        case name_key: aPropValue->setStringValue(getName()); return true;
      }
      return true;
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor); // let base class handle it
}


// MARK: ===== icon loading

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


// MARK: ===== load addressable settings from files


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
      ALOG(LOG_INFO, "Customized settings from config file %s", aCSVFilepath);
    }
  }
  return anySettingsApplied;
}


// MARK: ===== description/shortDesc/logging


void DsAddressable::logAddressable(int aErrLevel, const char *aFmt, ... )
{
  va_list args;
  va_start(args, aFmt);
  // format the message
  string message = string_format("%s %s: ", entityType(), shortDesc().c_str());
  string_format_v(message, true, aFmt, args);
  va_end(args);
  globalLogger.logStr(aErrLevel, message);
}


string DsAddressable::shortDesc()
{
  // short description is dSUID...
  string s = dSUID.getString();
  // ...and user-set name, if any
  if (!name.empty())
    string_format_append(s, " (%s)", name.c_str());
  return s;
}



string DsAddressable::description()
{
  string s = string_format("%s %s - %sannounced", entityType(), shortDesc().c_str(), announced==Never ? "NOT YET " : "");
  return s;
}
