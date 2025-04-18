//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2024 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

using namespace p44;

// MARK: - Factory

#define P44_DEFAULT_BRIDGE_PORT 4444

void ProxyVdc::instantiateProxies(const string aProxiesSpecification, VdcHost *aVdcHostP, int aTag)
{
  string proxyspec;
  const char* p = aProxiesSpecification.c_str();
  int instancenumber = 1;
  while(nextPart(p, proxyspec, ',')) {
    // found a proxy spec
    if (proxyspec=="dnssd") {
      // TODO: implement DNS-SD scanning
      LOG(LOG_ERR, "DNS-SD proxy discovery not yet implemented")
    }
    else {
      // must be a host[:port] specification
      string host;
      uint16_t port = P44_DEFAULT_BRIDGE_PORT;
      splitHost(proxyspec.c_str(), &host, &port);
      ProxyVdcPtr proxyVdc = ProxyVdcPtr(new ProxyVdc(instancenumber, aVdcHostP, aTag));
      proxyVdc->setAPIParams(host, string_format("%u", port));
      proxyVdc->addVdcToVdcHost();
      // count instance
      instancenumber++;
    }
  }
}



// MARK: - initialisation


ProxyVdc::ProxyVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  Vdc(aInstanceNumber, aVdcHostP, aTag),
  mProxiedDSUID(false),
  mProxiedDeviceReached(false)
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
  if (mInitialisationCompleteCB) {
    // initialisation has failed
    StatusCB cb = mInitialisationCompleteCB;
    mInitialisationCompleteCB = NoOP;
    cb(aStatus);
  }
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
      "\"configURL\":null, "
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
    // reached once, got basic vdc info
    if (!mProxiedDeviceReached) {
      // we had not reached the proxy before, but are not initializing
      mProxiedDeviceReached = true;
      if (!mInitialisationCompleteCB) {
        // we're not in initialisatin any more, scan for devices now
        setVdcError(ErrorPtr()); // clear previous error, if any
        collectDevices(NoOP, rescanmode_incremental);
      }
    }
  }
  // done initializing, (re)load persistent params
  acknowledgeInitialisation(aError);
}


string ProxyVdc::hardwareGUID()
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


string ProxyVdc::webuiURLString()
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


#define NEEDED_DEVICE_PROPERTIES \
  "{\"dSUID\":null, \"name\":null, \"zoneID\": null, \"x-p44-zonename\": null, " \
  "\"outputDescription\":null, \"outputSettings\": null, \"modelFeatures\":null, " \
  "\"scenes\": { \"0\":null, \"5\":null }, " \
  "\"vendorName\":null, \"model\":null, \"configURL\":null, " \
  "\"channelStates\":null, \"channelDescriptions\":null, " \
  "\"sensorDescriptions\":null, \"sensorStates\":null, " \
  "\"binaryInputDescriptions\":null, \"binaryInputStates\":null, " \
  "\"buttonInputDescriptions\":null, \"buttonInputStates\":null, " \
  "\"active\":null, " \
  "\"x-p44-bridgeable\":null, \"x-p44-bridged\":null, \"x-p44-bridgeAs\":null }"


/// collect devices from this vDC
/// @param aCompletedCB will be called when device scan for this vDC has been completed
void ProxyVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
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
    "} }} }}"
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

