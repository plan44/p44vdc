//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2024 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "proxydevice.hpp"

#if ENABLE_PROXYDEVICES

#include "proxyvdc.hpp"

using namespace p44;


ProxyDevice::ProxyDevice(ProxyVdc *aVdcP, JsonObjectPtr aProxyDeviceInfo) :
  inherited((Vdc *)aVdcP)
{
//  setColorClass(class_black_joker); // can be used to control any group
//  // Config is:
//  //  <type>
//  if (aBridgeDeviceConfig=="onoff")
//    mBridgeDeviceType = bridgedevice_onoff;
//  if (aBridgeDeviceConfig=="fivelevel")
//    mBridgeDeviceType = bridgedevice_fivelevel;
//  else {
//    LOG(LOG_ERR, "unknown bridge device type: %s", aBridgeDeviceConfig.c_str());
//  }
//  if (mBridgeDeviceType==bridgedevice_onoff || mBridgeDeviceType==bridgedevice_fivelevel) {
//    // scene emitting button
//    ButtonBehaviourPtr b = ButtonBehaviourPtr(new ButtonBehaviour(*this,"")); // automatic id
//    b->setHardwareButtonConfig(0, buttonType_undefined, buttonElement_center, false, 0, 1); // mode not restricted
//    b->setGroup(group_yellow_light); // pre-configure for light
//    b->setHardwareName(mBridgeDeviceType==bridgedevice_fivelevel ? "5 scenes" : "on-off scenes");
//    addBehaviour(b);
//    // pseudo-output (to capture room scenes)
//    // - standard scene device settings
//    DeviceSettingsPtr s = DeviceSettingsPtr(new SceneDeviceSettings(*this));
//    s->mAllowBridging = true; // bridging allowed from start (that's the purpose of these devices)
//    installSettings(s);
//    // - but we do not need a light behaviour, simple output will do
//    OutputBehaviourPtr o = OutputBehaviourPtr(new OutputBehaviour(*this));
//    // - add a default channel
//    o->addChannel(PercentageLevelChannelPtr(new PercentageLevelChannel(*o,"bridgedlevel")));
//    o->setGroupMembership(group_yellow_light, true); // default to light as well
//    if (mBridgeDeviceType==bridgedevice_fivelevel) {
//      // dimmable
//      o->setHardwareOutputConfig(outputFunction_dimmer, outputmode_gradual, usage_undefined, false, -1);
//    }
//    else {
//      o->setHardwareOutputConfig(outputFunction_switch, outputmode_binary, usage_undefined, false, -1);
//    }
//    addBehaviour(o);
//  }
  deriveDsUid();
}


ProxyDevice::~ProxyDevice()
{
}


bool ProxyDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Nothing to do to identify for now
  return true; // simple identification, callback will not be called
}


ProxyVdc &ProxyDevice::getProxyVdc()
{
  return *(static_cast<ProxyVdc *>(mVdcP));
}


string ProxyDevice::deviceTypeIdentifier() const
{
  // TODO: return proxied identifier
  return inherited::deviceTypeIdentifier();
}



string ProxyDevice::modelName()
{
  // TODO: return proxied model name
  return "";
}


string ProxyDevice::webuiURLString()
{
  return getVdc().webuiURLString(); // devices have the config URL of the vdc, which is the webui of the remote P44 unit
}



bool ProxyDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  // TODO: actually populate mIconBaseName
  // Assuming we have the same icon files as on the proxied device
  if (getClassColoredIcon(mIconBaseName.c_str(), getDominantColorClass(), aIcon, aWithData, aResolutionPrefix))
    return true;
  // fall back to generic proxy icon
  else if (getClassColoredIcon("proxy", getDominantColorClass(), aIcon, aWithData, aResolutionPrefix))
    return true;
  // fall back to default
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


void ProxyDevice::initializeDevice(StatusCB aCompletedCB, bool aFactoryReset)
{
  // TODO: actually do something

  // done
  inherited::initializeDevice(aCompletedCB, aFactoryReset);
}


void ProxyDevice::deriveDsUid()
{
  // TODO: possibly nothing, because dSUID is taken from proxied device
}


string ProxyDevice::description()
{
  // TODO: possibly forward original device's description
  string s = inherited::description();
//  if (mBridgeDeviceType==bridgedevice_onoff)
//    string_format_append(s, "\n- bridging onoff room state");
//  if (mBridgeDeviceType==bridgedevice_fivelevel)
//    string_format_append(s, "\n- bridging off,25,50,75,100%% level room state");
  return s;
}


#endif // ENABLE_PROXYDEVICES
