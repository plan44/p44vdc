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
#include "sensorbehaviour.hpp"
#include "lightbehaviour.hpp"

using namespace p44;

// MARK: - BridgeDevice

#define AUTORESET_DELAY (5*Second)

BridgeDevice::BridgeDevice(BridgeVdc *aVdcP, const string &aBridgeDeviceId, const string &aBridgeDeviceConfig, DsGroup aGroup, bool aAllowBridging) :
  inherited((Vdc *)aVdcP),
  mBridgeDeviceRowID(0),
  mBridgeDeviceId(aBridgeDeviceId),
  mBridgeDeviceType(bridgedevice_unknown),
  mActivateScene(INVALID_SCENE_NO),
  mResetScene(INVALID_SCENE_NO),
  mResetMode(reset_auto), // default to autoreset
  mProcessingBridgeNotification(false),
  mPreviousV(0)
{
  setColorClass(class_black_joker); // can be used to control any group
  // Config is:
  //  <type>[:<on sceneno>[:<off sceneno>][:<resetmode>]
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
  else if (s=="dimmerdial")
    mBridgeDeviceType = bridgedevice_dimmerdial;
  else {
    LOG(LOG_ERR, "unknown bridge device type: %s", s.c_str());
  }
  if (mBridgeDeviceType!=bridgedevice_unknown) {
    if (mBridgeDeviceType==bridgedevice_sceneresponder || mBridgeDeviceType==bridgedevice_scenecaller) {
      mActivateScene = ROOM_ON; // a valid scene by default
      mResetScene = ROOM_OFF; // a valid scene by default
      if (nextPart(p, s, ':')) {
        // activation
        mActivateScene = (SceneNo)atoi(s.c_str());
        if (mActivateScene>INVALID_SCENE_NO) mActivateScene=INVALID_SCENE_NO;
        bool haveReset = false;
        while (nextPart(p, s, ':')) {
          // legacy with only 2 params:
          if (s=="other") {
            if (!haveReset) mResetScene = INVALID_SCENE_NO;
            mResetMode = reset_other;
            break; // resetmode terminates config
          }
          else if (s=="autoreset") {
            mResetScene = INVALID_SCENE_NO; // autoreset cannot have a reset scene
            mResetMode = reset_auto;
            break; // resetmode terminates config
          }
          else if (s=="undo") {
            if (!haveReset) mResetScene = INVALID_SCENE_NO;
            mResetMode = reset_undo;
            break; // resetmode terminates config
          }
          else if (s=="none") {
            if (!haveReset) mResetScene = INVALID_SCENE_NO;
            mResetMode = reset_undo;
            break; // resetmode terminates config
          }
          else if (!haveReset) {
            mResetScene = (SceneNo)atoi(s.c_str());
            if (mResetScene>INVALID_SCENE_NO) mResetScene=INVALID_SCENE_NO;
            mResetMode = reset_none;
            haveReset = true;
          }
        }
      }
    }
    if (mBridgeDeviceType==bridgedevice_sceneresponder) {
      // scene responder needs a pseudo-input to inform bridge when scene call is detected
      BinaryInputBehaviourPtr i = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*this,"")); // automatic id
      if (mResetMode==reset_auto) {
        // signal just pulses (autoresets)
        i->setHardwareInputConfig(binInpType_none, usage_undefined, true, AUTORESET_DELAY, Never, 0); // autoreset
      }
      else {
        // signal remains set until reset according to mResetMode
        i->setHardwareInputConfig(binInpType_none, usage_undefined, true, Never, Never);
      }
      if (aGroup==group_undefined) aGroup = group_black_variable; // default to joker/app
      i->setGroup(aGroup);
      i->setHardwareName("scene responder");
      // responder must send input changes to bridges, no local processing!
      i->setBridgeExclusive();
      addBehaviour(i);
    }
    else if (mBridgeDeviceType==bridgedevice_dimmerdial) {
      // dimmer dial bridge needs a dimmer sensor (P44 extension, not DS-compatible) to emit room dimming values
      SensorBehaviourPtr s = SensorBehaviourPtr(new SensorBehaviour(*this,"")); // automatic id
      s->setHardwareSensorConfig(sensorType_percent, usage_user, 0, 100, 0.25, 0.5*Second, 0, 10*Second, true);
      if (aGroup==group_undefined) aGroup = group_yellow_light; // default to light
      s->setGroup(aGroup);
      s->setSensorFunc(sensorFunc_dimmer_room); // default to room dimmer
      s->setSensorChannel(channeltype_default); // default channel
      s->setHardwareName("dimmer dial");
      addBehaviour(s);
    }
    else {
      // level bridges and scene caller need a pseudo-button to emit scene calls to DS
      ButtonBehaviourPtr b = ButtonBehaviourPtr(new ButtonBehaviour(*this,"")); // automatic id
      b->setHardwareButtonConfig(0, buttonType_undefined, buttonElement_center, false, 0, 1); // mode not restricted
      if (aGroup==group_undefined) aGroup = group_yellow_light; // default to light
      b->setGroup(aGroup);
      b->setHardwareName(mBridgeDeviceType==bridgedevice_fivelevel ? "5 scenes" : "on-off scenes");
      addBehaviour(b);
    }
    // pseudo-output (to capture scenes)
    // - standard scene device settings
    DeviceSettingsPtr s = DeviceSettingsPtr(new SceneDeviceSettings(*this));
    s->mAllowBridging = aAllowBridging; // bridging allowed from start?
    installSettings(s);
    // - but we do not need a light behaviour, simple output will do
    OutputBehaviourPtr o = OutputBehaviourPtr(new OutputBehaviour(*this));
    // - add a default channel
    o->addChannel(PercentageLevelChannelPtr(new PercentageLevelChannel(*o,"bridgedlevel")));
    o->setGroupMembership(aGroup, true); // same group as for button
    if (mBridgeDeviceType==bridgedevice_fivelevel || mBridgeDeviceType==bridgedevice_dimmerdial) {
      // dimmable
      o->setHardwareOutputConfig(outputFunction_dimmer, outputmode_gradual, usage_undefined, false, -1);
    }
    else {
      // on-off
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
    ErrorPtr err = getBridgeVdc().mDb.prefixedExecute("DELETE FROM $PREFIX_bridgedevices WHERE rowid=%lld", mBridgeDeviceRowID);
    if (Error::notOK(err)) {
      OLOG(LOG_ERR, "Error deleting bridgedevice: %s", err->text());
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
    case bridgedevice_dimmerdial: return "room/area dimmer bridge";
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
  if (!aApiConnection) return;
  DBGOLOG(LOG_INFO, "willExamineNotificationFromConnection: domain=%d", aApiConnection->domain());
  mProcessingBridgeNotification = aApiConnection->domain()==BRIDGE_DOMAIN;
  // always capture the current output value for comparison with new one the notification might set
  mPreviousV = getChannelByType(channeltype_default)->getChannelValue();
  if (mProcessingBridgeNotification) {
    OLOG(LOG_DEBUG, "before processing bridge notification: default channel value = %.1f", mPreviousV);
  }
}


void BridgeDevice::didExamineNotificationFromConnection(VdcApiConnectionPtr aApiConnection)
{
  if (!aApiConnection) return;
  DBGOLOG(LOG_INFO, "didExamineNotificationFromConnection: domain=%d", aApiConnection->domain());
  mProcessingBridgeNotification = false;
}


void BridgeDevice::saveScene(SceneNo aSceneNo)
{
  if (mBridgeDeviceType==bridgedevice_scenecaller) {
    // bridge caller cannot apply scenes and should not modify them (they are dontCare by default)
    OLOG(LOG_INFO, "scene caller bridge does not save scene values");
    return;
  }
  // save normally
  inherited::saveScene(aSceneNo);
}



bool BridgeDevice::prepareSceneCall(DsScenePtr aScene)
{
  DBGOLOG(LOG_INFO, "prepareSceneCall: scene=%s", VdcHost::sceneText(aScene->mSceneNo).c_str());
  if (mBridgeDeviceType==bridgedevice_scenecaller) {
    // scene caller does not apply scenes, but output follows activation/deactivation state
    int newval = -1; // no change
    if (aScene->mSceneNo==mActivateScene) {
      OLOG(LOG_NOTICE, "- activation scene called -> set bridged value to max");
      newval = getChannelByType(channeltype_default)->getMax();
    }
    else if (aScene->mSceneNo==mResetScene) {
      OLOG(LOG_NOTICE, "- reset scene called -> set bridged value to 0");
      newval = 0;
    }
    else if (mResetMode==reset_other) {
      OLOG(LOG_NOTICE, "- another scene called (%s) -> set bridged value to 0", VdcHost::sceneText(aScene->mSceneNo).c_str());
      newval = 0;
    }
    if (newval>=0) {
      // Note: we do not "apply" the new channel value, but just "observe" it to
      //   become changed for "external" reasons and sync that.
      //   We need to call reportOutputState() to inform the bridge, though.
      getChannelByType(channeltype_default)->syncChannelValue(newval, true, true); // always, derived
      getOutput()->reportOutputState();
    }
    // suppress processing the scene call locally
    return false;
  }
  else if (mBridgeDeviceType==bridgedevice_sceneresponder && !mProcessingBridgeNotification) {
    bool undo = aScene->mSceneCmd==scene_cmd_undo;
    BinaryInputBehaviourPtr i = getInput(0);
    if (i) {
      if (aScene->mSceneNo==mActivateScene) {
        // this is our trigger scene
        // Note: input behaviour is always set to bridge exclusive, so DS side will not see an event
        if (undo && mResetMode==reset_undo) {
          OLOG(LOG_NOTICE, "- activation scene undone -> reset signal");
          i->updateInputState(0);
        }
        else {
          // call, raise input signal
          OLOG(LOG_NOTICE, "- activation scene called -> raise signal");
          i->updateInputState(1);
        }
      }
      else if (aScene->mSceneNo==mResetScene && !undo) {
        // our reset scene, resets the state
        OLOG(LOG_NOTICE, "reset scene called -> reset signal");
        i->updateInputState(0);
      }
      else if (mResetMode!=reset_auto) {
        // not autoreset: any another scene call resets the state
        OLOG(LOG_NOTICE, "another scene called (%s) -> reset signal", VdcHost::sceneText(aScene->mSceneNo).c_str());
        i->updateInputState(0);
      }
    }
  }
  return inherited::prepareSceneCall(aScene);
}


bool BridgeDevice::prepareSceneApply(DsScenePtr aScene)
{
  if (mBridgeDeviceType==bridgedevice_dimmerdial) return false; // prevent applying ANY scene to dimmer (but channel value are already loaded!)
  return inherited::prepareSceneApply(aScene);
}



void BridgeDevice::resetSignalChannel()
{
  OLOG(LOG_NOTICE, "auto-resetting scene caller's controlling output");
  getChannelByType(channeltype_default)->syncChannelValue(0, true);
  getOutput()->reportOutputState();
}


void BridgeDevice::updateDimmer(double aNewValue)
{
  SensorBehaviourPtr s = getSensor(0);
  if (s) s->updateSensorValue(aNewValue);
}



void BridgeDevice::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  ChannelBehaviourPtr ch = getChannelByType(channeltype_default);
  if (ch && ch->needsApplying()) {
    double newV = ch->getChannelValue();
    // all
    if (mBridgeDeviceType==bridgedevice_dimmerdial && newV!=mPreviousV) {
      // just forward to the dimmer dial sensor, but unwind stack before
      MainLoop::currentMainLoop().executeNow(boost::bind(&BridgeDevice::updateDimmer, this, newV));
    }
    else if (mProcessingBridgeNotification) {
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
            mResetSignalTicket.cancel();
            OLOG(LOG_NOTICE, "default channel change to %d -> on=%d", (int)newV, (int)newOn);
            // on/off has changed from bridge side
            if (newOn) {
              // switched on: issue forced scene call
              OLOG(LOG_NOTICE, "- activate: inject callscene(%s)", VdcHost::sceneText(mActivateScene, global).c_str());
              b->sendAction(buttonActionMode_force, mActivateScene);
              if (mResetMode==reset_auto) {
                // auto-reset bridge side
                mResetSignalTicket.executeOnce(boost::bind(&BridgeDevice::resetSignalChannel, this), AUTORESET_DELAY);
              }
            }
            else {
              if (mResetMode==reset_undo) {
                // set==reset -> undo
                OLOG(LOG_NOTICE, "- deactivate: inject undoscene(%s)", VdcHost::sceneText(mActivateScene, global).c_str());
                b->sendAction(buttonActionMode_undo, mActivateScene);
              }
              else if (mResetScene!=INVALID_SCENE_NO) {
                // send a specific reset scene
                OLOG(LOG_NOTICE, "- deactivate: inject callscene(%s)", VdcHost::sceneText(mResetScene, global).c_str());
                b->sendAction(buttonActionMode_force, mResetScene);
              }
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
    ch->channelValueApplied();
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
  static const char* resetModeDescs[num_resetmodes] = {
    "no",
    "automatic (timeout)",
    "other scene call",
    "scene undo"
  };

  string s = inherited::description();
  switch (mBridgeDeviceType) {
    case bridgedevice_onoff:
      string_format_append(s, "\n- bridging onoff room state");
      break;
    case bridgedevice_dimmerdial:
      string_format_append(s, "\n- distributing level as room/area dimmer value");
      break;
    case bridgedevice_fivelevel:
      string_format_append(s, "\n- bridging off,25,50,75,100%% level room state");
      break;
    case bridgedevice_scenecaller:
      string_format_append(s,
        "\n- call scene '%s' when bridged onoff goes on, '%s' when off\n- other bridged state reset: %s",
        VdcHost::sceneText(mActivateScene).c_str(),
        VdcHost::sceneText(mResetScene).c_str(),
        resetModeDescs[mResetMode]
      );
      break;
    case bridgedevice_sceneresponder:
      string_format_append(s,
        "\n- activate contact when detecting scene '%s', deactivate on '%s'\n- other deactivation: %s",
        VdcHost::sceneText(mActivateScene).c_str(),
        VdcHost::sceneText(mResetScene).c_str(),
        resetModeDescs[mResetMode]
      );
      break;
    default:
      break;
  }
  return s;
}


string BridgeDevice::bridgeAsHint()
{
  switch (mBridgeDeviceType) {
    case bridgedevice_onoff:
    case bridgedevice_scenecaller:
      return "on-off";
    case bridgedevice_dimmerdial:
    case bridgedevice_fivelevel:
      return "level-control";
    case bridgedevice_sceneresponder:
      return "no-output";
    default:
      return inherited::bridgeAsHint();
  }
}



#endif // ENABLE_JSONBRIDGEAPI
