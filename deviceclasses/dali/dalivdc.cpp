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

#include "dalivdc.hpp"

#include "dalidevice.hpp"

#if ENABLE_DALI

using namespace p44;


DaliVdc::DaliVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  Vdc(aInstanceNumber, aVdcHostP, aTag),
  usedDaliScenesMask(0),
  usedDaliGroupsMask(0)
{
  daliComm = DaliCommPtr(new 	DaliComm(MainLoop::currentMainLoop()));
  #if ENABLE_DALI_INPUTS
  daliComm->setBridgeEventHandler(boost::bind(&DaliVdc::daliEventHandler, this, _1, _2, _3));
  #endif
  // set default optimisation mode
  optimizerMode = opt_disabled; // FIXME: once we are confident, make opt_auto the default
}


DaliVdc::~DaliVdc()
{
}


// vDC name
const char *DaliVdc::vdcClassIdentifier() const
{
  return "DALI_Bus_Container";
}


bool DaliVdc::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("vdc_dali", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


// MARK: ===== DB and initialisation

// Version history
//  1 : first version
//  2 : added groupNo (0..15) for DALI groups
//  3 : added support for input devices
//  4 : added dali2ScanLock to keep compatibility with old installations that might have scanned DALI 2.x devices as 1.0
#define DALI_SCHEMA_MIN_VERSION 1 // minimally supported version, anything older will be deleted
#define DALI_SCHEMA_VERSION 4 // current version

string DaliPersistence::dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion)
{
  string sql;
  // input devices table needed for fromVersion==0 and 2
  static const char *inputDevicesTable =
    "CREATE TABLE inputDevices ("
    " daliInputConfig TEXT," // the input configuration
    " daliBaseAddr INTEGER," // DALI base address (internal abstracted DaliAddress type) of input device
    " PRIMARY KEY (daliBaseAddr)"
    ");";

  if (aFromVersion==0) {
    // create DB from scratch
		// - use standard globs table for schema version
    sql = inherited::dbSchemaUpgradeSQL(aFromVersion, aToVersion);
		// - create my tables
    sql.append(
      "CREATE TABLE compositeDevices ("
      " dimmerUID TEXT,"
      " dimmerType TEXT,"
      " collectionID INTEGER," // table-unique ID for this collection
      " groupNo INTEGER," // DALI group Number (0..15), valid for dimmerType "GRP" only
      " PRIMARY KEY (dimmerUID)"
      ");"
    );
    sql.append(inputDevicesTable);
		// - add dali2ScanLock to globs table and set it to 0 (this is a fresh installation)
    sql.append(
      "ALTER TABLE globs ADD dali2ScanLock INTEGER;"
      "UPDATE globs SET dali2ScanLock=0;" // not locked
    );
    // reached final version in one step
    aToVersion = DALI_SCHEMA_VERSION;
  }
  else if (aFromVersion==1) {
    // V1->V2: groupNo added
    sql =
      "ALTER TABLE compositeDevices ADD groupNo INTEGER;";
    // reached version 2
    aToVersion = 2;
  }
  else if (aFromVersion==2) {
    // V2->V3: added support for input devices
    sql = inputDevicesTable;
    // reached version 3
    aToVersion = 3;
  }
  else if (aFromVersion==3) {
    // V3->V4: added dali2ScanLock
    sql =
      "ALTER TABLE globs ADD dali2ScanLock INTEGER;"
      "UPDATE globs SET dali2ScanLock=1;"; // this is an upgrade: LOCKED!
    // reached version 4
    aToVersion = 4;
  }
  return sql;
}


void DaliVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
	string databaseName = getPersistentDataDir();
	string_format_append(databaseName, "%s_%d.sqlite3", vdcClassIdentifier(), getInstanceNumber());
  ErrorPtr error = db.connectAndInitialize(databaseName.c_str(), DALI_SCHEMA_VERSION, DALI_SCHEMA_MIN_VERSION, aFactoryReset);
  // load dali2ScanLock
  sqlite3pp::query qry(db);
  if (qry.prepare("SELECT dali2ScanLock FROM globs")==SQLITE_OK) {
    sqlite3pp::query::iterator i = qry.begin();
    if (i!=qry.end()) {
      daliComm->dali2ScanLock = i->get<bool>(0);
    }
  }
  // update map of groups and scenes used by manually configured groups and scene-listening input devices
  loadLocallyUsedGroupsAndScenes();
  // return status of DB init
	aCompletedCB(error);
}




// MARK: ===== collect devices


int DaliVdc::getRescanModes() const
{
  // incremental, normal, exhaustive (resolving conflicts) and enumerate (clearing short addrs before scan) are available.
  return rescanmode_incremental+rescanmode_normal+rescanmode_exhaustive+rescanmode_reenumerate;
}


void DaliVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  if (!(aRescanFlags & rescanmode_incremental)) {
    removeDevices(aRescanFlags & rescanmode_clearsettings);
    // clear the cache, we want fresh info from the devices!
    deviceInfoCache.clear();
    #if ENABLE_DALI_INPUTS
    // - add the DALI input devices from config
    inputDevices.clear();
    sqlite3pp::query qry(db);
    if (qry.prepare("SELECT daliInputConfig, daliBaseAddr, rowid FROM inputDevices")==SQLITE_OK) {
      for (sqlite3pp::query::iterator i = qry.begin(); i != qry.end(); ++i) {
        DaliInputDevicePtr dev = addInputDevice(i->get<string>(0), i->get<int>(1));
        if (dev) {
          dev->daliInputDeviceRowID = i->get<int>(2);
        }
      }
    }
    #endif
  }
  if (aRescanFlags & (rescanmode_exhaustive|rescanmode_reenumerate)) {
    // user is actively risking addressing changes, so we can enable DALI 2.0 scanning from now on
    if (daliComm->dali2ScanLock) {
      daliComm->dali2ScanLock = false;
      db.execute("UPDATE globs SET dali2ScanLock=0");
    }
  }
  // wipe bus addresses
  if (aRescanFlags & rescanmode_reenumerate) {
    // first reset ALL short addresses on the bus
    LOG(LOG_WARNING, "DALI Bus short address re-enumeration requested -> all short addresses will be re-assigned now (dSUIDs might change)!");
    daliComm->daliSendDtrAndConfigCommand(DaliBroadcast, DALICMD_STORE_DTR_AS_SHORT_ADDRESS, DALIVALUE_MASK);
  }
  // start collecting, allow quick scan when not exhaustively collecting (will still use full scan when bus collisions are detected)
  // Note: only in rescanmode_exhaustive, existing short addresses might get reassigned. In all other cases, only devices with no short
  //   address at all, will be assigned a short address.
  daliComm->daliFullBusScan(boost::bind(&DaliVdc::deviceListReceived, this, aCompletedCB, _1, _2, _3), !(aRescanFlags & rescanmode_exhaustive));
}



void DaliVdc::removeLightDevices(bool aForget)
{
  DeviceVector::iterator pos = devices.begin();
  while (pos!=devices.end()) {
    DaliOutputDevicePtr dev = boost::dynamic_pointer_cast<DaliOutputDevice>(*pos);
    if (dev) {
      // inform upstream about these devices going offline now (if API connection is up at all at this time)
      dev->reportVanished();
      // now actually remove
      getVdcHost().removeDevice(dev, aForget);
      // erase from list
      pos = devices.erase(pos);
    }
    else {
      // skip non-outputs
      pos++;
    }
  }
}



// recollect devices after grouping change without scanning bus again
void DaliVdc::recollectDevices(StatusCB aCompletedCB)
{

  // remove DALI scannable output devices (but not inputs)
  removeLightDevices(false);
  // no scan needed, just use the cache
  // - create a Dali bus device for every cached devInf
  DaliBusDeviceListPtr busDevices(new DaliBusDeviceList);
  for (DaliDeviceInfoMap::iterator pos = deviceInfoCache.begin(); pos!=deviceInfoCache.end(); ++pos) {
    // create bus device
    DaliBusDevicePtr busDevice(new DaliBusDevice(*this));
    busDevice->setDeviceInfo(pos->second); // use cached device info
    // - add bus device to list
    busDevices->push_back(busDevice);
  }
  // now start processing full device info for each device (no actual query will happen, it's already in the cache)
  queryNextDev(busDevices, busDevices->begin(), aCompletedCB, ErrorPtr());
}


void DaliVdc::deviceListReceived(StatusCB aCompletedCB, DaliComm::ShortAddressListPtr aDeviceListPtr, DaliComm::ShortAddressListPtr aUnreliableDeviceListPtr, ErrorPtr aError)
{
  // check if any devices
  if (aError || aDeviceListPtr->size()==0)
    return aCompletedCB(aError); // no devices to query, completed
  // create a Dali bus device for every detected device
  DaliBusDeviceListPtr busDevices(new DaliBusDeviceList);
  for (DaliComm::ShortAddressList::iterator pos = aDeviceListPtr->begin(); pos!=aDeviceListPtr->end(); ++pos) {
    // create simple device info containing only short address
    DaliDeviceInfoPtr info = DaliDeviceInfoPtr(new DaliDeviceInfo);
    info->shortAddress = *pos; // assign short address
    info->devInfStatus = DaliDeviceInfo::devinf_needsquery;
    deviceInfoCache[*pos] = info; // put it into the cache to represent the device
    // create bus device
    DaliBusDevicePtr busDevice(new DaliBusDevice(*this));
    busDevice->setDeviceInfo(info); // assign info to bus device
    // - add bus device to list
    busDevices->push_back(busDevice);
  }
  // now start collecting full device info for each device
  queryNextDev(busDevices, busDevices->begin(), aCompletedCB, ErrorPtr());
}


void DaliVdc::queryNextDev(DaliBusDeviceListPtr aBusDevices, DaliBusDeviceList::iterator aNextDev, StatusCB aCompletedCB, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    if (aNextDev != aBusDevices->end()) {
      DaliAddress addr = (*aNextDev)->deviceInfo->shortAddress;
      // check device info cache
      DaliDeviceInfoMap::iterator pos = deviceInfoCache.find(addr);
      if (pos!=deviceInfoCache.end() && pos->second->devInfStatus!=DaliDeviceInfo::devinf_needsquery) {
        // we already have real device info for this device, or know the device does not have any
        // -> have it processed (but via mainloop to avoid stacking up recursions here)
        LOG(LOG_INFO, "Using cached device info for device at shortAddress %d", addr);
        MainLoop::currentMainLoop().executeNow(boost::bind(&DaliVdc::deviceInfoValid, this, aBusDevices, aNextDev, aCompletedCB, pos->second));
        return;
      }
      else {
        // we need to fetch it from device
        daliComm->daliReadDeviceInfo(boost::bind(&DaliVdc::deviceInfoReceived, this, aBusDevices, aNextDev, aCompletedCB, _1, _2), addr);
        return;
      }
    }
    // all done successfully, complete bus info now available in aBusDevices
    // - look for dimmers that are to be addressed as a group
    DaliBusDeviceListPtr dimmerDevices = DaliBusDeviceListPtr(new DaliBusDeviceList());
    uint16_t groupsInUse = 0; // groups in use for configured groups
    while (aBusDevices->size()>0) {
      // get first remaining
      DaliBusDevicePtr busDevice = aBusDevices->front();
      // duplicate dSUID check for devInf-based IDs (if devinf is already detected unusable here, there's no need for checking)
      if (busDevice->deviceInfo->devInfStatus>=DaliDeviceInfo::devinf_solid) {
        DsUid thisDsuid;
        #if OLD_BUGGY_CHKSUM_COMPATIBLE
        if (busDevice->deviceInfo->devInfStatus==DaliDeviceInfo::devinf_notForID) {
          // check native dsuid, not shortaddress based fallback
          busDevice->dsUidForDeviceInfoStatus(thisDsuid, DaliDeviceInfo::devinf_solid);
        }
        else
        #endif
        {
          thisDsuid = busDevice->dSUID;
        }
        bool anyDuplicates = false;
        for (DaliBusDeviceList::iterator refpos = ++aBusDevices->begin(); refpos!=aBusDevices->end(); ++refpos) {
          DsUid otherDsuid;
          #if OLD_BUGGY_CHKSUM_COMPATIBLE
          if ((*refpos)->deviceInfo->devInfStatus==DaliDeviceInfo::devinf_notForID) {
            // check native dsuid, not shortaddress based fallback
            (*refpos)->dsUidForDeviceInfoStatus(otherDsuid, DaliDeviceInfo::devinf_solid);
          }
          else
          #endif
          {
            otherDsuid = (*refpos)->dSUID;
          }
          if (thisDsuid==otherDsuid) {
            // duplicate dSUID, indicates DALI devices with invalid device info that slipped all heuristics
            LOG(LOG_ERR, "Bus devices #%d and #%d have same devinf-based dSUID -> assuming invalid device info, forcing both to short address based dSUID", busDevice->deviceInfo->shortAddress, (*refpos)->deviceInfo->shortAddress);
            // - clear all device info except short address and revert to short address derived dSUID
            (*refpos)->clearDeviceInfo();
            anyDuplicates = true; // at least one found
          }
        }
        if (anyDuplicates) {
          // consider my own info invalid as well
          busDevice->clearDeviceInfo();
        }
      }
      // check if this device is part of a DALI group
      sqlite3pp::query qry(db);
      string sql = string_format("SELECT groupNo FROM compositeDevices WHERE dimmerUID = '%s' AND dimmerType='GRP'", busDevice->dSUID.getString().c_str());
      if (qry.prepare(sql.c_str())==SQLITE_OK) {
        sqlite3pp::query::iterator i = qry.begin();
        if (i!=qry.end()) {
          // this is part of a DALI group
          int groupNo = i->get<int>(0);
          // - collect all with same group (= those that once were combined, in any order)
          sql = string_format("SELECT dimmerUID FROM compositeDevices WHERE groupNo = %d AND dimmerType='GRP'", groupNo);
          if (qry.prepare(sql.c_str())==SQLITE_OK) {
            // we know that we found at least one dimmer of this group on the bus, so we'll instantiate
            // the group (even if some dimmers might be missing)
            groupsInUse |= 1<<groupNo; // groups in use for configured groups (optimizer groups excluded! Important because single-dimmers will get removed from groups in this mask later!)
            DaliBusDeviceGroupPtr daliGroup = DaliBusDeviceGroupPtr(new DaliBusDeviceGroup(*this, groupNo));
            for (sqlite3pp::query::iterator j = qry.begin(); j != qry.end(); ++j) {
              DsUid dimmerUID(nonNullCStr(i->get<const char *>(0)));
              // see if we have this dimmer on the bus
              DaliBusDevicePtr dimmer;
              for (DaliBusDeviceList::iterator pos = aBusDevices->begin(); pos!=aBusDevices->end(); ++pos) {
                if ((*pos)->dSUID == dimmerUID) {
                  // found dimmer
                  dimmer = *pos;
                  // consumed, remove from the list
                  aBusDevices->erase(pos);
                  break;
                }
              }
              // process dimmer
              if (!dimmer) {
                // dimmer not found
                LOG(LOG_WARNING, "Missing DALI dimmer %s for DALI group %d", dimmerUID.getString().c_str(), groupNo);
                // insert dummy instead
                dimmer = DaliBusDevicePtr(new DaliBusDevice(*this));
                dimmer->isDummy = true; // disable bus access
                dimmer->dSUID = dimmerUID; // just set the dSUID we know from the DB
              }
              // add the dimmer (real or dummy)
              daliGroup->addDaliBusDevice(dimmer);
            } // for all needed dimmers
            // - derive dSUID for group
            daliGroup->deriveDsUid();
            // - add group to the list of single channel dimmer devices (groups and single devices)
            dimmerDevices->push_back(daliGroup);
          }
        } // part of group
        else {
          // definitely NOT part of group, single device dimmer
          dimmerDevices->push_back(busDevice);
          aBusDevices->remove(busDevice);
        }
      }
    }
    // initialize dimmer devices
    initializeNextDimmer(dimmerDevices, groupsInUse, dimmerDevices->begin(), aCompletedCB, ErrorPtr());
  }
  else {
    // collecting failed
    aCompletedCB(aError);
  }
}


void DaliVdc::initializeNextDimmer(DaliBusDeviceListPtr aDimmerDevices, uint16_t aGroupsInUse, DaliBusDeviceList::iterator aNextDimmer, StatusCB aCompletedCB, ErrorPtr aError)
{
  if (!Error::isOK(aError)) {
    LOG(LOG_ERR, "Error initializing dimmer: %s", aError->description().c_str());
  }
  if (aNextDimmer!=aDimmerDevices->end()) {
    // check next
    (*aNextDimmer)->initialize(boost::bind(&DaliVdc::initializeNextDimmer, this, aDimmerDevices, aGroupsInUse, ++aNextDimmer, aCompletedCB, _1), aGroupsInUse);
  }
  else {
    // done, now create dS devices from dimmers
    createDsDevices(aDimmerDevices, aCompletedCB);
  }
}




void DaliVdc::createDsDevices(DaliBusDeviceListPtr aDimmerDevices, StatusCB aCompletedCB)
{
  // - look up multi-channel composite devices
  //   If none of the devices are found on the bus, the entire composite device is considered missing
  //   If at least one device is found, non-found bus devices will be added as dummy bus devices
  DaliBusDeviceList singleDevices;
  while (aDimmerDevices->size()>0) {
    // get first remaining
    DaliBusDevicePtr busDevice = aDimmerDevices->front();
    // check if this device is part of a multi-channel composite device (but not a DALI group)
    sqlite3pp::query qry(db);
    string sql = string_format("SELECT collectionID FROM compositeDevices WHERE dimmerUID = '%s' AND dimmerType!='GRP'", busDevice->dSUID.getString().c_str());
    if (qry.prepare(sql.c_str())==SQLITE_OK) {
      sqlite3pp::query::iterator i = qry.begin();
      if (i!=qry.end()) {
        // this is part of a composite device
        int collectionID = i->get<int>(0);
        // - collect all with same collectionID (= those that once were combined, in any order)
        sql = string_format("SELECT dimmerType, dimmerUID FROM compositeDevices WHERE collectionID = %d", collectionID);
        if (qry.prepare(sql.c_str())==SQLITE_OK) {
          // we know that we found at least one dimmer of this composite on the bus, so we'll instantiate
          // a composite (even if some dimmers might be missing)
          DaliCompositeDevicePtr daliDevice = DaliCompositeDevicePtr(new DaliCompositeDevice(this));
          daliDevice->collectionID = collectionID; // remember from what collection this was created
          for (sqlite3pp::query::iterator j = qry.begin(); j != qry.end(); ++j) {
            string dimmerType = nonNullCStr(i->get<const char *>(0));
            DsUid dimmerUID(nonNullCStr(i->get<const char *>(1)));
            // see if we have this dimmer on the bus
            DaliBusDevicePtr dimmer;
            for (DaliBusDeviceList::iterator pos = aDimmerDevices->begin(); pos!=aDimmerDevices->end(); ++pos) {
              if ((*pos)->dSUID == dimmerUID) {
                // found dimmer on the bus, use it
                dimmer = *pos;
                // consumed, remove from the list
                aDimmerDevices->erase(pos);
                break;
              }
            }
            // process dimmer
            if (!dimmer) {
              // dimmer not found
              LOG(LOG_WARNING, "Missing DALI dimmer %s (type %s) for composite device", dimmerUID.getString().c_str(), dimmerType.c_str());
              // insert dummy instead
              dimmer = DaliBusDevicePtr(new DaliBusDevice(*this));
              dimmer->isDummy = true; // disable bus access
              dimmer->dSUID = dimmerUID; // just set the dSUID we know from the DB
            }
            // add the dimmer (real or dummy)
            daliDevice->addDimmer(dimmer, dimmerType);
          } // for all needed dimmers
          // - add it to our collection (if not already there)
          simpleIdentifyAndAddDevice(daliDevice);
        }
      } // part of composite multichannel device
      else {
        // definitely NOT part of composite, put into single channel dimmer list
        singleDevices.push_back(busDevice);
        aDimmerDevices->remove(busDevice);
      }
    }
  }
  // remaining devices are single channel or DT8 dimmer devices
  for (DaliBusDeviceList::iterator pos = singleDevices.begin(); pos!=singleDevices.end(); ++pos) {
    DaliBusDevicePtr daliBusDevice = *pos;
    // single-dimmer (simple or DT8) device
    DaliSingleControllerDevicePtr daliSingleControllerDevice(new DaliSingleControllerDevice(this));
    // - set daliController (gives device info to calculate dSUID)
    daliSingleControllerDevice->daliController = daliBusDevice;
    // - add it to our collection (if not already there)
    simpleIdentifyAndAddDevice(daliSingleControllerDevice);
  }
  // collecting complete
  aCompletedCB(ErrorPtr());
}


void DaliVdc::deviceInfoReceived(DaliBusDeviceListPtr aBusDevices, DaliBusDeviceList::iterator aNextDev, StatusCB aCompletedCB, DaliDeviceInfoPtr aDaliDeviceInfoPtr, ErrorPtr aError)
{
  bool missingData = aError && aError->isError(DaliCommError::domain(), DaliCommError::MissingData);
  bool badData = aError && aError->isError(DaliCommError::domain(), DaliCommError::BadData);
  if (!Error::isOK(aError) && !missingData && !badData) {
    // real fatal error, can't continue
    LOG(LOG_ERR, "Error reading device info: %s",aError->description().c_str());
    return aCompletedCB(aError);
  }
  // no error, or error but due to missing or bad data -> device exists and possibly still has ok device info
  if (missingData) { LOG(LOG_INFO, "Device at shortAddress %d is missing all or some device info data",aDaliDeviceInfoPtr->shortAddress); }
  if (badData) { LOG(LOG_INFO, "Device at shortAddress %d has bad data in at least in one info bank",aDaliDeviceInfoPtr->shortAddress); }
  // update entry in the cache
  // Note: callback always gets a deviceInfo back, possibly with devinf_none if device does not have devInf at all (or garbage)
  //   So, assigning this here will make sure no entries with devinf_needsquery will remain.
  deviceInfoCache[aDaliDeviceInfoPtr->shortAddress] = aDaliDeviceInfoPtr;
  // use device info and continue
  deviceInfoValid(aBusDevices, aNextDev, aCompletedCB, aDaliDeviceInfoPtr);
}


void DaliVdc::deviceInfoValid(DaliBusDeviceListPtr aBusDevices, DaliBusDeviceList::iterator aNextDev, StatusCB aCompletedCB, DaliDeviceInfoPtr aDaliDeviceInfoPtr)
{
  // update device info entry in dali bus device
  (*aNextDev)->setDeviceInfo(aDaliDeviceInfoPtr);
  // query hardware features
  (*aNextDev)->queryFeatureSet(boost::bind(&DaliVdc::deviceFeaturesQueried, this, aBusDevices, aNextDev, aCompletedCB));
}


void DaliVdc::deviceFeaturesQueried(DaliBusDeviceListPtr aBusDevices, DaliBusDeviceList::iterator aNextDev, StatusCB aCompletedCB)
{
  // check next
  ++aNextDev;
  queryNextDev(aBusDevices, aNextDev, aCompletedCB, ErrorPtr());
}


// MARK: ===== DALI specific methods

ErrorPtr DaliVdc::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="x-p44-groupDevices") {
    // create a composite device out of existing single-channel ones
    respErr = groupDevices(aRequest, aParams);
  }
  #if ENABLE_DALI_INPUTS
  else if (aMethod=="x-p44-addDaliInput") {
    // add a DALI based input device
    respErr = addDaliInput(aRequest, aParams);
  }
  #endif
  else if (aMethod=="x-p44-daliScan") {
    // diagnostics: scan the entire DALI bus
    respErr = daliScan(aRequest, aParams);
  }
  else if (aMethod=="x-p44-daliCmd") {
    // diagnostics: direct DALI commands
    respErr = daliCmd(aRequest, aParams);
  }
  else if (aMethod=="x-p44-daliSummary") {
    // summary: returns documentation about devices on bus, reliability, device assignments etc.
    respErr = daliSummary(aRequest, aParams);
  }
  else {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }
  return respErr;
}


// MARK: ===== DALI bus diagnostics and summary


// scan bus, return status string

ErrorPtr DaliVdc::daliScan(VdcApiRequestPtr aRequest, ApiValuePtr aParams)
{
  StringPtr result(new string);
  daliScanNext(aRequest, 0, result);
  return ErrorPtr(); // no result yet, but later when scan is done
}


void DaliVdc::daliScanNext(VdcApiRequestPtr aRequest, DaliAddress aShortAddress, StringPtr aResult)
{
  if (aShortAddress<64) {
    // scan next
    daliComm->daliSendQuery(
      aShortAddress, DALICMD_QUERY_CONTROL_GEAR,
      boost::bind(&DaliVdc::handleDaliScanResult, this, aRequest, aShortAddress, aResult, _1, _2, _3)
    );
  }
  else {
    // done
    ApiValuePtr answer = aRequest->newApiValue();
    answer->setType(apivalue_object);
    answer->add("busState", answer->newString(*aResult));
    aRequest->sendResult(answer);
  }
}


void DaliVdc::handleDaliScanResult(VdcApiRequestPtr aRequest, DaliAddress aShortAddress, StringPtr aResult, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
{
  char statusChar = '.'; // default to "nothing here"
  // plain FF without error is valid device
  if (Error::isOK(aError)) {
    if (!aNoOrTimeout) {
      // data received
      if (aResponse==0xFF)
        statusChar = '*'; // ok device
      else
        statusChar = 'C'; // possibly conflict
    }
  }
  else if (Error::isError(aError, DaliCommError::domain(), DaliCommError::DALIFrame)) {
    statusChar = 'C'; // possibly conflict
  }
  else {
    statusChar = 'E'; // real error
  }
  // add to result
  *aResult += statusChar;
  // check next
  daliScanNext(aRequest, ++aShortAddress, aResult);
}



// send single device, group or broadcast commands to bus

ErrorPtr DaliVdc::daliCmd(VdcApiRequestPtr aRequest, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  string cmd;
  ApiValuePtr p = aParams->get("bridgecmd");
  if (p) {
    // direct bridge command as 3 byte hex string
    // bb1122 (bb=bridge command, 11=first DALI, 22=second DALI
    cmd = hexToBinaryString(p->stringValue().c_str(), true, 3);
    if (cmd.size()!=3) {
      respErr = WebError::webErr(500, "bridgecmd must be 3 hex bytes");
    }
    else {
      daliComm->sendBridgeCommand(cmd[0], cmd[1], cmd[2], boost::bind(&DaliVdc::bridgeCmdSent, this, aRequest, _1, _2, _3));
    }
  }
  else {
    // abstracted commands
    respErr = checkParam(aParams, "addr", p);
    if (Error::isOK(respErr)) {
      DaliAddress shortAddress = p->int8Value();
      respErr = checkStringParam(aParams, "cmd", cmd);
      if (Error::isOK(respErr)) {
        // command
        if (cmd=="max") {
          daliComm->daliSendDirectPower(shortAddress, 0xFE);
        }
        else if (cmd=="min") {
          daliComm->daliSendDirectPower(shortAddress, 0x01);
        }
        else if (cmd=="off") {
          daliComm->daliSendDirectPower(shortAddress, 0x00);
        }
        else if (cmd=="pulse") {
          daliComm->daliSendDirectPower(shortAddress, 0xFE);
          daliComm->daliSendDirectPower(shortAddress, 0x01, NULL, 1200*MilliSecond);
        }
        else {
          respErr = WebError::webErr(500, "unknown cmd");
        }
        if (Error::isOK(respErr)) {
          // send ok
          aRequest->sendResult(ApiValuePtr());
        }
      }
    }
  }
  // done
  return respErr;
}


void DaliVdc::bridgeCmdSent(VdcApiRequestPtr aRequest, uint8_t aResp1, uint8_t aResp2, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    ApiValuePtr answer = aRequest->newApiValue();
    answer->setType(apivalue_string);
    answer->setStringValue(string_format("%02X %02X", aResp1, aResp2));
    aRequest->sendResult(answer);
  }
  else {
    aRequest->sendError(aError);
  }
}


// create summary/inventory of entire bus

ErrorPtr DaliVdc::daliSummary(VdcApiRequestPtr aRequest, ApiValuePtr aParams)
{
  ApiValuePtr p = aParams->get("addr");
  if (p) {
    // quick info about a single bus address
    ApiValuePtr singleAddrSummary = aRequest->newApiValue();
    singleAddrSummary->setType(apivalue_object);
    daliAddressSummary(p->uint8Value(), singleAddrSummary);
    aRequest->sendResult(singleAddrSummary);
  }
  else {
//    #if DEBUG
//    DaliComm::ShortAddressListPtr l = DaliComm::ShortAddressListPtr(new DaliComm::ShortAddressList);
//    for (int i=51; i<64; i++) l->push_back(i);
//    daliSummaryScanDone(aRequest, l, NULL, ErrorPtr());
//    return ErrorPtr();
//    #warning "ugly hack!!!"
//    #endif
    // want info about entire bus - do a raw bus scan to learn what devices are there
    daliComm->daliBusScan(boost::bind(&DaliVdc::daliSummaryScanDone, this, aRequest, _1, _2, _3));
  }
  return ErrorPtr(); // already sent response or callback will send response
}


void DaliVdc::daliSummaryScanDone(VdcApiRequestPtr aRequest, DaliComm::ShortAddressListPtr aShortAddressListPtr, DaliComm::ShortAddressListPtr aUnreliableShortAddressListPtr, ErrorPtr aError)
{
  if (!Error::isOK(aError)) {
    aRequest->sendError(aError);
    return;
  }
  u_int64_t listedDevices = 0;
  ApiValuePtr summary = aRequest->newApiValue();
  summary->setType(apivalue_object);
  if (aShortAddressListPtr) {
    // reliably accessible addresses
    for (DaliComm::ShortAddressList::iterator pos = aShortAddressListPtr->begin(); pos!=aShortAddressListPtr->end(); ++pos) {
      listedDevices |= (1ll<<(*pos));
      ApiValuePtr busAddrInfo = summary->newObject();
      daliAddressSummary(*pos, busAddrInfo);
      // add to summary
      summary->add(string_format("%d",*pos), busAddrInfo);
    }
  }
  if (aUnreliableShortAddressListPtr) {
    // unreliably accessible addresses, which means something is probably connected, but not usable
    for (DaliComm::ShortAddressList::iterator pos = aUnreliableShortAddressListPtr->begin(); pos!=aUnreliableShortAddressListPtr->end(); ++pos) {
      listedDevices |= (1<<(*pos));
      ApiValuePtr busAddrInfo = summary->newObject();
      busAddrInfo->add("scanStateText", busAddrInfo->newString("unreliable/conflict"));
      busAddrInfo->add("scanState", busAddrInfo->newUint64(0));
      // add to summary
      summary->add(string_format("%d",*pos), busAddrInfo);
    }
  }
  // check all other addresses to show devices that the vdc knows and expects, but are not there
  for (DaliAddress a=0; a<DALI_MAXDEVICES; ++a) {
    if (listedDevices & (1ll<<a)) continue; // already listed
    // there might still be a device knowing about that bus address
    ApiValuePtr busAddrInfo = summary->newObject();
    if (daliAddressSummary(a, busAddrInfo)) {
      // override some info
      busAddrInfo->add("scanStateText", busAddrInfo->newString("missing"));
      busAddrInfo->add("scanState", busAddrInfo->newUint64(0));
      busAddrInfo->add("opStateText", busAddrInfo->newString("missing"));
      busAddrInfo->add("opState", busAddrInfo->newUint64(0));
      // add to summary
      summary->add(string_format("%d", a), busAddrInfo);
    }
  }
  // return
  aRequest->sendResult(summary);
}


bool DaliVdc::daliAddressSummary(DaliAddress aDaliAddress, ApiValuePtr aInfo)
{
  // check for being part of a scanned device
  if (daliBusDeviceSummary(aDaliAddress, aInfo)) {
    // full info available
    aInfo->add("scanStateText", aInfo->newString("scanned"));
    aInfo->add("scanState", aInfo->newUint64(100));
    return true;
  }
  else {
    // not a scanned device
    aInfo->add("scanStateText", aInfo->newString("not yet scanned"));
    aInfo->add("scanState", aInfo->newUint64(50));
    // but we might have cached device info
    DaliDeviceInfoMap::iterator ipos = deviceInfoCache.find(aDaliAddress);
    if (ipos!=deviceInfoCache.end()) {
      return daliInfoSummary(ipos->second, aInfo);
    }
  }
  return false;
}


bool DaliVdc::daliBusDeviceSummary(DaliAddress aDaliAddress, ApiValuePtr aInfo)
{
  for (DeviceVector::iterator pos = devices.begin(); pos!=devices.end(); ++pos) {
    DaliOutputDevicePtr dev = boost::dynamic_pointer_cast<DaliOutputDevice>(*pos);
    if (dev) {
      if (dev->daliBusDeviceSummary(aDaliAddress, aInfo)) {
        // found
        return true;
      }
    }
  }
  return false;
}


bool DaliVdc::daliInfoSummary(DaliDeviceInfoPtr aDeviceInfo, ApiValuePtr aInfo)
{
  if (!aDeviceInfo) return false;
  string devInfStatus;
  switch (aDeviceInfo->devInfStatus) {
    case DaliDeviceInfo::devinf_none:
      devInfStatus = "no stable serial";
      break;
    case DaliDeviceInfo::devinf_needsquery:
      devInfStatus = "not queried yet";
      break;
    #if OLD_BUGGY_CHKSUM_COMPATIBLE
    // devinfo itself is solid, just must not be used for dSUID for backwards compatibility reasons
    case DaliDeviceInfo::devinf_notForID:
    case DaliDeviceInfo::devinf_maybe:
    #endif
    case DaliDeviceInfo::devinf_solid:
      devInfStatus = "stable serial";
      // serial
      aInfo->add("serialNo", aInfo->newUint64(aDeviceInfo->serialNo));
      if (aDeviceInfo->oem_serialNo!=0) {
        aInfo->add("OEM_serialNo", aInfo->newUint64(aDeviceInfo->oem_serialNo));
      }
      goto gtin;
    case DaliDeviceInfo::devinf_only_gtin:
      devInfStatus = "GTIN, but no serial";
    gtin:
      // GTIN
      aInfo->add("GTIN", aInfo->newUint64(aDeviceInfo->gtin));
      if (aDeviceInfo->oem_gtin!=0) {
        aInfo->add("OEM_GTIN", aInfo->newUint64(aDeviceInfo->oem_gtin));
      }
      // firmware versions
      aInfo->add("versionMajor", aInfo->newUint64(aDeviceInfo->fw_version_major));
      aInfo->add("versionMinor", aInfo->newUint64(aDeviceInfo->fw_version_minor));
      break;
  }
  aInfo->add("devInfStatus", aInfo->newString(devInfStatus));
  aInfo->add("reliableId", aInfo->newBool(aDeviceInfo->devInfStatus==DaliDeviceInfo::devinf_solid));
  return true;
}



// MARK: ===== composite device creation


ErrorPtr DaliVdc::groupDevices(VdcApiRequestPtr aRequest, ApiValuePtr aParams)
{
  // create a composite device out of existing single-channel ones
  ErrorPtr respErr;
  ApiValuePtr components;
  long long collectionID = -1;
  int groupNo = -1;
  DeviceVector groupedDevices;
  respErr = checkParam(aParams, "members", components);
  if (Error::isOK(respErr)) {
    if (components->isType(apivalue_object)) {
      components->resetKeyIteration();
      string dimmerType;
      ApiValuePtr o;
      while (components->nextKeyValue(dimmerType, o)) {
        DsUid memberUID;
        memberUID.setAsBinary(o->binaryValue());
        bool deviceFound = false;
        // search for this device
        for (DeviceVector::iterator pos = devices.begin(); pos!=devices.end(); ++pos) {
          // only non-composite DALI devices can be grouped at all
          DaliOutputDevicePtr dev = boost::dynamic_pointer_cast<DaliOutputDevice>(*pos);
          if (dev && dev->daliTechnicalType()!=dalidevice_composite && dev->getDsUid() == memberUID) {
            // found this device
            // - check type of grouping
            if (dimmerType[0]=='D') {
              // only not-yet grouped dimmers can be added to group
              if (dev->daliTechnicalType()==dalidevice_single) {
                deviceFound = true;
                // determine free group No
                if (groupNo<0) {
                  sqlite3pp::query qry(db);
                  for (groupNo=0; groupNo<16; ++groupNo) {
                    if ((usedDaliGroupsMask & (1<<groupNo))==0) {
                      // group number is free - use it
                      break;
                    }
                  }
                  if (groupNo>=16) {
                    // no more unused DALI groups, cannot group at all
                    respErr = WebError::webErr(500, "16 groups already exist, cannot create additional group");
                    goto error;
                  }
                }
                // - create DB entry for DALI group member
                markUsed(DaliGroup+groupNo, true);
                if (db.executef(
                  "INSERT OR REPLACE INTO compositeDevices (dimmerUID, dimmerType, groupNo) VALUES ('%q','GRP',%d)",
                  memberUID.getString().c_str(),
                  groupNo
                )!=SQLITE_OK) {
                  ALOG(LOG_ERR, "Error saving DALI group member: %s", db.error()->description().c_str());
                }
              }
            }
            else {
              deviceFound = true;
              // - create DB entry for member of composite device
              if (db.executef(
                "INSERT OR REPLACE INTO compositeDevices (dimmerUID, dimmerType, collectionID) VALUES ('%q','%q',%lld)",
                memberUID.getString().c_str(),
                dimmerType.c_str(),
                collectionID
              )!=SQLITE_OK) {
                ALOG(LOG_ERR, "Error saving DALI composite device member: %s", db.error()->description().c_str());
              }
              if (collectionID<0) {
                // use rowid of just inserted item as collectionID
                collectionID = db.last_insert_rowid();
                // - update already inserted first record
                if (db.executef(
                  "UPDATE compositeDevices SET collectionID=%lld WHERE ROWID=%lld",
                  collectionID,
                  collectionID
                )!=SQLITE_OK) {
                  ALOG(LOG_ERR, "Error updating DALI composite device: %s", db.error()->description().c_str());
                }
              }
            }
            // remember
            groupedDevices.push_back(dev);
            // done
            break;
          }
        }
        if (!deviceFound) {
          respErr = WebError::webErr(404, "some devices of the group could not be found");
          break;
        }
      }
    error:
      if (Error::isOK(respErr) && groupedDevices.size()>0) {
        // all components inserted into DB
        // - remove individual devices that will become part of a DALI group or composite device now
        for (DeviceVector::iterator pos = groupedDevices.begin(); pos!=groupedDevices.end(); ++pos) {
          (*pos)->hasVanished(false); // vanish, but keep settings
        }
        // - re-collect devices to find groups and composites now, but only after a second, starting from main loop, not from here
        StatusCB cb = boost::bind(&DaliVdc::groupCollected, this, aRequest);
        recollectDelayTicket.executeOnce(boost::bind(&DaliVdc::recollectDevices, this, cb), 1*Second);
      }
    }
  }
  return respErr;
}


ErrorPtr DaliVdc::ungroupDevice(DaliOutputDevicePtr aDevice, VdcApiRequestPtr aRequest)
{
  ErrorPtr respErr;
  if (aDevice->daliTechnicalType()==dalidevice_composite) {
    // composite device, delete grouping
    DaliCompositeDevicePtr dev = boost::dynamic_pointer_cast<DaliCompositeDevice>(aDevice);
    if (dev) {
      if(db.executef(
        "DELETE FROM compositeDevices WHERE dimmerType!='GRP' AND collectionID=%ld",
        (long)dev->collectionID
      )!=SQLITE_OK) {
        ALOG(LOG_ERR, "Error deleting DALI composite device: %s", db.error()->description().c_str());
      }
    }
  }
  else if (aDevice->daliTechnicalType()==dalidevice_group) {
    // group device, delete grouping
    DaliSingleControllerDevicePtr dev = boost::dynamic_pointer_cast<DaliSingleControllerDevice>(aDevice);
    if (dev) {
      int groupNo = dev->daliController->deviceInfo->shortAddress & DaliGroupMask;
      markUsed(DaliGroup+groupNo, false);
      if(db.executef(
        "DELETE FROM compositeDevices WHERE dimmerType='GRP' AND groupNo=%d",
        groupNo
      )!=SQLITE_OK) {
        ALOG(LOG_ERR, "Error deleting DALI group: %s", db.error()->description().c_str());
      }
    }
  }
  else {
    // error, nothing done, just return error immediately
    return WebError::webErr(500, "device is not grouped, cannot be ungrouped");
  }
  // ungrouped a device
  // - delete the previously grouped dS device
  aDevice->hasVanished(true); // delete parameters
  // - re-collect devices to find groups and composites now, but only after a second, starting from main loop, not from here
  StatusCB cb = boost::bind(&DaliVdc::groupCollected, this, aRequest);
  recollectDelayTicket.executeOnce(boost::bind(&DaliVdc::recollectDevices, this, cb), 1*Second);
  return respErr;
}




void DaliVdc::groupCollected(VdcApiRequestPtr aRequest)
{
  // devices re-collected, return ok (empty response)
  aRequest->sendResult(ApiValuePtr());
}


// MARK: ===== management of used groups and scenes

void DaliVdc::markUsed(DaliAddress aSceneOrGroup, bool aUsed)
{
  if ((aSceneOrGroup&DaliAddressTypeMask)==DaliScene) {
    uint16_t m = 1<<(aSceneOrGroup & DaliSceneMask);
    if (aUsed) usedDaliScenesMask |= m; else usedDaliScenesMask &= ~m;
  }
  else if ((aSceneOrGroup&DaliAddressTypeMask)==DaliGroup) {
    uint16_t m = 1<<(aSceneOrGroup & DaliGroupMask);
    if (aUsed) usedDaliGroupsMask |= m; else usedDaliGroupsMask &= ~m;
  }
}


void DaliVdc::loadLocallyUsedGroupsAndScenes()
{
  usedDaliGroupsMask = 0;
  usedDaliScenesMask = 0;
  sqlite3pp::query qry(db);
  if (qry.prepare("SELECT DISTINCT groupNo FROM compositeDevices WHERE dimmerType='GRP'")==SQLITE_OK) {
    for (sqlite3pp::query::iterator i = qry.begin(); i!=qry.end(); ++i) {
      // this is a DALI group in use
      markUsed(DaliGroup+i->get<int>(0), true);
    }
  }
  #if ENABLE_DALI_INPUTS
  if (qry.prepare("SELECT DISTINCT daliBaseAddr FROM inputDevices")==SQLITE_OK) {
    for (sqlite3pp::query::iterator i = qry.begin(); i!=qry.end(); ++i) {
      markUsed(i->get<int>(0), true); // mark scenes and groups
    }
  }
  #endif
}




// MARK: ===== Native actions (groups and scenes on vDC level)

static DaliAddress daliAddressFromActionId(const string aNativeActionId)
{
  int no = -1;
  if (sscanf(aNativeActionId.c_str(), "DALI_scene_%d", &no)==1) {
    // native scene
    return DaliScene+(no & DaliSceneMask);
  }
  else if (sscanf(aNativeActionId.c_str(), "DALI_group_%d", &no)==1) {
    // native group
    return DaliGroup+(no & DaliGroupMask);
  }
  return NoDaliAddress; // no valid action ID
}


static string actionIdFromDaliAddress(DaliAddress aDaliAddress)
{
  if ((aDaliAddress&DaliAddressTypeMask)==DaliScene) {
    return string_format("DALI_scene_%d", aDaliAddress & DaliSceneMask);
  }
  else if ((aDaliAddress&DaliAddressTypeMask)==DaliGroup) {
    return string_format("DALI_group_%d", aDaliAddress & DaliGroupMask);
  }
  return "";
}


ErrorPtr DaliVdc::announceNativeAction(const string aNativeActionId)
{
  DaliAddress a = daliAddressFromActionId(aNativeActionId);
  markUsed(a, true);
  return ErrorPtr();
}


void DaliVdc::callNativeAction(StatusCB aStatusCB, const string aNativeActionId, NotificationDeliveryStatePtr aDeliveryState)
{
  DaliAddress a = daliAddressFromActionId(aNativeActionId);
  if (a!=NoDaliAddress) {
    if (aDeliveryState->optimizedType==ntfy_callscene) {
      groupDimTicket.cancel(); // just safety, should be cancelled already
      // set fade time according to scene transition time (usually: already ok, so no time wasted)
      // note: dalicomm will make sure the fade time adjustments are sent before the scene call
      for (DeviceList::iterator pos = aDeliveryState->affectedDevices.begin(); pos!=aDeliveryState->affectedDevices.end(); ++pos) {
        DaliSingleControllerDevicePtr dev = boost::dynamic_pointer_cast<DaliSingleControllerDevice>(*pos);
        if (dev && dev->daliController) {
          LightBehaviourPtr l = dev->getOutput<LightBehaviour>();
          if (l) {
            dev->daliController->setTransitionTime(l->transitionTimeToNewBrightness());
          }
        }
      }
      // Broadcast scene call: DALICMD_GO_TO_SCENE
      daliComm->daliSendCommand(DaliBroadcast, DALICMD_GO_TO_SCENE+(a&DaliSceneMask), boost::bind(&DaliVdc::nativeActionDone, this, aStatusCB, _1));
      return;
    }
    else if (aDeliveryState->optimizedType==ntfy_dimchannel) {
      // Dim group
      // - get mode
      VdcDimMode dm = (VdcDimMode)aDeliveryState->actionVariant;
      ALOG(LOG_INFO,
        "optimized group dimming (DALI): 'brightness' %s",
        dm==dimmode_stop ? "STOPS dimming" : (dm==dimmode_up ? "starts dimming UP" : "starts dimming DOWN")
      );
      // - prepare dimming in all affected devices, i.e. check fade rate (usually: already ok, so no time wasted)
      // note: we let all devices do this in parallel, continue when last device reports done
      aDeliveryState->pendingCount = aDeliveryState->affectedDevices.size(); // must be set before calling executePreparedOperation() the first time
      for (DeviceList::iterator pos = aDeliveryState->affectedDevices.begin(); pos!=aDeliveryState->affectedDevices.end(); ++pos) {
        DaliSingleControllerDevicePtr dev = boost::dynamic_pointer_cast<DaliSingleControllerDevice>(*pos);
        if (dev && dev->daliController) {
          // prepare
          dev->daliController->dimPrepare(dm, dev->getChannelByType(channeltype_brightness)->getDimPerMS(), boost::bind(&DaliVdc::groupDimPrepared, this, aStatusCB, a, aDeliveryState, _1));
        }
      }
      return;
    }
  }
  aStatusCB(TextError::err("Native action '%s' (DaliAddress 0x%02X) not supported", aNativeActionId.c_str(), a)); // causes normal execution
}


void DaliVdc::groupDimPrepared(StatusCB aStatusCB, DaliAddress aDaliAddress, NotificationDeliveryStatePtr aDeliveryState, ErrorPtr aError)
{
  if (!Error::isOK(aError)) {
    ALOG(LOG_WARNING, "Error while preparing device for group dimming: %s", aError->description().c_str());
  }
  if (--aDeliveryState->pendingCount>0) {
    AFOCUSLOG("waiting for all affected devices to confirm dim preparation: %d/%d remaining", aDeliveryState->pendingCount, aDeliveryState->affectedDevices.size());
    return; // not all confirmed yet
  }
  // now issue dimming command to group
  VdcDimMode dm = (VdcDimMode)aDeliveryState->actionVariant;
  if (dm==dimmode_stop) {
    // stop dimming
    // - cancel repeater ticket
    groupDimTicket.cancel();
    // - send MASK to group
    daliComm->daliSendDirectPower(aDaliAddress, DALIVALUE_MASK, boost::bind(&DaliVdc::nativeActionDone, this, aStatusCB, _1));
    return;
  }
  else {
    // start dimming right now
    groupDimTicket.executeOnce(boost::bind(&DaliVdc::groupDimRepeater, this, aDaliAddress, dm==dimmode_up ? DALICMD_UP : DALICMD_DOWN, _1));
    // confirm action
    nativeActionDone(aStatusCB, aError);
    return;
  }
}


void DaliVdc::groupDimRepeater(DaliAddress aDaliAddress, uint8_t aCommand, MLTimer &aTimer)
{
  daliComm->daliSendCommand(aDaliAddress, aCommand);
  MainLoop::currentMainLoop().retriggerTimer(aTimer, 200*MilliSecond);
}


void DaliVdc::nativeActionDone(StatusCB aStatusCB, ErrorPtr aError)
{
  AFOCUSLOG("DALI Native action done with status: %s", Error::text(aError).c_str());
  if (aStatusCB) aStatusCB(aError);
}




void DaliVdc::createNativeAction(StatusCB aStatusCB, OptimizerEntryPtr aOptimizerEntry, NotificationDeliveryStatePtr aDeliveryState)
{
  ErrorPtr err;
  DaliAddress a = NoDaliAddress;
  if (aOptimizerEntry->type==ntfy_callscene) {
    // need a free scene
    for (int s=0; s<16; s++) {
      if ((usedDaliScenesMask & (1<<s))==0) {
        a = DaliScene + s;
        break;
      }
    }
  }
  else if (aOptimizerEntry->type==ntfy_dimchannel) {
    // need a free group
    for (int g=0; g<16; g++) {
      if ((usedDaliGroupsMask & (1<<g))==0) {
        a = DaliGroup + g;
        break;
      }
    }
  }
  else {
    err = TextError::err("cannot create new DALI native action for type=%d", (int)aOptimizerEntry->type);
  }
  if (a==NoDaliAddress) {
    err = Error::err<VdcError>(VdcError::NoMoreActions, "DALI: no free scene or group available");
  }
  else {
    markUsed(a, true);
    aOptimizerEntry->nativeActionId = actionIdFromDaliAddress(a);
    aOptimizerEntry->lastNativeChange = MainLoop::now();
    ALOG(LOG_INFO,"creating action '%s' (DaliAddress=0x%02X)", aOptimizerEntry->nativeActionId.c_str(), a);
    if (aDeliveryState->optimizedType==ntfy_callscene) {
      // make sure no old scene settings remain in any device -> broadcast DALICMD_REMOVE_FROM_SCENE
      daliComm->daliSendConfigCommand(DaliBroadcast, DALICMD_REMOVE_FROM_SCENE+(a&DaliSceneMask));
      // now update this scene's values
      updateNativeAction(aStatusCB, aOptimizerEntry, aDeliveryState);
      return;
    }
    else if (aDeliveryState->optimizedType==ntfy_dimchannel) {
      // Make sure no old group settings remain -> broadcast DALICMD_REMOVE_FROM_GROUP
      daliComm->daliSendConfigCommand(DaliBroadcast, DALICMD_REMOVE_FROM_GROUP+(a&DaliGroupMask));
      // now create new group -> for each affected device sent DALICMD_ADD_TO_GROUP
      for (DeviceList::iterator pos = aDeliveryState->affectedDevices.begin(); pos!=aDeliveryState->affectedDevices.end(); ++pos) {
        DaliSingleControllerDevicePtr dev = boost::dynamic_pointer_cast<DaliSingleControllerDevice>(*pos);
        if (dev && dev->daliController) {
          daliComm->daliSendConfigCommand(dev->daliController->deviceInfo->shortAddress, DALICMD_ADD_TO_GROUP+(a&DaliGroupMask));
        }
      }
    }
    aOptimizerEntry->lastNativeChange = MainLoop::now();
  }
  aStatusCB(err);
}


void DaliVdc::updateNativeAction(StatusCB aStatusCB, OptimizerEntryPtr aOptimizerEntry, NotificationDeliveryStatePtr aDeliveryState)
{
  DaliAddress a = daliAddressFromActionId(aOptimizerEntry->nativeActionId);
  if ((a&DaliScene) && aDeliveryState->optimizedType==ntfy_callscene) {
    // now store scene values -> for each affected device send DALICMD_STORE_DTR_AS_SCENE
    // Note: we can do this immediately even if transitions might be running, because we store the locally known scene values
    for (DeviceList::iterator pos = aDeliveryState->affectedDevices.begin(); pos!=aDeliveryState->affectedDevices.end(); ++pos) {
      DaliSingleControllerDevicePtr dev = boost::dynamic_pointer_cast<DaliSingleControllerDevice>(*pos);
      if (dev && dev->daliController) {
        LightBehaviourPtr l = dev->getOutput<LightBehaviour>();
        if (l) {
          uint8_t power = dev->daliController->brightnessToArcpower(l->brightnessForHardware(true)); // non-transitional, final
          daliComm->daliSendDtrAndConfigCommand(dev->daliController->deviceInfo->shortAddress, DALICMD_STORE_DTR_AS_SCENE+(a&DaliSceneMask), power);
        }
      }
    }
    // done
    ALOG(LOG_INFO,"updated DALI scene #%d", a&DaliSceneMask);
    aOptimizerEntry->lastNativeChange = MainLoop::now();
    aStatusCB(ErrorPtr());
    return;
  }
  aStatusCB(TextError::err("cannot update DALI native action for type=%d", (int)aOptimizerEntry->type));
}


ErrorPtr DaliVdc::freeNativeAction(const string aNativeActionId)
{
  DaliAddress a = daliAddressFromActionId(aNativeActionId);
  markUsed(a, false);
  // Nothing more to do here, keep group or scene as-is, will not be called until re-used
  return ErrorPtr();
}




// MARK: ===== Self test

void DaliVdc::selfTest(StatusCB aCompletedCB)
{
  // do bus short address scan
  daliComm->daliBusScan(boost::bind(&DaliVdc::testScanDone, this, aCompletedCB, _1, _2, _3));
}


void DaliVdc::testScanDone(StatusCB aCompletedCB, DaliComm::ShortAddressListPtr aShortAddressListPtr, DaliComm::ShortAddressListPtr aUnreliableShortAddressListPtr, ErrorPtr aError)
{
  if (Error::isOK(aError) && aShortAddressListPtr && aShortAddressListPtr->size()>0) {
    // found at least one device, do a R/W test using the DTR
    DaliAddress testAddr = aShortAddressListPtr->front();
    LOG(LOG_NOTICE, "- DALI self test: switch all lights on, then do R/W tests with DTR of device short address %d",testAddr);
    daliComm->daliSendDirectPower(DaliBroadcast, 0, NULL); // off
    daliComm->daliSendDirectPower(DaliBroadcast, 254, NULL, 2*Second); // max
    testRW(aCompletedCB, testAddr, 0x55); // use first found device
  }
  else {
    // return error
    if (Error::isOK(aError)) aError = ErrorPtr(new DaliCommError(DaliCommError::DeviceSearch)); // no devices is also an error
    aCompletedCB(aError);
  }
}


void DaliVdc::testRW(StatusCB aCompletedCB, DaliAddress aShortAddr, uint8_t aTestByte)
{
  // set DTR
  daliComm->daliSend(DALICMD_SET_DTR, aTestByte);
  // query DTR again, with 200mS delay
  daliComm->daliSendQuery(aShortAddr, DALICMD_QUERY_CONTENT_DTR, boost::bind(&DaliVdc::testRWResponse, this, aCompletedCB, aShortAddr, aTestByte, _1, _2, _3), 200*MilliSecond);
}


void DaliVdc::testRWResponse(StatusCB aCompletedCB, DaliAddress aShortAddr, uint8_t aTestByte, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
{
  if (Error::isOK(aError) && !aNoOrTimeout && aResponse==aTestByte) {
    LOG(LOG_NOTICE, "  - sent 0x%02X, received 0x%02X, noOrTimeout=%d",aTestByte, aResponse, aNoOrTimeout);
    // successfully read back same value from DTR as sent before
    // - check if more tests
    switch (aTestByte) {
      case 0x55: aTestByte = 0xAA; break; // next test: inverse
      case 0xAA: aTestByte = 0x00; break; // next test: all 0
      case 0x00: aTestByte = 0xFF; break; // next test: all 1
      case 0xFF: aTestByte = 0xF0; break; // next test: half / half
      case 0xF0: aTestByte = 0x0F; break; // next test: half / half inverse
      default:
        // all tests done
        aCompletedCB(aError);
        // turn off lights
        daliComm->daliSendDirectPower(DaliBroadcast, 0); // off
        return;
    }
    // launch next test
    testRW(aCompletedCB, aShortAddr, aTestByte);
  }
  else {
    // not ok
    if (Error::isOK(aError) && aNoOrTimeout) aError = ErrorPtr(new DaliCommError(DaliCommError::MissingData));
    // report
    LOG(LOG_ERR, "DALI self test error: sent 0x%02X, error: %s",aTestByte, aError->description().c_str());
    aCompletedCB(aError);
  }
}

#if ENABLE_DALI_INPUTS

// MARK: ===== DALI input devices

DaliInputDevicePtr DaliVdc::addInputDevice(const string aConfig, DaliAddress aDaliBaseAddress)
{
  DaliInputDevicePtr newDev = DaliInputDevicePtr(new DaliInputDevice(this, aConfig, aDaliBaseAddress));
  // add to container if device was created
  if (newDev) {
    markUsed(aDaliBaseAddress, true); // mark scene or group used
    // add to container
    simpleIdentifyAndAddDevice(newDev);
  }
  return newDev;
}


ErrorPtr DaliVdc::addDaliInput(VdcApiRequestPtr aRequest, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  // add a new static device
  string deviceConfig;
  DaliAddress baseAddress;
  respErr = checkStringParam(aParams, "deviceConfig", deviceConfig);
  if (Error::isOK(respErr)) {
    ApiValuePtr o;
    respErr = checkParam(aParams, "daliAddress", o);
    if (Error::isOK(respErr)) {
      baseAddress = o->uint8Value();
      // optional name
      string name; // default to config
      checkStringParam(aParams, "name", name);
      // try to create device
      DaliInputDevicePtr dev = addInputDevice(deviceConfig, baseAddress);
      if (!dev) {
        respErr = WebError::webErr(500, "invalid configuration for DALI input device -> none created");
      }
      else {
        // set name
        if (name.size()>0) dev->setName(name);
        // insert into database
        if(db.executef(
          "INSERT OR REPLACE INTO inputDevices (daliInputConfig, daliBaseAddr) VALUES ('%q', %d)",
          deviceConfig.c_str(), baseAddress
        )!=SQLITE_OK) {
          respErr = db.error("saving DALI input device params");
        }
        else {
          dev->daliInputDeviceRowID = db.last_insert_rowid();
          // confirm
          ApiValuePtr r = aRequest->newApiValue();
          r->setType(apivalue_object);
          r->add("dSUID", r->newBinary(dev->dSUID.getBinary()));
          r->add("rowid", r->newUint64(dev->daliInputDeviceRowID));
          r->add("name", r->newString(dev->getName()));
          aRequest->sendResult(r);
          respErr.reset(); // make sure we don't send an extra ErrorOK
        }
      }
    }
  }
  return respErr;
}


void DaliVdc::daliEventHandler(uint8_t aEvent, uint8_t aData1, uint8_t aData2)
{
  for(DeviceVector::iterator pos = devices.begin(); pos!=devices.end(); ++pos) {
    DaliInputDevicePtr inputDev = boost::dynamic_pointer_cast<DaliInputDevice>(*pos);
    if (inputDev) {
      if (inputDev->checkDaliEvent(aEvent, aData1, aData2))
        break; // event consumed
    }
  }
}



#endif // ENABLE_DALI_INPUTS


#endif // ENABLE_DALI

