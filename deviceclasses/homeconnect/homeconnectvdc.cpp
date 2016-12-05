//
//  Copyright (c) 2016 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#define FOCUSLOGLEVEL 6

#include "homeconnectvdc.hpp"

#if ENABLE_HOMECONNECT

#include "homeconnectdevice.hpp"

using namespace p44;


HomeConnectVdc::HomeConnectVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  inherited(aInstanceNumber, aVdcHostP, aTag),
  homeConnectComm()
{
}



const char *HomeConnectVdc::vdcClassIdentifier() const
{
  return "HomeConnect_Container";
}


bool HomeConnectVdc::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("vdc_homeconnect", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


//string HomeConnectVdc::getExtraInfo()
//{
//  return string_format("hue api: %s", homeConnectComm.baseURL.c_str());
//}


void HomeConnectVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  // FIXME: implement
  aCompletedCB(ErrorPtr());
}


// MARK: ===== collect devices


int HomeConnectVdc::getRescanModes() const
{
  // normal and incremental make sense, no exhaustive mode
  return rescanmode_incremental+rescanmode_normal;
}


void HomeConnectVdc::collectDevices(StatusCB aCompletedCB, bool aIncremental, bool aExhaustive, bool aClearSettings)
{
  collectedHandler = aCompletedCB;
  if (!aIncremental) {
    // full collect, remove all devices
    removeDevices(aClearSettings);
  }
  // FIXME: fixed access token for first tests
  homeConnectComm.accessToken = "CF73126C795320E1FF2A67048CCB4504569001367584A1F92DE1967BDA04657A";
  // query all home connect devices
  homeConnectComm.apiQuery("", boost::bind(&HomeConnectVdc::deviceListReceived, this, aCompletedCB, _1, _2));
}


//{
//  "data": {
//    "homeappliances": [{
//      "haId": "BOSCH-HCS06COM1-CBF9981D149632",
//      "vib": "HCS06COM1",
//      "brand": "BOSCH",
//      "type": "CoffeeMaker",
//      "name": "CoffeeMaker Simulator",
//      "enumber": "HCS06COM1\/01",
//      "connected": true
//    }, {


void HomeConnectVdc::deviceListReceived(StatusCB aCompletedCB, JsonObjectPtr aResult, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    JsonObjectPtr o = aResult->get("data");
    if (o) {
      JsonObjectPtr has = o->get("homeappliances");
      if (has) {
        for (int i=0; i<has->arrayLength(); i++) {
          JsonObjectPtr ha = has->arrayGet(i);
          // check type
          string ty = ha->get("type")->stringValue();
          if (ty=="CoffeeMaker") {
            // create device now
            HomeConnectDevicePtr newDev = HomeConnectDevicePtr(new HomeConnectDevice(this, ha));
            if (addDevice(newDev)) {
              // actually added, no duplicate, set the name
              // (otherwise, this is an incremental collect and we knew this light already)
              JsonObjectPtr n = ha->get("name");
              if (n) newDev->initializeName(n->stringValue());
            }
          }
          else {
            ALOG(LOG_WARNING, "Not implemented home appliance type '%s' -> ignored", ty.c_str());
          }
        }
      }
      else {
        ALOG(LOG_INFO, "No home appliances");
      }
    }
  }
  if (aCompletedCB) aCompletedCB(aError);
}




ErrorPtr HomeConnectVdc::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
//  if (aMethod=="registerHueBridge") {
//    // hue specific addition, only via genericRequest
//    respErr = checkStringParam(aParams, "bridgeUuid", bridgeUuid);
//    if (!Error::isOK(respErr)) return respErr;
//    respErr = checkStringParam(aParams, "bridgeUsername", bridgeUserName);
//    if (!Error::isOK(respErr)) return respErr;
//    // save the bridge parameters
//    db.executef(
//      "UPDATE globs SET hueBridgeUUID='%s', hueBridgeUser='%s'",
//      bridgeUuid.c_str(),
//      bridgeUserName.c_str()
//    );
//    // now collect the lights from the new bridge, remove all settings from previous bridge
//    collectDevices(boost::bind(&DsAddressable::methodCompleted, this, aRequest, _1), false, false, true);
//  }
//  else
  {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }
  return respErr;
}



#endif // ENABLE_HOMECONNECT
