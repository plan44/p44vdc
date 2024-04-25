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
#define FOCUSLOGLEVEL 6

#include "proxyvdc.hpp"
#include "proxydevice.hpp"

#if ENABLE_PROXYDEVICES

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
  mProxiedDSUID(false)
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


void ProxyVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  // try to connect to the bridge API
  OLOG(LOG_INFO, "Connecting to bridge API");
  api().connectBridgeApi(boost::bind(&ProxyVdc::bridgeApiConnectedHandler, this, aCompletedCB, _1));
}


void ProxyVdc::bridgeApiConnectedHandler(StatusCB aCompletedCB, ErrorPtr aStatus)
{
  if (Error::notOK(aStatus)) {
    OLOG(LOG_WARNING, "bridge API connection error: %s", aStatus->text());
    aCompletedCB(aStatus); // init failed
  }
  else {
    // query for basic vdc identification
    JsonObjectPtr params = JsonObject::objFromText(
      "{ \"method\":\"getProperty\", \"dSUID\":\"root\", \"query\":{ "
      "\"dSUID\":null, \"model\":null, \"name\":null, \"x-p44-deviceHardwareId\":null, "
      "\"configURL\":null, "
      "}}"
    );
    api().call("getProperty", params, boost::bind(&ProxyVdc::bridgeApiIDQueryHandler, this, aCompletedCB, _1, _2));
  }
}


void ProxyVdc::bridgeApiIDQueryHandler(StatusCB aCompletedCB, ErrorPtr aError, JsonObjectPtr aJsonMsg)
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
    // dSUID is now final: load persistent params
    load();
  }
  // done initializing
  aCompletedCB(aError);
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
  // TODO: process relevant notifications
  // - output channel changes? maybe not needed, who would want to know? WebUI polls...
  // - input changes! Buttons, sensors, binaryinputs
  OLOG(LOG_INFO, "received bridge notification: %s", JsonObject::text(aJsonMsg));
}



int ProxyVdc::getRescanModes() const
{
  return rescanmode_incremental+rescanmode_normal;
}



/// collect devices from this vDC
/// @param aCompletedCB will be called when device scan for this vDC has been completed
void ProxyVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  // incrementally collecting configured devices makes no sense. The devices are "static"!
//  if (!(aRescanFlags & rescanmode_incremental)) {
//    // non-incremental, re-collect all devices
//    removeDevices(aRescanFlags & rescanmode_clearsettings);
//    // add from the DB
//    sqlite3pp::query qry(mDb);
//    if (qry.prepare("SELECT bridgeDeviceId, config, rowid FROM bridgedevices")==SQLITE_OK) {
//      for (sqlite3pp::query::iterator i = qry.begin(); i != qry.end(); ++i) {
//        BridgeDevicePtr dev = BridgeDevicePtr(new BridgeDevice(this, i->get<string>(0), i->get<string>(1)));
//        if (dev) {
//          dev->mBridgeDeviceRowID = i->get<int>(2);
//          simpleIdentifyAndAddDevice(dev);
//        }
//      }
//    }
//  }
  // assume ok
  aCompletedCB(ErrorPtr());
}


#endif // ENABLE_PROXYDEVICES

