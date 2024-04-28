//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2024 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#define FOCUSLOGLEVEL 7

#include "dmxvdc.hpp"

#if ENABLE_OLA || ENABLE_DMX

#include "dmxdevice.hpp"

#if ENABLE_OLA
  #include <ola/DmxBuffer.h>
#endif

using namespace p44;



// MARK: - DB and initialisation


// Version history
//  1 : First version
#define OLADEVICES_SCHEMA_MIN_VERSION 1 // minimally supported version, anything older will be deleted
#define OLADEVICES_SCHEMA_VERSION 1 // current version

string DmxDevicePersistence::dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion)
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


#define OLA_DEFAULT_UNIVERSE 42

DmxVdc::DmxVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  Vdc(aInstanceNumber, aVdcHostP, aTag),
  #if ENABLE_OLA
  mDMXUniverse(OLA_DEFAULT_UNIVERSE),
  #endif
  mUseOLA(false)
{
}

#define DMX_SERIAL_PARAMS "250000,8,N,2"

void DmxVdc::setDmxOutput(const string aDmxOutputSpec, uint16_t aDefaultPort)
{
  if (uequals(aDmxOutputSpec,"ola",3)) {
    #if ENABLE_OLA
    mUseOLA = true;
    int u;
    if (sscanf(aDmxOutputSpec.c_str()+3, ":%d", &u)==1) {
      mDMXUniverse = u;
    }
    #else
    OLOG(LOG_ERR, "OLA output not supported")
    #endif
  }
  else {
    #if ENABLE_DMX
    mDmxSender = SerialCommPtr(new SerialComm);
    mDmxSender->setConnectionSpecification(aDmxOutputSpec.c_str(), aDefaultPort, DMX_SERIAL_PARAMS);
    #else
    OLOG(LOG_ERR, "Direct DMX output not supported")
    #endif
  }
}


void DmxVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  ErrorPtr err;
  // load persistent params for dSUID
  load();
  // load private data
  string databaseName = getPersistentDataDir();
  string_format_append(databaseName, "%s_%d.sqlite3", vdcClassIdentifier(), getInstanceNumber());
  err = mDb.connectAndInitialize(databaseName.c_str(), OLADEVICES_SCHEMA_VERSION, OLADEVICES_SCHEMA_MIN_VERSION, aFactoryReset);
  // launch sender thread
  pthread_mutex_init(&mDmxBufferAccess, NULL);
  #if ENABLE_OLA
  if (mUseOLA) {
    mDMXSenderThread = MainLoop::currentMainLoop().executeInThread(boost::bind(&DmxVdc::olaThreadRoutine, this, _1), NoOP);
  }
  else
  #endif
  #if ENABLE_DMX
  {
    mDMXSenderThread = MainLoop::currentMainLoop().executeInThread(boost::bind(&DmxVdc::dmxThreadRoutine, this, _1), NoOP);
  }
  #endif
  // done
  if (!getVdcFlag(vdcflag_flagsinitialized)) setVdcFlag(vdcflag_hidewhenempty, true); // hide by default
  aCompletedCB(ErrorPtr());
}


#define DMX512_FRAME_INTERVAL (50*MilliSecond) // actual frame is ~23mS, 50mS is fine = 20Hz, 44Hz is max

#if ENABLE_OLA

#define OLA_RETRY_INTERVAL (15*Second)
#define OLA_SETUP_RETRY_INTERVAL (30*Second)

void DmxVdc::olaThreadRoutine(ChildThreadWrapper &aThread)
{
  // turn on OLA logging when loglevel is debugging, otherwise off
  ola::InitLogging(LOGENABLED(LOG_DEBUG) ? ola::OLA_LOG_WARN : ola::OLA_LOG_NONE, ola::OLA_LOG_STDERR);
  mOlaDmxBufferP = new ola::DmxBuffer;
  ola::client::StreamingClient::Options options;
  options.auto_start = false; // do not start olad from client
  mOlaClientP = new ola::client::StreamingClient(options);
  if (mOlaClientP && mOlaDmxBufferP) {
    mOlaDmxBufferP->Blackout();
    while (!aThread.shouldTerminate()) {
      if (!mOlaClientP->Setup()) {
        // cannot start yet, wait a little
        MainLoop::sleep(OLA_SETUP_RETRY_INTERVAL);
      }
      else {
        while (!aThread.shouldTerminate()) {
          pthread_mutex_lock(&mDmxBufferAccess);
          bool ok = mOlaClientP->SendDMX(mDMXUniverse, *mOlaDmxBufferP, ola::client::StreamingClient::SendArgs());
          pthread_mutex_unlock(&mDmxBufferAccess);
          if (ok) {
            // successful send
            MainLoop::sleep(DMX512_FRAME_INTERVAL); // sleep a little between frames.
          }
          else {
            // unsuccessful send, do not try too often
            MainLoop::sleep(OLA_RETRY_INTERVAL); // sleep longer between failed attempts
          }
        }
      }
    }
  }
}

#endif // ENABLE_OLA

#if ENABLE_DMX

#define SERIAL_CONNECT_RETRY_INTERVAL 30*Second

#define DMX512_BREAK_LEN (2*MilliSecond) // 100µS would be enough, but be above Linux minumum which is 1mS
#define DMX512_MIN_MARK_AFTER_BREAK (12*MicroSecond) // according to wikipedia DMX512

void DmxVdc::dmxThreadRoutine(ChildThreadWrapper &aThread)
{
  const size_t dmxframebytes = 1+512; // start code (slot0) and 512 channels (slot 1..512)
  mSerialDmxBufferP = new uint8_t[dmxframebytes];
  if (mSerialDmxBufferP) {
    memset(mSerialDmxBufferP, 0, dmxframebytes); // startcode==0 + blackout
    while (!aThread.shouldTerminate()) {
      ErrorPtr err = mDmxSender->establishConnection();
      if (Error::notOK(err)) {
        // failed, retry later
        LOG(LOG_ERR, "Cannot open DMX serial output: %s", Error::text(err));
        MainLoop::sleep(SERIAL_CONNECT_RETRY_INTERVAL);
      }
      else {
        while (!aThread.shouldTerminate()) {
          FOCUSLOG("- will send break");
          mDmxSender->sendBreak(DMX512_BREAK_LEN);
          FOCUSLOG("- did send break");
          pthread_mutex_lock(&mDmxBufferAccess);
          FOCUSLOG("- will transmit");
          mDmxSender->transmitBytes(dmxframebytes, mSerialDmxBufferP, err);
          FOCUSLOG("- did transmit");
          pthread_mutex_unlock(&mDmxBufferAccess);
          if (Error::isOK(err)) {
            // successful send
            MainLoop::sleep(DMX512_FRAME_INTERVAL-DMX512_BREAK_LEN-DMX512_MIN_MARK_AFTER_BREAK); // wait one interval before sending next
          }
          else {
            // sending error
            // - close connection
            mDmxSender->closeConnection();
            LOG(LOG_ERR, "Error sending DMX serial data: %s", Error::text(err));
            // - re-try later
            MainLoop::sleep(SERIAL_CONNECT_RETRY_INTERVAL);
          }
        }
      }
    }
    mDmxSender->closeConnection();
    pthread_mutex_lock(&mDmxBufferAccess);
    delete mSerialDmxBufferP;
    mSerialDmxBufferP = NULL;
    pthread_mutex_unlock(&mDmxBufferAccess);
  }
}

#endif // ENABLE_DMX


void DmxVdc::setDMXChannel(DmxChannel aChannel, DmxValue aChannelValue)
{
  if (aChannel>=1 && aChannel<=512) {
    #if ENABLE_OLA
    if (mUseOLA && mOlaDmxBufferP) {
      pthread_mutex_lock(&mDmxBufferAccess);
      mOlaDmxBufferP->SetChannel(aChannel-1, aChannelValue);
      pthread_mutex_unlock(&mDmxBufferAccess);
    }
    else
    #endif
    #if ENABLE_DMX
    {
      pthread_mutex_lock(&mDmxBufferAccess);
      if (mSerialDmxBufferP) {
        mSerialDmxBufferP[aChannel] = aChannelValue;
      }
      pthread_mutex_unlock(&mDmxBufferAccess);
    }
    #endif
  }
}


bool DmxVdc::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("vdc_dmx", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


const char *DmxVdc::vdcClassIdentifier() const
{
  // for historical reasons, keep that identifier as device dSUIDs are based on this
  return "OLA_Device_Container";
}


DmxDevicePtr DmxVdc::addDmxDevice(string aDeviceType, string aDeviceConfig)
{
  DevicePtr newDev;
  // TODO: for now, all devices are DmxDevice
  string cfg = aDeviceType;
  cfg += ":";
  cfg += aDeviceConfig;
  newDev = DevicePtr(new DmxDevice(this, cfg));
  // add to container if device was created
  if (newDev) {
    // add to container
    simpleIdentifyAndAddDevice(newDev);
    return boost::dynamic_pointer_cast<DmxDevice>(newDev);
  }
  // none added
  return DmxDevicePtr();
}


/// collect devices from this vDC
/// @param aCompletedCB will be called when device scan for this vDC has been completed
void DmxVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  // incrementally collecting static devices makes no sense. The devices are "static"!
  if (!(aRescanFlags & rescanmode_incremental)) {
    // non-incremental, re-collect all devices
    removeDevices(aRescanFlags & rescanmode_clearsettings);
    // then add those from the DB
    sqlite3pp::query qry(mDb);
    if (qry.prepare("SELECT devicetype, deviceconfig, rowid FROM devConfigs")==SQLITE_OK) {
      for (sqlite3pp::query::iterator i = qry.begin(); i != qry.end(); ++i) {
        DmxDevicePtr dev =addDmxDevice(i->get<string>(0), i->get<string>(1));
        dev->mDmxDeviceRowID = i->get<int>(2);
      }
    }
  }
  // assume ok
  aCompletedCB(ErrorPtr());
}


ErrorPtr DmxVdc::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="x-p44-addDevice") {
    // add a new DMX device
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
        DmxDevicePtr dev = addDmxDevice(deviceType, deviceConfig);
        if (!dev) {
          respErr = WebError::webErr(500, "invalid configuration for DMX device -> none created");
        }
        else {
          // set name
          if (name.size()>0) dev->setName(name);
          // insert into database
          if(mDb.executef(
            "INSERT OR REPLACE INTO devConfigs (devicetype, deviceconfig) VALUES ('%q','%q')",
            deviceType.c_str(), deviceConfig.c_str()
          )!=SQLITE_OK) {
            respErr = mDb.error("saving DMX params");
          }
          else {
            dev->mDmxDeviceRowID = mDb.last_insert_rowid();
            // confirm
            ApiValuePtr r = aRequest->newApiValue();
            r->setType(apivalue_object);
            r->add("dSUID", r->newBinary(dev->mDSUID.getBinary()));
            r->add("rowid", r->newUint64(dev->mDmxDeviceRowID));
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

#endif // ENABLE_OLA || ENABLE_DMX


