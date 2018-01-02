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


using namespace p44;


const string NetatmoVdc::CONFIG_FILE = "config.json";

NetatmoVdc::NetatmoVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  inherited(aInstanceNumber, aVdcHostP, aTag),
  deviceEnumerator(this, netatmoComm)
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
  string filePath = getVdcHost().getConfigDir();
  filePath.append(CONFIG_FILE);
  LOG(LOG_INFO, "Loading configuration from file '%s'", filePath.c_str());
  netatmoComm.loadConfigFile(JsonObject::objFromFile(filePath.c_str()));
  // load persistent data
  load();
  // schedule data polling
  MainLoop::currentMainLoop().executeOnce([&](auto...){ this->netatmoComm.pollCycle(); }, NETATMO_POLLING_START_DELAY);
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
    string accessToken, refreshToken;

    if (netatmoComm.getAccountStatus() != NetatmoComm::AccountStatus::disconnected) {
      respErr = TextError::err("Invalid account status");
    }

    if (Error::isOK(respErr)) {
      respErr = checkStringParam(aParams, "accessToken", accessToken);
    }

    if (Error::isOK(respErr)) {
      respErr = checkStringParam(aParams, "refreshToken", refreshToken);
    }

    if( !Error::isOK(respErr) ) {
      methodCompleted(aRequest, respErr);
      return respErr;
    }

    netatmoComm.setAccessToken(accessToken);
    netatmoComm.setRefreshToken(refreshToken);
    storeDataAndScanForDevices();

  } else if (aMethod=="authorizeByEmail") {
    string mail, password;

    if (netatmoComm.getAccountStatus() != NetatmoComm::AccountStatus::disconnected) {
      respErr = TextError::err("Invalid account status");
    }

    if (Error::isOK(respErr)) {
      respErr = checkStringParam(aParams, "email", mail);
    }

    if (Error::isOK(respErr)) {
      respErr = checkStringParam(aParams, "password", password);
    }

    if (!Error::isOK(respErr)) {
      methodCompleted(aRequest, respErr);
      return respErr;
    }

    netatmoComm.authorizeByEmail(mail, password, [&](ErrorPtr aError){
      if (Error::isOK(aError)) storeDataAndScanForDevices();
    });
  } else if (aMethod=="disconnect") {
    netatmoComm.disconnect();
    storeDataAndScanForDevices();
  } else {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }

  if (aRequest) methodCompleted(aRequest, respErr);
  return respErr;
}

void NetatmoVdc::storeDataAndScanForDevices()
{
  markDirty();
  save();
  collectDevices({}, rescanmode_normal);
}


void NetatmoVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  if (!(aRescanFlags & rescanmode_incremental)) {
    // full collect, remove all devices
    removeDevices(aRescanFlags & rescanmode_clearsettings);
  }

  deviceEnumerator.collectDevices(aCompletedCB);
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
          aPropValue->setStringValue(netatmoComm.getAccountStatusString());
          return true;
        }
        case netatmoUserEmail:{
          aPropValue->setStringValue(netatmoComm.getUserEmail());
          return true;
        }
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}



///Number of fields in Database
static const size_t numFields = 3;


size_t NetatmoVdc::numFieldDefs()
{
  return inherited::numFieldDefs()+numFields;
}


const FieldDefinition *NetatmoVdc::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numFields] = {
    { "accessToken", SQLITE_TEXT },
    { "refreshToken", SQLITE_TEXT },
    { "userEmail", SQLITE_TEXT }
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void NetatmoVdc::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inherited::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get the field values
  netatmoComm.setAccessToken(nonNullCStr(aRow->get<const char *>(aIndex++)));
  netatmoComm.setRefreshToken(nonNullCStr(aRow->get<const char *>(aIndex++)));
  netatmoComm.setUserEmail(nonNullCStr(aRow->get<const char *>(aIndex++)));
}


// bind values to passed statement
void NetatmoVdc::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, netatmoComm.getAccessToken().c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, netatmoComm.getRefreshToken().c_str(), false);
  aStatement.bind(aIndex++, netatmoComm.getUserEmail().c_str(), false);
}


#endif // ENABLE_NETATMO_V2
