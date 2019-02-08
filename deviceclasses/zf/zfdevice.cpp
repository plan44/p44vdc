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

#include "zfdevice.hpp"
#include "zfvdc.hpp"

#if ENABLE_ZF

#include "buttonbehaviour.hpp"
#include "binaryinputbehaviour.hpp"
#include "sensorbehaviour.hpp"
#include "outputbehaviour.hpp"


using namespace p44;


// MARK: ===== ZfDevice

#define INVALID_RSSI (-999)

ZfDevice::ZfDevice(ZfVdc *aVdcP, ZfDeviceType aDeviceType) :
  Device(aVdcP),
  zfDeviceType(aDeviceType),
  lastMessageTime(Never),
  lastRSSI(INVALID_RSSI)
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


#define BEST_RSSI (-60) // opState should be 100% above this
#define WORST_RSSI (-110)  // opState should be 1% below this


int ZfDevice::opStateLevel()
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


string ZfDevice::getOpStateText()
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


// MARK: ===== profile variants


static const ZfTypeVariantEntry ZfTypeVariants[] = {
  { 1, zf_button,               0, "button", NULL },
  { 1, zf_contact,              0, "contact", NULL },
  { 0, zf_unknown, 0, NULL, NULL } // terminator
};


const ZfTypeVariantEntry *ZfDevice::deviceTypeVariantsTable()
{
  return ZfTypeVariants;
}



void ZfDevice::getDeviceConfigurations(DeviceConfigurationsVector &aConfigurations, StatusCB aStatusCB)
{
  // check if current profile is one of the interchangeable ones
  const ZfTypeVariantEntry *currentVariant = deviceTypeVariantsTable();
  bool anyVariants = false;
  while (currentVariant && currentVariant->typeGroup!=0) {
    // look for current type in the list of variants
    if (getZfDeviceType()==currentVariant->zfDeviceType) {
      // create string from all other variants (same typeGroup), if any
      const ZfTypeVariantEntry *variant = deviceTypeVariantsTable();
      while (variant->typeGroup!=0) {
        if (variant->typeGroup==currentVariant->typeGroup) {
          if (variant->zfDeviceType!=getZfDeviceType()) anyVariants = true; // another variant than just myself
          string id;
          if (variant->configId)
            id = variant->configId; // has well-known configuration id
          else
            id = string_format("zf_%d", variant->zfDeviceType); // id generated from ZF type
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


string ZfDevice::getDeviceConfigurationId()
{
  const ZfTypeVariantEntry *currentVariant = deviceTypeVariantsTable();
  while (currentVariant && currentVariant->typeGroup!=0) {
    if (currentVariant->configId && getZfDeviceType()==currentVariant->zfDeviceType) {
      return currentVariant->configId; // has a well-known name, return that
    }
    currentVariant++;
  }
  // return a id generated from EEP
  return  string_format("zf_%d", getZfDeviceType());
}


ErrorPtr ZfDevice::switchConfiguration(const string aConfigurationId)
{
  ZfDeviceType newType = zf_unknown;
  int nt;
  if (sscanf(aConfigurationId.c_str(), "zf_%d", &nt)==1) {
    newType = (ZfDeviceType)nt;
  }
  // - find my typeGroup
  const ZfTypeVariantEntry *currentVariant = deviceTypeVariantsTable();
  while (currentVariant && currentVariant->typeGroup!=0) {
    if (getZfDeviceType()==currentVariant->zfDeviceType) {
      // this is my type group, now check if requested type is in my type group as well
      const ZfTypeVariantEntry *variant = deviceTypeVariantsTable();
      while (variant && variant->typeGroup!=0) {
        if (
          (variant->typeGroup==currentVariant->typeGroup) &&
          ((newType!=zf_unknown && newType==variant->zfDeviceType) || (newType==zf_unknown && variant->configId && aConfigurationId==variant->configId))
        ) {
          // prevent switching if new profile is same as current one
          if (variant->zfDeviceType==currentVariant->zfDeviceType) return ErrorPtr(); // we already have that type -> NOP
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
    if (newDev->deviceSettings && getZoneID()!=0) {
      hasNameOrZone = true;
      newDev->deviceSettings->zoneID = getZoneID();
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
  inherited(aVdcP, aDeviceType)
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
    ButtonBehaviourPtr bb = getButton(0);
    // pressing button now
    // - data==00 means "pressed", 01 means "released"
    bb->updateButtonState(aPacket->data==00);
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
    BinaryInputBehaviourPtr ib = getInput(0);
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
      ButtonBehaviourPtr bb = ButtonBehaviourPtr(new ButtonBehaviour(*newDev.get(),"")); // automatic id
      bb->setHardwareButtonConfig(0, buttonType_single, buttonElement_center, false, 0, 0); // fixed mode
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
      BinaryInputBehaviourPtr ib = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*newDev.get(),"contact"));
      ib->setHardwareInputConfig(binInpType_none, usage_room, true, Never, Never);
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

