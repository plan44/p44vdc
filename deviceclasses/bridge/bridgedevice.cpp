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
#include "binaryinputbehaviour.hpp"
#include "lightbehaviour.hpp"

using namespace p44;


BridgeDevice::BridgeDevice(BridgeVdc *aVdcP, const string &aBridgeDeviceId, const string &aBridgeDeviceConfig) :
  inherited((Vdc *)aVdcP),
  mBridgeDeviceRowID(0),
  mBridgeDeviceId(aBridgeDeviceId),
  mBridgeDeviceType(bridgedevice_unknown),
  mScene(INVALID_SCENE_NO),
  mProcessingBridgeNotification(false),
  mPreviousV(0)
{
  setColorClass(class_black_joker); // can be used to control any group
  // Config is:
  //  <type>[:<sceneno>[:undo]]
  const char *p = aBridgeDeviceConfig.c_str();
  string s;
  nextPart(p, s, ':');
  if (s=="onoff")
    mBridgeDeviceType = bridgedevice_onoff;
  else if (s=="fivelevel")
    mBridgeDeviceType = bridgedevice_fivelevel;
  else if (s=="sceneresponder")
    mBridgeDeviceType = bridgedevice_sceneresponder;
  else if (s=="scenecaller")
    mBridgeDeviceType = bridgedevice_scenecaller;
  else {
    LOG(LOG_ERR, "unknown bridge device type: %s", s.c_str());
  }
  if (mBridgeDeviceType!=bridgedevice_unknown) {
    if (mBridgeDeviceType==bridgedevice_sceneresponder || mBridgeDeviceType==bridgedevice_scenecaller) {
      mScene = ROOM_OFF; // a valid scene by default
      if (nextPart(p, s, ':')) {
        mScene = (SceneNo)atoi(s.c_str());
        if (nextPart(p, s, ':')) {
          mHandleUndo = s=="undo";
        }
      }
    }
    if (mBridgeDeviceType==bridgedevice_sceneresponder) {
      // scene responder needs a pseudo-input to inform bridge when scene call is detected
      BinaryInputBehaviourPtr i = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*this,"")); // automatic id
      if (mHandleUndo) {
        // signal remains set until undo call or other scene call
        i->setHardwareInputConfig(binInpType_none, usage_undefined, true, Never, Never);
      }
      else {
        // signal just pulses
        i->setHardwareInputConfig(binInpType_none, usage_undefined, true, 5*Second, Never, 0); // autoreset to 0 after 5 seconds
      }
      i->setGroup(group_black_variable);
      i->setHardwareName("scene responder");
      // responder must send input changes to bridges, no local processing!
      i->setBridgeExclusive();
      addBehaviour(i);
    }
    else {
      // level bridges and scene caller need a pseudo-button to emit scene calls to DS
      ButtonBehaviourPtr b = ButtonBehaviourPtr(new ButtonBehaviour(*this,"")); // automatic id
      b->setHardwareButtonConfig(0, buttonType_undefined, buttonElement_center, false, 0, 1); // mode not restricted
      b->setGroup(group_yellow_light); // pre-configure for light
      b->setHardwareName(mBridgeDeviceType==bridgedevice_fivelevel ? "5 scenes" : "on-off scenes");
      addBehaviour(b);
    }
    // pseudo-output (to capture scenes)
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
    case bridgedevice_scenecaller: return "scene calling bridge";
    case bridgedevice_sceneresponder: return "scene responding bridge";
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
    OLOG(LOG_DEBUG, "before processing bridge notification: default channel value = %.1f", mPreviousV);
  }
}


void BridgeDevice::didExamineNotificationFromConnection(VdcApiConnectionPtr aApiConnection)
{
  DBGOLOG(LOG_INFO, "didExamineNotificationFromConnection: domain=%d", aApiConnection->domain());
  mProcessingBridgeNotification = false;
}


bool BridgeDevice::prepareSceneCall(DsScenePtr aScene)
{
  DBGOLOG(LOG_INFO, "prepareSceneCall: scene=%s", VdcHost::sceneText(aScene->mSceneNo).c_str());
  if (mBridgeDeviceType==bridgedevice_sceneresponder && !mProcessingBridgeNotification) {
    BinaryInputBehaviourPtr i = getInput(0);
    if (aScene->mSceneNo==mScene) {
      // this is our trigger scene
      bool undo = aScene->mSceneCmd==scene_cmd_undo;
      OLOG(LOG_NOTICE, "detected scene %s %s", undo ? "undo" : "call", VdcHost::sceneText(mScene, false).c_str());
      if (i) {
        // Note: input behaviour is always set to bridge exclusive, so DS side will not see an event
        if (undo) {
          // undo call resets signal anyway
          OLOG(LOG_NOTICE, "- reset signal");
          i->updateInputState(0);
        }
        else {
          // call, raise input signal
          OLOG(LOG_NOTICE, "- raise signal (will be %s or other scene call)", mHandleUndo ? "cleared by undo" : "auto-reset in 5 secs");
          i->updateInputState(1);
        }
      }
    }
    else if (i->getCurrentState()>0 && mHandleUndo) {
      OLOG(LOG_NOTICE, "other scene called before undo -> reset signal");
      i->updateInputState(0);
    }
  }
  return inherited::prepareSceneCall(aScene);
}



void BridgeDevice::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  double newV = getChannelByType(channeltype_default)->getChannelValue();
  if (mProcessingBridgeNotification) {
    // this is an apply that originates from the bridge
    mProcessingBridgeNotification = false; // just make sure (didExamineNotificationFromConnection should clear it anyway)
    ButtonBehaviourPtr b = getButton(0);
    if (b && mBridgeDeviceType!=bridgedevice_sceneresponder) {
      bool global = b->getGroup()==group_black_variable;
      // only for output value following bridges
      if (mBridgeDeviceType==bridgedevice_scenecaller) {
        // local scene called is predefined
        bool newOn = newV>=50;
        if (newOn!=(mPreviousV>=50)) {
          OLOG(LOG_NOTICE, "default channel change to %d -> on=%d", (int)newV, (int)newOn);
          // on/off has changed from bridge side
          if (newOn) {
            // switched on: issue forced scene call
            OLOG(LOG_NOTICE, "- inject button callscene(%s) action", VdcHost::sceneText(mScene, global).c_str());
            b->sendAction(buttonActionMode_force, mScene);
          }
          else if (mHandleUndo) {
            // switched off: issue undo
            OLOG(LOG_NOTICE, "- inject button undoscene(%s) action", VdcHost::sceneText(mScene, global).c_str());
            b->sendAction(buttonActionMode_undo, mScene);
          }
        }
      }
      else {
        // local scene call depends on value match
        int prevPreset=-1;
        int newPreset=-1;
        double newSceneValue;
        // get the reference levels from relevant scenes and determine nearest levels
        ButtonScenesMap map(b->getButtonFunction(), global);
        // figure out the scene that will produce a level as near as possible to the value provided from the bridge
        double minPrevDiff;
        double minNewDiff;
        // - search off and preset1-4 (area on/off only for area buttons and on-off bridges)
        for (int i=0; i < (map.mArea>0 || mBridgeDeviceType==bridgedevice_onoff || global ? 2 : 5); i++) {
          SceneNo sn = map.mSceneClick[i];
          double scenevalue = -1;
          SimpleScenePtr scene = SimpleScenePtr();
          if (sn!=INVALID_SCENE_NO) {
            scene = boost::dynamic_pointer_cast<SimpleScene>(getScenes()->getScene(sn));
          }
          if (scene) {
            scenevalue = scene->value;
          }
          else if (i==0 && global) {
            scenevalue = 0; // assume a 0 value for the off scene, as reference for undo
          }
          if (scenevalue>=0) {
            SimpleScenePtr scene = boost::dynamic_pointer_cast<SimpleScene>(getScenes()->getScene(sn));
            if (scene) {
              double prevDiff = fabs(scenevalue-mPreviousV);
              if (prevPreset<0 || prevDiff<minPrevDiff) { minPrevDiff = prevDiff; prevPreset = i; }
              double newDiff =  fabs(scenevalue-newV);
              if (newPreset<0 || newDiff<minNewDiff) { newSceneValue = scenevalue; minNewDiff = newDiff; newPreset = i; }
            }
          }
        }
        OLOG(LOG_DEBUG, "global=%d, area=%d, prevLevel=%d, newLevel=%d, newSceneValue=%d", global, map.mArea, prevPreset, newPreset, (int)newSceneValue);
        OLOG(LOG_NOTICE, "default channel change to %d (adjusted to %d) originating from bridge", (int)newV, (int)newSceneValue);
        if (newPreset!=prevPreset && newPreset>=0) {
          // adjust the value to what it will be after the scene call returns to us from the room
          getChannelByType(channeltype_default)->syncChannelValue(newSceneValue);
          // figure out the scene call to make
          SceneNo actionID = map.mSceneClick[newPreset];
          VdcButtonActionMode actionMode = buttonActionMode_normal;
          if (global && actionID==INVALID_SCENE_NO && newPreset==0) {
            // no specific reset scene -> undo the activation instead
            actionMode = buttonActionMode_undo;
            actionID = map.mSceneClick[1];
          }
          // emit the scene call
          OLOG(LOG_NOTICE,
            "- preset changes from %d to %d -> inject button %sscene(%s) action",
            prevPreset, newPreset,
            actionMode==buttonActionMode_undo ? "undo" : "call",
            VdcHost::sceneText(actionID, global).c_str()
          );
          b->sendAction(actionMode, actionID);
        }
        else {
          OLOG(LOG_INFO, "- preset (%d) did not change -> no button action sent", prevPreset);
        }
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
  switch (mBridgeDeviceType) {
    case bridgedevice_onoff:
      string_format_append(s, "\n- bridging onoff room state"); break;
    case bridgedevice_fivelevel:
      string_format_append(s, "\n- bridging off,25,50,75,100%% level room state"); break;
    case bridgedevice_scenecaller:
      string_format_append(s, "\n- call scene '%s' when bridged onoff goes on%s", VdcHost::sceneText(mScene).c_str(), mHandleUndo ? ", undo when off" : ""); break;
    case bridgedevice_sceneresponder:
      string_format_append(s, "\n- activate contact when detecting scene '%s'%s", VdcHost::sceneText(mScene).c_str(), mHandleUndo ? ", deactivate at undo" : ""); break;
    default: break;
  }
  return s;
}


string BridgeDevice::bridgeAsHint()
{
  switch (mBridgeDeviceType) {
    case bridgedevice_onoff:
    case bridgedevice_scenecaller:
      return "on-off";
    case bridgedevice_fivelevel: 
      return "level-control";
    case bridgedevice_sceneresponder:
      return "no-output";
    default:
      return inherited::bridgeAsHint();
  }
}



#endif // ENABLE_JSONBRIDGEAPI
