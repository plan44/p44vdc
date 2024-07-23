//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2015-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#define FOCUSLOGLEVEL IFNOTREDUCEDFOOTPRINT(7)

#include "discovery.hpp"
#include "macaddress.hpp"

#if !DISABLE_DISCOVERY

using namespace p44;


#define VDC_SERVICE_TYPE "_ds-vdc._tcp"
#define BRIDGE_SERVICE_TYPE "_p44-br._tcp"
#define HTTP_SERVICE_TYPE "_http._tcp"
#define SSH_SERVICE_TYPE "_ssh._tcp"

#define INITIAL_STARTUP_DELAY (8*Second) // how long to wait before trying to start avahi server for the first time


// MARK: - ServiceAnnouncer

static ServiceAnnouncer *sharedServiceAnnouncerP = NULL;

ServiceAnnouncer &ServiceAnnouncer::sharedServiceAnnouncer()
{
  if (!sharedServiceAnnouncerP) {
    sharedServiceAnnouncerP = new ServiceAnnouncer();
  }
  return *sharedServiceAnnouncerP;
}


ServiceAnnouncer::ServiceAnnouncer() :
  mNoAuto(false),
  mPublishWebPort(0),
  mPublishSshPort(0),
  mPublishBridgePort(0)
{
}


ServiceAnnouncer::~ServiceAnnouncer()
{
}


void ServiceAnnouncer::advertiseVdcHostDevice(
  const char *aHostname,
  VdcHostPtr aVdcHost,
  bool aNoAuto,
  int aWebPort, const string aWebPath,
  int aSshPort,
  int aBridgePort
) {
  ErrorPtr err = DnsSdManager::sharedDnsSdManager().initialize(aHostname, true);
  if (Error::notOK(err)) {
    LOG(LOG_ERR, "cannot initialize dns-sd.");
    return;
  }
  // basic init ok
  // - store the new params
  mVdcHost = aVdcHost;
  mNoAuto = aNoAuto;
  mPublishWebPort = aWebPort;
  mPublishWebPath = aWebPath;
  mPublishSshPort = aSshPort;
  mPublishBridgePort = aBridgePort;
  // - request service now
  DnsSdManager::sharedDnsSdManager().requestService(boost::bind(&ServiceAnnouncer::serviceCallback, this, _1), INITIAL_STARTUP_DELAY);
}


bool ServiceAnnouncer::serviceCallback(ErrorPtr aStatus)
{
  if (Error::isOK(aStatus)) {
    // successful (re)start of service
    DnsSdServiceGroupPtr sg = DnsSdManager::sharedDnsSdManager().newServiceGroup();
    if (!sg) {
      aStatus = TextError::err("cannot get service group");
    }
    else {
      // - register our network services now
      DnsSdServiceInfoPtr svc = DnsSdServiceInfoPtr(new DnsSdServiceInfo);
      svc->reset();
      svc->name = mVdcHost->publishedDescription();
      if (mPublishWebPort) {
        // web UI
        svc->type = HTTP_SERVICE_TYPE;
        svc->port = mPublishWebPort;
        svc->txtRecords.clear();
        if (!mPublishWebPath.empty()) svc->txtRecords["path"] = mPublishWebPath;
        aStatus = sg->addService(svc);
      }
      if (Error::isOK(aStatus) && mPublishSshPort) {
        // ssh access
        svc->type = SSH_SERVICE_TYPE;
        svc->port = mPublishSshPort;
        svc->txtRecords.clear();
        aStatus = sg->addService(svc);
      }
      if (Error::isOK(aStatus) && mVdcHost->mVdcApiServer) {
        // advertise the vdc API (for the vdsm to connect)
        int vdcPort = 0;
        sscanf(mVdcHost->mVdcApiServer->getPort(), "%d", &vdcPort);
        svc->type = VDC_SERVICE_TYPE;
        svc->port = vdcPort;
        svc->txtRecords.clear();
        svc->txtRecords["dSUID"] = mVdcHost->getDsUid().getString().c_str();
        if (mNoAuto) svc->txtRecords["noauto"] = "";
        aStatus = sg->addService(svc);
      }
      if (Error::isOK(aStatus) && mPublishBridgePort) {
        // advertise the bridge API (to allow main device to proxy our devices)
        svc->type = BRIDGE_SERVICE_TYPE;
        svc->port = mPublishBridgePort;
        svc->txtRecords.clear();
        aStatus = sg->addService(svc);
      }
    }
    if (Error::isOK(aStatus)) {
      sg->startAdvertising(boost::bind(&ServiceAnnouncer::advertisingCallback, this, _1));
      return true; // call me again in case service goes down and up later
    }
  }
  // something went wrong, restart service
  DnsSdManager::sharedDnsSdManager().restartServiceBecause(aStatus);
  return true; // keep calling back!
}


void ServiceAnnouncer::advertisingCallback(ErrorPtr aStatus)
{
  if (Error::isOK(aStatus)) {
    LOG(LOG_NOTICE, "discovery: successfully published services as '%s'.", mVdcHost->publishedDescription().c_str());
  }
  else {
    // something went wrong, restart service
    DnsSdManager::sharedDnsSdManager().restartServiceBecause(aStatus);
  }
}



void ServiceAnnouncer::refreshAdvertisingDevice()
{
  // environment has changed, so we should re-announce
  DnsSdManager::sharedDnsSdManager().restartService(0);
}

#endif // !DISABLE_DISCOVERY






