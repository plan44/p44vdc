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

#include "utils.hpp"

using namespace p44;


HomeConnectVdc::HomeConnectVdc(int aInstanceNumber, bool aDeveloperApi, VdcHost *aVdcHostP, int aTag) :
  inherited(aInstanceNumber, aVdcHostP, aTag),
  homeConnectComm()
{
  homeConnectComm.setDeveloperApi(aDeveloperApi);
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



// MARK: ===== DB and initialisation

// Version history
//  1 : first version
//  2 : second, completely incompatible version
#define HOMECONNECT_SCHEMA_MIN_VERSION 2 // minimally supported version, anything older will be deleted
#define HOMECONNECT_SCHEMA_VERSION 2 // current version

string HomeConnectPersistence::dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion)
{
  string sql;
  if (aFromVersion==0) {
    // create DB from scratch
    // - use standard globs table for schema version
    sql = inherited::dbSchemaUpgradeSQL(aFromVersion, aToVersion);
    // - add fields to globs table
    sql.append(
      "ALTER TABLE globs ADD authData TEXT;"
      "ALTER TABLE globs ADD authScope TEXT;"
    );
    // reached final version in one step
    aToVersion = HOMECONNECT_SCHEMA_VERSION;
  }
  return sql;
}


void HomeConnectVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  string databaseName = getPersistentDataDir();
  string_format_append(databaseName, "%s_%d.sqlite3", vdcClassIdentifier(), getInstanceNumber());
  ErrorPtr error = db.connectAndInitialize(databaseName.c_str(), HOMECONNECT_SCHEMA_VERSION, HOMECONNECT_SCHEMA_MIN_VERSION, aFactoryReset);
  if (Error::isOK(error)) {
    // load account parameters
    sqlite3pp::query qry(db);
    if (qry.prepare("SELECT authData")==SQLITE_OK) {
      sqlite3pp::query::iterator i = qry.begin();
      if (i!=qry.end()) {
        // authorize
        string authData = nonNullCStr(i->get<const char *>(0));
        homeConnectComm.setAuthentication(authData);
      }
    }
  }
  aCompletedCB(error); // return status of DB init
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
  if (homeConnectComm.isConfigured()) {
    // query all home connect appliances
    homeConnectComm.apiQuery("/api/homeappliances", boost::bind(&HomeConnectVdc::deviceListReceived, this, aCompletedCB, _1, _2));
  }
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
  if (aMethod=="authenticate") {
    // oauth API specific addition, only via genericRequest
    string authData;
    string authScope;
    respErr = checkStringParam(aParams, "authData", authData);
    if (!Error::isOK(respErr)) return respErr;
    checkStringParam(aParams, "authScope", authScope);
    homeConnectComm.setAuthentication(authData);
    // save the account parameters
    db.executef(
      "UPDATE globs SET authData='%s', authScope='%s'",
      authData.c_str(),
      authScope.c_str()
    );
    // now collect the devices from the new account
    collectDevices(boost::bind(&DsAddressable::methodCompleted, this, aRequest, _1), false, false, true);
  }
  else
  {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }
  return respErr;
}



#endif // ENABLE_HOMECONNECT
