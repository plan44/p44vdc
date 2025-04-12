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
  if (!container || (container->data[0]==0x34 && container->data[1]==0x04)) return 0;
  if (FOCUSLOGENABLED) {
    logContainer(FOCUSLOGLEVEL, *container, "received");
  }
  return 0;
}


// MARK: payload manipulation helpers

void Ds485Comm::payload_append8(string &aPayload, uint8_t aByte)
{
  aPayload.append(1, aByte);
}

void Ds485Comm::payload_append16(string &aPayload, uint16_t aWord)
{
  aPayload.append(1, ((aWord>>8) & 0xFF));
  aPayload.append(1, ((aWord>>0) & 0xFF));
}

void Ds485Comm::payload_append32(string &aPayload, uint32_t aLongWord)
{
  aPayload.append(1, ((aLongWord>>24) & 0xFF));
  aPayload.append(1, ((aLongWord>>16) & 0xFF));
  aPayload.append(1, ((aLongWord>>8) & 0xFF));
  aPayload.append(1, ((aLongWord>>0) & 0xFF));
}


void Ds485Comm::payload_appendString(string &aPayload, size_t aFieldSize, const string aString)
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


size_t Ds485Comm::payload_get8(string &aPayload, size_t aAtIndex, uint8_t &aByte)
{
  if (aAtIndex+1>aPayload.size()) return 0;
  aByte = (uint8_t)aPayload[aAtIndex++];
  return aAtIndex;
}


size_t Ds485Comm::payload_get16(string &aPayload, size_t aAtIndex, uint16_t &aWord)
{
  if (aAtIndex+2>aPayload.size()) return 0; // cannot happen except in error case
  aWord  = (uint16_t)(aPayload[aAtIndex++] & 0xFF)<<8;
  aWord |= (uint16_t)(aPayload[aAtIndex++] & 0xFF);
  return aAtIndex;
}


size_t Ds485Comm::payload_get32(string &aPayload, size_t aAtIndex, uint32_t &aLongWord)
{
  if (aAtIndex+4>aPayload.size()) return 0; // cannot happen except in error case
  aLongWord  = (uint32_t)(aPayload[aAtIndex++] & 0xFF)<<24;
  aLongWord |= (uint32_t)(aPayload[aAtIndex++] & 0xFF)<<16;
  aLongWord |= (uint32_t)(aPayload[aAtIndex++] & 0xFF)<<8;
  aLongWord |= (uint32_t)(aPayload[aAtIndex++] & 0xFF);
  return aAtIndex;
}


size_t Ds485Comm::payload_get64(string &aPayload, size_t aAtIndex, uint64_t &aLongLongWord)
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


size_t Ds485Comm::payload_getString(string &aPayload, size_t aAtIndex, size_t aFieldSize, string &aString)
{
  if (aAtIndex+aFieldSize>aPayload.size()) return 0; // cannot happen except in error case
  string f = aPayload.substr(aAtIndex, aFieldSize);
  aString = f.c_str(); // do not copy any garbage beyond terminator
  aAtIndex += aFieldSize;
  return aAtIndex;
}


// MARK: - container utilities

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


// MARK: - initialisation


void Ds485Comm::establishTunnel()
{
  string cmd = string_substitute(string_substitute(mTunnelCommandTemplate, "%PORT%", string_format("%d", mApiPort)), "%HOST%", mDs485HostIP);
  OLOG(LOG_INFO, "starting ssh tunnel: %s", cmd.c_str());
  mTunnelPid = MainLoop::currentMainLoop().fork_and_system(boost::bind(&Ds485Comm::tunnelCollapsed, this, _1, _2), cmd.c_str());
}


#define DS485_SSH_TUNNEL_RESTART_INTERVAL_S 60

void Ds485Comm::tunnelCollapsed(ErrorPtr aError, const string &aOutputString)
{
  OLOG(LOG_WARNING, "ssh tunnel error: %s - retrying in %d seconds", Error::text(aError), DS485_SSH_TUNNEL_RESTART_INTERVAL_S);
  mTunnelRestarter.executeOnce(boost::bind(&Ds485Comm::establishTunnel, this), DS485_SSH_TUNNEL_RESTART_INTERVAL_S*Second);
}



void Ds485Comm::setConnectionSpecification(const char *aConnectionSpec, uint16_t aDefaultPort, const char* aTunnelCommandTemplate)
{
  mApiPort = aDefaultPort;
  splitHost(aConnectionSpec, &mApiHost, &mApiPort);
  mDs485HostIP = mApiHost; // default to specified host (for debugging without actual dSS connected)
  mTunnelCommandTemplate = nonNullCStr(aTunnelCommandTemplate);
}


#define DS485_SSH_TUNNEL_STARTUP_WAIT_S 3

void Ds485Comm::start(StatusCB aCompletedCB)
{
  if (!mTunnelCommandTemplate.empty()) {
    establishTunnel();
    mConnectDelay.executeOnce(boost::bind(&Ds485Comm::connect, this, aCompletedCB), DS485_SSH_TUNNEL_STARTUP_WAIT_S*Second);
  }
  else {
    connect(aCompletedCB);
  }
}


void Ds485Comm::connect(StatusCB aCompletedCB)
{
  mDs485ClientThread = MainLoop::currentMainLoop().executeInThread(
    boost::bind(&Ds485Comm::ds485ClientThread, this, _1),
    boost::bind(&Ds485Comm::ds485ClientThreadSignal, this, _1, _2)
  );
  if (aCompletedCB) aCompletedCB(ErrorPtr());
}


void Ds485Comm::stop()
{
  if (mDs485ClientThread) {
    mDs485ClientThread->terminate();
    mDs485ClientThread.reset();
  }
  if (mTunnelPid>0) kill(mTunnelPid, SIGTERM);
}

#if DEBUG
  #define DS485_THREAD_RESTART_INTERVAL_S 2
#else
  #define DS485_THREAD_RESTART_INTERVAL_S 15
#endif

void Ds485Comm::ds485ClientThreadSignal(ChildThreadWrapper &aChildThread, ThreadSignals aSignalCode)
{
  OLOG(LOG_WARNING, "ds485ClientThread signals code=%d", aSignalCode);
}


// MARK: - blocking calls, only to use in ds485 client thread


#define DEFAULT_QUERY_TIMEOUT (2*Second)

ErrorPtr Ds485Comm::executeQuerySync(string &aResponse, MLMicroSeconds aTimeout, DsUidPtr aDestination, uint8_t aCommand, uint8_t aModifier, const string& aPayload)
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
  while(!aThread.shouldTerminate()) {
    mDs485Client = ds485_client_open2(
      string_format("tcp://%s:%d", mTunnelCommandTemplate.empty() ? mApiHost.c_str() : "127.0.0.1", mApiPort).c_str(),
      0 | PROMISCUOUS_MODE,
      &mDs485Callbacks
    );
    if (mDs485Client) break;
    // failed to open, retry later
    OLOG(LOG_WARNING, "ds485_client_open2 failed, retrying in %d seconds", DS485_THREAD_RESTART_INTERVAL_S);
    MainLoop::sleep(DS485_THREAD_RESTART_INTERVAL_S*Second);
  }
  // basic init
  // - get my own dSUID
  dsuid_t libDsuid;
  ds485_client_get_dsuid(mDs485Client, &libDsuid);
  mMyDsuid = new DsUid(libDsuid);
  OLOG(LOG_NOTICE, "library dSUID: %s", DsUid::text(mMyDsuid).c_str());
  // wait for calls from main thread
  aThread.crossThreadCallProcessor();
  // done, close the client
  ds485_client_close(mDs485Client);
  mDs485Client = nullptr;
}

#endif // ENABLE_DS485DEVICES

