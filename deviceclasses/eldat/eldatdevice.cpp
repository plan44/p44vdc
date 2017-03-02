//
//  Copyright (c) 2016-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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


using namespace p44;


// MARK: ===== EldatDevice

EldatDevice::EldatDevice(EldatVdc *aVdcP, EldatDeviceType aDeviceType) :
  Device(aVdcP),
  eldatDeviceType(aDeviceType),
  lastMessageTime(Never),
  lastRSSI(-999)
{
  iconBaseName = "eldat";
  groupColoredIcon = true;
  lastMessageTime = MainLoop::now(); // consider packet received at time of creation (to avoid devices starting inactive)
}


void EldatDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Nothing to do to identify for now
  identificationOK(aIdentifyCB);
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
  getEldatVdc().db.executef("DELETE FROM knownDevices WHERE eldatAddress=%d AND subdevice=%d", getAddress(), getSubDevice());
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


// MARK: ===== property access


enum {
  typeVariants_key,
  devicetype_key,
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
    { "x-p44-profileVariants", apivalue_null, typeVariants_key, OKEY(eldatDevice_key) },
    { "x-p44-profile", apivalue_int64, devicetype_key, OKEY(eldatDevice_key) },
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
        case typeVariants_key:
          aPropValue->setType(apivalue_object); // make object (incoming object is NULL)
          return getTypeVariants(aPropValue);
        case devicetype_key:
          aPropValue->setInt32Value(getEldatDeviceType()); return true;
        case messageage_key:
          // Note lastMessageTime is set to now at startup, so additionally check lastRSSI
          if (lastMessageTime==Never || lastRSSI<=-999)
            aPropValue->setNull();
          else
            aPropValue->setDoubleValue((double)(MainLoop::now()-lastMessageTime)/Second);
          return true;
        case rssi_key:
          if (lastRSSI<=-999)
            aPropValue->setNull();
          else
            aPropValue->setInt32Value(lastRSSI);
          return true;
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        case devicetype_key:
          setTypeVariant((EldatDeviceType)aPropValue->int32Value()); return true;
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


// MARK: ===== profile variants


static const EldatTypeVariantEntry EldatTypeVariants[] = {
  // dual rocker RPS button alternatives
  { 1, eldat_rocker,            2, "2-way 1/0 or up/down buttons" }, // rocker switches affect 2 indices (of which odd one does not exist in 2-way mode)
  { 1, eldat_rocker_reversed,   2, "2-way 0/1 or down/up buttons (reversed)" }, // rocker switches affect 2 indices (of which odd one does not exist in 2-way mode)
  { 1, eldat_button,            2, "single button" },
  { 1, eldat_motiondetector,    0, "motion detector" },
  { 0, eldat_unknown, 0, NULL } // terminator
};


const EldatTypeVariantEntry *EldatDevice::deviceTypeVariantsTable()
{
  return EldatTypeVariants;
}



bool EldatDevice::getTypeVariants(ApiValuePtr aApiObjectValue)
{
  // check if current profile is one of the interchangeable ones
  const EldatTypeVariantEntry *currentVariant = deviceTypeVariantsTable();
  while (currentVariant && currentVariant->typeGroup!=0) {
    // look for current type in the list of variants
    if (getEldatDeviceType()==currentVariant->eldatDeviceType) {
      // create string from all other variants (same typeGroup), if any
      bool anyVariants = false;
      const EldatTypeVariantEntry *variant = deviceTypeVariantsTable();
      while (variant->typeGroup!=0) {
        if (variant->typeGroup==currentVariant->typeGroup) {
          if (variant->eldatDeviceType!=getEldatDeviceType()) anyVariants = true; // another variant than just myself
          aApiObjectValue->add(string_format("%d",variant->eldatDeviceType), aApiObjectValue->newString(variant->description));
        }
        variant++;
      }
      // there are variants
      return anyVariants;
    }
    currentVariant++;
  }
  return false; // no variants
}


bool EldatDevice::setTypeVariant(EldatDeviceType aType)
{
  // verify if changeable profile code requested
  // - check for already having that profile
  if (aType==getEldatDeviceType()) return true; // we already have that type -> NOP
  // - find my typeGroup
  const EldatTypeVariantEntry *currentVariant = deviceTypeVariantsTable();
  while (currentVariant && currentVariant->typeGroup!=0) {
    if (getEldatDeviceType()==currentVariant->eldatDeviceType) {
      // this is my type group, now check if requested type is in my type group as well
      const EldatTypeVariantEntry *variant = deviceTypeVariantsTable();
      while (variant && variant->typeGroup!=0) {
        if (variant->typeGroup==currentVariant->typeGroup && variant->eldatDeviceType==aType) {
          // requested type is in my group, change now
          switchTypes(*currentVariant, *variant); // will delete this device, so return immediately afterwards
          return true; // changed profile
        }
        variant++;
      }
    }
    currentVariant++;
  }
  return false; // invalid profile
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
    if (newDev->deviceSettings && deviceSettings && deviceSettings->zoneID!=0) {
      hasNameOrZone = true;
      newDev->deviceSettings->zoneID = deviceSettings->zoneID;
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





// MARK: ===== Eldat buttons


EldatButtonDevice::EldatButtonDevice(EldatVdc *aVdcP, EldatDeviceType aDeviceType) :
  inherited(aVdcP, aDeviceType),
  pressedTicket(0)
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
  ButtonBehaviourPtr bb = boost::dynamic_pointer_cast<ButtonBehaviour>(buttons[buttonNo]);
  // now handle
  if (!pressedTicket) {
    // pressing button now
    bb->buttonAction(true);
  }
  else {
    // cancel current ticket
    MainLoop::currentMainLoop().cancelExecutionTicket(pressedTicket);
  }
  pressedTicket = MainLoop::currentMainLoop().executeOnce(boost::bind(&EldatButtonDevice::buttonReleased, this, buttonNo), BUTTON_RELEASE_TIMEOUT);
}


void EldatButtonDevice::buttonReleased(int aButtonNo)
{
  pressedTicket = 0;
  ButtonBehaviourPtr bb = boost::dynamic_pointer_cast<ButtonBehaviour>(buttons[aButtonNo]);
  bb->buttonAction(false);
}



// MARK: ===== Eldat motion detector


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
  BinaryInputBehaviourPtr ib = boost::dynamic_pointer_cast<BinaryInputBehaviour>(binaryInputs[0]);
  if (ib) {
    ib->updateInputState(aFunction=='A' ? 1 : 0);
  }
}


// MARK: ===== device factory


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
      ButtonBehaviourPtr downBhvr = ButtonBehaviourPtr(new ButtonBehaviour(*newDev.get()));
      downBhvr->setHardwareButtonConfig(0, buttonType_2way, buttonElement_down, false, 1, true); // counterpart up-button has buttonIndex 1, fixed mode
      downBhvr->setGroup(group_yellow_light); // pre-configure for light
      downBhvr->setHardwareName("down key");
      newDev->addBehaviour(downBhvr);
      // - create button input 1 for what dS will handle as "up key" (actual button depends on "reversed")
      ButtonBehaviourPtr upBhvr = ButtonBehaviourPtr(new ButtonBehaviour(*newDev.get()));
      upBhvr->setGroup(group_yellow_light); // pre-configure for light
      upBhvr->setHardwareButtonConfig(0, buttonType_2way, buttonElement_up, false, 0, true); // counterpart down-button has buttonIndex 0, fixed mode
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
      ButtonBehaviourPtr bb = ButtonBehaviourPtr(new ButtonBehaviour(*newDev.get()));
      bb->setHardwareButtonConfig(0, buttonType_single, buttonElement_center, false, 0, true); // fixed mode
      bb->setGroup(group_yellow_light); // pre-configure for light
      bb->setHardwareName("button");
      newDev->addBehaviour(bb);
      // count it
      // - single buttons don't skip indices
      aSubDeviceIndex+=1;
    }
  }
  else if (aEldatDeviceType==eldat_motiondetector) {
    // single button
    if (aSubDeviceIndex==aFirstSubDevice) {
      // Create a ELDAT single button device
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
      BinaryInputBehaviourPtr ib = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*newDev.get()));
      ib->setHardwareInputConfig(binInpType_motion, usage_room, true, 0);
      ib->setHardwareName("detector");
      newDev->addBehaviour(ib);
      // - motion detector uses two indices (it uses A+B functions)
      aSubDeviceIndex+=2;
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

