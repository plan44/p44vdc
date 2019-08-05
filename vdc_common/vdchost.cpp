//
//  Copyright (c) 1-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

// default geolocation
#if !defined(DEFAULT_LATITUDE) || !defined(DEFAULT_LONGITUDE)
  #define DEFAULT_LONGITUDE 8.474552
  #define DEFAULT_LATITUDE 47.394691
  #define DEFAULT_HEIGHTABOVESEA 396
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
  lastActivity(0),
  lastPeriodicRun(0),
  learningMode(false),
  localDimDirection(0), // undefined
  mainloopStatsInterval(DEFAULT_MAINLOOP_STATS_INTERVAL),
  mainLoopStatsCounter(0),
  persistentChannels(aWithPersistentChannels),
  productName(DEFAULT_PRODUCT_NAME),
  geolocation(DEFAULT_LONGITUDE, DEFAULT_LATITUDE, DEFAULT_HEIGHTABOVESEA)
{
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
  // let all vdcs know
  for (VdcMap::iterator pos = vdcs.begin(); pos != vdcs.end(); ++pos) {
    pos->second->handleGlobalEvent(aEvent);
  }
  #if ENABLE_LOCALCONTROLLER
  if (localController) localController->processGlobalEvent(aEvent);
  #endif
  // also let app-level event monitor know
  if (eventMonitorHandler) {
    eventMonitorHandler(aEvent);
  }
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
    "\n*** Product name: '%s', Product Version: '%s', Device Hardware ID: '%s'"
    "\n*** dSUID (%s) = %s, MAC: %s, IP = %s\n",
    publishedDescription().c_str(),
    vdcHostInstance,
    productName.c_str(),
    productVersion.c_str(),
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
  // start initialisation of class containers
  initializeNextVdc(aCompletedCB, aFactoryReset, vdcs.begin());
}



void VdcHost::initializeNextVdc(StatusCB aCompletedCB, bool aFactoryReset, VdcMap::iterator aNextVdc)
{
  // initialize all vDCs, even when some have errors
  if (aNextVdc!=vdcs.end()) {
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
  // load persistent params for vdc
  aNextVdc->second->load();
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
    if (Error::notOK(pos->second->getVdcStatus())) {
      vdcInitErr = pos->second->getVdcStatus();
      LOG(LOG_ERR, "*** initial device collecting incomplete because of error: %s", vdcInitErr->text());
      break;
    }
  }
  aCompletedCB(vdcInitErr);
  LOG(LOG_NOTICE, "=== initialized all collected devices\n");
  collecting = false;
}


void VdcHost::nextDeviceInitialized(StatusCB aCompletedCB, DsDeviceMap::iterator aNextDevice, ErrorPtr aError)
{
  deviceInitialized(aNextDevice->second, aError);
  // check next
  ++aNextDevice;
  initializeNextDevice(aCompletedCB, aNextDevice);
}



// MARK: - adding/removing devices


bool VdcHost::addDevice(DevicePtr aDevice)
{
  if (!aDevice)
    return false; // no device, nothing added
  // check if device with same dSUID already exists
  DsDeviceMap::iterator pos = dSDevices.find(aDevice->getDsUid());
  if (pos!=dSDevices.end()) {
    LOG(LOG_INFO, "- device %s already registered, not added again",aDevice->shortDesc().c_str());
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
  LOG(LOG_NOTICE, "--- ignored duplicate device: %s",aDevice->shortDesc().c_str());
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
      // check again for devices that need to be announced
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
      ChannelBehaviourPtr channel = dev->getChannelByType(aButtonBehaviour.buttonChannel);
      if (scene==STOP_S) {
        // stop dimming
        dev->dimChannelForArea(channel, dimmode_stop, 0, 0);
      }
      else {
        // call scene or start dimming
        LightBehaviourPtr l = boost::dynamic_pointer_cast<LightBehaviour>(dev->getOutput());
        if (l) {
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



ErrorPtr VdcHost::addToAudienceByDsuid(NotificationAudience &aAudience, DsUid &aDsuid)
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


ErrorPtr VdcHost::addToAudienceByItemSpec(NotificationAudience &aAudience, string &aItemSpec)
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
      ALOG(LOG_INFO, "==== passing '%s' for %lu devices for delivery to vDC %s", aNotification.c_str(), gpos->members.size(), gpos->vdc->shortDesc().c_str());
      // let vdc process this, might be able to optimize delivery using hardware's native mechanisms such as scenes or groups
      gpos->vdc->deliverToAudience(gpos->members, aApiConnection, aNotification, aParams);
    }
    else {
      ALOG(LOG_INFO, "==== delivering notification '%s' to %lu non-devices now", aNotification.c_str(), gpos->members.size());
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
      (!vdc->invisibleWhenEmpty() || vdc->getNumberOfDevices()>0)
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
  #if ENABLE_LOCALCONTROLLER
  localController_key,
  #endif
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
    #if ENABLE_LOCALCONTROLLER
    { "x-p44-localController", apivalue_object, localController_key, OKEY(localController_obj) },
    #endif
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
  }
}


ValueSource *VdcHost::getValueSourceById(string aValueSourceID)
{
  ValueSource *valueSource = NULL;
  // value source ID is
  //  dSUID:Sx for sensors (x=sensor index)
  //  dSUID:Ix for inputs (x=input index)
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
  // check for settings from files
  loadSettingsFromFiles();
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



void VdcHost::loadSettingsFromFiles()
{
  // try to open config file
  string fn = getConfigDir();
  fn += "vdchostsettings.csv";
  // if vdc has already stored properties, only explicitly marked properties will be applied
  if (loadSettingsFromFile(fn.c_str(), rowid!=0)) markClean();
}


// MARK: - persistence implementation

// SQLIte3 table name to store these parameters to
const char *VdcHost::tableName()
{
  return "VdcHostSettings";
}


// data field definitions

static const size_t numFields = 6;

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



