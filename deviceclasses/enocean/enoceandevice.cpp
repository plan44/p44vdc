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

#include "enoceandevice.hpp"
#include "enoceanvdc.hpp"

#if ENABLE_ENOCEAN

#include "buttonbehaviour.hpp"
#include "sensorbehaviour.hpp"
#include "outputbehaviour.hpp"

#include "enoceanrps.hpp"
#include "enocean1bs.hpp"
#include "enocean4bs.hpp"
#include "enoceanvld.hpp"
#include "enoceanremotecontrol.hpp"

using namespace p44;


// MARK: - EnoceanChannelHandler

EnoceanChannelHandler::EnoceanChannelHandler(EnoceanDevice &aDevice) :
  device(aDevice),
  dsChannelIndex(0),
  batPercentage(100)
{
}


int EnoceanChannelHandler::opStateLevel()
{
  if (!isAlive()) return 0; // completely offline, operation not possible
  if (batPercentage<=LOW_BAT_PERCENTAGE) return batPercentage; // low battery, operation critical
  return 100;
}

string EnoceanChannelHandler::getOpStateText()
{
  if (batPercentage<=LOW_BAT_PERCENTAGE) return "low battery";
  return "";
}



// MARK: - EnoceanDevice

#define INVALID_RSSI (-999)

EnoceanDevice::EnoceanDevice(EnoceanVdc *aVdcP) :
  Device(aVdcP),
  eeProfile(eep_profile_unknown),
  eeManufacturer(manufacturer_unknown),
  alwaysUpdateable(false),
  pendingDeviceUpdate(false),
  updateAtEveryReceive(false),
  subDevice(0)
{
  eeFunctionDesc = "device"; // generic description is "device"
  iconBaseName = "enocean";
  groupColoredIcon = true;
  lastPacketTime = MainLoop::now(); // consider packet received at time of creation (to avoid devices starting inactive)
  lastRSSI = INVALID_RSSI; // not valid
  lastRepeaterCount = 0; // dummy
}


bool EnoceanDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Nothing to do to identify for now
  return true; // simple identification, callback will not be called
}



EnoceanVdc &EnoceanDevice::getEnoceanVdc()
{
  return *(static_cast<EnoceanVdc *>(vdcP));
}


EnoceanAddress EnoceanDevice::getAddress()
{
  return enoceanAddress;
}


EnoceanSubDevice EnoceanDevice::getSubDevice()
{
  return subDevice;
}


void EnoceanDevice::setAddressingInfo(EnoceanAddress aAddress, EnoceanSubDevice aSubDevice)
{
  enoceanAddress = aAddress;
  subDevice = aSubDevice;
  deriveDsUid();
}


void EnoceanDevice::setEEPInfo(EnoceanProfile aEEProfile, EnoceanManufacturer aEEManufacturer)
{
  eeProfile = aEEProfile;
  eeManufacturer = aEEManufacturer;
}


EnoceanProfile EnoceanDevice::getEEProfile()
{
  return eeProfile;
}


EnoceanManufacturer EnoceanDevice::getEEManufacturer()
{
  return eeManufacturer;
}


void EnoceanDevice::deriveDsUid()
{
  // UUID in EnOcean name space
  //   name = xxxxxxxx (x=8 digit enocean hex UPPERCASE address)
  DsUid enOceanNamespace(DSUID_ENOCEAN_NAMESPACE_UUID);
  string s = string_format("%08X", getAddress()); // hashed part of dSUID comes from unique EnOcean address
  dSUID.setNameInSpace(s, enOceanNamespace);
  dSUID.setSubdeviceIndex(getSubDevice()); // subdevice index is represented in the dSUID subdevice index byte
}


string EnoceanDevice::hardwareGUID()
{
  return string_format("enoceanaddress:%08X", getAddress());
}


string EnoceanDevice::hardwareModelGUID()
{
  return string_format("enoceaneep:%06X", EEP_PURE(getEEProfile()));
}


string EnoceanDevice::modelName()
{
  const char *mn = EnoceanComm::manufacturerName(eeManufacturer);
  return string_format("%s%sEnOcean %s (%02X-%02X-%02X)", mn ? mn : "", mn ? " " : "", eeFunctionDesc.c_str(), EEP_RORG(eeProfile), EEP_FUNC(eeProfile), EEP_TYPE(eeProfile));
}


string EnoceanDevice::vendorId()
{
  const char *mn = EnoceanComm::manufacturerName(eeManufacturer);
  return string_format("enoceanvendor:%03X%s%s", eeManufacturer, mn ? ":" : "", mn ? mn : "");
}


string EnoceanDevice::vendorName()
{
  const char *mn = EnoceanComm::manufacturerName(eeManufacturer);
  return mn ? mn : "";
}



bool EnoceanDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  bool iconFound = false;
  if (iconBaseName) {
    if (groupColoredIcon)
      iconFound = getClassColoredIcon(iconBaseName, getDominantColorClass(), aIcon, aWithData, aResolutionPrefix);
    else
      iconFound = getIcon(iconBaseName, aIcon, aWithData, aResolutionPrefix);
  }
  if (iconFound)
    return true;
  // failed
  return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


void EnoceanDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  // clear learn-in data from DB
  if(getEnoceanVdc().db.executef("DELETE FROM knownDevices WHERE enoceanAddress=%d AND subdevice=%d", getAddress(), getSubDevice())!=SQLITE_OK) {
    ALOG(LOG_ERR, "Error deleting device: %s", getEnoceanVdc().db.error()->description().c_str());
  }
  #if ENABLE_ENOCEAN_SECURE
  // clear security info if no subdevices are left
  getEnoceanVdc().removeUnusedSecurity(*this);
  #endif
  // disconnection is immediate, so we can call inherited right now
  inherited::disconnect(aForgetParams, aDisconnectResultHandler);
}



void EnoceanDevice::addChannelHandler(EnoceanChannelHandlerPtr aChannelHandler)
{
  // create channel number
  aChannelHandler->channel = channels.size();
  // add to my local list
  channels.push_back(aChannelHandler);
  // register behaviour of the channel (if it has a default behaviour at all) with the device
  if (aChannelHandler->behaviour) {
    addBehaviour(aChannelHandler->behaviour);
  }
}




EnoceanChannelHandlerPtr EnoceanDevice::channelForBehaviour(const DsBehaviour *aBehaviourP)
{
  EnoceanChannelHandlerPtr handler;
  for (EnoceanChannelHandlerVector::iterator pos = channels.begin(); pos!=channels.end(); ++pos) {
    if ((*pos)->behaviour.get()==static_cast<const DsBehaviour *>(aBehaviourP)) {
      handler = *pos;
      break;
    }
  }
  return handler;
}


void EnoceanDevice::sendCommand(Esp3PacketPtr aCommandPacket, ESPPacketCB aResponsePacketCB)
{
  aCommandPacket->finalize();
  ALOG(LOG_INFO, "Sending EnOcean Packet:\n%s", aCommandPacket->description().c_str());
  getEnoceanVdc().enoceanComm.sendCommand(aCommandPacket, aResponsePacketCB);
}



void EnoceanDevice::needOutgoingUpdate()
{
  // anyway, we need an update
  pendingDeviceUpdate = true;
  // send it right away when possible (line powered devices only)
  if (alwaysUpdateable) {
    sendOutgoingUpdate();
  }
  else {
    ALOG(LOG_NOTICE, "flagged output update pending -> outgoing EnOcean package will be sent later");
  }
}


void EnoceanDevice::sendOutgoingUpdate()
{
  if (pendingDeviceUpdate) {
    // clear flag now, so handlers can trigger yet another update in collectOutgoingMessageData() if needed (e.g. heating valve service sequence)
    pendingDeviceUpdate = false; // done
    // collect data from all channels to compose an outgoing message
    Esp3PacketPtr outgoingEsp3Packet;
    for (EnoceanChannelHandlerVector::iterator pos = channels.begin(); pos!=channels.end(); ++pos) {
      (*pos)->collectOutgoingMessageData(outgoingEsp3Packet);
    }
    if (outgoingEsp3Packet) {
      // set destination
      outgoingEsp3Packet->setRadioDestination(enoceanAddress); // the target is the device I manage
      // send it
      sendCommand(outgoingEsp3Packet, NULL);
    }
  }
}


void EnoceanDevice::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  // trigger updating all device outputs
  for (int i=0; i<numChannels(); i++) {
    if (getChannelByIndex(i, true)) {
      // channel needs update
      pendingDeviceUpdate = true;
      break; // no more checking needed, need device level update anyway
    }
  }
  if (pendingDeviceUpdate) {
    // we need to apply data
    needOutgoingUpdate();
  }
  inherited::applyChannelValues(aDoneCB, aForDimming);
}


void EnoceanDevice::updateRadioMetrics(Esp3PacketPtr aEsp3PacketPtr)
{
  if (aEsp3PacketPtr) {
    updatePresenceState(true); // when we get a telegram, we know device is present now
    lastPacketTime = MainLoop::now();
    lastRSSI = aEsp3PacketPtr->radioDBm();
    lastRepeaterCount = aEsp3PacketPtr->radioRepeaterCount();
  }
}


void EnoceanDevice::handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr)
{
  ALOG(LOG_INFO, "now starts processing EnOcean packet:\n%s", aEsp3PacketPtr->description().c_str());
  updateRadioMetrics(aEsp3PacketPtr);
  // pass to every channel
  for (EnoceanChannelHandlerVector::iterator pos = channels.begin(); pos!=channels.end(); ++pos) {
    (*pos)->handleRadioPacket(aEsp3PacketPtr);
  }
  // if device cannot be updated whenever output value change is requested, send updates after receiving a message
  if (pendingDeviceUpdate || updateAtEveryReceive) {
    // send updates, if any
    pendingDeviceUpdate = true; // set it in case of updateAtEveryReceive (so message goes out even if no changes pending)
    ALOG(LOG_NOTICE, "pending output update is now sent to device");
    sendOutgoingUpdate();
  }
}


bool EnoceanDevice::isAlive()
{
  bool alive = true;
  for (EnoceanChannelHandlerVector::iterator pos = channels.begin(); pos!=channels.end(); ++pos) {
    if (!(*pos)->isAlive()) {
      alive = false; // one channel not alive -> device not present
      break;
    }
  }
  return alive;
}


void EnoceanDevice::checkPresence(PresenceCB aPresenceResultHandler)
{
  aPresenceResultHandler(isAlive());
}


#define BEST_RSSI (-65) // opState should be 100% above this
#define WORST_RSSI (-95)  // opState should be 1% below this

int EnoceanDevice::opStateLevel()
{
  int opState = -1;
  if (lastRSSI>INVALID_RSSI) {
    // first judge from last RSSI
    opState = 1+(lastRSSI-WORST_RSSI)*99/(BEST_RSSI-WORST_RSSI); // 1..100 range
    if (opState<1) opState = 1;
    else if (opState>100) opState = 100;
    // also check opstate from channels
    for (EnoceanChannelHandlerVector::iterator pos = channels.begin(); pos!=channels.end(); ++pos) {
      int chOpState = (*pos)->opStateLevel();
      if (chOpState<opState) opState = chOpState; // lowest channel state determines overall state
    }
  }
  return opState;
}


string EnoceanDevice::getOpStateText()
{
  string t;
  if (!isAlive()) {
    t += "timeout, ";
  }
  if (lastRSSI>INVALID_RSSI) {
    string_format_append(t, "%ddBm (", lastRSSI);
    if (lastRepeaterCount>0) {
      string_format_append(t, "%d Rep., ", lastRepeaterCount);
    }
    format_duration_append(t, (MainLoop::now()-lastPacketTime)/Second, 2);
    t += " ago)";
  }
  else {
    t += "unseen";
  }
  // append info from enocean handlers
  for (EnoceanChannelHandlerVector::iterator pos = channels.begin(); pos!=channels.end(); ++pos) {
    string ht = (*pos)->getOpStateText();
    if (!ht.empty()) {
      t += ", " + ht;
    }
  }
  return t;
}



string EnoceanDevice::description()
{
  string s = inherited::description();
  #if ENABLE_ENOCEAN_SECURE
  if (secureDevice()) {
    string_format_append(s,
      "\n- With secured communication:%s%s%s%s",
      securityInfo->securityLevelFormat & 0xC0 ? " RLC" : "",
      securityInfo->securityLevelFormat & 0x20 ? "-TX" : "",
      securityInfo->securityLevelFormat & 0x18 ? " MAC" : "",
      securityInfo->securityLevelFormat & 0x07 ? " DATA_ENC" : ""
    );
  }
  #endif
  string_format_append(s, "\n- Enocean Address = 0x%08X, subDevice=%d", enoceanAddress, subDevice);
  const char *mn = EnoceanComm::manufacturerName(eeManufacturer);
  string_format_append(s,
    "\n- %s, EEP RORG/FUNC/TYPE: %02X %02X %02X, Manufacturer: %s (%03X), Profile variant: %02X",
    eeFunctionDesc.c_str(),
    EEP_RORG(eeProfile),
    EEP_FUNC(eeProfile),
    EEP_TYPE(eeProfile),
    mn ? mn : "<unknown>",
    eeManufacturer,
    EEP_VARIANT(eeProfile)
  );
  // show channels
  for (EnoceanChannelHandlerVector::iterator pos = channels.begin(); pos!=channels.end(); ++pos) {
    string_format_append(s, "\n- EnOcean device channel #%d: %s", (*pos)->channel, (*pos)->shortDesc().c_str());
  }
  return s;
}


// MARK: - profile variants


EnoceanProfile EnoceanDevice::expandEEPWildcard(EnoceanProfile aEEPWildcard)
{
  EnoceanProfile myEEP = getEEProfile();
  EnoceanProfile r = 0;
  for (int i=0; i<4; i++) {
    if (((aEEPWildcard>>i*8)&0xFF)==0xFF) {
      r |= myEEP&(0xFF<<i*8); // input has wildcard, use byte from my own EEP
    }
    else {
      r |= aEEPWildcard&(0xFF<<i*8); // use original input byte
    }
  }
  return r;
}


void EnoceanDevice::getDeviceConfigurations(DeviceConfigurationsVector &aConfigurations, StatusCB aStatusCB)
{
  // check if current profile is one of the interchangeable ones
  const ProfileVariantEntry *currentVariant = profileVariantsTable();
  bool anyVariants = false;
  while (currentVariant && currentVariant->profileGroup!=0) {
    // look for current EEP in the list of variants (which might contain wildcards, i.e. 0xFF bytes meaning "SAME AS MYSELF")
    if (getEEProfile()==expandEEPWildcard(currentVariant->eep)) {
      // create string from all other variants (same profileGroup), if any
      const ProfileVariantEntry *variant = profileVariantsTable();
      while (variant->profileGroup!=0) {
        if (variant->profileGroup==currentVariant->profileGroup) {
          if (expandEEPWildcard(variant->eep)!=getEEProfile()) anyVariants = true; // another variant than just myself
          string id;
          if (variant->configId)
            id = variant->configId; // has well-known configuration id
          else
            id = string_format("eep_%08X", expandEEPWildcard(variant->eep)); // id generated from EEP
          aConfigurations.push_back(DeviceConfigurationDescriptorPtr(new DeviceConfigurationDescriptor(id, variant->description)));
        }
        variant++;
      }
      break;
    }
    currentVariant++;
  }
  if (!anyVariants) aConfigurations.clear(); // prevent single option to show at all
  if (aStatusCB) aStatusCB(ErrorPtr());
}


string EnoceanDevice::getDeviceConfigurationId()
{
  const ProfileVariantEntry *currentVariant = profileVariantsTable();
  while (currentVariant && currentVariant->profileGroup!=0) {
    if (currentVariant->configId && getEEProfile()==expandEEPWildcard(currentVariant->eep)) {
      return currentVariant->configId; // has a well-known name, return that
    }
    currentVariant++;
  }
  // return a id generated from EEP
  return  string_format("eep_%08X", getEEProfile());
}



ErrorPtr EnoceanDevice::switchConfiguration(const string aConfigurationId)
{
  EnoceanProfile newProfile = 0;
  unsigned long np;
  if (sscanf(aConfigurationId.c_str(), "eep_%lx", &np)==1) {
    newProfile = (EnoceanProfile)np;
  }
  // - find my profileGroup
  const ProfileVariantEntry *currentVariant = profileVariantsTable();
  while (currentVariant && currentVariant->profileGroup!=0) {
    if (getEEProfile()==expandEEPWildcard(currentVariant->eep)) {
      // this is my profile group, now check if requested profile is in my profile group as well
      const ProfileVariantEntry *variant = profileVariantsTable();
      while (variant && variant->profileGroup!=0) {
        if (
          (variant->profileGroup==currentVariant->profileGroup) &&
          ((newProfile!=0 && newProfile==expandEEPWildcard(variant->eep)) || (newProfile==0 && variant->configId && aConfigurationId==variant->configId))
        ) {
          // prevent switching if new profile is same as current one
          if (expandEEPWildcard(variant->eep)==expandEEPWildcard(currentVariant->eep)) return ErrorPtr(); // we already have that profile -> NOP
          // requested profile is in my group, change now
          switchProfiles(*currentVariant, *variant); // will delete this device, so return immediately afterwards
          return ErrorPtr(); // changed profile
        }
        variant++;
      }
    }
    currentVariant++;
  }
  return inherited::switchConfiguration(aConfigurationId); // unknown profile at this level
}


void EnoceanDevice::switchProfiles(const ProfileVariantEntry &aFromVariant, const ProfileVariantEntry &aToVariant)
{
  // make sure object is retained locally
  EnoceanDevicePtr keepMeAlive(this); // make sure this object lives until routine terminates
  // determine range of subdevices affected by this profile switch
  // - larger of both counts, 0 means all indices affected
  EnoceanSubDevice rangesize = 0;
  EnoceanSubDevice rangestart = 0;
  if (aFromVariant.subDeviceIndices!=0 && aToVariant.subDeviceIndices==aFromVariant.subDeviceIndices) {
    // old and new profile affects same subrange of all subdevice -> we can switch these subdevices only -> restrict range
    rangesize = aToVariant.subDeviceIndices;
    // subDeviceIndices range is required to start at an even multiple of rangesize
    rangestart = getSubDevice()/rangesize*rangesize;
  }
  // have devices related to current profile deleted, including settings
  // Note: this removes myself from container, and deletes the config (which is valid only for the previous profile, i.e. a different type of device)
  getEnoceanVdc().unpairDevicesByAddressAndEEP(getAddress(), getEEProfile(), true, rangestart, rangesize);
  // - create new ones, with same address and manufacturer, but new profile
  EnoceanSubDevice subDeviceIndex = rangestart;
  while (rangesize==0 || subDeviceIndex<rangestart+rangesize) {
    // create devices until done
    EnoceanDevicePtr newDev = newDevice(
      &getEnoceanVdc(),
      getAddress(), // same address as current device
      subDeviceIndex, // index to create a device for
      expandEEPWildcard(aToVariant.eep), // the new EEP variant
      getEEManufacturer(),
      subDeviceIndex==0 // allow sending teach-in response for first subdevice only
    );
    if (!newDev) {
      // could not create a device for subDeviceIndex
      break; // -> done
    }
    // - keep assigned name and zone for new device(s)
    bool hasNameOrZone = false;
    if (!getAssignedName().empty()) {
      hasNameOrZone = true;
      newDev->initializeName(getAssignedName());
    }
    if (newDev->deviceSettings && getZoneID()!=0) {
      hasNameOrZone = true;
      newDev->deviceSettings->zoneID = getZoneID();
    }
    // - add it to the container
    getEnoceanVdc().addAndRememberDevice(newDev);
    // - make it dirty if we have set zone or name
    if (hasNameOrZone && newDev->deviceSettings) {
      newDev->deviceSettings->markDirty(); // make sure name and/or zone are saved permanently
    }
    // Note: subDeviceIndex is incremented according to device's index space requirements by newDevice() implementation
  }
}



// MARK: - property access


enum {
  packetage_key,
  rssi_key,
  repeaterCount_key,
  numProperties
};

static char enoceanDevice_key;


int EnoceanDevice::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // Note: only add my own count when accessing root level properties!!
  if (aParentDescriptor->isRootOfObject()) {
    // Accessing properties at the Device (root) level, add mine
    return inherited::numProps(aDomain, aParentDescriptor)+numProperties;
  }
  // just return base class' count
  return inherited::numProps(aDomain, aParentDescriptor);
}


PropertyDescriptorPtr EnoceanDevice::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numProperties] = {
    { "x-p44-packetAge", apivalue_double, packetage_key, OKEY(enoceanDevice_key) },
    { "x-p44-rssi", apivalue_int64, rssi_key, OKEY(enoceanDevice_key) },
    { "x-p44-repeaterCount", apivalue_int64, repeaterCount_key, OKEY(enoceanDevice_key) },
  };
  if (aParentDescriptor->isRootOfObject()) {
    // root level - accessing properties on the Device level
    int n = inherited::numProps(aDomain, aParentDescriptor);
    if (aPropIndex<n)
      return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor); // base class' property
    aPropIndex -= n; // rebase to 0 for my own first property
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
  }
  else {
    // other level
    return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor); // base class' property
  }
}


// access to all fields
bool EnoceanDevice::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(enoceanDevice_key)) {
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        case packetage_key:
          // Note lastPacketTime is set to now at startup, so additionally check lastRSSI
          if (lastPacketTime==Never || lastRSSI<=INVALID_RSSI)
            aPropValue->setNull();
          else
            aPropValue->setDoubleValue((double)(MainLoop::now()-lastPacketTime)/Second);
          return true;
        case rssi_key:
          if (lastRSSI<=INVALID_RSSI)
            aPropValue->setNull();
          else
            aPropValue->setInt32Value(lastRSSI);
          return true;
        case repeaterCount_key:
          if (lastRSSI<=INVALID_RSSI)
            aPropValue->setNull();
          else
            aPropValue->setUint8Value(lastRepeaterCount);
          return true;
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


// MARK: - device factory


EnoceanDevicePtr EnoceanDevice::newDevice(
  EnoceanVdc *aVdcP,
  EnoceanAddress aAddress,
  EnoceanSubDevice &aSubDeviceIndex,
  EnoceanProfile aEEProfile, EnoceanManufacturer aEEManufacturer,
  bool aSendTeachInResponse
) {
  EnoceanDevicePtr newDev;
  RadioOrg rorg = EEP_RORG(aEEProfile);
  // dispatch to factory according to RORG
  switch ((int)rorg) {
    case rorg_RPS:
      newDev = EnoceanRPSDevice::newDevice(aVdcP, aAddress, aSubDeviceIndex, aEEProfile, aEEManufacturer, aSendTeachInResponse);
      break;
    case rorg_1BS:
      newDev = Enocean1BSDevice::newDevice(aVdcP, aAddress, aSubDeviceIndex, aEEProfile, aEEManufacturer, aSendTeachInResponse);
      break;
    case rorg_4BS:
      newDev = Enocean4BSDevice::newDevice(aVdcP, aAddress, aSubDeviceIndex, aEEProfile, aEEManufacturer, aSendTeachInResponse);
      break;
    case rorg_VLD:
      newDev = EnoceanVLDDevice::newDevice(aVdcP, aAddress, aSubDeviceIndex, aEEProfile, aEEManufacturer, aSendTeachInResponse);
      break;
    // pseudo RORGs (internal encoding of non-standard devices)
    case PSEUDO_RORG_REMOTECONTROL:
      newDev = EnoceanRemoteControlDevice::newDevice(aVdcP, aAddress, aSubDeviceIndex, aEEProfile, aEEManufacturer, aSendTeachInResponse);
      break;
    default:
      LOG(LOG_WARNING, "EnoceanDevice::newDevice: unknown RORG = 0x%02X", rorg);
      break;
  }
  // return device (or empty if none created)
  return newDev;
}


int EnoceanDevice::createDevicesFromEEP(EnoceanVdc *aVdcP, EnoceanAddress aAddress, EnoceanProfile aProfile, EnoceanManufacturer aManufacturer, EnoceanLearnType aLearnType, Esp3PacketPtr aLearnPacket, EnOceanSecurityPtr aSecurityInfo)
{
  EnoceanSubDevice subDeviceIndex = 0; // start at index zero
  int numDevices = 0; // number of devices
  while (true) {
    // create devices until done
    EnoceanDevicePtr newDev = newDevice(
      aVdcP,
      aAddress,
      subDeviceIndex, // index to create a device for
      aProfile, aManufacturer,
      subDeviceIndex==0 && aLearnType!=learn_smartack // allow sending teach-in response for first subdevice only, and only if not smartAck
    );
    if (!newDev) {
      // could not create a device for subDeviceIndex
      break; // -> done
    }
    #if ENABLE_ENOCEAN_SECURE
    // set the device's security info (if any)
    newDev->setSecurity(aSecurityInfo);
    #endif
    // set new devices' radio metrics from learn telegram
    newDev->updateRadioMetrics(aLearnPacket);
    // created device
    numDevices++;
    // - add it to the container
    aVdcP->addAndRememberDevice(newDev);
    // Note: subDeviceIndex is incremented according to device's index space requirements by newDevice() implementation
  }
  // return number of devices created
  return numDevices;
}

#endif // ENABLE_ENOCEAN

