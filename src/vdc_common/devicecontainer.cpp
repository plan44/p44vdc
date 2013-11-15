//
//  devicecontainer.cpp
//  vdcd
//
//  Created by Lukas Zeller on 17.04.13.
//  Copyright (c) 2013 plan44.ch. All rights reserved.
//

#include "devicecontainer.hpp"

#include "deviceclasscontainer.hpp"

#include <string.h>

#include "device.hpp"

#include "macaddress.hpp"
#include "fnv.hpp"

// for local behaviour
#include "buttonbehaviour.hpp"
#include "lightbehaviour.hpp"


using namespace p44;


DeviceContainer::DeviceContainer() :
  mac(0),
  DsAddressable(this),
  vdcApiServer(SyncIOMainLoop::currentMainLoop()),
  collecting(false),
  learningMode(false),
  announcementTicket(0),
  periodicTaskTicket(0),
  localDimTicket(0),
  localDimDown(false),
  sessionActive(false),
  sessionActivityTicket(0),
  dsUids(false)
{
  // obtain MAC address
  mac = macAddress();
}



string DeviceContainer::macAddressString()
{
  string macStr;
  if (mac!=0) {
    for (int i=0; i<6; ++i) {
      string_format_append(macStr, "%02X",(mac>>((5-i)*8)) & 0xFF);
    }
  }
  else {
    macStr = "UnknownMACAddress";
  }
  return macStr;
}



void DeviceContainer::deriveDsUid()
{
  if (!externalDsuid) {
    // we don't have a fixed external dSUID to base everything on, derive a dSUID of our own
    if (usingDsUids()) {
      // single vDC per MAC-Adress scenario: generate UUIDv5 with name = macaddress
      // - calculate UUIDv5 based dSUID
      DsUid vdcNamespace(DSUID_VDC_NAMESPACE_UUID);
      dSUID.setNameInSpace(macAddressString(), vdcNamespace);
    }
    else {
      // classic dsids: create a hash from MAC hex string
      Fnv64 hash;
      string s = macAddressString();
      hash.addBytes(s.size(), (uint8_t *)s.c_str());
      #if FAKE_REAL_DSD_IDS
      dSUID.setObjectClass(DSID_OBJECTCLASS_DSDEVICE);
      dSUID.setDsSerialNo(hash.getHash32());
      #warning "TEST ONLY: faking digitalSTROM device addresses, possibly colliding with real devices"
      #else
      // TODO: validate, now we are using the MAC-address class with bits 48..51 set to 7
      dSUID.setObjectClass(DSID_OBJECTCLASS_MACADDRESS);
      dSUID.setSerialNo(0x7000000000000ll+hash.getHash48());
      #endif
    }
  }
}


void DeviceContainer::setIdMode(bool aDsUids, DsUidPtr aExternalDsUid)
{
  dsUids = aDsUids;
  if (aExternalDsUid) {
    externalDsuid = true;
    dSUID = *aExternalDsUid;
  }
  deriveDsUid(); // derive my dSUID now (again), if necessary
}



void DeviceContainer::addDeviceClassContainer(DeviceClassContainerPtr aDeviceClassContainerPtr)
{
  deviceClassContainers.push_back(aDeviceClassContainerPtr);
  aDeviceClassContainerPtr->setDeviceContainer(this);
}



void DeviceContainer::setPersistentDataDir(const char *aPersistentDataDir)
{
	persistentDataDir = nonNullCStr(aPersistentDataDir);
	if (!persistentDataDir.empty() && persistentDataDir[persistentDataDir.length()-1]!='/') {
		persistentDataDir.append("/");
	}
}


const char *DeviceContainer::getPersistentDataDir()
{
	return persistentDataDir.c_str();
}





#pragma mark - initializisation of DB and containers


class DeviceClassInitializer
{
  CompletedCB callback;
  ContainerVector::iterator nextContainer;
  DeviceContainer *deviceContainerP;
  bool factoryReset;
public:
  static void initialize(DeviceContainer *aDeviceContainerP, CompletedCB aCallback, bool aFactoryReset)
  {
    // create new instance, deletes itself when finished
    new DeviceClassInitializer(aDeviceContainerP, aCallback, aFactoryReset);
  };
private:
  DeviceClassInitializer(DeviceContainer *aDeviceContainerP, CompletedCB aCallback, bool aFactoryReset) :
		callback(aCallback),
		deviceContainerP(aDeviceContainerP),
    factoryReset(aFactoryReset)
  {
    nextContainer = deviceContainerP->deviceClassContainers.begin();
    queryNextContainer(ErrorPtr());
  }


  void queryNextContainer(ErrorPtr aError)
  {
    if ((!aError || factoryReset) && nextContainer!=deviceContainerP->deviceClassContainers.end())
      (*nextContainer)->initialize(boost::bind(&DeviceClassInitializer::containerInitialized, this, _1), factoryReset);
    else
      completed(aError);
  }

  void containerInitialized(ErrorPtr aError)
  {
    // check next
    ++nextContainer;
    queryNextContainer(aError);
  }

  void completed(ErrorPtr aError)
  {
    // start periodic tasks like registration checking and saving parameters
    MainLoop::currentMainLoop().executeOnce(boost::bind(&DeviceContainer::periodicTask, deviceContainerP, _2), 1*Second, deviceContainerP);
    // callback
    callback(aError);
    // done, delete myself
    delete this;
  }

};


#define DSPARAMS_SCHEMA_VERSION 1

string DsParamStore::dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion)
{
  string sql;
  if (aFromVersion==0) {
    // create DB from scratch
		// - use standard globs table for schema version
    sql = inherited::dbSchemaUpgradeSQL(aFromVersion, aToVersion);
		// - no devicecontainer level table to create at this time
    //   (PersistentParams create and update their tables as needed)
    // reached final version in one step
    aToVersion = DSPARAMS_SCHEMA_VERSION;
  }
  return sql;
}


void DeviceContainer::initialize(CompletedCB aCompletedCB, bool aFactoryReset)
{
  // Log start message
  LOG(LOG_NOTICE,"\n****** starting vDC initialisation, MAC: %s, dSUID (%s) = %s\n", macAddressString().c_str(), externalDsuid ? "external" : "MAC-derived", dSUID.getString().c_str());
  // start the API server
  vdcApiServer.startServer(boost::bind(&DeviceContainer::vdcApiConnectionHandler, this, _1), 3);
  // initialize dsParamsDB database
	string databaseName = getPersistentDataDir();
	string_format_append(databaseName, "DsParams.sqlite3");
  ErrorPtr error = dsParamStore.connectAndInitialize(databaseName.c_str(), DSPARAMS_SCHEMA_VERSION, aFactoryReset);

  // start initialisation of class containers
  DeviceClassInitializer::initialize(this, aCompletedCB, aFactoryReset);
}




#pragma mark - collect devices

namespace p44 {

/// collects and initializes all devices
class DeviceClassCollector
{
  CompletedCB callback;
  bool exhaustive;
  bool incremental;
  ContainerVector::iterator nextContainer;
  DeviceContainer *deviceContainerP;
  DsDeviceMap::iterator nextDevice;
public:
  static void collectDevices(DeviceContainer *aDeviceContainerP, CompletedCB aCallback, bool aIncremental, bool aExhaustive)
  {
    // create new instance, deletes itself when finished
    new DeviceClassCollector(aDeviceContainerP, aCallback, aIncremental, aExhaustive);
  };
private:
  DeviceClassCollector(DeviceContainer *aDeviceContainerP, CompletedCB aCallback, bool aIncremental, bool aExhaustive) :
    callback(aCallback),
    deviceContainerP(aDeviceContainerP),
    incremental(aIncremental),
    exhaustive(aExhaustive)
  {
    nextContainer = deviceContainerP->deviceClassContainers.begin();
    queryNextContainer(ErrorPtr());
  }


  void queryNextContainer(ErrorPtr aError)
  {
    if (!aError && nextContainer!=deviceContainerP->deviceClassContainers.end())
      (*nextContainer)->collectDevices(boost::bind(&DeviceClassCollector::containerQueried, this, _1), incremental, exhaustive);
    else
      collectedAll(aError);
  }

  void containerQueried(ErrorPtr aError)
  {
    // check next
    ++nextContainer;
    queryNextContainer(aError);
  }


  void collectedAll(ErrorPtr aError)
  {
    // now have each of them initialized
    nextDevice = deviceContainerP->dSDevices.begin();
    initializeNextDevice(ErrorPtr());
  }


  void initializeNextDevice(ErrorPtr aError)
  {
    if (!aError && nextDevice!=deviceContainerP->dSDevices.end())
      // TODO: now never doing factory reset init, maybe parametrize later
      nextDevice->second->initializeDevice(boost::bind(&DeviceClassCollector::deviceInitialized, this, _1), false);
    else
      completed(aError);
  }


  void deviceInitialized(ErrorPtr aError)
  {
    // check next
    ++nextDevice;
    initializeNextDevice(aError);
  }


  void completed(ErrorPtr aError)
  {
    callback(aError);
    deviceContainerP->collecting = false;
    // done, delete myself
    delete this;
  }

};



void DeviceContainer::collectDevices(CompletedCB aCompletedCB, bool aIncremental, bool aExhaustive)
{
  if (!collecting) {
    collecting = true;
    if (!aIncremental) {
      // only for non-incremental collect, close vdsm connection
      if (sessionComm) {
        // disconnect the vdSM
        sessionComm->closeConnection();
      }
      endContainerSession(); // end the session
      dSDevices.clear(); // forget existing ones
    }
    DeviceClassCollector::collectDevices(this, aCompletedCB, aIncremental, aExhaustive);
  }
}

} // namespace




#pragma mark - adding/removing devices


// add a new device, replaces possibly existing one based on dSUID
bool DeviceContainer::addDevice(DevicePtr aDevice)
{
  if (!aDevice)
    return false; // no device, nothing added
  // check if device with same dSUID already exists
  DsDeviceMap::iterator pos = dSDevices.find(aDevice->dSUID);
  if (pos!=dSDevices.end()) {
    LOG(LOG_INFO, "- device %s already registered, not added again\n",aDevice->dSUID.getString().c_str());
    return false; // duplicate dSUID, not added
  }
  // set for given dSUID in the container-wide map of devices
  dSDevices[aDevice->dSUID] = aDevice;
  LOG(LOG_NOTICE,"--- added device: %s\n",aDevice->shortDesc().c_str());
  LOG(LOG_INFO, "- device description: %s",aDevice->description().c_str());
  // load the device's persistent params
  aDevice->load();
  // register new device right away (unless collecting or already announcing)
  announceDevices();
  return true;
}

DevicePtr DeviceContainer::getDevice(DsUid dsuid)
{
  // check if device with same dSUID already exists
  DsDeviceMap::iterator pos = dSDevices.find(dsuid);
  if (pos!=dSDevices.end()) {
      return dSDevices[dsuid];
  }

  return NULL;
}



// remove a device from container list (but does not disconnect it!)
void DeviceContainer::removeDevice(DevicePtr aDevice, bool aForget)
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
  dSDevices.erase(aDevice->dSUID);
  LOG(LOG_NOTICE,"--- removed device: %s\n", aDevice->shortDesc().c_str());
}



void DeviceContainer::startLearning(LearnCB aLearnHandler)
{
  // enable learning in all class containers
  learnHandler = aLearnHandler;
  learningMode = true;
  for (ContainerVector::iterator pos = deviceClassContainers.begin(); pos != deviceClassContainers.end(); ++pos) {
    (*pos)->setLearnMode(true);
  }
}


void DeviceContainer::stopLearning()
{
  // disable learning in all class containers
  for (ContainerVector::iterator pos = deviceClassContainers.begin(); pos != deviceClassContainers.end(); ++pos) {
    (*pos)->setLearnMode(false);
  }
  learningMode = false;
  learnHandler.clear();
}


void DeviceContainer::reportLearnEvent(bool aLearnIn, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    if (aLearnIn)
      LOG(LOG_NOTICE,"--- learned in (paired) new device(s)\n");
    else
      LOG(LOG_NOTICE,"--- learned out (unpaired) device(s)\n");
  }
  // report status
  if (learnHandler) {
    learnHandler(aLearnIn, aError);
  }
}



#pragma mark - activity monitor


void DeviceContainer::setActivityMonitor(DoneCB aActivityCB)
{
  activityHandler = aActivityCB;
}


void DeviceContainer::signalActivity()
{
  if (activityHandler) {
    activityHandler();
  }
}


#pragma mark - periodic activity


#define PERIODIC_TASK_INTERVAL (5*Second)

void DeviceContainer::periodicTask(MLMicroSeconds aCycleStartTime)
{
  // cancel any pending executions
  MainLoop::currentMainLoop().cancelExecutionTicket(periodicTaskTicket);
  if (!collecting) {
    // check for devices that need to be announced
    announceDevices();
    // do a save run as well
    for (DsDeviceMap::iterator pos = dSDevices.begin(); pos!=dSDevices.end(); ++pos) {
      pos->second->save();
    }
  }
  // schedule next run
  periodicTaskTicket = MainLoop::currentMainLoop().executeOnce(boost::bind(&DeviceContainer::periodicTask, this, _2), PERIODIC_TASK_INTERVAL, this);
}


#pragma mark - local operation mode


void DeviceContainer::localDimHandler()
{
  for (DsDeviceMap::iterator pos = dSDevices.begin(); pos!=dSDevices.end(); ++pos) {
    DevicePtr dev = pos->second;
    if (dev->isMember(group_yellow_light)) {
      signalActivity();
      dev->callScene(localDimDown ? DEC_S : INC_S, true);
    }
  }
  localDimTicket = MainLoop::currentMainLoop().executeOnce(boost::bind(&DeviceContainer::localDimHandler, this), 250*MilliSecond, this);
}



void DeviceContainer::checkForLocalClickHandling(ButtonBehaviour &aButtonBehaviour, DsClickType aClickType)
{
  if (!sessionActive) {
    // not connected to a vdSM, handle clicks locally
    handleClickLocally(aButtonBehaviour, aClickType);
  }
}


void DeviceContainer::handleClickLocally(ButtonBehaviour &aButtonBehaviour, DsClickType aClickType)
{
  // TODO: Not really conforming to ds-light yet...
  int scene = -1; // none
  int direction = aButtonBehaviour.localFunctionElement()==buttonElement_up ? 1 : (aButtonBehaviour.localFunctionElement()==buttonElement_down ? -1 : 0); // -1=down/off, 1=up/on, 0=toggle
  switch (aClickType) {
    case ct_tip_1x:
    case ct_click_1x:
      scene = T0_S1;
      break;
    case ct_tip_2x:
    case ct_click_2x:
      scene = T0_S2;
      break;
    case ct_tip_3x:
    case ct_click_3x:
      scene = T0_S3;
      break;
    case ct_tip_4x:
      scene = T0_S4;
      break;
    case ct_hold_start:
      scene = INC_S;
      localDimTicket = MainLoop::currentMainLoop().executeOnce(boost::bind(&DeviceContainer::localDimHandler, this), 250*MilliSecond, this);
      if (direction!=0)
        localDimDown = direction<0;
      else {
        localDimDown = !localDimDown; // just toggle direction
        direction = localDimDown ? -1 : 1; // adjust direction as well
      }
      break;
    case ct_hold_end:
      MainLoop::currentMainLoop().cancelExecutionTicket(localDimTicket); // stop dimming
      scene = STOP_S; // stop any still ongoing dimming
      direction = 1; // really send STOP, not main off!
      break;
  }
  if (scene>=0) {
    if (aClickType!=ct_hold_start) {
      // safety: any scene call except hold start stops ongoing dimming
      MainLoop::currentMainLoop().cancelExecutionTicket(localDimTicket);
    }
    for (DsDeviceMap::iterator pos = dSDevices.begin(); pos!=dSDevices.end(); ++pos) {
      DevicePtr dev = pos->second;
      if (dev->isMember(group_yellow_light)) {
        // this is a light related device (but not necessarily a light output!)
        LightBehaviourPtr lightBehaviour;
        if (dev->outputs.size()>0) {
          lightBehaviour = boost::dynamic_pointer_cast<LightBehaviour>(dev->outputs[0]);
          if (lightBehaviour) {
            // this device has a light behaviour output
            if (direction==0) {
              // get direction from current value of first encountered light
              direction = lightBehaviour->getLogicalBrightness()>1 ? -1 : 1;
            }
            // determine the scene to call
            int effScene = scene;
            if (scene==INC_S) {
              // dimming
              if (direction<0)
                effScene = DEC_S;
              else {
                // increment - check if we need to do a MIN_S first
                if (lightBehaviour && lightBehaviour->getLogicalBrightness()==0)
                  effScene = MIN_S; // after calling this once, light should be logically on
              }
            }
            else {
              // switching
              if (direction<0) effScene = T0_S0; // main off
            }
            // call the effective scene
            signalActivity(); // local activity
            dev->callScene(effScene, true);
          } // if light behaviour
        } // if any outputs
      } // if in light group
    }
  }
}



#pragma mark - vDC API


#define SESSION_TIMEOUT (3*Minute) // 3 minutes



bool DeviceContainer::sendApiRequest(const char *aMethod, JsonObjectPtr aParams, JsonRpcResponseCB aResponseHandler)
{
  // TODO: once allowDisconnect is implemented, check here for creating a connection back to the vdSM
  if (sessionComm) {
    signalActivity();
    bool ok = Error::isOK(sessionComm->sendRequest(aMethod, aParams, aResponseHandler));
    LOG(LOG_INFO,"vdSM <- vDC request sent: id='%d', method='%s', params=%s\n", sessionComm->lastRequestId(), aMethod, aParams ? aParams->c_strValue() : "<none>");
    return ok;
  }
  // cannot send
  return false;
}


bool DeviceContainer::sendApiResult(const string &aJsonRpcId, JsonObjectPtr aResult)
{
  // TODO: once allowDisconnect is implemented, we might need to close the connection after sending the result
  if (sessionComm) {
    signalActivity();
    bool ok = Error::isOK(sessionComm->sendResult(aJsonRpcId.c_str(), aResult));
    LOG(LOG_INFO,"vdSM <- vDC result sent: id='%s', result=%s\n", aJsonRpcId.c_str(), aResult ? aResult->c_strValue() : "<none>");
    return ok;
  }
  // cannot send
  return false;
}


bool DeviceContainer::sendApiError(const string &aJsonRpcId, ErrorPtr aErrorToSend)
{
  // TODO: once allowDisconnect is implemented, we might need to close the connection after sending the result
  if (sessionComm) {
    signalActivity();
    bool ok = Error::isOK(sessionComm->sendError(aJsonRpcId.size()>0 ? aJsonRpcId.c_str() : NULL, aErrorToSend));
    LOG(LOG_INFO,"vdSM <- vDC error sent: id='%s', error=%s\n", aJsonRpcId.c_str(), aErrorToSend ? aErrorToSend->description().c_str() : "<none>");
    return ok;
  }
  // cannot send
  return false;
}




void DeviceContainer::sessionTimeoutHandler()
{
  LOG(LOG_INFO,"vDC API session timed out -> ends here\n");
  endContainerSession();
  if (sessionComm) {
    sessionComm->closeConnection();
    sessionComm.reset();
  }
}



SocketCommPtr DeviceContainer::vdcApiConnectionHandler(SocketComm *aServerSocketCommP)
{
  JsonRpcCommPtr conn = JsonRpcCommPtr(new JsonRpcComm(SyncIOMainLoop::currentMainLoop()));
  conn->setRequestHandler(boost::bind(&DeviceContainer::vdcApiRequestHandler, this, _1, _2, _3, _4));
  conn->setConnectionStatusHandler(boost::bind(&DeviceContainer::vdcApiConnectionStatusHandler, this, _1, _2));
  // save in my own list of connections
  apiConnections.push_back(conn);
  return conn;
}


void DeviceContainer::vdcApiConnectionStatusHandler(SocketComm *aSocketComm, ErrorPtr aError)
{
  if (!Error::isOK(aError)) {
    LOG(LOG_INFO,"vDC API connection ends due to %s\n", aError->description().c_str());
    // connection failed/closed and we don't support reconnect yet -> end session
    JsonRpcComm *connP = dynamic_cast<JsonRpcComm *>(aSocketComm);
    endApiConnection(connP);
  }
  else {
    LOG(LOG_INFO,"vDC API connection started\n");
  }
}



void DeviceContainer::vdcApiRequestHandler(JsonRpcComm *aJsonRpcComm, const char *aMethod, const char *aJsonRpcId, JsonObjectPtr aParams)
{
  ErrorPtr respErr;
  string method = aMethod;
  signalActivity();
  LOG(LOG_INFO,"vdSM -> vDC request received: id='%s', method='%s', params=%s\n", aJsonRpcId, aMethod, aParams ? aParams->c_strValue() : "<none>");
  // retrigger session timout
  MainLoop::currentMainLoop().cancelExecutionTicket(sessionActivityTicket);
  sessionActivityTicket = MainLoop::currentMainLoop().executeOnce(boost::bind(&DeviceContainer::sessionTimeoutHandler,this), SESSION_TIMEOUT);
  if (aJsonRpcId) {
    // Check session init/end methods
    if (method=="hello") {
      respErr = helloHandler(aJsonRpcComm, aJsonRpcId, aParams);
    }
    else if (method=="bye") {
      respErr = byeHandler(aJsonRpcComm, aJsonRpcId, aParams);
    }
    else {
      if (!sessionActive) {
        // all following methods must have an active session
        respErr = ErrorPtr(new JsonRpcError(401,"no vDC session - cannot call method"));
      }
      else {
        // session active - all commands need dSUID parameter
        string dsidstring;
        if (Error::isOK(respErr = checkStringParam(aParams, "dSUID", dsidstring))) {
          // operation method
          DsUid temp(dsidstring);
          respErr = handleMethodForDsid(aMethod, aJsonRpcId, DsUid(dsidstring), aParams);
        }
      }
    }
  }
  else {
    // Notifications
    if (sessionActive) {
      // out of session, notifications are simply ignored
      string dsidstring;
      if (Error::isOK(respErr = checkStringParam(aParams, "dSUID", dsidstring))) {
        handleNotificationForDsid(aMethod, DsUid(dsidstring), aParams);
      }
    }
  }
  // report back error if any
  if (!Error::isOK(respErr)) {
    aJsonRpcComm->sendError(aJsonRpcId, respErr);
  }
}



void DeviceContainer::endApiConnection(JsonRpcComm *aJsonRpcComm)
{
  // remove from my list of connection
  for (ApiConnectionList::iterator pos = apiConnections.begin(); pos!=apiConnections.end(); ++pos) {
    if (pos->get()==aJsonRpcComm) {
      if (*pos==sessionComm) {
        // this is the current vDC session, end it
        MainLoop::currentMainLoop().cancelExecutionTicket(sessionActivityTicket);
        endContainerSession();
        sessionComm.reset();
      }
      // remove from my connections
      apiConnections.erase(pos);
      break;
    }
  }
}



ErrorPtr DeviceContainer::handleMethodForDsid(const string &aMethod, const string &aJsonRpcId, const DsUid &aDsid, JsonObjectPtr aParams)
{
  if (aDsid==dSUID) {
    // container level method
    return handleMethod(aMethod, aJsonRpcId, aParams);
  }
  else {
    // Must be device level method
    // - find device to handle it
    DsDeviceMap::iterator pos = dSDevices.find(aDsid);
    if (pos!=dSDevices.end()) {
      DevicePtr dev = pos->second;
      // check special case of Remove command - we must execute this because device should not try to remove itself
      if (aMethod=="remove") {
        return removeHandler(dev, aJsonRpcId);
      }
      else {
        // let device handle it
        return dev->handleMethod(aMethod, aJsonRpcId, aParams);
      }
    }
    else {
      return ErrorPtr(new JsonRpcError(404, "unknown dSID"));
    }
  }
}



void DeviceContainer::handleNotificationForDsid(const string &aMethod, const DsUid &aDsid, JsonObjectPtr aParams)
{
  if (aDsid==dSUID) {
    // container level notification
    handleNotification(aMethod, aParams);
  }
  else {
    // Must be device level notification
    // - find device to handle it
    DsDeviceMap::iterator pos = dSDevices.find(aDsid);
    if (pos!=dSDevices.end()) {
      DevicePtr dev = pos->second;
      dev->handleNotification(aMethod, aParams);
    }
    else {
      LOG(LOG_WARNING, "Target device %s not found for notification '%s'\n", aDsid.getString().c_str(), aMethod.c_str());
    }
  }
}


#pragma mark - vDC level session management methods and notifications



ErrorPtr DeviceContainer::helloHandler(JsonRpcComm *aJsonRpcComm, const string &aJsonRpcId, JsonObjectPtr aParams)
{
  ErrorPtr respErr;
  string s;
  // check API version
  if (Error::isOK(respErr = checkStringParam(aParams, "APIVersion", s))) {
    if (s!="1.0")
      respErr = ErrorPtr(new JsonRpcError(505, "Incompatible vDC API version - expected '1.0'"));
    else {
      // API version ok, check dsID
      if (Error::isOK(respErr = checkStringParam(aParams, "dSUID", s))) {
        DsUid vdsmDsid = DsUid(s);
        // same vdSM can restart session any time. Others will be rejected
        if (!sessionActive || vdsmDsid==connectedVdsm) {
          // ok to start new session
          // - start session with this vdSM
          connectedVdsm = vdsmDsid;
          // - remember the session's connection
          for (ApiConnectionList::iterator pos = apiConnections.begin(); pos!=apiConnections.end(); ++pos) {
            if (pos->get()==aJsonRpcComm) {
              // remember the current session's communication object
              sessionComm = *pos;
              break;
            }
          }
          // - create answer
          JsonObjectPtr result = JsonObject::newObj();
          result->add("dSUID", JsonObject::newString(dSUID.getString()));
          result->add("allowDisconnect", JsonObject::newBool(false));
          sendResult(aJsonRpcId, result);
          // - start session, enable sending announces now
          startContainerSession();
        }
        else {
          // not ok to start new session, reject
          respErr = ErrorPtr(new JsonRpcError(503, string_format("this vDC already has an active session with vdSM %s",connectedVdsm.getString().c_str())));
          aJsonRpcComm->sendError(aJsonRpcId.c_str(), respErr);
          // close after send
          aJsonRpcComm->closeAfterSend();
          // prevent sending error again
          respErr.reset();
        }
      }
    }
  }
  return respErr;
}


ErrorPtr DeviceContainer::byeHandler(JsonRpcComm *aJsonRpcComm, const string &aJsonRpcId, JsonObjectPtr aParams)
{
  // always confirm Bye, even out-of-session, so using aJsonRpcComm directly to answer (sessionComm might not be ready)
  aJsonRpcComm->sendResult(aJsonRpcId.c_str(), JsonObjectPtr());
  // close after send
  aJsonRpcComm->closeAfterSend();
  // success
  return ErrorPtr();
}


ErrorPtr DeviceContainer::removeHandler(DevicePtr aDevice, const string &aJsonRpcId)
{
  // dS system wants to disconnect this device from this vDC. Try it and report back success or failure
  // Note: as disconnect() removes device from all containers, only aDevice may keep it alive until disconnection is complete
  aDevice->disconnect(true, boost::bind(&DeviceContainer::removeResultHandler, this, aJsonRpcId, _1, _2));
  return ErrorPtr();
}


void DeviceContainer::removeResultHandler(const string &aJsonRpcId, DevicePtr aDevice, bool aDisconnected)
{
  if (aDisconnected)
    aDevice->sendResult(aJsonRpcId, JsonObjectPtr()); // disconnected successfully
  else
    aDevice->sendError(aJsonRpcId, ErrorPtr(new JsonRpcError(403, "Device cannot be removed, is still connected")));
}






#pragma mark - session management


/// start vDC session (say Hello to the vdSM)
void DeviceContainer::startContainerSession()
{
  // end previous container session first (set all devices unannounced)
  endContainerSession();
  sessionActive = true;
  // announce devices
  announceDevices();
}


/// end vDC session
void DeviceContainer::endContainerSession()
{
  // end pending announcement
  MainLoop::currentMainLoop().cancelExecutionTicket(announcementTicket);
  // end all device sessions
  for (DsDeviceMap::iterator pos = dSDevices.begin(); pos!=dSDevices.end(); ++pos) {
    DevicePtr dev = pos->second;
    dev->announced = Never;
    dev->announcing = Never;
  }
  // not active any more
  sessionActive = false;
}


// how long until a not acknowledged registrations is considered timed out (and next device can be attempted)
#define ANNOUNCE_TIMEOUT (15*Second)

// how long until a not acknowledged announcement for a device is retried again for the same device
#define ANNOUNCE_RETRY_TIMEOUT (300*Second)

// how long vDC waits after receiving ok from one announce until it fires the next
#define ANNOUNCE_PAUSE (1*Second)

/// announce all not-yet announced devices to the vdSM
void DeviceContainer::announceDevices()
{
  if (!collecting && announcementTicket==0 && sessionActive) {
    announceNext();
  }
}


void DeviceContainer::announceNext()
{
  if (collecting) return; // prevent announcements during collect.
  // cancel re-announcing
  MainLoop::currentMainLoop().cancelExecutionTicket(announcementTicket);
  // check all devices for unnannounced ones and announce those
  for (DsDeviceMap::iterator pos = dSDevices.begin(); pos!=dSDevices.end(); ++pos) {
    DevicePtr dev = pos->second;
    if (
      dev->isPublicDS() && // only public ones
      dev->announced==Never &&
      (dev->announcing==Never || MainLoop::now()>dev->announcing+ANNOUNCE_RETRY_TIMEOUT)
    ) {
      // mark device as being in process of getting announced
      dev->announcing = MainLoop::now();
      // call announce method
      if (!dev->sendRequest("announce", JsonObjectPtr(), boost::bind(&DeviceContainer::announceResultHandler, this, dev, _1, _2, _3, _4))) {
        LOG(LOG_ERR, "Could not send announcement message for device %s\n", dev->shortDesc().c_str());
        dev->announcing = Never; // not registering
      }
      else {
        LOG(LOG_NOTICE, "Sent announcement for device %s\n", dev->shortDesc().c_str());
      }
      // schedule a retry
      announcementTicket = MainLoop::currentMainLoop().executeOnce(boost::bind(&DeviceContainer::announceNext, this), ANNOUNCE_TIMEOUT);
      // done for now, continues after ANNOUNCE_TIMEOUT or when registration acknowledged
      break;
    }
  }
}


void DeviceContainer::announceResultHandler(DevicePtr aDevice, JsonRpcComm *aJsonRpcComm, int32_t aResponseId, ErrorPtr &aError, JsonObjectPtr aResultOrErrorData)
{
  if (Error::isOK(aError)) {
    // set device announced successfully
    LOG(LOG_INFO,"vdSM -> vDC result received: id='%d', result/error=%s\n", sessionComm->lastRequestId(), aResultOrErrorData ? aResultOrErrorData->c_strValue() : "<none>");
    LOG(LOG_NOTICE, "Announcement for device %s acknowledged by vdSM\n", aDevice->shortDesc().c_str());
    aDevice->announced = MainLoop::now();
    aDevice->announcing = Never; // not announcing any more
  }
  // cancel retry timer
  MainLoop::currentMainLoop().cancelExecutionTicket(announcementTicket);
  // try next announcement, after a pause
  announcementTicket = MainLoop::currentMainLoop().executeOnce(boost::bind(&DeviceContainer::announceNext, this), ANNOUNCE_PAUSE);
}


#pragma mark - DsAddressable API implementation

ErrorPtr DeviceContainer::handleMethod(const string &aMethod, const string &aJsonRpcId, JsonObjectPtr aParams)
{
  return inherited::handleMethod(aMethod, aJsonRpcId, aParams);
}


void DeviceContainer::handleNotification(const string &aMethod, JsonObjectPtr aParams)
{
  inherited::handleNotification(aMethod, aParams);
}



#pragma mark - DsAddressable API implementation


#pragma mark - property access

enum {
  classes_key,
  numDeviceContainerProperties
};



int DeviceContainer::numProps(int aDomain)
{
  return inherited::numProps(aDomain)+numDeviceContainerProperties;
}


const PropertyDescriptor *DeviceContainer::getPropertyDescriptor(int aPropIndex, int aDomain)
{
  static const PropertyDescriptor properties[numDeviceContainerProperties] = {
    { "classes", ptype_object, true, classes_key }
  };
  int n = inherited::numProps(aDomain);
  if (aPropIndex<n)
    return inherited::getPropertyDescriptor(aPropIndex, aDomain); // base class' property
  aPropIndex -= n; // rebase to 0 for my own first property
  return &properties[aPropIndex];
}


PropertyContainerPtr DeviceContainer::getContainer(const PropertyDescriptor &aPropertyDescriptor, int &aDomain, int aIndex)
{
  if (aPropertyDescriptor.accessKey==classes_key) {
    // return the class container by index
    if (aIndex<deviceClassContainers.size())
      return deviceClassContainers[aIndex];
    else
      return NULL;
  }
  return inherited::getContainer(aPropertyDescriptor, aDomain);
}


bool DeviceContainer::accessField(bool aForWrite, JsonObjectPtr &aPropValue, const PropertyDescriptor &aPropertyDescriptor, int aIndex)
{
  if (aPropertyDescriptor.accessKey==classes_key) {
    if (aIndex==PROP_ARRAY_SIZE) {
      if (aForWrite) return false; // cannot write
      // return size of array
      aPropValue = JsonObject::newInt32((uint32_t)deviceClassContainers.size());
      return true;
    }
  }
  return inherited::accessField(aForWrite, aPropValue, aPropertyDescriptor, aIndex);
}





#pragma mark - description

string DeviceContainer::description()
{
  string d = string_format("DeviceContainer with %d device classes:\n", deviceClassContainers.size());
  for (ContainerVector::iterator pos = deviceClassContainers.begin(); pos!=deviceClassContainers.end(); ++pos) {
    d.append((*pos)->description());
  }
  return d;
}



