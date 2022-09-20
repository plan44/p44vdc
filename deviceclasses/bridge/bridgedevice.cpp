//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2022 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "bridgedevice.hpp"

#if ENABLE_JSONBRIDGEAPI

#include "bridgevdc.hpp"

#include "buttonbehaviour.hpp"
#include "lightbehaviour.hpp"

using namespace p44;


BridgeDevice::BridgeDevice(BridgeVdc *aVdcP, const string &aBridgeDeviceId, const string &aBridgeDeviceConfig) :
  inherited((Vdc *)aVdcP),
  mBridgeDeviceRowID(0),
  mBridgeDeviceId(aBridgeDeviceId),
  mBridgeDeviceType(bridgedevice_unknown)
{
  // Config is:
  //  <type>
  if (aBridgeDeviceConfig=="onoff")
    mBridgeDeviceType = bridgedevice_onoff;
  if (aBridgeDeviceConfig=="fivelevel")
    mBridgeDeviceType = bridgedevice_fivelevel;
  else {
    LOG(LOG_ERR, "unknown bridge device type: %s", aBridgeDeviceConfig.c_str());
  }
  if (mBridgeDeviceType==bridgedevice_onoff || mBridgeDeviceType==bridgedevice_fivelevel) {
    // scene emitting button
    ButtonBehaviourPtr b = ButtonBehaviourPtr(new ButtonBehaviour(*this,"")); // automatic id
    b->setHardwareButtonConfig(0, buttonType_undefined, buttonElement_center, false, 0, 1); // mode not restricted
    b->setGroup(group_yellow_light); // pre-configure for light
    b->setHardwareName(mBridgeDeviceType==bridgedevice_fivelevel ? "5 scenes" : "on-off scenes");
    addBehaviour(b);
    // pseudo-output (to capture room scenes)
    // - use light settings for the scene table
    installSettings(DeviceSettingsPtr(new LightDeviceSettings(*this)));
    // - but we do not need a light behaviour, simple output will do
    OutputBehaviourPtr o = OutputBehaviourPtr(new OutputBehaviour(*this));
    if (mBridgeDeviceType==bridgedevice_fivelevel) {
      // dimmable
      o->setHardwareOutputConfig(outputFunction_dimmer, outputmode_gradual, usage_undefined, false, -1);
    }
    else {
      o->setHardwareOutputConfig(outputFunction_switch, outputmode_binary, usage_undefined, false, -1);
    }
    addBehaviour(o);
  }
  deriveDsUid();
}


BridgeDevice::~BridgeDevice()
{
}


bool BridgeDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Nothing to do to identify for now
  return true; // simple identification, callback will not be called
}


BridgeVdc &BridgeDevice::getBridgeVdc()
{
  return *(static_cast<BridgeVdc *>(mVdcP));
}


void BridgeDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  // clear learn-in data from DB
  if (mBridgeDeviceRowID) {
    if(getBridgeVdc().mDb.executef("DELETE FROM bridgedevices WHERE rowid=%lld", mBridgeDeviceRowID)!=SQLITE_OK) {
      OLOG(LOG_ERR, "Error deleting bridgedevice: %s", getBridgeVdc().mDb.error()->description().c_str());
    }
  }
  // disconnection is immediate, so we can call inherited right now
  inherited::disconnect(aForgetParams, aDisconnectResultHandler);
}


string BridgeDevice::modelName()
{
  switch (mBridgeDeviceType) {
    case bridgedevice_onoff: return "on-off bridge";
    case bridgedevice_fivelevel: return "5-level bridge";
    default: break;
  }
  return "";
}



bool BridgeDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("brdg", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


void BridgeDevice::initializeDevice(StatusCB aCompletedCB, bool aFactoryReset)
{
  // done
  inherited::initializeDevice(aCompletedCB, aFactoryReset);
}


ErrorPtr BridgeDevice::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  if (false) {
    /* maybe add methods later */
  }
  else {
    return inherited::handleMethod(aRequest, aMethod, aParams);
  }
}


void BridgeDevice::deriveDsUid()
{
  // vDC implementation specific UUID:
  //   UUIDv5 with name = classcontainerinstanceid::evaluatorID
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  string s = mVdcP->vdcInstanceIdentifier();
  s += "::" + mBridgeDeviceId;
  mDSUID.setNameInSpace(s, vdcNamespace);
}


string BridgeDevice::description()
{
  string s = inherited::description();
  if (mBridgeDeviceType==bridgedevice_onoff)
    string_format_append(s, "\n- bridging onoff room state");
  if (mBridgeDeviceType==bridgedevice_fivelevel)
    string_format_append(s, "\n- bridging off,25,50,75,100%% level room state");
  return s;
}


string BridgeDevice::getBridgeDeviceType()
{
  switch (mBridgeDeviceType) {
    case bridgedevice_onoff: return "onOff-scenes";
    case bridgedevice_fivelevel: return "fivelevel-scenes";
    default:
      return "unknown";
  }
}


// MARK: - property access

enum {
  bridgeDeviceType_key,
  numProperties
};

static char bridgeDevice_key;


int BridgeDevice::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // Note: only add my own count when accessing root level properties!!
  if (aParentDescriptor->isRootOfObject()) {
    // Accessing properties at the Device (root) level, add mine
    return inherited::numProps(aDomain, aParentDescriptor)+numProperties;
  }
  // just return base class' count
  return inherited::numProps(aDomain, aParentDescriptor);
}


PropertyDescriptorPtr BridgeDevice::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numProperties] = {
    { "x-p44-bridgeDeviceType", apivalue_string, bridgeDeviceType_key, OKEY(bridgeDevice_key) },
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
bool BridgeDevice::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(bridgeDevice_key)) {
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        case bridgeDeviceType_key: aPropValue->setStringValue(getBridgeDeviceType()); return true;
      }
    }
    else {
      // write properties
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}



#endif // ENABLE_JSONBRIDGEAPI
