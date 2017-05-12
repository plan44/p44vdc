//
//  Copyright (c) 2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "zfdevice.hpp"
#include "zfvdc.hpp"

#if ENABLE_ZF

#include "buttonbehaviour.hpp"
#include "binaryinputbehaviour.hpp"
#include "sensorbehaviour.hpp"
#include "outputbehaviour.hpp"


using namespace p44;


// MARK: ===== ZfDevice

ZfDevice::ZfDevice(ZfVdc *aVdcP, ZfDeviceType aDeviceType) :
  Device(aVdcP),
  zfDeviceType(aDeviceType),
  lastMessageTime(Never),
  lastRSSI(-999)
{
  iconBaseName = "zf";
  groupColoredIcon = true;
  lastMessageTime = MainLoop::now(); // consider packet received at time of creation (to avoid devices starting inactive)
}


ZfVdc &ZfDevice::getZfVdc()
{
  return *(static_cast<ZfVdc *>(vdcP));
}


bool ZfDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Nothing to do to identify for now
  return true; // simple identification, callback will not be called
}



ZfAddress ZfDevice::getAddress()
{
  return zfAddress;
}


ZfSubDevice ZfDevice::getSubDevice()
{
  return subDevice;
}


void ZfDevice::deriveDsUid()
{
  // vDC implementation specific UUID:
  //   UUIDv5 with name = vdcClassIdentifier::unique_zf_address
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  string s = vdcP->vdcClassIdentifier();
  string_format_append(s, "%08X", getAddress()); // hashed part of dSUID comes from unique ZF address
  dSUID.setNameInSpace(s, vdcNamespace);
  dSUID.setSubdeviceIndex(getSubDevice()); // subdevice index is represented in the dSUID subdevice index byte
}


string ZfDevice::hardwareGUID()
{
  return string_format("zfaddress:%08X", getAddress());
}


string ZfDevice::modelName()
{
  // base class "model", derived classes might have nicer model names
  return string_format("ZF device type %d", zfDeviceType);
}


string ZfDevice::vendorName()
{
  return "ZF Friedrichshafen AG";
}


void ZfDevice::setAddressingInfo(ZfAddress aAddress, ZfSubDevice aSubDeviceIndex)
{
  zfAddress = aAddress;
  subDevice = aSubDeviceIndex;
  deriveDsUid();
}




bool ZfDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
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


void ZfDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  // clear learn-in data from DB
  if(getZfVdc().db.executef("DELETE FROM knownDevices WHERE zfAddress=%d AND subdevice=%d", getAddress(), getSubDevice())!=SQLITE_OK) {
    ALOG(LOG_ERR, "Error deleting device: %s", getZfVdc().db.error()->description().c_str());
  }
  // disconnection is immediate, so we can call inherited right now
  inherited::disconnect(aForgetParams, aDisconnectResultHandler);
}



void ZfDevice::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
//  // trigger updating all device outputs
//  for (int i=0; i<numChannels(); i++) {
//    if (getChannelByIndex(i, true)) {
//      // channel needs update
//      pendingDeviceUpdate = true;
//      break; // no more checking needed, need device level update anyway
//    }
//  }
//  if (pendingDeviceUpdate) {
//    // we need to apply data
//    needOutgoingUpdate();
//  }
  inherited::applyChannelValues(aDoneCB, aForDimming);
}


void ZfDevice::checkPresence(PresenceCB aPresenceResultHandler)
{
  bool present = true;
  // TODO: implement
  aPresenceResultHandler(present);
}


void ZfDevice::handlePacket(ZfPacketPtr aPacket)
{
  // remember last message time
  lastMessageTime = MainLoop::now();
  lastRSSI = aPacket->rssi;
  processPacket(aPacket);
}




string ZfDevice::description()
{
  string s = inherited::description();
  string_format_append(s, "\n- ZF Address = 0x%08X, subDevice=%d", zfAddress, subDevice);
  string_format_append(s, "\n- device type %d", zfDeviceType);
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

static char zfDevice_key;


int ZfDevice::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // Note: only add my own count when accessing root level properties!!
  if (aParentDescriptor->isRootOfObject()) {
    // Accessing properties at the Device (root) level, add mine
    return inherited::numProps(aDomain, aParentDescriptor)+numProperties;
  }
  // just return base class' count
  return inherited::numProps(aDomain, aParentDescriptor);
}


PropertyDescriptorPtr ZfDevice::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numProperties] = {
    { "x-p44-profileVariants", apivalue_null, typeVariants_key, OKEY(zfDevice_key) },
    { "x-p44-profile", apivalue_int64, devicetype_key, OKEY(zfDevice_key) },
    { "x-p44-packetAge", apivalue_double, messageage_key, OKEY(zfDevice_key) },
    { "x-p44-rssi", apivalue_int64, rssi_key, OKEY(zfDevice_key) },
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
bool ZfDevice::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(zfDevice_key)) {
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        case typeVariants_key:
          aPropValue->setType(apivalue_object); // make object (incoming object is NULL)
          return getTypeVariants(aPropValue);
        case devicetype_key:
          aPropValue->setInt32Value(getZfDeviceType()); return true;
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
          setTypeVariant((ZfDeviceType)aPropValue->int32Value()); return true;
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


// MARK: ===== profile variants


static const ZfTypeVariantEntry ZfTypeVariants[] = {
  { 1, zf_button,               0, "button" },
  { 1, zf_contact,              0, "contact" },
  { 0, zf_unknown, 0, NULL } // terminator
};


const ZfTypeVariantEntry *ZfDevice::deviceTypeVariantsTable()
{
  return ZfTypeVariants;
}



bool ZfDevice::getTypeVariants(ApiValuePtr aApiObjectValue)
{
  // check if current profile is one of the interchangeable ones
  const ZfTypeVariantEntry *currentVariant = deviceTypeVariantsTable();
  while (currentVariant && currentVariant->typeGroup!=0) {
    // look for current type in the list of variants
    if (getZfDeviceType()==currentVariant->zfDeviceType) {
      // create string from all other variants (same typeGroup), if any
      bool anyVariants = false;
      const ZfTypeVariantEntry *variant = deviceTypeVariantsTable();
      while (variant->typeGroup!=0) {
        if (variant->typeGroup==currentVariant->typeGroup) {
          if (variant->zfDeviceType!=getZfDeviceType()) anyVariants = true; // another variant than just myself
          aApiObjectValue->add(string_format("%d",variant->zfDeviceType), aApiObjectValue->newString(variant->description));
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


bool ZfDevice::setTypeVariant(ZfDeviceType aType)
{
  // verify if changeable profile code requested
  // - check for already having that profile
  if (aType==getZfDeviceType()) return true; // we already have that type -> NOP
  // - find my typeGroup
  const ZfTypeVariantEntry *currentVariant = deviceTypeVariantsTable();
  while (currentVariant && currentVariant->typeGroup!=0) {
    if (getZfDeviceType()==currentVariant->zfDeviceType) {
      // this is my type group, now check if requested type is in my type group as well
      const ZfTypeVariantEntry *variant = deviceTypeVariantsTable();
      while (variant && variant->typeGroup!=0) {
        if (variant->typeGroup==currentVariant->typeGroup && variant->zfDeviceType==aType) {
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


void ZfDevice::switchTypes(const ZfTypeVariantEntry &aFromVariant, const ZfTypeVariantEntry &aToVariant)
{
  // make sure object is retained locally
  ZfDevicePtr keepMeAlive(this); // make sure this object lives until routine terminates
  // determine range of subdevices affected by this profile switch
  // - larger of both counts, 0 means all indices affected
  ZfSubDevice rangesize = 0;
  ZfSubDevice rangestart = 0;
  if (aFromVariant.subDeviceIndices!=0 && aToVariant.subDeviceIndices==aFromVariant.subDeviceIndices) {
    // old and new profile affects same subrange of all subdevice -> we can switch these subdevices only -> restrict range
    rangesize = aToVariant.subDeviceIndices;
    // subDeviceIndices range is required to start at an even multiple of rangesize
    rangestart = getSubDevice()/rangesize*rangesize;
  }
  // have devices related to current profile deleted, including settings
  // Note: this removes myself from container, and deletes the config (which is valid for the previous profile, i.e. a different type of device)
  getZfVdc().unpairDevicesByAddress(getAddress(), true, rangestart, rangesize);
  // - create new ones, with same address and manufacturer, but new profile
  ZfSubDevice subDeviceIndex = rangestart;
  while (rangesize==0 || subDeviceIndex<rangestart+rangesize) {
    // create devices until done
    ZfDevicePtr newDev = newDevice(
      &getZfVdc(),
      getAddress(), // same address as current device
      subDeviceIndex, // index to create a device for
      aToVariant.zfDeviceType, // the new ZF device type
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
    getZfVdc().addAndRememberDevice(newDev);
    // - make it dirty if we have set zone or name
    if (hasNameOrZone && newDev->deviceSettings) {
      newDev->deviceSettings->markDirty(); // make sure name and/or zone are saved permanently
    }
    // Note: subDeviceIndex is incremented according to device's index space requirements by newDevice() implementation
  }
}





// MARK: ===== ZF buttons


ZfButtonDevice::ZfButtonDevice(ZfVdc *aVdcP, ZfDeviceType aDeviceType) :
  inherited(aVdcP, aDeviceType),
  pressedTicket(0)
{
}


string ZfButtonDevice::modelName()
{
  return "ZF button";
}



#define BUTTON_RELEASE_TIMEOUT (100*MilliSecond)

void ZfButtonDevice::processPacket(ZfPacketPtr aPacket)
{
  if (aPacket->opCode==1) {
    ButtonBehaviourPtr bb = boost::dynamic_pointer_cast<ButtonBehaviour>(buttons[0]);
    // pressing button now
    // - data==00 means "pressed", 01 means "released"
    bb->buttonAction(aPacket->data==00);
  }
}


// MARK: ===== ZF single contact


ZfSimpleContact::ZfSimpleContact(ZfVdc *aVdcP) :
  inherited(aVdcP, zf_contact)
{
}


string ZfSimpleContact::modelName()
{
  return "ZF simple contact";
}



void ZfSimpleContact::processPacket(ZfPacketPtr aPacket)
{
  if (aPacket->opCode==1) {
    BinaryInputBehaviourPtr ib = boost::dynamic_pointer_cast<BinaryInputBehaviour>(binaryInputs[0]);
    if (ib) {
      // - data==00 means "pressed", 01 means "released"
      ib->updateInputState(aPacket->data==00);
    }
  }
}


// MARK: ===== device factory


ZfDevicePtr ZfDevice::newDevice(
  ZfVdc *aVdcP,
  ZfAddress aAddress,
  ZfSubDevice &aSubDeviceIndex,
  ZfDeviceType aZfDeviceType,
  ZfSubDevice aFirstSubDevice
) {
  ZfDevicePtr newDev; // none so far
  if (aZfDeviceType==zf_button) {
    // single button
    if (aSubDeviceIndex<=aFirstSubDevice) {
      // Create a ZF single button device
      newDev = ZfDevicePtr(new ZfButtonDevice(aVdcP, aZfDeviceType));
      // standard device settings without scene table
      newDev->installSettings();
      // assign channel and address
      newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
      newDev->setFunctionDesc("button");
      // set icon name
      newDev->setIconInfo("zf_button", true);
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
  else if (aZfDeviceType==zf_contact) {
    // simple contact
    if (aSubDeviceIndex==aFirstSubDevice) {
      // Create a ZF single button device
      newDev = ZfDevicePtr(new ZfSimpleContact(aVdcP));
      // standard device settings without scene table
      newDev->installSettings();
      // assign channel and address
      newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
      newDev->setFunctionDesc("contact");
      // set icon name
      newDev->setIconInfo("zf", true);
      // Contacts can be used for anything
      newDev->setColorClass(class_black_joker);
      // Create one input behaviour
      BinaryInputBehaviourPtr ib = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*newDev.get()));
      ib->setHardwareInputConfig(binInpType_none, usage_room, true, 0);
      ib->setHardwareName("contact");
      newDev->addBehaviour(ib);
      // count it
      // - contacts don't skip indices
      aSubDeviceIndex+=1;
    }
  }
  // return device (or empty if none created)
  return newDev;
}





int ZfDevice::createDevicesFromType(
  ZfVdc *aVdcP,
  ZfAddress aAddress,
  ZfDeviceType aZfDeviceType,
  ZfSubDevice aFirstSubDevice
)
{
  ZfSubDevice subDeviceIndex = aFirstSubDevice; // start at given index
  int numDevices = 0; // number of devices
  while (true) {
    // create devices until done
    ZfDevicePtr newDev = newDevice(
      aVdcP,
      aAddress,
      subDeviceIndex, // index to create next device for
      aZfDeviceType, // the type
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




#endif // ENABLE_ZF

