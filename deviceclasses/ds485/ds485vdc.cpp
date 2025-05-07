//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "ds485vdc.hpp"
#include "ds485device.hpp"

#if ENABLE_DS485DEVICES

#include "colorlightbehaviour.hpp"
#include "shadowbehaviour.hpp"
#include "ventilationbehaviour.hpp"
#include "buttonbehaviour.hpp"
#include "binaryinputbehaviour.hpp"
#include "sensorbehaviour.hpp"

using namespace p44;

// MARK: - Ds485Vdc

Ds485Vdc::Ds485Vdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  Vdc(aInstanceNumber, aVdcHostP, aTag),
  mDs485Started(false),
  mDs485HostKnown(false)
{
  mDs485Comm.isMemberVariable();
}


Ds485Vdc::~Ds485Vdc()
{
  mDs485Comm.stop();
}


P44LoggingObj* Ds485Vdc::getTopicLogObject(const string aTopic)
{
  if (aTopic=="ds485comm") return &mDs485Comm;
  // unknown at this level
  return inherited::getTopicLogObject(aTopic);
}


void Ds485Vdc::handleGlobalEvent(VdchostEvent aEvent)
{
  if (aEvent==vdchost_vdcapi_connected) {
    if (!mDs485HostKnown) {
      mDs485Comm.mDs485HostIP = getVdcHost().vdsmHostIp();
      OLOG(LOG_INFO, "got dS485 host: %s", mDs485Comm.mDs485HostIP.c_str());
      mDs485HostKnown = true;
    }
    if (!mDs485Started) {
      // could not be started at initialize, do it now and then do collect
      mDs485Started = true;
      mDs485Comm.start(boost::bind(&Ds485Vdc::recollect, this, rescanmode_normal));
    }
    else {
      // just recollect
      recollect(rescanmode_incremental);
    }
  }
  inherited::handleGlobalEvent(aEvent);
}


#define RECOLLECT_RETRY_DELAY (30*Second)

void Ds485Vdc::recollect(RescanMode aRescanMode)
{
  mRecollectTicket.cancel();
  if (!isCollecting()) {
    // collect now
    collectDevices(NoOP, aRescanMode);
  }
  else {
    // retry later
    mRecollectTicket.executeOnce(boost::bind(&Ds485Vdc::recollect, this, aRescanMode), RECOLLECT_RETRY_DELAY);
  }
}



// MARK: - DB + initialisation

// Version history
//  1 : first version
#define DS485_SCHEMA_MIN_VERSION 1 // minimally supported version, anything older will be deleted
#define DS485_SCHEMA_VERSION 1 // current version

string Ds485Persistence::schemaUpgradeSQL(int aFromVersion, int &aToVersion)
{
  string sql;
  if (aFromVersion==0) {
    // create table group from scratch
    // - use standard globs table for schema version
    sql = inherited::schemaUpgradeSQL(aFromVersion, aToVersion);
    // - add fields to globs table
    sql.append(
      "ALTER TABLE $PREFIX_globs ADD tunnelPw TEXT;"
      "ALTER TABLE $PREFIX_globs ADD tunnelHost TEXT;"
    );
    // reached final version in one step
    aToVersion = DS485_SCHEMA_VERSION;
  }
  return sql;
}


ErrorPtr Ds485Vdc::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="setDs485Params") {
    // hue specific addition, only via genericRequest
    ApiValuePtr o = aParams->get("tunnelPw");
    if (o) {
      string pw = o->stringValue();
      mDs485Comm.setTunnelPw(pw);
      string host; // default to automatic
      o = aParams->get("tunnelHost");
      if (o) {
        // host specified, overrides existing one
        host = o->stringValue();
        mDs485Comm.mApiHost = host;
      }
      // save
      if(mDb.db().executef(
        mDb.prefixedSql("UPDATE $PREFIX_globs SET tunnelPw='%q', tunnelHost='%q'").c_str(),
        pw.c_str(), host.c_str()
      )!=SQLITE_OK) {
        respErr = mDb.db().error("saving dS485 params");
      }
      else {
        // done
        respErr = Error::ok();
      }
    }
  }
  else {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }
  return respErr;
}


void Ds485Vdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  // load persistent params for dSUID
  load();
  // load private data
  ErrorPtr err = initializePersistence(mDb,  DS485_SCHEMA_VERSION, DS485_SCHEMA_MIN_VERSION);
  if (Error::notOK(err)) aCompletedCB(err); // failed
  // get tunnel pw
  sqlite3pp::query qry(mDb.db());
  if (qry.prepare(mDb.prefixedSql("SELECT tunnelPw, tunnelHost FROM $PREFIX_globs").c_str())==SQLITE_OK) {
    sqlite3pp::query::iterator i = qry.begin();
    if (i!=qry.end()) {
      mDs485Comm.setTunnelPw(nonNullCStr(i->get<const char *>(0)));
      string host = nonNullCStr(i->get<const char *>(1));
      if (!host.empty()) {
        mDs485Comm.mDs485HostIP = host;
        mDs485HostKnown = true; // prevent automatic
      }
    }
  }
  // install handler
  mDs485Comm.setDs485MessageHandler(boost::bind(&Ds485Vdc::ds485MessageHandler, this, _1, _2, _3));
  // start if we have everything ready
  if (
    !mDs485Started &&
    (mDs485Comm.mTunnelCommandTemplate.empty() || mDs485HostKnown) // no tunnel needed or already startable
  ) {
    mDs485Started = true;
    mDs485Comm.start(aCompletedCB);
  }
  else {
    OLOG(LOG_WARNING, "cannot yet start dS485 device scan - waiting for API to get ready");
    if (aCompletedCB) aCompletedCB(ErrorPtr());
  }
}


const char *Ds485Vdc::vdcClassIdentifier() const
{
  // The class identifier is only for addressing by specifier
  return "dS485_Device_Container";
}


string Ds485Vdc::webuiURLString()
{
  if (mDs485Comm.mDs485HostIP.empty()) return inherited::webuiURLString();
  return string_format("http://%s", mDs485Comm.mDs485HostIP.c_str());
}


bool Ds485Vdc::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("vdc_ds485", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


int Ds485Vdc::getRescanModes() const
{
  return rescanmode_incremental+rescanmode_normal;
}


/// collect devices from this vDC
/// @param aCompletedCB will be called when device scan for this vDC has been completed
void Ds485Vdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  if (!mDs485Started) {
    if (aCompletedCB) aCompletedCB(ErrorPtr()); // too early
    return;
  }
  if (!(aRescanFlags & rescanmode_incremental)) {
    // full collect, remove all devices
    removeDevices(aRescanFlags & rescanmode_clearsettings);
  }
  mDs485Devices.clear();
  mDs485Comm.mDs485ClientThread->executeOnChildThreadAsync(boost::bind(&Ds485Vdc::scanDs485BusSync, this, _1), boost::bind(&Ds485Vdc::ds485BusScanned, this, _1, aCompletedCB));
}


string fullDevId(const DsUid& aDsuid, const uint16_t aDevId)
{
  string fullid(aDsuid.getBinary());
  fullid.append(1, (uint8_t)(aDevId>>8));
  fullid.append(1, (uint8_t)(aDevId&0xFF));
  return fullid;
}


Ds485DevicePtr Ds485Vdc::deviceFor(const DsUid& aDsmDsUid, uint16_t aDevId)
{
  Ds485DevicePtr dev;
  if (!aDsmDsUid.empty()) {
    Ds485DeviceMap::iterator pos = mDs485Devices.find(fullDevId(aDsmDsUid, aDevId));
    if (pos!=mDs485Devices.end()) return pos->second;
  }
  return dev;
}


void Ds485Vdc::ds485BusScanned(ErrorPtr aScanStatus, StatusCB aCompletedCB)
{
  if (Error::isOK(aScanStatus)) {
    // now add my devices
    for (Ds485DeviceMap::iterator pos = mDs485Devices.begin(); pos!=mDs485Devices.end(); ++pos) {
      Ds485DevicePtr dev = pos->second;
      if (simpleIdentifyAndAddDevice(dev)) {
        // TODO: maybe something
      }
    }
  }
  if (aCompletedCB) aCompletedCB(aScanStatus);
}



// MARK: - operation

void Ds485Vdc::ds485MessageHandler(const DsUid& aSource, const DsUid& aTarget, const string aPayload)
{
  OLOG(LOG_INFO,"dS485 Message: %s -> %s: [%zu] %s", aSource.text().c_str(), aTarget.text().c_str(), aPayload.size(), binaryToHexString(aPayload, ' ').c_str());
  size_t pli = 0;
  uint8_t command;
  if ((pli = Ds485Comm::payload_get8(aPayload, pli, command))==0) return;
  uint8_t modifier;
  if ((pli = Ds485Comm::payload_get8(aPayload, pli, modifier))==0) return;
  switch (command) {
    case EVENT_COMMUNICATION_LOG: {
      switch (modifier) {
        case EVENT_COMMUNICATION_LOG_UPSTREAM_SHORT: {
          pli++; // skip that 3rd byte dsm events seem to have
          uint16_t devId;
          if ((pli = Ds485Comm::payload_get16(aPayload, pli, devId))==0) return;
          pli++; // skip CircuitId
          pli++; // skip Resend // TODO: maybe evaluate this
          uint8_t isSensor;
          if ((pli = Ds485Comm::payload_get8(aPayload, pli, isSensor))==0) return;
          uint8_t keyNo;
          if ((pli = Ds485Comm::payload_get8(aPayload, pli, keyNo))==0) return;
          uint8_t click;
          if ((pli = Ds485Comm::payload_get8(aPayload, pli, click))==0) return;
          // TODO: quality, flags, crosstalk are not read for now
          Ds485DevicePtr dev = deviceFor(aSource, devId); // upstream -> source is relevant
          if (dev) dev->handleDeviceUpstreamMessage(isSensor, keyNo, (DsClickType)click);
          break;
        }
      }
      break;
    }
    case EVENT_DEVICE_SENSOR: {
      pli++; // skip that 3rd byte dsm events seem to have
      uint16_t devId;
      if ((pli = Ds485Comm::payload_get16(aPayload, pli, devId))==0) return;
      Ds485DevicePtr dev = deviceFor(aSource, devId); // upstream -> source is relevant
      if (dev) {
        switch (modifier) {
          case EVENT_DEVICE_SENSOR_VALUE: {
            uint8_t sensIdx;
            if ((pli = Ds485Comm::payload_get8(aPayload, pli, sensIdx))==0) return;
            uint16_t sens12bit;
            if ((pli = Ds485Comm::payload_get16(aPayload, pli, sens12bit))==0) return;
            dev->processSensorValue12Bit(sensIdx, sens12bit);
          }
          case 5 /* missing: EVENT_DEVICE_SENSOR_VALUE_EXTENDED */: {
            uint8_t sensIdx;
            if ((pli = Ds485Comm::payload_get8(aPayload, pli, sensIdx))==0) return;
            POLOG(dev, LOG_WARNING, "dS485 Sensor extended (double) value event not yet handled: sensor index=%d", sensIdx);
          }
          case EVENT_DEVICE_SENSOR_BINARYINPUTEVENT: {
            uint8_t bininpIdx;
            if ((pli = Ds485Comm::payload_get8(aPayload, pli, bininpIdx))==0) return;
            uint8_t bininpType;
            if ((pli = Ds485Comm::payload_get8(aPayload, pli, bininpType))==0) return;
            uint8_t bininpVal;
            if ((pli = Ds485Comm::payload_get8(aPayload, pli, bininpVal))==0) return;
            dev->processBinaryInputValue(bininpIdx, bininpVal);
          }
          case EVENT_DEVICE_SENSOR_EVENT: {
            uint8_t eventIdx;
            if ((pli = Ds485Comm::payload_get8(aPayload, pli, eventIdx))==0) return;
            POLOG(dev, LOG_WARNING, "dS485 Sensor event not yet handled: event index=%d", eventIdx);
          }
        }
      }
    }
    case ZONE_GROUP_ACTION_REQUEST: {
      uint16_t zoneId;
      if ((pli = Ds485Comm::payload_get16(aPayload, pli, zoneId))==0) return;
      uint8_t group;
      if ((pli = Ds485Comm::payload_get8(aPayload, pli, group))==0) return;
      uint16_t originDevId;
      if ((pli = Ds485Comm::payload_get16(aPayload, pli, originDevId))==0) return;
      // send to every device matching zone and group
      for (Ds485DeviceMap::iterator pos = mDs485Devices.begin(); pos!=mDs485Devices.end(); ++pos) {
        Ds485DevicePtr dev = pos->second;
        OutputBehaviourPtr o = dev->getOutput();
        if (o && o->isMember((DsGroup)group) && dev->getZoneID()==zoneId) {
          // device is in this group and zone
          dev->processActionRequest(ZG(modifier), aPayload, pli);
        }
      }
      break;
    }
    case DEVICE_ACTION_REQUEST: {
      uint16_t devId;
      if ((pli = Ds485Comm::payload_get16(aPayload, pli, devId))==0) return;
      Ds485DevicePtr dev = deviceFor(aTarget, devId); // downstream -> target is relevant
      if (dev) {
        dev->processActionRequest(DEV(modifier), aPayload, pli);
      }
      break;
    }
    case DEVICE_PROPERTIES: {
      uint16_t devId;
      if ((pli = Ds485Comm::payload_get16(aPayload, pli, devId))==0) return;
      Ds485DevicePtr dev = deviceFor(aTarget, devId); // downstream -> target is relevant
      if (dev) {
        dev->processPropertyRequest(DEV(modifier), aPayload, pli);
      }
      break;
    }
    case DEVICE_CONFIG: {
      // this is a device config (bank/offset) read or write request
      switch (modifier) {
        case DEVICE_CONFIG_SET: {
          // this is a device config write
          uint16_t devId;
          if ((pli = Ds485Comm::payload_get16(aPayload, pli, devId))==0) return;
          Ds485DevicePtr dev = deviceFor(aTarget, devId); // downstream -> target is relevant
          if (dev) {
            // this is a device config write to this device
            uint8_t bank;
            if ((pli = Ds485Comm::payload_get8(aPayload, pli, bank))==0) return;
            uint8_t offs;
            if ((pli = Ds485Comm::payload_get8(aPayload, pli, offs))==0) return;
            uint8_t byte;
            if ((pli = Ds485Comm::payload_get8(aPayload, pli, byte))==0) return;
            dev->traceConfigValue(bank, offs, byte);
          }
          break;
        }
      }
      break;
    }
    case EVENT_DEVICE_CONFIG: {
      // Note: does not have a modifier!
      // this is the response for requesting a bank/offset type DEVICE_CONFIG request
      // 302ED89F43F0000000000E400000E9D700 -> FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF, t=0xff: [08] 74 00 00 03 ED 40 00 FF
      // 302ED89F43F0000000000E400000E9D700 -> FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF, t=0xff: [08] 74 00 00 03 ED 40 01 00
      pli++; // skip that 3rd byte dsm events seem to have
      uint16_t devId;
      if ((pli = Ds485Comm::payload_get16(aPayload, pli, devId))==0) return;
      Ds485DevicePtr dev = deviceFor(aSource, devId); // upstream -> source is relevant
      if (dev) {
        // this is a device config readout from this device
        uint8_t bank;
        if ((pli = Ds485Comm::payload_get8(aPayload, pli, bank))==0) return;
        uint8_t offs;
        if ((pli = Ds485Comm::payload_get8(aPayload, pli, offs))==0) return;
        uint8_t byte;
        if ((pli = Ds485Comm::payload_get8(aPayload, pli, byte))==0) return;
        dev->traceConfigValue(bank, offs, byte);
      }
      break;
    }
  }
  return;
}


void Ds485Vdc::deliverToDevicesAudience(DsAddressablesList aAudience, VdcApiConnectionPtr aApiConnection, const string &aNotification, ApiValuePtr aParams)
{
  inherited::deliverToDevicesAudience(aAudience, aApiConnection, aNotification, aParams);
  // TODO: implement optimisations to call native scenes instead of device adjustment

//  for (DsAddressablesList::iterator apos = aAudience.begin(); apos!=aAudience.end(); ++apos) {
//    // TODO: implement
//  }
}


// MARK: - things that need to run on ds485 thread because they are blocking

ErrorPtr Ds485Vdc::scanDs485BusSync(ChildThreadWrapper &aThread)
{
  // startup, collect info
  // - the bus devices
  ErrorPtr err;
  const int maxbusdevices = 64;
  dsuid_t busdevices[maxbusdevices];
  int numDsms = ds485_client_query_devices(mDs485Comm.mDs485Client, busdevices, maxbusdevices);
  // iterate dSMs
  for (int di=0; di<numDsms; di++) {
    DsUid dsmDsuid(busdevices[di]);
    OLOG(LOG_NOTICE, "scanning dSM #%d: %s", di, dsmDsuid.text().c_str());
    // prevent asking myself
    if (dsmDsuid!=mDs485Comm.mMyDsuid) {
      string resp;
      size_t pli;
      // - get the dSM info
      err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, DSM_INFO);
      if (Error::notOK(err)) return err;
      pli = 3;
      uint32_t dsmHwVersion;
      if ((pli = Ds485Comm::payload_get32(resp, pli, dsmHwVersion))==0) continue; // something is wrong, skip
      uint32_t dsmArmVersion;
      if ((pli = Ds485Comm::payload_get32(resp, pli, dsmArmVersion))==0) continue; // something is wrong, skip
      uint32_t dsmDSPVersion;
      if ((pli = Ds485Comm::payload_get32(resp, pli, dsmDSPVersion))==0) continue; // something is wrong, skip
      uint16_t dsmAPIVersion;
      if ((pli = Ds485Comm::payload_get16(resp, pli, dsmAPIVersion))==0) continue; // something is wrong, skip
      pli += 12; // skip "dSID"
      string dsmName;
      if ((pli = Ds485Comm::payload_getString(resp, pli, 21, dsmName))==0) continue; // something is wrong, skip
      OLOG(LOG_INFO, "dSM #%d: '%s', hwV=0x%08x, armV=0x%08x, dspV=0x%08x, apiV=0x%04x", di, dsmName.c_str(), dsmHwVersion, dsmArmVersion, dsmDSPVersion, dsmAPIVersion);
      // - get the zone count
      err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, ZONE_COUNT);
      if (Error::notOK(err)) return err;
      pli = 3;
      uint8_t zoneCount;
      if ((pli = Ds485Comm::payload_get8(resp, pli, zoneCount))==0) continue; // something is wrong, skip
      OLOG(LOG_INFO, "dSM #%d: has %d zones", di, zoneCount);
      // - the zones
      for (int i=0; i<zoneCount; i++) {
        string req;
        Ds485Comm::payload_append8(req, i);
        err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, ZONE_INFO, ZONE_INFO_BY_INDEX, req);
        if (Error::notOK(err)) return err;
        size_t pli;
        pli = 3;
        uint16_t zoneId;
        if ((pli = Ds485Comm::payload_get16(resp, pli, zoneId))==0) continue; // something is wrong, skip
        uint8_t vzoneId;
        if ((pli = Ds485Comm::payload_get8(resp, pli, vzoneId))==0) continue; // something is wrong, skip
        uint8_t numGroups;
        if ((pli = Ds485Comm::payload_get8(resp, pli, numGroups))==0) continue; // something is wrong, skip
        string zonename;
        if ((pli = Ds485Comm::payload_getString(resp, pli, 21, zonename))==0) continue; // something is wrong, skip
        OLOG(LOG_NOTICE, "scanning zone #%d: id=%d, virtid=%d, numgroups=%d, name='%s'", i, zoneId, vzoneId, numGroups, zonename.c_str());
        // - the devices in the zone
        req.clear();
        Ds485Comm::payload_append16(req, zoneId);
        err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, ZONE_DEVICE_COUNT, ZONE_DEVICE_COUNT_ALL, req);
        if (Error::notOK(err)) return err;
        uint16_t numZoneDevices;
        pli = 3;
        if ((pli = Ds485Comm::payload_get16(resp, pli, numZoneDevices))==0) continue; // something is wrong, skip
        OLOG(LOG_INFO, "zone #%d: number of devices = %d", i, numZoneDevices);
        for (int j=0; j<numZoneDevices; j++) {
          string req;
          Ds485Comm::payload_append16(req, zoneId);
          Ds485Comm::payload_append16(req, j);
          err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, DEVICE_INFO, DEVICE_INFO_BY_INDEX, req);
          if (Error::notOK(err)) return err;
          pli = 3;
          uint16_t devId;
          if ((pli = Ds485Comm::payload_get16(resp, pli, devId))==0) continue; // something is wrong, skip
          uint16_t vendId;
          if ((pli = Ds485Comm::payload_get16(resp, pli, vendId))==0) continue; // something is wrong, skip
          uint16_t prodId;
          if ((pli = Ds485Comm::payload_get16(resp, pli, prodId))==0) continue; // something is wrong, skip
          uint16_t funcId;
          if ((pli = Ds485Comm::payload_get16(resp, pli, funcId))==0) continue; // something is wrong, skip
          uint16_t vers;
          if ((pli = Ds485Comm::payload_get16(resp, pli, vers))==0) continue; // something is wrong, skip
          uint16_t zoneId;
          if ((pli = Ds485Comm::payload_get16(resp, pli, zoneId))==0) continue; // something is wrong, skip
          uint8_t active;
          if ((pli = Ds485Comm::payload_get8(resp, pli, active))==0) continue; // something is wrong, skip
          uint8_t locked;
          if ((pli = Ds485Comm::payload_get8(resp, pli, locked))==0) continue; // something is wrong, skip
          uint8_t outMode;
          if ((pli = Ds485Comm::payload_get8(resp, pli, outMode))==0) continue; // something is wrong, skip
          uint8_t ltMode;
          if ((pli = Ds485Comm::payload_get8(resp, pli, ltMode))==0) continue; // something is wrong, skip
          DsGroupMask groups;
          if ((pli = Ds485Comm::payload_getGroups(resp, pli, groups))==0) continue; // something is wrong, skip
          string devName;
          if ((pli = Ds485Comm::payload_getString(resp, pli, 21, devName))==0) continue; // something is wrong, skip
          DsUid dSUID;
          dSUID.setAsBinary(resp.substr(pli, 17)); pli += 17;
          uint8_t activeGroup;
          if ((pli = Ds485Comm::payload_get8(resp, pli, activeGroup))==0) continue; // something is wrong, skip
          uint8_t defaultGroup;
          if ((pli = Ds485Comm::payload_get8(resp, pli, defaultGroup))==0) continue; // something is wrong, skip
          OLOG(LOG_INFO,
            "device #%d: %s [0x%04x] - '%s'\n"
            "- vendId=0x%04x, prodId=0x%04x, funcId=0x%04x, vers=0x%04x\n"
            "- zoneID=%d/0x%04x, active=%d, locked=%d\n"
            "- outMode=0x%04x, ltMode=0x%04x\n"
            "- groups=0x%016llx, activeGroup=%d, defaultGroup=%d",
            j, dSUID.text().c_str(), devId, devName.c_str(),
            vendId, prodId, funcId, vers,
            zoneId, zoneId, active, locked,
            outMode, ltMode,
            groups, activeGroup, defaultGroup
          );
          Ds485DevicePtr dev = new Ds485Device(this, dsmDsuid, devId);
          dev->mIsPresent = active;
          // make a real dSUID out of it
          dev->mDSUID.setAsDSId(dSUID.getBinary().substr(12, 4));
          dev->initializeName(devName);
          // - output channel info for determining output function
          req.clear();
          Ds485Comm::payload_append16(req, devId);
          err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, DEVICE_O_P_C_TABLE, DEVICE_O_P_C_TABLE_GET_COUNT, req);
          if (Error::notOK(err)) return err;
          pli = 3;
          uint8_t numOPC;
          if ((pli = Ds485Comm::payload_get8(resp, pli, numOPC))==0) continue; // something is wrong, skip
          OLOG(LOG_INFO, "device #%d: number of OPC channels = %d", j, numOPC);
          dev->mNumOPC = numOPC;
          // - output mode and function
          VdcOutputMode mode = outputmode_disabled;
          if ((outMode>=17 && outMode<=24) || outMode==28 || outMode==30) mode = outputmode_gradual;
          else if (outMode!=0) mode = outputmode_binary;
          VdcOutputFunction func = mode==outputmode_binary ? outputFunction_switch : outputFunction_dimmer;
          VdcUsageHint usage = usage_room;
          // - OPC channels
          for (int oi=0; oi<numOPC; oi++) {
            // - OPC channel info
            req.clear();
            Ds485Comm::payload_append16(req, devId);
            Ds485Comm::payload_append8(req, oi);
            err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, DEVICE_O_P_C_TABLE, DEVICE_O_P_C_TABLE_GET_BY_INDEX, req);
            if (Error::notOK(err)) return err;
            pli = 3;
            uint8_t channelId;
            if ((pli = Ds485Comm::payload_get8(resp, pli, channelId))==0) continue; // something is wrong, skip
            OLOG(LOG_INFO, "device #%d: channel #%d: channelId=%d", j, oi, channelId);
            // check channelid, gives indication for output function
            if (channelId==channeltype_hue) func = outputFunction_colordimmer;
            if (channelId==channeltype_colortemp && func!=outputFunction_colordimmer) func = outputFunction_ctdimmer;
            if (channelId==channeltype_colortemp && func!=outputFunction_colordimmer) func = outputFunction_ctdimmer;
            if (channelId==channeltype_shade_position_outside || channelId==channeltype_shade_position_inside) func = outputFunction_positional;
            if (channelId==channeltype_shade_position_outside || channelId==channeltype_shade_angle_outside) usage = usage_outdoors;
          }
          // examine the funcid for basic device setup
          // - color class
          uint8_t funcClass = (funcId>>12)&0x0F;
          // default to joker for unsupported or DS special ones (such as 16==server controlled)
          dev->setColorClass(static_cast<DsClass>(funcClass==0 || funcClass>=numColorClasses ? class_black_joker : funcClass));
          if (mode!=outputmode_disabled) {
            // - instantiate output
            OutputBehaviourPtr ob;
            if (funcClass==class_yellow_light) {
              if (func==outputFunction_colordimmer || func==outputFunction_ctdimmer) {
                // color or CT light
                dev->installSettings(new ColorLightDeviceSettings(*dev));
                ob = new ColorLightBehaviour(*dev, func==outputFunction_ctdimmer);
                ob->setHardwareName(func==outputFunction_ctdimmer ? "CT light" : "color light");
              }
              else {
                // single color light
                dev->installSettings(new LightDeviceSettings(*dev));
                ob = new LightBehaviour(*dev);
                ob->setHardwareName("light");
              }
            }
            else if (funcClass==class_grey_shadow) {
              // shadow
              dev->installSettings(new ShadowDeviceSettings(*dev));
              ShadowBehaviourPtr sb = new ShadowBehaviour(*dev, (DsGroup)defaultGroup);
              sb->setDeviceParams(shadowdevice_jalousie, false, 0, 0, 0, true); // no move semantics, just set values
              ob = sb;
              ob->setHardwareName("light");
            }
            else {
              // just a simple single channel output
              dev->installSettings(DeviceSettingsPtr(new SceneDeviceSettings(*dev)));
              ob = new OutputBehaviour(*dev);
              if (mode==outputmode_gradual) {
                ob->addChannel(new PercentageLevelChannel(*ob, "dimmer"));
              }
              else {
                ob->addChannel(new DigitalChannel(*ob, "relay"));
              }
            }
            ob->setHardwareOutputConfig(func, mode, usage, false, -1);
            ob->resetGroupMembership(groups);
            dev->addBehaviour(ob);
          }
          else {
            dev->installSettings();
          }
          // - zoneid (needs instantiated settings)
          dev->setZoneID(zoneId);
          // - button info
          //   Note: DS does have terminal blocks with multiple button inputs, but these are represented as multiple devices on the bus
          bool hasButton = true; // most dS blocks do
          if (
            ((funcId&0xFFC0)==0x1000 && (funcId&0x07)==7) || // dS R100
            ((funcId&0xFFC0)==0x1100 && (funcId&0x07)==0) // dS R105
          ) {
            hasButton = false;
          }
          if (hasButton) {
            req.clear();
            Ds485Comm::payload_append16(req, devId);
            err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, DEVICE_BUTTON_INFO, DEVICE_BUTTON_INFO_BY_DEVICE, req);
            if (Error::notOK(err)) return err;
            pli = 3;
            uint8_t ltNumGrp0;
            if ((pli = Ds485Comm::payload_get8(resp, pli, ltNumGrp0))==0) goto nobutton; // something is wrong, skip
            pli++; // skip "DeprecatedGroupIfUpTo15"
            uint8_t buttongroup;
            if ((pli = Ds485Comm::payload_get8(resp, pli, buttongroup))==0) goto nobutton; // something is wrong, skip
            uint8_t buttonflags;
            if ((pli = Ds485Comm::payload_get8(resp, pli, buttonflags))==0) goto nobutton; // something is wrong, skip
            uint8_t buttonchannel;
            if ((pli = Ds485Comm::payload_get8(resp, pli, buttonchannel))==0) goto nobutton; // something is wrong, skip
            pli++; // skip "unused"
            OLOG(LOG_INFO,
              "device #%d '%s': button: id/LTNUMGRP0=0x%02x, group=%d, flags=0x%02x, channel=%d",
              j, devName.c_str(),
              ltNumGrp0, buttongroup, buttonflags, buttonchannel
            );
            DsButtonMode buttonMode = (DsButtonMode)((ltNumGrp0>>4) & 0x0F);
            VdcButtonType bty = buttonType_single;
            VdcButtonElement bel = buttonElement_center;
            const char* bname = "button";
            int bcount = 1;
            if (ltMode>=5 && ltMode<=12) bty = buttonType_2way;
            else if (ltMode==2 || ltMode==3) bty = buttonType_onOffSwitch;
            else if (ltMode==13) {
              bname = "up";
              bel = buttonElement_up;
              bcount = 2;
            }
            for (int bidx=0; bidx<bcount; bidx++) {
              ButtonBehaviourPtr bb = new ButtonBehaviour(*dev, bname); // automatic id
              bb->setHardwareButtonConfig(0, bty, bel, false, 0, 0); // not combinable
              bb->setGroup((DsGroup)buttongroup);
              bb->setChannel((DsChannelType)buttonchannel);
              bb->setFunction((DsButtonFunc)(ltNumGrp0 & 0x0F));
              bb->setDsMode(buttonMode);
              bb->setCallsPresent((buttonflags&(1<<1))==0); // inversed, bit 1 means NOT calling present
              bb->setSetsLocalPriority(buttonflags&(1<<0));
              dev->addBehaviour(bb);
              bname = "down";
              bel = buttonElement_down;
            }
          }
        nobutton:
          // - binary input info
          req.clear();
          Ds485Comm::payload_append16(req, devId);
          err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, DEVICE_BINARY_INPUT, DEVICE_BINARY_INPUT_GET_COUNT, req);
          if (Error::notOK(err)) return err;
          pli = 3;
          uint8_t numBinInps;
          if ((pli = Ds485Comm::payload_get8(resp, pli, numBinInps))==0) continue; // something is wrong, skip
          OLOG(LOG_INFO, "device #%d: number of binary inputs = %d", j, numBinInps);
          for (int bi=0; bi<numBinInps; bi++) {
            // - binary input info
            req.clear();
            Ds485Comm::payload_append16(req, devId);
            Ds485Comm::payload_append8(req, bi);
            err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, DEVICE_BINARY_INPUT, DEVICE_BINARY_INPUT_GET_BY_INDEX, req);
            if (Error::notOK(err)) return err;
            pli = 3;
            uint8_t inpTargetGroupType;
            if ((pli = Ds485Comm::payload_get8(resp, pli, inpTargetGroupType))==0) continue; // something is wrong, skip
            uint8_t inpTargetGroup;
            if ((pli = Ds485Comm::payload_get8(resp, pli, inpTargetGroup))==0) continue; // something is wrong, skip
            uint8_t inpType;
            if ((pli = Ds485Comm::payload_get8(resp, pli, inpType))==0) continue; // something is wrong, skip
            uint8_t inpButtonId;
            if ((pli = Ds485Comm::payload_get8(resp, pli, inpButtonId))==0) continue; // something is wrong, skip
            uint8_t inpIndependent;
            if ((pli = Ds485Comm::payload_get8(resp, pli, inpIndependent))==0) continue; // something is wrong, skip
            OLOG(LOG_INFO,
              "- device #%d: binary input #%d: targetGroupType=%d, targetGroup=%d, type=%d, buttonId=0x%02x, independent=%d",
              j, bi, inpTargetGroupType, inpTargetGroup, inpType, inpButtonId, inpIndependent
            );
            BinaryInputBehaviourPtr ib = new BinaryInputBehaviour(*dev, ""); // automatic ID
            ib->setHardwareInputConfig((DsBinaryInputType)inpType, usage_undefined, true, Never, Never);
            // TODO: maybe need to model some inputs as buttons
            ib->setGroup((DsGroup)inpTargetGroup);
            dev->addBehaviour(ib);
          }
          // - sensor info
          req.clear();
          Ds485Comm::payload_append16(req, devId);
          err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, DEVICE_SENSOR, DEVICE_SENSOR_GET_COUNT, req);
          if (Error::notOK(err)) return err;
          pli = 3;
          uint8_t numSensors;
          if ((pli = Ds485Comm::payload_get8(resp, pli, numSensors))==0) continue; // something is wrong, skip
          OLOG(LOG_INFO, "device #%d: number of sensors = %d", j, numSensors);
          for (int si=0; si<numSensors; si++) {
            // all sensors must have a corresponding info, even if null
            dev->setSensorInfoAtIndex(si, DsSensorInstanceInfo()); ///< we do not know it yet, if we fail getting details we still need this index position occupied
            // - sensor info
            req.clear();
            Ds485Comm::payload_append16(req, devId);
            Ds485Comm::payload_append8(req, si);
            err = mDs485Comm.executeQuerySync(resp, 0, dsmDsuid, DEVICE_SENSOR, DEVICE_SENSOR_GET_BY_INDEX, req);
            if (Error::notOK(err)) return err;
            pli = 3;
            uint8_t sensorType;
            if ((pli = Ds485Comm::payload_get8(resp, pli, sensorType))==0) continue; // something is wrong, skip
            uint32_t sensorPollinterval;
            if ((pli = Ds485Comm::payload_get32(resp, pli, sensorPollinterval))==0) continue; // something is wrong, skip
            uint8_t sensorZone;
            if ((pli = Ds485Comm::payload_get8(resp, pli, sensorZone))==0) continue; // something is wrong, skip
            uint8_t sensorPushConvert;
            if ((pli = Ds485Comm::payload_get8(resp, pli, sensorPushConvert))==0) continue; // something is wrong, skip
            OLOG(LOG_INFO,
              "device #%d: sensor #%d: type=%d, pollinterval=%d, globalZone=%d, pushConvert=%d",
              j, si, sensorType, sensorPollinterval, sensorZone, sensorPushConvert
            );
            // get sensor info
            const DsSensorTypeInfo* siP = Ds485Device::sensorTypeInfoByDsType(sensorType);
            if (siP) {
              // update info we need later to process values
              DsSensorInstanceInfo sinfo;
              sinfo.mSensorTypeInfoP = siP;
              if (!siP->internal) {
                SensorBehaviourPtr sb = new SensorBehaviour(*dev, ""); // automatic ID
                sb->setHardwareSensorConfig(
                  siP->vdcSensorType, siP->usageHint,
                  siP->min, siP->max, siP->resolution,
                  sensorPollinterval*Second, sensorPollinterval*Second*3
                );
                sb->initColorClass(siP->colorclass);
                sb->setGroup(siP->group);
                dev->addBehaviour(sb);
                sinfo.mSensorBehaviour = sb;
              }
              dev->setSensorInfoAtIndex(si, sinfo);
            }
          } // sensors
          // save device in list
          mDs485Devices[fullDevId(dev->mDsmDsUid, dev->mDevId)] = dev;
        } // device
      } // zone
    } // not me
  } // dsms

  return ErrorPtr();
}


#endif // ENABLE_DS485DEVICES

