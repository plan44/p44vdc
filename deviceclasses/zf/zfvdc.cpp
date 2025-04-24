//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2017-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "zfvdc.hpp"

#if ENABLE_ZF

using namespace p44;


ZfVdc::ZfVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  Vdc(aInstanceNumber, aVdcHostP, aTag),
	mZfComm(MainLoop::currentMainLoop()),
  learningMode(false)
{
  mZfComm.isMemberVariable();
}


void ZfVdc::setLogLevelOffset(int aLogLevelOffset)
{
  mZfComm.setLogLevelOffset(aLogLevelOffset);
  inherited::setLogLevelOffset(aLogLevelOffset);
}


const char *ZfVdc::vdcClassIdentifier() const
{
  return "ZF_Bus_Container";
}


bool ZfVdc::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("vdc_zf", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}



// MARK: - DB and initialisation

// Version history
//  1 : initial version
#define ZF_SCHEMA_MIN_VERSION 1 // minimally supported version, anything older will be deleted
#define ZF_SCHEMA_VERSION 1 // current version

string ZfPersistence::dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion)
{
  string sql;
  if (aFromVersion==0) {
    // create DB from scratch
		// - use standard globs table for schema version
    sql = inherited::dbSchemaUpgradeSQL(aFromVersion, aToVersion);
		// - create my tables
    sql.append(
			"CREATE TABLE knownDevices ("
			" zfAddress INTEGER,"
      " subdevice INTEGER,"
      " deviceType INTEGER,"
      " PRIMARY KEY (zfAddress, subdevice)"
			");"
		);
    // reached final version in one step
    aToVersion = ZF_SCHEMA_VERSION;
  }
  return sql;
}


void ZfVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  // load persistent params for dSUID
  load();
  // load private data
	string databaseName = getPersistentDataDir();
	string_format_append(databaseName, "%s_%d.sqlite3", vdcClassIdentifier(), getInstanceNumber());
  ErrorPtr error = db.connectAndInitialize(databaseName.c_str(), ZF_SCHEMA_VERSION, ZF_SCHEMA_MIN_VERSION, aFactoryReset);
  if (Error::notOK(error)) {
    // failed DB, no point in starting communication
    aCompletedCB(error); // return status of DB init
  }
  else {
    // start communication
    mZfComm.initialize(aCompletedCB);
  }
}




// MARK: - collect devices

void ZfVdc::removeDevices(bool aForget)
{
  inherited::removeDevices(aForget);
  zfDevices.clear();
}



void ZfVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  // install standard message handler
  mZfComm.setReceivedPacketHandler(boost::bind(&ZfVdc::handlePacket, this, _1, _2));
  // incrementally collecting ZF devices makes no sense as the set of devices is defined by learn-in (DB state)
  if (!(aRescanFlags & rescanmode_incremental)) {
    // start with zero
    removeDevices(aRescanFlags & rescanmode_clearsettings);
    // - read learned-in EnOcean button IDs from DB
    sqlite3pp::query qry(db);
    if (qry.prepare("SELECT zfAddress, subdevice, deviceType FROM knownDevices")==SQLITE_OK) {
      for (sqlite3pp::query::iterator i = qry.begin(); i != qry.end(); ++i) {
        ZfSubDevice subDeviceIndex = i->get<int>(1);
        ZfDevicePtr newdev = ZfDevice::newDevice(
          this,
          i->get<int>(0), // address
          subDeviceIndex, // subdeviceIndex
          (ZfDeviceType)i->get<int>(2), // device type
          subDeviceIndex // first subdeviceIndex (is automatically last as well)
        );
        if (newdev) {
          // we fetched this from DB, so it is already known (don't save again!)
          addKnownDevice(newdev);
        }
        else {
          OLOG(LOG_ERR,
            "ZF device could not be created for addr=%08X, subdevice=%d, deviceType=%d",
            i->get<int>(0), // address
            subDeviceIndex, // subdeviceIndex
            i->get<int>(2) // device type
          );
        }
      }
    }
  }
  // assume ok
  aCompletedCB(ErrorPtr());
}



bool ZfVdc::addKnownDevice(ZfDevicePtr aZfDevice)
{
  if (simpleIdentifyAndAddDevice(aZfDevice)) {
    // not a duplicate, actually added - add to my own list
    zfDevices.insert(make_pair(aZfDevice->getAddress(), aZfDevice));
    return true;
  }
  return false;
}



bool ZfVdc::addAndRememberDevice(ZfDevicePtr aZfDevice)
{
  if (addKnownDevice(aZfDevice)) {
    // save ZF ID to DB
    // - check if this subdevice is already stored
    if(db.executef(
      "INSERT OR REPLACE INTO knownDevices (zfAddress, subdevice, deviceType) VALUES (%d,%d,%d)",
      aZfDevice->getAddress(),
      aZfDevice->getSubDevice(),
      aZfDevice->getZfDeviceType()
    )!=SQLITE_OK) {
      OLOG(LOG_ERR, "Error saving device: %s", db.error()->description().c_str());
    }
    return true;
  }
  return false;
}


void ZfVdc::removeDevice(DevicePtr aDevice, bool aForget)
{
  ZfDevicePtr ed = boost::dynamic_pointer_cast<ZfDevice>(aDevice);
  if (ed) {
    // - remove single device from superclass
    inherited::removeDevice(aDevice, aForget);
    // - remove only selected subdevice from my own list, other subdevices might be other devices
    ZfDeviceMap::iterator pos = zfDevices.lower_bound(ed->getAddress());
    while (pos!=zfDevices.upper_bound(ed->getAddress())) {
      if (pos->second->getSubDevice()==ed->getSubDevice()) {
        // this is the subdevice we want deleted
        zfDevices.erase(pos);
        break; // done
      }
      pos++;
    }
  }
}


void ZfVdc::unpairDevicesByAddress(ZfAddress aZfAddress, bool aForgetParams, ZfSubDevice aFromIndex, ZfSubDevice aNumIndices)
{
  // remove all logical devices with same physical Zf address
  typedef list<ZfDevicePtr> TbdList;
  TbdList toBeDeleted;
  // collect those we need to remove
  for (ZfDeviceMap::iterator pos = zfDevices.lower_bound(aZfAddress); pos!=zfDevices.upper_bound(aZfAddress); ++pos) {
    // check subdevice index
    ZfSubDevice i = pos->second->getSubDevice();
    if (i>=aFromIndex && ((aNumIndices==0) || (i<aFromIndex+aNumIndices))) {
      toBeDeleted.push_back(pos->second);
    }
  }
  // now call vanish (which will in turn remove devices from the container's list
  for (TbdList::iterator pos = toBeDeleted.begin(); pos!=toBeDeleted.end(); ++pos) {
    (*pos)->hasVanished(aForgetParams);
  }
}


// MARK: - Handle received packets


void ZfVdc::handlePacket(ZfPacketPtr aPacket, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    if (learningMode) {
      processLearn(aPacket);
    }
    else {
      dispatchPacket(aPacket);
    }
  }
}


Tristate ZfVdc::processLearn(ZfPacketPtr aPacket)
{
  // learn only for "pressed" packet, ignore "released"
  if (aPacket->opCode==1 && aPacket->data==0) {
    // very simple for now, always create button
    ZfDeviceType type = zf_button;
    // check if we already know the (sub)device
    ZfSubDevice subdevice = 0;
    ZfSubDevice numSubDevices = 1; // default to 1 (for removal, 0 for removing all subdevices of same address)
    bool learnIn = true; // if we don't find anything below, it's a learn-in for sure
    for (ZfDeviceMap::iterator pos = zfDevices.lower_bound(aPacket->uid); pos!=zfDevices.upper_bound(aPacket->uid); ++pos) {
      ZfSubDevice i = pos->second->getSubDevice();
      if (numSubDevices==0 || (subdevice>=i && subdevice<i+numSubDevices)) {
        // always delete all subdevices or unlearn comes from specified subdevice range
        learnIn = false;
        break;
      }
    }
    if (learnIn) {
      if (onlyEstablish!=no && type!=zf_unknown) {
        int numNewDevices = ZfDevice::createDevicesFromType(this, aPacket->uid, type, subdevice);
        if (numNewDevices>0) {
          // successfully learned at least one device
          // - update learn status (device learned)
          getVdcHost().reportLearnEvent(true, ErrorPtr());
          return yes; // learned in
        }
      }
    }
    else {
      if (onlyEstablish!=yes) {
        // device learned out, un-pair all logical dS devices it has represented
        // but keep dS level config in case it is reconnected
        unpairDevicesByAddress(aPacket->uid, false, subdevice, numSubDevices);
        getVdcHost().reportLearnEvent(false, ErrorPtr());
        return no; // always successful learn out
      }
    }
  }
  return undefined; // nothing learned in, nothing learned out
}


void ZfVdc::dispatchPacket(ZfPacketPtr aPacket)
{
  bool reachedDevice = false;
  for (ZfDeviceMap::iterator pos = zfDevices.lower_bound(aPacket->uid); pos!=zfDevices.upper_bound(aPacket->uid); ++pos) {
    // handle regularily (might be RPS switch which does not have separate learn/action packets
    pos->second->handlePacket(aPacket);
    reachedDevice = true;
  }
  if (!reachedDevice) {
    OLOG(LOG_INFO, "Received ZF message with sender-ID=%08X not directed to any known device -> ignored", aPacket->uid);
  }
}



// MARK: - EnOcean specific methods


ErrorPtr ZfVdc::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
//  if (aMethod=="x-p44-addProfile") {
//    // create a composite device out of existing single-channel ones
//    respErr = addProfile(aRequest, aParams);
//  }
//  else if (aMethod=="x-p44-simulatePacket") {
//    // simulate reception of a ESP packet
//    respErr = simulatePacket(aRequest, aParams);
//  }
//  else
  {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }
  return respErr;
}



//  ErrorPtr ZfVdc::addProfile(VdcApiRequestPtr aRequest, ApiValuePtr aParams)
//  {
//    // add an EnOcean profile
//    ErrorPtr respErr;
//    ApiValuePtr o;
//    respErr = checkParam(aParams, "eep", o); // EEP with variant in MSB
//    if (Error::isOK(respErr)) {
//      EnoceanProfile eep = o->uint32Value();
//      respErr = checkParam(aParams, "address", o);
//      if (Error::isOK(respErr)) {
//        // remote device address
//        // if 0xFF800000..0xFF80007F : bit0..6 = ID base offset to ID base of modem
//        // if 0xFF8000FF : automatically take next unused ID base offset
//        EnoceanAddress addr = o->uint32Value();
//        if ((addr & 0xFFFFFF00)==0xFF800000) {
//          // relative to ID base
//          // - get map of already used offsets
//          string usedOffsetMap;
//          usedOffsetMap.assign(128,'0');
//          for (EnoceanDeviceMap::iterator pos = enoceanDevices.begin(); pos!=enoceanDevices.end(); ++pos) {
//            pos->second->markUsedBaseOffsets(usedOffsetMap);
//          }
//          addr &= 0xFF; // extract offset
//          if (addr==0xFF) {
//            // auto-determine offset
//            for (addr=0; addr<128; addr++) {
//              if (usedOffsetMap[addr]=='0') break; // free offset here
//            }
//            if (addr>128) {
//              respErr = ErrorPtr(new WebError(400, "no more free base ID offsets"));
//            }
//          }
//          else {
//            if (usedOffsetMap[addr]!='0') {
//              respErr = ErrorPtr(new WebError(400, "invalid or already used base ID offset specifier"));
//            }
//          }
//          // add-in my own ID base
//          addr += enoceanComm.idBase();
//        }
//        // now create device(s)
//        if (Error::isOK(respErr)) {
//          // create devices as if this was a learn-in
//          int newDevices = EnoceanDevice::createDevicesFromEEP(this, addr, eep, manufacturer_unknown);
//          if (newDevices<1) {
//            respErr = ErrorPtr(new WebError(400, "Unknown EEP specification, no device(s) created"));
//          }
//          else {
//            ApiValuePtr r = aRequest->newApiValue();
//            r->setType(apivalue_object);
//            r->add("newDevices", r->newUint64(newDevices));
//            aRequest->sendResult(r);
//            respErr.reset(); // make sure we don't send an extra ErrorOK
//          }
//        }
//      }
//    }
//    return respErr;
//  }
//

// MARK: - learn and unlearn devices


void ZfVdc::setLearnMode(bool aEnableLearning, bool aDisableProximityCheck, Tristate aOnlyEstablish)
{
  // put normal radio packet evaluator into learn mode
  learningMode = aEnableLearning;
  onlyEstablish = aOnlyEstablish;
  // TODO: do we need that?
  // disableProximityCheck = aDisableProximityCheck;
}




//  // MARK: - Self test
//
//  void ZfVdc::selfTest(StatusCB aCompletedCB)
//  {
//    // install test packet handler
//    zfComm.setRadioPacketHandler(boost::bind(&ZfVdc::handleTestRadioPacket, this, aCompletedCB, _1, _2));
//    // start watchdog
//    enoceanComm.initialize(NULL);
//  }
//
//
//  void ZfVdc::handleTestRadioPacket(StatusCB aCompletedCB, Esp3PacketPtr aEsp3PacketPtr, ErrorPtr aError)
//  {
//    // ignore packets with error
//    if (Error::isOK(aError)) {
//      if (aEsp3PacketPtr->eepRorg()==rorg_RPS && aEsp3PacketPtr->radioDBm()>MIN_LEARN_DBM && enoceanComm.modemAppVersion()>0) {
//        // uninstall handler
//        enoceanComm.setRadioPacketHandler(NoOP);
//        // seen both watchdog response (modem works) and independent RPS telegram (RF is ok)
//        OLOG(LOG_NOTICE,
//          "- enocean modem info: appVersion=0x%08X, apiVersion=0x%08X, modemAddress=0x%08X, idBase=0x%08X",
//          enoceanComm.modemAppVersion(), enoceanComm.modemApiVersion(), enoceanComm.modemAddress(), enoceanComm.idBase()
//        );
//        aCompletedCB(ErrorPtr());
//        // done
//        return;
//      }
//    }
//    // - still waiting
//    OLOG(LOG_NOTICE, "- enocean test: still waiting for RPS telegram in learn distance");
//  }

#endif // ENABLE_ZF

