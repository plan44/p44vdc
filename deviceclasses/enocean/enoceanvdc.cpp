//
//  Copyright (c) 2013-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "enoceanvdc.hpp"

#if ENABLE_ENOCEAN

using namespace p44;


EnoceanVdc::EnoceanVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  Vdc(aInstanceNumber, aVdcHostP, aTag),
  learningMode(false),
  selfTesting(false),
  disableProximityCheck(false),
	enoceanComm(MainLoop::currentMainLoop())
{
  enoceanComm.isMemberVariable();
}



const char *EnoceanVdc::vdcClassIdentifier() const
{
  return "EnOcean_Bus_Container";
}


bool EnoceanVdc::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("vdc_enocean", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


string EnoceanVdc::vdcModelVersion() const
{
  uint32_t v = enoceanComm.modemAppVersion();
  uint32_t a = enoceanComm.modemApiVersion();
  if (v==0) return inherited::vdcModelVersion();
  return string_format(
    "%d.%d.%d.%d/%d.%d.%d.%d",
    (v>>24)&0xFF, (v>>16)&0xFF, (v>>8)&0xFF, v&0xFF, 
    (a>>24)&0xFF, (a>>16)&0xFF, (a>>8)&0xFF, a&0xFF
  );
};




// MARK: ===== DB and initialisation

// Version history
//  1..3 : development versions
//  4 : first actually used schema
//  5 : subdevice indices of 2-way enocean buttons must be adjusted (now 2-spaced to leave room for single button mode)
//  6 : added additional table for secure device info
#define ENOCEAN_SCHEMA_MIN_VERSION 4 // minimally supported version, anything older will be deleted
#define ENOCEAN_SCHEMA_VERSION 6 // current version

string EnoceanPersistence::dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion)
{
  string sql;
  if (aFromVersion==0) {
    // create DB from scratch
		// - use standard globs table for schema version
    sql = inherited::dbSchemaUpgradeSQL(aFromVersion, aToVersion);
		// - create my tables
    sql.append(
			"CREATE TABLE knownDevices ("
			" enoceanAddress INTEGER,"
      " subdevice INTEGER,"
      " eeProfile INTEGER,"
      " eeManufacturer INTEGER,"
      " PRIMARY KEY (enoceanAddress, subdevice)"
			");"
		);
    // reached final version in one step
    aToVersion = ENOCEAN_SCHEMA_VERSION;
  }
  else if (aFromVersion==4) {
    // V4->V5: subdevice indices of 2-way enocean buttons must be adjusted (now 2-spaced to leave room for single button mode)
    // - affected profiles = 00-F6-02-FF and 00-F6-03-FF
    sql =
      "UPDATE knownDevices SET subdevice = 2*subdevice WHERE eeProfile=16122623 OR eeProfile=16122879;";
    // reached version 5
    aToVersion = 5;
  }
  else if (aFromVersion==5) {
    // V5->V6: add security info table
    sql =
			"CREATE TABLE secureDevices ("
			" enoceanAddress INTEGER,"
      " slf INTEGER,"
      " rlc INTEGER,"
      " key BLOB,"
      " teachInInfo INTEGER,"
      " PRIMARY KEY (enoceanAddress)"
			");";
    // reached version 6
    aToVersion = 6;
  }
  return sql;
}


void EnoceanVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
	string databaseName = getPersistentDataDir();
	string_format_append(databaseName, "%s_%d.sqlite3", vdcClassIdentifier(), getInstanceNumber());
  ErrorPtr error = db.connectAndInitialize(databaseName.c_str(), ENOCEAN_SCHEMA_VERSION, ENOCEAN_SCHEMA_MIN_VERSION, aFactoryReset);
  if (!Error::isOK(error)) {
    // failed DB, no point in starting communication
    aCompletedCB(error); // return status of DB init
  }
  else {
    #if ENABLE_ENOCEAN_SECURE
    // load the security infos, to be ready for secure communication from the very start
    loadSecurityInfos();
    #endif
    // start communication
    enoceanComm.initialize(aCompletedCB);
  }
}




// MARK: ===== collect devices

void EnoceanVdc::removeDevices(bool aForget)
{
  inherited::removeDevices(aForget);
  enoceanDevices.clear();
}



void EnoceanVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  // install standard packet handler
  enoceanComm.setRadioPacketHandler(boost::bind(&EnoceanVdc::handleRadioPacket, this, _1, _2));
  enoceanComm.setEventPacketHandler(boost::bind(&EnoceanVdc::handleEventPacket, this, _1, _2));
  // incrementally collecting EnOcean devices makes no sense as the set of devices is defined by learn-in (DB state)
  if (!(aRescanFlags & rescanmode_incremental)) {
    // start with zero
    removeDevices(aRescanFlags & rescanmode_clearsettings);
    // - read learned-in EnOcean device IDs from DB
    sqlite3pp::query qry(db);
    if (qry.prepare("SELECT enoceanAddress, subdevice, eeProfile, eeManufacturer FROM knownDevices")==SQLITE_OK) {
      for (sqlite3pp::query::iterator i = qry.begin(); i != qry.end(); ++i) {
        EnoceanSubDevice subDeviceIndex = i->get<int>(1);
        EnoceanDevicePtr newdev = EnoceanDevice::newDevice(
          this,
          i->get<int>(0), subDeviceIndex, // address / subdeviceIndex
          i->get<int>(2), i->get<int>(3), // profile / manufacturer
          false // don't send teach-in responses
        );
        if (newdev) {
          // we fetched this from DB, so it is already known (don't save again!)
          addKnownDevice(newdev);
        }
        else {
          LOG(LOG_ERR,
            "EnOcean device could not be created for addr=%08X, subdevice=%d, profile=%08X, manufacturer=%d",
            i->get<int>(0), subDeviceIndex, // address / subdevice
            i->get<int>(2), i->get<int>(3) // profile / manufacturer
          );
        }
      }
    }
  }
  // assume ok
  aCompletedCB(ErrorPtr());
}


bool EnoceanVdc::addKnownDevice(EnoceanDevicePtr aEnoceanDevice)
{
  if (simpleIdentifyAndAddDevice(aEnoceanDevice)) {
    // not a duplicate, actually added - add to my own list
    enoceanDevices.insert(make_pair(aEnoceanDevice->getAddress(), aEnoceanDevice));
    #if ENABLE_ENOCEAN_SECURE
    // set device security info if available
    EnOceanSecurityPtr sec = securityInfoForSender(aEnoceanDevice->getAddress(), false);
    if (sec) {
      aEnoceanDevice->setSecurity(sec);
    }
    #endif
    return true;
  }
  return false;
}


bool EnoceanVdc::addAndRememberDevice(EnoceanDevicePtr aEnoceanDevice)
{
  if (addKnownDevice(aEnoceanDevice)) {
    // save enocean ID to DB
    if(db.executef(
      "INSERT OR REPLACE INTO knownDevices (enoceanAddress, subdevice, eeProfile, eeManufacturer) VALUES (%d,%d,%d,%d)",
      aEnoceanDevice->getAddress(),
      aEnoceanDevice->getSubDevice(),
      aEnoceanDevice->getEEProfile(),
      aEnoceanDevice->getEEManufacturer()
    )!=SQLITE_OK) {
      ALOG(LOG_ERR, "Error saving device: %s", db.error()->description().c_str());
    }
    return true;
  }
  return false;
}


void EnoceanVdc::removeDevice(DevicePtr aDevice, bool aForget)
{
  EnoceanDevicePtr ed = boost::dynamic_pointer_cast<EnoceanDevice>(aDevice);
  if (ed) {
    // - remove single device from superclass
    inherited::removeDevice(aDevice, aForget);
    // - remove only selected subdevice from my own list, other subdevices might be other devices
    EnoceanDeviceMap::iterator pos = enoceanDevices.lower_bound(ed->getAddress());
    while (pos!=enoceanDevices.upper_bound(ed->getAddress())) {
      if (pos->second->getSubDevice()==ed->getSubDevice()) {
        // this is the subdevice we want deleted
        enoceanDevices.erase(pos);
        break; // done
      }
      pos++;
    }
  }
}


bool EnoceanVdc::unpairDevicesByAddressAndEEP(EnoceanAddress aEnoceanAddress, EnoceanProfile aEEP, bool aForgetParams, EnoceanSubDevice aFromIndex, EnoceanSubDevice aNumIndices)
{
  // remove all logical devices with same physical EnOcean address
  typedef list<EnoceanDevicePtr> TbdList;
  TbdList toBeDeleted;
  // collect those we need to remove
  for (EnoceanDeviceMap::iterator pos = enoceanDevices.lower_bound(aEnoceanAddress); pos!=enoceanDevices.upper_bound(aEnoceanAddress); ++pos) {
    // check EEP if specified
    if (EEP_PURE(aEEP)==EEP_PURE(pos->second->getEEProfile())) {
      // check subdevice index
      EnoceanSubDevice i = pos->second->getSubDevice();
      if (i>=aFromIndex && ((aNumIndices==0) || (i<aFromIndex+aNumIndices))) {
        toBeDeleted.push_back(pos->second);
      }
    }
  }
  // now call vanish (which will in turn remove devices from the container's list
  for (TbdList::iterator pos = toBeDeleted.begin(); pos!=toBeDeleted.end(); ++pos) {
    (*pos)->hasVanished(aForgetParams);
  }
  return toBeDeleted.size()>0; // true only if anything deleted at all
}


// MARK: ===== EnOcean specific methods


ErrorPtr EnoceanVdc::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="x-p44-addProfile") {
    // add new device (without learn-in, usually for remotecontrol-type devices or debugging)
    respErr = addProfile(aRequest, aParams);
  }
  else if (aMethod=="x-p44-simulatePacket") {
    // simulate reception of a ESP packet
    respErr = simulatePacket(aRequest, aParams);
  }
  else {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }
  return respErr;
}



ErrorPtr EnoceanVdc::addProfile(VdcApiRequestPtr aRequest, ApiValuePtr aParams)
{
  // add an EnOcean profile
  ErrorPtr respErr;
  ApiValuePtr o;
  respErr = checkParam(aParams, "eep", o); // EEP with variant in MSB
  if (Error::isOK(respErr)) {
    EnoceanProfile eep = o->uint32Value();
    respErr = checkParam(aParams, "address", o);
    if (Error::isOK(respErr)) {
      // remote device address
      // if 0xFF800000..0xFF80007F : bit0..6 = ID base offset to ID base of modem
      // if 0xFF8000FF : automatically take next unused ID base offset
      EnoceanAddress addr = o->uint32Value();
      if ((addr & 0xFFFFFF00)==0xFF800000) {
        // relative to ID base
        // - get map of already used offsets
        string usedOffsetMap;
        usedOffsetMap.assign(128,'0');
        for (EnoceanDeviceMap::iterator pos = enoceanDevices.begin(); pos!=enoceanDevices.end(); ++pos) {
          pos->second->markUsedBaseOffsets(usedOffsetMap);
        }
        addr &= 0xFF; // extract offset
        if (addr==0xFF) {
          // auto-determine offset
          for (addr=0; addr<128; addr++) {
            if (usedOffsetMap[addr]=='0') break; // free offset here
          }
          if (addr>128) {
            respErr = WebError::webErr(400, "no more free base ID offsets");
          }
        }
        else {
          if (addr>=128 || usedOffsetMap[addr]!='0') {
            respErr = WebError::webErr(400, "invalid or already used base ID offset specifier");
          }
        }
        // add-in my own ID base
        addr += enoceanComm.idBase();
      }
      // now create device(s)
      if (Error::isOK(respErr)) {
        // create devices as if this was a learn-in
        int newDevices = EnoceanDevice::createDevicesFromEEP(this, addr, eep, manufacturer_unknown, learn_none, Esp3PacketPtr(), NULL); // not a real learn, but only re-creation from DB
        if (newDevices<1) {
          respErr = WebError::webErr(400, "Unknown EEP specification, no device(s) created");
        }
        else {
          ApiValuePtr r = aRequest->newApiValue();
          r->setType(apivalue_object);
          r->add("newDevices", r->newUint64(newDevices));
          aRequest->sendResult(r);
          respErr.reset(); // make sure we don't send an extra ErrorOK
        }
      }
    }
  }
  return respErr;
}


ErrorPtr EnoceanVdc::simulatePacket(VdcApiRequestPtr aRequest, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  ApiValuePtr o;
  respErr = checkParam(aParams, "data", o); // ESP packet data, no need for matching CRCs
  if (Error::isOK(respErr)) {
    Esp3PacketPtr simPacket = Esp3PacketPtr(new Esp3Packet);
    // input string is hex bytes, optionally separated by spaces, colons or dashes
    string dataStr = o->stringValue();
    string bs = hexToBinaryString(dataStr.c_str(), true);
    // process with no CRC checks
    if (simPacket->acceptBytes(bs.size(), (const uint8_t *)bs.c_str(), true)!=bs.size()) {
      respErr = WebError::webErr(400, "Wrong number of bytes in simulated ESP3 packet data");
    }
    else {
      // process if complete
      if (simPacket->isComplete()) {
        LOG(LOG_DEBUG, "Simulated Enocean Packet:\n%s", simPacket->description().c_str());
        if (simPacket->packetType()==pt_radio_erp1) {
          handleRadioPacket(simPacket, ErrorPtr());
        }
        else if (simPacket->packetType()==pt_event_message) {
          handleEventPacket(simPacket, ErrorPtr());
        }
        // done
        respErr = Error::ok();
      }
      else {
        respErr = WebError::webErr(400, "invalid simulated ESP3 packet data");
      }
    }
  }
  return respErr;
}


// MARK: ===== Security info handling

#if ENABLE_ENOCEAN_SECURE

EnOceanSecurityPtr EnoceanVdc::securityInfoForSender(EnoceanAddress aSender, bool aCreateNew)
{
  EnoceanSecurityMap::iterator pos = securityInfos.find(aSender);
  if (pos==securityInfos.end()) {
    if (!aCreateNew) return EnOceanSecurityPtr(); // none
    // create new
    EnOceanSecurityPtr sec = EnOceanSecurityPtr(new EnOceanSecurity);
    securityInfos[aSender] = sec;
    // - link all existing devices to this security info
    for (EnoceanDeviceMap::iterator pos = enoceanDevices.lower_bound(aSender); pos!=enoceanDevices.upper_bound(aSender); ++pos) {
      pos->second->setSecurity(sec);
    }
    return sec;
  }
  return pos->second;
}


bool EnoceanVdc::dropSecurityInfoForSender(EnoceanAddress aSender)
{
  if (securityInfos.erase(aSender)>0) {
    // also delete from db
    if (db.executef("DELETE FROM secureDevices WHERE enoceanAddress=%d", aSender)!=SQLITE_OK) {
      ALOG(LOG_ERR, "Error deleting security info for device %08X: %s", aSender, db.error()->description().c_str());
      return false;
    }
    ALOG(LOG_INFO, "Deleted security info for device %08X", aSender);
  }
  return true;
}


void EnoceanVdc::removeUnusedSecurity(EnoceanDevice &aDevice)
{
  bool otherSubdevices = false;
  for (EnoceanDeviceMap::iterator pos = enoceanDevices.lower_bound(aDevice.getAddress()); pos!=enoceanDevices.upper_bound(aDevice.getAddress()); ++pos) {
    // check subdevice index
    if (pos->second!=&aDevice) {
      otherSubdevices = true;
      break;
    }
  }
  if (!otherSubdevices) {
    // this is the last subdevice for this address -> forget security info
    dropSecurityInfoForSender(aDevice.getAddress());
  }
}


void EnoceanVdc::loadSecurityInfos()
{
  securityInfos.clear();
  sqlite3pp::query qry(db);
  MLMicroSeconds now = MainLoop::now();
  if (qry.prepare("SELECT enoceanAddress, slf, rlc, key, teachInInfo FROM secureDevices")==SQLITE_OK) {
    for (sqlite3pp::query::iterator i = qry.begin(); i != qry.end(); ++i) {
      EnOceanSecurityPtr sec = EnOceanSecurityPtr(new EnOceanSecurity);
      // get info from DB
      int idx = 0;
      EnoceanAddress addr = i->get<int>(idx++);
      sec->securityLevelFormat = i->get<int>(idx++);
      sec->rollingCounter = i->get<int>(idx++);
      memcpy(sec->privateKey, i->get<const void *>(idx++), EnOceanSecurity::AES128BlockLen);
      sec->teachInInfo = i->get<int>(idx++);
      // derived values
      sec->lastSavedRLC = sec->rollingCounter; // this value is saved
      sec->lastSave = now;
      sec->deriveSubkeysFromPrivateKey();
      // store in list
      securityInfos[addr] = sec;
    }
  }
  ALOG(LOG_INFO, "loaded security info for %lu devices", securityInfos.size());
}


#define MIN_RLC_DISTANCE_FOR_SAVE 100 // a flash write every 50 clicks (press+release) seems ok

bool EnoceanVdc::saveSecurityInfo(EnOceanSecurityPtr aSecurityInfo, EnoceanAddress aEnoceanAddress, bool aRLCOnly, bool aOnlyIfNeeded)
{
  if (aOnlyIfNeeded) {
    // avoid too many saves
    uint32_t d = aSecurityInfo->rlcDistance(aSecurityInfo->rollingCounter, aSecurityInfo->lastSavedRLC);
    if (d<MIN_RLC_DISTANCE_FOR_SAVE) {
      LOG(LOG_DEBUG, "Not saving because RLC distance (%u) is not high enough", d);
      return true; // not saved, but ok
    }
  }
  if (aRLCOnly) {
    if (db.executef(
      "UPDATE secureDevices SET rlc=%d WHERE enoceanAddress=%d",
      aSecurityInfo->rollingCounter,
      aEnoceanAddress
    )!=SQLITE_OK) {
      ALOG(LOG_ERR, "Error updating RLC for device %08X: %s", aEnoceanAddress, db.error()->description().c_str());
      return false;
    }
  }
  else {
    sqlite3pp::command cmd(db);
    if (cmd.prepare("INSERT OR REPLACE INTO secureDevices (enoceanAddress, slf, rlc, key, teachInInfo) VALUES (?,?,?,?,?)")!=SQLITE_OK) {
      ALOG(LOG_ERR, "Error preparing SQL for device %08X: %s", aEnoceanAddress, db.error()->description().c_str());
      return false;
    }
    else {
      int idx = 1; // SQLite parameter indexes are 1-based!
      cmd.bind(idx++, (int)aEnoceanAddress);
      cmd.bind(idx++, aSecurityInfo->securityLevelFormat);
      cmd.bind(idx++, (int)aSecurityInfo->rollingCounter);
      cmd.bind(idx++, aSecurityInfo->privateKey, EnOceanSecurity::AES128BlockLen, true); // is static
      cmd.bind(idx++, aSecurityInfo->teachInInfo);
      if (cmd.execute()!=SQLITE_OK) {
        ALOG(LOG_ERR, "Error saving security info for device %08X: %s", aEnoceanAddress, db.error()->description().c_str());
      }
    }
  }
  // saved
  aSecurityInfo->lastSavedRLC = aSecurityInfo->rollingCounter;
  aSecurityInfo->lastSave = MainLoop::now();
  ALOG(LOG_INFO, "Saved/updated security info for device %08X", aEnoceanAddress);
  return true;
}

#else

// dummy when no real security is implemented
EnOceanSecurityPtr EnoceanVdc::securityInfoForSender(EnoceanAddress aSender, bool aCreateNew)
{
  return NULL;
}

#endif


// MARK: ===== learn and unlearn devices

#define SMART_ACK_RESPONSE_TIME (100*MilliSecond)

#define MIN_LEARN_DBM -50
// -50 = for experimental luz v1 patched bridge: within approx one meter of the TCM310
// -50 = for v2 bridge 223: very close to device, about 10-20cm
// -55 = for v2 bridge 223: within approx one meter of the TCM310

Tristate EnoceanVdc::processLearn(EnoceanAddress aDeviceAddress, EnoceanProfile aEEProfile, EnoceanManufacturer aManufacturer, Tristate aTeachInfoType, EnoceanLearnType aLearnType, Esp3PacketPtr aLearnPacket, EnOceanSecurityPtr aSecurityInfo)
{
  // no learn/unlearn actions detected so far
  // - check if we know that device address AND EEP already. If so, it is a learn-out
  bool learnIn = true;
  for (EnoceanDeviceMap::iterator pos = enoceanDevices.lower_bound(aDeviceAddress); pos!=enoceanDevices.upper_bound(aDeviceAddress); ++pos) {
    if (EEP_PURE(aEEProfile)==EEP_PURE(pos->second->getEEProfile())) {
      // device with same address and same EEP already known
      learnIn = false;
    }
  }
  if (learnIn) {
    // this is a not-yet known device, so we might be able to learn it in
    if  (onlyEstablish!=no && aTeachInfoType!=no) {
      // neither our side nor the info in the telegram insists on learn-out, so we can learn-in
      // - create devices from EEP
      int numNewDevices = EnoceanDevice::createDevicesFromEEP(this, aDeviceAddress, aEEProfile, aManufacturer, aLearnType, aLearnPacket, aSecurityInfo);
      if (numNewDevices>0) {
        // successfully learned at least one device
        // - confirm learning FIRST (before reporting end-of-learn!)
        if (aLearnType==learn_UTE) {
          enoceanComm.confirmUTE(UTE_LEARNED_IN, aLearnPacket);
        }
        else if (aLearnType==learn_smartack) {
          enoceanComm.smartAckRespondToLearn(SA_RESPONSECODE_LEARNED, SMART_ACK_RESPONSE_TIME);
        }
        // - now report learned-in, which will in turn disable smart-ack learn
        getVdcHost().reportLearnEvent(true, ErrorPtr());
        return yes; // learned in
      }
      else {
        // unknown EEP
        if (aLearnType==learn_UTE) {
          enoceanComm.confirmUTE(UTE_UNKNOWN_EEP, aLearnPacket);
        }
        else if (aLearnType==learn_smartack) {
          enoceanComm.smartAckRespondToLearn(SA_RESPONSECODE_UNKNOWNEEP);
        }
        return undefined; // nothing learned in, nothing learned out
      }
    }
  }
  else {
    // this is an already known device, so we might be able to learn it out
    if (onlyEstablish!=yes && aTeachInfoType!=yes) {
      // neither our side nor the info in the telegram insists on learn-in, so we can learn-out
      // - un-pair all logical dS devices it has represented
      //   but keep dS level config in case it is reconnected
      bool anyRemoved = unpairDevicesByAddressAndEEP(aDeviceAddress, aEEProfile, false);
      // - confirm smart ack FIRST (before reporting end-of-learn!)
      if (aLearnType==learn_UTE) {
        enoceanComm.confirmUTE(anyRemoved ? UTE_LEARNED_OUT : UTE_FAIL, aLearnPacket);
      }
      else if (aLearnType==learn_smartack) {
        enoceanComm.smartAckRespondToLearn(anyRemoved ? SA_RESPONSECODE_REMOVED : SA_RESPONSECODE_UNKNOWNEEP);
      }
      if (!anyRemoved) return undefined; // nothing learned out (or in)
      // - now report learned-out, which will in turn disable smart-ack learn
      getVdcHost().reportLearnEvent(false, ErrorPtr());
      return no; // always successful learn out
    }
  }
  // generic failure to learn in or out
  if (aLearnType==learn_UTE) {
    enoceanComm.confirmUTE(UTE_FAIL, aLearnPacket); // general failure
  }
  else if (aLearnType==learn_smartack) {
    enoceanComm.smartAckRespondToLearn(SA_RESPONSECODE_NOMEM); // use "no capacity to learn in new device"
  }
  return undefined; // nothing learned in, nothing learned out
}


void EnoceanVdc::handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr, ErrorPtr aError)
{
  EnoceanAddress sender = aEsp3PacketPtr->radioSender();
  if (aError) {
    LOG(LOG_INFO, "Radio packet error: %s", aError->description().c_str());
    return;
  }
  // suppress radio packets send by one of my secondary IDs
  if ((sender & 0xFFFFFF80) == enoceanComm.idBase()) {
    LOG(LOG_DEBUG, "Suppressed radio packet coming from one of my own base IDs: %08X", sender);
    return;
  }
  // check encrypted packets
  EnOceanSecurityPtr sec;
  RadioOrg rorg = aEsp3PacketPtr->eepRorg();
  #if ENABLE_ENOCEAN_SECURE
  if (rorg==rorg_SEC_TEACHIN) {
    LOG(LOG_NOTICE, "Secure teach-in packet received from %08X", sender);
    bool known = enoceanDevices.find(sender)!=enoceanDevices.end();
    // allow creating new security info records in learning mode or for known devices (=upgrade to secure mode)
    sec = securityInfoForSender(sender, learningMode || known);
    if (sec) {
      Tristate res = sec->processTeachInMsg(aEsp3PacketPtr, NULL); // TODO: pass in PSK once we have one
      // TODO: for bidirectional teach-in, generate and send back my own teach-in message
      if (res==yes) {
        // complete secure teach-in info found
        if ((sec->teachInInfo & 0x07)==0x01) {
          // bidirectional teach-in requested - send immediately because it must occur not later than 500mS after receiving teach-in (750mS device side timeout)
          LOG(LOG_NOTICE, "- Device %08X requests bidirectional secure teach-in, sending response now", sender);
          for (int seg=0; seg<2; seg++) {
            Esp3PacketPtr secTeachInResponse = sec->teachInMessage(seg);
            secTeachInResponse->setRadioDestination(sender);
            enoceanComm.sendPacket(secTeachInResponse);
            LOG(LOG_DEBUG, "Sent secure teach-in response segment #%d:\n%s", seg, secTeachInResponse->description().c_str());
          }
        }
        if (!learningMode) {
          // just refreshing or adding security info outside of actually adding/removing device
          LOG(LOG_NOTICE, "- Device %08X upgraded to EnOcean security or refreshed security info", sender);
          saveSecurityInfo(sec, sender, false, false);
        }
        else {
          // actual secure teach-in (or out)
          // - check type
          if ((sec->teachInInfo & 0x06)==0x04) {
            // PTM implicit teach-in (PTM: bit2=1, INFO: bit1==0, bit0==X)
            LOG(LOG_NOTICE, "- is implicit PTM learn in");
            // process as F6-02-01 dual rocker (altough the pseudo-profile is called D2-03-00)
            Tristate lrn = processLearn(sender, 0xF60201, manufacturer_unknown, undefined, learn_simple, aEsp3PacketPtr, sec);
            if (lrn!=undefined) {
              if (lrn==yes) {
                // learned in, must save security info
                saveSecurityInfo(sec, sender, false, false);
              }
              // implicit learn (in or out) done
              learningMode = false;
            }
          }
        }
      }
      else if (res==no) {
        // invalid secure teach-in, discard info
        dropSecurityInfoForSender(sender);
      }
    }
    else {
      LOG(LOG_NOTICE, "- secure teach in ignored (no known device and not in learn mode");
    }
    // no other processing for rorg_SEC_TEACHIN
    return;
  }
  else {
    // not secure teach-in, just look up from existing security infos
    sec = securityInfoForSender(sender, false);
  }
  // unwrap secure telegrams, if any
  if (sec) {
    // security context for that device exists -> only encrypted messages are allowed
    Esp3PacketPtr unpackedMsg = sec->unpackSecureMessage(aEsp3PacketPtr);
    if (!unpackedMsg) {
      LOG(LOG_NOTICE, "Ignoring invalid packet for secure device (not secure or not authenticated):\n%s", aEsp3PacketPtr->description().c_str());
      return;
    }
    LOG(LOG_INFO, "Received and unpacked secure radio packet, original is:\n%s", aEsp3PacketPtr->description().c_str());
    aEsp3PacketPtr = unpackedMsg;
    rorg = unpackedMsg->eepRorg();
    LOG(LOG_DEBUG, "Unpacked secure radio packet resulting:\n%s", aEsp3PacketPtr->description().c_str());
    // check if we need to save the security context
    saveSecurityInfo(sec, sender, true, true);
  }
  else
  #endif
  {
    // no security context for this device
    if (rorg==rorg_SEC || rorg==rorg_SEC_ENCAPS) {
      LOG(LOG_NOTICE, "Secure packet received from sender w/o security info available -> ignored:\n%s", aEsp3PacketPtr->description().c_str());
      return;
    }
  }
  // check learning mode
  if (learningMode) {
    // now add/remove the device (if the action is a valid learn/unlearn)
    // detect implicit (RPS) learn in only with sufficient radio strength (or explicit override of that check),
    // explicit ones are always recognized
    if (aEsp3PacketPtr->radioHasTeachInfo(disableProximityCheck ? 0 : MIN_LEARN_DBM, false)) {
      LOG(LOG_NOTICE, "Learn mode enabled: processing EnOcean learn packet:\n%s", aEsp3PacketPtr->description().c_str());
      EnoceanLearnType lt = aEsp3PacketPtr->eepRorg()==rorg_UTE ? learn_UTE : learn_simple;
      Tristate lrn = processLearn(sender, aEsp3PacketPtr->eepProfile(), aEsp3PacketPtr->eepManufacturer(), aEsp3PacketPtr->teachInfoType(), lt, aEsp3PacketPtr, sec);
      if (lrn!=undefined) {
        // - only allow one learn action (to prevent learning out device when
        //   button is released or other repetition of radio packet)
        learningMode = false;
      }
    } // learn action
    else {
      LOG(LOG_INFO, "Learn mode enabled: Received non-learn EnOcean packet -> ignored:\n%s", aEsp3PacketPtr->description().c_str());
    }
  }
  else {
    // not learning mode, dispatch packet to all devices known for that address
    bool reachedDevice = false;
    for (EnoceanDeviceMap::iterator pos = enoceanDevices.lower_bound(sender); pos!=enoceanDevices.upper_bound(sender); ++pos) {
      if (aEsp3PacketPtr->radioHasTeachInfo(MIN_LEARN_DBM, false) && aEsp3PacketPtr->eepRorg()!=rorg_RPS) {
        // learning packet in non-learn mode -> report as non-regular user action, might be attempt to identify a device
        // Note: RPS devices are excluded because for these all telegrams are regular user actions.
        // signalDeviceUserAction() will be called from button and binary input behaviours
        if (getVdcHost().signalDeviceUserAction(*(pos->second), false)) {
          // consumed for device identification purposes, suppress further processing
          break;
        }
      }
      // handle regularily (might be RPS switch which does not have separate learn/action packets
      pos->second->handleRadioPacket(aEsp3PacketPtr);
      reachedDevice = true;
    }
    if (!reachedDevice) {
      LOG(LOG_INFO, "Received EnOcean packet not directed to any known device -> ignored:\n%s", aEsp3PacketPtr->description().c_str());
    }
  }
}


void EnoceanVdc::handleEventPacket(Esp3PacketPtr aEsp3PacketPtr, ErrorPtr aError)
{
  if (aError) {
    LOG(LOG_INFO, "Event packet error: %s", aError->description().c_str());
    return;
  }
  uint8_t *dataP = aEsp3PacketPtr->data();
  uint8_t eventCode = dataP[0];
  if (eventCode==SA_CONFIRM_LEARN) {
    if (learningMode) {
      // process smart-ack learn
      // - extract learn data
      uint8_t postmasterFlags = dataP[1];
      EnoceanManufacturer manufacturer =
        ((EnoceanManufacturer)(dataP[2] & 0x03)<<8) +
        dataP[3];
      EnoceanProfile profile =
        ((EnoceanProfile)dataP[4]<<16) +
        ((EnoceanProfile)dataP[5]<<8) +
        dataP[6];
      int rssi = -dataP[7];
      EnoceanAddress postmasterAddress =
        ((EnoceanAddress)dataP[8]<<24) +
        ((EnoceanAddress)dataP[9]<<16) +
        ((EnoceanAddress)dataP[10]<<8) +
        dataP[11];
      EnoceanAddress deviceAddress =
        ((EnoceanAddress)dataP[12]<<24) +
        ((EnoceanAddress)dataP[13]<<16) +
        ((EnoceanAddress)dataP[14]<<8) +
        dataP[15];
      uint8_t hopCount = dataP[16];
      if (LOGENABLED(LOG_NOTICE)) {
        const char *mn = EnoceanComm::manufacturerName(manufacturer);
        LOG(LOG_NOTICE,
          "ESP3 SA_CONFIRM_LEARN, sender=0x%08X, rssi=%d, hops=%d"
          "\n- postmaster=0x%08X (priority flags = 0x%1X)"
          "\n- EEP RORG/FUNC/TYPE: %02X %02X %02X, Manufacturer = %s (%03X)",
          deviceAddress,
          rssi,
          hopCount,
          postmasterAddress,
          postmasterFlags,
          EEP_RORG(profile),
          EEP_FUNC(profile),
          EEP_TYPE(profile),
          mn ? mn : "<unknown>",
          manufacturer
        );
      }
      // try to process
      // Note: processLearn will always confirm the SA_CONFIRM_LEARN event (even if failing)
      EnOceanSecurityPtr sec = securityInfoForSender(deviceAddress, false);
      processLearn(deviceAddress, profile, manufacturer, undefined, learn_smartack, aEsp3PacketPtr, sec); // smart ack
    }
    else {
      LOG(LOG_WARNING, "Received SA_CONFIRM_LEARN while not in learning mode -> rejecting");
      enoceanComm.smartAckRespondToLearn(SA_RESPONSECODE_NOMEM);
    }
  }
  else {
    LOG(LOG_INFO, "Unknown Event code: %d", eventCode);
  }
}



void EnoceanVdc::setLearnMode(bool aEnableLearning, bool aDisableProximityCheck, Tristate aOnlyEstablish)
{
  // put normal radio packet evaluator into learn mode
  learningMode = aEnableLearning;
  disableProximityCheck = aDisableProximityCheck;
  onlyEstablish = aOnlyEstablish;
  // also enable smartAck learn mode in the EnOcean module
  enoceanComm.smartAckLearnMode(aEnableLearning, 60*Second); // actual timeout of learn is usually smaller
}


#if SELFTESTING_ENABLED

// MARK: ===== Self test

void EnoceanVdc::selfTest(StatusCB aCompletedCB)
{
  // install test packet handler
  enoceanComm.setRadioPacketHandler(boost::bind(&EnoceanVdc::handleTestRadioPacket, this, aCompletedCB, _1, _2));
}


void EnoceanVdc::handleTestRadioPacket(StatusCB aCompletedCB, Esp3PacketPtr aEsp3PacketPtr, ErrorPtr aError)
{
  // ignore packets with error
  if (Error::isOK(aError)) {
    if (aEsp3PacketPtr->eepRorg()==rorg_RPS && aEsp3PacketPtr->radioDBm()>MIN_LEARN_DBM && enoceanComm.modemAppVersion()>0) {
      // uninstall handler
      enoceanComm.setRadioPacketHandler(NULL);
      // seen both watchdog response (modem works) and independent RPS telegram (RF is ok)
      LOG(LOG_NOTICE,
        "- enocean modem info: appVersion=0x%08X, apiVersion=0x%08X, modemAddress=0x%08X, idBase=0x%08X",
        enoceanComm.modemAppVersion(), enoceanComm.modemApiVersion(), enoceanComm.modemAddress(), enoceanComm.idBase()
      );
      aCompletedCB(ErrorPtr());
      // done
      return;
    }
  }
  // - still waiting
  LOG(LOG_NOTICE, "- enocean test: still waiting for RPS telegram in learn distance");
}

#endif // SELFTESTING_ENABLED

#endif // ENABLE_ENOCEAN

