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

#include "macaddress.hpp"

#include "vdchost.hpp"
#include "vdc.hpp"
#include "device.hpp"

#include "jsonvdcapi.hpp" // need it for the case of no vDC api, as default

#include "outputbehaviour.hpp"
#include "sensorbehaviour.hpp"
#include "binaryinputbehaviour.hpp"
#include "buttonbehaviour.hpp"

#if ENABLE_LOCAL_BEHAVIOUR
// for local behaviour
#include "lightbehaviour.hpp"
#endif

#if ENABLE_LOCALCONTROLLER
#include "localcontroller.hpp"
#endif

#if ENABLE_P44SCRIPT
#include "httpcomm.hpp"
#include "socketcomm.hpp"
#endif


using namespace p44;


// how often to write mainloop statistics into log output
#ifndef DEFAULT_MAINLOOP_STATS_INTERVAL
  //#define DEFAULT_MAINLOOP_STATS_INTERVAL (60) // every 5 min (with periodic activity every 5 seconds: 60*5 = 300 = 5min)
  #define DEFAULT_MAINLOOP_STATS_INTERVAL (0) // not by default. We can use setMainLoopStatsInterval() to enable
#endif

// how long vDC waits after receiving ok from one announce until it fires the next
#ifndef ANNOUNCE_PAUSE
  #define ANNOUNCE_PAUSE (10*MilliSecond)
#endif

// how long until a not acknowledged registrations is considered timed out (and next device can be attempted)
#ifndef ANNOUNCE_TIMEOUT
  #define ANNOUNCE_TIMEOUT (30*Second)
#endif

// how long until a not acknowledged announcement for a device is retried again for the same device
#ifndef ANNOUNCE_RETRY_TIMEOUT
  #define ANNOUNCE_RETRY_TIMEOUT (300*Second)
#endif

// default product name
#ifndef DEFAULT_PRODUCT_NAME
  #define DEFAULT_PRODUCT_NAME "plan44.ch vdcd"
#endif

// default description template
#ifndef DEFAULT_DESCRIPTION_TEMPLATE
  #define DEFAULT_DESCRIPTION_TEMPLATE "%V %M%N #%S"
#endif

static VdcHost *sharedVdcHostP = NULL;

VdcHost::VdcHost(bool aWithLocalController, bool aWithPersistentChannels) :
  inheritedParams(dsParamStore),
  mac(0),
  networkConnected(true), // start with the assumption of a connected network
  maxApiVersion(0), // no API version limit
  externalDsuid(false),
  vdcHostInstance(0),
  storedDsuid(false),
  allowCloud(false),
  DsAddressable(this),
  collecting(false),
  lastActivity(Never),
  lastPeriodicRun(Never),
  learningMode(false),
  localDimDirection(0), // undefined
  mainloopStatsInterval(DEFAULT_MAINLOOP_STATS_INTERVAL),
  mainLoopStatsCounter(0),
  persistentChannels(aWithPersistentChannels),
  #if P44SCRIPT_FULL_SUPPORT
  mainScript(sourcecode+regular, "main", this),
  #endif
  #if P44SCRIPT_FULL_SUPPORT || EXPRESSION_SCRIPT_SUPPORT
  globalScriptsStarted(false),
  #endif
  productName(DEFAULT_PRODUCT_NAME),
  geolocation() // default location will be set from timeutils
{
  #if ENABLE_P44SCRIPT
  P44Script::StandardScriptingDomain::sharedDomain().setGeoLocation(&geolocation);
  #if P44SCRIPT_FULL_SUPPORT
  // vdchost is the global context for this app, so register its members in the standard scripting
  // domain making them accessible in all scripts
  StandardScriptingDomain::sharedDomain().registerMemberLookup(VdcHostLookup::sharedLookup());
  vdcHostScriptContext = StandardScriptingDomain::sharedDomain().newContext();
  // init main script source
  mainScript.setSharedMainContext(vdcHostScriptContext);
  // Add some extras
  #if ENABLE_HTTP_SCRIPT_FUNCS
  StandardScriptingDomain::sharedDomain().registerMemberLookup(new P44Script::HttpLookup);
  #endif // ENABLE_HTTP_SCRIPT_FUNCS
  #if ENABLE_SOCKET_SCRIPT_FUNCS
  StandardScriptingDomain::sharedDomain().registerMemberLookup(new P44Script::SocketLookup);
  #endif // ENABLE_SOCKET_SCRIPT_FUNCS
  #endif // P44SCRIPT_FULL_SUPPORT
  #endif
  // remember singleton's address
  sharedVdcHostP = this;
  // obtain default MAC address (might be changed by setIdMode())
  mac = macAddress();
  #if ENABLE_LOCALCONTROLLER
  if (aWithLocalController) {
    // create it
    localController = LocalControllerPtr(new LocalController(*this));
  }
  #endif
  // initialize real time jump detection as early as possible (to catch changes happening during initialisation)
  timeOfDayDiff = Infinite;
  checkTimeOfDayChange(); // will not post a event when timeOfDayDiff is Infinite
}


VdcHost::~VdcHost()
{
  #if ENABLE_LOCALCONTROLLER
  if (localController) localController.reset();
  #endif
}


VdcHostPtr VdcHost::sharedVdcHost()
{
  return VdcHostPtr(sharedVdcHostP);
}


#if ENABLE_LOCALCONTROLLER
LocalControllerPtr VdcHost::getLocalController()
{
  return localController;
}
#endif


void VdcHost::setEventMonitor(VdchostEventCB aEventCB)
{
  eventMonitorHandler = aEventCB;
}


void VdcHost::identifyToUser()
{
  postEvent(vdchost_identify); // send out signal anyway
  if (!canIdentifyToUser()) inherited::identifyToUser(); // make sure it is at least logged
}


bool VdcHost::canIdentifyToUser()
{
  // assume vdchost can identify itself when it has a event monitor installed which will actually see vdchost_identify
  return eventMonitorHandler!=NULL;
}



void VdcHost::postEvent(VdchostEvent aEvent)
{
  if (aEvent>=vdchost_redistributed_events) {
    // let all vdcs (and their devices) know
    LOG(LOG_INFO, ">>> vdcs start processing global event %d", (int)aEvent);
    for (VdcMap::iterator pos = vdcs.begin(); pos != vdcs.end(); ++pos) {
      pos->second->handleGlobalEvent(aEvent);
    }
    LOG(LOG_INFO, ">>> vdcs done processing event %d", (int)aEvent);
  }
  #if ENABLE_LOCALCONTROLLER
  if (localController) localController->processGlobalEvent(aEvent);
  #endif
  // let vdchost itself know
  handleGlobalEvent(aEvent);
  // also let app-level event monitor know
  if (eventMonitorHandler) {
    eventMonitorHandler(aEvent);
  }
}


void VdcHost::handleGlobalEvent(VdchostEvent aEvent)
{
  if (aEvent==vdchost_devices_initialized) {
    #if P44SCRIPT_FULL_SUPPORT || EXPRESSION_SCRIPT_SUPPORT
    // after the first device initialisation run, it is the moment to start global scripts
    if (!globalScriptsStarted) {
      // only once
      globalScriptsStarted = true;
      runGlobalScripts();
    }
    #endif // P44SCRIPT_FULL_SUPPORT || EXPRESSION_SCRIPT_SUPPORT
  }
  inherited::handleGlobalEvent(aEvent);
}


ApiValuePtr VdcHost::newApiValue()
{
  return vdcApiServer ? vdcApiServer->newApiValue() : ApiValuePtr(new JsonApiValue);
}


void VdcHost::setName(const string &aName)
{
  if (aName!=getAssignedName()) {
    // has changed
    inherited::setName(aName);
    // make sure it will be saved
    markDirty();
    // is a global event - might need re-advertising services
    postEvent(vdchost_descriptionchanged);
  }
}



void VdcHost::setIdMode(DsUidPtr aExternalDsUid, const string aIfNameForMAC, int aInstance)
{
  vdcHostInstance = aInstance;
  if (!aIfNameForMAC.empty()) {
    // use MAC from specific interface
    mac = macAddress(aIfNameForMAC.c_str());
  }
  if (aExternalDsUid) {
    externalDsuid = true;
    dSUID = *aExternalDsUid;
  }
}



void VdcHost::addVdc(VdcPtr aVdcPtr)
{
  vdcs[aVdcPtr->getDsUid()] = aVdcPtr;
}



void VdcHost::setIconDir(const char *aIconDir)
{
	iconDir = nonNullCStr(aIconDir);
	if (!iconDir.empty() && iconDir[iconDir.length()-1]!='/') {
		iconDir.append("/");
	}
}


const char *VdcHost::getIconDir()
{
	return iconDir.c_str();
}



void VdcHost::setPersistentDataDir(const char *aPersistentDataDir)
{
	persistentDataDir = nonNullCStr(aPersistentDataDir);
  pathstring_format_append(persistentDataDir,""); // make sure filenames can be appended without adding a delimiter
}


const char *VdcHost::getPersistentDataDir()
{
	return persistentDataDir.c_str();
}



void VdcHost::setConfigDir(const char *aConfigDir)
{
  configDir = nonNullCStr(aConfigDir);
  pathstring_format_append(configDir,""); // make sure filenames can be appended without adding a delimiter
}


const char *VdcHost::getConfigDir()
{
  return configDir.c_str();
}




string VdcHost::publishedDescription()
{
  // derive the descriptive name
  // "%V %M%N %S"
  string n = descriptionTemplate;
  if (n.empty()) n = DEFAULT_DESCRIPTION_TEMPLATE;
  string s;
  size_t i;
  // Vendor
  while ((i = n.find("%V"))!=string::npos) { n.replace(i, 2, vendorName()); }
  // Model
  while ((i = n.find("%M"))!=string::npos) { n.replace(i, 2, modelName()); }
  // (optional) Name
  s = getName();
  if (!s.empty()) {
    s = " \""+s+"\"";
  }
  while ((i = n.find("%N"))!=string::npos) { n.replace(i, 2, s); }
  // Serial/hardware ID
  s = getDeviceHardwareId();
  if (s.empty()) {
    // use dSUID if no other ID is specified
    s = getDsUid().getString();
  }
  while ((i = n.find("%S"))!=string::npos) { n.replace(i, 2, s); }
  return n;
}


// MARK: - global status

bool VdcHost::isApiConnected()
{
  return getSessionConnection()!=NULL;
}


uint32_t VdcHost::getIpV4Address()
{
  return ipv4Address(ifNameForConn.c_str());
}


bool VdcHost::isNetworkConnected()
{
  uint32_t ipv4 = getIpV4Address();
  // Only consider connected if we have a IP address, and none from the 169.254.0.0/16
  // link-local autoconfigured ones (RFC 3927/APIPA).
  bool nowConnected = (ipv4!=0) && ((ipv4 & 0xFFFF0000)!=0xA9FE0000);
  if (nowConnected!=networkConnected) {
    // change in connection status - post it
    networkConnected = nowConnected;
    LOG(LOG_NOTICE, "*** Network connection %s", networkConnected ? "re-established" : "lost");
    postEvent(networkConnected ? vdchost_network_reconnected : vdchost_network_lost);
  }
  return networkConnected;
}


void VdcHost::checkTimeOfDayChange()
{
  struct tm lt;
  MainLoop::getLocalTime(lt);
  long long lm = ((((long long)lt.tm_year*366+lt.tm_yday)*24+lt.tm_hour)*60+lt.tm_min)*60+lt.tm_sec; // local time fingerprint (not monotonic, not accurate)
  MLMicroSeconds td = MainLoop::now()-lm*Second;
  if (timeOfDayDiff==Infinite) {
    timeOfDayDiff = td; // first time
  }
  else {
    MLMicroSeconds d = timeOfDayDiff-td;
    if (d<0) d = -d;
    if (d>5*Second) {
      LOG(LOG_NOTICE, "*** Time-Of-Day has changed by %.f seconds", (double)(timeOfDayDiff-td)/Second);
      timeOfDayDiff = td;
      postEvent(vdchost_timeofday_changed);
    }
  }
}


// MARK: - initializisation of DB and containers


// Version history
//  1 : alpha/beta phase DB
//  2 : no schema change, but forced re-creation due to changed scale of brightness (0..100 now, was 0..255 before)
//  3 : no schema change, but forced re-creation due to bug in storing output behaviour settings
#define DSPARAMS_SCHEMA_MIN_VERSION 3 // minimally supported version, anything older will be deleted
#define DSPARAMS_SCHEMA_VERSION 3 // current version

string DsParamStore::dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion)
{
  string sql;
  if (aFromVersion==0) {
    // create DB from scratch
		// - use standard globs table for schema version
    sql = inherited::dbSchemaUpgradeSQL(aFromVersion, aToVersion);
		// - no vdchost level table to create at this time
    //   (PersistentParams create and update their tables as needed)
    // reached final version in one step
    aToVersion = DSPARAMS_SCHEMA_VERSION;
  }
  return sql;
}



void VdcHost::prepareForVdcs(bool aFactoryReset)
{
  // initialize dsParamsDB database
  string databaseName = getPersistentDataDir();
  string_format_append(databaseName, "DsParams.sqlite3");
  ErrorPtr error = dsParamStore.connectAndInitialize(databaseName.c_str(), DSPARAMS_SCHEMA_VERSION, DSPARAMS_SCHEMA_MIN_VERSION, aFactoryReset);
  // load the vdc host settings and determine the dSUID (external > stored > mac-derived)
  loadAndFixDsUID();
}


void VdcHost::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  // Log start message
  LOG(LOG_NOTICE,
    "\n\n\n*** starting initialisation of vcd host '%s' (Instance #%d)"
    "\n*** Product name: '%s', Product Version: '%s', App Version: '%s', Device Hardware ID: '%s'"
    "\n*** dSUID (%s) = %s, MAC: %s, IP = %s\n",
    publishedDescription().c_str(),
    vdcHostInstance,
    productName.c_str(),
    productVersion.c_str(),
    Application::sharedApplication()->version().c_str(),
    deviceHardwareId.c_str(),
    externalDsuid ? "external" : "MAC-derived",
    shortDesc().c_str(),
    macAddressToString(mac, ':').c_str(),
    ipv4ToString(getIpV4Address()).c_str()
  );
  // start the API server if API is enabled
  if (vdcApiServer) {
    vdcApiServer->setConnectionStatusHandler(boost::bind(&VdcHost::vdcApiConnectionStatusHandler, this, _1, _2));
    vdcApiServer->start();
  }
  // start initialisation of vDCs
  initializeNextVdc(aCompletedCB, aFactoryReset, vdcs.begin());
}



void VdcHost::initializeNextVdc(StatusCB aCompletedCB, bool aFactoryReset, VdcMap::iterator aNextVdc)
{
  // initialize all vDCs, even when some have errors
  if (aNextVdc!=vdcs.end()) {
    // load persistent params for vdc (dSUID is already stable at this point, cannot depend on initialize()!)
    aNextVdc->second->load();
    // initialize with parameters already loaded
    aNextVdc->second->initialize(boost::bind(&VdcHost::vdcInitialized, this, aCompletedCB, aFactoryReset, aNextVdc, _1), aFactoryReset);
    return;
  }
  // successfully done
  postEvent(vdchost_vdcs_initialized);
  aCompletedCB(ErrorPtr());
}


void VdcHost::vdcInitialized(StatusCB aCompletedCB, bool aFactoryReset, VdcMap::iterator aNextVdc, ErrorPtr aError)
{
  if (Error::notOK(aError)) {
    LOG(LOG_ERR, "vDC %s: failed to initialize: %s", aNextVdc->second->shortDesc().c_str(), aError->text());
    aNextVdc->second->setVdcError(aError);
  }
  // anyway, initialize next
  aNextVdc++;
  // ...but unwind stack first, let mainloop call next init
  MainLoop::currentMainLoop().executeNow(boost::bind(&VdcHost::initializeNextVdc, this, aCompletedCB, aFactoryReset, aNextVdc));
}



void VdcHost::startRunning()
{
  // force initial network connection check
  // Note: will NOT post re-connected message if we're initializing normally with network up,
  //   but will post network lost event if we do NOT have a connection now.
  isNetworkConnected();
  // start periodic tasks needed during normal running like announcement checking and saving parameters
  periodicTaskTicket.executeOnce(boost::bind(&VdcHost::periodicTask, vdcHostP, _2), 1*Second);
  #if ENABLE_LOCALCONTROLLER
  if (localController) localController->startRunning();
  #endif
}



// MARK: - collect devices


void VdcHost::collectDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  if (!collecting) {
    collecting = true;
    if ((aRescanFlags & rescanmode_incremental)==0) {
      // only for non-incremental collect, close vdsm connection
      if (activeSessionConnection) {
        LOG(LOG_NOTICE, "requested to re-collect devices -> closing vDC API connection");
        activeSessionConnection->closeConnection(); // close the API connection
        resetAnnouncing();
        activeSessionConnection.reset(); // forget connection
        postEvent(vdchost_vdcapi_disconnected);
      }
      dSDevices.clear(); // forget existing ones
    }
    collectFromNextVdc(aCompletedCB, aRescanFlags, vdcs.begin());
  }
}


void VdcHost::collectFromNextVdc(StatusCB aCompletedCB, RescanMode aRescanFlags, VdcMap::iterator aNextVdc)
{
  if (aNextVdc!=vdcs.end()) {
    VdcPtr vdc = aNextVdc->second;
    LOG(LOG_NOTICE,
      "=== collecting devices from vdc %s (%s #%d)",
      vdc->shortDesc().c_str(),
      vdc->vdcClassIdentifier(),
      vdc->getInstanceNumber()
    );
    vdc->collectDevices(boost::bind(&VdcHost::vdcCollected, this, aCompletedCB, aRescanFlags, aNextVdc, _1), aRescanFlags);
    return;
  }
  // all devices collected, but not yet initialized
  postEvent(vdchost_devices_collected);
  LOG(LOG_NOTICE, "=== collected devices from all vdcs -> initializing devices now\n");
  // now initialize devices (which are already identified by now!)
  initializeNextDevice(aCompletedCB, dSDevices.begin());
}


void VdcHost::vdcCollected(StatusCB aCompletedCB, RescanMode aRescanFlags, VdcMap::iterator aNextVdc, ErrorPtr aError)
{
  if (Error::notOK(aError)) {
    LOG(LOG_ERR, "vDC %s: error collecting devices: %s", aNextVdc->second->shortDesc().c_str(), aError->text());
  }
  LOG(LOG_NOTICE, "=== done collecting from %s\n", aNextVdc->second->shortDesc().c_str());
  // next
  aNextVdc++;
  collectFromNextVdc(aCompletedCB, aRescanFlags, aNextVdc);
}


void VdcHost::initializeNextDevice(StatusCB aCompletedCB, DsDeviceMap::iterator aNextDevice)
{
  if (aNextDevice!=dSDevices.end()) {
    // TODO: now never doing factory reset init, maybe parametrize later
    aNextDevice->second->initializeDevice(boost::bind(&VdcHost::nextDeviceInitialized, this, aCompletedCB, aNextDevice, _1), false);
    return;
  }
  // all devices initialized
  postEvent(vdchost_devices_initialized);
  // check for global vdc errors now
  ErrorPtr vdcInitErr;
  for (VdcMap::iterator pos = vdcs.begin(); pos!=vdcs.end(); pos++) {
    if (Error::notOK(pos->second->getVdcErr())) {
      vdcInitErr = pos->second->getVdcErr();
      LOG(LOG_ERR, "*** initial device collecting incomplete because of error: %s", vdcInitErr->text());
      break;
    }
  }
  aCompletedCB(vdcInitErr);
  LOG(LOG_NOTICE, "=== initialized all collected devices\n");
  collecting = false;
  // make sure at least one vdc can be announced to dS, even if all are empty and instructed to hide when empty
  bool someVisible = false;
  VdcPtr firstPublic;
  for (VdcMap::iterator pos = vdcs.begin(); pos!=vdcs.end(); ++pos) {
    VdcPtr vdc = pos->second;
    if (vdc->isPublicDS()) {
      if (!firstPublic) firstPublic = vdc;
      if (!vdc->getVdcFlag(vdcflag_hidewhenempty) || vdc->getNumberOfDevices()>0) {
        someVisible = true;
        break;
      }
    }
  }
  if (!someVisible && firstPublic) {
    firstPublic->vdcFlags &= ~vdcflag_hidewhenempty; // temporarily show this vdc to avoid webui getting unreachable from dS
  }
}


void VdcHost::nextDeviceInitialized(StatusCB aCompletedCB, DsDeviceMap::iterator aNextDevice, ErrorPtr aError)
{
  deviceInitialized(aNextDevice->second, aError);
  // check next
  ++aNextDevice;
  initializeNextDevice(aCompletedCB, aNextDevice);
}



// MARK: - adding/removing/finding devices


DevicePtr VdcHost::getDeviceByNameOrDsUid(const string &aName)
{
  DsDeviceMap::iterator pos;
  DsUid dsuid;
  if (dsuid.setAsString(aName)) {
    pos = dSDevices.find(dsuid);
  }
  else {
    for (pos = dSDevices.begin(); pos!=dSDevices.end(); ++pos) {
      if (pos->second->getName()==aName) break;
    }
  }
  if (pos!=dSDevices.end()) {
    return pos->second;
  }
  return DevicePtr();
}


bool VdcHost::addDevice(DevicePtr aDevice)
{
  if (!aDevice)
    return false; // no device, nothing added
  // check if device with same dSUID already exists
  DsDeviceMap::iterator pos = dSDevices.find(aDevice->getDsUid());
  if (pos!=dSDevices.end()) {
    LOG(LOG_DEBUG, "- device %s already registered, not adding again",aDevice->shortDesc().c_str());
    // first unwind call chain that triggered deletion, keep aDevice living until then
    MainLoop::currentMainLoop().executeNow(boost::bind(&VdcHost::duplicateIgnored, this, aDevice));
    return false; // duplicate dSUID, not added
  }
  // set for given dSUID in the container-wide map of devices
  dSDevices[aDevice->getDsUid()] = aDevice;
  LOG(LOG_NOTICE, "--- added device: %s (not yet initialized)",aDevice->shortDesc().c_str());
  // load the device's persistent params
  aDevice->load();
  // if not collecting, initialize device right away.
  // Otherwise, initialisation will be done when collecting is complete
  if (!collecting) {
    aDevice->initializeDevice(boost::bind(&VdcHost::separateDeviceInitialized, this, aDevice, _1), false);
  }
  return true;
}


void VdcHost::duplicateIgnored(DevicePtr aDevice)
{
  LOG(LOG_INFO, "- ignored duplicate device: %s",aDevice->shortDesc().c_str());
  // aDevice will go out of scope here and possibly delete device now
}


void VdcHost::separateDeviceInitialized(DevicePtr aDevice, ErrorPtr aError)
{
  deviceInitialized(aDevice, aError);
  // trigger announcing when initialized (no problem when called while already announcing)
  startAnnouncing();
}



void VdcHost::deviceInitialized(DevicePtr aDevice, ErrorPtr aError)
{
  if (Error::notOK(aError)) {
    LOG(LOG_ERR, "*** error initializing device %s: %s", aDevice->shortDesc().c_str(), aError->text());
  }
  else {
    LOG(LOG_NOTICE, "--- initialized device: %s",aDevice->description().c_str());
    #if ENABLE_LOCALCONTROLLER
    if (localController) localController->deviceAdded(aDevice);
    #endif
    aDevice->addedAndInitialized();
  }
}




// remove a device from container list (but does not disconnect it!)
void VdcHost::removeDevice(DevicePtr aDevice, bool aForget)
{
  if (aForget) {
    // permanently remove from DB
    aDevice->forget();
  }
  else {
    // save, as we don't want to forget the settings associated with the device
    aDevice->save();
  }
  // remove from container-wide map of devices
  dSDevices.erase(aDevice->getDsUid());
  LOG(LOG_NOTICE, "--- removed device: %s", aDevice->shortDesc().c_str());
  #if ENABLE_LOCALCONTROLLER
  if (localController) localController->deviceRemoved(aDevice);
  #endif
}



void VdcHost::startLearning(LearnCB aLearnHandler, bool aDisableProximityCheck)
{
  // enable learning in all class containers
  learnHandler = aLearnHandler;
  learningMode = true;
  LOG(LOG_NOTICE, "=== start learning%s", aDisableProximityCheck ? " with proximity check disabled" : "");
  for (VdcMap::iterator pos = vdcs.begin(); pos != vdcs.end(); ++pos) {
    pos->second->setLearnMode(true, aDisableProximityCheck, undefined);
  }
}


void VdcHost::stopLearning()
{
  // disable learning in all class containers
  for (VdcMap::iterator pos = vdcs.begin(); pos != vdcs.end(); ++pos) {
    pos->second->setLearnMode(false, false, undefined);
  }
  LOG(LOG_NOTICE, "=== stopped learning");
  learningMode = false;
  learnHandler.clear();
}


void VdcHost::reportLearnEvent(bool aLearnIn, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    if (aLearnIn) {
      LOG(LOG_NOTICE, "--- learned in (paired) new device(s)");
    }
    else {
      LOG(LOG_NOTICE, "--- learned out (unpaired) device(s)");
    }
  }
  // report status
  if (learnHandler) {
    learnHandler(aLearnIn, aError);
  }
}



// MARK: - activity monitoring


void VdcHost::signalActivity()
{
  lastActivity = MainLoop::now();
  postEvent(vdchost_activitysignal);
}



void VdcHost::setUserActionMonitor(DeviceUserActionCB aUserActionCB)
{
  deviceUserActionHandler = aUserActionCB;
}


bool VdcHost::signalDeviceUserAction(Device &aDevice, bool aRegular)
{
  LOG(LOG_INFO, "vdSD %s: reports %s user action", aDevice.shortDesc().c_str(), aRegular ? "regular" : "identification");
  if (deviceUserActionHandler) {
    deviceUserActionHandler(DevicePtr(&aDevice), aRegular);
    return true; // suppress normal action
  }
  if (!aRegular) {
    // this is a non-regular user action, i.e. one for identification purposes. Generate special identification notification
    VdcApiConnectionPtr api = getSessionConnection();
    if (api) {
      // send an identify notification
      aDevice.sendRequest("identify", ApiValuePtr(), NULL);
    }
    return true; // no normal action, prevent further processing
  }
  return false; // normal processing
}




// MARK: - periodic activity


#define PERIODIC_TASK_INTERVAL (5*Second)
#define PERIODIC_TASK_FORCE_INTERVAL (1*Minute)

#define ACTIVITY_PAUSE_INTERVAL (1*Second)

void VdcHost::periodicTask(MLMicroSeconds aNow)
{
  // cancel any pending executions
  periodicTaskTicket.cancel();
  // prevent during activity as saving DB might affect performance
  if (
    (aNow>lastActivity+ACTIVITY_PAUSE_INTERVAL) || // some time passed after last activity or...
    (aNow>lastPeriodicRun+PERIODIC_TASK_FORCE_INTERVAL) // ...too much time passed since last run
  ) {
    lastPeriodicRun = aNow;
    if (!collecting) {
      // re-check network connection, might cause re-collection in some vdcs
      isNetworkConnected();
      // track time of day changes
      checkTimeOfDayChange();
      // check again for devices and vdcs that need to be announced
      startAnnouncing();
      // do a save run as well
      // - myself
      save();
      #if ENABLE_LOCALCONTROLLER
      if (localController) localController->save();
      #endif
      // - device containers
      for (VdcMap::iterator pos = vdcs.begin(); pos!=vdcs.end(); ++pos) {
        pos->second->save();
      }
      // - devices
      for (DsDeviceMap::iterator pos = dSDevices.begin(); pos!=dSDevices.end(); ++pos) {
        pos->second->save();
      }
    }
  }
  if (mainloopStatsInterval>0) {
    // show mainloop statistics
    if (mainLoopStatsCounter<=0) {
      LOG(LOG_INFO, "%s", MainLoop::currentMainLoop().description().c_str());
      MainLoop::currentMainLoop().statistics_reset();
      mainLoopStatsCounter = mainloopStatsInterval;
    }
    else {
      --mainLoopStatsCounter;
    }
  }
  // schedule next run
  periodicTaskTicket.executeOnce(boost::bind(&VdcHost::periodicTask, this, _2), PERIODIC_TASK_INTERVAL);
}


// MARK: - local operation mode


bool VdcHost::checkForLocalClickHandling(ButtonBehaviour &aButtonBehaviour, DsClickType aClickType)
{
  #if ENABLE_LOCALCONTROLLER
  if (localController) {
    if (localController->processButtonClick(aButtonBehaviour, aClickType)) {
      LOG(LOG_NOTICE, "localcontroller has handled clicktype %d from Button[%zu] '%s' in %s", aClickType, aButtonBehaviour.index, aButtonBehaviour.getHardwareName().c_str(), aButtonBehaviour.device.shortDesc().c_str());
      return true; // handled
    }
  }
  #endif
  // not handled by local controller
  if (!activeSessionConnection) {
    // not connected to a vdSM, handle clicks locally
    handleClickLocally(aButtonBehaviour, aClickType);
    return true; // handled
  }
  return false; // not handled
}


void VdcHost::handleClickLocally(ButtonBehaviour &aButtonBehaviour, DsClickType aClickType)
{
  #if ENABLE_LOCAL_BEHAVIOUR
  if (aButtonBehaviour.buttonFunc==buttonFunc_app || aButtonBehaviour.buttonGroup!=group_yellow_light) {
    return; // do not try to handle non-light or app buttons
  }
  // TODO: Not really conforming to ds-light yet...
  int scene = -1; // none
  // if button has up/down, direction is derived from button
  int newDirection = aButtonBehaviour.localFunctionElement()==buttonElement_up ? 1 : (aButtonBehaviour.localFunctionElement()==buttonElement_down ? -1 : 0); // -1=down/off, 1=up/on, 0=toggle
  if (newDirection!=0)
    localDimDirection = newDirection;
  switch (aClickType) {
    case ct_tip_1x:
    case ct_click_1x:
      scene = ROOM_ON;
      // toggle direction if click has none
      if (newDirection==0)
        localDimDirection *= -1; // reverse if already determined
      break;
    case ct_tip_2x:
    case ct_click_2x:
      scene = PRESET_2;
      break;
    case ct_tip_3x:
    case ct_click_3x:
      scene = PRESET_3;
      break;
    case ct_tip_4x:
      scene = PRESET_4;
      break;
    case ct_hold_start:
      scene = INC_S; // just as a marker to start dimming (we'll use dimChannelForArea(), not legacy dimming!)
      // toggle direction if click has none
      if (newDirection==0)
        localDimDirection *= -1; // reverse if already determined
      break;
    case ct_hold_end:
      scene = STOP_S; // just as a marker to stop dimming (we'll use dimChannelForArea(), not legacy dimming!)
      break;
    default:
      break;
  }
  if (scene>=0) {
    signalActivity(); // local activity
    // some action to perform on every light device
    for (DsDeviceMap::iterator pos = dSDevices.begin(); pos!=dSDevices.end(); ++pos) {
      DevicePtr dev = pos->second;
      LightBehaviourPtr l = boost::dynamic_pointer_cast<LightBehaviour>(dev->getOutput());
      if (l) {
        ChannelBehaviourPtr channel = dev->getChannelByType(aButtonBehaviour.buttonChannel);
        if (scene==STOP_S) {
          // stop dimming
          dev->dimChannelForArea(channel, dimmode_stop, 0, 0);
        }
        else {
          // call scene or start dimming
          // - figure out direction if not already known
          if (localDimDirection==0 && l->brightness->getLastSync()!=Never) {
            // get initial direction from current value of first encountered light with synchronized brightness value
            localDimDirection = l->brightness->getChannelValue() >= l->brightness->getMinDim() ? -1 : 1;
          }
          if (scene==INC_S) {
            // Start dimming
            // - minimum scene if not already there
            if (localDimDirection>0 && l->brightness->getChannelValue()==0) {
              // starting dimming up from minimum
              l->brightness->setChannelValue(l->brightness->getMinDim(), 0, true);
            }
            // now dim (safety timeout after 10 seconds)
            dev->dimChannelForArea(channel, localDimDirection>0 ? dimmode_up : dimmode_down, 0, 10*Second);
          }
          else {
            // call a scene
            if (localDimDirection<0)
              scene = ROOM_OFF; // switching off a scene = call off scene
            dev->callScene(scene, true);
          }
        }
      }
    }
  }
  #endif // ENABLE_LOCAL_BEHAVIOUR
}


// MARK: - notification delivery


NotificationGroup::NotificationGroup(VdcPtr aVdc, DsAddressablePtr aFirstMember) :
  vdc(aVdc)
{
  if (aFirstMember) {
    members.push_back(aFirstMember);
  }
}



void VdcHost::addTargetToAudience(NotificationAudience &aAudience, DsAddressablePtr aTarget)
{
  VdcPtr vdc;
  DevicePtr dev = boost::dynamic_pointer_cast<Device>(aTarget);
  if (dev) {
    // is a device, associated with a vDC
    vdc = dev->vdcP;
  }
  // search for notification group for this vdc (for devices, vdc!=NULL) or none (for other addressables, vdc==NULL)
  for (NotificationAudience::iterator pos = aAudience.begin(); pos!=aAudience.end(); ++pos) {
    if (pos->vdc==vdc) {
      // vdc group already exists, add device
      pos->members.push_back(aTarget);
      return;
    }
  }
  // vdc group does not yet exist, create it
  aAudience.push_back(NotificationGroup(vdc, aTarget));
  return;
}



ErrorPtr VdcHost::addToAudienceByDsuid(NotificationAudience &aAudience, const DsUid &aDsuid)
{
  if (aDsuid.empty()) {
    return Error::err<VdcApiError>(415, "missing/invalid dSUID");
  }
  DsAddressablePtr a = addressableForDsUid(aDsuid);
  if (a) {
    addTargetToAudience(aAudience, a);
    return ErrorPtr();
  }
  else {
    return Error::err<VdcApiError>(404, "unknown dSUID");
  }
}


ErrorPtr VdcHost::addToAudienceByItemSpec(NotificationAudience &aAudience, const string &aItemSpec)
{
  DsAddressablePtr a = addressableForItemSpec(aItemSpec);
  if (a) {
    addTargetToAudience(aAudience, a);
    return ErrorPtr();
  }
  else {
    return Error::err<VdcApiError>(404, "missing/invalid itemSpec");
  }
}


void VdcHost::addToAudienceByZoneAndGroup(NotificationAudience &aAudience, DsZoneID aZone, DsGroup aGroup)
{
  // Zone 0 = all zones
  // group_undefined (0) = all groups
  for (DsDeviceMap::iterator pos = dSDevices.begin(); pos!=dSDevices.end(); ++pos) {
    Device *devP = pos->second.get();
    if (
      (aZone==0 || devP->getZoneID()==aZone) &&
      (aGroup==group_undefined || (devP->getOutput() && devP->getOutput()->isMember(aGroup)))
    ) {
      addTargetToAudience(aAudience, DsAddressablePtr(devP));
    }
  }
}



void VdcHost::deliverToAudience(NotificationAudience &aAudience, VdcApiConnectionPtr aApiConnection, const string &aNotification, ApiValuePtr aParams)
{
  for (NotificationAudience::iterator gpos = aAudience.begin(); gpos!=aAudience.end(); ++gpos) {
    if (gpos->vdc) {
      OLOG(LOG_INFO, "==== passing '%s' for %lu devices for delivery to vDC %s", aNotification.c_str(), gpos->members.size(), gpos->vdc->shortDesc().c_str());
      // let vdc process this, might be able to optimize delivery using hardware's native mechanisms such as scenes or groups
      gpos->vdc->deliverToAudience(gpos->members, aApiConnection, aNotification, aParams);
    }
    else {
      OLOG(LOG_INFO, "==== delivering notification '%s' to %lu non-devices now", aNotification.c_str(), gpos->members.size());
      // just deliver to each member, no optimization for non-devices
      for (DsAddressablesList::iterator apos = gpos->members.begin(); apos!=gpos->members.end(); ++apos) {
        (*apos)->handleNotification(aApiConnection, aNotification, aParams);
      }
    }
  }
}


void VdcHost::deviceWillApplyNotification(DevicePtr aDevice, NotificationDeliveryState &aDeliveryState)
{
  #if ENABLE_LOCALCONTROLLER
  if (localController) localController->deviceWillApplyNotification(aDevice, aDeliveryState);
  #endif
}




// MARK: - vDC API


bool VdcHost::sendApiRequest(const string &aMethod, ApiValuePtr aParams, VdcApiResponseCB aResponseHandler)
{
  if (activeSessionConnection) {
    signalActivity();
    return Error::isOK(activeSessionConnection->sendRequest(aMethod, aParams, aResponseHandler));
  }
  // cannot send
  return false;
}


void VdcHost::vdcApiConnectionStatusHandler(VdcApiConnectionPtr aApiConnection, ErrorPtr &aError)
{
  if (Error::isOK(aError)) {
    // new connection, set up reequest handler
    aApiConnection->setRequestHandler(boost::bind(&VdcHost::vdcApiRequestHandler, this, _1, _2, _3, _4));
  }
  else {
    // error or connection closed
    if (!aError->isError(SocketCommError::domain(), SocketCommError::HungUp)) {
      LOG(LOG_ERR, "vDC API connection closing due to error: %s", aError->text());
    }
    // - close if not already closed
    aApiConnection->closeConnection();
    if (aApiConnection==activeSessionConnection) {
      // this is the active session connection
      resetAnnouncing(); // stop possibly ongoing announcing
      activeSessionConnection.reset();
      postEvent(vdchost_vdcapi_disconnected);
      LOG(LOG_NOTICE, "=== vDC API session ends because connection closed");
    }
    else {
      LOG(LOG_NOTICE, "=== vDC API connection (not yet in session) closed");
    }
  }
}


void VdcHost::vdcApiRequestHandler(VdcApiConnectionPtr aApiConnection, VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  signalActivity();
  // now process
  if (aRequest) {
    // Methods
    // - Check session init/end methods
    if (aMethod=="hello") {
      respErr = helloHandler(aRequest, aParams);
    }
    else if (aMethod=="bye") {
      respErr = byeHandler(aRequest, aParams);
    }
    else {
      if (activeSessionConnection) {
        // session active
        respErr = handleMethodForParams(aRequest, aMethod, aParams);
      }
      else {
        // all following methods must have an active session
        respErr = Error::err<VdcApiError>(401, "no vDC session - cannot call method");
      }
    }
  }
  else {
    // Notifications
    // Note: out of session, notifications are simply ignored
    if (activeSessionConnection) {
      respErr = handleNotificationForParams(aApiConnection, aMethod, aParams);
    }
    else {
      LOG(LOG_INFO, "Received notification '%s' out of session -> ignored", aMethod.c_str());
    }
  }
  // check status
  // Note: in case method call triggers an action that does not immediately complete,
  //   we'll get NULL for respErr here, and method handler must take care of acknowledging the method call!
  if (respErr) {
    // method call immediately returned a status (might be explicit OK error object)
    if (aRequest) {
      // report back in case of method call
      aRequest->sendStatus(respErr);
    }
    else {
      // just log in case of error of a notification
      if (Error::notOK(respErr)) {
        LOG(LOG_WARNING, "Notification '%s' processing error: %s", aMethod.c_str(), respErr->text());
      }
    }
  }
}


ErrorPtr VdcHost::helloHandler(VdcApiRequestPtr aRequest, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  ApiValuePtr v;
  string s;
  // check API version
  if (Error::isOK(respErr = checkParam(aParams, "api_version", v))) {
    int version = v->int32Value();
    int maxversion = (maxApiVersion==0 || maxApiVersion>=VDC_API_VERSION_MAX) ? VDC_API_VERSION_MAX : maxApiVersion;
    if (version<VDC_API_VERSION_MIN || version>maxversion) {
      // incompatible version
      respErr = Error::err<VdcApiError>(505, "Incompatible vDC API version - found %d, expected %d..%d", version, VDC_API_VERSION_MIN, maxversion);
      LOG(LOG_WARNING, "=== hello rejected: %s", respErr->text());
    }
    else {
      // API version ok, save it
      aRequest->connection()->setApiVersion(version);
      // check dSUID
      DsUid vdsmDsUid;
      if (Error::isOK(respErr = checkDsuidParam(aParams, "dSUID", vdsmDsUid))) {
        // same vdSM can restart session any time. Others will be rejected
        if (!activeSessionConnection || vdsmDsUid==connectedVdsm) {
          // ok to start new session
          if (activeSessionConnection) {
            // session connection was already there, re-announce
            resetAnnouncing();
          }
          // - start session with this vdSM
          connectedVdsm = vdsmDsUid;
          // - remember the session's connection
          activeSessionConnection = aRequest->connection();
          // - log connection
          const char *ip = "<unknown>";
          if (activeSessionConnection->socketConnection()) {
            ip = activeSessionConnection->socketConnection()->getHost();
          }
          LOG(LOG_NOTICE, "=== vdSM %s (%s) starts new session with API Version %d", vdsmDsUid.getString().c_str(), ip, version);
          // - inform interested objects
          postEvent(vdchost_vdcapi_connected);
          // - create answer
          ApiValuePtr result = activeSessionConnection->newApiValue();
          result->setType(apivalue_object);
          result->add("dSUID", aParams->newBinary(getDsUid().getBinary()));
          aRequest->sendResult(result);
          // - trigger announcing devices
          startAnnouncing();
        }
        else {
          // not ok to start new session, reject
          respErr = Error::err<VdcApiError>(503, "this vDC already has an active session with vdSM %s",connectedVdsm.getString().c_str());
          LOG(LOG_WARNING, "=== hello rejected: %s", respErr->text());
          aRequest->sendError(respErr);
          // close after send
          aRequest->connection()->closeAfterSend();
          // prevent sending error again
          respErr.reset();
        }
      }
    }
  }
  return respErr;
}


ErrorPtr VdcHost::byeHandler(VdcApiRequestPtr aRequest, ApiValuePtr aParams)
{
  LOG(LOG_NOTICE, "=== vDC API connection will close due to 'bye' command");
  // always confirm Bye, even out-of-session, so using aJsonRpcComm directly to answer (jsonSessionComm might not be ready)
  aRequest->sendResult(ApiValuePtr());
  // close after send
  aRequest->connection()->closeAfterSend();
  // success
  return ErrorPtr();
}



DsAddressablePtr VdcHost::addressableForItemSpec(const string &aItemSpec)
{
  string query = aItemSpec;
  if(query.find("vdc:")==0) {
    // starts with "vdc:" -> look for vdc by implementationId (vdcClassIdentifier()) and instance no
    query.erase(0, 4); // remove "vdc:" prefix
    // ccccccc[:ii] cccc=vdcClassIdentifier(), ii=instance
    size_t i=query.find(':');
    int instanceNo = 1; // default to first instance
    if (i!=string::npos) {
      // with instance number
      instanceNo = atoi(query.c_str()+i+1);
      query.erase(i); // cut off :iii part
    }
    for (VdcMap::iterator pos = vdcs.begin(); pos!=vdcs.end(); ++pos) {
      VdcPtr c = pos->second;
      if (
        strcmp(c->vdcClassIdentifier(), query.c_str())==0 &&
        c->getInstanceNumber()==instanceNo
      ) {
        // found - return this vDC container
        return c;
      }
    }
  }
  // nothing found
  return DsAddressablePtr();
}


DsAddressablePtr VdcHost::addressableForDsUid(const DsUid &aDsUid)
{
  // not special query, not empty dSUID
  if (aDsUid==getDsUid()) {
    // my own dSUID: vdc-host is addressed
    return DsAddressablePtr(this);
  }
  else {
    // Must be device or vdc level
    // - find device to handle it (more probable case)
    DsDeviceMap::iterator pos = dSDevices.find(aDsUid);
    if (pos!=dSDevices.end()) {
      return pos->second;
    }
    else {
      // is not a device, try vdcs
      VdcMap::iterator pos = vdcs.find(aDsUid);
      if (pos!=vdcs.end()) {
        return pos->second;
      }
    }
  }
  // not found
  return DsAddressablePtr();
}


ErrorPtr VdcHost::handleNotificationForParams(VdcApiConnectionPtr aApiConnection, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  // Notifications can be adressed to one or multiple dSUIDs explicitly, or sent to a zone_id/group pair
  // Notes
  // - for protobuf API, dSUID is always an array (as it is a repeated field in protobuf)
  // - for JSON API, caller may provide an array or a single dSUID.
  // - only if no explicit dSUID is provided, zoneId and group parameters are evaluated
  // collect a list of addressables for this notification
  NotificationAudience audience;
  bool audienceOk = false;
  // - check if there is a dSUID or a non-empty array of dSUIDs
  if (aParams) {
    ApiValuePtr o = aParams->get("dSUID");
    if (o) {
      // dSUID parameter found
      DsUid dsuid;
      if (o->isType(apivalue_array)) {
        // array of dSUIDs
        for (int i=0; i<o->arrayLength(); i++) {
          audienceOk = true; // non-empty array is a valid audience specification
          ApiValuePtr e = o->arrayGet(i);
          if (!dsuid.setAsBinary(e->binaryValue())) dsuid.clear();
          respErr = addToAudienceByDsuid(audience, dsuid);
          if (Error::notOK(respErr)) {
            respErr->prefixMessage("Ignored target for notification '%s': ", aMethod.c_str());
            LOG(LOG_INFO, "%s", respErr->text());
          }
        }
        respErr.reset();
      }
      else {
        // single dSUIDs
        if (!dsuid.setAsBinary(o->binaryValue())) dsuid.clear();
        respErr = addToAudienceByDsuid(audience, dsuid);
        audienceOk = true; // non-empty dSUID valid audience specification
      }
    }
    if (audience.empty() && (o = aParams->get("x-p44-itemSpec"))) {
      string itemSpec = o->stringValue();
      respErr = addToAudienceByItemSpec(audience, itemSpec);
      audienceOk = true; // non-empty itemSpec is valid audience specification
    }
    if (audience.empty()) {
      // evaluate zone_id/group
      o = aParams->get("zone_id");
      if (o) {
        DsZoneID zone = o->uint16Value();
        o = aParams->get("group");
        if (o) {
          audienceOk = true; // zone_id/group is valid audience spec
          DsGroup group = (DsGroup)o->uint16Value();
          addToAudienceByZoneAndGroup(audience, zone, group);
        }
      }
    }
  }
  if (!audienceOk) {
    respErr = Error::err<VdcApiError>(400, "notification needs dSUID, itemSpec or zone_id/group parameters");
  }
  else {
    // we have an audience, start delivery process
    deliverToAudience(audience, aApiConnection, aMethod, aParams);
  }
  return respErr;
}



ErrorPtr VdcHost::handleMethodForParams(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  DsUid dsuid;
  ErrorPtr respErr;
  if (Error::isOK(respErr = checkDsuidParam(aParams, "dSUID", dsuid))) {
    DsAddressablePtr addressable;
    if (dsuid.empty()) {
      // not addressing by dSUID, check for alternative addressing methods
      ApiValuePtr o = aParams->get("x-p44-itemSpec");
      if (o) {
        string itemSpec = o->stringValue();
        addressable = addressableForItemSpec(itemSpec);
      }
      else {
        // default to vdchost (allows start accessing a vdchost by getProperty without knowing a dSUID in the first place)
        addressable = DsAddressablePtr(this);
      }
    }
    else {
      // by dSUID
      addressable = addressableForDsUid(dsuid);
    }
    if (addressable) {
      // check special case of device remove command - we must execute this because device should not try to remove itself
      DevicePtr dev = boost::dynamic_pointer_cast<Device>(addressable);
      if (dev && aMethod=="remove") {
        return removeHandler(aRequest, dev);
      }
      // non-device addressable or not remove -> just let addressable handle the method itself
      return addressable->handleMethod(aRequest, aMethod, aParams);
    }
    else {
      LOG(LOG_WARNING, "Target entity %s not found for method '%s'", dsuid.getString().c_str(), aMethod.c_str());
      return Error::err<VdcApiError>(404, "unknown target (missing/invalid dSUID or itemSpec)");
    }
  }
  return respErr;
}


// MARK: - vDC level methods and notifications


ErrorPtr VdcHost::removeHandler(VdcApiRequestPtr aRequest, DevicePtr aDevice)
{
  // dS system wants to disconnect this device from this vDC. Try it and report back success or failure
  // Note: as disconnect() removes device from all containers, only aDevice may keep it alive until disconnection is complete.
  //   That's why we are passing aDevice to the handler, so we can be certain the device lives long enough
  aDevice->disconnect(true, boost::bind(&VdcHost::removeResultHandler, this, aDevice, aRequest, _1));
  return ErrorPtr();
}


void VdcHost::removeResultHandler(DevicePtr aDevice, VdcApiRequestPtr aRequest, bool aDisconnected)
{
  if (aDisconnected)
    aRequest->sendResult(ApiValuePtr()); // disconnected successfully
  else
    aRequest->sendError(Error::err<VdcApiError>(403, "Device cannot be removed, is still connected"));
}



// MARK: - session management


/// reset announcing devices (next startAnnouncing will restart from beginning)
void VdcHost::resetAnnouncing()
{
  // end pending announcement
  announcementTicket.cancel();
  // end all device sessions
  for (DsDeviceMap::iterator pos = dSDevices.begin(); pos!=dSDevices.end(); ++pos) {
    DevicePtr dev = pos->second;
    dev->announced = Never;
    dev->announcing = Never;
  }
  // end all vdc sessions
  for (VdcMap::iterator pos = vdcs.begin(); pos!=vdcs.end(); ++pos) {
    VdcPtr vdc = pos->second;
    vdc->announced = Never;
    vdc->announcing = Never;
  }
}



/// start announcing all not-yet announced entities to the vdSM
void VdcHost::startAnnouncing()
{
  if (!collecting && !announcementTicket && activeSessionConnection) {
    // start announcing
    announceNext();
  }
}


void VdcHost::announceNext()
{
  if (collecting) return; // prevent announcements during collect.
  // cancel re-announcing
  announcementTicket.cancel();
  // announce vdcs first
  for (VdcMap::iterator pos = vdcs.begin(); pos!=vdcs.end(); ++pos) {
    VdcPtr vdc = pos->second;
    if (
      vdc->isPublicDS() && // only public ones
      vdc->announced==Never &&
      (vdc->announcing==Never || MainLoop::now()>vdc->announcing+ANNOUNCE_RETRY_TIMEOUT) &&
      (!vdc->getVdcFlag(vdcflag_hidewhenempty) || vdc->getNumberOfDevices()>0)
    ) {
      // mark device as being in process of getting announced
      vdc->announcing = MainLoop::now();
      // send announcevdc request
      ApiValuePtr params = getSessionConnection()->newApiValue();
      params->setType(apivalue_object);
      params->add("dSUID", params->newBinary(vdc->getDsUid().getBinary()));
      if (!sendApiRequest("announcevdc", params, boost::bind(&VdcHost::announceResultHandler, this, vdc, _2, _3, _4))) {
        LOG(LOG_ERR, "Could not send vdc announcement message for %s %s", vdc->entityType(), vdc->shortDesc().c_str());
        vdc->announcing = Never; // not registering
      }
      else {
        LOG(LOG_NOTICE, "Sent vdc announcement for %s %s", vdc->entityType(), vdc->shortDesc().c_str());
      }
      // schedule a retry
      announcementTicket.executeOnce(boost::bind(&VdcHost::announceNext, this), ANNOUNCE_TIMEOUT);
      // done for now, continues after ANNOUNCE_TIMEOUT or when registration acknowledged
      return;
    }
  }
  // check all devices for unnannounced ones and announce those
  for (DsDeviceMap::iterator pos = dSDevices.begin(); pos!=dSDevices.end(); ++pos) {
    DevicePtr dev = pos->second;
    if (
      dev->isPublicDS() && // only public ones
      (dev->vdcP->isAnnounced()) && // class container must have already completed an announcement...
      !dev->isAnnounced() && // ...but not yet device...
      (dev->announcing==Never || MainLoop::now()>dev->announcing+ANNOUNCE_RETRY_TIMEOUT) // ...and not too soon after last attempt to announce
    ) {
      // mark device as being in process of getting announced
      dev->announcing = MainLoop::now();
      // send announcedevice request
      ApiValuePtr params = getVdcHost().getSessionConnection()->newApiValue();
      params->setType(apivalue_object);
      // include link to vdc for device announcements
      params->add("vdc_dSUID", params->newBinary(dev->vdcP->getDsUid().getBinary()));
      if (!dev->sendRequest("announcedevice", params, boost::bind(&VdcHost::announceResultHandler, this, dev, _2, _3, _4))) {
        LOG(LOG_ERR, "Could not send device announcement message for %s %s", dev->entityType(), dev->shortDesc().c_str());
        dev->announcing = Never; // not announcing
      }
      else {
        LOG(LOG_NOTICE, "Sent device announcement for %s %s", dev->entityType(), dev->shortDesc().c_str());
      }
      // schedule a retry
      announcementTicket.executeOnce(boost::bind(&VdcHost::announceNext, this), ANNOUNCE_TIMEOUT);
      // done for now, continues after ANNOUNCE_TIMEOUT or when announcement acknowledged
      return;
    }
  }
}


void VdcHost::announceResultHandler(DsAddressablePtr aAddressable, VdcApiRequestPtr aRequest, ErrorPtr &aError, ApiValuePtr aResultOrErrorData)
{
  if (Error::isOK(aError)) {
    // set device announced successfully
    LOG(LOG_NOTICE, "Announcement for %s %s acknowledged by vdSM", aAddressable->entityType(), aAddressable->shortDesc().c_str());
    aAddressable->announced = MainLoop::now();
    aAddressable->announcing = Never; // not announcing any more
    aAddressable->announcementAcknowledged(); // give instance opportunity to do things following an announcement
  }
  // cancel retry timer
  announcementTicket.cancel();
  // try next announcement, after a pause
  announcementTicket.executeOnce(boost::bind(&VdcHost::announceNext, this), ANNOUNCE_PAUSE);
}


// MARK: - DsAddressable API implementation

ErrorPtr VdcHost::handleMethod(VdcApiRequestPtr aRequest,  const string &aMethod, ApiValuePtr aParams)
{
  #if ENABLE_LOCALCONTROLLER
  if (localController) {
    ErrorPtr lcErr;
    if (localController->handleLocalControllerMethod(lcErr, aRequest, aMethod, aParams)) {
      // local controller did or will handle the method
      return lcErr;
    }
  }
  #endif
  #if P44SCRIPT_FULL_SUPPORT
  if (aMethod=="x-p44-scriptExec") {
    // direct execution of a script command line in the common main/initscript context
    ApiValuePtr o = aParams->get("script");
    if (o) {
      ScriptSource src(sourcecode+regular+keepvars+concurrently+floatingGlobs, "scriptExec", this);
      src.setSource(o->stringValue());
      src.setSharedMainContext(vdcHostScriptContext);
      src.run(inherit, boost::bind(&VdcHost::scriptExecHandler, this, aRequest, _1));
    }
    else {
      aRequest->sendStatus(NULL); // no script -> NOP
    }
    return ErrorPtr();
  }
  if (aMethod=="x-p44-restartMain") {
    // re-run the main script
    OLOG(LOG_NOTICE, "Re-starting global main script");
    mainScript.run(stopall, boost::bind(&VdcHost::globalScriptEnds, this, _1, mainScript.getOriginLabel()), Infinite);
    return Error::ok();
  }
  if (aMethod=="x-p44-stopMain") {
    // re-run the main script
    OLOG(LOG_NOTICE, "Stopping global main script");
    vdcHostScriptContext->abort(stopall, new ErrorValue(ScriptError::Aborted, "main script stopped"));
    return Error::ok();
  }
  if (aMethod=="x-p44-checkMain") {
    // check the main script for syntax errors (but do not re-start it)
    ScriptObjPtr res = mainScript.syntaxcheck();
    ApiValuePtr checkResult = aRequest->newApiValue();
    checkResult->setType(apivalue_object);
    if (!res || !res->isErr()) {
      OLOG(LOG_NOTICE, "Checked global main script: syntax OK");
      checkResult->add("result", checkResult->newNull());
    }
    else {
      OLOG(LOG_NOTICE, "Error in global main script: %s", res->errorValue()->text());
      checkResult->add("error", checkResult->newString(res->errorValue()->getErrorMessage()));
      SourceCursor* cursor = res->cursor();
      if (cursor) {
        checkResult->add("at", checkResult->newUint64(cursor->textpos()));
        checkResult->add("line", checkResult->newUint64(cursor->lineno()));
        checkResult->add("char", checkResult->newUint64(cursor->charpos()));
      }
    }
    aRequest->sendResult(checkResult);
  }
  #endif // P44SCRIPT_FULL_SUPPORT
  return inherited::handleMethod(aRequest, aMethod, aParams);
}


bool VdcHost::handleNotification(VdcApiConnectionPtr aApiConnection, const string &aNotification, ApiValuePtr aParams)
{
  return inherited::handleNotification(aApiConnection, aNotification, aParams);
}



// MARK: - property access

static char vdchost_obj;
static char vdcs_obj;
static char vdc_obj;
static char localController_obj;

enum {
  vdcs_key,
  valueSources_key,
  persistentChannels_key,
  writeOperations_key,
  latitude_key,
  longitude_key,
  heightabovesea_key,
  #if P44SCRIPT_FULL_SUPPORT
  mainscript_key,
  #endif
  #if ENABLE_LOCALCONTROLLER
  localController_key,
  #endif
  #if !REDUCED_FOOTPRINT
  scenesList_key,
  #endif
  nextVersion_key,
  numVdcHostProperties
};



int VdcHost::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aParentDescriptor->hasObjectKey(vdcs_obj)) {
    return (int)vdcs.size();
  }
  return inherited::numProps(aDomain, aParentDescriptor)+numVdcHostProperties;
}


// note: is only called when getDescriptorByName does not resolve the name
PropertyDescriptorPtr VdcHost::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numVdcHostProperties] = {
    { "x-p44-vdcs", apivalue_object+propflag_container, vdcs_key, OKEY(vdcs_obj) },
    { "x-p44-valueSources", apivalue_null, valueSources_key, OKEY(vdchost_obj) },
    { "x-p44-persistentChannels", apivalue_bool, persistentChannels_key, OKEY(vdchost_obj) },
    { "x-p44-writeOperations", apivalue_uint64, writeOperations_key, OKEY(vdchost_obj) },
    { "x-p44-latitude", apivalue_double, latitude_key, OKEY(vdchost_obj) },
    { "x-p44-longitude", apivalue_double, longitude_key, OKEY(vdchost_obj) },
    { "x-p44-heightabovesea", apivalue_double, heightabovesea_key, OKEY(vdchost_obj) },
    #if P44SCRIPT_FULL_SUPPORT
    { "x-p44-mainscript", apivalue_string, mainscript_key, OKEY(vdchost_obj) },
    #endif
    #if ENABLE_LOCALCONTROLLER
    { "x-p44-localController", apivalue_object, localController_key, OKEY(localController_obj) },
    #endif
    #if !REDUCED_FOOTPRINT
    { "x-p44-scenesList", apivalue_null, scenesList_key, OKEY(vdchost_obj) },
    #endif
    { "x-p44-nextVersion", apivalue_string, nextVersion_key, OKEY(vdchost_obj) },
  };
  int n = inherited::numProps(aDomain, aParentDescriptor);
  if (aPropIndex<n)
    return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor); // base class' property
  aPropIndex -= n; // rebase to 0 for my own first property
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}


PropertyDescriptorPtr VdcHost::getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyAccessMode aMode, PropertyDescriptorPtr aParentDescriptor)
{
  if (aParentDescriptor->hasObjectKey(vdcs_obj)) {
    // accessing one of the vdcs by numeric index
    return getDescriptorByNumericName(
      aPropMatch, aStartIndex, aDomain, aParentDescriptor,
      OKEY(vdc_obj)
    );
  }
  // None of the containers within vdc host - let base class handle root-Level properties
  return inherited::getDescriptorByName(aPropMatch, aStartIndex, aDomain, aMode, aParentDescriptor);
}


PropertyContainerPtr VdcHost::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->isArrayContainer()) {
    // local container
    return PropertyContainerPtr(this); // handle myself
  }
  #if ENABLE_LOCALCONTROLLER
  else if (aPropertyDescriptor->hasObjectKey(localController_obj)) {
    return localController; // can be NULL if local controller is not enabled
  }
  #endif
  else if (aPropertyDescriptor->hasObjectKey(vdc_obj)) {
    // - just iterate into map, we'll never have more than a few logical vdcs!
    int i = 0;
    for (VdcMap::iterator pos = vdcs.begin(); pos!=vdcs.end(); ++pos) {
      if (i==aPropertyDescriptor->fieldKey()) {
        // found
        return pos->second;
      }
      i++;
    }
  }
  // unknown here
  return NULL;
}


bool VdcHost::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(vdchost_obj)) {
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        case valueSources_key:
          aPropValue->setType(apivalue_object); // make object (incoming object is NULL)
          createValueSourcesList(aPropValue);
          return true;
        #if !REDUCED_FOOTPRINT
        case scenesList_key:
          aPropValue->setType(apivalue_object); // make object (incoming object is NULL)
          createScenesList(aPropValue);
          return true;
        #endif
        case persistentChannels_key:
          aPropValue->setBoolValue(persistentChannels);
          return true;
        case writeOperations_key:
          aPropValue->setUint32Value(dsParamStore.writeOpsCount);
          return true;
        case latitude_key:
          aPropValue->setDoubleValue(geolocation.latitude);
          return true;
        case longitude_key:
          aPropValue->setDoubleValue(geolocation.longitude);
          return true;
        #if P44SCRIPT_FULL_SUPPORT
        case mainscript_key:
          aPropValue->setStringValue(mainScript.getSource());
          return true;
        #endif
        case nextVersion_key:
          aPropValue->setStringValue(nextModelVersion());
          return true;
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        case persistentChannels_key:
          setPVar(persistentChannels, aPropValue->boolValue());
          return true;
        case latitude_key:
          setPVar(geolocation.latitude, aPropValue->doubleValue());
          return true;
        case longitude_key:
          setPVar(geolocation.longitude, aPropValue->doubleValue());
          return true;
        #if P44SCRIPT_FULL_SUPPORT
        case mainscript_key:
          if (mainScript.setSource(aPropValue->stringValue())) markDirty();
          return true;
        #endif
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


void VdcHost::createDeviceList(DeviceVector &aDeviceList)
{
  aDeviceList.clear();
  for (DsDeviceMap::iterator pos = dSDevices.begin(); pos!=dSDevices.end(); ++pos) {
    aDeviceList.push_back(pos->second);
  }
}



// MARK: - value sources

void VdcHost::createValueSourcesList(ApiValuePtr aApiObjectValue)
{
  // iterate through all devices and all of their sensors and inputs
  for (DsDeviceMap::iterator pos = dSDevices.begin(); pos!=dSDevices.end(); ++pos) {
    DevicePtr dev = pos->second;
    // Sensors
    for (BehaviourVector::iterator pos2 = dev->sensors.begin(); pos2!=dev->sensors.end(); ++pos2) {
      DsBehaviourPtr b = *pos2;
      ValueSource *vs = dynamic_cast<ValueSource *>(b.get());
      if (vs && vs->isEnabled()) {
        aApiObjectValue->add(vs->getSourceId(), aApiObjectValue->newString(vs->getSourceName().c_str()));
      }
    }
    // Inputs
    for (BehaviourVector::iterator pos2 = dev->inputs.begin(); pos2!=dev->inputs.end(); ++pos2) {
      DsBehaviourPtr b = *pos2;
      ValueSource *vs = dynamic_cast<ValueSource *>(b.get());
      if (vs && vs->isEnabled()) {
        aApiObjectValue->add(vs->getSourceId(), aApiObjectValue->newString(vs->getSourceName().c_str()));
      }
    }
    // Buttons
    for (BehaviourVector::iterator pos2 = dev->buttons.begin(); pos2!=dev->buttons.end(); ++pos2) {
      DsBehaviourPtr b = *pos2;
      ValueSource *vs = dynamic_cast<ValueSource *>(b.get());
      if (vs && vs->isEnabled()) {
        aApiObjectValue->add(vs->getSourceId(), aApiObjectValue->newString(vs->getSourceName().c_str()));
      }
    }
    #if P44SCRIPT_FULL_SUPPORT
    // Channels
    // Note: we do not (yet) expose channels in the list. Channels can be explicitly referenced using p44script device(x).output.channel(y)
    #endif // P44SCRIPT_FULL_SUPPORT
  }
}


ValueSource *VdcHost::getValueSourceById(string aValueSourceID)
{
  ValueSource *valueSource = NULL;
  // value source ID is
  //  dSUID_Sx for sensors (x=sensor index)
  //  dSUID_Ix for inputs (x=input index)
  //  dSUID_Bx for buttons (x=button index)
  //  dSUID_Ciii for channels (ii=channel id)
  // - extract dSUID
  size_t i = aValueSourceID.find("_");
  if (i!=string::npos) {
    DsUid dsuid(aValueSourceID.substr(0,i));
    DsDeviceMap::iterator pos = dSDevices.find(dsuid);
    if (pos!=dSDevices.end()) {
      // is a device
      DevicePtr dev = pos->second;
      const char *p = aValueSourceID.c_str()+i+1;
      if (*p) {
        // first character is type: I=Input, S=Sensor, B=Button
        char ty = *p++;
        DsBehaviourPtr bhv;
        switch (ty) {
          case 'S' : bhv = dev->getSensor(Device::by_id_or_index, string(p)); break;
          case 'I' : bhv = dev->getInput(Device::by_id_or_index, string(p)); break;
          case 'B' : bhv = dev->getButton(Device::by_id_or_index, string(p)); break;
          #if P44SCRIPT_FULL_SUPPORT
          case 'C' : {
            ChannelBehaviourPtr cbhv = dev->getChannelById(p);
            if (cbhv) {
              valueSource = dynamic_cast<ValueSource *>(cbhv.get());
            }
            break;
          }
          #endif // P44SCRIPT_FULL_SUPPORT
        }
        if (bhv) {
          valueSource = dynamic_cast<ValueSource *>(bhv.get());
        }
      }
    }
  }
  return valueSource;
}



// MARK: - persistent vdc host level parameters

ErrorPtr VdcHost::loadAndFixDsUID()
{
  ErrorPtr err;
  // generate a default dSUID if no external one is given
  if (!externalDsuid) {
    // we don't have a fixed external dSUID to base everything on, so create a dSUID of our own:
    // single vDC per MAC-Adress scenario: generate UUIDv5 with name = macaddress
    // - calculate UUIDv5 based dSUID
    DsUid vdcNamespace(DSUID_VDC_NAMESPACE_UUID);
    string m = mac ? macAddressToString(mac,0) : "UnknownMACAddress";
    if (vdcHostInstance>0) string_format_append(m, "_%d", vdcHostInstance); // add-in instance number
    dSUID.setNameInSpace(m, vdcNamespace);
  }
  DsUid originalDsUid = dSUID;
  // load the vdc host settings, which might override the default dSUID
  err = loadFromStore(entityType()); // is a singleton, identify by type
  if (Error::notOK(err)) LOG(LOG_ERR,"Error loading settings for vdc host: %s", err->text());
  #if ENABLE_SETTINGS_FROM_FILES
  // check for settings from files
  loadSettingsFromFiles();
  #endif
  // now check
  if (!externalDsuid) {
    if (storedDsuid) {
      // a dSUID was loaded from DB -> check if different from default
      if (!(originalDsUid==dSUID)) {
        // stored dSUID is not same as MAC derived -> we are running a migrated config
        LOG(LOG_WARNING,"Running a migrated configuration: dSUID collisions with original unit possible");
        LOG(LOG_WARNING,"- native vDC host dSUID of this instance would be %s", originalDsUid.getString().c_str());
        LOG(LOG_WARNING,"- if this is not a replacement unit -> factory reset recommended!");
      }
    }
    else {
      // no stored dSUID was found so far -> we need to save the current one
      markDirty();
      save();
    }
  }
  #if ENABLE_LOCALCONTROLLER
  if (localController) localController->load();
  #endif
  return ErrorPtr();
}



ErrorPtr VdcHost::save()
{
  ErrorPtr err;
  // save the vdc settings
  err = saveToStore(entityType(), false); // is a singleton, identify by type, single instance
  return ErrorPtr();
}


ErrorPtr VdcHost::forget()
{
  // delete the vdc settings
  deleteFromStore();
  return ErrorPtr();
}


#if ENABLE_SETTINGS_FROM_FILES

void VdcHost::loadSettingsFromFiles()
{
  // try to open config file
  string fn = getConfigDir();
  fn += "vdchostsettings.csv";
  // if vdc has already stored properties, only explicitly marked properties will be applied
  if (loadSettingsFromFile(fn.c_str(), rowid!=0)) markClean();
}

#endif // ENABLE_SETTINGS_FROM_FILES


// MARK: - persistence implementation

// SQLIte3 table name to store these parameters to
const char *VdcHost::tableName()
{
  return "VdcHostSettings";
}


// data field definitions

static const size_t numFields = 7;

size_t VdcHost::numFieldDefs()
{
  return inheritedParams::numFieldDefs()+numFields;
}


const FieldDefinition *VdcHost::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numFields] = {
    { "vdcHostName", SQLITE_TEXT },
    { "vdcHostDSUID", SQLITE_TEXT },
    { "persistentChannels", SQLITE_INTEGER },
    { "latitude", SQLITE_FLOAT },
    { "longitude", SQLITE_FLOAT },
    { "heightabovesea", SQLITE_FLOAT },
    { "mainscript", SQLITE_TEXT }, // always have this field, but it's not always in use
  };
  if (aIndex<inheritedParams::numFieldDefs())
    return inheritedParams::getFieldDef(aIndex);
  aIndex -= inheritedParams::numFieldDefs();
  if (aIndex<numFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void VdcHost::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inheritedParams::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get the name
  setName(nonNullCStr(aRow->get<const char *>(aIndex++)));
  // get the vdc host dSUID
  if (!externalDsuid) {
    // only if dSUID is not set externally, we try to load it
    DsUid loadedDsUid;
    if (loadedDsUid.setAsString(nonNullCStr(aRow->get<const char *>(aIndex)))) {
      // dSUID string from DB is valid
      dSUID = loadedDsUid; // activate it as the vdc host dSUID
      storedDsuid = true; // we're using a stored dSUID now
    }
  }
  aIndex++;
  // the persistentchannels flag
  aRow->getIfNotNull(aIndex++, persistentChannels);
  aRow->getIfNotNull(aIndex++, geolocation.latitude);
  aRow->getIfNotNull(aIndex++, geolocation.longitude);
  aRow->getIfNotNull(aIndex++, geolocation.heightAboveSea);
  #if P44SCRIPT_FULL_SUPPORT
  mainScript.setSource(nonNullCStr(aRow->get<const char *>(aIndex++)), sourcecode);
  #else
  aIndex++; // just ignore
  #endif
}


// bind values to passed statement
void VdcHost::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inheritedParams::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, getAssignedName().c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  if (externalDsuid) {
    aStatement.bind(aIndex++); // do not save externally defined dSUIDs
  }
  else {
    aStatement.bind(aIndex++, dSUID.getString().c_str(), false); // not static, string is local obj
  }
  aStatement.bind(aIndex++, persistentChannels);
  aStatement.bind(aIndex++, geolocation.latitude);
  aStatement.bind(aIndex++, geolocation.longitude);
  aStatement.bind(aIndex++, geolocation.heightAboveSea);
  #if P44SCRIPT_FULL_SUPPORT
  aStatement.bind(aIndex++, mainScript.getSource().c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  #else
  aStatement.bind(aIndex++); // bind null
  #endif
}



// MARK: - description

string VdcHost::description()
{
  string d = string_format("VdcHost with %lu vDCs:", vdcs.size());
  for (VdcMap::iterator pos = vdcs.begin(); pos!=vdcs.end(); ++pos) {
    d.append("\n");
    d.append(pos->second->description());
  }
  return d;
}


#if !REDUCED_FOOTPRINT

// MARK: - scene names

const SceneKindDescriptor p44::roomScenes[] = {
  { ROOM_OFF, scene_room|scene_preset|scene_off , "off"},
  { AUTO_OFF, scene_room|scene_preset|scene_off|scene_extended , "slow off"},
  { ROOM_ON, scene_room|scene_preset, "preset 1" },
  { PRESET_2, scene_room|scene_preset, "preset 2" },
  { PRESET_3, scene_room|scene_preset, "preset 3" },
  { PRESET_4, scene_room|scene_preset, "preset 4" },
  { STANDBY, scene_room|scene_preset|scene_off|scene_extended, "standby" },
  { DEEP_OFF, scene_room|scene_preset|scene_off|scene_extended, "deep off" },
  { SLEEPING, scene_room|scene_preset|scene_off|scene_extended, "sleeping" },
  { WAKE_UP, scene_room|scene_preset|scene_extended, "wakeup" },
  { AREA_1_OFF, scene_room|scene_preset|scene_off|scene_area|scene_extended, "area 1 off" },
  { AREA_1_ON, scene_room|scene_preset|scene_area|scene_extended, "area 1 on" },
  { AREA_2_OFF, scene_room|scene_preset|scene_off|scene_area|scene_extended, "area 2 off" },
  { AREA_2_ON, scene_room|scene_preset|scene_area|scene_extended, "area 2 on" },
  { AREA_3_OFF, scene_room|scene_preset|scene_off|scene_area|scene_extended, "area 3 off" },
  { AREA_3_ON, scene_room|scene_preset|scene_area|scene_extended, "area 3 on" },
  { AREA_4_OFF, scene_room|scene_preset|scene_off|scene_area|scene_extended, "area 4 off" },
  { AREA_4_ON, scene_room|scene_preset|scene_area|scene_extended, "area 4 on" },
  { PRESET_OFF_10, scene_room|scene_preset|scene_off|scene_extended, "off 10" },
  { PRESET_11, scene_room|scene_preset|scene_extended, "preset 11" },
  { PRESET_12, scene_room|scene_preset|scene_extended, "preset 12" },
  { PRESET_13, scene_room|scene_preset|scene_extended, "preset 13" },
  { PRESET_14, scene_room|scene_preset|scene_extended, "preset 14" },
  { PRESET_OFF_20, scene_room|scene_preset|scene_off|scene_extended, "off 20" },
  { PRESET_21, scene_room|scene_preset|scene_extended, "preset 21" },
  { PRESET_22, scene_room|scene_preset|scene_extended, "preset 22" },
  { PRESET_23, scene_room|scene_preset|scene_extended, "preset 23" },
  { PRESET_24, scene_room|scene_preset|scene_extended, "preset 24" },
  { PRESET_OFF_30, scene_room|scene_preset|scene_off|scene_extended, "off 30" },
  { PRESET_31, scene_room|scene_preset|scene_extended, "preset 31" },
  { PRESET_32, scene_room|scene_preset|scene_extended, "preset 32" },
  { PRESET_33, scene_room|scene_preset|scene_extended, "preset 33" },
  { PRESET_34, scene_room|scene_preset|scene_extended, "preset 34" },
  { PRESET_OFF_40, scene_room|scene_preset|scene_off|scene_extended, "off 40" },
  { PRESET_41, scene_room|scene_preset|scene_extended, "preset 41" },
  { PRESET_42, scene_room|scene_preset|scene_extended, "preset 42" },
  { PRESET_43, scene_room|scene_preset|scene_extended, "preset 43" },
  { PRESET_44, scene_room|scene_preset|scene_extended, "preset 44" },
  { INVALID_SCENE_NO, 0, NULL } // terminator
};


const SceneKindDescriptor p44::globalScenes[] = {
  { ROOM_OFF, scene_global|scene_preset|scene_overlap|scene_off|scene_extended , "all off"},
  { ROOM_ON, scene_global|scene_preset|scene_overlap|scene_extended, "global preset 1" },
  { PRESET_2, scene_global|scene_preset|scene_overlap|scene_extended, "global preset 2" },
  { PRESET_3, scene_global|scene_preset|scene_overlap|scene_extended, "global preset 3" },
  { PRESET_4, scene_global|scene_preset|scene_overlap|scene_extended, "global preset 4" },
  { AUTO_STANDBY, scene_global, "auto-standby" },
  { STANDBY, scene_global|scene_preset|scene_overlap|scene_off, "standby" },
  { DEEP_OFF, scene_global|scene_preset|scene_overlap|scene_off, "deep off" },
  { SLEEPING, scene_global|scene_preset|scene_overlap|scene_off, "sleeping" },
  { WAKE_UP, scene_global|scene_preset|scene_overlap, "wakeup" },
  { PRESENT, scene_global|scene_preset, "present" },
  { ABSENT, scene_global|scene_preset, "absent" },
  { ZONE_ACTIVE, scene_global, "zone active" },
  { BELL1, scene_global|scene_preset, "bell 1" },
  { BELL2, scene_global|scene_preset|scene_extended, "bell 2" },
  { BELL3, scene_global|scene_preset|scene_extended, "bell 3" },
  { BELL4, scene_global|scene_preset|scene_extended, "bell 4" },
  { PANIC, scene_global|scene_preset, "panic" },
  { ALARM1, scene_global, "alarm 1" },
  { ALARM2, scene_global|scene_extended, "alarm 2" },
  { ALARM3, scene_global|scene_extended, "alarm 3" },
  { ALARM4, scene_global|scene_extended, "alarm 4" },
  { FIRE, scene_global, "fire" },
  { SMOKE, scene_global, "smoke" },
  { WATER, scene_global, "water" },
  { GAS, scene_global, "gas" },
  { WIND, scene_global, "wind" },
  { NO_WIND, scene_global, "no wind" },
  { RAIN, scene_global, "rain" },
  { NO_RAIN, scene_global, "no rain" },
  { HAIL, scene_global, "hail" },
  { NO_HAIL, scene_global, "no hail" },
  { POLLUTION, scene_global, "pollution" },
  { INVALID_SCENE_NO, 0 } // terminator
};


void VdcHost::createScenesList(ApiValuePtr aApiObjectValue)
{
  const SceneKindDescriptor* scenes = roomScenes;
  int idx = 0; // to keep the order
  for (int glob=0; glob<2; glob++) {
    while (scenes->no!=INVALID_SCENE_NO) {
      if ((scenes->kind&scene_overlap)==0) {
        ApiValuePtr sc = aApiObjectValue->newObject();
        sc->add("index", sc->newInt64(idx));
        sc->add("name", sc->newString(scenes->actionName));
        sc->add("kind", sc->newInt64(scenes->kind));
        aApiObjectValue->add(string_format("%d",scenes->no), sc);
        idx++;
      }
      scenes++;
    }
    scenes = globalScenes;
  }
}

#endif // REDUCED_FOOTPRINT



#if P44SCRIPT_FULL_SUPPORT || EXPRESSION_SCRIPT_SUPPORT

// MARK: - script API - ScriptCallConnection

ScriptCallConnection::ScriptCallConnection()
{
  setApiVersion(VDC_API_VERSION_MAX);
}


ApiValuePtr ScriptCallConnection::newApiValue()
{
  return ApiValuePtr(new JsonApiValue);
}


// MARK: - global vdc host scripts


#if ENABLE_P44SCRIPT

void VdcHost::runGlobalScripts()
{
  // command line provided script
  string scriptFn;
  string script;
  if (CmdLineApp::sharedCmdLineApp()->getStringOption("initscript", scriptFn)) {
    scriptFn = Application::sharedApplication()->resourcePath(scriptFn);
    ErrorPtr err = string_fromfile(scriptFn, script);
    if (Error::notOK(err)) {
      OLOG(LOG_ERR, "cannot open initscript: %s", err->text());
    }
    else {
      ScriptSource initScript(sourcecode+regular, "initscript", this);
      initScript.setSource(script, scriptbody|floatingGlobs);
      initScript.setSharedMainContext(vdcHostScriptContext);
      OLOG(LOG_NOTICE, "Starting initscript specified on commandline '%s'", scriptFn.c_str());
      initScript.run(regular|concurrently|keepvars, boost::bind(&VdcHost::globalScriptEnds, this, _1, initScript.getOriginLabel()), Infinite);
    }
  }
  // stored global script
  if (!mainScript.getSource().empty()) {
    OLOG(LOG_NOTICE, "Starting global main script");
    mainScript.run(regular|concurrently|keepvars, boost::bind(&VdcHost::globalScriptEnds, this, _1, mainScript.getOriginLabel()), Infinite);
  }
}

void VdcHost::globalScriptEnds(ScriptObjPtr aResult, const char *aOriginLabel)
{
  OLOG(aResult && aResult->isErr() ? LOG_WARNING : LOG_NOTICE, "Global %s script finished running, result=%s", aOriginLabel, ScriptObj::describe(aResult).c_str());
}


#else

class VdcHostScriptContext : public ScriptExecutionContext
{
  typedef ScriptExecutionContext inherited;
  VdcHost &p44VdcHost;

public:

  VdcHostScriptContext(P44VdcHost &aP44VdcHost) :
    inherited(&aP44VdcHost.geolocation),
    p44VdcHost(aP44VdcHost)
  {
  }

  bool evaluateAsyncFunction(const string &aFunc, const FunctionArguments &aArgs, bool &aNotYielded)
  {
    if (aFunc=="vdcapi" && aArgs.size()==1) {
      // vdcapi(jsoncall)
      if (aArgs[0].notValue()) return errorInArg(aArgs[0], false); // return error from argument
      // get method/notification and params
      JsonObjectPtr rq = aArgs[0].jsonValue();
      JsonObjectPtr m = rq->get("method");
      bool isMethod = false;
      ErrorPtr err;
      if (m) {
        isMethod = true;
      }
      else {
        m = rq->get("notification");
      }
      if (!m) {
        return throwError(Error::err<P44VdcError>(400, "invalid API request, must specify 'method' or 'notification'"));
      }
      else {
        // Note: the "method" or "notification" param will also be in the params, but should not cause any problem
        ApiValuePtr params = JsonApiValue::newValueFromJson(rq);
        VdcApiRequestPtr request = VdcApiRequestPtr(new ScriptApiRequest(this));
        if (isMethod) {
          err = p44VdcHost.handleMethodForParams(request, m->stringValue(), params);
          // Note: if method returns NULL, it has sent or will send results itself.
          //   Otherwise, even if Error is ErrorOK we must send a generic response
        }
        else {
          // handle notification
          err = p44VdcHost.handleNotificationForParams(request->connection(), m->stringValue(), params);
          // Notifications are always immediately confirmed, so make sure there's an explicit ErrorOK
          if (!err) {
            err = ErrorPtr(new Error(Error::OK));
          }
        }
        if (!err) {
          // API result will arrive later and complete function then
          aNotYielded = false;
        }
        else {
          request->sendError(err);
        }
      }
      return true;
    }
    return inherited::evaluateAsyncFunction(aFunc, aArgs, aNotYielded);
  }

};

void P44VdcHost::runGlobalScripts()
{
  globalScripts.clear();
  // command line provided script
  string scriptFn;
  string script;
  if (CmdLineApp::sharedCmdLineApp()->getStringOption("initscript", scriptFn)) {
    scriptFn = Application::sharedApplication()->resourcePath(scriptFn);
    ErrorPtr err = string_fromfile(scriptFn, script);
    if (Error::notOK(err)) {
      OLOG(LOG_ERR, "cannot open initscript: %s", err->text());
    }
    else {
      ScriptExecutionContextPtr s = ScriptExecutionContextPtr(new VdcHostScriptContext(*this));
      s->setCode(script);
      s->setContextInfo("initscript", this);
      OLOG(LOG_NOTICE, "Starting initscript specified on commandline '%s'", scriptFn.c_str());
      globalScripts.queueScript(s);
    }
  }
}

#endif // EXPRESSION_SCRIPT_SUPPORT

#endif // P44SCRIPT_FULL_SUPPORT || EXPRESSION_SCRIPT_SUPPORT


#if P44SCRIPT_FULL_SUPPORT

using namespace P44Script;


// MARK: - script API - ScriptApiRequest

VdcApiConnectionPtr ScriptApiRequest::connection()
{
  return VdcApiConnectionPtr(new ScriptCallConnection());
}


ErrorPtr ScriptApiRequest::sendResult(ApiValuePtr aResult)
{
  LOG(LOG_DEBUG, "script <- vdcd (JSON) result: %s", aResult ? aResult->description().c_str() : "<none>");
  JsonApiValuePtr result = boost::dynamic_pointer_cast<JsonApiValue>(aResult);
  #if ENABLE_P44SCRIPT
  mBuiltinFunctionContext->finish(result ? ScriptObjPtr(new JsonValue(result->jsonObject())) : ScriptObjPtr(new AnnotatedNullValue("no vdcapi result")));
  #else
  ExpressionValue res;
  if (result) {
    res.setJson(result->jsonObject());
  }
  else {
    res.setNull();
  }
  scriptContext->continueWithAsyncFunctionResult(res);
  #endif
  return ErrorPtr();
}


ErrorPtr ScriptApiRequest::sendError(ErrorPtr aError)
{
  ErrorPtr err;
  if (!aError) {
    aError = Error::ok();
  }
  LOG(LOG_DEBUG, "script <- vdcd (JSON) error: %ld (%s)", aError->getErrorCode(), aError->getErrorMessage());
  #if ENABLE_P44SCRIPT
  mBuiltinFunctionContext->finish(ScriptObjPtr(new ErrorValue(aError)));
  #else
  ExpressionValue res;
  res.setError(aError);
  scriptContext->continueWithAsyncFunctionResult(res);
  #endif
  return ErrorPtr();
}


void VdcHost::scriptExecHandler(VdcApiRequestPtr aRequest, ScriptObjPtr aResult)
{
  ApiValuePtr ans = aRequest->newApiValue();
  ans->setType(apivalue_object);
  if (aResult) {
    if (aResult->isErr()) {
      ans->add("error", ans->newString(aResult->errorValue()->text()));
    }
    else {
      ans->add("result", ans->newScriptValue(aResult));
    }
    ans->add("annotation", ans->newString(aResult->getAnnotation()));
    SourceCursor *cursorP = aResult->cursor();
    if (cursorP) {
      ans->add("sourceline", ans->newString(cursorP->linetext()));
      ans->add("at", ans->newUint64(cursorP->textpos()));
      ans->add("line", ans->newUint64(cursorP->lineno()));
      ans->add("char", ans->newUint64(cursorP->charpos()));
    }
  }
  aRequest->sendResult(ans);
}


// MARK: - VdcHost global script members and functions

// vdcapi(jsoncall)
static const BuiltInArgDesc vdcapi_args[] = { { json|structured } };
static const size_t vdcapi_numargs = sizeof(vdcapi_args)/sizeof(BuiltInArgDesc);
static void vdcapi_func(BuiltinFunctionContextPtr f)
{
  // get method/notification and params
  JsonObjectPtr rq = f->arg(0)->jsonValue();
  JsonObjectPtr m = rq->get("method");
  bool isMethod = false;
  ErrorPtr err;
  if (m) {
    isMethod = true;
  }
  else {
    m = rq->get("notification");
  }
  if (!m) {
    f->finish(new ErrorValue(Error::err<WebError>(400, "invalid API request, must specify 'method' or 'notification'")));
    return;
  }
  else {
    // Note: the "method" or "notification" param will also be in the params, but should not cause any problem
    ApiValuePtr params = JsonApiValue::newValueFromJson(rq);
    VdcApiRequestPtr request = VdcApiRequestPtr(new ScriptApiRequest(f));
    if (isMethod) {
      err = VdcHost::sharedVdcHost()->handleMethodForParams(request, m->stringValue(), params);
      // Note: if method returns NULL, it has sent or will send results itself.
      //   Otherwise, even if Error is ErrorOK we must send a generic response
    }
    else {
      // handle notification
      err = VdcHost::sharedVdcHost()->handleNotificationForParams(request->connection(), m->stringValue(), params);
      // Notifications are always immediately confirmed, so make sure there's an explicit ErrorOK
      if (!err) {
        err = ErrorPtr(new Error(Error::OK));
      }
    }
    if (err) {
      // no API result will arrive later, so finish here
      request->sendError(err);
      f->finish();
      return;
    }
    // otherwise, method will finish function call
  }
}


// device(device_name_or_dSUID)
static const BuiltInArgDesc device_args[] = { { text } };
static const size_t device_numargs = sizeof(device_args)/sizeof(BuiltInArgDesc);
static void device_func(BuiltinFunctionContextPtr f)
{
  DevicePtr device = VdcHost::sharedVdcHost()->getDeviceByNameOrDsUid(f->arg(0)->stringValue());
  if (!device) {
    f->finish(new ErrorValue(ScriptError::NotFound, "no device '%s' found", f->arg(0)->stringValue().c_str()));
    return;
  }
  f->finish(device->newDeviceObj());
}

#if P44SCRIPT_FULL_SUPPORT

static const BuiltInArgDesc valuesource_args[] = { { text } };
static const size_t valuesource_numargs = sizeof(valuesource_args)/sizeof(BuiltInArgDesc);
static void valuesource_func(BuiltinFunctionContextPtr f)
{
  ValueSource* valueSource = VdcHost::sharedVdcHost()->getValueSourceById(f->arg(0)->stringValue());
  if (!valueSource) {
    f->finish(new ErrorValue(ScriptError::NotFound, "no value source '%s' found", f->arg(0)->stringValue().c_str()));
    return;
  }
  f->finish(new ValueSourceObj(valueSource));
}

#endif

// macaddress()
static void macaddress_func(BuiltinFunctionContextPtr f)
{
  f->finish(new StringValue(macAddressToString(macAddress(),0)));
}

// productversion()
static void productversion_func(BuiltinFunctionContextPtr f)
{
  f->finish(new StringValue(VdcHost::sharedVdcHost()->modelVersion()));
}

// nextversion()
static void nextversion_func(BuiltinFunctionContextPtr f)
{
  f->finish(new StringValue(VdcHost::sharedVdcHost()->nextModelVersion()));
}

static const BuiltinMemberDescriptor p44VdcHostMembers[] = {
  { "vdcapi", executable|json, vdcapi_numargs, vdcapi_args, &vdcapi_func },
  { "device", executable|any, device_numargs, device_args, &device_func },
  #if P44SCRIPT_FULL_SUPPORT
  { "valuesource", executable|any, valuesource_numargs, valuesource_args, &valuesource_func },
  #endif
  { "productversion", executable|text, 0, NULL, &productversion_func },
  { "nextversion", executable|text, 0, NULL, &nextversion_func },
  { "macaddress", executable|text, 0, NULL, &macaddress_func },
  { NULL } // terminator
};

VdcHostLookup::VdcHostLookup() :
  inherited(p44VdcHostMembers)
{
}

static MemberLookupPtr sharedVdcHostLookup;

MemberLookupPtr VdcHostLookup::sharedLookup()
{
  if (!sharedVdcHostLookup) {
    sharedVdcHostLookup = new VdcHostLookup;
    sharedVdcHostLookup->isMemberVariable(); // disable refcounting
  }
  return sharedVdcHostLookup;
}


#endif // P44SCRIPT_FULL_SUPPORT
