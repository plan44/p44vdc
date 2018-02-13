//
//  Copyright (c) 2017 digitalSTROM.org, Zurich, Switzerland
//
//  Author: Krystian Heberlein <krystian.heberlein@digitalstrom.com>
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
#define FOCUSLOGLEVEL 6

#include "netatmovdc.hpp"

#if ENABLE_NETATMO_V2

#include "utils.hpp"
#include "netatmodeviceenumerator.hpp"
#include "boost/range/algorithm_ext/erase.hpp"


using namespace p44;


const string NetatmoVdc::CONFIG_FILE = "config.json";

NetatmoVdc::NetatmoVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  inherited(aInstanceNumber, aVdcHostP, aTag)
{
  initializeName("Netatmo Controller");
}

NetatmoVdc::~NetatmoVdc()
{

}



const char *NetatmoVdc::vdcClassIdentifier() const
{
  return "Netatmo_Container";
}


bool NetatmoVdc::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("netatmo", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


#define NETATMO_RECOLLECT_INTERVAL (30*Minute)



void NetatmoVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  netatmoComm = make_unique<NetatmoComm>(getVdcHost().getDsParamStore(), dSUID.getString());
  deviceEnumerator = make_unique<NetatmoDeviceEnumerator>(this, *netatmoComm);

  string filePath = getVdcHost().getConfigDir();
  filePath.append(CONFIG_FILE);
  LOG(LOG_INFO, "Loading configuration from file '%s'", filePath.c_str());
  netatmoComm->loadConfigFile(JsonObject::objFromFile(filePath.c_str()));

  // schedule data polling
  MainLoop::currentMainLoop().executeOnce([&](auto...){ this->netatmoComm->pollCycle(); }, NETATMO_POLLING_START_DELAY);
  // schedule incremental re-collect from time to time
  setPeriodicRecollection(NETATMO_RECOLLECT_INTERVAL, rescanmode_incremental);
  if (aCompletedCB) aCompletedCB(Error::ok());
}



// MARK: ===== collect devices


int NetatmoVdc::getRescanModes() const
{
  // normal and incremental make sense, no exhaustive mode
  return rescanmode_incremental+rescanmode_normal;
}


ErrorPtr NetatmoVdc::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="authenticate") {

    string authData;
    respErr = checkStringParam(aParams, "authData", authData);

    if (auto jsonAuthData = JsonObject::objFromText(authData.c_str())) {
      auto accessToken = jsonutils::getJsonStringValue(jsonAuthData, "access_token");
      auto refreshToken = jsonutils::getJsonStringValue(jsonAuthData, "refresh_token");

      if (accessToken && refreshToken) {

        netatmoComm->setAccessToken(*accessToken);
        netatmoComm->setRefreshToken(*refreshToken);
        collectDevices({}, rescanmode_normal);
        respErr = Error::ok();

      } else {
        respErr = TextError::err("Cannot parse authData json");
      }

    } else {
      respErr = TextError::err("Cannot create from authData json");
    }

  } else if (aMethod=="disconnect") {
    netatmoComm->disconnect();
    collectDevices({}, rescanmode_normal);
    respErr = Error::ok();
  } else {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }

  if (aRequest) methodCompleted(aRequest, respErr);
  return respErr;
}


void NetatmoVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  if (!(aRescanFlags & rescanmode_incremental)) {
    // full collect, remove all devices
    removeDevices(aRescanFlags & rescanmode_clearsettings);
  }

  deviceEnumerator->collectDevices([=](auto aError){
    netatmoComm->pollStationsData();
    if (aCompletedCB) aCompletedCB(aError);
  });
}


static char netatmo_key;

enum {
  netatmoAccountStatus,
  netatmoUserEmail,
  netatmoVdcPropertiesMax
};


int NetatmoVdc::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // Note: only add my own count when accessing root level properties!!
  if (aParentDescriptor->isRootOfObject()) {
    // Accessing properties at the Device (root) level, add mine
    return inherited::numProps(aDomain, aParentDescriptor)+netatmoVdcPropertiesMax;
  }
  // just return base class' count
  return inherited::numProps(aDomain, aParentDescriptor);
}

PropertyDescriptorPtr NetatmoVdc::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[netatmoVdcPropertiesMax] = {
    { "netatmoAccountStatus", apivalue_string, netatmoAccountStatus, OKEY(netatmo_key) },
    { "netatmoUserEmail", apivalue_string, netatmoUserEmail, OKEY(netatmo_key) }
  };

  if (aParentDescriptor->isRootOfObject()) {
    // root level - accessing properties on the Device level
    int n = inherited::numProps(aDomain, aParentDescriptor);
    if (aPropIndex<n)
      return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor); // base class' property
    aPropIndex -= n; // rebase to 0 for my own first property
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
  }
  else {
    // other level
    return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor); // base class' property
  }
}

bool NetatmoVdc::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(netatmo_key)) {
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        case netatmoAccountStatus:{
          aPropValue->setStringValue(toString(netatmoComm->getAccountStatus()));
          return true;
        }
        case netatmoUserEmail:{
          aPropValue->setStringValue(netatmoComm->getUserEmail());
          return true;
        }
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


#endif // ENABLE_NETATMO_V2
