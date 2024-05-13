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

#include "staticvdc.hpp"

#if ENABLE_STATIC

#include "digitaliodevice.hpp"
#include "analogiodevice.hpp"
#include "consoledevice.hpp"
#include "evaluatordevice.hpp"
#include "mystromdevice.hpp"

using namespace p44;



// MARK: - StaticDevice


StaticDevice::StaticDevice(Vdc *aVdcP) :
  Device(aVdcP),
  mStaticDeviceRowID(0)
{
}


bool StaticDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Nothing to do to identify for now
  return true; // simple identification, callback will not be called
}


bool StaticDevice::isSoftwareDisconnectable()
{
  return mStaticDeviceRowID>0; // disconnectable by software if it was created from DB entry (and not on the command line)
}

StaticVdc &StaticDevice::getStaticVdc()
{
  return *(static_cast<StaticVdc *>(mVdcP));
}


void StaticDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  OLOG(LOG_DEBUG, "disconnecting static device with rowid=%lld", mStaticDeviceRowID);
  // clear learn-in data from DB
  if (mStaticDeviceRowID) {
    if(getStaticVdc().mDb.executef("DELETE FROM devConfigs WHERE rowid=%lld", mStaticDeviceRowID)!=SQLITE_OK) {
      OLOG(LOG_ERR, "Edeleting static device: %s", getStaticVdc().mDb.error()->description().c_str());
    }
  }
  // disconnection is immediate, so we can call inherited right now
  inherited::disconnect(aForgetParams, aDisconnectResultHandler);
}


// MARK: - DB and initialisation


// Version history
//  1 : First version
#define STATICDEVICES_SCHEMA_MIN_VERSION 1 // minimally supported version, anything older will be deleted
#define STATICDEVICES_SCHEMA_VERSION 1 // current version

string StaticDevicePersistence::dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion)
{
  string sql;
  if (aFromVersion==0) {
    // create DB from scratch
    // - use standard globs table for schema version
    sql = inherited::dbSchemaUpgradeSQL(aFromVersion, aToVersion);
    // - create my tables
    sql.append(
      "CREATE TABLE devConfigs ("
      " devicetype TEXT,"
      " deviceconfig TEXT"
      ");"
    );
    // reached final version in one step
    aToVersion = STATICDEVICES_SCHEMA_VERSION;
  }
  return sql;
}



StaticVdc::StaticVdc(int aInstanceNumber, DeviceConfigMap aDeviceConfigs, VdcHost *aVdcHostP, int aTag) :
  Vdc(aInstanceNumber, aVdcHostP, aTag),
	mDeviceConfigs(aDeviceConfigs)
{
}


void StaticVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  // load persistent params for dSUID
  load();
  // load private data
  string databaseName = getPersistentDataDir();
  string_format_append(databaseName, "%s_%d.sqlite3", vdcClassIdentifier(), getInstanceNumber());
  ErrorPtr error = mDb.connectAndInitialize(databaseName.c_str(), STATICDEVICES_SCHEMA_VERSION, STATICDEVICES_SCHEMA_MIN_VERSION, aFactoryReset);
  if (!getVdcFlag(vdcflag_flagsinitialized)) setVdcFlag(vdcflag_hidewhenempty, true); // hide by default
  aCompletedCB(error); // return status of DB init
}



// vDC name
const char *StaticVdc::vdcClassIdentifier() const
{
  return "Static_Device_Container";
}


StaticDevicePtr StaticVdc::addStaticDevice(string aDeviceType, string aDeviceConfig)
{
  DevicePtr newDev;
  if (aDeviceType=="digitalio") {
    // Digital IO based device
    newDev = DevicePtr(new DigitalIODevice(this, aDeviceConfig));
  }
  else if (aDeviceType=="analogio") {
    // Analog IO based device
    newDev = DevicePtr(new AnalogIODevice(this, aDeviceConfig));
  }
  else if (aDeviceType=="console") {
    // console based simulated device
    newDev = DevicePtr(new ConsoleDevice(this, aDeviceConfig));
  }
  else if (aDeviceType=="mystrom") {
    // mystrom WiFi switch
    newDev = DevicePtr(new MyStromDevice(this, aDeviceConfig));
  }
  // add to container if device was created
  if (newDev) {
    // add to container
    simpleIdentifyAndAddDevice(newDev);
    return boost::dynamic_pointer_cast<StaticDevice>(newDev);
  }
  // none added
  return StaticDevicePtr();
}


/// collect devices from this vDC
/// @param aCompletedCB will be called when device scan for this vDC has been completed
void StaticVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  // incrementally collecting static devices makes no sense. The devices are "static"!
  if (!(aRescanFlags & rescanmode_incremental)) {
    // non-incremental, re-collect all devices
    removeDevices(aRescanFlags & rescanmode_clearsettings);
    // create devices from command line config
    for (DeviceConfigMap::iterator pos = mDeviceConfigs.begin(); pos!=mDeviceConfigs.end(); ++pos) {
      // create device of appropriate class
      StaticDevicePtr dev = addStaticDevice(pos->first, pos->second);
      if (dev) {
        dev->initializeName(pos->second); // for command line devices, use config as name
      }
    }
    // then add those from the DB
    sqlite3pp::query qry(mDb);
    if (qry.prepare("SELECT devicetype, deviceconfig, rowid FROM devConfigs")==SQLITE_OK) {
      for (sqlite3pp::query::iterator i = qry.begin(); i != qry.end(); ++i) {
        StaticDevicePtr dev = addStaticDevice(i->get<string>(0), i->get<string>(1));
        if (dev) {
          dev->mStaticDeviceRowID = i->get<int>(2);
        }
      }
    }
  }
  // assume ok
  aCompletedCB(ErrorPtr());
}


ErrorPtr StaticVdc::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="x-p44-addDevice") {
    // add a new static device
    string deviceType;
    string deviceConfig;
    respErr = checkStringParam(aParams, "deviceType", deviceType);
    if (Error::isOK(respErr)) {
      respErr = checkStringParam(aParams, "deviceConfig", deviceConfig);
      if (Error::isOK(respErr)) {
        // optional name
        string name; // default to config
        checkStringParam(aParams, "name", name);
        // try to create device
        StaticDevicePtr dev = addStaticDevice(deviceType, deviceConfig);
        if (!dev) {
          respErr = WebError::webErr(500, "invalid configuration for static device -> none created");
        }
        else {
          // set name
          if (name.size()>0) dev->setName(name);
          // insert into database
          if(mDb.executef(
            "INSERT OR REPLACE INTO devConfigs (devicetype, deviceconfig) VALUES ('%q','%q')",
            deviceType.c_str(), deviceConfig.c_str()
          )!=SQLITE_OK) {
            respErr = mDb.error("saving static device params");
          }
          else {
            dev->mStaticDeviceRowID = mDb.last_insert_rowid();
            // confirm
            ApiValuePtr r = aRequest->newApiValue();
            r->setType(apivalue_object);
            r->add("dSUID", r->newBinary(dev->mDSUID.getBinary()));
            r->add("rowid", r->newUint64(dev->mStaticDeviceRowID));
            r->add("name", r->newString(dev->getName()));
            aRequest->sendResult(r);
            respErr.reset(); // make sure we don't send an extra ErrorOK
          }
        }
      }
    }
  }
  else {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }
  return respErr;
}



#endif // ENABLE_STATIC
