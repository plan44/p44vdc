//  SPDX-License-Identifier: GPL-3.0-or-later
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
  mBridgeDeviceType(bridgedevice_unknown),
  mProcessingBridgeNotification(false),
  mPreviousV(0)
{
  setColorClass(class_black_joker); // can be used to control any group
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
    // - standard scene device settings
    DeviceSettingsPtr s = DeviceSettingsPtr(new SceneDeviceSettings(*this));
    s->mAllowBridging = true; // bridging allowed from start (that's the purpose of these devices)
    installSettings(s);
    // - but we do not need a light behaviour, simple output will do
    OutputBehaviourPtr o = OutputBehaviourPtr(new OutputBehaviour(*this));
    // - add a default channel
    o->addChannel(PercentageLevelChannelPtr(new PercentageLevelChannel(*o,"bridgedlevel")));
    o->setGroupMembership(group_yellow_light, true); // default to light as well
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
  if (getClassColoredIcon("brdg", getDominantColorClass(), aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


void BridgeDevice::initializeDevice(StatusCB aCompletedCB, bool aFactoryReset)
{
  // done
  inherited::initializeDevice(aCompletedCB, aFactoryReset);
}


void BridgeDevice::willExamineNotificationFromConnection(VdcApiConnectionPtr aApiConnection)
{
  DBGOLOG(LOG_INFO, "willExamineNotificationFromConnection: domain=%d", aApiConnection->domain());
  mProcessingBridgeNotification = aApiConnection->domain()==BRIDGE_DOMAIN;
  if (mProcessingBridgeNotification) {
    // capture the current output value for comparison with new one the notification might set
    mPreviousV = getChannelByType(channeltype_default)->getChannelValue();
  }
}


void BridgeDevice::didExamineNotificationFromConnection(VdcApiConnectionPtr aApiConnection)
{
  DBGOLOG(LOG_INFO, "didExamineNotificationFromConnection: domain=%d", aApiConnection->domain());
  mProcessingBridgeNotification = false;
}


void BridgeDevice::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  double newV = getChannelByType(channeltype_default)->getChannelValue();
  if (mProcessingBridgeNotification) {
    // this is an apply that originates from the bridge
    mProcessingBridgeNotification = false; // just make sure (didExamineNotificationFromConnection should clear it anyway)
    ButtonBehaviourPtr b = getButton(0);
    if (b) {
      int prevPreset=-1;
      int newPreset=-1;
      double newSceneValue;
      // get the reference levels from relevant scenes and determine nearest levels
      ButtonScenesMap map(b->getButtonFunction(), false);
      // figure out the scene that will produce a level as near as possible to the value provided from the bridge
      double minPrevDiff;
      double minNewDiff;
      // search off and preset1-4 (area on/off only for area buttons and on-off bridges)
      for (int i=0; i < (map.mArea>0 || mBridgeDeviceType==bridgedevice_onoff ? 2 : 5); i++) {
        SceneNo sn = map.mSceneClick[i];
        if (sn!=INVALID_SCENE_NO) {
          SimpleScenePtr scene = boost::dynamic_pointer_cast<SimpleScene>(getScenes()->getScene(sn));
          if (scene) {
            double prevDiff = fabs(scene->value-mPreviousV);
            if (prevPreset<0 || prevDiff<minPrevDiff) { minPrevDiff = prevDiff; prevPreset = i; }
            double newDiff =  fabs(scene->value-newV);
            if (newPreset<0 || newDiff<minNewDiff) { newSceneValue = scene->value; minNewDiff = newDiff; newPreset = i; }
          }
        }
      }
      OLOG(LOG_DEBUG, "area=%d, prevLevel=%d, newLevel=%d, newSceneValue=%d", map.mArea, prevPreset, newPreset, (int)newSceneValue);
      OLOG(LOG_NOTICE, "default channel change to %d (adjusted to %d) originating from bridge", (int)newV, (int)newSceneValue);
      if (newPreset!=prevPreset && newPreset>=0) {
        SceneNo actionID = map.mSceneClick[newPreset];
        // adjust the value to what it will be after the scene call returns to us from the room
        getChannelByType(channeltype_default)->syncChannelValue(newSceneValue);
        OLOG(LOG_NOTICE,
          "- preset changes from %d to %d -> inject button callscene(%s) action",
          prevPreset, newPreset, VdcHost::sceneText(actionID, false).c_str()
        );
        b->sendAction(buttonActionMode_normal, actionID);
      }
      else {
        OLOG(LOG_INFO, "- preset (%d) did not change -> no button action", prevPreset);
      }
    }
  }
  else {
    OLOG(LOG_INFO, "default channel change to %d - NOT caused by bridged device", (int)newV);
  }
  inherited::applyChannelValues(aDoneCB, aForDimming);
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


string BridgeDevice::bridgeAsHint()
{
  switch (mBridgeDeviceType) {
    case bridgedevice_onoff: return "on-off";
    case bridgedevice_fivelevel: return "level-control";
    default: return inherited::bridgeAsHint();
  }
}



#endif // ENABLE_JSONBRIDGEAPI
