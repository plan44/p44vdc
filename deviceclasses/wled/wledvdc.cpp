//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Michael Troß <digitalstrom@tross.org>
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

#include "wledvdc.hpp"
#include "wleddevice.hpp"

#if ENABLE_WLED

#define WLED_DB_VERSION 2
#define WLED_MAX_PRESET_ID 250   ///< WLED supports preset IDs 1..250

using namespace p44;


// MARK: - WledPersistence

string WledPersistence::schemaUpgradeSQL(int aFromVersion, int &aToVersion)
{
  string sql;

  if (aFromVersion < 1) {
    sql = inherited::schemaUpgradeSQL(aFromVersion, aToVersion);
    sql.append(
      "ALTER TABLE $PREFIX_globs ADD wledDeviceURL TEXT;"
      "ALTER TABLE $PREFIX_globs ADD wledDeviceHostname TEXT;"
    );
    aToVersion = 1;
  }

  if (aFromVersion == 1) {
    // Version 2: per-device table; migrate legacy single-hostname entry
    sql.append(
      "CREATE TABLE $PREFIX_wledDevices (mac TEXT NOT NULL PRIMARY KEY, hostname TEXT NOT NULL);"
      "INSERT OR IGNORE INTO $PREFIX_wledDevices (mac, hostname)"
      "  SELECT wledDeviceHostname, wledDeviceHostname FROM $PREFIX_globs"
      "  WHERE wledDeviceHostname IS NOT NULL AND wledDeviceHostname != '';"
    );
    aToVersion = 2;
  }

  return sql;
}


// MARK: - WledVdc

WledVdc::WledVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  inherited(aInstanceNumber, aVdcHostP, aTag),
  mAutoDiscovery(true)
{
}


WledVdc::~WledVdc()
{
}


void WledVdc::setLogLevelOffset(int aLogLevelOffset)
{
  inherited::setLogLevelOffset(aLogLevelOffset);
}


const char *WledVdc::vdcClassIdentifier() const
{
  return "wled_color_lights_container";
}


void WledVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  LOG(LOG_NOTICE, ">>> WLED VDC initialize() (instance %d)", getInstanceNumber());
  load();

  ErrorPtr err = initializePersistence(mDb, WLED_DB_VERSION, 1);
  if (Error::notOK(err)) {
    if (aCompletedCB) aCompletedCB(err);
    return;
  }

  setPeriodicRecollection(30*Second, rescanmode_incremental);
  if (aCompletedCB) aCompletedCB(ErrorPtr());
}


// MARK: - Device scanning

void WledVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  LOG(LOG_INFO, "WLED VDC scanForDevices (flags=0x%x)", aRescanFlags);

  if (!(aRescanFlags & rescanmode_incremental)) {
    removeDevices(aRescanFlags & rescanmode_clearsettings);
    mWledDevices.clear();
    if (aRescanFlags & rescanmode_clearsettings) {
      mDb.prefixedExecute("DELETE FROM $PREFIX_wledDevices");
    }
  }

  // Load all known (mac, hostname) pairs
  typedef vector<pair<string,string>> DeviceList;
  auto knownDevices = std::make_shared<DeviceList>();

  SQLiteTGQuery qry(mDb);
  if (Error::isOK(qry.prefixedPrepare("SELECT mac, hostname FROM $PREFIX_wledDevices"))) {
    for (sqlite3pp::query::iterator i = qry.begin(); i != qry.end(); ++i) {
      string mac      = nonNullCStr(i->get<const char*>(0));
      string hostname = nonNullCStr(i->get<const char*>(1));
      if (!hostname.empty()) knownDevices->push_back({mac, hostname});
    }
  }
  LOG(LOG_INFO, "WLED: %zu known device(s) in DB", knownDevices->size());

  if (knownDevices->empty()) {
    if (mAutoDiscovery) {
      performDiscovery(aCompletedCB);
    } else {
      if (aCompletedCB) aCompletedCB(ErrorPtr());
    }
    return;
  }

  auto countdown = std::make_shared<int>((int)knownDevices->size());
  for (auto &kv : *knownDevices) {
    connectToDevice(kv.second, [this, aCompletedCB, countdown, aRescanFlags](ErrorPtr err) {
      if (--(*countdown) == 0) {
        if ((aRescanFlags & rescanmode_exhaustive) && mAutoDiscovery) {
          performDiscovery(aCompletedCB);
        } else {
          if (aCompletedCB) aCompletedCB(ErrorPtr());
        }
      }
    });
  }
}


void WledVdc::connectToDevice(const string &aHostname, StatusCB aCompletedCB)
{
  LOG(LOG_INFO, "WLED: connecting to '%s'", aHostname.c_str());

  // One shared WledComm per physical WLED device.
  WledCommPtr comm = new WledComm();
  comm->setDeviceURL(string_format("http://%s", aHostname.c_str()));

  // First: get device info
  comm->getInfo([this, aHostname, comm, aCompletedCB](JsonObjectPtr aInfo, ErrorPtr aError) {
    if (Error::notOK(aError) || !aInfo) {
      LOG(LOG_WARNING, "WLED: cannot reach '%s': %s",
          aHostname.c_str(), aError ? aError->text() : "no info");
      if (aCompletedCB) aCompletedCB(aError ? aError : Error::err<WledCommError>(WledCommError::NoDeviceInfo));
      return;
    }

    // Second: get state to determine segment count
    comm->getState([this, aHostname, comm, aInfo, aCompletedCB](JsonObjectPtr aState, ErrorPtr aError) {
      // state errors are non-fatal — fall back to single-segment
      createDevicesForPhysicalDevice(comm, aHostname, aInfo, aState, aCompletedCB);
    });
  });
}


void WledVdc::createDevicesForPhysicalDevice(WledCommPtr aComm, const string &aHostname,
                                              JsonObjectPtr aInfo, JsonObjectPtr aState,
                                              StatusCB aCompletedCB)
{
  // Determine MAC address (used as the unique physical device key)
  string mac;
  JsonObjectPtr macObj = aInfo->get("mac");
  if (!macObj) macObj = aInfo->get("deviceId");
  if (macObj) mac = macObj->stringValue();
  if (mac.empty()) mac = aHostname;

  // Persist/update the hostname mapping
  mDb.prefixedExecute(
    "DELETE FROM $PREFIX_wledDevices WHERE mac = hostname AND hostname = '%q'",
    aHostname.c_str()
  );
  mDb.prefixedExecute(
    "INSERT OR REPLACE INTO $PREFIX_wledDevices (mac, hostname) VALUES ('%q', '%q')",
    mac.c_str(), aHostname.c_str()
  );

  // Count segments from state.seg array
  int numSegments = 1;
  if (aState) {
    JsonObjectPtr segArr = aState->get("seg");
    if (segArr) numSegments = max(1, segArr->arrayLength());
  }
  LOG(LOG_INFO, "WLED: device '%s' has %d segment(s)", mac.c_str(), numSegments);

  if (numSegments <= 1) {
    // Single-segment: create one whole-device WledDevice (mSegmentId = -1)
    string key = mac;
    auto it = mWledDevices.find(key);
    if (it != mWledDevices.end()) {
      it->second->updateInfo(aInfo);
      LOG(LOG_INFO, "WLED: updated device mac=%s", mac.c_str());
    } else {
      WledDevicePtr device = new WledDevice(this, aComm, -1, aInfo);
      mWledDevices[key] = device;
      simpleIdentifyAndAddDevice(device);
      JsonObjectPtr n = aInfo->get("name");
      if (n) device->initializeName(n->stringValue());
      LOG(LOG_INFO, "WLED: added device mac=%s", mac.c_str());
    }
  }
  else {
    // Multi-segment: one WledDevice per segment, all sharing the same WledComm
    for (int seg = 0; seg < numSegments; seg++) {
      string key = string_format("%s:seg%d", mac.c_str(), seg);
      auto it = mWledDevices.find(key);
      if (it != mWledDevices.end()) {
        it->second->updateInfo(aInfo);
        LOG(LOG_INFO, "WLED: updated seg device %s", key.c_str());
      } else {
        WledDevicePtr device = new WledDevice(this, aComm, seg, aInfo);
        mWledDevices[key] = device;
        simpleIdentifyAndAddDevice(device);
        // Name each segment: "<devicename> seg N"
        JsonObjectPtr n = aInfo->get("name");
        string segName = (n ? n->stringValue() : mac) + string_format(" seg%d", seg);
        device->initializeName(segName);
        LOG(LOG_INFO, "WLED: added seg device %s", key.c_str());
      }
    }
  }

  if (aCompletedCB) aCompletedCB(ErrorPtr());
}


void WledVdc::handleGlobalEvent(VdchostEvent aEvent)
{
  if (aEvent == vdchost_network_reconnected) {
    OLOG(LOG_NOTICE, "network reconnected — triggering incremental rescan");
    collectDevices(NoOP, rescanmode_incremental);
  }
  inherited::handleGlobalEvent(aEvent);
}


string WledVdc::hardwareGUID()
{
  return string_format("wled_%d", getInstanceNumber());
}


bool WledVdc::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("vdc_wled", aIcon, aWithData, aResolutionPrefix)) return true;
  return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


string WledVdc::getExtraInfo()
{
  return string_format("wled: %zu device(s)", mWledDevices.size());
}


void WledVdc::removeDevice(DevicePtr aDevice, bool aForget)
{
  for (auto it = mWledDevices.begin(); it != mWledDevices.end(); ++it) {
    if (it->second == aDevice) {
      if (aForget) {
        // Remove from DB.  For segment devices, only delete the physical row once
        // (when the last segment is removed); for simplicity delete by hostname
        // (which may be a no-op if already gone).
        WledDevicePtr wd = it->second;
        mDb.prefixedExecute(
          "DELETE FROM $PREFIX_wledDevices WHERE mac = '%q'",
          wd->mUniqueId.c_str()
        );
      }
      mWledDevices.erase(it);
      break;
    }
  }
  inherited::removeDevice(aDevice, aForget);
}


ErrorPtr WledVdc::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  if (uequals(aMethod, "setDeviceHostname")) {
    if (!aParams) return ErrorPtr();
    ApiValuePtr hostnameParam = aParams->get("hostname");
    if (!hostnameParam) return ErrorPtr();
    connectToDevice(hostnameParam->stringValue(), NULL);
    return ErrorPtr();
  }
  return inherited::handleMethod(aRequest, aMethod, aParams);
}


// MARK: - DNS-SD discovery

void WledVdc::performDiscovery(StatusCB aCompletedCB)
{
  #if WLED_DNSSD_DISCOVERY
  LOG(LOG_INFO, "WLED: starting DNS-SD discovery for _wled._tcp");
  DnsSdManager::sharedDnsSdManager().browse(
    "_wled._tcp",
    boost::bind(&WledVdc::handleDiscoveryHandler, this, _1, _2, aCompletedCB)
  );
  #else
  LOG(LOG_WARNING, "WLED DNS-SD discovery is disabled");
  if (aCompletedCB) aCompletedCB(Error::err<WledCommError>(WledCommError::DeviceNotFound));
  #endif
}


bool WledVdc::handleDiscoveryHandler(ErrorPtr aError, DnsSdServiceInfoPtr aServiceInfo, StatusCB aCompletedCB)
{
  if (Error::notOK(aError)) {
    LOG(LOG_ERR, "WLED discovery error: %s", aError->text());
    if (aCompletedCB) aCompletedCB(aError);
    return false;
  }

  if (aServiceInfo && !aServiceInfo->disappeared) {
    string hostname = aServiceInfo->hostaddress;
    LOG(LOG_INFO, "WLED: DNS-SD found '%s'", hostname.c_str());
    connectToDevice(hostname, [this, aCompletedCB](ErrorPtr err) {
      if (aCompletedCB) aCompletedCB(ErrorPtr());
    });
  }
  return true; // continue browsing for more devices
}


// MARK: - Preset helpers

string WledVdc::wledNativeActionId(const string &aMac, int aPresetId)
{
  return string_format("wled:%s:p:%d", aMac.c_str(), aPresetId);
}


bool WledVdc::parseWledNativeActionId(const string &aId, string &aMac, int &aPresetId)
{
  // format: "wled:<mac>:p:<N>"
  if (aId.substr(0, 5) != "wled:") return false;
  size_t pPos = aId.rfind(":p:");
  if (pPos == string::npos) return false;
  aMac = aId.substr(5, pPos - 5);
  aPresetId = atoi(aId.c_str() + pPos + 3);
  return aPresetId >= 1 && aPresetId <= WLED_MAX_PRESET_ID;
}


int WledVdc::allocatePresetId(const string &aMac)
{
  auto &used = mUsedPresetIds[aMac];
  for (int id = 1; id <= WLED_MAX_PRESET_ID; id++) {
    if (used.find(id) == used.end()) {
      used.insert(id);
      return id;
    }
  }
  return -1; // no free slots
}


void WledVdc::freePresetId(const string &aMac, int aPresetId)
{
  auto it = mUsedPresetIds.find(aMac);
  if (it != mUsedPresetIds.end()) {
    it->second.erase(aPresetId);
  }
}


WledDevicePtr WledVdc::findDeviceByMac(const string &aMac)
{
  // Return the first device whose mUniqueId matches aMac
  for (auto &kv : mWledDevices) {
    if (kv.second->mUniqueId == aMac) return kv.second;
  }
  return WledDevicePtr();
}


void WledVdc::savePreset(WledDevicePtr aDev, int aPresetId, int aSceneId, StatusCB aStatusCB)
{
  JsonObjectPtr saveReq = JsonObject::newObj();
  saveReq->add("psave", JsonObject::newInt32(aPresetId));
  saveReq->add("n",     JsonObject::newString(string_format("dS-Scene-%d", aSceneId)));
  aDev->mComm->setState(saveReq, [aStatusCB](JsonObjectPtr, ErrorPtr aError) {
    if (aStatusCB) aStatusCB(aError);
  });
}


// MARK: - Scene optimizer

bool WledVdc::shouldUseOptimizerFor(NotificationDeliveryStatePtr aDeliveryState)
{
  // Enable optimizer for any scene call, regardless of how many devices are involved.
  // (The base-class default requires >= mMinDevicesForOptimizing which is usually 5.)
  return aDeliveryState->mOptimizedType == ntfy_callscene;
}


ErrorPtr WledVdc::announceNativeAction(const string aNativeActionId)
{
  string mac; int presetId;
  if (parseWledNativeActionId(aNativeActionId, mac, presetId)) {
    mUsedPresetIds[mac].insert(presetId);
    OLOG(LOG_DEBUG, "announceNativeAction: preset %d on device %s re-registered",
         presetId, mac.c_str());
  }
  return ErrorPtr();
}


void WledVdc::callNativeAction(StatusCB aStatusCB, const string aNativeActionId,
                                NotificationDeliveryStatePtr aDeliveryState)
{
  if (aDeliveryState->mOptimizedType != ntfy_callscene) {
    if (aStatusCB) aStatusCB(TextError::err("WLED: only callscene native actions are supported"));
    return;
  }

  string mac; int presetId;
  if (!parseWledNativeActionId(aNativeActionId, mac, presetId)) {
    if (aStatusCB) aStatusCB(TextError::err("WLED: invalid native action id '%s'", aNativeActionId.c_str()));
    return;
  }

  WledDevicePtr dev = findDeviceByMac(mac);
  if (!dev) {
    if (aStatusCB) aStatusCB(TextError::err("WLED: device '%s' not found for native action", mac.c_str()));
    return;
  }

  OLOG(LOG_INFO, "callNativeAction: recalling WLED preset %d on device %s", presetId, mac.c_str());
  JsonObjectPtr recallReq = JsonObject::newObj();
  recallReq->add("ps", JsonObject::newInt32(presetId));
  dev->mComm->setState(recallReq, [aStatusCB](JsonObjectPtr, ErrorPtr aError) {
    // Returning NULL (no error) tells the framework the preset was applied — skip per-device apply.
    // Returning an error causes the framework to fall back to device-by-device apply.
    if (aStatusCB) aStatusCB(aError);
  });
}


void WledVdc::createNativeAction(StatusCB aStatusCB, OptimizerEntryPtr aOptimizerEntry,
                                  NotificationDeliveryStatePtr aDeliveryState)
{
  if (aOptimizerEntry->mType != ntfy_callscene) {
    if (aStatusCB) aStatusCB(TextError::err("WLED: cannot create native action for type %d",
                                             (int)aOptimizerEntry->mType));
    return;
  }

  // Use the first affected device to determine which physical WLED device to use
  if (aDeliveryState->mAffectedDevices.empty()) {
    if (aStatusCB) aStatusCB(TextError::err("WLED: no affected devices for native action"));
    return;
  }
  WledDevicePtr dev = boost::dynamic_pointer_cast<WledDevice>(aDeliveryState->mAffectedDevices.front());
  if (!dev) {
    if (aStatusCB) aStatusCB(TextError::err("WLED: affected device is not a WledDevice"));
    return;
  }

  string mac = dev->mUniqueId;
  int presetId = allocatePresetId(mac);
  if (presetId < 0) {
    if (aStatusCB) aStatusCB(TextError::err("WLED: no free WLED preset slots on device %s", mac.c_str()));
    return;
  }

  OLOG(LOG_INFO, "createNativeAction: saving preset %d on device %s for scene %d",
       presetId, mac.c_str(), aOptimizerEntry->mContentId);

  savePreset(dev, presetId, aOptimizerEntry->mContentId, [this, aStatusCB, aOptimizerEntry, mac, presetId](ErrorPtr aError) {
    if (Error::notOK(aError)) {
      freePresetId(mac, presetId);
      if (aStatusCB) aStatusCB(aError);
      return;
    }
    aOptimizerEntry->mNativeActionId = wledNativeActionId(mac, presetId);
    aOptimizerEntry->mLastNativeChange = MainLoop::now();
    aOptimizerEntry->markDirty();
    OLOG(LOG_INFO, "createNativeAction: created preset, nativeId='%s'",
         aOptimizerEntry->mNativeActionId.c_str());
    if (aStatusCB) aStatusCB(ErrorPtr());
  });
}


void WledVdc::updateNativeAction(StatusCB aStatusCB, OptimizerEntryPtr aOptimizerEntry,
                                  NotificationDeliveryStatePtr aDeliveryState)
{
  // Find the device for this preset
  string mac; int presetId;
  if (!parseWledNativeActionId(aOptimizerEntry->mNativeActionId, mac, presetId)) {
    if (aStatusCB) aStatusCB(TextError::err("WLED: invalid native action id for update"));
    return;
  }
  WledDevicePtr dev = findDeviceByMac(mac);
  if (!dev) {
    if (aStatusCB) aStatusCB(TextError::err("WLED: device not found for preset update"));
    return;
  }

  // Find the longest transition time across all affected devices
  MLMicroSeconds maxtt = 0;
  for (auto &d : aDeliveryState->mAffectedDevices) {
    MLMicroSeconds devtt = d->transitionTimeForPreparedScene(true);
    if (devtt > maxtt) maxtt = devtt;
  }

  // Tell the framework we are done immediately (do not block further notifications),
  // but save the preset after the transition has finished.
  uint64_t newHash = aOptimizerEntry->mContentsHash;
  aOptimizerEntry->mContentsHash = 0; // mark as stale until confirmed saved
  aOptimizerEntry->markDirty();

  OLOG(LOG_INFO, "updateNativeAction: delaying preset %d update by %lld ms (transition)",
       presetId, (long long)(maxtt * 3 / 2 / MilliSecond));

  string nativeId = aOptimizerEntry->mNativeActionId;
  mDelayedPresetUpdateTicket.executeOnce(
    [this, newHash, nativeId, dev, presetId, aOptimizerEntry](MLTimer&, MLMicroSeconds) {
      performPresetUpdate(newHash, nativeId, dev, presetId, aOptimizerEntry);
    },
    maxtt * 3 / 2   // wait 150% of the transition time
  );

  if (aStatusCB) aStatusCB(ErrorPtr()); // tell framework we are done immediately
}


void WledVdc::performPresetUpdate(uint64_t aNewHash, string aNativeActionId,
                                   WledDevicePtr aDev, int aPresetId,
                                   OptimizerEntryPtr aOptimizerEntry)
{
  // Verify the nativeActionId still matches (it might have been freed/recreated in the meantime)
  if (aOptimizerEntry->mNativeActionId != aNativeActionId) {
    OLOG(LOG_DEBUG, "performPresetUpdate: native action changed, skipping stale update");
    return;
  }

  OLOG(LOG_INFO, "performPresetUpdate: saving updated preset %d on device %s",
       aPresetId, aDev->mUniqueId.c_str());

  savePreset(aDev, aPresetId, 0 /* sceneId not needed for update */, [this, aNewHash, aOptimizerEntry](ErrorPtr aError) {
    if (Error::isOK(aError)) {
      aOptimizerEntry->mContentsHash = aNewHash;
      aOptimizerEntry->mLastNativeChange = MainLoop::now();
      aOptimizerEntry->markDirty();
      OLOG(LOG_INFO, "performPresetUpdate: preset updated successfully");
    } else {
      OLOG(LOG_WARNING, "performPresetUpdate: preset save failed: %s", aError->text());
    }
  });
}


void WledVdc::cancelNativeActionUpdate()
{
  mDelayedPresetUpdateTicket.cancel();
}


void WledVdc::freeNativeAction(StatusCB aStatusCB, const string aNativeActionId)
{
  string mac; int presetId;
  if (!parseWledNativeActionId(aNativeActionId, mac, presetId)) {
    if (aStatusCB) aStatusCB(ErrorPtr());
    return;
  }

  OLOG(LOG_INFO, "freeNativeAction: deleting preset %d on device %s", presetId, mac.c_str());

  WledDevicePtr dev = findDeviceByMac(mac);
  if (!dev) {
    freePresetId(mac, presetId);
    if (aStatusCB) aStatusCB(ErrorPtr());
    return;
  }

  JsonObjectPtr delReq = JsonObject::newObj();
  delReq->add("pdel", JsonObject::newInt32(presetId));
  dev->mComm->setState(delReq, [this, aStatusCB, mac, presetId](JsonObjectPtr, ErrorPtr aError) {
    freePresetId(mac, presetId);
    if (aStatusCB) aStatusCB(ErrorPtr()); // errors during delete are non-fatal
  });
}

#endif // ENABLE_WLED
