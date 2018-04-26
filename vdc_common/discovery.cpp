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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 7

#include "discovery.hpp"

#include "macaddress.hpp"

#if !DISABLE_DISCOVERY

using namespace p44;


#define VDC_SERVICE_TYPE "_ds-vdc._tcp"
#define DS485P_SERVICE_TYPE "_ds-ds485p._tcp"
#define DSS_SERVICE_TYPE "_dssweb._tcp"
#define HTTP_SERVICE_TYPE "_http._tcp"
#define SSH_SERVICE_TYPE "_ssh._tcp"

#define VDSM_VDC_ROLE_NOAUTO "noauto"

#define INITIAL_STARTUP_DELAY (8*Second) // how long to wait before trying to start avahi server for the first time
#define STARTUP_RETRY_DELAY (30*Second) // how long to wait before retrying to start avahi server when failed because of missing network
#define SERVICES_RESTART_DELAY (2*Minute) // how long to wait before restarting the server (after a problem that caused calling restartServer())

// MARK: ===== DiscoveryManager

static DiscoveryManager *sharedDiscoveryManagerP = NULL;

DiscoveryManager &DiscoveryManager::sharedDiscoveryManager()
{
  if (!sharedDiscoveryManagerP) {
    sharedDiscoveryManagerP = new DiscoveryManager();
  }
  return *sharedDiscoveryManagerP;
}


DiscoveryManager::DiscoveryManager() :
  simple_poll(NULL),
  service(NULL),
  serviceBrowser(NULL),
  dSEntryGroup(NULL),
  debugServiceBrowser(NULL),
  dmState(dm_disabled),
  noAuto(false),
  publishWebPort(0),
  publishSshPort(0),
  pollTicket(0)
{
  // register a cleanup handler
  MainLoop::currentMainLoop().registerCleanupHandler(boost::bind(&DiscoveryManager::stop, this));
  #if USE_AVAHI_CORE
  // route avahi logs to our own log system
  avahi_set_log_function(&DiscoveryManager::avahi_log);
  #endif
}


void DiscoveryManager::avahi_log(AvahiLogLevel level, const char *txt)
{
  // show all avahi log stuff only when we have focus
  FOCUSLOG("avahi(%d): %s", level, txt);
}


DiscoveryManager::~DiscoveryManager()
{
  // full stop
  stop();
}


void DiscoveryManager::stop()
{
  // unregister idle handler
  MainLoop::currentMainLoop().cancelExecutionTicket(pollTicket);
  // stop service
  stopService();
  // stop polling
  if (simple_poll) {
    avahi_simple_poll_quit(simple_poll);
    avahi_simple_poll_free(simple_poll);
    simple_poll = NULL;
  }
}

#define AVAHI_POLL_INTERVAL (30*MilliSecond)
#define AVAHI_POLL_TOLERANCE (15*MilliSecond)


ErrorPtr DiscoveryManager::start(
  const char *aHostname
) {
  ErrorPtr err;

  // stop current operation
  stop();
  // set the hostname
  hostname = aHostname;
  // allocate the simple-poll object
  if (!(simple_poll = avahi_simple_poll_new())) {
    err = TextError::err("Avahi: Failed to create simple poll object.");
  }
  if (Error::isOK(err)) {
    // start polling
    MainLoop::currentMainLoop().executeTicketOnce(pollTicket, boost::bind(&DiscoveryManager::avahi_poll, this, _1));
    // prepare service
    MainLoop::currentMainLoop().executeOnce(boost::bind(&DiscoveryManager::startService, this), INITIAL_STARTUP_DELAY);
  }
  return err;
}



void DiscoveryManager::avahi_poll(MLTimer &aTimer)
{
  if (simple_poll) {
    avahi_simple_poll_iterate(simple_poll, 0);
  }
  // schedule next execution
  MainLoop::currentMainLoop().retriggerTimer(aTimer, AVAHI_POLL_INTERVAL, AVAHI_POLL_TOLERANCE);
}



// MARK: ===== Basic service

void DiscoveryManager::startService()
{
  // only start if not already started, and only if vdc host has been set
  if (!service) {
    // if device has no network connection (no IP) yet, starting avahi makes no sense - delay it (again)
    // Note: network connection cannot be checked if vdchost has not yet been set at this point
    if (vdcHost && !vdcHost->isNetworkConnected()) {
      // TODO: checking IPv4 only at this time, need to add IPv6 later
      LOG(LOG_WARNING, "discovery: device has no IP address -> retry later");
      MainLoop::currentMainLoop().executeOnce(boost::bind(&DiscoveryManager::startService, this), STARTUP_RETRY_DELAY);
      return;
    }
    #if USE_AVAHI_CORE
    // single avahi instance for embedded use, no other process uses avahi
    LOG(LOG_NOTICE, "avahi: starting service");
    int avahiErr;
    AvahiServerConfig config;
    avahi_server_config_init(&config);
    // basic info
    config.host_name = avahi_strdup(hostname.c_str()); // unique hostname
    #ifdef __APPLE__
    config.disallow_other_stacks = 0; // om OS X, we always have a mDNS, so allow more than one for testing
    #else
    config.disallow_other_stacks = 1; // we wants to be the only mdNS (also avoids problems with SO_REUSEPORT on older Linux kernels)
    #endif
    // TODO: IPv4 only at this time!
    config.use_ipv4 = 1;
    config.use_ipv6 = 0; // NONE
    // publishing options
    config.publish_aaaa_on_ipv4 = 0; // prevent publishing IPv6 AAAA record on IPv4
    config.publish_a_on_ipv6 = 0; // prevent publishing IPv4 A on IPV6
    config.publish_hinfo = 0; // no CPU specifics
    config.publish_addresses = 1; // publish addresses
    config.publish_workstation = 0; // no workstation
    config.publish_domain = 1; // announce the local domain for browsing
    // create server with prepared config
    service = avahi_server_new(avahi_simple_poll_get(simple_poll), &config, avahi_server_callback, this, &avahiErr);
    avahi_server_config_free(&config); // don't need it any more
    if (!service) {
      if (avahiErr==AVAHI_ERR_NO_NETWORK) {
        // no network to publish to - might be that it is not yet up, try again later
        LOG(LOG_WARNING, "avahi: no network available to publish services now -> retry later");
        MainLoop::currentMainLoop().executeOnce(boost::bind(&DiscoveryManager::startService, this), STARTUP_RETRY_DELAY);
        return;
      }
      else {
        // other problem, report it
        LOG(LOG_ERR, "avahi: failed to create server: %s (%d)", avahi_strerror(avahiErr), avahiErr);
      }
    }
    else {
      // server created
      serviceStarted();
    }
    #else
    // Use client
    LOG(LOG_NOTICE, "avahi: starting client");
    int avahiErr;
    // create client
    service = avahi_client_new(avahi_simple_poll_get(simple_poll), (AvahiClientFlags)0, avahi_client_callback, this, &avahiErr);
    if (!service) {
      if (avahiErr==AVAHI_ERR_NO_NETWORK || avahiErr==AVAHI_ERR_NO_DAEMON) {
        // no network or no daemon to publish to - might be that it is not yet up, try again later
        LOG(LOG_WARNING, "avahi: daemon or network not available to publish services now -> retry later");
        MainLoop::currentMainLoop().executeOnce(boost::bind(&DiscoveryManager::startService, this), STARTUP_RETRY_DELAY);
        return;
      }
      else {
        // other problem, report it
        LOG(LOG_ERR, "avahi: failed to create client: %s (%d)", avahi_strerror(avahiErr), avahiErr);
      }
    }
    else {
      // client created
      serviceStarted();
    }
    #endif
  }
  else {
    LOG(LOG_WARNING, "avahi: startService called while service already running");
  }
}



void DiscoveryManager::stopService()
{
  // stop advertising
  stopAdvertisingDS();
  if (debugServiceBrowser) {
    avahi_service_browser_free(debugServiceBrowser);
    debugServiceBrowser = NULL;
  }
  if (service) {
    #if USE_AVAHI_CORE
    avahi_server_free(service);
    #else
    avahi_client_free(service);
    #endif
    service = NULL;
  }
}




void DiscoveryManager::serviceStarted()
{
  // service started
  // - when debugging, browse for http
  if (FOCUSLOGENABLED) {
    debugServiceBrowser = avahi_service_browser_new(service, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, HTTP_SERVICE_TYPE, NULL, (AvahiLookupFlags)0, avahi_debug_browse_callback, this);
    if (!debugServiceBrowser) {
      FOCUSLOG("avahi: failed to create debug service browser: %s", avahi_strerror(avahi_service_errno(service)));
    }
  }
  // start advertisement if needed
  startAdvertisingDS(service);
}


void DiscoveryManager::restartService()
{
  bool wasAdvertising = dmState>=dm_requeststart;
  stopService();
  if (wasAdvertising) dmState = dm_requeststart; // make sure advertising comes up again if it was running or requested before
  LOG(LOG_WARNING, "discovery: stopped avahi service - restarting in %lld Seconds", SERVICES_RESTART_DELAY/Second);
  MainLoop::currentMainLoop().executeOnce(boost::bind(&DiscoveryManager::startService, this), SERVICES_RESTART_DELAY);
}


bool DiscoveryManager::serviceRunning()
{
  if (!service) return false;
  return serviceRunning(service);
}


#if USE_AVAHI_CORE

// C stubs for avahi callbacks

void DiscoveryManager::avahi_server_callback(AvahiServer *s, AvahiServerState state, void* userdata)
{
  DiscoveryManager *discoveryManager = static_cast<DiscoveryManager*>(userdata);
  discoveryManager->server_callback(s, state);
}

// actual avahi server callback implementation
void DiscoveryManager::server_callback(AvahiServer *s, AvahiServerState state)
{
  // Avahi server state has changed
  switch (state) {
    case AVAHI_SERVER_RUNNING: {
      LOG(LOG_INFO, "avahi: server running");
      // The server has started up successfully and registered its hostname
      // - create services that are set up to get published
      startAdvertising(s);
      break;
    }
    case AVAHI_SERVER_COLLISION: {
      // Host name collision detected
      // - create alternative name
      char *newName = avahi_alternative_host_name(avahi_server_get_host_name(s));
      LOG(LOG_WARNING, "avahi: host name collision, retrying with '%s'", newName);
      int avahiErr = avahi_server_set_host_name(s, newName);
      avahi_free(newName);
      if (avahiErr<0) {
        LOG(LOG_ERR, "avahi: cannot set new host name");
        restartService();
        break;
      }
      // otherwise fall through to AVAHI_SERVER_REGISTERING
    }
    case AVAHI_SERVER_REGISTERING: {
      // drop service registration, will be recreated once server is running
      LOG(LOG_NOTICE, "avahi: host records are being registered");
      if (dSEntryGroup) {
        AvahiEntryGroup *eg = dSEntryGroup;
        dSEntryGroup = NULL; // Null before, in case call below causes immediate second server_callback callback
        avahi_entry_group_reset(eg);
      }
      break;
    }
    case AVAHI_SERVER_FAILURE: {
      LOG(LOG_ERR, "avahi: server failure: %s", avahi_strerror(avahi_server_errno(s)));
      restartService();
      break;
    }
    case AVAHI_SERVER_INVALID: break;
  }
}


bool DiscoveryManager::serviceRunning(AvahiService *aService)
{
  if (!aService) return false;
  return avahi_server_get_state(aService)==AVAHI_SERVER_RUNNING;
}

#else

// C stub for avahi client callback
void DiscoveryManager::avahi_client_callback(AvahiClient *c, AvahiClientState state, void* userdata)
{
  DiscoveryManager *discoveryManager = static_cast<DiscoveryManager*>(userdata);
  discoveryManager->client_callback(c, state);
}


// actual avahi client callback implementation
void DiscoveryManager::client_callback(AvahiClient *c, AvahiClientState state)
{
  // Avahi server state has changed
  switch (state) {
    case AVAHI_CLIENT_S_RUNNING: {
      LOG(LOG_INFO, "avahi: client reports server running");
      // The client reports that server has started up successfully and registered its hostname
      // - create services that are set up to get published
      startAdvertising(c);
      break;
    }
    case AVAHI_CLIENT_S_REGISTERING: {
      // drop service registration, will be recreated once server is running
      LOG(LOG_NOTICE, "avahi: host records are being registered");
      if (dSEntryGroup) {
        AvahiEntryGroup *eg = dSEntryGroup;
        dSEntryGroup = NULL; // Null before, in case call below causes immediate second server_callback callback
        avahi_entry_group_reset(eg);
      }
      break;
    }
    case AVAHI_CLIENT_S_COLLISION: {
      // fall through to AVAHI_CLIENT_FAILURE
    }
    case AVAHI_CLIENT_FAILURE: {
      LOG(LOG_ERR, "avahi: service failure: %s", avahi_strerror(avahi_client_errno(c)));
      restartService();
      break;
    }
    case AVAHI_CLIENT_CONNECTING:
      break;
  }
}

bool DiscoveryManager::serviceRunning(AvahiService *aService)
{
  if (!aService) return false;
  return avahi_client_get_state(aService)==AVAHI_CLIENT_S_RUNNING;
}


#endif // !USE_AVAHI_CORE



void DiscoveryManager::startAdvertising(AvahiService *aService)
{
  // try to start advertising DS (if set up but could not be started before)
  startAdvertisingDS(aService);
}




// MARK: ===== dS advertising


void DiscoveryManager::advertiseDS(
  VdcHostPtr aVdcHost,
  bool aNoAuto,
  int aWebPort, const string aWebPath, int aSshPort
) {
  ErrorPtr err;

  // stop advertising current information
  stopAdvertisingDS();
  // store the new params
  vdcHost = aVdcHost;
  noAuto = aNoAuto;
  publishWebPort = aWebPort;
  publishWebPath = aWebPath;
  publishSshPort = aSshPort;
  // start advertising now
  dmState = dm_setup; // set up for advertising
  restartAdvertising();
}


void DiscoveryManager::refreshAdvertisingDS()
{
  // only valid if service actually started
  if (serviceRunning()) {
    restartAdvertising();
  }
}


void DiscoveryManager::stopAdvertisingDS()
{
  if (dSEntryGroup) {
    avahi_entry_group_free(dSEntryGroup);
    dSEntryGroup = NULL;
    LOG(LOG_NOTICE, "discovery: unpublished vDC service.");
  }
  if (dmState>dm_setup) {
    dmState = dm_setup;
  }
}


void DiscoveryManager::restartAdvertising()
{
  // stop advertising current information
  stopAdvertisingDS();
  // init state
  if (dmState==dm_setup) {
    dmState = dm_requeststart; // requested to start
    // try to start now
    startAdvertisingDS(service);
  }
}




void DiscoveryManager::startAdvertisingDS(AvahiService *aService)
{
  if (dmState==dm_requeststart && serviceRunning(aService)) {
    // create entry group now
    string descriptiveName = vdcHost->publishedDescription();
    // create entry group if needed
    if (!(dSEntryGroup = avahi_entry_group_new(aService, ds_entry_group_callback, this))) {
      LOG(LOG_ERR, "avahi_entry_group_new() failed: %s", avahi_strerror(avahi_service_errno(aService)));
      goto fail;
    }
    // add the services
    int avahiErr;
    if (publishWebPort) {
      // - web UI
      string txt_path = string_format("path=%s", publishWebPath.c_str());
      if ((avahiErr = avahi_add_service(
        aService,
        dSEntryGroup,
        AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, // all interfaces and protocols (as enabled at server level)
        (AvahiPublishFlags)0, // no flags
        descriptiveName.c_str(),
        HTTP_SERVICE_TYPE, // Web service
        NULL, // no domain
        NULL, // no host
        publishWebPort, // web port
        publishWebPath.empty() ? NULL : txt_path.c_str(), // possibly, path TXT record
        NULL // TXT record terminator
      ))<0) {
        LOG(LOG_ERR, "avahi: failed to add _http._tcp service: %s", avahi_strerror(avahiErr));
        goto fail;
      }
    }
    if (publishSshPort) {
      // - ssh access
      if ((avahiErr = avahi_add_service(
        aService,
        dSEntryGroup,
        AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, // all interfaces and protocols (as enabled at server level)
        (AvahiPublishFlags)0, // no flags
        descriptiveName.c_str(),
        SSH_SERVICE_TYPE, // ssh
        NULL, // no domain
        NULL, // no host
        publishSshPort, // ssh port
        NULL // no TXT records
      ))<0) {
        LOG(LOG_ERR, "avahi: failed to add _ssh._tcp service: %s", avahi_strerror(avahiErr));
        goto fail;
      }
    }
    if (vdcHost->vdcApiServer) {
      // advertise the vdc host (p44vdc) to the network
      int vdcPort = 0;
      sscanf(vdcHost->vdcApiServer->getPort(), "%d", &vdcPort);
      string txt_dsuid = string_format("dSUID=%s", vdcHost->getDsUid().getString().c_str());
      if ((avahiErr = avahi_add_service(
        aService,
        dSEntryGroup,
        AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, // all interfaces and protocols (as enabled at server level)
        (AvahiPublishFlags)0, // no flags
        descriptiveName.c_str(),
        VDC_SERVICE_TYPE, // vdc
        NULL, // no domain
        NULL, // no host
        vdcPort, // the vdc API host port
        txt_dsuid.c_str(), // TXT record for the vdc host's dSUID
        noAuto ? VDSM_VDC_ROLE_NOAUTO : NULL, // noauto flag or early TXT terminator
        NULL // TXT record terminator
      ))<0) {
        LOG(LOG_ERR, "avahi: failed to add _ds-vdc._tcp service: %s", avahi_strerror(avahiErr));
        goto fail;
      }
    } // has vDC API
    else if (!publishWebPort && !publishSshPort) {
      // no vDC API and nothing else to publish: just disable advertising
      dmState = dm_disabled; // disabled
      LOG(LOG_WARNING, "avahi: no services to publish - disabled advertising");
      return;
    }
    // register the services
    if ((avahiErr = avahi_entry_group_commit(dSEntryGroup)) < 0) {
      LOG(LOG_ERR, "avahi: Failed to commit entry_group: %s", avahi_strerror(avahiErr));
      goto fail;
    }
    dmState = dm_starting; // waiting for publishing to happen (ds_entry_group_callback)
    return;
  fail:
    // Error: issue overall service restart
    restartService();
  }
}


#if USE_AVAHI_CORE

// C stubs for avahi callbacks

void DiscoveryManager::ds_entry_group_callback(AvahiServer *s, AvahiSEntryGroup *g, AvahiEntryGroupState state, void* userdata)
{
  DiscoveryManager *discoveryManager = static_cast<DiscoveryManager*>(userdata);
  discoveryManager->avahi_ds_entry_group_callback(s, g, state);
}

#else

void DiscoveryManager::ds_entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state, void* userdata)
{
  DiscoveryManager *discoveryManager = static_cast<DiscoveryManager*>(userdata);
  discoveryManager->avahi_ds_entry_group_callback(discoveryManager->service, g, state);
}

#endif // !USE_AVAHI_CORE



void DiscoveryManager::avahi_ds_entry_group_callback(AvahiService *aService, AvahiEntryGroup *g, AvahiEntryGroupState state)
{
  // entry group state has changed
  switch (state) {
    case AVAHI_ENTRY_GROUP_ESTABLISHED: {
      if (dmState<dm_started) {
        dmState = dm_started;
      }
      LOG(LOG_NOTICE, "discovery: successfully published services as '%s'.", vdcHost->publishedDescription().c_str());
      break;
    }
    case AVAHI_ENTRY_GROUP_COLLISION: {
      // service name collision detected
      // Note: we don't handle this as it can't really happen (publishedName contains the deviceId or the vdcHost dSUID which MUST be unique)
      LOG(LOG_CRIT, "avahi: service name collision, '%s' is apparently not unique", vdcHost->publishedDescription().c_str());
      break;
    }
    case AVAHI_ENTRY_GROUP_FAILURE: {
      LOG(LOG_ERR, "avahi: entry group failure: %s -> restarting avahi service", avahi_strerror(avahi_service_errno(aService)));
      restartService();
      break;
    }
    case AVAHI_ENTRY_GROUP_UNCOMMITED:
    case AVAHI_ENTRY_GROUP_REGISTERING:
      break;
  }
}



// MARK: ==== Debug http browser

#if FOCUSLOGGING

void DiscoveryManager::avahi_debug_browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol, AvahiBrowserEvent event, const char *name, const char *type, const char *domain, AVAHI_GCC_UNUSED AvahiLookupResultFlags flags, void* userdata)
{
  DiscoveryManager *discoveryManager = static_cast<DiscoveryManager*>(userdata);
  discoveryManager->debug_browse_callback(b, interface, protocol, event, name, type, domain, flags);
}


void DiscoveryManager::debug_browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol, AvahiBrowserEvent event, const char *name, const char *type, const char *domain, AVAHI_GCC_UNUSED AvahiLookupResultFlags flags)
{
  // debug (show http services)
  switch (event) {
    case AVAHI_BROWSER_NEW:
      FOCUSLOG("avahi: new http service '%s' in domain '%s' discovered", name, domain);
      break;
    case AVAHI_BROWSER_REMOVE:
      FOCUSLOG("avahi: http service '%s' in domain '%s' has disappeared", name, domain);
      break;
    default:
      break;
  }
}

#endif // FOCUSLOGGING


// MARK: ==== generic service browser


ServiceBrowserPtr DiscoveryManager::newServiceBrowser(const char *aServiceType, ServiceDiscoveryCB aServiceDiscoveryCB)
{
  if (serviceRunning()) {
    return ServiceBrowserPtr(new ServiceBrowser(aServiceType, aServiceDiscoveryCB));
  }
  else {
    LOG(LOG_ERR, "ServiceBrowser: Discovery Service is not running, cannot create service browser");
    return ServiceBrowserPtr();
  }
}



void ServiceBrowser::avahi_browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol, AvahiBrowserEvent event, const char *name, const char *type, const char *domain, AVAHI_GCC_UNUSED AvahiLookupResultFlags flags, void* userdata)
{
  ServiceBrowser *serviceBrowser = static_cast<ServiceBrowser*>(userdata);
  serviceBrowser->browse_callback(b, interface, protocol, event, name, type, domain, flags);
}


ServiceBrowser::ServiceBrowser(const char *aServiceType, ServiceDiscoveryCB aServiceDiscoveryCB) :
  serviceDiscoveryCB(aServiceDiscoveryCB),
  avahiServiceBrowser(NULL)
{
  AvahiService *s = DiscoveryManager::sharedDiscoveryManager().service;
  avahiServiceBrowser = avahi_service_browser_new(s, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, aServiceType, NULL, (AvahiLookupFlags)0, avahi_browse_callback, this);
  if (!avahiServiceBrowser) {
    LOG(LOG_ERR, "ServiceBrowser: Failed to create browser for type '%s': %s", aServiceType, avahi_strerror(avahi_service_errno(s)));
  }
}


ServiceBrowser::~ServiceBrowser()
{
  if (avahiServiceBrowser) {
    avahi_service_browser_free(avahiServiceBrowser);
    avahiServiceBrowser = NULL;
  }
}


void ServiceBrowser::browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol, AvahiBrowserEvent event, const char *name, const char *type, const char *domain, AVAHI_GCC_UNUSED AvahiLookupResultFlags flags)
{
  AvahiService *s = DiscoveryManager::sharedDiscoveryManager().service;

  // Called whenever a new services becomes available on the LAN or is removed from the LAN
  // - may use global "service" var, because browsers are no set up within server/client callbacks, but only afterwards, when "service" is defined
  switch (event) {
    case AVAHI_BROWSER_FAILURE:
      LOG(LOG_ERR, "ServiceBrowser: browser failure: %s", avahi_strerror(avahi_service_errno(s)));
      break;
    case AVAHI_BROWSER_NEW:
      LOG(LOG_INFO, "ServiceBrowser: NEW service '%s' of type '%s' in domain '%s' -> resolving now", name, type, domain);
      // Note: the returned resolver object can be ignored, it is freed in the callback
      //   if the server terminates before the callback has been executes, the server deletes the resolver.
      if (!(avahi_service_resolver_new(s, interface, protocol, name, type, domain, AVAHI_PROTO_UNSPEC, (AvahiLookupFlags)0, avahi_resolve_callback, this))) {
        LOG(LOG_ERR, "ServiceBrowser: failed to create resolver for service '%s': %s", name, avahi_strerror(avahi_service_errno(s)));
        break;
      }
      break;
    case AVAHI_BROWSER_REMOVE:
      LOG(LOG_INFO, "ServiceBrowser: VANISHED service '%s' of type '%s' in domain '%s'", name, type, domain);
      if (serviceDiscoveryCB) {
        // create info object
        ServiceInfoPtr bi = ServiceInfoPtr(new ServiceInfo);
        bi->disappeared = true;
        bi->name = name;
        bi->domain = domain;
        bi->port = 0;
        bi->lookupFlags = flags;
        // report service having disappeared
        serviceDiscoveryCB(bi);
      }
      break;
    case AVAHI_BROWSER_ALL_FOR_NOW:
      LOG(LOG_DEBUG, "avahi: BROWSER_ALL_FOR_NOW");
      break;
    case AVAHI_BROWSER_CACHE_EXHAUSTED:
      LOG(LOG_DEBUG, "avahi: BROWSER_CACHE_EXHAUSTED");
      break;
  }
}


void ServiceBrowser::avahi_resolve_callback(AvahiServiceResolver *r, AvahiIfIndex interface, AvahiProtocol protocol, AvahiResolverEvent event, const char *name, const char *type, const char *domain, const char *host_name, const AvahiAddress *a, uint16_t port, AvahiStringList *txt, AvahiLookupResultFlags flags, void* userdata)
{
  ServiceBrowser *serviceBrowser = static_cast<ServiceBrowser*>(userdata);
  serviceBrowser->resolve_callback(r, interface, protocol, event, name, type, domain, host_name, a, port, txt, flags);

}

void ServiceBrowser::resolve_callback(AvahiServiceResolver *r, AvahiIfIndex interface, AvahiProtocol protocol, AvahiResolverEvent event, const char *name, const char *type, const char *domain, const char *host_name, const AvahiAddress *a, uint16_t port, AvahiStringList *txt, AvahiLookupResultFlags flags)
{
  AvahiService *s = DiscoveryManager::sharedDiscoveryManager().service;

  switch (event) {
    case AVAHI_RESOLVER_FAILURE:
      FOCUSLOG("avahi: failed to resolve service '%s' of type '%s' in domain '%s': %s", name, type, domain, avahi_strerror(avahi_service_errno(s)));
      break;
    case AVAHI_RESOLVER_FOUND: {
      char addrtxt[AVAHI_ADDRESS_STR_MAX];
      avahi_address_snprint(addrtxt, sizeof(addrtxt), a);
      FOCUSLOG("avahi: resolved service '%s' of type '%s' in domain '%s' at %s:", name, type, domain, addrtxt);
      if (serviceDiscoveryCB) {
        // create info object
        ServiceInfoPtr bi = ServiceInfoPtr(new ServiceInfo);
        bi->disappeared = false;
        bi->name = name;
        bi->domain = domain;
        bi->hostname = host_name;
        bi->hostaddress = addrtxt;
        bi->port = port;
        bi->lookupFlags = flags;
        // copy txt records, if any
        while (txt) {
          string t,k,v;
          t.assign((const char *)avahi_string_list_get_text(txt), (size_t)avahi_string_list_get_size(txt));
          if(keyAndValue(t, k, v, '=')) {
            bi->txtRecords[k] = v;
          }
          else {
            bi->txtRecords[t] = "";
          }
          txt = avahi_string_list_get_next(txt);
        }
        // report service found
        serviceDiscoveryCB(bi);
      }
      break;
    }
  }
  avahi_service_resolver_free(r);
}



#endif // !DISABLE_DISCOVERY






