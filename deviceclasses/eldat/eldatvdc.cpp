//
//  Copyright (c) 2016-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "eldatvdc.hpp"

#if ENABLE_ELDAT

using namespace p44;


EldatVdc::EldatVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  Vdc(aInstanceNumber, aVdcHostP, aTag),
	eldatComm(MainLoop::currentMainLoop()),
  learningMode(false),
  disableProximityCheck(false)
{
  eldatComm.isMemberVariable();
}



const char *EldatVdc::vdcClassIdentifier() const
{
  return "Eldat_Bus_Container";
}


bool EldatVdc::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("vdc_eldat", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}



// MARK: ===== DB and initialisation

// Version history
//  1 : initial version
#define ELDAT_SCHEMA_MIN_VERSION 1 // minimally supported version, anything older will be deleted
#define ELDAT_SCHEMA_VERSION 1 // current version

string EldatPersistence::dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion)
{
  string sql;
  if (aFromVersion==0) {
    // create DB from scratch
		// - use standard globs table for schema version
    sql = inherited::dbSchemaUpgradeSQL(aFromVersion, aToVersion);
		// - create my tables
    sql.append(
			"CREATE TABLE knownDevices ("
			" eldatAddress INTEGER,"
      " subdevice INTEGER,"
      " deviceType INTEGER,"
      " PRIMARY KEY (eldatAddress, subdevice)"
			");"
		);
    // reached final version in one step
    aToVersion = ELDAT_SCHEMA_VERSION;
  }
  return sql;
}


void EldatVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
	string databaseName = getPersistentDataDir();
	string_format_append(databaseName, "%s_%d.sqlite3", vdcClassIdentifier(), getInstanceNumber());
  ErrorPtr error = db.connectAndInitialize(databaseName.c_str(), ELDAT_SCHEMA_VERSION, ELDAT_SCHEMA_MIN_VERSION, aFactoryReset);
  if (!Error::isOK(error)) {
    // failed DB, no point in starting communication
    aCompletedCB(error); // return status of DB init
  }
  else {
    // start communication
    eldatComm.initialize(aCompletedCB);
  }
}




// MARK: ===== collect devices

void EldatVdc::removeDevices(bool aForget)
{
  inherited::removeDevices(aForget);
  eldatDevices.clear();
}



void EldatVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  // install standard message handler
  eldatComm.setReceivedMessageHandler(boost::bind(&EldatVdc::handleMessage, this, _1, _2));
  // incrementally collecting Eldat devices makes no sense as the set of devices is defined by learn-in (DB state)
  if (!(aRescanFlags & rescanmode_incremental)) {
    // start with zero
    removeDevices(aRescanFlags & rescanmode_clearsettings);
    // - read learned-in ELDAT devices from DB
    sqlite3pp::query qry(db);
    if (qry.prepare("SELECT eldatAddress, subdevice, deviceType FROM knownDevices")==SQLITE_OK) {
      for (sqlite3pp::query::iterator i = qry.begin(); i != qry.end(); ++i) {
        EldatSubDevice subDeviceIndex = i->get<int>(1);
        EldatDevicePtr newdev = EldatDevice::newDevice(
          this,
          i->get<int>(0), // address
          subDeviceIndex, // subdeviceIndex
          (EldatDeviceType)i->get<int>(2), // device type
          subDeviceIndex // first subdeviceIndex (is automatically last as well)
        );
        if (newdev) {
          // we fetched this from DB, so it is already known (don't save again!)
          addKnownDevice(newdev);
        }
        else {
          LOG(LOG_ERR,
            "ELDAT device could not be created for addr=%08X, subdevice=%d, deviceType=%d",
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



bool EldatVdc::addKnownDevice(EldatDevicePtr aEldatDevice)
{
  if (simpleIdentifyAndAddDevice(aEldatDevice)) {
    // not a duplicate, actually added - add to my own list
    eldatDevices.insert(make_pair(aEldatDevice->getAddress(), aEldatDevice));
    return true;
  }
  return false;
}



bool EldatVdc::addAndRememberDevice(EldatDevicePtr aEldatDevice)
{
  if (addKnownDevice(aEldatDevice)) {
    // save Eldat ID to DB
    // - check if this subdevice is already stored
    if(db.executef(
      "INSERT OR REPLACE INTO knownDevices (eldatAddress, subdevice, deviceType) VALUES (%d,%d,%d)",
      aEldatDevice->getAddress(),
      aEldatDevice->getSubDevice(),
      aEldatDevice->getEldatDeviceType()
    )!=SQLITE_OK) {
      ALOG(LOG_ERR, "Error saving device: %s", db.error()->description().c_str());
    }
    return true;
  }
  return false;
}


void EldatVdc::removeDevice(DevicePtr aDevice, bool aForget)
{
  EldatDevicePtr ed = boost::dynamic_pointer_cast<EldatDevice>(aDevice);
  if (ed) {
    // - remove single device from superclass
    inherited::removeDevice(aDevice, aForget);
    // - remove only selected subdevice from my own list, other subdevices might be other devices
    EldatDeviceMap::iterator pos = eldatDevices.lower_bound(ed->getAddress());
    while (pos!=eldatDevices.upper_bound(ed->getAddress())) {
      if (pos->second->getSubDevice()==ed->getSubDevice()) {
        // this is the subdevice we want deleted
        eldatDevices.erase(pos);
        break; // done
      }
      pos++;
    }
  }
}


void EldatVdc::unpairDevicesByAddress(EldatAddress aEldatAddress, bool aForgetParams, EldatSubDevice aFromIndex, EldatSubDevice aNumIndices)
{
  // remove all logical devices with same physical Eldat address
  typedef list<EldatDevicePtr> TbdList;
  TbdList toBeDeleted;
  // collect those we need to remove
  for (EldatDeviceMap::iterator pos = eldatDevices.lower_bound(aEldatAddress); pos!=eldatDevices.upper_bound(aEldatAddress); ++pos) {
    // check subdevice index
    EldatSubDevice i = pos->second->getSubDevice();
    if (i>=aFromIndex && ((aNumIndices==0) || (i<aFromIndex+aNumIndices))) {
      toBeDeleted.push_back(pos->second);
    }
  }
  // now call vanish (which will in turn remove devices from the container's list
  for (TbdList::iterator pos = toBeDeleted.begin(); pos!=toBeDeleted.end(); ++pos) {
    (*pos)->hasVanished(aForgetParams);
  }
}


// MARK: ===== Handle received messages


void EldatVdc::handleMessage(string aEldatMessage, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    EldatAddress senderAddress;
    char data[100];
    // try to scan Mode 0
    int rssi;
    int mode;
    if (sscanf(aEldatMessage.c_str(),"REC%2d,-%X,%X,%99s", &mode, &rssi, &senderAddress, data)==4) {
      rssi = -rssi;
      LOG(LOG_NOTICE, "processing REC message mode=%d, sender=0x%08X, RSSI=%d", mode, senderAddress, rssi);
      if (learningMode) {
        processLearn(senderAddress, (EldatMode)mode, rssi, data);
      }
      else {
        dispatchMessage(senderAddress, (EldatMode)mode, rssi, data);
      }
    }
    else {
      LOG(LOG_NOTICE, "received unknown ELDAT message: %s", aEldatMessage.c_str());
    }
  }
}


#define MIN_LEARN_DBM -65
// -65 = with RX10 R01 stick, about 20-30cm from device
// -75 = with RX10 R01 stick, about 1m from device

Tristate EldatVdc::processLearn(EldatAddress aSenderAddress, EldatMode aMode, int aRSSI, string aData)
{
  if (aMode!=0 || aData.size()!=1)
    return undefined; // invalid data
  // check RSSI
  if (!disableProximityCheck && aRSSI<MIN_LEARN_DBM) {
    // not close enough
    return undefined; // signal too weak for learn-in, treat as invalid data
  }
  char function = aData[0];
  // Unlike enocean, we only learn in/out one pair per learning action: A-B or C-D
  EldatDeviceType type = eldat_unknown;
  EldatSubDevice subdevice = 0;
  EldatSubDevice numSubDevices = 1; // default to 1 (for removal, 0 for removing all subdevices of same address)
  switch (function) {
    case 'A':
    case 'B':
      type = eldat_rocker;
      subdevice = 0;
      break;
    case 'C':
    case 'D':
      type = eldat_rocker;
      subdevice = 2;
      break;
  }
  // check if we already know the (sub)device
  bool learnIn = true; // if we don't find anything below, it's a learn-in for sure
  for (EldatDeviceMap::iterator pos = eldatDevices.lower_bound(aSenderAddress); pos!=eldatDevices.upper_bound(aSenderAddress); ++pos) {
    EldatSubDevice i = pos->second->getSubDevice();
    if (numSubDevices==0 || (subdevice>=i && subdevice<i+numSubDevices)) {
      // always delete all subdevices or unlearn comes from specified subdevice range
      learnIn = false;
      break;
    }
  }
  if (learnIn) {
    if (onlyEstablish!=no && type!=eldat_unknown) {
      int numNewDevices = EldatDevice::createDevicesFromType(this, aSenderAddress, type, subdevice);
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
      unpairDevicesByAddress(aSenderAddress, false, subdevice, numSubDevices);
      getVdcHost().reportLearnEvent(false, ErrorPtr());
      return no; // always successful learn out
    }
  }
  return undefined; // nothing learned in, nothing learned out
}


void EldatVdc::dispatchMessage(EldatAddress aSenderAddress, EldatMode aMode, int aRSSI, string aData)
{
  bool reachedDevice = false;
  for (EldatDeviceMap::iterator pos = eldatDevices.lower_bound(aSenderAddress); pos!=eldatDevices.upper_bound(aSenderAddress); ++pos) {
    // handle regularily (might be RPS switch which does not have separate learn/action packets
    pos->second->handleMessage(aMode, aRSSI, aData);
    reachedDevice = true;
  }
  if (!reachedDevice) {
    LOG(LOG_INFO, "Received Eldat message with sender-ID=%08X not directed to any known device -> ignored", aSenderAddress);
  }
}



// MARK: ===== ELDAT specific methods


ErrorPtr EldatVdc::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  // for now: NOP, let inherited handle it
  respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  return respErr;
}




// MARK: ===== learn and unlearn devices


void EldatVdc::setLearnMode(bool aEnableLearning, bool aDisableProximityCheck, Tristate aOnlyEstablish)
{
  // put normal radio packet evaluator into learn mode
  learningMode = aEnableLearning;
  onlyEstablish = aOnlyEstablish;
  disableProximityCheck = aDisableProximityCheck;
}



#endif // ENABLE_ELDAT

