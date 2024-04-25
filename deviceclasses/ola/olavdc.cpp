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

#include "olavdc.hpp"

#if ENABLE_OLA

#include "oladevice.hpp"

#include <ola/DmxBuffer.h>

using namespace p44;



// MARK: - DB and initialisation


// Version history
//  1 : First version
#define OLADEVICES_SCHEMA_MIN_VERSION 1 // minimally supported version, anything older will be deleted
#define OLADEVICES_SCHEMA_VERSION 1 // current version

string OlaDevicePersistence::dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion)
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
    aToVersion = OLADEVICES_SCHEMA_VERSION;
  }
  return sql;
}



OlaVdc::OlaVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  Vdc(aInstanceNumber, aVdcHostP, aTag)
{
}


#define DMX512_INTERFRAME_PAUSE (50*MilliSecond)
#define DMX512_RETRY_INTERVAL (15*Second)
#define OLA_SETUP_RETRY_INTERVAL (30*Second)
#define DMX512_UNIVERSE 42

void OlaVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  ErrorPtr err;
  // load persistent params for dSUID
  load();
  // load private data
  string databaseName = getPersistentDataDir();
  string_format_append(databaseName, "%s_%d.sqlite3", vdcClassIdentifier(), getInstanceNumber());
  err = db.connectAndInitialize(databaseName.c_str(), OLADEVICES_SCHEMA_VERSION, OLADEVICES_SCHEMA_MIN_VERSION, aFactoryReset);
  // launch OLA thread
  pthread_mutex_init(&olaBufferAccess, NULL);
  olaThread = MainLoop::currentMainLoop().executeInThread(boost::bind(&OlaVdc::olaThreadRoutine, this, _1), NoOP);
  // done
  if (!getVdcFlag(vdcflag_flagsinitialized)) setVdcFlag(vdcflag_hidewhenempty, true); // hide by default
  aCompletedCB(ErrorPtr());
}


void OlaVdc::olaThreadRoutine(ChildThreadWrapper &aThread)
{
  // turn on OLA logging when loglevel is debugging, otherwise off
  ola::InitLogging(LOGENABLED(LOG_DEBUG) ? ola::OLA_LOG_WARN : ola::OLA_LOG_NONE, ola::OLA_LOG_STDERR);
  dmxBufferP = new ola::DmxBuffer;
  ola::client::StreamingClient::Options options;
  options.auto_start = false; // do not start olad from client
  olaClientP = new ola::client::StreamingClient(options);
  if (olaClientP && dmxBufferP) {
    dmxBufferP->Blackout();
    while (!aThread.shouldTerminate()) {
      if (!olaClientP->Setup()) {
        // cannot start yet, wait a little
        usleep(OLA_SETUP_RETRY_INTERVAL);
      }
      else {
        while (!aThread.shouldTerminate()) {
          pthread_mutex_lock(&olaBufferAccess);
          bool ok = olaClientP->SendDMX(DMX512_UNIVERSE, *dmxBufferP, ola::client::StreamingClient::SendArgs());
          pthread_mutex_unlock(&olaBufferAccess);
          if (ok) {
            // successful send
            usleep(DMX512_INTERFRAME_PAUSE); // sleep a little between frames.
          }
          else {
            // unsuccessful send, do not try too often
            usleep(DMX512_RETRY_INTERVAL); // sleep longer between failed attempts
          }
        }
      }
    }
  }
}


void OlaVdc::setDMXChannel(DmxChannel aChannel, DmxValue aChannelValue)
{
  if (dmxBufferP && aChannel>=1 && aChannel<=512) {
    pthread_mutex_lock(&olaBufferAccess);
    dmxBufferP->SetChannel(aChannel-1, aChannelValue);
    pthread_mutex_unlock(&olaBufferAccess);
  }
}


bool OlaVdc::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("vdc_ola", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


// vDC name
const char *OlaVdc::vdcClassIdentifier() const
{
  return "OLA_Device_Container";
}


OlaDevicePtr OlaVdc::addOlaDevice(string aDeviceType, string aDeviceConfig)
{
  DevicePtr newDev;
  // TODO: for now, all devices are OlaDevice
  string cfg = aDeviceType;
  cfg += ":";
  cfg += aDeviceConfig;
  newDev = DevicePtr(new OlaDevice(this, cfg));
  // add to container if device was created
  if (newDev) {
    // add to container
    simpleIdentifyAndAddDevice(newDev);
    return boost::dynamic_pointer_cast<OlaDevice>(newDev);
  }
  // none added
  return OlaDevicePtr();
}


/// collect devices from this vDC
/// @param aCompletedCB will be called when device scan for this vDC has been completed
void OlaVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  // incrementally collecting static devices makes no sense. The devices are "static"!
  if (!(aRescanFlags & rescanmode_incremental)) {
    // non-incremental, re-collect all devices
    removeDevices(aRescanFlags & rescanmode_clearsettings);
    // then add those from the DB
    sqlite3pp::query qry(db);
    if (qry.prepare("SELECT devicetype, deviceconfig, rowid FROM devConfigs")==SQLITE_OK) {
      for (sqlite3pp::query::iterator i = qry.begin(); i != qry.end(); ++i) {
        OlaDevicePtr dev =addOlaDevice(i->get<string>(0), i->get<string>(1));
        dev->olaDeviceRowID = i->get<int>(2);
      }
    }
  }
  // assume ok
  aCompletedCB(ErrorPtr());
}


ErrorPtr OlaVdc::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="x-p44-addDevice") {
    // add a new OLA device
    string deviceType;
    string deviceConfig;
    respErr = checkStringParam(aParams, "deviceType", deviceType);
    if (Error::isOK(respErr)) {
      respErr = checkStringParam(aParams, "deviceConfig", deviceConfig);
      if (Error::isOK(respErr)) {
        // optional name
        string name;
        checkStringParam(aParams, "name", name);
        // try to create device
        OlaDevicePtr dev = addOlaDevice(deviceType, deviceConfig);
        if (!dev) {
          respErr = WebError::webErr(500, "invalid configuration for OLA device -> none created");
        }
        else {
          // set name
          if (name.size()>0) dev->setName(name);
          // insert into database
          if(db.executef(
            "INSERT OR REPLACE INTO devConfigs (devicetype, deviceconfig) VALUES ('%q','%q')",
            deviceType.c_str(), deviceConfig.c_str()
          )!=SQLITE_OK) {
            respErr = db.error("saving OLA params");
          }
          else {
            dev->olaDeviceRowID = db.last_insert_rowid();
            // confirm
            ApiValuePtr r = aRequest->newApiValue();
            r->setType(apivalue_object);
            r->add("dSUID", r->newBinary(dev->mDSUID.getBinary()));
            r->add("rowid", r->newUint64(dev->olaDeviceRowID));
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

#endif // ENABLE_OLA


