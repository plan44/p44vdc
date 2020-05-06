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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 7


#include "device.hpp"
#include "vdc.hpp"

using namespace p44;


// default vdc modelname template
#define DEFAULT_MODELNAME_TEMPLATE "%M %m"

#define DEFAULT_MIN_CALLS_BEFORE_OPTIMIZING 3 // do not optimize calls before they have repeated this number of times
#define DEFAULT_MIN_DEVICES_FOR_OPTIMIZING 5 // do not optimize sets with less than this number of devices
#define DEFAULT_MAX_OPTIMIZER_SCENES 50 // general upper limit suggestion, individual vdcs might set other defaults
#define DEFAULT_MAX_OPTIMIZER_GROUPS 20 // general upper limit suggestion, individual vdcs might set other defaults



Vdc::Vdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  inherited(aVdcHostP),
  inheritedParams(aVdcHostP->getDsParamStore()),
  instanceNumber(aInstanceNumber),
  defaultZoneID(0),
  optimizerMode(opt_unavailable), // no optimisation by default
  vdcFlags(0),
  tag(aTag),
  rescanInterval(Never),
  rescanMode(rescanmode_incremental),
  collecting(false),
  delivering(false),
  totalOptimizableCalls(0),
  minCallsBeforeOptimizing(DEFAULT_MIN_CALLS_BEFORE_OPTIMIZING),
  minDevicesForOptimizing(DEFAULT_MIN_DEVICES_FOR_OPTIMIZING),
  maxOptimizerScenes(DEFAULT_MAX_OPTIMIZER_SCENES),
  maxOptimizerGroups(DEFAULT_MAX_OPTIMIZER_GROUPS)
{
}


Vdc::~Vdc()
{
}


void Vdc::addVdcToVdcHost()
{
  // derive dSUID first, as it will be mapped by dSUID in the device container
  deriveDsUid();
  // add to container
  getVdcHost().addVdc(VdcPtr(this));
}



void Vdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  // done
  aCompletedCB(ErrorPtr()); // default to error-free initialisation
}


#if SELFTESTING_ENABLED
void Vdc::selfTest(StatusCB aCompletedCB)
{
  // by default, signal "no hardware tested"
  aCompletedCB(Error::err<VdcError>(VdcError::NoHWTested, "No hardware tested"));
}
#endif


const char *Vdc::getPersistentDataDir()
{
  return getVdcHost().getPersistentDataDir();
}


void Vdc::handleGlobalEvent(VdchostEvent aEvent)
{
  // note: global events that reach the vdcs are not too frequent and must be distributed to all devices
  if (aEvent==vdchost_logstats) {
    if (optimizerMode>opt_disabled) {
      optimizerCacheStats();
    }
  }
  for (DeviceVector::iterator pos = devices.begin(); pos!=devices.end(); ++pos) {
    (*pos)->handleGlobalEvent(aEvent);
  }
  inherited::handleGlobalEvent(aEvent);
}


/// MARK: - Identification


string Vdc::modelUID()
{
  // use vDC identifier as modelID
  DsUid vdcNamespace(DSUID_P44VDC_MODELUID_UUID);
  // now make UUIDv5 type dSUID out of it
  DsUid modelUID;
  modelUID.setNameInSpace(vdcClassIdentifier(), vdcNamespace);
  return modelUID.getString();
}


string Vdc::getName()
{
  if (inherited::getName().empty()) {
    // no name set for this vdc
    // - check if vdc host has a name
    if (!getVdcHost().getName().empty()) {
      // there is a custom name set for the entire vdc host, use it as base for default names
      return string_format("%s %s", getVdcHost().getName().c_str(), vdcModelSuffix().c_str());
    }
  }
  // just use assigned name
  return inherited::getName();
}


void Vdc::setName(const string &aName)
{
  if (aName!=getAssignedName()) {
    // has changed
    inherited::setName(aName);
    // make sure it will be saved
    markDirty();
  }
}


int Vdc::getInstanceNumber() const
{
  return instanceNumber;
}


void Vdc::deriveDsUid()
{
  // class containers have v5 UUIDs based on the device container's master UUID as namespace
  string name = string_format("%s.%d", vdcClassIdentifier(), getInstanceNumber()); // name is class identifier plus instance number: classID.instNo
  dSUID.setNameInSpace(name, getVdcHost().dSUID); // domain is dSUID of device container
}


string Vdc::vdcInstanceIdentifier() const
{
  string s(vdcClassIdentifier());
  string_format_append(s, ".%d@", getInstanceNumber());
  s.append(getVdcHost().dSUID.getString());
  return s;
}


bool Vdc::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("vdc", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


string Vdc::vendorName()
{
  // default to same vendor as vdc host (device container)
  return getVdcHost().vendorName();
}


string Vdc::modelVersion() const
{
  string v = vdcModelVersion();
  if (v.empty()) {
    return inherited::modelVersion();
  }
  else {
    return inherited::modelVersion() + " / " + v;
  }
}


string Vdc::modelName()
{
  // derive the descriptive name
  // "%M %m"
  string n = getVdcHost().vdcModelNameTemplate;
  if (n.empty()) n = DEFAULT_MODELNAME_TEMPLATE;
  string s;
  size_t i;
  // Vendor (of the vdc, defaults to vendor of the vdchost unless vdc has its own vendor)
  while ((i = n.find("%V"))!=string::npos) { n.replace(i, 2, vendorName()); }
  // Model of the vdchost
  while ((i = n.find("%M"))!=string::npos) { n.replace(i, 2, getVdcHost().modelName()); }
  // vdc model suffix
  while ((i = n.find("%m"))!=string::npos) { n.replace(i, 2, vdcModelSuffix()); }
  // Serial/hardware ID
  s = getVdcHost().getDeviceHardwareId();
  if (s.empty()) {
    // use dSUID if no other ID is specified
    s = getVdcHost().getDsUid().getString();
  }
  while ((i = n.find("%S"))!=string::npos) { n.replace(i, 2, s); }
  return n;
}


void Vdc::identifyToUser()
{
  // by default, delegate to vdchost (as likely physical "location" of all vdcs)
  return getVdcHost().identifyToUser();
}


bool Vdc::canIdentifyToUser()
{
  // by default, delegate to vdchost (as likely physical "location" of all vdcs)
  return getVdcHost().canIdentifyToUser();
}


/// MARK: - grouped delivery of notification to devices (for scene/group optimizations)


NotificationDeliveryState::~NotificationDeliveryState()
{
  // if this delivery was executing, report completion to vDC
  if (delivering) {
    delivering = false;
    vdc.notificationDeliveryComplete(*this);
  }
}


static const char *NotificationNames[numNotificationTypes] = {
  "undefined",
  "none",
  "callScene",
  "dimChannel",
  "retrigger"
};


void Vdc::deliverToAudience(DsAddressablesList aAudience, VdcApiConnectionPtr aApiConnection, const string &aNotification, ApiValuePtr aParams)
{
  if (aNotification=="callScene") {
    // call scene
    NotificationDeliveryStatePtr nds = NotificationDeliveryStatePtr(new NotificationDeliveryState(*this));
    nds->audience = aAudience;
    nds->callType = ntfy_callscene;
    nds->callParams = aParams;
    nds->optimizedType = ntfy_undefined; // first device will decide (callScene might become dimChannel)
    queueDelivery(nds);
    return;
  }
  else if (aNotification=="dimChannel") {
    // start or stop dimming a channel
    NotificationDeliveryStatePtr nds = NotificationDeliveryStatePtr(new NotificationDeliveryState(*this));
    nds->audience = aAudience;
    nds->callType = ntfy_dimchannel;
    nds->callParams = aParams;
    nds->optimizedType = ntfy_dimchannel; // dimchannel will always remain dimchannel
    queueDelivery(nds);
    return;
  }
  else {
    // not a specially handled/optimized notification: just let every device handle it individually
    for (DsAddressablesList::iterator apos = aAudience.begin(); apos!=aAudience.end(); ++apos) {
      (*apos)->handleNotification(aApiConnection, aNotification, aParams);
    }
  }
}


void Vdc::queueDelivery(NotificationDeliveryStatePtr aDeliveryState)
{
  // starting a new callScene or dimChannel must make sure no delayed output state saving (native scene update) is still pending
  cancelNativeActionUpdate();
  if (delivering) {
    // queue for when current delivery is done
    pendingDeliveries.push_back(aDeliveryState);
    ALOG(LOG_INFO, "'%s' delivery queued (previous delivery still running) - now %lu queued deliveries", NotificationNames[aDeliveryState->callType], pendingDeliveries.size());
  }
  else {
    // optimization - start right now
    ALOG(LOG_INFO, "===== '%s' delivery to %lu devices starts now", NotificationNames[aDeliveryState->callType], aDeliveryState->audience.size());
    delivering = true;
    aDeliveryState->delivering = true;
    prepareNextNotification(aDeliveryState);
  }
}


void Vdc::notificationDeliveryComplete(NotificationDeliveryState &aDeliveryStateBeingDeleted)
{
  // - done
  delivering = false;
  ALOG(LOG_INFO, "===== '%s' delivery complete", NotificationNames[aDeliveryStateBeingDeleted.callType]);
  // check for pending deliveries
  if (pendingDeliveries.size()>0) {
    // get next from queue
    NotificationDeliveryStatePtr nds = pendingDeliveries.front();
    // - remove from queue, only passed smart pointer keeps the object now. This is important
    //   because we rely on the NotificationDeliveryState's destructor to call us back here when done!
    pendingDeliveries.pop_front();
    // - now start
    ALOG(LOG_INFO, "===== '%s' queued delivery to %ld devices starts now", NotificationNames[nds->callType], nds->audience.size());
    delivering = true;
    nds->delivering = true;
    prepareNextNotification(nds);
  }
}


void Vdc::prepareNextNotification(NotificationDeliveryStatePtr aDeliveryState)
{
  if (aDeliveryState->audience.empty()) {
    // preparation complete, now process affected devices
    executePreparedNotification(aDeliveryState);
  }
  else {
    // need to prepare next device
    DevicePtr dev = boost::dynamic_pointer_cast<Device>(aDeliveryState->audience.front());
    if (dev) {
      dev->notificationPrepare(boost::bind(&Vdc::notificationPrepared, this, aDeliveryState, _1), aDeliveryState);
    }
  }
}


void Vdc::notificationPrepared(NotificationDeliveryStatePtr aDeliveryState, NotificationType aNotificationToApply)
{
  // front device in audience is now prepared
  DevicePtr dev = boost::dynamic_pointer_cast<Device>(aDeliveryState->audience.front());
  aDeliveryState->audience.pop_front(); // processed, remove from list
  if (aNotificationToApply==ntfy_retrigger) {
    // nothing to apply, retrigger repeat when it is running
    if (optimizedCallRepeaterTicket && dev->currentAutoStopTime!=Never) {
      AFOCUSLOG("- retriggering repeater for another %.2f seconds", (double)(dev->currentAutoStopTime)/Second);
      MainLoop::currentMainLoop().rescheduleExecutionTicket(optimizedCallRepeaterTicket, dev->currentAutoStopTime);
    }
  }
  else if (aNotificationToApply!=ntfy_none) {
    // this notification should be applied
    if (aDeliveryState->optimizedType==ntfy_undefined) {
      // first to-be-applied notification determines actual type
      aDeliveryState->optimizedType = aNotificationToApply;
      // Note: first device also decides about the actionParam (and has set it in notificationPrepare() when needed)
    }
    if (optimizerMode<=opt_disabled || aDeliveryState->optimizedType!=aNotificationToApply || !dev->addToOptimizedSet(aDeliveryState)) {
      // optimisation off, different notification type than others in set, or otherwise not optimizable -> just execute and apply right now
      dev->updateDeliveryState(aDeliveryState, false); // still: do basic updating of state such that processing has all the info
      getVdcHost().deviceWillApplyNotification(dev, *aDeliveryState); // let vdchost process for possibly updating global zone state
      dev->executePreparedOperation(boost::bind(&Vdc::preparedOperationExecuted, this, dev), aNotificationToApply);
    }
  }
  // break caller chain by going via mainloop
  MainLoop::currentMainLoop().executeNow(boost::bind(&Vdc::prepareNextNotification, this, aDeliveryState));
}


void Vdc::preparedOperationExecuted(DevicePtr aDevice)
{
  // non-optimized execution of operation complete
  aDevice->releasePreparedOperation();
}


bool Vdc::shouldUseOptimizerFor(NotificationDeliveryStatePtr aDeliveryState)
{
  // simple base class strategy: at least MIN_DEVICES_TO_OPTIMIZE devices must be involved.
  // derived classes can use refined strategy more suitable for the hardware
  return aDeliveryState->affectedDevices.size()>=minDevicesForOptimizing;
}


void Vdc::executePreparedNotification(NotificationDeliveryStatePtr aDeliveryState)
{
  if (aDeliveryState->affectedDevices.size()>0) {
    totalOptimizableCalls++;
    AFOCUSLOG(
      "notification #%ld affects %lu optimizable devices for '%s' with hash=%s, contentId=%d, contentsHash=%016llX",
      totalOptimizableCalls,
      aDeliveryState->affectedDevices.size(),
      NotificationNames[aDeliveryState->optimizedType],
      binaryToHexString(aDeliveryState->affectedDevicesHash).c_str(),
      aDeliveryState->contentId,
      aDeliveryState->contentsHash
    );
    OptimizerEntryPtr entry;
    if (shouldUseOptimizerFor(aDeliveryState)) {
      // search for cache entry
      for (OptimizerEntryList::iterator pos = optimizerCache.begin(); pos!=optimizerCache.end(); ++pos) {
        OptimizerEntryPtr e = *pos;
        if (e->type==aDeliveryState->optimizedType) {
          // correct optimized type
          if (e->affectedDevicesHash==aDeliveryState->affectedDevicesHash) {
            // same set of affected devices
            if (e->contentId==aDeliveryState->contentId) {
              // match
              AFOCUSLOG("- found cache entry with %ld calls already", e->numCalls);
              entry = e;
              break;
            }
          }
        }
      }
      if (!entry && optimizerMode==opt_auto) {
        AFOCUSLOG("- creating new cache entry");
        entry = OptimizerEntryPtr(new OptimizerEntry);
        entry->type = aDeliveryState->optimizedType;
        entry->affectedDevicesHash = aDeliveryState->affectedDevicesHash;
        entry->contentId = aDeliveryState->contentId;
        entry->contentsHash = aDeliveryState->contentsHash;
        entry->numberOfDevices = (int)aDeliveryState->affectedDevices.size();
        // TODO: limit number of entries, kick out least used ones
        optimizerCache.push_back(entry);
      }
    }
    if (entry) {
      // replace actual count by time-weighted call (fading with time since lastUse)
      entry->numCalls = entry->timeWeightedCallCount();
      entry->lastUse = MainLoop::now();
      // count the call (but not in frozen mode, as new entries are not created so new candidates can't get their count and can't compete)
      if (optimizerMode>opt_frozen) {
        entry->numCalls++;
      }
      // can we already use the entry?
      if (!entry->nativeActionId.empty()) {
        // cache entry already has a native action identifier attached
        AFOCUSLOG("- native action already exists: '%s'", entry->nativeActionId.c_str());
        if (entry->contentsHash==aDeliveryState->contentsHash) {
          // content has not changed since native action was last updated -> we can use it!
          ALOG(LOG_NOTICE, "Optimized %s: executing %s using native action '%s' (contentId %d, variant %d)", NotificationNames[aDeliveryState->callType], NotificationNames[aDeliveryState->optimizedType], entry->nativeActionId.c_str(), entry->contentId, aDeliveryState->actionVariant);
          if (aDeliveryState->repeatAfter!=Never) {
            AFOCUSLOG("- action scheduled to repeat in %.2f seconds", (double)(aDeliveryState->repeatAfter)/Second);
            // Note: it is essential to create a new delivery state here, otherwise the original notification cannot complete (coupled to object lifetime)
            NotificationDeliveryStatePtr rep = NotificationDeliveryStatePtr(new NotificationDeliveryState(*this));
            // shallow copy only, this deliveryState is only used for callNativeAction() and is considered already prepared
            rep->contentId = aDeliveryState->contentId;
            rep->affectedDevices = aDeliveryState->affectedDevices;
            rep->optimizedType = aDeliveryState->optimizedType;
            rep->actionParam = aDeliveryState->actionParam;
            rep->actionVariant = aDeliveryState->repeatVariant; // is a varied repetition of the original notification
            // Note: not copied because not needed: audience, hashes, callType, actionParams
            optimizedCallRepeaterTicket.executeOnce(boost::bind(&Vdc::repeatPreparedNotification, this, entry, rep), aDeliveryState->repeatAfter);
          }
          else {
            // cancel possibly running repeater ticket
            if (optimizedCallRepeaterTicket) {
              AFOCUSLOG("- cancelling repeating previous action");
              optimizedCallRepeaterTicket.cancel();
            }
          }
          callNativeAction(boost::bind(&Vdc::finalizePreparedNotification, this, entry, aDeliveryState, _1), entry->nativeActionId, aDeliveryState);
          return;
        }
        else {
          if (optimizerMode>=opt_update) {
            AFOCUSLOG("- content hash mismatch -> cannot be called now, must be updated later");
            finalizePreparedNotification(entry, aDeliveryState, Error::err<VdcError>(VdcError::StaleAction, "Stale native action '%s'", entry->nativeActionId.c_str()));
          }
          else {
            AFOCUSLOG("- content hash mismatch in frozen mode -> just execute normally now");
            finalizePreparedNotification(entry, aDeliveryState, Error::ok());
          }
          return;
        }
      }
      else {
        // affected device set/contentId has no native scene/group installed yet
        AFOCUSLOG("- no native action assigned yet -> checking statistics to see if we should add one");
        if (optimizerMode==opt_auto && entry->timeWeightedCallCount()>=minCallsBeforeOptimizing) {
          ALOG(LOG_NOTICE, "Optimizer: %s for these devices has occurred repeatedly (weighted: %ld times) -> optimzing it using native action", NotificationNames[aDeliveryState->optimizedType], entry->timeWeightedCallCount());
          finalizePreparedNotification(entry, aDeliveryState, Error::err<VdcError>(VdcError::AddAction, "Request adding native action"));
          return;
        }
        else {
          // not adding action, we'll wait and see if this call repeats enough to do so
          AFOCUSLOG("- not important enough -> just execute normally now");
          finalizePreparedNotification(entry, aDeliveryState, Error::ok());
          return;
        }
      }
    }
    else {
      // no cache entry (because optimizer off or not enough devices in affected set
      AFOCUSLOG("- no cache entry (optimizer off or set of devices not suitable for optimization) -> just execute normally now");
      finalizePreparedNotification(entry, aDeliveryState, Error::ok()); // non-null but OK error means that we have nothing applied yet
      return;
    }
  }
  // no affected devices
}


void Vdc::repeatPreparedNotification(OptimizerEntryPtr aEntry, NotificationDeliveryStatePtr aDeliveryState)
{
  // this is called to repeat an action after a timeout (usually, dim autostop)
  optimizedCallRepeaterTicket = 0;
  // - prepare affected devices for repeat
  for (DeviceList::iterator pos = aDeliveryState->affectedDevices.begin(); pos!=aDeliveryState->affectedDevices.end(); ++pos) {
    (*pos)->optimizerRepeatPrepare(aDeliveryState);
  }
  // - call the native action again
  AFOCUSLOG("- scheduled repeat of action %s", aEntry->nativeActionId.c_str());
  callNativeAction(boost::bind(&Vdc::finalizeRepeatedNotification, this, aEntry, aDeliveryState), aEntry->nativeActionId, aDeliveryState);
}


void Vdc::finalizeRepeatedNotification(OptimizerEntryPtr aEntry, NotificationDeliveryStatePtr aDeliveryState)
{
  AFOCUSLOG("Finalizing repeated notification call");
  // let all devices know operation has repeated
  for (DeviceList::iterator pos = aDeliveryState->affectedDevices.begin(); pos!=aDeliveryState->affectedDevices.end(); ++pos) {
    // Note: vdchost does not need to be informed about repeated notifications (so deviceProcessedNotification is not called here)
    (*pos)->executePreparedOperation(NULL, ntfy_none);
  }
}


void Vdc::finalizePreparedNotification(OptimizerEntryPtr aEntry, NotificationDeliveryStatePtr aDeliveryState, ErrorPtr aError)
{
  if (Error::notOK(aError) && !aError->isDomain(VdcError::domain())) {
    // actual error, not only signalling add/update
    ALOG(LOG_ERR, "Failed calling native action: %s", Error::text(aError));
  }
  bool notAppliedYet = aError!=NULL; // any error, including Error::OK, means that notification was NOT applied yet
  // now let all devices either finish the operation or apply it (in case no cached operation was applied on vdc level)
  // Note: if notAppliedYet is not set, actual apply operation was done via a native action call already.
  //   Still, all devices must be notified to update their software state
  // note: we let all devices do this in parallel, continue when last device reports done
  aDeliveryState->pendingCount = aDeliveryState->affectedDevices.size(); // must be set before calling executePreparedOperation() the first time
  for (DeviceList::iterator pos = aDeliveryState->affectedDevices.begin(); pos!=aDeliveryState->affectedDevices.end(); ++pos) {
    getVdcHost().deviceWillApplyNotification(*pos, *aDeliveryState); // let vdchost process for possibly updating global zone state
    (*pos)->executePreparedOperation(boost::bind(&Vdc::preparedDeviceExecuted, this, aEntry, aDeliveryState, aError), notAppliedYet ? aDeliveryState->optimizedType : ntfy_none);
  }
}


void Vdc::preparedDeviceExecuted(OptimizerEntryPtr aEntry, NotificationDeliveryStatePtr aDeliveryState, ErrorPtr aError)
{
  if (--aDeliveryState->pendingCount>0) {
    AFOCUSLOG("waiting for all affected devices to confirm apply: %zu/%lu remaining", aDeliveryState->pendingCount, aDeliveryState->affectedDevices.size());
    return; // not all confirmed yet
  }
  // check if we need to update or create native actions
  if (Error::isError(aError, VdcError::domain(), VdcError::AddAction)) {
    // create native action for this set of devices
    createNativeAction(boost::bind(&Vdc::createdNativeAction, this, aEntry, aDeliveryState, _1), aEntry, aDeliveryState);
    return;
  }
  else if (Error::isError(aError, VdcError::domain(), VdcError::StaleAction)) {
    // update native action contents
    ALOG(LOG_NOTICE, "Updating native action '%s' for '%s' (variant %d)", aEntry->nativeActionId.c_str(), NotificationNames[aDeliveryState->optimizedType], aDeliveryState->actionVariant);
    aEntry->contentsHash = aDeliveryState->contentsHash; // prepare new hash BEFORE - delayed updates might want to reset it in updateNativeAction()
    updateNativeAction(boost::bind(&Vdc::preparedNotificationComplete, this, aEntry, aDeliveryState, true, _1), aEntry, aDeliveryState); // is a change of the entry
    return;
  }
  else {
    // nothing to do at vdc implementation level
    preparedNotificationComplete(aEntry, aDeliveryState, false, ErrorPtr());
    return;
  }
}


#define CALL_COUNT_WEIGHT 3 // weight of call count for score (relative to device count)
#define NUM_DEVICES_WEIGHT 5 // weight of device count for score (relative to call count)
#define MIN_ENTRY_AGE (1*Day) // how old another entry must be at least to get kicked out (to prevent quickly oscillating between two scenes)
#define MIN_SCORE_DIFFERENCE (3*10000*CALL_COUNT_WEIGHT) // minimum score difference between new and kicked out entries (here specified in call count "units")

void Vdc::createdNativeAction(OptimizerEntryPtr aEntry, NotificationDeliveryStatePtr aDeliveryState, ErrorPtr aError)
{
  if (Error::isError(aError, VdcError::domain(), VdcError::NoMoreActions)) {
    // could not add another action because there are too many already.
    // go through cache to find least important entry
    // - timeWeightedCallCount()
    // - lastUse
    // - numberOfDevices
    MLMicroSeconds now = MainLoop::now();
    OptimizerEntryPtr loosingEntry;
    long loosingScore;
    long refScore = (aEntry->timeWeightedCallCount()*CALL_COUNT_WEIGHT + aEntry->numberOfDevices*NUM_DEVICES_WEIGHT)*10000;
    AFOCUSLOG("========= Scoring optimizer entries to find an entry to replace, refScore=%ld", refScore);
    for (OptimizerEntryList::iterator pos = optimizerCache.begin(); pos!=optimizerCache.end(); ++pos) {
      OptimizerEntryPtr entry = *pos;
      if (entry!=aEntry && !entry->nativeActionId.empty() && entry->type==aEntry->type && (now-entry->lastNativeChange>MIN_ENTRY_AGE)) {
        // not myself, right type, has native action, old enough -> is a competing entry
        // Scoring components:
        // - high timeWeightedCallCount()
        //   - lastUse counts for entries with timeWeightedCallCount()==0, newer one is more important
        // - high numberOfDevices
        long days = (now-entry->lastUse)/Hour/24;
        if (days>10000) days = 10000;
        long score =
          (entry->timeWeightedCallCount()*CALL_COUNT_WEIGHT + entry->numberOfDevices*NUM_DEVICES_WEIGHT)*10000 +
          (10000-days);
        FOCUSLOG("- '%s' called %ld times (weighted), last call %lld S ago, last modified %lld S ago, with native action '%s' for %d devices -> score = %ld",
          NotificationNames[entry->type],
          entry->timeWeightedCallCount(),
          (now-entry->lastUse)/Second,
          (now-entry->lastNativeChange)/Second,
          entry->nativeActionId.c_str(),
          entry->numberOfDevices,
          score
        );
        if (score<refScore && (!loosingEntry || score+MIN_SCORE_DIFFERENCE<loosingScore)) {
          // lowest score seen, and lower than new entry's
          loosingScore = score;
          loosingEntry = entry;
        }
      }
    }
    if (loosingEntry) {
      // found an entry to remove
      ALOG(LOG_NOTICE, "Entry for action '%s' (score=%ld) will be removed to make room for new entry (score=%ld)", loosingEntry->nativeActionId.c_str(), loosingScore, refScore);
      freeNativeAction(boost::bind(&Vdc::removedNativeAction, this, loosingEntry, aEntry, aDeliveryState, _1), loosingEntry->nativeActionId);
      return;
    }
    else {
      ALOG(LOG_INFO, "Could not create native action now: no room for new action, and none old/unimportant enough to delete");
      preparedNotificationComplete(aEntry, aDeliveryState, false, ErrorPtr());
      return;
    }
  }
  else if (Error::isOK(aError)) {
    ALOG(LOG_NOTICE, "Created native action '%s' for '%s' (contentId %d, variant %d)", aEntry->nativeActionId.c_str(), NotificationNames[aDeliveryState->optimizedType], aEntry->contentId, aDeliveryState->actionVariant);
    preparedNotificationComplete(aEntry, aDeliveryState, true, aError);
    return;
  }
  // other error
  preparedNotificationComplete(aEntry, aDeliveryState, false, aError);
}

void Vdc::removedNativeAction(OptimizerEntryPtr aFromEntry, OptimizerEntryPtr aForEntry, NotificationDeliveryStatePtr aDeliveryState, ErrorPtr aError)
{
  if (Error::notOK(aError)) {
    ALOG(LOG_INFO, "Could not delete action to make room for a new one");
    preparedNotificationComplete(aForEntry, aDeliveryState, false, aError);
    return;
  }
  aFromEntry->nativeActionId.clear();
  // retry adding now - if it fails again with VdcError::NoMoreActions, we will not repeat (should not happen anyway after having removed an entry before)
  createNativeAction(boost::bind(&Vdc::preparedNotificationComplete, this, aForEntry, aDeliveryState, true, _1), aForEntry, aDeliveryState); // is a change when successful
}



void Vdc::preparedNotificationComplete(OptimizerEntryPtr aEntry, NotificationDeliveryStatePtr aDeliveryState, bool aChanged, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    if (aChanged) aEntry->markDirty(); // is a successful change, must be persisted
  }
  else {
    ALOG(LOG_WARNING, "Creating or updating native action has failed: %s", Error::text(aError));
  }
  // release anything left from preparation now
  for (DeviceList::iterator pos = aDeliveryState->affectedDevices.begin(); pos!=aDeliveryState->affectedDevices.end(); ++pos) {
    (*pos)->releasePreparedOperation();
  }
  if (LOGENABLED(LOG_INFO) && optimizerMode>opt_disabled) {
    // show current statistics
    optimizerCacheStats(aEntry);
  }
}


void Vdc::optimizerCacheStats(OptimizerEntryPtr aCurrentEntry)
{
  string stats;
  for (OptimizerEntryList::iterator pos = optimizerCache.begin(); pos!=optimizerCache.end(); ++pos) {
    OptimizerEntryPtr oe = *pos;
    string_format_append(stats,
      "%c '%s' called %ld times (weighted, raw=%ld), last %lld seconds ago, contentId=%d, numdevices=%d, nativeAction='%s'\n",
      oe==aCurrentEntry ? '*' : '-', // mark entry used in current call
      NotificationNames[oe->type],
      oe->timeWeightedCallCount(),
      oe->numCalls,
      (MainLoop::now()-oe->lastUse)/Second,
      oe->contentId,
      oe->numberOfDevices,
      oe->nativeActionId.c_str()
    );
  }
  LOG(LOG_NOTICE,
    "\nOptimizer statistics after %ld optimizable calls for vDC %s:\n%s\n",
    totalOptimizableCalls, shortDesc().c_str(),
    stats.c_str()
  );
}


void Vdc::callNativeAction(StatusCB aStatusCB, const string aNativeActionId, NotificationDeliveryStatePtr aDeliveryState)
{
  // base class does not support native actions
  aStatusCB(TextError::err("Native action '%s' not supported", aNativeActionId.c_str()));
}


void Vdc::createNativeAction(StatusCB aStatusCB, OptimizerEntryPtr aOptimizerEntry, NotificationDeliveryStatePtr aDeliveryState)
{
  aStatusCB(TextError::err("createNativeAction not implemented yet"));
}


void Vdc::updateNativeAction(StatusCB aStatusCB, OptimizerEntryPtr aOptimizerEntry, NotificationDeliveryStatePtr aDeliveryState)
{
  aStatusCB(TextError::err("updateNativeAction not implemented yet"));
}



// note: clearing operation might continue in background
void Vdc::clearOptimizerCache()
{
  while (optimizerCache.size()>0) {
    OptimizerEntryPtr e = optimizerCache.front();
    optimizerCache.pop_front();
    e->deleteFromStore();
    // free this one and continue clearing later
    freeNativeAction(boost::bind(&Vdc::clearOptimizerCache, this), e->nativeActionId);
    return;
  }
  ALOG(LOG_WARNING, "Optimizer cache cleared");
}




/// MARK: - handle vdc level methods

ErrorPtr Vdc::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="scanDevices") {
    // vDC API v2c addition, only via genericRequest
    // (re)collect devices of this particular vDC
    bool incremental = true;
    bool exhaustive = false;
    bool reenumerate = false;
    bool clear = false;
    RescanMode mode = rescanmode_none;
    checkBoolParam(aParams, "incremental", incremental);
    // prevent more dangerous scans from vDC API
    if (aRequest->connection()!=getVdcHost().getSessionConnection()) {
      checkBoolParam(aParams, "exhaustive", exhaustive);
      checkBoolParam(aParams, "reenumerate", reenumerate);
      checkBoolParam(aParams, "clearconfig", clear);
    }
    if (exhaustive)
      mode |= rescanmode_exhaustive;
    else if (incremental)
      mode |= rescanmode_incremental;
    else
      mode |= rescanmode_normal;
    if (clear) mode |= rescanmode_clearsettings;
    if (reenumerate) mode |= rescanmode_reenumerate;
    mode |= rescanmode_force; // when explicitly triggered via method, even try scanning when there is a global vdc error present
    collectDevices(boost::bind(&DsAddressable::methodCompleted, this, aRequest, _1), mode);
  }
  else if (aMethod=="pair") {
    // only via genericRequest
    // (re)collect devices of this particular vDC
    Tristate establish = undefined; // default to pair or unpair
    ApiValuePtr o = aParams->get("establish");
    if (o && !o->isNull()) {
      establish = o->boolValue() ? yes : no;
    }
    bool disableProximityCheck = false; // default to proximity check enabled (if technology can detect proximity)
    checkBoolParam(aParams, "disableProximityCheck", disableProximityCheck);
    int timeout = 30; // default to 30 seconds timeout
    o = aParams->get("timeout");
    if (o) {
      timeout = o->int32Value();
    }
    // actually run the pairing process
    performPair(aRequest, establish, disableProximityCheck, timeout*Second);
  }
  else {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }
  return respErr;
}


/// MARK: - paring

void Vdc::performPair(VdcApiRequestPtr aRequest, Tristate aEstablish, bool aDisableProximityCheck, MLMicroSeconds aTimeout)
{
  // anyway - first stop any device-wide learn that might still be running on this or other vdcs
  pairTicket.cancel();
  getVdcHost().stopLearning();
  if (aTimeout<=0) {
    // calling with timeout==0 means aborting learn (which has already happened by now)
    // - confirm with OK
    ALOG(LOG_NOTICE, "- pairing aborted");
    aRequest->sendStatus(Error::err<VdcApiError>(404, "pairing/unpairing aborted"));
    return;
  }
  // start new pairing
  ALOG(LOG_NOTICE, "Starting single vDC pairing");
  pairTicket.executeOnce(boost::bind(&Vdc::pairingTimeout, this, aRequest), aTimeout);
  getVdcHost().learnHandler = boost::bind(&Vdc::pairingEvent, this, aRequest, _1, _2);
  setLearnMode(true, aDisableProximityCheck, aEstablish);
}


void Vdc::pairingEvent(VdcApiRequestPtr aRequest, bool aLearnIn, ErrorPtr aError)
{
  pairTicket.cancel();
  if (Error::isOK(aError)) {
    if (aLearnIn) {
      // learned in something
      ALOG(LOG_NOTICE, "- pairing established");
      aRequest->sendStatus(Error::ok());
    }
    else {
      // learned out something
      ALOG(LOG_NOTICE, "- pairing removed");
      aRequest->sendStatus(Error::err<VdcApiError>(410, "device unpaired"));
    }
  }
  else {
    aRequest->sendError(aError);
  }
}


void Vdc::pairingTimeout(VdcApiRequestPtr aRequest)
{
  getVdcHost().stopLearning();
  ALOG(LOG_NOTICE, "- timeout: no pairing or unpairing occurred");
  aRequest->sendStatus(Error::err<VdcApiError>(404, "timeout, no (un)pairing event occurred"));
}


// MARK: - Collecting devices

void Vdc::collectDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  // prevent collecting from vdc which has global error (except if rescanmode_force is set)
  if ((aRescanFlags&rescanmode_force)==0 && Error::notOK(vdcErr)) {
    if (aCompletedCB) aCompletedCB(vdcErr);
    return;
  }
  // prevent collecting while already collecting
  if (collecting) {
    // already collecting - don't collect again
    ALOG(LOG_WARNING,"requested collecting while already collecting");
    if (aCompletedCB) aCompletedCB(Error::err<VdcError>(VdcError::Collecting, "already collecting"));
    return;
  }
  collecting = true;
  // call actual vdc's implementation
  scanForDevices(
    boost::bind(&Vdc::collectedDevices, this, aCompletedCB, _1),
    aRescanFlags
  );
}


void Vdc::collectedDevices(StatusCB aCompletedCB, ErrorPtr aError)
{
  // call back
  if (aCompletedCB) aCompletedCB(aError);
  // store as global error
  setVdcError(aError);
  // done
  collecting = false;
  // now schedule periodic recollect
  schedulePeriodicRecollecting();
}


void Vdc::scheduleRecollect(RescanMode aRescanMode, MLMicroSeconds aDelay)
{
  rescanTicket.cancel();
  rescanTicket.executeOnce(boost::bind(&Vdc::initiateRecollect, this, aRescanMode), aDelay);
}



void Vdc::schedulePeriodicRecollecting()
{
  rescanTicket.cancel();
  if (rescanInterval!=Never) {
    rescanTicket.executeOnce(boost::bind(&Vdc::initiateRecollect, this, rescanMode), rescanInterval);
  }
}


void Vdc::initiateRecollect(RescanMode aRescanMode)
{
  if (delivering) {
    ALOG(LOG_NOTICE, "busy processing notification: postponed in-operation recollect");
    schedulePeriodicRecollecting();
  }
  else {
    ALOG(LOG_NOTICE, "starting in-operation recollect");
    collectDevices(boost::bind(&Vdc::recollectDone, this), aRescanMode);
    // end of collect will schedule periodic recollect again
  }
}


void Vdc::recollectDone()
{
  ALOG(LOG_NOTICE, "in-operation recollect done");
}



void Vdc::setPeriodicRecollection(MLMicroSeconds aRecollectInterval, RescanMode aRescanFlags)
{
  rescanInterval = aRecollectInterval;
  rescanMode = aRescanFlags;
  if (!isCollecting()) {
    // not already collecting, start schedule now (otherwise, end of collecting will schedule next recollect
    schedulePeriodicRecollecting();
  }
}



// MARK: - Managing devices


void Vdc::removeDevice(DevicePtr aDevice, bool aForget)
{
	// find and remove from my list.
	for (DeviceVector::iterator pos = devices.begin(); pos!=devices.end(); ++pos) {
		if (*pos==aDevice) {
			devices.erase(pos);
			break;
		}
	}
  // remove from global device container
  getVdcHost().removeDevice(aDevice, aForget);
}



void Vdc::removeDevices(bool aForget)
{
	for (DeviceVector::iterator pos = devices.begin(); pos!=devices.end(); ++pos) {
    DevicePtr dev = *pos;
    // inform upstream about these devices going offline now (if API connection is up at all at this time)
    dev->reportVanished();
    // now actually remove
    getVdcHost().removeDevice(dev, aForget);
  }
  // clear my own list
  devices.clear();
}



void Vdc::identifyDevice(DevicePtr aNewDevice, IdentifyDeviceCB aIdentifyCB, int aMaxRetries, MLMicroSeconds aRetryDelay)
{
  // Note: aNewDevice bound to the callback prevents it from being deleted during identification
  if (aNewDevice->identifyDevice(boost::bind(&Vdc::identifyDeviceCB, this, aNewDevice, aIdentifyCB, aMaxRetries, aRetryDelay, _1, _2))) {
    // instant identify, callback is not called by device -> simulate it at this level
    ALOG(LOG_WARNING, "has instant identification, but vdc seems to expect it to be non-instant!");
    identifyDeviceCB(aNewDevice, aIdentifyCB, 0, 0, ErrorPtr(), aNewDevice.get());
  }
}

void Vdc::identifyDeviceCB(DevicePtr aNewDevice, IdentifyDeviceCB aIdentifyCB, int aMaxRetries, MLMicroSeconds aRetryDelay, ErrorPtr aError, Device *aIdentifiedDevice)
{
  if (Error::isOK(aError)) {
    if (aIdentifiedDevice) {
      // success
      DevicePtr dev = DevicePtr(aIdentifiedDevice); // keep alive during callback
      // aNewDevice keeps original device alive, dev keeps identified device alive (might be the same)
      if (aIdentifyCB) aIdentifyCB(aError, aIdentifiedDevice);
      // now dev and aNewDevice go out of scope -> objects will be deleted when no longer used anywhere else. 
      return;
    }
    // no device
    aError = Error::err<VdcError>(VdcError::NoDevice, "identifyDevice returned no device");
  }
  // failed, check for retries
  if (aMaxRetries>0) {
    // report this error into the log
    LOG(LOG_WARNING, "device identification failed: %s -> retrying %d times", aError->text(), aMaxRetries);
    aMaxRetries--;
    identifyTicket.executeOnce(boost::bind(&Vdc::identifyDevice, this, aNewDevice, aIdentifyCB, aMaxRetries, aRetryDelay), aRetryDelay);
    return;
  }
  // no retries left, give up
  // Note: break handler chain to make sure initial trigger (such as http request callback) terminates BEFORE device gets deleted
  MainLoop::currentMainLoop().executeNow(boost::bind(&Vdc::identifyDeviceFailed, this, aNewDevice, aError, aIdentifyCB));
}


void Vdc::identifyDeviceFailed(DevicePtr aNewDevice, ErrorPtr aError, IdentifyDeviceCB aIdentifyCB)
{
  // this code will be called from mainloop, after handler chain that led to trigger for
  // identification failure has been unwound already.
  if (aIdentifyCB) aIdentifyCB(aError, NULL);
  // aNewDevice goes out of scope here, and somewhere up the caller chain all callbacks that still hold a reference
  // will get unwound so device will finally get deleted.
}


bool Vdc::simpleIdentifyAndAddDevice(DevicePtr aNewDevice)
{
  if (!aNewDevice->identifyDevice(NULL)) {
    // error: device does not support simple identification
    LOG(LOG_WARNING, "Could not identify device or device not supported -> ignored");
    return false;
  }
  // simple identification successful
  if (getVdcHost().addDevice(aNewDevice)) {
    // not a duplicate
    // - save in my own list
    devices.push_back(aNewDevice);
    // added
    return true;
  }
  // was a duplicate or could not be added for another reason
  return false;
}



void Vdc::identifyAndAddDevice(DevicePtr aNewDevice, StatusCB aCompletedCB, int aMaxRetries, MLMicroSeconds aRetryDelay)
{
  identifyDevice(aNewDevice, boost::bind(&Vdc::identifyAndAddDeviceCB, this, aCompletedCB, _1, _2), aMaxRetries, aRetryDelay);
}


void Vdc::identifyAndAddDeviceCB(StatusCB aCompletedCB, ErrorPtr aError, Device *aIdentifiedDevice)
{
  // Note: to keep aIdentifiedDevice alive, it must be wrapped into a DevicePtr now. Otherwise, it will be deleted
  if (Error::isOK(aError)) {
    // announce to global device container
    DevicePtr newDev(aIdentifiedDevice);
    if (getVdcHost().addDevice(newDev)) {
      // not a duplicate
      // - save in my own list
      devices.push_back(newDev);
    }
  }
  else {
    LOG(LOG_ERR, "Could not get device identification: %s -> ignored", aError->text());
    // we can't add this device, continue to next without adding
  }
  if (aCompletedCB) aCompletedCB(aError);
}



void Vdc::identifyAndAddDevices(DeviceList aToBeAddedDevices, StatusCB aCompletedCB, int aMaxRetries, MLMicroSeconds aRetryDelay, MLMicroSeconds aAddDelay)
{
  if (aToBeAddedDevices.size()>0) {
    // more devices to add
    DevicePtr dev = aToBeAddedDevices.front();
    aToBeAddedDevices.pop_front();
    identifyAndAddDevice(
      dev,
      boost::bind(&Vdc::identifyAndAddDevicesCB, this, aToBeAddedDevices, aCompletedCB, aMaxRetries, aRetryDelay, aAddDelay),
      aMaxRetries, aRetryDelay
    );
    return;
  }
  // done
  if (aCompletedCB) aCompletedCB(ErrorPtr());
}


void Vdc::identifyAndAddDevicesCB(DeviceList aToBeAddedDevices, StatusCB aCompletedCB, int aMaxRetries, MLMicroSeconds aRetryDelay, MLMicroSeconds aAddDelay)
{
  // even without add delay, it's important to defer this call to avoid stacking up calls along aToBeAddedDevices
  // Note: only now, remove the device from the list, which should deallocate it if it has not been added to the vdc(host) by now.
  identifyTicket.executeOnce(
    boost::bind(&Vdc::identifyAndAddDevices, this, aToBeAddedDevices, aCompletedCB, aMaxRetries, aRetryDelay, aAddDelay),
    aAddDelay
  );
}



// MARK: - persistent vdc level params

ErrorPtr Vdc::loadOptimizerCache()
{
  ErrorPtr err;

  // create a template
  OptimizerEntryPtr newEntry = OptimizerEntryPtr(new OptimizerEntry());
  // get the query
  sqlite3pp::query *queryP = newEntry->newLoadAllQuery(dSUID.getString().c_str());
  if (queryP==NULL) {
    // real error preparing query
    err = newEntry->paramStore.error();
  }
  else {
    for (sqlite3pp::query::iterator row = queryP->begin(); row!=queryP->end(); ++row) {
      // got record
      int index = 0;
      newEntry->loadFromRow(row, index, NULL);
      optimizerCache.push_back(newEntry);
      // - fresh object for next row
      newEntry = OptimizerEntryPtr(new OptimizerEntry());
    }
    delete queryP; queryP = NULL;
  }
  return err;
}


ErrorPtr Vdc::saveOptimizerCache()
{
  ErrorPtr err;

  // if any of the active entries is dirty, all of them need to be save (to keep relative call statistics)
  bool needsSave = false;
  for (OptimizerEntryList::iterator pos = optimizerCache.begin(); pos!=optimizerCache.end(); ++pos) {
    if (!(*pos)->nativeActionId.empty() && (*pos)->isDirty()) {
      needsSave = true;
      break;
    }
  }
  if (needsSave) {
    // save all entries with native actions, not only the dirty ones (statistics coherence)
    for (OptimizerEntryList::iterator pos = optimizerCache.begin(); pos!=optimizerCache.end(); ++pos) {
      if (!(*pos)->nativeActionId.empty()) {
        (*pos)->markDirty();
        err = (*pos)->saveToStore(dSUID.getString().c_str(), true); // multiple instances allowed, it's a *list*!
        if (Error::notOK(err)) LOG(LOG_ERR,"Error saving optimizer entry: %s", err->text());
      }
    }
  }
  return err;
}




ErrorPtr Vdc::load()
{
  ErrorPtr err;
  // load the vdc settings (collecting phase is already over by now)
  err = loadFromStore(dSUID.getString().c_str());
  if (Error::notOK(err)) ALOG(LOG_ERR,"Error loading settings: %s", err->text());
  #if ENABLE_SETTINGS_FROM_FILES
  loadSettingsFromFiles();
  #endif
  // load the optimizer cache
  err = loadOptimizerCache();
  if (Error::notOK(err)) ALOG(LOG_ERR,"Error loading optimizer cache: %s", err->text());
  // announce groups and scenes used by optimizer
  for (OptimizerEntryList::iterator pos = optimizerCache.begin(); pos!=optimizerCache.end(); ++pos) {
    if (!(*pos)->nativeActionId.empty()) {
      ErrorPtr announceErr = announceNativeAction((*pos)->nativeActionId);
      if (Error::notOK(announceErr)) {
        ALOG(LOG_WARNING,"Native action '%s' is no longer valid - removed (%s)", (*pos)->nativeActionId.c_str(), err->text());
        (*pos)->nativeActionId = ""; // erase it, repeated use of that entry will re-create a native action later
      }
    }
  }
  return ErrorPtr();
}


ErrorPtr Vdc::save()
{
  ErrorPtr err;
  // save the vdc settings
  err = saveToStore(dSUID.getString().c_str(), false); // only one record per vdc
  // load the optimizer cache
  err = saveOptimizerCache();
  return ErrorPtr();
}


ErrorPtr Vdc::forget()
{
  // delete the vdc settings
  deleteFromStore();
  clearOptimizerCache();
  return ErrorPtr();
}


#if ENABLE_SETTINGS_FROM_FILES

void Vdc::loadSettingsFromFiles()
{
  string dir = getVdcHost().getConfigDir();
  const int numLevels = 2;
  string levelids[numLevels];
  // Level strategy: most specialized will be active, unless lower levels specify explicit override
  // - Baselines are hardcoded defaults plus settings (already) loaded from persistent store
  // - Level 0 are settings related to the device instance (dSUID)
  // - Level 1 are settings related to the vDC (vdcClassIdentifier())
  levelids[0] = getDsUid().getString();
  levelids[1] = vdcClassIdentifier();
  for(int i=0; i<numLevels; ++i) {
    // try to open config file
    string fn = dir+"vdcsettings_"+levelids[i]+".csv";
    // if vdc has already stored properties, only explicitly marked properties will be applied
    if (loadSettingsFromFile(fn.c_str(), rowid!=0)) markClean();
  }
}

#endif // ENABLE_SETTINGS_FROM_FILES


// MARK: - property access

static char vdc_key;
static char devices_container_key;
static char capabilities_container_key;
static char device_key;

enum {
  defaultzone_key,
  capabilities_key,
  implementationId_key,
  devices_key,
  instancenumber_key,
  rescanModes_key,
  optimizerMode_key,
  minDevicesForOptimizing_key,
  minCallsBeforeOptimizing_key,
  maxOptimizerScenes_key,
  maxOptimizerGroups_key,
  hideWhenEmpty_key,
  numVdcProperties
};


enum {
  capability_metering_key,
  capability_dynamicdefinitions_key,
  capability_identification_key,
  numVdcCapabilities
};



int Vdc::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aParentDescriptor->hasObjectKey(devices_container_key)) {
    return (int)devices.size();
  }
  else if (aParentDescriptor->hasObjectKey(capabilities_container_key)) {
    return numVdcCapabilities;
  }
  return inherited::numProps(aDomain, aParentDescriptor)+numVdcProperties;
}


PropertyDescriptorPtr Vdc::getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyAccessMode aMode, PropertyDescriptorPtr aParentDescriptor)
{
  if (aParentDescriptor->hasObjectKey(devices_container_key)) {
    // accessing one of the devices by numeric index
    return getDescriptorByNumericName(
      aPropMatch, aStartIndex, aDomain, aParentDescriptor,
      OKEY(device_key)
    );
  }
  // None of the containers within Device - let base class handle vdc-Level properties
  return inherited::getDescriptorByName(aPropMatch, aStartIndex, aDomain, aMode, aParentDescriptor);
}


PropertyContainerPtr Vdc::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->isArrayContainer()) {
    // local container
    return PropertyContainerPtr(this); // handle myself
  }
  else if (aPropertyDescriptor->hasObjectKey(device_key)) {
    // - get device
    PropertyContainerPtr container = devices[aPropertyDescriptor->fieldKey()];
    return container;
  }
  // unknown here
  return NULL;
}



// note: is only called when getDescriptorByName does not resolve the name
PropertyDescriptorPtr Vdc::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aParentDescriptor->hasObjectKey(capabilities_container_key)) {
    // capabilities level
    static const PropertyDescription capability_props[numVdcCapabilities] = {
      { "metering", apivalue_bool, capability_metering_key, OKEY(capabilities_container_key) },
      { "dynamicDefinitions", apivalue_bool, capability_dynamicdefinitions_key, OKEY(capabilities_container_key) },
      { "identification", apivalue_bool, capability_identification_key, OKEY(capabilities_container_key) },
    };
    // simple, all on this level
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&capability_props[aPropIndex], aParentDescriptor));
  }
  else {
    // vdc level
    static const PropertyDescription properties[numVdcProperties] = {
      { "zoneID", apivalue_uint64, defaultzone_key, OKEY(vdc_key) },
      { "capabilities", apivalue_object+propflag_container, capabilities_key, OKEY(capabilities_container_key) },
      { "implementationId", apivalue_string, implementationId_key, OKEY(vdc_key) },
      { "x-p44-devices", apivalue_object+propflag_container+propflag_nowildcard, devices_key, OKEY(devices_container_key) },
      { "x-p44-instanceNo", apivalue_uint64, instancenumber_key, OKEY(vdc_key) },
      { "x-p44-rescanModes", apivalue_uint64, rescanModes_key, OKEY(vdc_key) },
      { "x-p44-optimizerMode", apivalue_uint64, optimizerMode_key, OKEY(vdc_key) },
      { "x-p44-minDevicesForOptimizing", apivalue_uint64, minDevicesForOptimizing_key, OKEY(vdc_key) },
      { "x-p44-minCallsBeforeOptimizing", apivalue_uint64, minCallsBeforeOptimizing_key, OKEY(vdc_key) },
      { "x-p44-maxOptimizerScenes", apivalue_uint64, maxOptimizerScenes_key, OKEY(vdc_key) },
      { "x-p44-maxOptimizerGroups", apivalue_uint64, maxOptimizerGroups_key, OKEY(vdc_key) },
      { "x-p44-hideWhenEmpty", apivalue_bool, hideWhenEmpty_key, OKEY(vdc_key) }
    };
    int n = inherited::numProps(aDomain, aParentDescriptor);
    if (aPropIndex<n)
      return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor); // base class' property
    aPropIndex -= n; // rebase to 0 for my own first property
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
  }
}



bool Vdc::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(vdc_key)) {
    // vdc level properties
    if (aMode==access_read) {
      // read
      switch (aPropertyDescriptor->fieldKey()) {
        case defaultzone_key:
          aPropValue->setUint16Value(defaultZoneID);
          return true;
        case implementationId_key:
          aPropValue->setStringValue(vdcClassIdentifier());
          return true;
        case instancenumber_key:
          aPropValue->setUint32Value(getInstanceNumber());
          return true;
        case rescanModes_key:
          aPropValue->setUint32Value(getRescanModes());
          return true;
        case optimizerMode_key:
          if (optimizerMode==opt_unavailable) return false; // do not show the property at all
          aPropValue->setUint32Value(optimizerMode);
          return true;
        case minCallsBeforeOptimizing_key:
          if (optimizerMode==opt_unavailable) return false; // do not show the property at all
          aPropValue->setUint32Value(minCallsBeforeOptimizing);
          return true;
        case minDevicesForOptimizing_key:
          if (optimizerMode==opt_unavailable) return false; // do not show the property at all
          aPropValue->setUint32Value(minDevicesForOptimizing);
          return true;
        case maxOptimizerScenes_key:
          if (optimizerMode==opt_unavailable) return false; // do not show the property at all
          aPropValue->setUint32Value(maxOptimizerScenes);
          return true;
        case maxOptimizerGroups_key:
          if (optimizerMode==opt_unavailable) return false; // do not show the property at all
          aPropValue->setUint32Value(maxOptimizerGroups);
          return true;
        case hideWhenEmpty_key:
          aPropValue->setBoolValue(getVdcFlag(vdcflag_hidewhenempty));
          return true;
      }
    }
    else {
      // write
      switch (aPropertyDescriptor->fieldKey()) {
        case defaultzone_key:
          setPVar(defaultZoneID, (DsZoneID)aPropValue->int32Value());
          return true;
        case optimizerMode_key: {
          if (optimizerMode==opt_unavailable) return false; // property not writable
          OptimizerMode m = (OptimizerMode)aPropValue->int32Value();
          if (m==opt_reset) {
            clearOptimizerCache();
            return true;
          }
          else if (m>opt_unavailable && m<opt_reset) {
            setPVar(optimizerMode, m);
            return true;
          }
          else {
            return false;
          }
        }
        case minCallsBeforeOptimizing_key:
          if (optimizerMode==opt_unavailable) return false; // property not writable
          setPVar(minCallsBeforeOptimizing, aPropValue->int32Value());
          return true;
        case minDevicesForOptimizing_key:
          if (optimizerMode==opt_unavailable) return false; // property not writable
          setPVar(minDevicesForOptimizing, aPropValue->int32Value());
          return true;
        case maxOptimizerScenes_key:
          if (optimizerMode==opt_unavailable) return false; // property not writable
          setPVar(maxOptimizerScenes, aPropValue->int32Value());
          return true;
        case maxOptimizerGroups_key:
          if (optimizerMode==opt_unavailable) return false; // property not writable
          setPVar(maxOptimizerGroups, aPropValue->int32Value());
          return true;
        case hideWhenEmpty_key:
          setVdcFlag(vdcflag_hidewhenempty, aPropValue->boolValue());
          return true;
      }
    }
  }
  else if (aPropertyDescriptor->hasObjectKey(capabilities_container_key)) {
    // capabilities
    if (aMode==access_read) {
      switch (aPropertyDescriptor->fieldKey()) {
        case capability_metering_key: aPropValue->setBoolValue(false); return true; // TODO: implement actual metering flag
        case capability_dynamicdefinitions_key: aPropValue->setBoolValue(dynamicDefinitions()); return true;
        case capability_identification_key: aPropValue->setBoolValue(canIdentifyToUser()); return true;
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


// MARK: - persistence implementation

// SQLIte3 table name to store these parameters to
const char *Vdc::tableName()
{
  return "VdcSettings";
}


// data field definitions

static const size_t numFields = 8;

size_t Vdc::numFieldDefs()
{
  return inheritedParams::numFieldDefs()+numFields;
}


const FieldDefinition *Vdc::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numFields] = {
    { "vdcFlags", SQLITE_INTEGER },
    { "vdcName", SQLITE_TEXT },
    { "defaultZoneID", SQLITE_INTEGER },
    { "optimizerMode", SQLITE_INTEGER },
    { "minCallsBeforeOptimizing", SQLITE_INTEGER },
    { "minDevicesForOptimizing", SQLITE_INTEGER },
    { "maxOptimizerScenes", SQLITE_INTEGER },
    { "maxOptimizerGroups", SQLITE_INTEGER }
  };
  if (aIndex<inheritedParams::numFieldDefs())
    return inheritedParams::getFieldDef(aIndex);
  aIndex -= inheritedParams::numFieldDefs();
  if (aIndex<numFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void Vdc::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inheritedParams::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get the field values
  vdcFlags = aRow->get<int>(aIndex++);
  setName(nonNullCStr(aRow->get<const char *>(aIndex++)));
  defaultZoneID = aRow->getCasted<DsZoneID, int>(aIndex++);
  // read optimizer mode only for vdcs that support it
  if (optimizerMode!=opt_unavailable) {
    optimizerMode = aRow->getCastedWithDefault<OptimizerMode, int>(aIndex, optimizerMode);
  }
  aIndex++;
  aRow->getIfNotNull(aIndex++, minCallsBeforeOptimizing);
  aRow->getIfNotNull(aIndex++, minDevicesForOptimizing);
  aRow->getIfNotNull(aIndex++, maxOptimizerScenes);
  aRow->getIfNotNull(aIndex++, maxOptimizerGroups);
}


// bind values to passed statement
void Vdc::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inheritedParams::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  vdcFlags |= vdcflag_flagsinitialized; // this save now contains real content for vdcFlags, which must not be overriden by defaults at next initialisation
  // bind the fields
  aStatement.bind(aIndex++, (int)vdcFlags);
  aStatement.bind(aIndex++, getAssignedName().c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, defaultZoneID);
  aStatement.bind(aIndex++, optimizerMode);
  aStatement.bind(aIndex++, minCallsBeforeOptimizing);
  aStatement.bind(aIndex++, minDevicesForOptimizing);
  aStatement.bind(aIndex++, maxOptimizerScenes);
  aStatement.bind(aIndex++, maxOptimizerGroups);
}

// MARK: - description/shortDesc/status


string Vdc::description()
{
  string d = string_format(
    "%s #%d: %s (%ld devices), status %s",
    vdcClassIdentifier(),
    getInstanceNumber(),
    shortDesc().c_str(),
    (long)devices.size(),
    Error::isOK(vdcErr) ? "OK" : vdcErr->text()
  );
  return d;
}


int Vdc::opStateLevel()
{
  return Error::isOK(vdcErr) ? (collecting ? 50 : 100) : 0;
}


string Vdc::getOpStateText()
{
  if (Error::notOK(vdcErr)) {
    return string_format("Error: %s", vdcErr->text());
  }
  else if (collecting) {
    return "Scanning...";
  }
  return inherited::getOpStateText();
}



// MARK: - OptimizerEntry


OptimizerEntry::OptimizerEntry() :
  inheritedParams(VdcHost::sharedVdcHost()->getDsParamStore()),
  type(ntfy_undefined),
  numberOfDevices(0),
  contentId(0),
  contentsHash(0),
  lastNativeChange(Never),
  numCalls(0),
  lastUse(Never)
{
}


OptimizerEntry::~OptimizerEntry()
{
}


const char *OptimizerEntry::tableName()
{
  return "VdcOptimizer";
}


// data field definitions

static const size_t numOptimizerEntryFields = 9;

size_t OptimizerEntry::numFieldDefs()
{
  return inheritedParams::numFieldDefs()+numOptimizerEntryFields;
}


const FieldDefinition *OptimizerEntry::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numOptimizerEntryFields] = {
    { "type", SQLITE_INTEGER },
    { "numberOfDevices", SQLITE_INTEGER },
    { "affectedDevicesHash", SQLITE_TEXT },
    { "contentId", SQLITE_INTEGER },
    { "contentsHash", SQLITE_INTEGER },
    { "nativeActionId", SQLITE_TEXT },
    { "numCalls", SQLITE_INTEGER },
    { "lastUse", SQLITE_INTEGER },
    { "lastNativeChange", SQLITE_INTEGER }
  };
  if (aIndex<inheritedParams::numFieldDefs())
    return inheritedParams::getFieldDef(aIndex);
  aIndex -= inheritedParams::numFieldDefs();
  if (aIndex<numOptimizerEntryFields)
    return &dataDefs[aIndex];
  return NULL;
}


void OptimizerEntry::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inheritedParams::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get fields
  type = aRow->getCastedWithDefault<NotificationType, int>(aIndex++, ntfy_undefined);
  numberOfDevices = aRow->get<int>(aIndex++);
  affectedDevicesHash = hexToBinaryString(nonNullCStr(aRow->get<const char *>(aIndex++)), false);
  contentId = aRow->get<int>(aIndex++);
  contentsHash = aRow->getCasted<uint64_t, long long>(aIndex++);
  nativeActionId = nonNullCStr(aRow->get<const char *>(aIndex++));
  numCalls = aRow->getCasted<long,int>(aIndex++);
  // timestamps are stored as unix timestamps
  lastUse = MainLoop::unixTimeToMainLoopTime(aRow->getCasted<uint64_t, long long>(aIndex++));
  lastNativeChange = MainLoop::unixTimeToMainLoopTime(aRow->getCasted<uint64_t, long long>(aIndex++));
  // sanity checks, cannot be in the future
  MLMicroSeconds now = MainLoop::now();
  if (lastUse>now) lastUse = Never; // default to not being used recently at all
  if (lastNativeChange>now) lastNativeChange = now; // default to being a recently set-up entry to avoid deletion
}


void OptimizerEntry::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inheritedParams::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind fields
  aStatement.bind(aIndex++, type);
  aStatement.bind(aIndex++, numberOfDevices);
  aStatement.bind(aIndex++, binaryToHexString(affectedDevicesHash).c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, contentId);
  aStatement.bind(aIndex++, (long long)contentsHash);
  aStatement.bind(aIndex++, nativeActionId.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, (int)numCalls);
  // timestamps are stored as unix timestamps
  aStatement.bind(aIndex++, (long long)MainLoop::mainLoopTimeToUnixTime(lastUse));
  aStatement.bind(aIndex++, (long long)MainLoop::mainLoopTimeToUnixTime(lastNativeChange));
}


#define CALL_COUNT_FADE_TIMEOUT (5*24*Hour) // call count is reduced to 0 over 5 days

long OptimizerEntry::timeWeightedCallCount()
{
  if (lastUse!=Never) {
    MLMicroSeconds age = MainLoop::now()-lastUse;
    if (age<0) return numCalls>0 ? numCalls : 0; // in case of jumping time, to avoid negative numCalls
    if (age>CALL_COUNT_FADE_TIMEOUT) return 0; // completely faded away already
    return numCalls-(long)((uint64_t)numCalls*age/CALL_COUNT_FADE_TIMEOUT);
  }
  else {
    return numCalls;
  }
}



