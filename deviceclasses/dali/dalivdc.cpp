//
//  Copyright (c) 2013-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
  mUsedDaliScenesMask(0),
  mUsedDaliGroupsMask(0),
  mDaliComm(MainLoop::currentMainLoop())
{
  mDaliComm.isMemberVariable();
  #if ENABLE_DALI_INPUTS
  mDaliComm.setBridgeEventHandler(boost::bind(&DaliVdc::daliEventHandler, this, _1, _2, _3));
  #endif
  // set default optimisation mode
  optimizerMode = opt_disabled; // FIXME: once we are confident, make opt_auto the default
  maxOptimizerScenes = 16; // dummy, not really checked as HW limits this
  maxOptimizerGroups = 16; // dummy, not really checked as HW limits this
}


DaliVdc::~DaliVdc()
{
}


void DaliVdc::setLogLevelOffset(int aLogLevelOffset)
{
  mDaliComm.setLogLevelOffset(aLogLevelOffset);
  inherited::setLogLevelOffset(aLogLevelOffset);
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


// MARK: - DB and initialisation

// Version history
//  1 : first version
//  2 : added groupNo (0..15) for DALI groups
//  3 : added support for input devices
//  4 : added dali2ScanLock to keep compatibility with old installations that might have scanned DALI 2.x devices as 1.0
//  5 : extended dali2ScanLock to also use bit1 as dali2LUNLock
#define DALI_SCHEMA_MIN_VERSION 1 // minimally supported version, anything older will be deleted
#define DALI_SCHEMA_VERSION 5 // current version

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
      "UPDATE globs SET dali2ScanLock=0;" // nothing locked
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
  else if (aFromVersion==4) {
    // V4->V5: extended dali2ScanLock for dali2LUNLock
    sql =
      "UPDATE globs SET dali2ScanLock=dali2ScanLock | 2;"; // this is an upgrade: lock DALI2 LUN support
    // reached version 5
    aToVersion = 5;
  }
  return sql;
}


void DaliVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
	string databaseName = getPersistentDataDir();
	string_format_append(databaseName, "%s_%d.sqlite3", vdcClassIdentifier(), getInstanceNumber());
  ErrorPtr error = mDb.connectAndInitialize(databaseName.c_str(), DALI_SCHEMA_VERSION, DALI_SCHEMA_MIN_VERSION, aFactoryReset);
  // load dali2ScanLock
  sqlite3pp::query qry(mDb);
  if (qry.prepare("SELECT dali2ScanLock FROM globs")==SQLITE_OK) {
    sqlite3pp::query::iterator i = qry.begin();
    if (i!=qry.end()) {
      // dali2ScanLock DB field contains dali2ScanLock flag in bit 0 and dali2LUNLock in bit 1
      int lockFlags = i->get<int>(0);
      mDaliComm.mDali2ScanLock = lockFlags & 0x01;
      mDaliComm.mDali2LUNLock = lockFlags & 0x02;
    }
  }
  // update map of groups and scenes used by manually configured groups and scene-listening input devices
  reserveLocallyUsedGroupsAndScenes();
  // return status of DB init
	aCompletedCB(error);
}




// MARK: - collect devices


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
    mDeviceInfoCache.clear();
    #if ENABLE_DALI_INPUTS
    // - add the DALI input devices from config
    mInputDevices.clear();
    sqlite3pp::query qry(mDb);
    if (qry.prepare("SELECT daliInputConfig, daliBaseAddr, rowid FROM inputDevices")==SQLITE_OK) {
      for (sqlite3pp::query::iterator i = qry.begin(); i != qry.end(); ++i) {
        DaliInputDevicePtr dev = addInputDevice(i->get<string>(0), i->get<int>(1));
        if (dev) {
          dev->mDaliInputDeviceRowID = i->get<int>(2);
        }
      }
    }
    #endif
  }
  if (aRescanFlags & (rescanmode_exhaustive|rescanmode_reenumerate)) {
    // user is actively risking addressing changes, so we can enable DALI 2.0 scanning and LUN usage from now on
    if (mDaliComm.mDali2ScanLock || mDaliComm.mDali2LUNLock) {
      mDaliComm.mDali2ScanLock = false;
      mDaliComm.mDali2LUNLock = false;
      mDb.execute("UPDATE globs SET dali2ScanLock=0"); // clear DALI2.0 scan lock and LUN lock
    }
  }
  // wipe bus addresses
  if (aRescanFlags & rescanmode_reenumerate) {
    // first reset ALL short addresses on the bus
    LOG(LOG_WARNING, "DALI Bus short address re-enumeration requested -> all short addresses will be re-assigned now (dSUIDs might change)!");
    mDaliComm.daliSendDtrAndConfigCommand(DaliBroadcast, DALICMD_STORE_DTR_AS_SHORT_ADDRESS, DALIVALUE_MASK);
  }
  // start collecting, allow quick scan when not exhaustively collecting (will still use full scan when bus collisions are detected)
  // Note: only in rescanmode_exhaustive, existing short addresses might get reassigned. In all other cases, only devices with no short
  //   address at all, will be assigned a short address.
  mDaliComm.daliFullBusScan(boost::bind(&DaliVdc::deviceListReceived, this, aCompletedCB, _1, _2, _3), !(aRescanFlags & rescanmode_exhaustive));
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
  for (DaliDeviceInfoMap::iterator pos = mDeviceInfoCache.begin(); pos!=mDeviceInfoCache.end(); ++pos) {
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
  setVdcError(aError); // even if not fatal, pass errors
  // check if any devices
  if (aDeviceListPtr->size()==0) {
    return aCompletedCB(aError); // just no devices to query, nothing more to do no matter if error or not
  }
  // there are some devices
  if (aError) {
    // but we also had an error
    LOG(LOG_WARNING, "DALI bus scan with some problems, but using found OK devices. Full bus scan recommended! - %s", aError->text());
  }
  // create a Dali bus device for every detected device
  DaliBusDeviceListPtr busDevices(new DaliBusDeviceList);
  for (DaliComm::ShortAddressList::iterator pos = aDeviceListPtr->begin(); pos!=aDeviceListPtr->end(); ++pos) {
    // create simple device info containing only short address
    DaliDeviceInfoPtr info = DaliDeviceInfoPtr(new DaliDeviceInfo);
    info->mShortAddress = *pos; // assign short address
    info->mDevInfStatus = DaliDeviceInfo::devinf_needsquery;
    mDeviceInfoCache[*pos] = info; // put it into the cache to represent the device
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
      DaliAddress addr = (*aNextDev)->mDeviceInfo->mShortAddress;
      // check device info cache
      DaliDeviceInfoMap::iterator pos = mDeviceInfoCache.find(addr);
      if (pos!=mDeviceInfoCache.end() && pos->second->mDevInfStatus!=DaliDeviceInfo::devinf_needsquery) {
        // we already have real device info for this device, or know the device does not have any
        // -> have it processed (but via mainloop to avoid stacking up recursions here)
        LOG(LOG_INFO, "Using cached device info for device at shortAddress %d", addr);
        MainLoop::currentMainLoop().executeNow(boost::bind(&DaliVdc::deviceInfoValid, this, aBusDevices, aNextDev, aCompletedCB, pos->second));
        return;
      }
      else {
        // we need to fetch it from device
        mDaliComm.daliReadDeviceInfo(boost::bind(&DaliVdc::deviceInfoReceived, this, aBusDevices, aNextDev, aCompletedCB, _1, _2), addr);
        return;
      }
    }
    // all devices queried successfully, complete bus info now available in aBusDevices
    // - BEFORE looking up any dSUID-based grouping, check for possible devinf-based dSUID duplicates
    //   and apply fallbacks to shortaddress based dSUIDs
    for (DaliBusDeviceList::iterator busdevpos = aBusDevices->begin(); busdevpos!=aBusDevices->end(); ++busdevpos) {
      // duplicate dSUID check for devInf-based IDs (if devinf is already detected unusable here, there's no need for checking)
      if ((*busdevpos)->mDeviceInfo->mDevInfStatus>=DaliDeviceInfo::devinf_solid) {
        DsUid thisDsuid;
        #if OLD_BUGGY_CHKSUM_COMPATIBLE
        if ((*busdevpos)->deviceInfo->devInfStatus==DaliDeviceInfo::devinf_notForID) {
          // check native dsuid, not shortaddress based fallback
          (*busdevpos)->dsUidForDeviceInfoStatus(thisDsuid, DaliDeviceInfo::devinf_solid);
        }
        else
        #endif
        {
          thisDsuid = (*busdevpos)->mDSUID;
        }
        bool anyDuplicates = false;
        // compare this busdevices with all following ones (previous ones are already checked)
        DaliBusDeviceList::iterator refpos = busdevpos;
        for (refpos++; refpos!=aBusDevices->end(); ++refpos) {
          DsUid otherDsuid;
          #if OLD_BUGGY_CHKSUM_COMPATIBLE
          if ((*refpos)->deviceInfo->devInfStatus==DaliDeviceInfo::devinf_notForID) {
            // check native dsuid, not shortaddress based fallback
            (*refpos)->dsUidForDeviceInfoStatus(otherDsuid, DaliDeviceInfo::devinf_solid);
          }
          else
          #endif
          {
            otherDsuid = (*refpos)->mDSUID;
          }
          if (thisDsuid==otherDsuid) {
            // duplicate dSUID, indicates DALI devices with invalid device info that slipped all heuristics
            LOG(LOG_ERR, "Bus devices #%d and #%d have same devinf-based dSUID -> assuming invalid device info, forcing both to short address based dSUID", (*busdevpos)->mDeviceInfo->mShortAddress, (*refpos)->mDeviceInfo->mShortAddress);
            LOG(LOG_NOTICE, "- device #%d claims to have GTIN=%llu and Serial=%llu", (*busdevpos)->mDeviceInfo->mShortAddress, (*busdevpos)->mDeviceInfo->mGtin, (*busdevpos)->mDeviceInfo->mSerialNo);
            LOG(LOG_NOTICE, "- device #%d claims to have GTIN=%llu and Serial=%llu", (*refpos)->mDeviceInfo->mShortAddress, (*refpos)->mDeviceInfo->mGtin, (*refpos)->mDeviceInfo->mSerialNo);
            // - invalidate device info (but keep GTIN) and revert to short address derived dSUID
            (*refpos)->invalidateDeviceInfoSerial();
            anyDuplicates = true; // at least one found
          }
        }
        if (anyDuplicates) {
          // consider my own info invalid as well
          (*busdevpos)->invalidateDeviceInfoSerial();
        }
      }
    }
    // At this point, all bus device dSUIDs can be considered stable for further use (all fallbacks due to duplicate serials in devinf applied)
    // - look for dimmers that are to be addressed as a group
    DaliBusDeviceListPtr dimmerDevices = DaliBusDeviceListPtr(new DaliBusDeviceList());
    uint16_t groupsInUse = 0; // groups in use for configured groups
    while (aBusDevices->size()>0) {
      // get first remaining
      DaliBusDevicePtr busDevice = aBusDevices->front();
      // check if this device is part of a DALI group
      sqlite3pp::query qry(mDb);
      string sql = string_format("SELECT groupNo FROM compositeDevices WHERE dimmerUID = '%s' AND dimmerType='GRP'", busDevice->mDSUID.getString().c_str());
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
                if ((*pos)->mDSUID == dimmerUID) {
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
                dimmer->mIsDummy = true; // disable bus access
                dimmer->mDSUID = dimmerUID; // just set the dSUID we know from the DB
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
    LOG(LOG_NOTICE, "Groups in use by manually grouped DALI bus devices (bitmask): 0x%04x", groupsInUse);
    initializeNextDimmer(dimmerDevices, groupsInUse, dimmerDevices->begin(), aCompletedCB, ErrorPtr());
  }
  else {
    // collecting failed
    aCompletedCB(aError);
  }
}


void DaliVdc::initializeNextDimmer(DaliBusDeviceListPtr aDimmerDevices, uint16_t aGroupsInUse, DaliBusDeviceList::iterator aNextDimmer, StatusCB aCompletedCB, ErrorPtr aError)
{
  if (Error::notOK(aError)) {
    LOG(LOG_ERR, "Error initializing dimmer: %s", aError->text());
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
    sqlite3pp::query qry(mDb);
    string sql = string_format("SELECT collectionID FROM compositeDevices WHERE dimmerUID = '%s' AND dimmerType!='GRP'", busDevice->mDSUID.getString().c_str());
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
          daliDevice->mCollectionID = collectionID; // remember from what collection this was created
          for (sqlite3pp::query::iterator j = qry.begin(); j != qry.end(); ++j) {
            string dimmerType = nonNullCStr(i->get<const char *>(0));
            DsUid dimmerUID(nonNullCStr(i->get<const char *>(1)));
            // see if we have this dimmer on the bus
            DaliBusDevicePtr dimmer;
            for (DaliBusDeviceList::iterator pos = aDimmerDevices->begin(); pos!=aDimmerDevices->end(); ++pos) {
              if ((*pos)->mDSUID == dimmerUID) {
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
              dimmer->mIsDummy = true; // disable bus access
              dimmer->mDSUID = dimmerUID; // just set the dSUID we know from the DB
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
    daliSingleControllerDevice->mDaliController = daliBusDevice;
    // - add it to our collection (if not already there)
    simpleIdentifyAndAddDevice(daliSingleControllerDevice);
  }
  // collecting complete
  aCompletedCB(getVdcErr());
}


void DaliVdc::deviceInfoReceived(DaliBusDeviceListPtr aBusDevices, DaliBusDeviceList::iterator aNextDev, StatusCB aCompletedCB, DaliDeviceInfoPtr aDaliDeviceInfoPtr, ErrorPtr aError)
{
  bool missingData = aError && aError->isError(DaliCommError::domain(), DaliCommError::MissingData);
  bool badData = aError && aError->isError(DaliCommError::domain(), DaliCommError::BadData);
  if (Error::notOK(aError) && !missingData && !badData) {
    // real fatal error, can't continue
    LOG(LOG_ERR, "Error reading device info: %s",aError->text());
    return aCompletedCB(aError);
  }
  // no error, or error but due to missing or bad data -> device exists and possibly still has ok device info
  if (missingData) { LOG(LOG_INFO, "Device at shortAddress %d is missing all or some device info data in at least one info bank",aDaliDeviceInfoPtr->mShortAddress); }
  if (badData) { LOG(LOG_INFO, "Device at shortAddress %d has bad data in at least in one info bank",aDaliDeviceInfoPtr->mShortAddress); }
  // update entry in the cache
  // Note: callback always gets a deviceInfo back, possibly with devinf_none if device does not have devInf at all (or garbage)
  //   So, assigning this here will make sure no entries with devinf_needsquery will remain.
  mDeviceInfoCache[aDaliDeviceInfoPtr->mShortAddress] = aDaliDeviceInfoPtr;
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


// MARK: - DALI specific methods

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
  else if (aMethod=="x-p44-daliInputAddrs") {
    // get list of available DALI input addresses (groups, scenes)
    respErr = getDaliInputAddrs(aRequest, aParams);
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


// MARK: - DALI bus diagnostics and summary


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
    mDaliComm.daliSendQuery(
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
    // direct bridge command as 3 byte hex string, can be repeated, result of last command is returned
    // bb1122 (bb=bridge command, 11=first DALI, 22=second DALI
    cmd = hexToBinaryString(p->stringValue().c_str(), true);
    if ((cmd.size()%3)!=0) {
      respErr = WebError::webErr(500, "bridgecmd must be integer multiple of 3 hex bytes (one or multiple DALI bridge commands)");
    }
    else {
      // - process all but last cmd w/o returning result
      int c;
      for (c=0; c<cmd.size()-3; c+=3) {
        mDaliComm.sendBridgeCommand(cmd[c+0], cmd[c+1], cmd[c+2], NoOP);
      }
      // - last cmd: return result
      mDaliComm.sendBridgeCommand(cmd[c+0], cmd[c+1], cmd[c+2], boost::bind(&DaliVdc::bridgeCmdSent, this, aRequest, _1, _2, _3));
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
          mDaliComm.daliSendDirectPower(shortAddress, 0xFE);
        }
        else if (cmd=="min") {
          mDaliComm.daliSendDirectPower(shortAddress, 0x01);
        }
        else if (cmd=="off") {
          mDaliComm.daliSendDirectPower(shortAddress, 0x00);
        }
        else if (cmd=="pulse") {
          mDaliComm.daliSendDirectPower(shortAddress, 0xFE);
          mDaliComm.daliSendDirectPower(shortAddress, 0x01, NoOP, 1200*MilliSecond);
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
    // want info about entire bus - do a raw bus scan to learn what devices are there
    mDaliComm.daliBusScan(boost::bind(&DaliVdc::daliSummaryScanDone, this, aRequest, _1, _2, _3));
  }
  return ErrorPtr(); // already sent response or callback will send response
}


void DaliVdc::daliSummaryScanDone(VdcApiRequestPtr aRequest, DaliComm::ShortAddressListPtr aShortAddressListPtr, DaliComm::ShortAddressListPtr aUnreliableShortAddressListPtr, ErrorPtr aError)
{
  ApiValuePtr res = aRequest->newApiValue();
  res->setType(apivalue_object);
  if (Error::notOK(aError)) {
    res->add("errormessage", res->newString(aError->text()));
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
  res->add("summary", summary);
  aRequest->sendResult(res);
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
    DaliDeviceInfoMap::iterator ipos = mDeviceInfoCache.find(aDaliAddress);
    if (ipos!=mDeviceInfoCache.end()) {
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
  switch (aDeviceInfo->mDevInfStatus) {
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
      aInfo->add("serialNo", aInfo->newUint64(aDeviceInfo->mSerialNo));
      if (aDeviceInfo->mOemSerialNo!=0) {
        aInfo->add("OEM_serialNo", aInfo->newUint64(aDeviceInfo->mOemSerialNo));
      }
      goto gtin;
    case DaliDeviceInfo::devinf_only_gtin:
      devInfStatus = "GTIN, but no serial";
    gtin:
      // GTIN
      aInfo->add("GTIN", aInfo->newUint64(aDeviceInfo->mGtin));
      if (aDeviceInfo->mOemGtin!=0) {
        aInfo->add("OEM_GTIN", aInfo->newUint64(aDeviceInfo->mOemGtin));
      }
      // firmware versions
      aInfo->add("versionMajor", aInfo->newUint64(aDeviceInfo->mFwVersionMajor));
      aInfo->add("versionMinor", aInfo->newUint64(aDeviceInfo->mFwVersionMinor));
      // DALI standard versions
      if (aDeviceInfo->mVers_101) aInfo->add("version_101", aInfo->newString(string_format("%d.%d", DALI_STD_VERS_MAJOR(aDeviceInfo->mVers_101), DALI_STD_VERS_MINOR(aDeviceInfo->mVers_101))));
      if (aDeviceInfo->mVers_102) aInfo->add("version_102", aInfo->newString(string_format("%d.%d", DALI_STD_VERS_MAJOR(aDeviceInfo->mVers_102), DALI_STD_VERS_MINOR(aDeviceInfo->mVers_102))));
      if (aDeviceInfo->mVers_103) aInfo->add("version_103", aInfo->newString(string_format("%d.%d", DALI_STD_VERS_MAJOR(aDeviceInfo->mVers_103), DALI_STD_VERS_MINOR(aDeviceInfo->mVers_103))));
      // logical unit index
      aInfo->add("lunIndex", aInfo->newUint64(aDeviceInfo->mLunIndex));
      break;
  }
  aInfo->add("devInfStatus", aInfo->newString(devInfStatus));
  aInfo->add("reliableId", aInfo->newBool(aDeviceInfo->mDevInfStatus==DaliDeviceInfo::devinf_solid));
  return true;
}



// MARK: - composite device creation


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
                  sqlite3pp::query qry(mDb);
                  for (groupNo=0; groupNo<16; ++groupNo) {
                    if ((mUsedDaliGroupsMask & (1<<groupNo))==0) {
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
                if (mDb.executef(
                  "INSERT OR REPLACE INTO compositeDevices (dimmerUID, dimmerType, groupNo) VALUES ('%q','GRP',%d)",
                  memberUID.getString().c_str(),
                  groupNo
                )!=SQLITE_OK) {
                  OLOG(LOG_ERR, "Error saving DALI group member: %s", mDb.error()->description().c_str());
                }
              }
            }
            else {
              deviceFound = true;
              // - create DB entry for member of composite device
              if (mDb.executef(
                "INSERT OR REPLACE INTO compositeDevices (dimmerUID, dimmerType, collectionID) VALUES ('%q','%q',%lld)",
                memberUID.getString().c_str(),
                dimmerType.c_str(),
                collectionID
              )!=SQLITE_OK) {
                OLOG(LOG_ERR, "Error saving DALI composite device member: %s", mDb.error()->description().c_str());
              }
              if (collectionID<0) {
                // use rowid of just inserted item as collectionID
                collectionID = mDb.last_insert_rowid();
                // - update already inserted first record
                if (mDb.executef(
                  "UPDATE compositeDevices SET collectionID=%lld WHERE ROWID=%lld",
                  collectionID,
                  collectionID
                )!=SQLITE_OK) {
                  OLOG(LOG_ERR, "Error updating DALI composite device: %s", mDb.error()->description().c_str());
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
        mRecollectDelayTicket.executeOnce(boost::bind(&DaliVdc::recollectDevices, this, cb), 1*Second);
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
      if(mDb.executef(
        "DELETE FROM compositeDevices WHERE dimmerType!='GRP' AND collectionID=%ld",
        (long)dev->mCollectionID
      )!=SQLITE_OK) {
        OLOG(LOG_ERR, "Error deleting DALI composite device: %s", mDb.error()->description().c_str());
      }
    }
  }
  else if (aDevice->daliTechnicalType()==dalidevice_group) {
    // group device, delete grouping
    DaliSingleControllerDevicePtr dev = boost::dynamic_pointer_cast<DaliSingleControllerDevice>(aDevice);
    if (dev) {
      int groupNo = dev->mDaliController->mDeviceInfo->mShortAddress & DaliGroupMask;
      markUsed(DaliGroup+groupNo, false);
      if(mDb.executef(
        "DELETE FROM compositeDevices WHERE dimmerType='GRP' AND groupNo=%d",
        groupNo
      )!=SQLITE_OK) {
        OLOG(LOG_ERR, "Error deleting DALI group: %s", mDb.error()->description().c_str());
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
  mRecollectDelayTicket.executeOnce(boost::bind(&DaliVdc::recollectDevices, this, cb), 1*Second);
  return respErr;
}




void DaliVdc::groupCollected(VdcApiRequestPtr aRequest)
{
  // devices re-collected, return ok (empty response)
  aRequest->sendResult(ApiValuePtr());
}


// MARK: - management of used groups and scenes

void DaliVdc::markUsed(DaliAddress aSceneOrGroup, bool aUsed)
{
  if ((aSceneOrGroup&DaliAddressTypeMask)==DaliScene) {
    uint16_t m = 1<<(aSceneOrGroup & DaliSceneMask);
    if (aUsed) mUsedDaliScenesMask |= m; else mUsedDaliScenesMask &= ~m;
    LOG(LOG_INFO,"marked DALI scene %d %s, new mask = 0x%04hX", aSceneOrGroup & DaliSceneMask, aUsed ? "IN USE" : "FREE", mUsedDaliScenesMask);
  }
  else if ((aSceneOrGroup&DaliAddressTypeMask)==DaliGroup) {
    uint16_t m = 1<<(aSceneOrGroup & DaliGroupMask);
    if (aUsed) mUsedDaliGroupsMask |= m; else mUsedDaliGroupsMask &= ~m;
    LOG(LOG_INFO,"marked DALI group %d %s, new mask = 0x%04hX", aSceneOrGroup & DaliGroupMask, aUsed ? "IN USE" : "FREE", mUsedDaliGroupsMask);
  }
}


void DaliVdc::removeMemberships(DaliAddress aSceneOrGroup)
{
  if ((aSceneOrGroup&DaliAddressTypeMask)==DaliScene) {
    // make sure no old scene settings remain in any device -> broadcast DALICMD_REMOVE_FROM_SCENE
    mDaliComm.daliSendConfigCommand(DaliBroadcast, DALICMD_REMOVE_FROM_SCENE+(aSceneOrGroup&DaliSceneMask));
  }
  else if ((aSceneOrGroup&DaliAddressTypeMask)==DaliGroup) {
    // Make sure no old group settings remain -> broadcast DALICMD_REMOVE_FROM_GROUP
    mDaliComm.daliSendConfigCommand(DaliBroadcast, DALICMD_REMOVE_FROM_GROUP+(aSceneOrGroup&DaliGroupMask));
  }
}





void DaliVdc::reserveLocallyUsedGroupsAndScenes()
{
  sqlite3pp::query qry(mDb);
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




// MARK: - Native actions (groups and scenes on vDC level)

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
      mGroupDimTicket.cancel(); // just safety, should be cancelled already
      // set fade time according to scene transition time (usually: already ok, so no time wasted)
      // note: dalicomm will make sure the fade time adjustments are sent before the scene call
      bool needDT8Activation = false;
      for (DeviceList::iterator pos = aDeliveryState->affectedDevices.begin(); pos!=aDeliveryState->affectedDevices.end(); ++pos) {
        DaliSingleControllerDevicePtr dev = boost::dynamic_pointer_cast<DaliSingleControllerDevice>(*pos);
        if (dev && dev->mDaliController) {
          dev->mDaliController->setTransitionTime(dev->transitionTimeForPreparedScene(true)); // including override value
          if (dev->mDaliController->mSupportsDT8 && !dev->mDaliController->mDT8AutoActivation) {
            needDT8Activation = true; // device does NOT have auto-activation, so we'll need to activate the called scene's
          }
        }
      }
      // Broadcast scene call: DALICMD_GO_TO_SCENE
      if (needDT8Activation) {
        // call scene
        mDaliComm.daliSendCommand(DaliBroadcast, DALICMD_GO_TO_SCENE+(a&DaliSceneMask));
        // activate the colors the scene call might have set into temporary color registers (not affected devices should have set no temp colors at this point!)
        mDaliComm.daliSendCommand(DaliBroadcast, DALICMD_DT8_ACTIVATE, boost::bind(&DaliVdc::nativeActionDone, this, aStatusCB, _1));
      }
      else {
        // just call the scene
        mDaliComm.daliSendCommand(DaliBroadcast, DALICMD_GO_TO_SCENE+(a&DaliSceneMask), boost::bind(&DaliVdc::nativeActionDone, this, aStatusCB, _1));
      }
      return;
    }
    else if (aDeliveryState->optimizedType==ntfy_dimchannel) {
      // Dim group
      // - get mode
      VdcDimMode dm = (VdcDimMode)aDeliveryState->actionVariant;
      OLOG(LOG_INFO,
        "optimized group dimming (DALI): 'brightness' %s",
        dm==dimmode_stop ? "STOPS dimming" : (dm==dimmode_up ? "starts dimming UP" : "starts dimming DOWN")
      );
      // - prepare dimming in all affected devices, i.e. check fade rate (usually: already ok, so no time wasted)
      // note: we let all devices do this in parallel, continue when last device reports done
      aDeliveryState->pendingCount = aDeliveryState->affectedDevices.size(); // must be set before calling executePreparedOperation() the first time
      for (DeviceList::iterator pos = aDeliveryState->affectedDevices.begin(); pos!=aDeliveryState->affectedDevices.end(); ++pos) {
        DaliSingleControllerDevicePtr dev = boost::dynamic_pointer_cast<DaliSingleControllerDevice>(*pos);
        if (dev && dev->mDaliController) {
          // prepare
          dev->mDaliController->dimPrepare(dm, dev->getChannelByType(channeltype_brightness)->getDimPerMS(), boost::bind(&DaliVdc::groupDimPrepared, this, aStatusCB, a, aDeliveryState, _1));
        }
      }
      return;
    }
  }
  aStatusCB(TextError::err("Native action '%s' (DaliAddress 0x%02X) not supported", aNativeActionId.c_str(), a)); // causes normal execution
}


void DaliVdc::groupDimPrepared(StatusCB aStatusCB, DaliAddress aDaliAddress, NotificationDeliveryStatePtr aDeliveryState, ErrorPtr aError)
{
  if (Error::notOK(aError)) {
    OLOG(LOG_WARNING, "Error while preparing device for group dimming: %s", aError->text());
  }
  if (--aDeliveryState->pendingCount>0) {
    FOCUSOLOG("waiting for all affected devices to confirm dim preparation: %d/%d remaining", aDeliveryState->pendingCount, aDeliveryState->affectedDevices.size());
    return; // not all confirmed yet
  }
  // now issue dimming command to group
  VdcDimMode dm = (VdcDimMode)aDeliveryState->actionVariant;
  if (dm==dimmode_stop) {
    // stop dimming
    // - cancel repeater ticket
    mGroupDimTicket.cancel();
    // - send MASK to group
    mDaliComm.daliSendDirectPower(aDaliAddress, DALIVALUE_MASK, boost::bind(&DaliVdc::nativeActionDone, this, aStatusCB, _1));
    return;
  }
  else {
    // start dimming right now
    mGroupDimTicket.executeOnce(boost::bind(&DaliVdc::groupDimRepeater, this, aDaliAddress, dm==dimmode_up ? DALICMD_UP : DALICMD_DOWN, _1));
    // confirm action
    nativeActionDone(aStatusCB, aError);
    return;
  }
}


void DaliVdc::groupDimRepeater(DaliAddress aDaliAddress, uint8_t aCommand, MLTimer &aTimer)
{
  mDaliComm.daliSendCommand(aDaliAddress, aCommand);
  MainLoop::currentMainLoop().retriggerTimer(aTimer, 200*MilliSecond);
}


void DaliVdc::nativeActionDone(StatusCB aStatusCB, ErrorPtr aError)
{
  FOCUSOLOG("DALI Native action done with status: %s", Error::text(aError).c_str());
  if (aStatusCB) aStatusCB(aError);
}




void DaliVdc::createNativeAction(StatusCB aStatusCB, OptimizerEntryPtr aOptimizerEntry, NotificationDeliveryStatePtr aDeliveryState)
{
  ErrorPtr err;
  DaliAddress a = NoDaliAddress;
  if (aOptimizerEntry->type==ntfy_callscene) {
    // need a free scene
    for (int s=0; s<16; s++) {
      if ((mUsedDaliScenesMask & (1<<s))==0) {
        a = DaliScene + s;
        break;
      }
    }
  }
  else if (aOptimizerEntry->type==ntfy_dimchannel) {
    // need a free group
    for (int g=0; g<16; g++) {
      if ((mUsedDaliGroupsMask & (1<<g))==0) {
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
    OLOG(LOG_INFO,"creating action '%s' (DaliAddress=0x%02X)", aOptimizerEntry->nativeActionId.c_str(), a);
    if (aDeliveryState->optimizedType==ntfy_callscene) {
      // make sure no old scene settings remain in any device -> broadcast DALICMD_REMOVE_FROM_SCENE
      mDaliComm.daliSendConfigCommand(DaliBroadcast, DALICMD_REMOVE_FROM_SCENE+(a&DaliSceneMask));
      // now update this scene's values
      updateNativeAction(aStatusCB, aOptimizerEntry, aDeliveryState);
      return;
    }
    else if (aDeliveryState->optimizedType==ntfy_dimchannel) {
      // Make sure no old group settings remain -> broadcast DALICMD_REMOVE_FROM_GROUP
      mDaliComm.daliSendConfigCommand(DaliBroadcast, DALICMD_REMOVE_FROM_GROUP+(a&DaliGroupMask));
      // now create new group -> for each affected device sent DALICMD_ADD_TO_GROUP
      for (DeviceList::iterator pos = aDeliveryState->affectedDevices.begin(); pos!=aDeliveryState->affectedDevices.end(); ++pos) {
        DaliSingleControllerDevicePtr dev = boost::dynamic_pointer_cast<DaliSingleControllerDevice>(*pos);
        if (dev && dev->mDaliController) {
          mDaliComm.daliSendConfigCommand(dev->mDaliController->mDeviceInfo->mShortAddress, DALICMD_ADD_TO_GROUP+(a&DaliGroupMask));
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
      if (dev && dev->mDaliController) {
        LightBehaviourPtr l = dev->getOutput<LightBehaviour>();
        if (l) {
          ColorLightBehaviourPtr cl = dev->getOutput<ColorLightBehaviour>();
          if (cl) {
            // need to set up the temp color param registers before storing the scene
            dev->mDaliController->setColorParamsFromChannels(cl, false, true, false); // non-transitional, always set, not silent
          }
          uint8_t power = dev->mDaliController->brightnessToArcpower(l->brightnessForHardware(true)); // non-transitional, final
          mDaliComm.daliSendDtrAndConfigCommand(dev->mDaliController->mDeviceInfo->mShortAddress, DALICMD_STORE_DTR_AS_SCENE+(a&DaliSceneMask), power);
        }
        // in case this is a DT8 device, enable automatic activation at scene call (and at brightness changes)
        // Note: before here, i.e. when the optimizer is used, we don't touch the auto-activation bit and just use it as-is
        if (dev->mDaliController->mSupportsDT8 && !dev->mDaliController->mDT8AutoActivation) {
          OLOG(LOG_INFO,"enabling color auto-activation for device %d", dev->mDaliController->mDeviceInfo->mShortAddress);
          dev->mDaliController->mDT8AutoActivation = true; // now enabled
          mDaliComm.daliSendDtrAndConfigCommand(dev->mDaliController->mDeviceInfo->mShortAddress, DALICMD_DT8_SET_GEAR_FEATURES, 0x01); // Bit0 = auto activation
        }
      }
    }
    // done
    OLOG(LOG_INFO,"updated DALI scene #%d", a&DaliSceneMask);
    aOptimizerEntry->lastNativeChange = MainLoop::now();
    aStatusCB(ErrorPtr());
    return;
  }
  aStatusCB(TextError::err("cannot update DALI native action for type=%d", (int)aOptimizerEntry->type));
}


void DaliVdc::freeNativeAction(StatusCB aStatusCB, const string aNativeActionId)
{
  DaliAddress a = daliAddressFromActionId(aNativeActionId);
  markUsed(a, false);
  // Nothing more to do here, keep group or scene as-is, will not be called until re-used
  if (aStatusCB) aStatusCB(ErrorPtr());
}



#if SELFTESTING_ENABLED

// MARK: - Self test

void DaliVdc::selfTest(StatusCB aCompletedCB)
{
  // do bus short address scan
  mDaliComm.daliBusScan(boost::bind(&DaliVdc::testScanDone, this, aCompletedCB, _1, _2, _3));
}


void DaliVdc::testScanDone(StatusCB aCompletedCB, DaliComm::ShortAddressListPtr aShortAddressListPtr, DaliComm::ShortAddressListPtr aUnreliableShortAddressListPtr, ErrorPtr aError)
{
  if (Error::isOK(aError) && aShortAddressListPtr && aShortAddressListPtr->size()>0) {
    // found at least one device, do a R/W test using the DTR
    DaliAddress testAddr = aShortAddressListPtr->front();
    LOG(LOG_NOTICE, "- DALI self test: switch all lights on, then do R/W tests with DTR of device short address %d",testAddr);
    mDaliComm.daliSendDirectPower(DaliBroadcast, 0, NoOP); // off
    mDaliComm.daliSendDirectPower(DaliBroadcast, 254, NoOP, 2*Second); // max
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
  mDaliComm.daliSend(DALICMD_SET_DTR, aTestByte);
  // query DTR again, with 200mS delay
  mDaliComm.daliSendQuery(aShortAddr, DALICMD_QUERY_CONTENT_DTR, boost::bind(&DaliVdc::testRWResponse, this, aCompletedCB, aShortAddr, aTestByte, _1, _2, _3), 200*MilliSecond);
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
        mDaliComm.daliSendDirectPower(DaliBroadcast, 0); // off
        return;
    }
    // launch next test
    testRW(aCompletedCB, aShortAddr, aTestByte);
  }
  else {
    // not ok
    if (Error::isOK(aError) && aNoOrTimeout) aError = ErrorPtr(new DaliCommError(DaliCommError::MissingData));
    // report
    LOG(LOG_ERR, "DALI self test error: sent 0x%02X, error: %s",aTestByte, aError->text());
    aCompletedCB(aError);
  }
}

#endif // SELFTESTING_ENABLED



#if ENABLE_DALI_INPUTS

// MARK: - DALI input devices

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
        // remove all control gear from the addresses used for this input device
        dev->freeAddresses();
        // set name
        if (name.size()>0) dev->setName(name);
        // insert into database
        if(mDb.executef(
          "INSERT OR REPLACE INTO inputDevices (daliInputConfig, daliBaseAddr) VALUES ('%q', %d)",
          deviceConfig.c_str(), baseAddress
        )!=SQLITE_OK) {
          respErr = mDb.error("saving DALI input device params");
        }
        else {
          dev->mDaliInputDeviceRowID = mDb.last_insert_rowid();
          // confirm
          ApiValuePtr r = aRequest->newApiValue();
          r->setType(apivalue_object);
          r->add("dSUID", r->newBinary(dev->mDSUID.getBinary()));
          r->add("rowid", r->newUint64(dev->mDaliInputDeviceRowID));
          r->add("name", r->newString(dev->getName()));
          aRequest->sendResult(r);
          respErr.reset(); // make sure we don't send an extra ErrorOK
        }
      }
    }
  }
  return respErr;
}


ErrorPtr DaliVdc::getDaliInputAddrs(VdcApiRequestPtr aRequest, ApiValuePtr aParams)
{
  ApiValuePtr resp = aRequest->newApiValue();
  resp->setType(apivalue_array);
  // available groups
  for (int g=0; g<16; g++) {
    if ((mUsedDaliGroupsMask & (1<<g))==0) {
      ApiValuePtr grp = resp->newObject();
      grp->add("name", resp->newString(string_format("DALI group %d",g)));
      grp->add("addr", resp->newUint64(DaliGroup|g));
      resp->arrayAppend(grp);
    }
  }
  // available scenes
  for (int s=0; s<16; s++) {
    if ((mUsedDaliScenesMask & (1<<s))==0) {
      ApiValuePtr scn = resp->newObject();
      scn->add("name", resp->newString(string_format("DALI scene %d",s)));
      scn->add("addr", resp->newUint64(DaliScene|s));
      resp->arrayAppend(scn);
    }
  }
  aRequest->sendResult(resp);
  return ErrorPtr();
}


void DaliVdc::daliEventHandler(uint8_t aEvent, uint8_t aData1, uint8_t aData2)
{
  if (aEvent==EVENT_CODE_FOREIGN_FRAME && aData1==DALICMD_PING && aData2==0) {
    LOG(LOG_WARNING, "DALI: another bus master is using this bus -> NOT SUPPORTED!");
  }
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

