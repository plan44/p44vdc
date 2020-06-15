//
//  Copyright (c) 2016-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "eldatdevice.hpp"
#include "eldatvdc.hpp"

#if ENABLE_ELDAT

#include "buttonbehaviour.hpp"
#include "binaryinputbehaviour.hpp"
#include "sensorbehaviour.hpp"
#include "outputbehaviour.hpp"
#include "lightbehaviour.hpp"


using namespace p44;


// MARK: - EldatDevice

#define INVALID_RSSI (-999)

EldatDevice::EldatDevice(EldatVdc *aVdcP, EldatDeviceType aDeviceType) :
  Device(aVdcP),
  eldatDeviceType(aDeviceType),
  lastMessageTime(Never),
  lastRSSI(INVALID_RSSI)
{
  iconBaseName = "eldat";
  groupColoredIcon = true;
  lastMessageTime = MainLoop::now(); // consider packet received at time of creation (to avoid devices starting inactive)
}


bool EldatDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Nothing to do to identify for now
  return true; // simple identification, callback will not be called
}



EldatVdc &EldatDevice::getEldatVdc()
{
  return *(static_cast<EldatVdc *>(vdcP));
}



EldatAddress EldatDevice::getAddress()
{
  return eldatAddress;
}


EldatSubDevice EldatDevice::getSubDevice()
{
  return subDevice;
}


void EldatDevice::deriveDsUid()
{
  // vDC implementation specific UUID:
  //   UUIDv5 with name = vdcClassIdentifier::unique_eldat_address
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  string s = vdcP->vdcClassIdentifier();
  string_format_append(s, "%08X", getAddress()); // hashed part of dSUID comes from unique Eldat address
  dSUID.setNameInSpace(s, vdcNamespace);
  dSUID.setSubdeviceIndex(getSubDevice()); // subdevice index is represented in the dSUID subdevice index byte
}


string EldatDevice::hardwareGUID()
{
  return string_format("eldataddress:%08X", getAddress());
}


//string EldatDevice::hardwareModelGUID()
//{
//  return string_format("eldat:%06X", EEP_PURE(getEEProfile()));
//}


string EldatDevice::modelName()
{
  // base class "model", derived classes might have nicer model names
  return string_format("ELDAT device type %d", eldatDeviceType);
}


string EldatDevice::vendorName()
{
  return "ELDAT GmbH";
}


void EldatDevice::setAddressingInfo(EldatAddress aAddress, EldatSubDevice aSubDeviceIndex)
{
  eldatAddress = aAddress;
  subDevice = aSubDeviceIndex;
  deriveDsUid();
}




bool EldatDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
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


void EldatDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  // clear learn-in data from DB
  if(getEldatVdc().db.executef("DELETE FROM knownDevices WHERE eldatAddress=%d AND subdevice=%d", getAddress(), getSubDevice())!=SQLITE_OK) {
    OLOG(LOG_ERR, "Error deleting device: %s", getEldatVdc().db.error()->description().c_str());
  }
  // disconnection is immediate, so we can call inherited right now
  inherited::disconnect(aForgetParams, aDisconnectResultHandler);
}



void EldatDevice::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  // NOP for now
  inherited::applyChannelValues(aDoneCB, aForDimming);
}


void EldatDevice::checkPresence(PresenceCB aPresenceResultHandler)
{
  bool present = true;
  // TODO: implement
  aPresenceResultHandler(present);
}


#define BEST_RSSI (-60) // opState should be 100% above this
#define WORST_RSSI (-110)  // opState should be 1% below this


int EldatDevice::opStateLevel()
{
  int opState = -1;
  if (lastRSSI>INVALID_RSSI) {
    // first judge from last RSSI
    opState = 1+(lastRSSI-WORST_RSSI)*99/(BEST_RSSI-WORST_RSSI); // 1..100 range
    if (opState<1) opState = 1;
    else if (opState>100) opState = 100;
  }
  return opState;
}


string EldatDevice::getOpStateText()
{
  string t;
  if (lastRSSI>INVALID_RSSI) {
    string_format_append(t, "%ddBm (", lastRSSI);
    format_duration_append(t, (MainLoop::now()-lastMessageTime)/Second, 2);
    t += " ago)";
  }
  else {
    t += "unseen";
  }
  return t;
}



void EldatDevice::handleMessage(EldatMode aMode, int aRSSI, string aData)
{
  // remember last message time
  lastMessageTime = MainLoop::now();
  lastRSSI = aRSSI;
  if (aMode==0 && aData.size()==1) {
    handleFunction(aData[0]);
  }
}




string EldatDevice::description()
{
  string s = inherited::description();
  string_format_append(s, "\n- ELDAT Address = 0x%08X, subDevice=%d", eldatAddress, subDevice);
  string_format_append(s, "\n- device type %d", eldatDeviceType);
  return s;
}


// MARK: - property access


enum {
  messageage_key,
  rssi_key,
  numProperties
};

static char eldatDevice_key;


int EldatDevice::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // Note: only add my own count when accessing root level properties!!
  if (aParentDescriptor->isRootOfObject()) {
    // Accessing properties at the Device (root) level, add mine
    return inherited::numProps(aDomain, aParentDescriptor)+numProperties;
  }
  // just return base class' count
  return inherited::numProps(aDomain, aParentDescriptor);
}


PropertyDescriptorPtr EldatDevice::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numProperties] = {
    { "x-p44-packetAge", apivalue_double, messageage_key, OKEY(eldatDevice_key) },
    { "x-p44-rssi", apivalue_int64, rssi_key, OKEY(eldatDevice_key) },
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
bool EldatDevice::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(eldatDevice_key)) {
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        case messageage_key:
          // Note lastMessageTime is set to now at startup, so additionally check lastRSSI
          if (lastMessageTime==Never || lastRSSI<=INVALID_RSSI)
            aPropValue->setNull();
          else
            aPropValue->setDoubleValue((double)(MainLoop::now()-lastMessageTime)/Second);
          return true;
        case rssi_key:
          if (lastRSSI<=INVALID_RSSI)
            aPropValue->setNull();
          else
            aPropValue->setInt32Value(lastRSSI);
          return true;
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


// MARK: - profile variants


static const EldatTypeVariantEntry EldatTypeVariants[] = {
  // dual rocker RPS button alternatives
  { 1, eldat_rocker,                2, "2-way 1/0 or up/down buttons", DeviceConfigurations::buttonTwoWay }, // rocker switches affect 2 indices (of which odd one does not exist in 2-way mode)
  { 1, eldat_rocker_reversed,       2, "2-way 0/1 or down/up buttons (reversed)", DeviceConfigurations::buttonTwoWayReversed }, // rocker switches affect 2 indices (of which odd one does not exist in 2-way mode)
  { 1, eldat_button,                2, "single button", DeviceConfigurations::buttonSingle  },
  { 1, eldat_motiondetector,        0, "motion detector", NULL },
  { 1, eldat_windowcontact_onoff,   0, "window contact (ON/OFF)", NULL },
  { 1, eldat_windowcontact_onoff_s, 0, "window contact (ON/OFF) with status every 24h", NULL },
  { 1, eldat_windowcontact_offon,   0, "window contact (OFF/ON)", NULL },
  { 1, eldat_windowcontact_offon_s, 0, "window contact (OFF/ON) with status every 24h", NULL },
  { 1, eldat_windowhandle_onoff,    0, "window handle (ON/OFF)", NULL },
  { 1, eldat_windowhandle_onoff_s,  0, "window handle (ON/OFF) with status every 24h", NULL },
  { 1, eldat_windowhandle_offon,    0, "window handle (OFF/ON)", NULL },
  { 1, eldat_windowhandle_offon_s,  0, "window handle (OFF/ON) with status every 24h", NULL },
  { 0, eldat_unknown, 0, NULL, NULL } // terminator
};


const EldatTypeVariantEntry *EldatDevice::deviceTypeVariantsTable()
{
  return EldatTypeVariants;
}



void EldatDevice::getDeviceConfigurations(DeviceConfigurationsVector &aConfigurations, StatusCB aStatusCB)
{
  // check if current profile is one of the interchangeable ones
  const EldatTypeVariantEntry *currentVariant = deviceTypeVariantsTable();
  bool anyVariants = false;
  while (currentVariant && currentVariant->typeGroup!=0) {
    // look for current type in the list of variants
    if (getEldatDeviceType()==currentVariant->eldatDeviceType) {
      // create string from all other variants (same typeGroup), if any
      const EldatTypeVariantEntry *variant = deviceTypeVariantsTable();
      while (variant->typeGroup!=0) {
        if (variant->typeGroup==currentVariant->typeGroup) {
          if (variant->eldatDeviceType!=getEldatDeviceType()) anyVariants = true; // another variant than just myself
          string id;
          if (variant->configId)
            id = variant->configId; // has well-known configuration id
          else
            id = string_format("eldat_%d", variant->eldatDeviceType); // id generated from type
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


string EldatDevice::getDeviceConfigurationId()
{
  const EldatTypeVariantEntry *currentVariant = deviceTypeVariantsTable();
  while (currentVariant && currentVariant->typeGroup!=0) {
    if (currentVariant->configId && getEldatDeviceType()==currentVariant->eldatDeviceType) {
      return currentVariant->configId; // has a well-known name, return that
    }
    currentVariant++;
  }
  // return a id generated from EEP
  return  string_format("eldat_%d", getEldatDeviceType());
}



ErrorPtr EldatDevice::switchConfiguration(const string aConfigurationId)
{
  EldatDeviceType newType = eldat_unknown;
  int nt;
  if (sscanf(aConfigurationId.c_str(), "eldat_%d", &nt)==1) {
    newType = (EldatDeviceType)nt;
  }
  // - find my typeGroup
  const EldatTypeVariantEntry *currentVariant = deviceTypeVariantsTable();
  while (currentVariant && currentVariant->typeGroup!=0) {
    if (getEldatDeviceType()==currentVariant->eldatDeviceType) {
      // this is my type group, now check if requested type is in my type group as well
      const EldatTypeVariantEntry *variant = deviceTypeVariantsTable();
      while (variant && variant->typeGroup!=0) {
        if (
          (variant->typeGroup==currentVariant->typeGroup) &&
          ((newType!=eldat_unknown && newType==variant->eldatDeviceType) || (newType==eldat_unknown && variant->configId && aConfigurationId==variant->configId))
        ) {
          // prevent switching if new profile is same as current one
          if (variant->eldatDeviceType==currentVariant->eldatDeviceType) return ErrorPtr(); // we already have that type -> NOP
          // requested type is in my group, change now
          switchTypes(*currentVariant, *variant); // will delete this device, so return immediately afterwards
          return ErrorPtr(); // changed profile
        }
        variant++;
      }
    }
    currentVariant++;
  }
  return inherited::switchConfiguration(aConfigurationId); // unknown profile at this level
}


void EldatDevice::switchTypes(const EldatTypeVariantEntry &aFromVariant, const EldatTypeVariantEntry &aToVariant)
{
  // make sure object is retained locally
  EldatDevicePtr keepMeAlive(this); // make sure this object lives until routine terminates
  // determine range of subdevices affected by this profile switch
  // - larger of both counts, 0 means all indices affected
  EldatSubDevice rangesize = 0;
  EldatSubDevice rangestart = 0;
  if (aFromVariant.subDeviceIndices!=0 && aToVariant.subDeviceIndices==aFromVariant.subDeviceIndices) {
    // old and new profile affects same subrange of all subdevice -> we can switch these subdevices only -> restrict range
    rangesize = aToVariant.subDeviceIndices;
    // subDeviceIndices range is required to start at an even multiple of rangesize
    rangestart = getSubDevice()/rangesize*rangesize;
  }
  // have devices related to current profile deleted, including settings
  // Note: this removes myself from container, and deletes the config (which is valid for the previous profile, i.e. a different type of device)
  getEldatVdc().unpairDevicesByAddress(getAddress(), true, rangestart, rangesize);
  // - create new ones, with same address and manufacturer, but new profile
  EldatSubDevice subDeviceIndex = rangestart;
  while (rangesize==0 || subDeviceIndex<rangestart+rangesize) {
    // create devices until done
    EldatDevicePtr newDev = newDevice(
      &getEldatVdc(),
      getAddress(), // same address as current device
      subDeviceIndex, // index to create a device for
      aToVariant.eldatDeviceType, // the new eldat device type
      rangestart
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
    getEldatVdc().addAndRememberDevice(newDev);
    // - make it dirty if we have set zone or name
    if (hasNameOrZone && newDev->deviceSettings) {
      newDev->deviceSettings->markDirty(); // make sure name and/or zone are saved permanently
    }
    // Note: subDeviceIndex is incremented according to device's index space requirements by newDevice() implementation
  }
}


// MARK: - Eldat buttons


EldatButtonDevice::EldatButtonDevice(EldatVdc *aVdcP, EldatDeviceType aDeviceType) :
  inherited(aVdcP, aDeviceType)
{
}


string EldatButtonDevice::modelName()
{
  if (eldatDeviceType==eldat_rocker)
    return "ELDAT two-way button";
  else
    return "ELDAT single button";
}



#define BUTTON_RELEASE_TIMEOUT (100*MilliSecond)

void EldatButtonDevice::handleFunction(EldatFunction aFunction)
{
  // device responsible for this function?
  int funcIndex = aFunction-'A';
  if (eldatDeviceType==eldat_button) {
    // single button
    if (funcIndex!=subDevice)
      return; // not my function
  }
  else {
    // rocker
    if (funcIndex!=subDevice && funcIndex!=subDevice+1)
      return; // not my function
  }
  // select behaviour
  int buttonNo = 0; // 0=down or single, 1=up
  bool funcAC = aFunction=='A' || aFunction=='C';
  if (
    (eldatDeviceType==eldat_rocker && funcAC) ||
    (eldatDeviceType==eldat_rocker_reversed && !funcAC)
  ) {
    buttonNo = 1;
  }
  ButtonBehaviourPtr bb = getButton(buttonNo);
  // now handle
  if (!pressedTicket) {
    // pressing button now
    bb->updateButtonState(true);
  }
  else {
    // cancel current ticket
    pressedTicket.cancel();
  }
  pressedTicket.executeOnce(boost::bind(&EldatButtonDevice::buttonReleased, this, buttonNo), BUTTON_RELEASE_TIMEOUT);
}


void EldatButtonDevice::buttonReleased(int aButtonNo)
{
  pressedTicket = 0;
  ButtonBehaviourPtr bb = getButton(aButtonNo);
  bb->updateButtonState(false);
}



// MARK: - Eldat motion detector


EldatMotionDetector::EldatMotionDetector(EldatVdc *aVdcP) :
  inherited(aVdcP, eldat_motiondetector)
{
}


string EldatMotionDetector::modelName()
{
  return "ELDAT motion detector";
}



void EldatMotionDetector::handleFunction(EldatFunction aFunction)
{
  // A = detector on, B = detector off
  BinaryInputBehaviourPtr ib = getInput(0);
  if (ib) {
    ib->updateInputState(aFunction=='A' ? 1 : 0);
  }
}


// MARK: - Eldat window contact


EldatWindowContact::EldatWindowContact(EldatVdc *aVdcP, bool aOffOnType, bool aWithStatus) :
  inherited(aVdcP, aOffOnType ? (aWithStatus ? eldat_windowcontact_offon_s : eldat_windowcontact_offon) : (aWithStatus ? eldat_windowcontact_onoff_s : eldat_windowcontact_onoff))
{
}


string EldatWindowContact::modelName()
{
  return "ELDAT window contact";
}



void EldatWindowContact::handleFunction(EldatFunction aFunction)
{
  // eldat_windowcontact_onoff: A = contact/window opened, B = contact/window closed
  // eldat_windowcontact_offon: B = contact/window opened, A = contact/window closed
  BinaryInputBehaviourPtr ib = getInput(0);
  if (ib) {
    ib->updateInputState(aFunction==(eldatDeviceType==eldat_windowcontact_onoff || eldatDeviceType==eldat_windowcontact_onoff_s ? 'A' : 'B') ? 1 : 0);
  }
}




// MARK: - Eldat window handle


EldatWindowHandle::EldatWindowHandle(EldatVdc *aVdcP, bool aOffOnType, bool aWithStatus) :
  inherited(aVdcP, aOffOnType ? (aWithStatus ? eldat_windowhandle_offon_s : eldat_windowhandle_offon) : (aWithStatus ? eldat_windowhandle_onoff_s : eldat_windowhandle_onoff))
{
}


string EldatWindowHandle::modelName()
{
  return "ELDAT window handle";
}



void EldatWindowHandle::handleFunction(EldatFunction aFunction)
{
  // eldat_windowhandle_onoff: A = handle in opened position, B = handle in closed position
  // eldat_windowhandle_offon: B = handle in opened position, A = handle in closed position
  BinaryInputBehaviourPtr ib = getInput(0);
  if (ib) {
    ib->updateInputState(aFunction==(eldatDeviceType==eldat_windowhandle_onoff || eldatDeviceType==eldat_windowhandle_onoff_s  ? 'A' : 'B') ? 1 : 0);
  }
}




// MARK: - Eldat remote control device


EldatRemoteControlDevice::EldatRemoteControlDevice(EldatVdc *aVdcP, EldatDeviceType aDeviceType) :
  inherited(aVdcP, aDeviceType)
{
}


string EldatRemoteControlDevice::modelName()
{
  if (eldatDeviceType==eldat_ABlight)
    return "ELDAT on/off light";
  else
    return "ELDAT on/off relay";
}



uint8_t EldatRemoteControlDevice::teachInSignal(int8_t aVariant)
{
  if (aVariant<4) {
    // issue simulated buttom press - variant: 0=A, 1=B, 2=C, 3=D
    if (aVariant<0) return 4; // only query: we have 4 teach-in variants
    sendFunction('A'+aVariant); // send message
    return 4;
  }
  return inherited::teachInSignal(aVariant);
}



void EldatRemoteControlDevice::markUsedSendChannels(string &aUsedSendChannelsMap)
{
  int chan = getAddress() & 0x7F;
  if (chan<aUsedSendChannelsMap.size()) {
    aUsedSendChannelsMap[chan]='1';
  }
}


void EldatRemoteControlDevice::sendFunction(EldatFunction aFunction)
{
  string cmd = string_format("TXP,%02X,%c", getAddress() & 0x7F, aFunction);
  getEldatVdc().eldatComm.sendCommand(cmd, boost::bind(&EldatRemoteControlDevice::sentFunction, this, _1, _2));
}


void EldatRemoteControlDevice::sentFunction(string aAnswer, ErrorPtr aError)
{
  if (Error::notOK(aError)) {
    OLOG(LOG_ERR, "Error sending message: %s", aError->text());
  }
  else {
    OLOG(LOG_INFO, "Sending function result: %s", aAnswer.c_str());
  }
}


void EldatRemoteControlDevice::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  // standard output behaviour
  if (getOutput()) {
    ChannelBehaviourPtr ch = getOutput()->getChannelByType(channeltype_default);
    if (ch->needsApplying()) {
      bool on = ch->getChannelValueBool();
      sendFunction(on ? 'A' : 'B');
      ch->channelValueApplied();
    }
  }
  inherited::applyChannelValues(aDoneCB, aForDimming);
}




// MARK: - device factory


EldatDevicePtr EldatDevice::newDevice(
  EldatVdc *aVdcP,
  EldatAddress aAddress,
  EldatSubDevice &aSubDeviceIndex,
  EldatDeviceType aEldatDeviceType,
  EldatSubDevice aFirstSubDevice
) {
  EldatDevicePtr newDev; // none so far
  if (aEldatDeviceType==eldat_rocker || aEldatDeviceType==eldat_rocker_reversed) {
    // create a single rocker per learn-in (unlike EnOcean!)
    if (aSubDeviceIndex==aFirstSubDevice) {
      // Create a ELDAT rocker button device
      newDev = EldatDevicePtr(new EldatButtonDevice(aVdcP, aEldatDeviceType));
      // standard device settings without scene table
      newDev->installSettings();
      // assign channel and address
      newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
      newDev->setFunctionDesc("two-way button");
      // set icon name
      newDev->setIconInfo("eldat_button", true);
      // Buttons can be used for anything
      newDev->setColorClass(class_black_joker);
      // Create two behaviours, one for the up button, one for the down button
      // - create button input 0 for what dS will handle as "down key" (actual button depends on rocker type - reversed or normal)
      ButtonBehaviourPtr downBhvr = ButtonBehaviourPtr(new ButtonBehaviour(*newDev.get(),"")); // automatic id
      downBhvr->setHardwareButtonConfig(0, buttonType_2way, buttonElement_down, false, 1, 0); // counterpart up-button has buttonIndex 1, fixed mode
      downBhvr->setGroup(group_yellow_light); // pre-configure for light
      downBhvr->setHardwareName("down key");
      newDev->addBehaviour(downBhvr);
      // - create button input 1 for what dS will handle as "up key" (actual button depends on "reversed")
      ButtonBehaviourPtr upBhvr = ButtonBehaviourPtr(new ButtonBehaviour(*newDev.get(),"")); // automatic id
      upBhvr->setGroup(group_yellow_light); // pre-configure for light
      upBhvr->setHardwareButtonConfig(0, buttonType_2way, buttonElement_up, false, 0, 0); // counterpart down-button has buttonIndex 0, fixed mode
      upBhvr->setHardwareName("up key");
      newDev->addBehaviour(upBhvr);
      // count it
      // - 2-way rocker switches use indices 0,2,4,6,... to leave room for separate button mode without shifting indices
      aSubDeviceIndex+=2;
    }
  }
  else if (aEldatDeviceType==eldat_button) {
    // single buttons, created in pairs when learned in
    if (aSubDeviceIndex<=aFirstSubDevice+1) {
      // Create a ELDAT single button device
      newDev = EldatDevicePtr(new EldatButtonDevice(aVdcP, aEldatDeviceType));
      // standard device settings without scene table
      newDev->installSettings();
      // assign channel and address
      newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
      newDev->setFunctionDesc("button");
      // set icon name
      newDev->setIconInfo("eldat_button", true);
      // Buttons can be used for anything
      newDev->setColorClass(class_black_joker);
      // Create one button behaviour
      ButtonBehaviourPtr bb = ButtonBehaviourPtr(new ButtonBehaviour(*newDev.get(),"")); // automatic id
      bb->setHardwareButtonConfig(0, buttonType_single, buttonElement_center, false, 0, 2); // might be combined to form pairs
      bb->setGroup(group_yellow_light); // pre-configure for light
      bb->setHardwareName("button");
      newDev->addBehaviour(bb);
      // count it
      // - single buttons don't skip indices
      aSubDeviceIndex+=1;
    }
  }
  else if (aEldatDeviceType==eldat_motiondetector) {
    // motion detector
    if (aSubDeviceIndex==aFirstSubDevice) {
      // Create a ELDAT single motion detector device
      newDev = EldatDevicePtr(new EldatMotionDetector(aVdcP));
      // standard device settings without scene table
      newDev->installSettings();
      // assign channel and address
      newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
      newDev->setFunctionDesc("motion detector");
      // set icon name
      newDev->setIconInfo("eldat", true);
      // motion detectors can be used for anything
      newDev->setColorClass(class_black_joker);
      // Create one input behaviour
      BinaryInputBehaviourPtr ib = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*newDev.get(),"")); // automatic id
      ib->setHardwareInputConfig(binInpType_motion, usage_room, true, Never, Never);
      ib->setHardwareName("detector");
      newDev->addBehaviour(ib);
      // - motion detector uses two indices (it uses A+B functions)
      aSubDeviceIndex+=2;
    }
  }
  else if (
    aEldatDeviceType==eldat_windowcontact_onoff || aEldatDeviceType==eldat_windowcontact_onoff_s ||
    aEldatDeviceType==eldat_windowcontact_offon || aEldatDeviceType==eldat_windowcontact_offon_s
  ) {
    // window contact
    if (aSubDeviceIndex==aFirstSubDevice) {
      // Create a ELDAT window contact device
      bool hasStatus = aEldatDeviceType==eldat_windowcontact_onoff_s || aEldatDeviceType==eldat_windowcontact_offon_s;
      bool isOffOn = aEldatDeviceType==eldat_windowcontact_offon || aEldatDeviceType==eldat_windowcontact_offon_s;
      newDev = EldatDevicePtr(new EldatWindowContact(aVdcP, isOffOn, hasStatus));
      // standard device settings without scene table
      newDev->installSettings();
      // assign channel and address
      newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
      newDev->setFunctionDesc("window contact");
      // set icon name
      newDev->setIconInfo("eldat", true);
      // window contacts can be used for anything
      newDev->setColorClass(class_black_joker);
      // Create one input behaviour
      BinaryInputBehaviourPtr ib = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*newDev.get(),"")); // automatic id
      ib->setHardwareInputConfig(binInpType_windowOpen, usage_room, true, Never, hasStatus ? 24*Hour : Never);
      ib->setHardwareName("window open");
      newDev->addBehaviour(ib);
      // - window contact uses two indices (it uses A+B functions)
      aSubDeviceIndex+=2;
    }
  }
  else if (
    aEldatDeviceType==eldat_windowhandle_onoff || aEldatDeviceType==eldat_windowhandle_onoff_s ||
    aEldatDeviceType==eldat_windowhandle_offon || aEldatDeviceType==eldat_windowhandle_offon_s
  ) {
    // window handle
    if (aSubDeviceIndex==aFirstSubDevice) {
      // Create a ELDAT window handle device
      bool hasStatus = aEldatDeviceType==eldat_windowhandle_onoff_s || aEldatDeviceType==eldat_windowhandle_offon_s;
      bool isOffOn = aEldatDeviceType==eldat_windowhandle_offon || aEldatDeviceType==eldat_windowhandle_offon_s;
      newDev = EldatDevicePtr(new EldatWindowHandle(aVdcP, isOffOn, hasStatus));
      // standard device settings without scene table
      newDev->installSettings();
      // assign channel and address
      newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
      newDev->setFunctionDesc("window handle");
      // set icon name
      newDev->setIconInfo("eldat", true);
      // window handles can be used for anything
      newDev->setColorClass(class_black_joker);
      // Create one input behaviour
      BinaryInputBehaviourPtr ib = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*newDev.get(),"")); // automatic id
      ib->setHardwareInputConfig(binInpType_windowHandle, usage_room, true, Never, hasStatus ? 24*Hour : Never);
      ib->setHardwareName("handle state");
      newDev->addBehaviour(ib);
      // - window contact uses two indices (it uses A+B functions)
      aSubDeviceIndex+=2;
    }
  }
  else if (aEldatDeviceType==eldat_ABrelay || aEldatDeviceType==eldat_ABlight) {
    if (aSubDeviceIndex==aFirstSubDevice) {
      // Create a ELDAT remote control device
      newDev = EldatDevicePtr(new EldatRemoteControlDevice(aVdcP, aEldatDeviceType));
      // assign channel and address
      newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
      // set icon name
      newDev->setIconInfo("eldat", true);
      // type specifics
      if (aEldatDeviceType==eldat_ABlight) {
        // light device scene
        newDev->installSettings(DeviceSettingsPtr(new LightDeviceSettings(*newDev)));
        newDev->setFunctionDesc("on/off light");
        newDev->setColorClass(class_yellow_light);
        // - add standard light output behaviour
        LightBehaviourPtr l = LightBehaviourPtr(new LightBehaviour(*newDev.get()));
        l->setHardwareOutputConfig(outputFunction_switch, outputmode_binary, usage_undefined, false, -1);
        newDev->addBehaviour(l);
      }
      else {
        // standard single-value scene table (SimpleScene)
        newDev->installSettings(DeviceSettingsPtr(new SceneDeviceSettings(*newDev)));
        newDev->setFunctionDesc("on/off relay");
        newDev->setColorClass(class_black_joker);
        OutputBehaviourPtr o = OutputBehaviourPtr(new OutputBehaviour(*newDev.get()));
        o->setHardwareOutputConfig(outputFunction_switch, outputmode_binary, usage_undefined, false, -1);
        o->setGroupMembership(group_black_variable, true); // put into joker group by default
        o->addChannel(ChannelBehaviourPtr(new DigitalChannel(*o, "relay")));
        newDev->addBehaviour(o);
      }
      // count it
      aSubDeviceIndex++;
    }
  }
  // return device (or empty if none created)
  return newDev;
}





int EldatDevice::createDevicesFromType(
  EldatVdc *aVdcP,
  EldatAddress aAddress,
  EldatDeviceType aEldatDeviceType,
  EldatSubDevice aFirstSubDevice
)
{
  EldatSubDevice subDeviceIndex = aFirstSubDevice; // start at given index
  int numDevices = 0; // number of devices
  while (true) {
    // create devices until done
    EldatDevicePtr newDev = newDevice(
      aVdcP,
      aAddress,
      subDeviceIndex, // index to create next device for
      aEldatDeviceType, // the type
      aFirstSubDevice // the first subdevice to be created for this address and type
    );
    if (!newDev) {
      // could not create a device for subDeviceIndex
      break; // -> done
    }
    // created device
    numDevices++;
    // - add it to the container
    aVdcP->addAndRememberDevice(newDev);
    // Note: subDeviceIndex is incremented according to device's index space requirements by newDevice() implementation
  }
  // return number of devices created
  return numDevices;
}




#endif // ENABLE_ELDAT

