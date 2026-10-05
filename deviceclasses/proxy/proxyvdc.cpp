//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2024-2026 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "proxyvdc.hpp"
#include "proxydevice.hpp"

#if ENABLE_PROXYDEVICES

#include "jsonvdcapi.hpp"

#if ENABLE_LOCALCONTROLLER
#include "localcontroller.hpp"
#endif

using namespace p44;

// MARK: - Factory

#define P44_DEFAULT_BRIDGE_PORT 4444

// MARK: - DB

// Version history
//  1 : first version
#define PROXY_SCHEMA_MIN_VERSION 1 // minimally supported version, anything older will be deleted
#define PROXY_SCHEMA_VERSION 1 // current version

string ProxyPersistence::schemaUpgradeSQL(int aFromVersion, int &aToVersion)
{
  string sql;
  if (aFromVersion==0) {
    // create table group from scratch
    // - use standard globs table for schema version
    sql = inherited::schemaUpgradeSQL(aFromVersion, aToVersion);
    // - create my tables
    sql.append(
      "DROP TABLE IF EXISTS $PREFIX_proxytargets;"
      "CREATE TABLE $PREFIX_proxytargets ("
      " name TEXT,"
      " hostname TEXT,"
      " confirmed INTEGER"
      ");"
    );
    // reached final version in one step
    aToVersion = PROXY_SCHEMA_VERSION;
  }
  return sql;
}

/// Note: unlike many other vdc types, for proxies we have **multiple instances** of
///   ProxyVdc, one for each target. So the DB is **not** at the vdc level, but global
int ProxyVdc::mNextInstanceNumber = 0;
ProxyPersistence* ProxyVdc::mDbP = nullptr;

ProxyPersistence& ProxyVdc::sharedDb(VdcHost& aVdcHost)
{
  if (!mDbP) {
    mDbP = new ProxyPersistence;
    ErrorPtr err;
    err = mDbP->initialize(aVdcHost.getPersistence(), "proxy_vdcs_common", PROXY_SCHEMA_VERSION, PROXY_SCHEMA_MIN_VERSION, nullptr);
    if (Error::notOK(err)) {
      LOG(LOG_ERR, "Cannot access proxy DB: %s", Error::text(err));
    }
  }
  return *mDbP;
}



void ProxyVdc::instantiateProxies(const string aProxiesSpecification, VdcHost *aVdcHostP, int aTag)
{
  string proxyspec;
  const char* p = aProxiesSpecification.c_str();
  mNextInstanceNumber = 1;
  while(nextPart(p, proxyspec, ',')) {
    // found a proxy spec
    if (proxyspec=="dnssd") {
      #if PROXY_DNSSD_DISCOVERY
      LOG(LOG_INFO, "Starting DNS-SD proxy discovery");
      DnsSdManager::sharedDnsSdManager().browse("_p44-br._tcp", boost::bind(&ProxyVdc::p44BridgeApiDiscoveryHandler, _1, _2, aVdcHostP, aTag));
      #else
      LOG(LOG_ERR, "DNS-SD proxy discovery not available in this build")
      #endif
    }
    else {
      // must be a host[:port] specification
      string host;
      uint16_t port = P44_DEFAULT_BRIDGE_PORT;
      splitHost(proxyspec.c_str(), &host, &port);
      ProxyVdcPtr proxyVdc = ProxyVdcPtr(new ProxyVdc(mNextInstanceNumber, aVdcHostP, aTag));
      proxyVdc->setAPIParams(host, string_format("%u", port));
      proxyVdc->mConfirmed = true; // command line proxies are implicitly confirmed
      proxyVdc->addVdcToVdcHost();
      // count instance
      mNextInstanceNumber++;
    }
  }
}


#if PROXY_DNSSD_DISCOVERY

bool ProxyVdc::p44BridgeApiDiscoveryHandler(ErrorPtr aError, DnsSdServiceInfoPtr aServiceInfo, VdcHost *aVdcHostP, int aTag)
{
  if (Error::isOK(aError)) {
    if (!aServiceInfo->disappeared) {
      // is this a confirmed device?
      SQLiteTGQuery qry(sharedDb(*aVdcHostP));
      ErrorPtr err;
      err = qry.prefixedPrepare("SELECT ROWID, name, confirmed FROM $PREFIX_proxytargets WHERE hostname = '%q'", aServiceInfo->hostname.c_str());
      bool isConfirmed = false;
      long long int rowId = 0;
      if (Error::isOK(err)) {
        sqlite3pp::query::iterator i = qry.begin();
        if (i!=qry.end()) {
          // known hostname
          rowId = i->getWithDefault(0, 0);
          string name = nonNullCStr(i->get<const char *>(1));
          isConfirmed = i->getCastedWithDefault<bool, int>(2, false);
        }
        else {
          sharedDb(*aVdcHostP).prefixedExecute(
            "INSERT INTO $PREFIX_proxytargets (name, hostname, confirmed) VALUES ('%q', '%q', 0);",
            aServiceInfo->name.c_str(),
            aServiceInfo->hostname.c_str()
          );
          rowId = sharedDb(*aVdcHostP).db().last_insert_rowid();
        }
      }
      ProxyVdcPtr proxyVdc = ProxyVdcPtr(new ProxyVdc(mNextInstanceNumber, aVdcHostP, aTag));
      LOG(LOG_NOTICE,
        "Found %sCONFIRMED proxy '%s' at %s(%s):%d (instance: %d)",
        isConfirmed ? "" : "UN",
        aServiceInfo->name.c_str(), aServiceInfo->hostname.c_str(), aServiceInfo->hostaddress.c_str(), aServiceInfo->port,
        mNextInstanceNumber
      );
      proxyVdc->setAPIParams(aServiceInfo->hostaddress, string_format("%u", aServiceInfo->port));
      proxyVdc->mConfirmed = isConfirmed;
      if (!isConfirmed) {
        proxyVdc->setVdcError(Error::err<VdcError>(VdcError::NotConfirmed, "Needs confirming"));
      }
      proxyVdc->mRowId = rowId;
      proxyVdc->addVdcToVdcHost();
      mNextInstanceNumber++;
    }
  }
  else {
    LOG(LOG_INFO, "DNS-SD proxy discovery ends with: %s", Error::text(aError));
  }
  return true; // continue looking for devices
}

#endif // PROXY_DNSSD_DISCOVERY


// MARK: - initialisation


ProxyVdc::ProxyVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  Vdc(aInstanceNumber, aVdcHostP, aTag),
  mProxiedDSUID(false),
  mProxiedDeviceReached(false),
  mConfirmed(false),
  mRowId(0)
{
  mBridgeApi.isMemberVariable();
}


ProxyVdc::~ProxyVdc()
{
  // nop so far
}


void ProxyVdc::setAPIParams(const string aApiHost, const string aApiService)
{
  api().setConnectionParams(aApiHost.c_str(), aApiService.c_str(), SOCK_STREAM);
  api().setNotificationHandler(boost::bind(&ProxyVdc::bridgeApiNotificationHandler, this, _1, _2));
}


#define INITIALISATION_TIMEOUT (10*Second)

void ProxyVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  // try to connect to the bridge API
  OLOG(LOG_INFO, "Connecting to bridge API");
  mInitialisationCompleteCB = aCompletedCB;
  api().connectBridgeApi(boost::bind(&ProxyVdc::bridgeApiConnectedHandler, this, _1));
  mInitialisationTimeout.executeOnce(boost::bind(&ProxyVdc::initialisationTimeout, this), INITIALISATION_TIMEOUT);
}


void ProxyVdc::initialisationTimeout()
{
  initializeName("Timeout/Placeholder");
  OLOG(LOG_ERR, "Initialisation timeout for now - devices may appear later");
  ErrorPtr err = TextError::err("Proxy/Bridge API timeout");
  acknowledgeInitialisation(err);
}


void ProxyVdc::acknowledgeInitialisation(ErrorPtr aStatus)
{
  // load parameters
  // Note: in case this happens after initialisation, we must load again because we have the dSUID only now
  load();
  if (!getVdcFlag(vdcflag_flagsinitialized)) setVdcFlag(vdcflag_hidewhenempty, true); // hide by default
  updatePresenceState(mConfirmed);
  if (mInitialisationCompleteCB) {
    // initialisation has failed
    StatusCB cb = mInitialisationCompleteCB;
    mInitialisationCompleteCB = NoOP;
    cb(aStatus);
  }
}


ErrorPtr ProxyVdc::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="confirm") {
    // confirm (or revoke) operation of this ProxyVdc instance
    bool newConfirmed = true;
    ApiValuePtr a = aParams->get("revoke"); if (a) newConfirmed = !(a->boolValue());
    if (mConfirmed!=newConfirmed) {
      mConfirmed = newConfirmed;
      if (mRowId>0) {
        respErr = sharedDb(getVdcHost()).prefixedExecute(
          "UPDATE $PREFIX_proxytargets SET confirmed=%d WHERE ROWID=%lld",
          mConfirmed,
          mRowId
        );
        if (Error::notOK(respErr)) {
          OLOG(LOG_ERR, "Error updating proxy confirmed status %s", respErr->text());
        }
      }
      // act on change
      updatePresenceState(mConfirmed);
      if (mConfirmed) {
        // freshly confirmed: re-scan
        setVdcError(ErrorPtr()); // clear "not confirmed" error
        collectDevices(boost::bind(&ProxyVdc::confirmedAndCollected, this, aRequest, _1), rescanmode_normal);
        return ErrorPtr();
      }
      else {
        // revoke: remove devices including settings
        removeDevices(true);
        respErr = Error::ok();
      }
    }
  }
  else {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }
  return respErr;
}



void ProxyVdc::confirmedAndCollected(VdcApiRequestPtr aRequest, ErrorPtr aError)
{
  aRequest->sendStatus(Error::ok());
}




void ProxyVdc::bridgeApiConnectedHandler(ErrorPtr aStatus)
{
  mInitialisationTimeout.cancel();
  if (Error::notOK(aStatus)) {
    OLOG(LOG_WARNING, "bridge API connection error: %s", aStatus->text());
    acknowledgeInitialisation(aStatus);
  }
  else {
    // reset the bridge info in the remote device
    api().setProperty("root", "x-p44-bridge.bridgetype", JsonObject::newString("proxy"));
    api().setProperty("root", "x-p44-bridge.configURL", JsonObject::newString(getVdcHost().webuiURLString()));
    api().setProperty("root", "x-p44-bridge.started", JsonObject::newBool(true));
    // query for basic vdc identification
    JsonObjectPtr params = JsonObject::objFromText(
      "{ \"method\":\"getProperty\", \"dSUID\":\"root\", \"query\":{ "
        "\"dSUID\":null, \"model\":null, \"name\":null, \"x-p44-deviceHardwareId\":null, "
        "\"configURL\":null"
        #if ENABLE_LOCALCONTROLLER
        ", \"x-p44-localController\": { \"zones\": { \"\": null } }"
        #endif
      "}}"
    );
    api().call("getProperty", params, boost::bind(&ProxyVdc::bridgeApiIDQueryHandler, this, _1, _2));
  }
}


void ProxyVdc::bridgeApiIDQueryHandler(ErrorPtr aError, JsonObjectPtr aJsonMsg)
{
  JsonObjectPtr result;
  JsonObjectPtr o;
  FOCUSOLOG("bridgeapi ID query: status=%s, answer:\n%s", Error::text(aError), JsonObject::text(aJsonMsg));
  if (aJsonMsg && aJsonMsg->get("result", result)) {
    // global infos
    if (result->get("dSUID", o) && mDSUID.setAsString(o->stringValue())) {
      // differentiate proxy from original vdchost by setting subdevice index to 1 (original has always 0)
      mDSUID.setSubdeviceIndex(1);
      mProxiedDSUID = true;
    }
    else {
      aError = TextError::err("bridge API delivered no or invalid dSUID");
    }
    if (result->get("name", o)) {
      initializeName(o->stringValue());
    }
    if (result->get("x-p44-deviceHardwareId", o)) {
      mProxiedDeviceSerial = o->stringValue();
    }
    if (result->get("configURL", o)) {
      mProxiedDeviceConfigUrl = o->stringValue();
    }
    #if ENABLE_LOCALCONTROLLER
    // import (volatile) zone names from proxied controller
    LocalControllerPtr lc = VdcHost::sharedVdcHost()->getLocalController();
    if (lc && result->get("x-p44-localController", o)) {
      JsonObjectPtr zones;
      if (o->get("zones", zones)) {
        zones->resetKeyIteration();
        string zoneIDStr;
        JsonObjectPtr zone;
        while (zones->nextKeyValue(zoneIDStr, zone)) {
          DsZoneID zoneID = (DsZoneID)atoi(zoneIDStr.c_str());
          JsonObjectPtr o2;
          if (zone->get("name", o2)) {
            // for zones except global, import them with the remote's zone name, unless already existing
            if (zoneID!=0) lc->mLocalZones.getZoneById(zoneID, true, o2->c_strValue());
          }
        }
      }
    }
    #endif // ENABLE_LOCALCONTROLLER
    // reached once, got basic vdc info
    if (!mProxiedDeviceReached) {
      // we had not reached the proxy before, but are not initializing
      mProxiedDeviceReached = true;
      if (!mInitialisationCompleteCB) {
        // try to connect to the bridge API
        if (!mConfirmed) {
          OLOG(LOG_WARNING, "Proxy target P44 device %s (#%s) not yet confirmed -> not using its devices yet", getName().c_str(), mProxiedDeviceSerial.c_str());
          setVdcError(Error::err<VdcError>(VdcError::NotConfirmed, "Needs confirming"));
        }
        else {
          // we're not in initialisation any more, scan for devices now
          setVdcError(ErrorPtr()); // clear previous error, if any
          collectDevices(NoOP, rescanmode_incremental);
        }
      }
    }
  }
  // done initializing, (re)load persistent params
  acknowledgeInitialisation(aError);
}


string ProxyVdc::hardwareGUID() const
{
  return mProxiedDeviceSerial.empty() ? "" : string_format("p44serial:%s", mProxiedDeviceSerial.c_str());
}


void ProxyVdc::deriveDsUid()
{
  if (mProxiedDSUID) return; // we have the final dSUID, do not change it any more
  // in the meantime: use standard static method
  inherited::deriveDsUid();
}


const char *ProxyVdc::vdcClassIdentifier() const
{
  // note: unlike most other vdcs, the final dSUID is not generated based on this,
  //   but on the dSUID obtained from the proxied vdcd via bridge API
  // The class identifier is only for addressing by specifier
  return "Proxy_Device_Container";
}


string ProxyVdc::webuiURLString() const
{
  if (!mProxiedDeviceConfigUrl.empty())
    return mProxiedDeviceConfigUrl;
  else
    return inherited::webuiURLString();
}


bool ProxyVdc::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("vdc_proxy", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


void ProxyVdc::bridgeApiNotificationHandler(ErrorPtr aError, JsonObjectPtr aJsonMsg)
{
  if (Error::isOK(aError)) {
    OLOG(LOG_DEBUG, "bridge API message received: %s", JsonObject::text(aJsonMsg));
    // handle push notifications
    JsonObjectPtr o;
    string targetDSUID;
    if (aJsonMsg && aJsonMsg->get("dSUID", o, true)) {
      // request targets a device
      DsUid targetDSUID(o->stringValue());
      for (DeviceVector::iterator devpos = mDevices.begin(); devpos!=mDevices.end(); ++devpos) {
        ProxyDevicePtr dev = boost::static_pointer_cast<ProxyDevice>(*devpos);
        if (dev->getDsUid()==targetDSUID) {
          // device exists, dispatch
          if (aJsonMsg->get("notification", o, true)) {
            string notification = o->stringValue();
            POLOG(dev, LOG_INFO, "bridge notification '%s' received: %s", notification.c_str(), JsonObject::text(aJsonMsg));
            bool handled = dev->handleBridgedDeviceNotification(notification, aJsonMsg);
            if (handled) {
              POLOG(dev, LOG_INFO, "processed bridge notification");
            }
            else {
              POLOG(dev, LOG_ERR, "could not handle bridge notification '%s'", notification.c_str());
            }
          }
          else {
            POLOG(dev, LOG_ERR, "unknown bridge request for device");
          }
          // done with this notification
          return;
        }
      }
      // unknown DSUID
      // note: we do not check for changes in bridgeability
      //   (must issue a rescan to get newly bridged devices).
      //   So just warn getting notified for an unknown dSUID
      OLOG(LOG_WARNING, "request targeting unknown device %s - maybe need to scan for devices?", targetDSUID.getString().c_str());
    }
    else {
      // bridge level request
      if (aJsonMsg->get("notification", o, true)) {
        string notification = o->stringValue();
        OLOG(LOG_NOTICE, "bridge level notification '%s' received: %s", notification.c_str(), JsonObject::text(aJsonMsg));
        handleBridgeLevelNotification(notification, aJsonMsg);
      }
      else {
        OLOG(LOG_ERR, "unexpected bridge API message: %s", JsonObject::text(aJsonMsg));
      }
    }
  }
  else {
    OLOG(LOG_ERR, "bridge API Error %s", aError->text());
  }
}


bool ProxyVdc::handleBridgeLevelNotification(const string aNotification, JsonObjectPtr aParams)
{
  // none known so far
  return false;
}



int ProxyVdc::getRescanModes() const
{
  return rescanmode_incremental+rescanmode_normal;
}



bool ProxyVdc::isConfigured()
{
  // if we are connected, we're configured successfully
  return api().connected();
}



#define NEEDED_DEVICE_PROPERTIES \
  "{" \
    "\"dSUID\":null, \"name\":null, \"zoneID\": null, \"x-p44-zonename\": null, " \
    "\"primaryGroup\": null, " \
    "\"modelFeatures\":null, " \
    "\"scenes\": { \"0\":null, \"5\":null }, " \
    "\"vendorName\":null, \"model\":null, \"configURL\":null, " \
    "\"outputDescription\":null, \"outputSettings\": null, \"outputState\": null, " \
    "\"channelStates\":null, \"channelDescriptions\":null, " \
    "\"sensorDescriptions\":null, \"sensorSettings\":null, \"sensorStates\":null, " \
    "\"binaryInputDescriptions\":null, \"binaryInputSettings\":null, \"binaryInputStates\":null, " \
    "\"buttonInputDescriptions\":null, \"buttonInputSettings\":null, \"buttonInputStates\":null, " \
    "\"active\":null, " \
    "\"x-p44-bridgeable\":null, \"x-p44-bridged\":null, \"x-p44-bridgeAs\":null " \
  "}"


/// collect devices from this vDC
/// @param aCompletedCB will be called when device scan for this vDC has been completed
void ProxyVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  if (!mConfirmed) {
    OLOG(LOG_WARNING, "Proxy target P44 device not confirmed -> scanning devices disabled");
    aCompletedCB(Error::err<VdcError>(VdcError::NotConfirmed, "Needs confirming"));
    return;
  }
  if (!(aRescanFlags & rescanmode_incremental)) {
    // full collect, remove all devices
    removeDevices(aRescanFlags & rescanmode_clearsettings);
  }
  if (!mProxiedDeviceReached) {
    // we did not ever reach the API of the to-be-proxied device, meaning we cannot really scan now
    // - return error for now
    aCompletedCB(TextError::err("Proxied device not reachable"));
    return;
  }
  // query devices
  JsonObjectPtr params = JsonObject::objFromText(
    "{ \"method\":\"getProperty\", \"dSUID\":\"root\", \"query\":{ "
      "\"x-p44-vdcs\": { \"*\":{ \"x-p44-devices\": { \"*\": "
        NEEDED_DEVICE_PROPERTIES
      "} }}"
    "}}"
  );
  api().call("getProperty", params, boost::bind(&ProxyVdc::bridgeApiCollectQueryHandler, this, aCompletedCB, _1, _2));
}


void ProxyVdc::bridgeApiCollectQueryHandler(StatusCB aCompletedCB, ErrorPtr aError, JsonObjectPtr aJsonMsg)
{
  JsonObjectPtr result;
  JsonObjectPtr o;
  FOCUSOLOG("bridgeapi devices query: status=%s, answer:\n%s", Error::text(aError), JsonObject::text(aJsonMsg));
  if (aJsonMsg && aJsonMsg->get("result", result)) {
    // process device list
    JsonObjectPtr vdcs;
    // devices
    if (result->get("x-p44-vdcs", vdcs)) {
      vdcs->resetKeyIteration();
      string vn;
      JsonObjectPtr vdc;
      while(vdcs->nextKeyValue(vn, vdc)) {
        JsonObjectPtr devices;
        if (vdc->get("x-p44-devices", devices)) {
          devices->resetKeyIteration();
          string dn;
          JsonObjectPtr device;
          while(devices->nextKeyValue(dn, device)) {
            // examine device
            if (device->get("x-p44-bridgeable", o) && o->boolValue()) {
              // bridgeable device
              ProxyDevicePtr dev = addProxyDevice(device);
            }
          }
        }
      }
    }
  }
  // done collecting
  aCompletedCB(aError);
}


ProxyDevicePtr ProxyVdc::addProxyDevice(JsonObjectPtr aDeviceJSON)
{
  ProxyDevicePtr newDev;
  newDev = ProxyDevicePtr(new ProxyDevice(this, aDeviceJSON));
  // add to container if device was created
  if (newDev) {
    // add to container
    simpleIdentifyAndAddDevice(newDev);
  }
  return newDev;
}


// MARK: - operation


void ProxyVdc::deliverToDevicesAudience(DsAddressablesList aAudience, VdcApiConnectionPtr aApiConnection, const string &aNotification, ApiValuePtr aParams)
{
  // instead of having each proxied device issue its own call,
  // send as one notification with multiple target dSUIDs
  // Note: this keeps target vdc's ability to optimize calls
  JsonObjectPtr params = JsonApiValue::getAsJson(aParams);
  JsonObjectPtr targetDSUIDs = JsonObject::newArray();
  for (DsAddressablesList::iterator apos = aAudience.begin(); apos!=aAudience.end(); ++apos) {
    DevicePtr dev = boost::dynamic_pointer_cast<Device>(*apos);
    if (dev) {
      targetDSUIDs->arrayAppend(JsonObject::newString(dev->getDsUid().getString()));
      // also need to announce delivery for local zone tracking
      NotificationDeliveryStatePtr nds = createDeliveryState(aNotification, aParams, true);
      if (nds) {
        getVdcHost().deviceWillApplyNotification(dev, *nds); // let vdchost process for possibly updating global zone state
      }
    }
  }
  params->add("dSUID", targetDSUIDs);
  OLOG(LOG_INFO, "===== '%s' forwarding to %d proxy devices starts now: %s", aNotification.c_str(), targetDSUIDs->arrayLength(), JsonObject::text(params));
  api().notify(aNotification, params);
}


#endif // ENABLE_PROXYDEVICES

