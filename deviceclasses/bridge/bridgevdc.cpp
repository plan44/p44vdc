//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2022 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "bridgevdc.hpp"
#include "bridgedevice.hpp"

#if ENABLE_JSONBRIDGEAPI

using namespace p44;


// MARK: - DB and initialisation


// Version history
//  1 : First version
#define BRIDGEDEVICES_SCHEMA_MIN_VERSION 1 // minimally supported version, anything older will be deleted
#define BRIDGEDEVICES_SCHEMA_VERSION 1 // current version

string BridgeDevicePersistence::dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion)
{
  string sql;
  if (aFromVersion==0) {
    // create DB from scratch
    // - use standard globs table for schema version
    sql = inherited::dbSchemaUpgradeSQL(aFromVersion, aToVersion);
    // - create my tables
    sql.append(
      "CREATE TABLE bridgedevices ("
      " bridgeDeviceId, config TEXT,"
      " PRIMARY KEY (bridgeDeviceId)"
      ");"
    );
    // reached final version in one step
    aToVersion = BRIDGEDEVICES_SCHEMA_VERSION;
  }
  return sql;
}



BridgeVdc::BridgeVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  Vdc(aInstanceNumber, aVdcHostP, aTag)
{
}


void BridgeVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  // load persistent params for dSUID
  load();
  // load private data
  string databaseName = getPersistentDataDir();
  string_format_append(databaseName, "%s_%d.sqlite3", vdcClassIdentifier(), getInstanceNumber());
  ErrorPtr error = mDb.connectAndInitialize(databaseName.c_str(), BRIDGEDEVICES_SCHEMA_VERSION, BRIDGEDEVICES_SCHEMA_MIN_VERSION, aFactoryReset);
  if (!getVdcFlag(vdcflag_flagsinitialized)) setVdcFlag(vdcflag_hidewhenempty, true); // hide by default
  aCompletedCB(error); // return status of DB init
}



// vDC name
const char *BridgeVdc::vdcClassIdentifier() const
{
  return "Bridge_Device_Container";
}


bool BridgeVdc::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("vdc_brdg", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}




/// collect devices from this vDC
/// @param aCompletedCB will be called when device scan for this vDC has been completed
void BridgeVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  // incrementally collecting configured devices makes no sense. The devices are "static"!
  if (!(aRescanFlags & rescanmode_incremental)) {
    // non-incremental, re-collect all devices
    removeDevices(aRescanFlags & rescanmode_clearsettings);
    // add from the DB
    sqlite3pp::query qry(mDb);
    if (qry.prepare("SELECT bridgeDeviceId, config, rowid FROM bridgedevices")==SQLITE_OK) {
      for (sqlite3pp::query::iterator i = qry.begin(); i != qry.end(); ++i) {
        BridgeDevicePtr dev = BridgeDevicePtr(new BridgeDevice(this, i->get<string>(0), i->get<string>(1)));
        if (dev) {
          dev->mBridgeDeviceRowID = i->get<int>(2);
          simpleIdentifyAndAddDevice(dev);
        }
      }
    }
  }
  // assume ok
  aCompletedCB(ErrorPtr());
}


ErrorPtr BridgeVdc::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="x-p44-addDevice") {
    // add a new bridge device
    string bridgeConfig;
    respErr = checkStringParam(aParams, "bridgeType", bridgeConfig);
    if (Error::isOK(respErr)) {
      // optional name
      string name;
      checkStringParam(aParams, "name", name);
      // use current time as ID for new bridgeDevices
      string bridgeDeviceId = string_format("bridgedevice_%lld", MainLoop::now());
      // try to create device
      BridgeDevicePtr dev = BridgeDevicePtr(new BridgeDevice(this, bridgeDeviceId, bridgeConfig));
      if (!dev) {
        respErr = WebError::webErr(500, "invalid configuration for bridge device -> none created");
      }
      else {
        // set name
        if (name.size()>0) dev->setName(name);
        // insert into database
        if (mDb.executef(
          "INSERT OR REPLACE INTO bridgedevices (bridgeDeviceId, config) VALUES ('%q','%q')",
          bridgeDeviceId.c_str(), bridgeConfig.c_str()
        )!=SQLITE_OK) {
          respErr = mDb.error("saving bridge device");
        }
        else {
          dev->mBridgeDeviceRowID = mDb.last_insert_rowid();
          simpleIdentifyAndAddDevice(dev);
          // confirm
          ApiValuePtr r = aRequest->newApiValue();
          r->setType(apivalue_object);
          r->add("dSUID", r->newBinary(dev->mDSUID.getBinary()));
          r->add("rowid", r->newUint64(dev->mBridgeDeviceRowID));
          r->add("name", r->newString(dev->getName()));
          aRequest->sendResult(r);
          respErr.reset(); // make sure we don't send an extra ErrorOK
        }
      }
    }
  }
  else {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }
  return respErr;
}


#endif // ENABLE_JSONBRIDGEAPI

