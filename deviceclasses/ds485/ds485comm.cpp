//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "ds485comm.hpp"

#if ENABLE_DS485DEVICES

#include "dsuid.h"
//#include "dsm-api.h"
#include "dsm-api-const.h"

using namespace p44;

// MARK: - Ds485CommError

#if ENABLE_NAMED_ERRORS
const char* Ds485CommError::errorName() const
{
  return ds485c_strerror((int)getErrorCode());
}
#endif // ENABLE_NAMED_ERRORS

// MARK: - Ds485Comm

Ds485Comm::Ds485Comm() :
  mDs485Client(nullptr)
{
}


Ds485Comm::~Ds485Comm()
{
  stop();
}

// MARK: - ds485 client interaction

static int link_cb(void *_data, bool _state)
{
  Ds485Comm* dscomm = static_cast<Ds485Comm*>(_data);
  return dscomm->linkStateChanged(_state);
}

static int bus_change_cb(void *data, dsuid_t *id, int flags)
{
  Ds485Comm* dscomm = static_cast<Ds485Comm*>(data);
  return dscomm->busMemberChanged(new DsUid(*id), !flags);
}

static int container_cb(void *data, const ds485_container_t *container)
{
  Ds485Comm* dscomm = static_cast<Ds485Comm*>(data);
  return dscomm->containerReceived(container);
}

static int netlib_packet_cb(void *data, const ds485n_packet_t *packet)
{
  Ds485Comm* dscomm = static_cast<Ds485Comm*>(data);
  // we do not expect those when being connected to classic DS only
  POLOG(dscomm, LOG_WARNING, "netlib callback received!");
  return 0;
}

static void blocking_cb(void *data)
{
  Ds485Comm* dscomm = static_cast<Ds485Comm*>(data);
  FOCUSPOLOG(dscomm,"blocking callback received");
}


int Ds485Comm::linkStateChanged(bool aActive)
{
  FOCUSOLOG("link state: %s", aActive ? "ACTIVE" : "ISOLATED");
  return 0;
}


int Ds485Comm::busMemberChanged(DsUidPtr aDsUid, bool aJoined)
{
  FOCUSOLOG("bus: %s %s", DsUid::text(aDsUid).c_str(), aJoined ? "JOINED" : "LEFT");
  return 0;
}


int Ds485Comm::containerReceived(const ds485_container_t *container)
{
  // FIXME: do not show noisy metering
  if (!container || container->data[0]==0x34 && container->data[1]==0x04) return 0;
  if (FOCUSLOGENABLED) {
    logContainer(FOCUSLOGLEVEL, *container, "received");
  }
  return 0;
}

// MARK: - utilities

void Ds485Comm::logContainer(int aLevel, const ds485_container_t& container, const char *aLabel)
{
  if (LOGENABLED(aLevel)) {
    DsUidPtr source = new DsUid(container.sourceId);
    DsUidPtr destination = new DsUid(container.destinationId);
    OLOG(aLevel,
      "%s: %s%s (%d): %s -> %s, t=0x%02x: [%02d] %s",
      aLabel,
      container.containerFlags & DS485_FLAG_BROADCAST ? "BROADCAST " : "",
      container.containerType==DS485_CONTAINER_EVENT ? "EVENT   " : (container.containerType==DS485_CONTAINER_REQUEST ? "REQUEST " : "RESPONSE"), container.containerType,
      source->getString().c_str(),
      destination->getString().c_str(),
      container.transactionId,
      container.length,
      dataToHexString(container.data, container.length, ' ').c_str()
    );
  }
}


void payload_append8(string &aPayload, uint8_t aByte)
{
  aPayload.append(1, aByte);
}

void payload_append16(string &aPayload, uint16_t aWord)
{
  aPayload.append(1, ((aWord>>8) & 0xFF));
  aPayload.append(1, ((aWord>>0) & 0xFF));
}

void payload_append32(string &aPayload, uint32_t aLongWord)
{
  aPayload.append(1, ((aLongWord>>24) & 0xFF));
  aPayload.append(1, ((aLongWord>>16) & 0xFF));
  aPayload.append(1, ((aLongWord>>8) & 0xFF));
  aPayload.append(1, ((aLongWord>>0) & 0xFF));
}


void payload_appendString(string &aPayload, size_t aFieldSize, const string aString)
{
  size_t stringsize = aString.size();
  ssize_t padding = aFieldSize-stringsize;
  if (padding<1) {
    stringsize = aFieldSize-1;
    padding = 1;
  }
  aPayload.append(aString.c_str(), stringsize);
  aPayload.append(padding, 0);
}


size_t payload_get8(string &aPayload, size_t aAtIndex, uint8_t &aByte)
{
  if (aAtIndex+1>aPayload.size()) return 0;
  aByte = (uint8_t)aPayload[aAtIndex++];
  return aAtIndex;
}


size_t payload_get16(string &aPayload, size_t aAtIndex, uint16_t &aWord)
{
  if (aAtIndex+2>aPayload.size()) return 0; // cannot happen except in error case
  aWord  = (uint16_t)(aPayload[aAtIndex++] & 0xFF)<<8;
  aWord |= (uint16_t)(aPayload[aAtIndex++] & 0xFF);
  return aAtIndex;
}


size_t payload_get32(string &aPayload, size_t aAtIndex, uint32_t &aLongWord)
{
  if (aAtIndex+4>aPayload.size()) return 0; // cannot happen except in error case
  aLongWord  = (uint32_t)(aPayload[aAtIndex++] & 0xFF)<<24;
  aLongWord |= (uint32_t)(aPayload[aAtIndex++] & 0xFF)<<16;
  aLongWord |= (uint32_t)(aPayload[aAtIndex++] & 0xFF)<<8;
  aLongWord |= (uint32_t)(aPayload[aAtIndex++] & 0xFF);
  return aAtIndex;
}


size_t payload_get64(string &aPayload, size_t aAtIndex, uint64_t &aLongLongWord)
{
  if (aAtIndex+8>aPayload.size()) return 0; // cannot happen except in error case
  aLongLongWord  = (uint64_t)(aPayload[aAtIndex++] & 0xFF)<<56;
  aLongLongWord |= (uint64_t)(aPayload[aAtIndex++] & 0xFF)<<48;
  aLongLongWord |= (uint64_t)(aPayload[aAtIndex++] & 0xFF)<<40;
  aLongLongWord |= (uint64_t)(aPayload[aAtIndex++] & 0xFF)<<32;
  aLongLongWord |= (uint64_t)(aPayload[aAtIndex++] & 0xFF)<<24;
  aLongLongWord |= (uint64_t)(aPayload[aAtIndex++] & 0xFF)<<16;
  aLongLongWord |= (uint64_t)(aPayload[aAtIndex++] & 0xFF)<<8;
  aLongLongWord |= (uint64_t)(aPayload[aAtIndex++] & 0xFF);
  return aAtIndex;
}


size_t payload_getString(string &aPayload, size_t aAtIndex, size_t aFieldSize, string &aString)
{
  if (aAtIndex+aFieldSize>aPayload.size()) return 0; // cannot happen except in error case
  string f = aPayload.substr(aAtIndex, aFieldSize);
  aString = f.c_str(); // do not copy any garbage beyond terminator
  aAtIndex += aFieldSize;
  return aAtIndex;
}



void Ds485Comm::setupRequestContainer(ds485_container& aContainer, DsUidPtr aDestination, DsUidPtr aSource, const string aPayload)
{
  // clear everything
  memset(&aContainer, 0, sizeof(ds485_container));
  // destination: if passed null, this is a broadcast
  if (!aDestination) {
    // broadcast
    aContainer.destinationId = DSUID_BROADCAST;
    aContainer.containerFlags = DS485_FLAG_BROADCAST;
  }
  else {
    aDestination->copyAsDs485DsUid(aContainer.destinationId);
    aContainer.containerFlags = DS485_FLAG_NONE;
  }
  // source: if passed null, use my own dSUID
  if (!aSource) {
    if (mMyDsuid) mMyDsuid->copyAsDs485DsUid(aContainer.sourceId);
  }
  else {
    aSource->copyAsDs485DsUid(aContainer.sourceId);
  }
  // this is a request
  aContainer.containerType = DS485_CONTAINER_REQUEST;
  // TODO: figure out what transaction ID we should use
  // See remarks in ds485-stack/ds485-netlib/src/ds485-socket-server-clients.c line 122ff
  // - basically, ds485p uses the upper 4 bits, ds485d the lower 4 bits
  aContainer.transactionId = 0x42; // just arbitrary, fun, hopefully not clashing (we only see 0x10 being used so far)
  // payload
  aContainer.length = aPayload.size();
  memcpy(aContainer.data, aPayload.c_str(), aPayload.size());
}


void Ds485Comm::setupRequestCommand(ds485_container& aContainer, DsUidPtr aDestination, uint8_t aCommand, uint8_t aModifier, const string& aPayload)
{
  string payload;
  payload_append8(payload, aCommand);
  payload_append8(payload, aModifier);
  payload.append(aPayload);
  setupRequestContainer(aContainer, aDestination, DsUidPtr(), payload);
}


#define DEFAULT_QUERY_TIMEOUT (2*Second)

ErrorPtr Ds485Comm::executeQuery(string &aResponse, MLMicroSeconds aTimeout, DsUidPtr aDestination, uint8_t aCommand, uint8_t aModifier, const string& aPayload)
{
  if (aTimeout==0) aTimeout = DEFAULT_QUERY_TIMEOUT;
  ds485_container request;
  ds485_container response;
  setupRequestCommand(request, aDestination, aCommand, aModifier, aPayload);
  if (FOCUSLOGENABLED) {
    logContainer(FOCUSLOGLEVEL, request, "executeQuery sends:");
  }
  ErrorPtr err = Error::errIfNotOk<Ds485CommError>(ds485_client_send_sync_command(mDs485Client, &request, &response, (int)(aTimeout/Second)));
  if (Error::isOK(err)) {
    if (FOCUSLOGENABLED) {
      logContainer(FOCUSLOGLEVEL, response, "executeQuery response:");
    }
    aResponse.assign((const char*)response.data, response.length);
  }
  return err;
}



// MARK: - initialisation

void Ds485Comm::setConnectionSpecification(const char *aConnectionSpec, uint16_t aDefaultPort)
{
  string host;
  uint16_t port = aDefaultPort;
  splitHost(aConnectionSpec, &host, &port);
  mConnSpec = string_format("tcp://%s:%d", host.c_str(), port);
}


void Ds485Comm::start(StatusCB aCompletedCB)
{
  mDs485ClientThread = MainLoop::currentMainLoop().executeInThread(
    boost::bind(&Ds485Comm::ds485ClientThread, this, _1),
    boost::bind(&Ds485Comm::ds485ClientThreadSignal, this, _1, _2)
  );
  if (aCompletedCB) {
    aCompletedCB(ErrorPtr());
  }
}


void Ds485Comm::stop()
{
  if (mDs485ClientThread) {
    mDs485ClientThread->terminate();
    mDs485ClientThread.reset();
  }
}

#if DEBUG
  #define DS485_THREAD_RESTART_INTERVAL (2*Second)
#else
  #define DS485_THREAD_RESTART_INTERVAL (15*Second)
#endif

void Ds485Comm::ds485ClientThreadSignal(ChildThreadWrapper &aChildThread, ThreadSignals aSignalCode)
{
  FOCUSOLOG("ds485ClientThread signals code=%d", aSignalCode);
  if (aSignalCode<threadSignalUserSignal) {
    // some sort of thread termination
    OLOG(LOG_WARNING, "ds485 client thread terminated, restarting in %lld seconds", DS485_THREAD_RESTART_INTERVAL/Second);
    mDs485ThreadRestarter.executeOnce(boost::bind(&Ds485Comm::start, this, StatusCB()), DS485_THREAD_RESTART_INTERVAL);
  }
}


// MARK: - ds485 client thread





void Ds485Comm::ds485ClientThread(ChildThreadWrapper &aThread)
{
  // set up callbacks
  memset(&mDs485Callbacks, 0, sizeof(mDs485Callbacks));
  mDs485Callbacks.link_cb = link_cb;
  mDs485Callbacks.link_data = this;
  mDs485Callbacks.bus_change_cb = bus_change_cb;
  mDs485Callbacks.bus_change_data = this;
  mDs485Callbacks.container_pkt_cb = container_cb;
  mDs485Callbacks.container_pkt_data = this;
  mDs485Callbacks.netlib_pkt_cb = netlib_packet_cb;
  mDs485Callbacks.netlib_pkt_data = this;
  mDs485Callbacks.blocking_cb = blocking_cb;
  mDs485Callbacks.blocking_data = this;
  // now start the client
  mDs485Client = ds485_client_open2(
    mConnSpec.c_str(),
    0 | PROMISCUOUS_MODE,
    &mDs485Callbacks
  );
  if (mDs485Client) {
    // startup, collect info
    // - my own dSUID
    dsuid_t libDsuid;
    ds485_client_get_dsuid(mDs485Client, &libDsuid);
    mMyDsuid = new DsUid(libDsuid);
    OLOG(LOG_NOTICE, "library: %s", DsUid::text(mMyDsuid).c_str());
    // - the bus devices
    const int maxbusdevices = 64;
    dsuid_t busdevices[maxbusdevices];
    int numDsms = ds485_client_query_devices(mDs485Client, busdevices, maxbusdevices);
    // iterate dSMs
    for (int di=0; di<numDsms; di++) {
      DsUidPtr dsmDsuid = new DsUid(busdevices[di]);
      OLOG(LOG_NOTICE, "dSM #%d: %s", di, dsmDsuid->getString().c_str());
      // prevent asking myself
      if (*dsmDsuid!=*mMyDsuid) {
        string resp;
        size_t pli;
        // - get the dSM info
        executeQuery(resp, 0, dsmDsuid, DSM_INFO);
        pli = 3;
        uint32_t dsmHwVersion;
        pli = payload_get32(resp, pli, dsmHwVersion);
        uint32_t dsmArmVersion;
        pli = payload_get32(resp, pli, dsmArmVersion);
        uint32_t dsmDSPVersion;
        pli = payload_get32(resp, pli, dsmDSPVersion);
        uint16_t dsmAPIVersion;
        pli = payload_get16(resp, pli, dsmAPIVersion);
        pli += 12; // skip "dSID"
        string dsmName;
        pli = payload_getString(resp, pli, 21, dsmName);
        OLOG(LOG_NOTICE, "dSM #%d: '%s', hwV=0x%08x, armV=0x%08x, dspV=0x%08x, apiV=0x%04x", di, dsmName.c_str(), dsmHwVersion, dsmArmVersion, dsmDSPVersion, dsmAPIVersion);
        // - get the zone count
        executeQuery(resp, 0, dsmDsuid, ZONE_COUNT);
        pli = 3;
        uint8_t zoneCount;
        pli = payload_get8(resp, pli, zoneCount);
        OLOG(LOG_NOTICE, "dSM #%d: has %d zones", di, zoneCount);
        // - the zones
        for (int i=0; i<zoneCount; i++) {
          string req;
          payload_append8(req, i);
          executeQuery(resp, 0, dsmDsuid, ZONE_INFO, ZONE_INFO_BY_INDEX, req);
          size_t pli;
          pli = 3;
          uint16_t zoneId;
          pli = payload_get16(resp, pli, zoneId);
          uint8_t vzoneId;
          pli = payload_get8(resp, pli, vzoneId);
          uint8_t numGroups;
          pli = payload_get8(resp, pli, numGroups);
          string zonename;
          pli = payload_getString(resp, pli, 21, zonename);
          OLOG(LOG_NOTICE, "zone #%d: id=%d, virtid=%d, numgroups=%d, name='%s'", i, zoneId, vzoneId, numGroups, zonename.c_str());
          // - the devices in the zone
          req.clear();
          payload_append16(req, zoneId);
          executeQuery(resp, 0, dsmDsuid, ZONE_DEVICE_COUNT, ZONE_DEVICE_COUNT_ALL, req);
          uint16_t numZoneDevices;
          pli = 3;
          pli = payload_get16(resp, pli, numZoneDevices);
          OLOG(LOG_NOTICE, "zone #%d: number of devices = %d", i, numZoneDevices);
          for (int j=0; j<numZoneDevices; j++) {
            string req;
            payload_append16(req, zoneId);
            payload_append16(req, j);
            executeQuery(resp, 0, dsmDsuid, DEVICE_INFO, DEVICE_INFO_BY_INDEX, req);
            pli = 3;
            uint16_t devId;
            pli = payload_get16(resp, pli, devId);
            uint16_t vendId;
            pli = payload_get16(resp, pli, vendId);
            uint16_t prodId;
            pli = payload_get16(resp, pli, prodId);
            uint16_t funcId;
            pli = payload_get16(resp, pli, funcId);
            uint16_t vers;
            pli = payload_get16(resp, pli, vers);
            uint16_t zoneId;
            pli = payload_get16(resp, pli, zoneId);
            uint8_t active;
            pli = payload_get8(resp, pli, active);
            uint8_t locked;
            pli = payload_get8(resp, pli, locked);
            uint8_t outMode;
            pli = payload_get8(resp, pli, outMode);
            uint8_t ltMode;
            pli = payload_get8(resp, pli, ltMode);
            uint64_t groups;
            pli = payload_get64(resp, pli, groups);
            string devName;
            pli = payload_getString(resp, pli, 21, devName);
            DsUidPtr dSUID = new DsUid;
            dSUID->setAsBinary(resp.substr(pli, 17)); pli += 17;
            uint8_t activeGroup;
            pli = payload_get8(resp, pli, activeGroup);
            uint8_t defaultGroup;
            pli = payload_get8(resp, pli, defaultGroup);
            OLOG(LOG_NOTICE,
              "device #%d: %s [0x%04x] - '%s'\n"
              "- vendId=0x%04x, prodId=0x%04x, funcId=0x%04x, vers=0x%04x\n"
              "- zoneID=%d/0x%04x, active=%d, locked=%d\n"
              "- outMode=0x%04x, ltMode=0x%04x\n"
              "- groups=0x%016llx, activeGroup=%d, defaultGroup=%d",
              j, dSUID->getString().c_str(), devId, devName.c_str(),
              vendId, prodId, funcId, vers,
              zoneId, zoneId, active, locked,
              outMode, ltMode,
              groups, activeGroup, defaultGroup
            );
            // - button info
            req.clear();
            payload_append16(req, devId);
            executeQuery(resp, 0, dsmDsuid, DEVICE_BUTTON_INFO, DEVICE_BUTTON_INFO_BY_DEVICE, req);
            pli = 3;
            uint8_t buttonId;
            pli = payload_get8(resp, pli, buttonId);
            pli++; // skip "DeprecatedGroupIfUpTo15"
            uint8_t buttongroup;
            pli = payload_get8(resp, pli, buttongroup);
            uint8_t buttonflags;
            pli = payload_get8(resp, pli, buttonflags);
            uint8_t buttonchannel;
            pli = payload_get8(resp, pli, buttonchannel);
            pli++; // skip "unused"
            OLOG(LOG_NOTICE,
              "device #%d '%s': button: id/LTNUMGRP0=0x%02x, group=%d, flags=0x%02x, channel=%d",
              j, devName.c_str(),
              buttonId, buttongroup, buttonflags, buttonchannel
            );
            // - binary input info
            req.clear();
            payload_append16(req, devId);
            executeQuery(resp, 0, dsmDsuid, DEVICE_BINARY_INPUT, DEVICE_BINARY_INPUT_GET_COUNT, req);
            pli = 3;
            uint8_t numBinInps;
            pli = payload_get8(resp, pli, numBinInps);
            OLOG(LOG_NOTICE, "device #%d: number of binary inputs = %d", j, numBinInps);
            for (int bi=0; bi<numBinInps; bi++) {
              // - binary input info
              req.clear();
              payload_append16(req, devId);
              payload_append8(req, bi);
              executeQuery(resp, 0, dsmDsuid, DEVICE_BINARY_INPUT, DEVICE_BINARY_INPUT_GET_BY_INDEX, req);
              pli = 3;
              uint8_t inpTargetGroupType;
              pli = payload_get8(resp, pli, inpTargetGroupType);
              uint8_t inpTargetGroup;
              pli = payload_get8(resp, pli, inpTargetGroup);
              uint8_t inpType;
              pli = payload_get8(resp, pli, inpType);
              uint8_t inpButtonId;
              pli = payload_get8(resp, pli, inpButtonId);
              uint8_t inpIndependent;
              pli = payload_get8(resp, pli, inpIndependent);
              OLOG(LOG_NOTICE,
                "- device #%d: binary input #%d: targetGroupType=%d, targetGroup=%d, type=%d, buttonId=0x%02x, independent=%d",
                j, bi, inpTargetGroupType, inpTargetGroup, inpType, inpButtonId, inpIndependent
              );
            }
            // - output channel info
            req.clear();
            payload_append16(req, devId);
            executeQuery(resp, 0, dsmDsuid, DEVICE_O_P_C_TABLE, DEVICE_O_P_C_TABLE_GET_COUNT, req);
            pli = 3;
            uint8_t numChannels;
            pli = payload_get8(resp, pli, numChannels);
            OLOG(LOG_NOTICE, "device #%d: number of OPC channels = %d", j, numChannels);
            for (int oi=0; oi<numBinInps; oi++) {
              // - OPC channel info
              req.clear();
              payload_append16(req, devId);
              payload_append8(req, oi);
              executeQuery(resp, 0, dsmDsuid, DEVICE_O_P_C_TABLE, DEVICE_O_P_C_TABLE_GET_BY_INDEX, req);
              pli = 3;
              uint8_t channelId;
              pli = payload_get8(resp, pli, channelId);
              OLOG(LOG_NOTICE, "device #%d: channel #%d: channelId=%d", j, oi, channelId);
            }
            // - sensor info
            req.clear();
            payload_append16(req, devId);
            executeQuery(resp, 0, dsmDsuid, DEVICE_SENSOR, DEVICE_SENSOR_GET_COUNT, req);
            pli = 3;
            uint8_t numSensors;
            pli = payload_get8(resp, pli, numSensors);
            OLOG(LOG_NOTICE, "device #%d: number of sensors = %d", j, numSensors);
            for (int si=0; si<numSensors; si++) {
              // - sensor info
              req.clear();
              payload_append16(req, devId);
              payload_append8(req, si);
              executeQuery(resp, 0, dsmDsuid, DEVICE_SENSOR, DEVICE_SENSOR_GET_BY_INDEX, req);
              pli = 3;
              uint8_t sensorType;
              pli = payload_get8(resp, pli, sensorType);
              uint32_t sensorPollinterval;
              pli = payload_get32(resp, pli, sensorPollinterval);
              uint8_t sensorZone;
              pli = payload_get8(resp, pli, sensorZone);
              uint8_t sensorPushConvert;
              pli = payload_get8(resp, pli, sensorPushConvert);
              OLOG(LOG_NOTICE,
                "device #%d: sensor #%d: type=%d, pollinterval=%d, globalZone=%d, pushConvert=%d",
                j, si, sensorType, sensorPollinterval, sensorZone, sensorPushConvert
              );
            }
          }
        }
      }
    }

    while(!aThread.shouldTerminate()) {
      // TODO: process main thread requests
      MainLoop::sleep(0.5*Second);
    }
    ds485_client_close(mDs485Client);
  }
  mDs485Client = nullptr;
}


#endif // ENABLE_DS485DEVICES

