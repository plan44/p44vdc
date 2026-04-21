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

using namespace p44;


// MARK: - WledPersistence

string WledPersistence::schemaUpgradeSQL(int aFromVersion, int &aToVersion)
{
  string sql;

  if (aFromVersion < 1) {
    // Initial schema — create globs table and add legacy single-device columns
    sql = inherited::schemaUpgradeSQL(aFromVersion, aToVersion);
    sql.append(
      "ALTER TABLE $PREFIX_globs ADD wledDeviceURL TEXT;"
      "ALTER TABLE $PREFIX_globs ADD wledDeviceHostname TEXT;"
    );
    aToVersion = 1;
  }

  if (aFromVersion == 1) {
    // Version 2: per-device table for multi-device support.
    // Also migrates any existing single-device hostname from the globs table.
    sql.append(
      "CREATE TABLE $PREFIX_wledDevices (mac TEXT NOT NULL PRIMARY KEY, hostname TEXT NOT NULL);"
      // Copy old single-device hostname, using hostname as MAC placeholder (will be
      // replaced with real MAC on first successful connection).
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
  LOG(LOG_DEBUG, "WledVdc constructor: instance %d, tag %d", aInstanceNumber, aTag);
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
  LOG(LOG_NOTICE, ">>> WLED VDC initialize() called (instance %d)", getInstanceNumber());

  load();

  ErrorPtr err = initializePersistence(mDb, WLED_DB_VERSION, 1);
  if (Error::notOK(err)) {
    LOG(LOG_ERR, "Failed to initialize WLED database: %s", err->text());
    if (aCompletedCB) aCompletedCB(err);
    return;
  }

  setPeriodicRecollection(30*Second, rescanmode_incremental);

  if (aCompletedCB) aCompletedCB(ErrorPtr());
}


void WledVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  LOG(LOG_INFO, "WLED VDC scanning for devices (flags=0x%x)", aRescanFlags);

  if (!(aRescanFlags & rescanmode_incremental)) {
    removeDevices(aRescanFlags & rescanmode_clearsettings);
    mWledDevices.clear();
    if (aRescanFlags & rescanmode_clearsettings) {
      mDb.prefixedExecute("DELETE FROM $PREFIX_wledDevices");
    }
  }

  // Load all known (mac, hostname) pairs from DB
  typedef vector<pair<string,string>> DeviceList;
  auto knownDevices = std::make_shared<DeviceList>();

  SQLiteTGQuery qry(mDb);
  if (Error::isOK(qry.prefixedPrepare("SELECT mac, hostname FROM $PREFIX_wledDevices"))) {
    for (sqlite3pp::query::iterator i = qry.begin(); i != qry.end(); ++i) {
      string mac      = nonNullCStr(i->get<const char*>(0));
      string hostname = nonNullCStr(i->get<const char*>(1));
      if (!hostname.empty()) {
        knownDevices->push_back({mac, hostname});
      }
    }
  }
  LOG(LOG_INFO, "WLED: %zu known device(s) in DB", knownDevices->size());

  if (knownDevices->empty()) {
    // No stored devices — run discovery if enabled
    if (mAutoDiscovery) {
      performDiscovery(aCompletedCB);
    } else {
      if (aCompletedCB) aCompletedCB(ErrorPtr());
    }
    return;
  }

  // Reconnect all known devices; use a shared countdown to call aCompletedCB when all done.
  auto countdown = std::make_shared<int>((int)knownDevices->size());
  for (auto &kv : *knownDevices) {
    string hostname = kv.second;
    connectToDevice(hostname, [this, aCompletedCB, countdown, aRescanFlags](ErrorPtr err) {
      if (--(*countdown) == 0) {
        // After all known devices reconnected, optionally also discover new ones
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
  LOG(LOG_INFO, "WLED: connecting to device at '%s'", aHostname.c_str());

  // Create a temporary WledComm just for the discovery query.
  // The WledComm shared_ptr is captured in the lambda and lives until the callback fires.
  WledCommPtr comm = new WledComm();
  comm->setDeviceURL(string_format("http://%s", aHostname.c_str()));

  comm->getInfo([this, aHostname, comm, aCompletedCB](JsonObjectPtr aInfo, ErrorPtr aError) {
    if (Error::notOK(aError) || !aInfo) {
      LOG(LOG_WARNING, "WLED: cannot reach device at '%s': %s",
          aHostname.c_str(), aError ? aError->text() : "no info");
      if (aCompletedCB) aCompletedCB(aError ? aError : Error::err<WledCommError>(WledCommError::NoDeviceInfo));
      return;
    }

    // Extract MAC address as unique device key
    string mac;
    JsonObjectPtr macObj = aInfo->get("mac");
    if (macObj) mac = macObj->stringValue();
    if (mac.empty()) mac = aHostname; // fallback: use hostname as key

    // Persist the mapping — remove any placeholder row first (where mac == hostname),
    // then insert/replace the real entry
    mDb.prefixedExecute(
      "DELETE FROM $PREFIX_wledDevices WHERE mac = hostname AND hostname = '%q'",
      aHostname.c_str()
    );
    mDb.prefixedExecute(
      "INSERT OR REPLACE INTO $PREFIX_wledDevices (mac, hostname) VALUES ('%q', '%q')",
      mac.c_str(), aHostname.c_str()
    );

    // Create or update WledDevice
    auto it = mWledDevices.find(mac);
    if (it != mWledDevices.end()) {
      it->second->updateInfo(aInfo);
      LOG(LOG_INFO, "WLED: updated existing device mac=%s at %s", mac.c_str(), aHostname.c_str());
    } else {
      WledDevicePtr device = new WledDevice(this, aHostname, aInfo);
      mWledDevices[mac] = device;
      simpleIdentifyAndAddDevice(device);
      JsonObjectPtr n = aInfo->get("name");
      if (n) device->initializeName(n->stringValue());
      LOG(LOG_INFO, "WLED: added new device mac=%s at %s", mac.c_str(), aHostname.c_str());
    }

    if (aCompletedCB) aCompletedCB(ErrorPtr());
  });
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
        mDb.prefixedExecute(
          "DELETE FROM $PREFIX_wledDevices WHERE mac = '%q'",
          it->first.c_str()
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
    // Manually add/refresh a WLED device by hostname or IP
    if (!aParams) return ErrorPtr();
    ApiValuePtr hostnameParam = aParams->get("hostname");
    if (!hostnameParam) return ErrorPtr();

    string hostname = hostnameParam->stringValue();
    LOG(LOG_INFO, "WLED: setDeviceHostname called with '%s'", hostname.c_str());
    connectToDevice(hostname, NULL); // async, no completion needed
    return ErrorPtr();
  }

  return inherited::handleMethod(aRequest, aMethod, aParams);
}


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
  if (aCompletedCB) {
    aCompletedCB(Error::err<WledCommError>(WledCommError::DeviceNotFound));
  }
  #endif
}


bool WledVdc::handleDiscoveryHandler(ErrorPtr aError, DnsSdServiceInfoPtr aServiceInfo, StatusCB aCompletedCB)
{
  if (Error::notOK(aError)) {
    LOG(LOG_ERR, "WLED discovery error: %s", aError->text());
    if (aCompletedCB) aCompletedCB(aError);
    return false; // stop browsing on error
  }

  if (aServiceInfo && !aServiceInfo->disappeared) {
    string hostname = aServiceInfo->hostaddress;
    LOG(LOG_INFO, "WLED: DNS-SD found device at '%s'", hostname.c_str());

    connectToDevice(hostname, [this, aCompletedCB](ErrorPtr err) {
      // Signal aCompletedCB once the first successful device is added.
      // Browsing continues in parallel (we returned true below), so more devices
      // may arrive after aCompletedCB fires — that is intentional.
      if (aCompletedCB) aCompletedCB(ErrorPtr());
    });
  }

  return true; // continue browsing for additional WLED devices
}

#endif // ENABLE_WLED
