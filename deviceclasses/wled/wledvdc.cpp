//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Michael Troß <digitalstrom@tross.org>
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

#include "wledvdc.hpp"
#include "wleddevice.hpp"

#if ENABLE_WLED

#define WLED_DB_VERSION 1

using namespace p44;


// MARK: - WledPersistence

string WledPersistence::schemaUpgradeSQL(int aFromVersion, int &aToVersion)
{
  string sql;

  if (aFromVersion < 1) {
    // create table group from scratch
    // - use standard globs table for schema version
    sql = inherited::schemaUpgradeSQL(aFromVersion, aToVersion);
    // - add fields to globs table
    sql.append(
      "ALTER TABLE $PREFIX_globs ADD wledDeviceURL TEXT;"
      "ALTER TABLE $PREFIX_globs ADD wledDeviceHostname TEXT;"
    );
    aToVersion = 1;
  }

  return sql;
}


// MARK: - WledVdc

WledVdc::WledVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  inherited(aInstanceNumber, aVdcHostP, aTag),
  mAutoDiscovery(true)
{
  LOG(LOG_DEBUG, "WledVdc constructor: instance %d, tag %d", aInstanceNumber, aTag);
  mWledComm.isMemberVariable();
}


WledVdc::~WledVdc()
{
  // Destructor
}


void WledVdc::setLogLevelOffset(int aLogLevelOffset)
{
  mWledComm.setLogLevelOffset(aLogLevelOffset);
  inherited::setLogLevelOffset(aLogLevelOffset);
}


P44LoggingObj* WledVdc::getTopicLogObject(const string aTopic)
{
  if (uequals(aTopic, "wledcomm")) {
    return &mWledComm;
  }
  return inherited::getTopicLogObject(aTopic);
}


const char *WledVdc::vdcClassIdentifier() const
{
  return "wled_color_lights_container";
}


void WledVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  LOG(LOG_NOTICE, ">>> WLED VDC initialize() called (instance %d)", getInstanceNumber());

  // Load persistent params
  load();

  // Initialize database
  LOG(LOG_DEBUG, "Initializing WLED persistence...");
  ErrorPtr err = initializePersistence(mDb, WLED_DB_VERSION, 1);
  if (Error::notOK(err)) {
    LOG(LOG_ERR, "Failed to initialize WLED database: %s", err->text());
    if (aCompletedCB) aCompletedCB(err);
    return;
  }

  // Setup periodic device recollection
  LOG(LOG_DEBUG, "Setting up periodic WLED device recollection");
  setPeriodicRecollection(30*Second, rescanmode_incremental);

  // signal ready — device collection happens via scanForDevices()
  if (aCompletedCB) aCompletedCB(ErrorPtr());
}


void WledVdc::initializeConnection(StatusCB aCompletedCB)
{
  if (mDeviceHostname.empty()) {
    // No device configured yet
    LOG(LOG_INFO, "No WLED device hostname configured");
    if (aCompletedCB) aCompletedCB(ErrorPtr());
    return;
  }

  // Base URL is just http://hostname — queueApiCall() appends /json/<path>
  string deviceURL = string_format("http://%s", mDeviceHostname.c_str());
  mWledComm.setDeviceURL(deviceURL);

  // Query device info to verify connection and create device object
  mWledComm.getInfo(boost::bind(&WledVdc::handleInfoResponse, this, _1, _2, aCompletedCB));
}


void WledVdc::handleInfoResponse(JsonObjectPtr aInfo, ErrorPtr aError, StatusCB aCompletedCB)
{
  if (Error::notOK(aError)) {
    LOG(LOG_ERR, "Failed to get WLED device info: %s", aError->text());
    if (aCompletedCB) aCompletedCB(aError);
    return;
  }

  if (!aInfo) {
    if (aCompletedCB) aCompletedCB(Error::err<WledCommError>(WledCommError::InvalidResponse));
    return;
  }

  LOG(LOG_INFO, "WLED device info retrieved successfully");

  // Create or update device
  createOrUpdateDevice(aInfo, aCompletedCB);
}


void WledVdc::createOrUpdateDevice(JsonObjectPtr aInfo, StatusCB aCompletedCB)
{
  if (!aInfo) {
    if (aCompletedCB) aCompletedCB(ErrorPtr());
    return;
  }

  // Check if device already exists
  string deviceName = "WLED Device";
  string uniqueId = "";
  
  JsonObjectPtr nameObj = aInfo->get("name");
  if (nameObj) {
    deviceName = nameObj->stringValue();
  }
  
  JsonObjectPtr macObj = aInfo->get("mac");
  if (macObj) {
    uniqueId = macObj->stringValue();
  }

  WledDevicePtr device;
  for (auto &entry : mWledDevices) {
    if (entry.first == uniqueId) {
      device = entry.second;
      break;
    }
  }

  if (!device) {
    // Create new device
    device = new WledDevice(this, aInfo);
    mWledDevices[uniqueId] = device;
    simpleIdentifyAndAddDevice(device);
    LOG(LOG_INFO, "WLED device created: %s", deviceName.c_str());
  } else {
    // Update existing device
    device->updateInfo(aInfo);
    LOG(LOG_INFO, "WLED device updated: %s", deviceName.c_str());
  }

  if (aCompletedCB) aCompletedCB(ErrorPtr());
}


string WledVdc::hardwareGUID()
{
  string guid = string_format("wled_%d", getInstanceNumber());
  return guid;
}


bool WledVdc::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("vdc_wled", aIcon, aWithData, aResolutionPrefix)) {
    return true;
  }
  return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


string WledVdc::getExtraInfo()
{
  string info = string_format("wled: %s", mDeviceHostname.c_str());
  return info;
}


void WledVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  LOG(LOG_INFO, "WLED VDC scanning for devices");

  if (!(aRescanFlags & rescanmode_incremental)) {
    // full collect — remove existing devices
    removeDevices(aRescanFlags & rescanmode_clearsettings);
    mWledDevices.clear();
  }

  // Load stored device hostname from DB
  SQLiteTGQuery qry(mDb);
  if (Error::isOK(qry.prefixedPrepare("SELECT wledDeviceURL, wledDeviceHostname FROM $PREFIX_globs"))) {
    sqlite3pp::query::iterator i = qry.begin();
    if (i != qry.end()) {
      mDeviceURL = nonNullCStr(i->get<const char*>(0));
      mDeviceHostname = nonNullCStr(i->get<const char*>(1));
    }
  }
  LOG(LOG_INFO, "WLED stored hostname='%s'", mDeviceHostname.c_str());

  // If auto-discovery is enabled and no specific device configured, discover devices
  if (mAutoDiscovery && mDeviceHostname.empty()) {
    performDiscovery(aCompletedCB);
  } else if (!mDeviceHostname.empty()) {
    // Ensure the comm layer has the device URL — it may not be set on a fresh start
    // where the hostname was loaded from DB but initializeConnection() was never called.
    if (mWledComm.getDeviceURL().empty()) {
      mWledComm.setDeviceURL(string_format("http://%s", mDeviceHostname.c_str()));
    }
    performScan(aCompletedCB);
  } else {
    // Neither discovery enabled nor device configured
    if (aCompletedCB) aCompletedCB(ErrorPtr());
  }
}


void WledVdc::performScan(StatusCB aCompletedCB)
{
  // Try to get current state from device
  mWledComm.getInfo(boost::bind(&WledVdc::handleInfoResponse, this, _1, _2, aCompletedCB));
}


void WledVdc::removeDevice(DevicePtr aDevice, bool aForget)
{
  LOG(LOG_INFO, "WLED VDC removing device");

  // Find and remove device from map
  for (auto it = mWledDevices.begin(); it != mWledDevices.end(); ++it) {
    if (it->second == aDevice) {
      mWledDevices.erase(it);
      break;
    }
  }

  inherited::removeDevice(aDevice, aForget);
}


ErrorPtr WledVdc::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr err;

  if (uequals(aMethod, "setDeviceHostname")) {
    // Set the hostname/IP of the WLED device
    if (!aParams) {
      return ErrorPtr();
    }

    ApiValuePtr hostname = aParams->get("hostname");
    if (!hostname) {
      return ErrorPtr();
    }

    mDeviceHostname = hostname->stringValue();
    LOG(LOG_INFO, "WLED device hostname set to: %s", mDeviceHostname.c_str());

    // Persist to database
    mDb.prefixedExecute("UPDATE $PREFIX_globs SET wledDeviceHostname='%q'",
      mDeviceHostname.c_str());

    // Re-initialize connection to the new device
    initializeConnection(NULL);

    return ErrorPtr();
  }
  else if (uequals(aMethod, "getDeviceInfo")) {
    // Return current device info
    ErrorPtr respErr = ErrorPtr();
    // Could return device info here, but for now just acknowledge the request
    return respErr;
  }

  return inherited::handleMethod(aRequest, aMethod, aParams);
}


void WledVdc::performDiscovery(StatusCB aCompletedCB)
{
  LOG(LOG_INFO, "Starting WLED device discovery");

  #if WLED_DNSSD_DISCOVERY
  LOG(LOG_INFO, "Starting WLED device discovery via DNS-SD");
  // Browse for _wled._tcp services
  DnsSdManager::sharedDnsSdManager().browse("_wled._tcp", boost::bind(&WledVdc::handleDiscoveryHandler, this, _1, _2, aCompletedCB));
  #else
  LOG(LOG_WARNING, "WLED DNS-SD discovery is disabled");
  if (aCompletedCB) {
    ErrorPtr error = Error::err<WledCommError>(WledCommError::DeviceNotFound);
    aCompletedCB(error);
  }
  #endif
}


bool WledVdc::handleDiscoveryHandler(ErrorPtr aError, DnsSdServiceInfoPtr aServiceInfo, StatusCB aCompletedCB)
{
  if (Error::notOK(aError)) {
    LOG(LOG_ERR, "WLED discovery error: %s", aError->text());
    if (aCompletedCB) {
      aCompletedCB(aError);
    }
    return false; // stop browsing on error
  }

  if (aServiceInfo) {
    // Base URL: http://hostname — queueApiCall() will append /json/<path>
    string deviceURL = string_format("http://%s", aServiceInfo->hostaddress.c_str());
    LOG(LOG_INFO, "Discovered WLED device at %s", deviceURL.c_str());

    // Persist the discovered hostname so we reconnect after restart
    mDeviceHostname = aServiceInfo->hostaddress;
    mDb.prefixedExecute("UPDATE $PREFIX_globs SET wledDeviceHostname='%q'",
      mDeviceHostname.c_str());

    mWledComm.setDeviceURL(deviceURL);
    mWledComm.getInfo(boost::bind(&WledVdc::collectedLightsHandler, this, _1, _2));
  }

  return false; // stop browsing for more devices, we support only one device per VDC for now
}


void WledVdc::collectedLightsHandler(JsonObjectPtr aResult, ErrorPtr aError)
{
  OLOG(LOG_INFO, "wled light = \n%s", aResult ? aResult->c_strValue() : "<none>");

  if (aResult) {
    string uniqueID;
    JsonObjectPtr o = aResult->get("deviceId");
    if (o) uniqueID = o->stringValue();

    // create device now
    WledDevicePtr newDev = WledDevicePtr(new WledDevice(this, aResult));
    if (simpleIdentifyAndAddDevice(newDev)) {
      // actually added, no duplicate, set the name
      // (otherwise, this is an incremental collect and we knew this light already)
      JsonObjectPtr n = aResult->get("name");
      if (n) newDev->initializeName(n->stringValue());
    }
  }
}

#endif // ENABLE_WLED
