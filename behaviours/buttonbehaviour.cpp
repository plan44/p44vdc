//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#define FOCUSLOGLEVEL 0

#include "buttonbehaviour.hpp"
#include "outputbehaviour.hpp"


using namespace p44;

#if ENABLE_LOCALCONTROLLER || ENABLE_JSONBRIDGEAPI
// MARK: - ButtonScenesMap

ButtonScenesMap::ButtonScenesMap(DsButtonFunc aButtonFunc, bool aGlobal)
{
  // defaults
  mArea = 0;
  for (int i=0; i<5; i++) mSceneClick[i] = INVALID_SCENE_NO;
  // analyze
  if (aGlobal) {
    switch (aButtonFunc) {
      case buttonFunc_alarm:
        mSceneClick[1] = ALARM1;
        break;
      case buttonFunc_panic:
        mSceneClick[1] = PANIC;
        break;
      case buttonFunc_leave:
        mSceneClick[0] = PRESENT;
        mSceneClick[1] = ABSENT;
        break;
      case buttonFunc_doorbell:
        mSceneClick[1] = BELL1;
        break;
      default:
        break;
    }
  }
  else {
    switch (aButtonFunc) {
      case buttonFunc_area1_preset0x:
        mArea = 1;
        mSceneClick[1] = AREA_1_ON;
        mSceneClick[0] = AREA_1_OFF;
        goto preset0x;
      case buttonFunc_area2_preset0x:
        mArea = 2;
        mSceneClick[1] = AREA_2_ON;
        mSceneClick[0] = AREA_2_OFF;
        goto preset0x;
      case buttonFunc_area3_preset0x:
        mArea = 3;
        mSceneClick[1] = AREA_3_ON;
        mSceneClick[0] = AREA_3_OFF;
        goto preset0x;
      case buttonFunc_area4_preset0x:
        mArea = 4;
        mSceneClick[1] = AREA_4_ON;
        mSceneClick[0] = AREA_4_OFF;
        goto preset0x;
      case buttonFunc_area1_preset1x:
        mArea = 1;
        mSceneClick[1] = AREA_1_ON;
        mSceneClick[0] = AREA_1_OFF;
        goto preset1x;
      case buttonFunc_area2_preset2x:
        mArea = 2;
        mSceneClick[1] = AREA_2_ON;
        mSceneClick[0] = AREA_2_OFF;
        goto preset2x;
      case buttonFunc_area3_preset3x:
        mArea = 3;
        mSceneClick[1] = AREA_3_ON;
        mSceneClick[0] = AREA_3_OFF;
        goto preset3x;
      case buttonFunc_area4_preset4x:
        mArea = 4;
        mSceneClick[1] = AREA_4_ON;
        mSceneClick[0] = AREA_4_OFF;
        goto preset4x;
      case buttonFunc_room_preset0x:
        mSceneClick[1] = ROOM_ON;
        mSceneClick[0] = ROOM_OFF;
      preset0x:
        mSceneClick[2] = PRESET_2;
        mSceneClick[3] = PRESET_3;
        mSceneClick[4] = PRESET_4;
        break;
      case buttonFunc_room_preset1x:
        mSceneClick[1] = PRESET_11;
        mSceneClick[0] = ROOM_OFF;
      preset1x:
        mSceneClick[2] = PRESET_12;
        mSceneClick[3] = PRESET_13;
        mSceneClick[4] = PRESET_14;
        break;
      case buttonFunc_room_preset2x:
        mSceneClick[1] = PRESET_21;
        mSceneClick[0] = ROOM_OFF;
      preset2x:
        mSceneClick[2] = PRESET_22;
        mSceneClick[3] = PRESET_23;
        mSceneClick[4] = PRESET_24;
        break;
      case buttonFunc_room_preset3x:
        mSceneClick[1] = PRESET_31;
        mSceneClick[0] = ROOM_OFF;
      preset3x:
        mSceneClick[2] = PRESET_32;
        mSceneClick[3] = PRESET_33;
        mSceneClick[4] = PRESET_34;
        break;
      case buttonFunc_room_preset4x:
        mSceneClick[1] = PRESET_41;
        mSceneClick[0] = ROOM_OFF;
      preset4x:
        mSceneClick[2] = PRESET_42;
        mSceneClick[3] = PRESET_43;
        mSceneClick[4] = PRESET_44;
        break;
      default:
        break;
    }
  }
}

#endif // ENABLE_LOCALCONTROLLER || ENABLE_JSONBRIDGEAPI


// MARK: - ButtonBehaviour

ButtonBehaviour::ButtonBehaviour(Device &aDevice, const string aId) :
  inherited(aDevice, aId),
  // persistent settings
  mButtonGroup(group_yellow_light),
  mButtonMode(buttonMode_inactive), // none by default, hardware should set a default matching the actual HW capabilities
  mFixedButtonMode(buttonMode_inactive), // by default, mode can be set. Hardware may fix the possible mode
  mButtonChannel(channeltype_default), // by default, buttons act on default channel
  mButtonFunc(buttonFunc_room_preset0x), // act as room button by default
  mSetsLocalPriority(false),
  mClickType(ct_none),
  mActionMode(buttonActionMode_none),
  mActionId(0),
  mButtonPressed(false),
  mLastAction(Never),
  mCallsPresent(false),
  mButtonActionMode(buttonActionMode_none),
  mButtonActionId(0),
  #if ENABLE_JSONBRIDGEAPI
  mBridgeExclusive(false),
  #endif
  mStateMachineMode(statemachine_standard),
  mLongFunctionDelay(t_long_function_delay) // standard dS value, might need tuning for some special (slow) hardware
{
  // set default hardware configuration
  setHardwareButtonConfig(0, buttonType_single, buttonElement_center, false, 0, 1); // not combinable, but button mode writable
  // reset the button state machine
  resetStateMachine();
}


void ButtonBehaviour::setHardwareButtonConfig(int aButtonID, VdcButtonType aType, VdcButtonElement aElement, bool aSupportsLocalKeyMode, int aCounterPartIndex, int aNumCombinables)
{
  mButtonID = aButtonID;
  mButtonType = aType;
  mButtonElementID = aElement;
  mSupportsLocalKeyMode = aSupportsLocalKeyMode;
  mCombinables = aNumCombinables;
  // now derive default settings from hardware
  // - default to standard mode
  mButtonMode = buttonMode_standard;
  // - modify for 2-way
  if (mButtonType==buttonType_2way) {
    // part of a 2-way button.
    if (mButtonElementID==buttonElement_up) {
      mButtonMode = (DsButtonMode)((int)buttonMode_rockerUp_pairWith0+aCounterPartIndex);
    }
    else if (mButtonElementID==buttonElement_down) {
      mButtonMode = (DsButtonMode)((int)buttonMode_rockerDown_pairWith0+aCounterPartIndex);
    }
  }
  if (mCombinables==0) {
    // not combinable and limited to only this mode
    mFixedButtonMode = mButtonMode;
  }
}


string ButtonBehaviour::getAutoId()
{
  if (mButtonType==buttonType_2way) {
    return mButtonElementID==buttonElement_up ? "up" : "down";
  }
  else {
    return "button";
  }
}


void ButtonBehaviour::setGroup(DsGroup aGroup)
{
  if (setPVar(mButtonGroup, aGroup)) {
    #if ENABLE_JSONBRIDGEAPI
    if (mDevice.isBridged()) {
      // inform bridges
      VdcApiConnectionPtr api = mDevice.getVdcHost().getBridgeApi();
      if (api) {
        ApiValuePtr q = api->newApiValue();
        q = q->wrapNull("group")->wrapAs(getApiId(api->getApiVersion()))->wrapAs("buttonSettings");
        mDevice.pushNotification(api, q, ApiValuePtr());
      }
    }
    #endif
  }
}


void ButtonBehaviour::setChannel(DsChannelType aChannel)
{
  if (setPVar(mButtonChannel, aChannel)) {
    #if ENABLE_JSONBRIDGEAPI
    if (mDevice.isBridged()) {
      // inform bridges
      VdcApiConnectionPtr api = mDevice.getVdcHost().getBridgeApi();
      if (api) {
        ApiValuePtr q = api->newApiValue();
        q = q->wrapNull("channel")->wrapAs(getApiId(api->getApiVersion()))->wrapAs("buttonSettings");
        mDevice.pushNotification(api, q, ApiValuePtr());
      }
    }
    #endif
  }
}


void ButtonBehaviour::setBridgeExclusive()
{
  #if ENABLE_JSONBRIDGEAPI
  mBridgeExclusive = true;
  #else
  // no bridge -> NOP
  #endif
}


bool ButtonBehaviour::isBridgeExclusive()
{
  #if ENABLE_JSONBRIDGEAPI
  return mDevice.isBridged() && mBridgeExclusive;
  #else
  return false;
  #endif
}




void ButtonBehaviour::updateButtonState(bool aPressed)
{
  OLOG(LOG_NOTICE, "reports %s", aPressed ? "pressed" : "released");
  bool stateChanged = aPressed!=mButtonPressed;
  mButtonPressed = aPressed; // remember new state
  // check which statemachine to use
  if (mButtonMode==buttonMode_turbo || mStateMachineMode!=statemachine_standard) {
    // use custom state machine
    checkCustomStateMachine(stateChanged, MainLoop::now());
  }
  else {
    // use regular state machine
    checkStandardStateMachine(stateChanged, MainLoop::now());
  }
}


void ButtonBehaviour::injectState(bool aButtonPressed)
{
  mButtonPressed = aButtonPressed;
  mLastAction = MainLoop::now();
}


void ButtonBehaviour::injectClick(DsClickType aClickType)
{
  switch (aClickType) {
    // add clicks and tips to counter (which will expire after t_tip_timeout)
    case ct_tip_4x:
      mClickCounter++;
    case ct_tip_3x:
    case ct_click_3x:
      mClickCounter++;
    case ct_tip_2x:
    case ct_click_2x:
      mClickCounter++;
    case ct_tip_1x:
    case ct_click_1x:
      mClickCounter++;
      // report current count as tips
      mState = S4_nextTipWait; // must set a state, although regular state machine is not used, to make sure valueSource reports clicks
      if (isLocalButtonEnabled() && mClickCounter==1) {
        // first tip switches local output if local button is enabled
        localSwitchOutput();
      }
      else if (mClickCounter<=4) {
        // simulate complete press and release (altough of no duration)
        mButtonPressed = true;
        sendClick(ct_progress); // report extra progress of click starting
        mButtonPressed = false;
        sendClick((DsClickType)(ct_tip_1x+mClickCounter-1));
      }
      if (mClickCounter<4) {
        // time out after we're sure all tips are accumulated
        mButtonStateMachineTicket.executeOnce(boost::bind(&ButtonBehaviour::injectedOpComplete, this, true), t_tip_timeout);
      }
      else {
        // counter overflow, reset right now
        injectedOpComplete(true);
      }
      break;
    case ct_hold_start:
      mButtonPressed = true;
      if (mClickType==ct_hold_start) {
        aClickType = ct_hold_repeat; // already started before -> treat as repeat
      }
      mState = S8_awaitrelease; // must set a state, although regular state machine is not used, to make sure valueSource reports holds
      sendClick(aClickType);
      mButtonStateMachineTicket.executeOnce(boost::bind(&ButtonBehaviour::holdRepeat, this), t_dim_repeat_time);
      break;
    case ct_hold_end:
      mButtonPressed = false;
      if (mClickType!=ct_hold_start && mClickType!=ct_hold_repeat) break; // suppress hold end when not in hold start
      sendClick(aClickType);
      injectedOpComplete(false);
      break;
    default:
      break;

  }
}


void ButtonBehaviour::injectedOpComplete(bool aSequence)
{
  resetStateMachine();
  if (aSequence) clickSequenceComplete();
}


void ButtonBehaviour::resetStateMachine()
{
  mButtonPressed = false;
  mState = S0_idle;
  mClickCounter = 0;
  mHoldRepeats = 0;
  mDimmingUp = false;
  mTimerRef = Never;
  mButtonStateMachineTicket.cancel();
}


void ButtonBehaviour::holdRepeat()
{
  mButtonStateMachineTicket.cancel();
  // button still pressed
  FOCUSOLOG("dimming in progress - sending ct_hold_repeat (repeatcount = %d)", holdRepeats);
  sendClick(ct_hold_repeat);
  mHoldRepeats++;
  if (mHoldRepeats<max_hold_repeats) {
    // schedule next repeat
    mButtonStateMachineTicket.executeOnce(boost::bind(&ButtonBehaviour::holdRepeat, this), t_dim_repeat_time);
  }
}


#if FOCUSLOGGING
static const char *stateNames[] = {
  "S0_idle",
  "S1_initialpress",
  "S2_holdOrTip",
  "S3_hold",
  "S4_nextTipWait",
  "S5_nextPauseWait",
  "S6_2ClickWait",
  "S7_progModeWait",
  "S8_awaitrelease",
  "S9_2pauseWait",
  // S10 missing
  "S11_localdim",
  "S12_3clickWait",
  "S13_3pauseWait",
  "S14_awaitrelease_timedout"
};
#endif




// Custom state machines:
// - plan44 "turbo" state machine which can tolerate missing a "press" or a "release" event
//   but cannot detect multi-clicks, only multi-tips, and cannot dim
// - dim-only state machine
// - single click, no-dim state machine
// Note: must only be changed on receiving a press or release event (which however does NOT
//   necessarily mean aStateChanged, in case of lost press or releases!)
void ButtonBehaviour::checkCustomStateMachine(bool aStateChanged, MLMicroSeconds aNow)
{
  MLMicroSeconds timeSinceRef = aNow-mTimerRef;
  mTimerRef = aNow;

  mButtonStateMachineTicket.cancel();
  if (mStateMachineMode==statemachine_single) {
    FOCUSOLOG("single click only button state machine entered in state %s", stateNames[state]);
    if (mButtonPressed) {
      // the button was pressed right now
      mState = S8_awaitrelease;
      sendClick(ct_progress); // report getting pressed to bridges (not dS)
    }
    else {
      // the button was released right now
      if (mState==S0_idle) {
        // we haven't seen a press before, assume the press got lost and act (late) on the release
        // - simulate the button pressing (for bridges)
        mButtonPressed = true;
        sendClick(ct_progress); // report getting pressed to bridges (not dS)
        mButtonPressed = false;
      }
      // report getting released to bridges (not dS)
      sendClick(ct_progress);
      mState = S0_idle;
      // Note: we do not have other states but idle and awaitrelease
      if (isLocalButtonEnabled()) {
        // first tip switches local output if local button is enabled
        localSwitchOutput();
      }
      else {
        // other tips are sent upstream
        sendClick(ct_tip_1x);
      }
    }
  }
  else if (mButtonMode==buttonMode_turbo || mStateMachineMode==statemachine_simple) {
    FOCUSOLOG("simple button state machine entered in state %s at reference time %d and clickCounter=%d", stateNames[state], (int)(timeSinceRef/MilliSecond), clickCounter);
    // reset click counter if tip timeout has passed since last event
    if (timeSinceRef>t_tip_timeout) {
      mClickCounter = 0;
    }
    // use Idle and Awaitrelease states only to remember previous button state detected
    bool isTip = false;
    if (mButtonPressed) {
      // the button was pressed right now
      // - always count button press as a tip
      isTip = true;
      // - state is now Awaitrelease
      mState = S8_awaitrelease;
      sendClick(ct_progress); // report getting pressed to bridges (not dS)
    }
    else {
      // the button was released right now
      if (mState==S0_idle) {
        // we haven't seen a press before, assume the press got lost and act (late) on the release
        // - simulate the button pressing (for bridges)
        mButtonPressed = true;
        sendClick(ct_progress); // report getting pressed to bridges (not dS)
        mButtonPressed = false;
        // - process as tip
        isTip = true;
      }
      sendClick(ct_progress); // report getting released to bridges (not dS)
      // Note: we do not have other states but idle and awaitrelease
      mState = S0_idle;
      // complete the sequence if nothing happens within tip_timeout, anyway
      mButtonStateMachineTicket.executeOnce(boost::bind(&ButtonBehaviour::clickSequenceComplete, this), t_tip_timeout);
    }
    if (isTip) {
      if (isLocalButtonEnabled() && mClickCounter==0) {
        // first tip switches local output if local button is enabled
        localSwitchOutput();
      }
      else {
        // other tips are sent upstream
        sendClick((DsClickType)(ct_tip_1x+mClickCounter));
        mClickCounter++;
        if (mClickCounter>=4) mClickCounter = 0; // wrap around
      }
    }
  }
  else if (mStateMachineMode==statemachine_dimmer) {
    FOCUSOLOG("dimmer button state machine entered");
    // just issue hold and stop events (e.g. for volume)
    if (aStateChanged) {
      if (isLocalButtonEnabled() && isOutputOn()) {
        // local dimming start/stop
        localDim(mButtonPressed);
      }
      else {
        // not local button mode
        if (mButtonPressed) {
          FOCUSOLOG("started dimming - sending ct_hold_start");
          // button just pressed
          sendClick(ct_hold_start);
          // schedule hold repeats
          mHoldRepeats = 0;
          mButtonStateMachineTicket.executeOnce(boost::bind(&ButtonBehaviour::holdRepeat, this), t_dim_repeat_time);
        }
        else {
          // button just released
          FOCUSOLOG("stopped dimming - sending ct_hold_end");
          sendClick(ct_hold_end);
          mButtonStateMachineTicket.cancel();
        }
      }
    }
  }
  else {
    OLOG(LOG_ERR, "invalid stateMachineMode");
  }
}


// standard button state machine
void ButtonBehaviour::checkStandardStateMachine(bool aStateChanged, MLMicroSeconds aNow)
{
  mButtonStateMachineTicket.cancel();
  MLMicroSeconds timeSinceRef = aNow-mTimerRef;

  FOCUSOLOG("state machine entered in state %s at reference time %d and clickCounter=%d", stateNames[state], (int)(timeSinceRef/MilliSecond), clickCounter);
  switch (mState) {

    case S0_idle :
      mTimerRef = Never; // no timer running
      if (aStateChanged && mButtonPressed) {
        mClickCounter = isLocalButtonEnabled() ? 0 : 1;
        mTimerRef = aNow;
        mState = S1_initialpress;
        sendClick(ct_progress); // report getting pressed to bridges (not dS)
      }
      break;

    case S1_initialpress :
      if (aStateChanged && !mButtonPressed) {
        mTimerRef = aNow;
        mState = S5_nextPauseWait;
        sendClick(ct_progress); // report getting short-released to bridges (not dS)
      }
      else if (timeSinceRef>=t_click_length) {
        mState = S2_holdOrTip;
      }
      break;

    case S2_holdOrTip:
      if (aStateChanged && !mButtonPressed && mClickCounter==0) {
        localSwitchOutput();
        mTimerRef = aNow;
        mClickCounter = 1;
        mState = S4_nextTipWait;
      }
      else if (aStateChanged && !mButtonPressed && mClickCounter>0) {
        sendClick((DsClickType)(ct_tip_1x+mClickCounter-1));
        mTimerRef = aNow;
        mState = S4_nextTipWait;
      }
      else if (timeSinceRef>=mLongFunctionDelay) {
        // long function
        if (!isLocalButtonEnabled() || !isOutputOn()) {
          // hold
          mHoldRepeats = 0;
          mTimerRef = aNow;
          sendClick(ct_hold_start);
          mState = S3_hold;
        }
        else if (isLocalButtonEnabled() && isOutputOn()) {
          // local dimming
          localDim(true); // start dimming
          mState = S11_localdim;
        }
      }
      break;

    case S3_hold:
      if (aStateChanged && !mButtonPressed) {
        // no packet send time, skip S15
        sendClick(ct_hold_end);
        mState = S0_idle;
      }
      else if (timeSinceRef>=t_dim_repeat_time) {
        if (mHoldRepeats<max_hold_repeats) {
          mTimerRef = aNow;
          sendClick(ct_hold_repeat);
          mHoldRepeats++;
        }
        else {
          // early hold end reporting, still waiting for actual release of the button
          sendClick(ct_hold_end);
          mState = S14_awaitrelease_timedout;
        }
      }
      break;

    case S4_nextTipWait:
      if (aStateChanged && mButtonPressed) {
        mTimerRef = aNow;
        if (mClickCounter>=4)
          mClickCounter = 2;
        else
          mClickCounter++;
        sendClick(ct_progress); // report getting pressed again to bridges (not dS)
        mState = S2_holdOrTip;
      }
      else if (timeSinceRef>=t_tip_timeout) {
        mState = S0_idle;
        clickSequenceComplete();
      }
      break;

    case S5_nextPauseWait:
      if (aStateChanged && mButtonPressed) {
        sendClick(ct_progress); // report getting short-released to bridges (not dS)
        mTimerRef = aNow;
        mClickCounter = 2;
        mState = S6_2ClickWait;
      }
      else if (timeSinceRef>=t_click_pause) {
        if (isLocalButtonEnabled())
          localSwitchOutput();
        else
          sendClick(ct_click_1x);
        mState = S4_nextTipWait;
      }
      break;

    case S6_2ClickWait:
      if (aStateChanged && !mButtonPressed) {
        sendClick(ct_progress); // report getting short-released to bridges (not dS)
        mTimerRef = aNow;
        mState = S9_2pauseWait;
      }
      else if (timeSinceRef>t_click_length) {
        mState = S7_progModeWait;
      }
      break;

    case S7_progModeWait:
      if (aStateChanged && !mButtonPressed) {
        sendClick(ct_tip_2x);
        mTimerRef = aNow;
        mState = S4_nextTipWait;
      }
      else if (timeSinceRef>mLongFunctionDelay) {
        sendClick(ct_short_long);
        mState = S8_awaitrelease;
      }
      break;

    case S9_2pauseWait:
      if (aStateChanged && mButtonPressed) {
        sendClick(ct_progress); // report getting short-released to bridges (not dS)
        mTimerRef = aNow;
        mClickCounter = 3;
        mState = S12_3clickWait;
      }
      else if (timeSinceRef>=t_click_pause) {
        sendClick(ct_click_2x);
        mState = S4_nextTipWait;
      }
      break;

    case S12_3clickWait:
      if (aStateChanged && !mButtonPressed) {
        mTimerRef = aNow;
        sendClick(ct_click_3x);
        mState = S4_nextTipWait;
      }
      else if (timeSinceRef>=t_click_length) {
        mState = S13_3pauseWait;
      }
      break;

    case S13_3pauseWait:
      if (aStateChanged && !mButtonPressed) {
        mTimerRef = aNow;
        sendClick(ct_tip_3x);
      }
      else if (timeSinceRef>=mLongFunctionDelay) {
        sendClick(ct_short_short_long);
        mState = S8_awaitrelease;
      }
      break;

    case S11_localdim:
      if (aStateChanged && !mButtonPressed) {
        mState = S0_idle;
        localDim(dimmode_stop); // stop dimming
      }
      break;

    case S8_awaitrelease:
      // normal wait for release
      if (aStateChanged && !mButtonPressed) {
        mState = S0_idle;
        clickSequenceComplete();
      }
      break;
    case S14_awaitrelease_timedout:
      // silently reset the state machine, hold_end was already sent before
      if (aStateChanged && !mButtonPressed) {
        mState = S0_idle;
      }
      break;
  }
  FOCUSOLOG(" -->                       exit state %s with %sfurther timing needed", stateNames[mState], timerRef!=Never ? "" : "NO ");
  if (mTimerRef!=Never) {
    // need timing, schedule calling again
    mButtonStateMachineTicket.executeOnce(boost::bind(&ButtonBehaviour::checkStandardStateMachine, this, false, _2), 10*MilliSecond);
  }
}


VdcButtonElement ButtonBehaviour::localFunctionElement()
{
  if (mButtonType!=buttonType_undefined) {
    // hardware defines the button
    return mButtonElementID;
  }
  // default to center
  return buttonElement_center;
}


bool ButtonBehaviour::isLocalButtonEnabled()
{
  return mSupportsLocalKeyMode && mButtonFunc==buttonFunc_device;
}


bool ButtonBehaviour::isOutputOn()
{
  if (mDevice.getOutput()) {
    ChannelBehaviourPtr ch = mDevice.getOutput()->getChannelByType(channeltype_default);
    if (ch) {
      return ch->getChannelValue()>0; // on if channel is above zero
    }
  }
  return false; // no output or channel -> is not on
}


VdcDimMode ButtonBehaviour::twoWayDirection()
{
  if (mButtonMode<buttonMode_rockerDown_pairWith0 || mButtonMode>buttonMode_rockerUp_pairWith3) return dimmode_stop; // single button -> no direction
  return mButtonMode>=buttonMode_rockerDown_pairWith0 && mButtonMode<=buttonMode_rockerDown_pairWith3 ? dimmode_down : dimmode_up; // down = -1, up = 1
}



void ButtonBehaviour::localSwitchOutput()
{
  OLOG(LOG_NOTICE, "Local switch");
  int dir = twoWayDirection();
  if (dir==0) {
    // single button, toggle
    dir = isOutputOn() ? -1 : 1;
  }
  // actually switch output
  if (mDevice.getOutput()) {
    ChannelBehaviourPtr ch = mDevice.getOutput()->getChannelByType(channeltype_default);
    if (ch) {
      ch->setChannelValue(dir>0 ? ch->getMax() : ch->getMin());
      mDevice.requestApplyingChannels(NoOP, false);
    }
  }
  // send status
  sendClick(dir>0 ? ct_local_on : ct_local_off);
}


void ButtonBehaviour::localDim(bool aStart)
{
  OLOG(LOG_NOTICE, "Local dim %s", aStart ? "START" : "STOP");
  ChannelBehaviourPtr channel = mDevice.getChannelByIndex(0); // default channel
  if (channel) {
    if (aStart) {
      // start dimming, determine direction (directly from two-way buttons or via toggling direction for single buttons)
      VdcDimMode dm = twoWayDirection();
      if (dm==dimmode_stop) {
        // not two-way, need to toggle direction
        mDimmingUp = !mDimmingUp; // change direction
        dm = mDimmingUp ? dimmode_up : dimmode_down;
      }
      mDevice.dimChannel(channel, dm, true);
    }
    else {
      // just stop
      mDevice.dimChannel(channel, dimmode_stop, true);
    }
  }
}


void ButtonBehaviour::sendClick(DsClickType aClickType)
{
  OLOG(LOG_DEBUG, "sendClick: clicktype=%d, state=%d, clickcounter=%d", aClickType, mButtonPressed, mClickCounter);
  // check for p44-level scene buttons
  if (mButtonActionMode!=buttonActionMode_none && (aClickType==ct_tip_1x || aClickType==ct_click_1x)) {
    // trigger direct scene action for single clicks
    sendAction(mButtonActionMode, mButtonActionId);
    return;
  }
  // update button state
  mLastAction = MainLoop::now();
  mClickType = aClickType;
  mActionMode = buttonActionMode_none; // not action! Regular click!
  // button press is considered a (regular!) user action, have it checked globally first
  if (!mDevice.getVdcHost().signalDeviceUserAction(mDevice, true)) {
    // button press not consumed on global level
    // - forward to upstream dS if not bridgeExclusive (except for ct_progress/ct_complete, which are for bridges only)
    // - forward to bridges (except for hold-repeat, which bridges don't need)
    if (pushBehaviourState(!isBridgeExclusive() && mClickType!=ct_progress && mClickType!=ct_complete, mClickType!=ct_hold_repeat)) {
      OLOG(mClickType==ct_hold_repeat ? LOG_INFO : LOG_NOTICE, "successfully pushed state=%d, clickType=%d (%s)", mButtonPressed, aClickType, clickTypeName(aClickType).c_str());
    }
    #if ENABLE_LOCALCONTROLLER && ENABLE_P44SCRIPT
    // send event
    if (mClickType!=ct_hold_repeat && mClickType!=ct_progress) {
      OLOG(LOG_INFO, "sending value event for clicktype=%s, state=%d", clickTypeName(aClickType).c_str(), mButtonPressed);
      sendValueEvent();
    }
    #endif
    // also let vdchost know for local click handling
    // TODO: more elegant solution for this
    if (!isBridgeExclusive()) {
      mDevice.getVdcHost().checkForLocalClickHandling(*this);
    }
  }
}


void ButtonBehaviour::clickSequenceComplete()
{
  // click sequence complete
  // - report progress
  sendClick(ct_complete); // always report state (not to dS)
}


bool ButtonBehaviour::hasDefinedState()
{
  return false; // buttons don't have a defined state, only actions are of interest (no delayed reporting of button states)
}


void ButtonBehaviour::sendAction(VdcButtonActionMode aActionMode, uint8_t aActionId)
{
  mLastAction = MainLoop::now();
  mActionMode = aActionMode; // action!
  mActionId = aActionId;
  OLOG(LOG_NOTICE, "sendAction: actionMode = %d, actionId %d", mActionMode, mActionId);
  // issue a state property push. This is dS/P44 specific, but will not harm bridges that are not interested
  if (pushBehaviourState(!isBridgeExclusive(), true)) {
    OLOG(LOG_NOTICE, "successfully pushed actionMode = %d, actionId = %d", aActionMode, aActionId);
  }
  #if ENABLE_LOCALCONTROLLER && ENABLE_P44SCRIPT
  // send event
  sendValueEvent();
  #endif
  // also let vdchost know for local click handling
  // TODO: more elegant solution for this
  if (!isBridgeExclusive()) {
    mDevice.getVdcHost().checkForLocalClickHandling(*this); // will check mButtonActionMode/mActionMode/mActionId
  }
}


string ButtonBehaviour::clickTypeName(DsClickType aClickType)
{
  if (aClickType<=ct_tip_4x) return string_format("tip_%dx", aClickType+1-ct_tip_1x);
  if (aClickType==ct_hold_start) return "hold";
  if (aClickType==ct_hold_repeat) return "keep_holding";
  if (aClickType==ct_hold_end) return "release";
  if (aClickType<=ct_click_3x) return string_format("click_%dx", aClickType+1-ct_click_1x);
  if (aClickType==ct_local_on) return "local_on";
  if (aClickType==ct_local_off) return "local_off";
  if (aClickType==ct_local_stop) return "local_stop";
  if (aClickType==ct_progress) return "progress";
  if (aClickType==ct_complete) return "complete";
  if (aClickType==ct_none) return "none";
  // all others: just numeric
  return string_format("ct_%d", aClickType);
}




#if ENABLE_LOCALCONTROLLER

// MARK: - value source implementation


bool ButtonBehaviour::isEnabled()
{
  // only app buttons are available for use in local processing as valuesource
  return mButtonFunc==buttonFunc_app;
}


string ButtonBehaviour::getSourceId()
{
  return string_format("%s_B%s", mDevice.getDsUid().getString().c_str(), getId().c_str());
}


string ButtonBehaviour::getSourceName()
{
  // get device name or dSUID for context
  string n = mDevice.getAssignedName();
  if (n.empty()) {
    // use abbreviated dSUID instead
    string d = mDevice.getDsUid().getString();
    n = d.substr(0,8) + "..." + d.substr(d.size()-2,2);
  }
  // append behaviour description
  string_format_append(n, ": %s", getHardwareName().c_str());
  return n;
}


double ButtonBehaviour::getSourceValue()
{
  // -1: end of sequence event
  // <=0: not pressed
  // 1..4: number of clicks
  // >4 : held down
  if (mState==S0_idle) return 0;
  switch (mClickType) {
    case ct_tip_1x:
    case ct_click_1x:
      return 1;
    case ct_tip_2x:
    case ct_click_2x:
      return 2;
    case ct_tip_3x:
    case ct_click_3x:
      return 3;
    case ct_tip_4x:
      return 4;
    case ct_hold_start:
    case ct_hold_repeat:
      return 5;
    case ct_hold_end:
    default:
      return 0; // not pressed any more
    case ct_complete:
      return -1; // special marker to signal end of click sequence
  }
}


MLMicroSeconds ButtonBehaviour::getSourceLastUpdate()
{
  return mLastAction;
}


int ButtonBehaviour::getSourceOpLevel()
{
  return mDevice.opStateLevel();
}

#endif // ENABLE_LOCALCONTROLLER

// MARK: - persistence implementation


// SQLIte3 table name to store these parameters to
const char *ButtonBehaviour::tableName()
{
  return "ButtonSettings";
}



// data field definitions

static const size_t numFields = 8;

size_t ButtonBehaviour::numFieldDefs()
{
  return inherited::numFieldDefs()+numFields;
}


const FieldDefinition *ButtonBehaviour::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numFields] = {
    { "dsGroup", SQLITE_INTEGER }, // Note: don't call a SQL field "group"!
    { "buttonFunc", SQLITE_INTEGER },  // ACTUALLY: buttonMode! (harmless old bug, but DB field names are misleading)
    { "buttonGroup", SQLITE_INTEGER }, // ACTUALLY: buttonFunc! (harmless old bug, but DB field names are misleading)
    { "buttonFlags", SQLITE_INTEGER },
    { "buttonChannel", SQLITE_INTEGER },
    { "buttonActionMode", SQLITE_INTEGER },
    { "buttonActionId", SQLITE_INTEGER },
    { "buttonSMMode", SQLITE_INTEGER },
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numFields)
    return &dataDefs[aIndex];
  return NULL;
}

// Buggy (but functionally harmless) mapping as per 2016-01-11
//  DB                    actual property
//  --------------------- -----------------------
//  dsGroup               buttonGroup
//  buttonFunc            buttonMode    // WRONG
//  buttonGroup           buttonFunc    // WRONG
//  buttonFlags           flags
//  buttonChannel         buttonChannel
//  ...all ok from here


/// load values from passed row
void ButtonBehaviour::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inherited::loadFromRow(aRow, aIndex, NULL); // no common flags in base class
  // get the fields
  aRow->getCastedIfNotNull<DsGroup, int>(aIndex++, mButtonGroup);
  aRow->getCastedIfNotNull<DsButtonMode, int>(aIndex++, mButtonMode);
  if (mButtonMode!=buttonMode_inactive && mFixedButtonMode!=buttonMode_inactive && mButtonMode!=mFixedButtonMode) {
    // force mode according to fixedButtonMode, even if settings (from older versions) say something different
    mButtonMode = mFixedButtonMode;
  }
  aRow->getCastedIfNotNull<DsButtonFunc, int>(aIndex++, mButtonFunc);
  uint64_t flags = aRow->getWithDefault<int>(aIndex++, 0);
  aRow->getCastedIfNotNull<DsChannelType, int>(aIndex++, mButtonChannel);
  aRow->getCastedIfNotNull<VdcButtonActionMode, int>(aIndex++, mButtonActionMode);
  aRow->getCastedIfNotNull<uint8_t, int>(aIndex++, mButtonActionId);
  if (!aRow->getCastedIfNotNull<ButtonStateMachineMode, int>(aIndex++, mStateMachineMode)) {
    // no value yet for stateMachineMode -> old simpleStateMachine flag is still valid
    if (flags & buttonflag_OBSOLETE_simpleStateMachine) mStateMachineMode = statemachine_simple; // flag is set, use simple state machine mode
  }
  // decode the flags
  mSetsLocalPriority = flags & buttonflag_setsLocalPriority;
  mCallsPresent = flags & buttonflag_callsPresent;
  // pass the flags out to subclasses which call this superclass to get the flags (and decode themselves)
  if (aCommonFlagsP) *aCommonFlagsP = flags;
}


// bind values to passed statement
void ButtonBehaviour::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // encode the local button flags
  int flags = 0;
  if (mSetsLocalPriority) flags |= buttonflag_setsLocalPriority;
  if (mCallsPresent) flags |= buttonflag_callsPresent;
  // bind the fields
  aStatement.bind(aIndex++, mButtonGroup);
  aStatement.bind(aIndex++, mButtonMode);
  aStatement.bind(aIndex++, mButtonFunc);
  aStatement.bind(aIndex++, flags);
  aStatement.bind(aIndex++, mButtonChannel);
  aStatement.bind(aIndex++, mButtonActionMode);
  aStatement.bind(aIndex++, mButtonActionId);
  aStatement.bind(aIndex++, mStateMachineMode);
}



// MARK: - property access

static char button_key;

// description properties

enum {
  supportsLocalKeyMode_key,
  buttonID_key,
  buttonType_key,
  buttonElementID_key,
  combinables_key,
  numDescProperties
};


int ButtonBehaviour::numDescProps() { return numDescProperties; }
const PropertyDescriptorPtr ButtonBehaviour::getDescDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numDescProperties] = {
    { "supportsLocalKeyMode", apivalue_bool, supportsLocalKeyMode_key+descriptions_key_offset, OKEY(button_key) },
    { "buttonID", apivalue_uint64, buttonID_key+descriptions_key_offset, OKEY(button_key) },
    { "buttonType", apivalue_uint64, buttonType_key+descriptions_key_offset, OKEY(button_key) },
    { "buttonElementID", apivalue_uint64, buttonElementID_key+descriptions_key_offset, OKEY(button_key) },
    { "combinables", apivalue_uint64, combinables_key+descriptions_key_offset, OKEY(button_key) },
  };
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}


// settings properties

enum {
  group_key,
  mode_key,
  function_key,
  channel_key,
  setsLocalPriority_key,
  callsPresent_key,
  buttonActionMode_key,
  buttonActionId_key,
  stateMachineMode_key,
  longFunctionDelay_key,
  #if ENABLE_JSONBRIDGEAPI
  bridgeExclusive_key,
  #endif
  numSettingsProperties
};


int ButtonBehaviour::numSettingsProps() { return numSettingsProperties; }
const PropertyDescriptorPtr ButtonBehaviour::getSettingsDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numSettingsProperties] = {
    { "group", apivalue_uint64, group_key+settings_key_offset, OKEY(button_key) },
    { "mode", apivalue_uint64, mode_key+settings_key_offset, OKEY(button_key) },
    { "function", apivalue_uint64, function_key+settings_key_offset, OKEY(button_key) },
    { "channel", apivalue_uint64, channel_key+settings_key_offset, OKEY(button_key) },
    { "setsLocalPriority", apivalue_bool, setsLocalPriority_key+settings_key_offset, OKEY(button_key) },
    { "callsPresent", apivalue_bool, callsPresent_key+settings_key_offset, OKEY(button_key) },
    { "x-p44-buttonActionMode", apivalue_uint64, buttonActionMode_key+settings_key_offset, OKEY(button_key) },
    { "x-p44-buttonActionId", apivalue_uint64, buttonActionId_key+settings_key_offset, OKEY(button_key) },
    { "x-p44-stateMachineMode", apivalue_uint64, stateMachineMode_key+settings_key_offset, OKEY(button_key) },
    { "x-p44-longFunctionDelay", apivalue_uint64, longFunctionDelay_key+settings_key_offset, OKEY(button_key) },
    #if ENABLE_JSONBRIDGEAPI
    { "x-p44-bridgeExclusive", apivalue_bool, bridgeExclusive_key+settings_key_offset, OKEY(button_key) },
    #endif
  };
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}

// state properties

enum {
  value_key,
  clickType_key,
  actionMode_key,
  actionId_key,
  age_key,
  numStateProperties
};


int ButtonBehaviour::numStateProps() { return numStateProperties; }
const PropertyDescriptorPtr ButtonBehaviour::getStateDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numStateProperties] = {
    { "value", apivalue_bool, value_key+states_key_offset, OKEY(button_key) },
    { "clickType", apivalue_uint64, clickType_key+states_key_offset, OKEY(button_key) },
    { "actionMode", apivalue_uint64, actionMode_key+states_key_offset, OKEY(button_key) },
    { "actionId", apivalue_uint64, actionId_key+states_key_offset, OKEY(button_key) },
    { "age", apivalue_double, age_key+states_key_offset, OKEY(button_key) },
  };
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}


// access to all fields

bool ButtonBehaviour::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(button_key)) {
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Description properties
        case supportsLocalKeyMode_key+descriptions_key_offset:
          aPropValue->setBoolValue(mSupportsLocalKeyMode);
          return true;
        case buttonID_key+descriptions_key_offset:
          aPropValue->setUint64Value(mButtonID);
          return true;
        case buttonType_key+descriptions_key_offset:
          aPropValue->setUint64Value(mButtonType);
          return true;
        case buttonElementID_key+descriptions_key_offset:
          aPropValue->setUint64Value(mButtonElementID);
          return true;
        case combinables_key+descriptions_key_offset:
          aPropValue->setUint64Value(mCombinables); // 0 and 1 both mean non-combinable, but 1 means that buttonmode is still not fixed
          return true;
        // Settings properties
        case group_key+settings_key_offset:
          aPropValue->setUint16Value(mButtonGroup);
          return true;
        case mode_key+settings_key_offset:
          aPropValue->setUint64Value(mButtonMode);
          return true;
        case function_key+settings_key_offset:
          aPropValue->setUint64Value(mButtonFunc);
          return true;
        case channel_key+settings_key_offset:
          aPropValue->setUint64Value(mButtonChannel);
          return true;
        case setsLocalPriority_key+settings_key_offset:
          aPropValue->setBoolValue(mSetsLocalPriority);
          return true;
        case callsPresent_key+settings_key_offset:
          aPropValue->setBoolValue(mCallsPresent);
          return true;
        case buttonActionMode_key+settings_key_offset:
          aPropValue->setUint8Value(mButtonActionMode);
          return true;
        case buttonActionId_key+settings_key_offset:
          aPropValue->setUint8Value(mButtonActionId);
          return true;
        case stateMachineMode_key+settings_key_offset:
          aPropValue->setUint8Value(mStateMachineMode);
          return true;
        case longFunctionDelay_key+settings_key_offset:
          aPropValue->setDoubleValue((double)mLongFunctionDelay/Second);
          return true;
        #if ENABLE_JSONBRIDGEAPI
        case bridgeExclusive_key+settings_key_offset:
          if (!mDevice.isBridged()) return false; // hide when not bridged
          aPropValue->setBoolValue(mBridgeExclusive);
          return true;
        #endif
        // States properties
        case value_key+states_key_offset:
          if (mLastAction==Never)
            aPropValue->setNull();
          else
            aPropValue->setBoolValue(mButtonPressed);
          return true;
        case clickType_key+states_key_offset:
          // click type is available only if last actions was a regular click
          if (mActionMode!=buttonActionMode_none) return false;
          aPropValue->setUint64Value(mClickType);
          return true;
        case actionMode_key+states_key_offset:
          // actionMode is available only if last actions was direct action
          if (mActionMode==buttonActionMode_none) return false;
          aPropValue->setUint64Value(mActionMode);
          return true;
        case actionId_key+states_key_offset:
          // actionId is available only if last actions was direct action
          if (mActionMode==buttonActionMode_none) return false;
          aPropValue->setUint64Value(mActionId);
          return true;
        case age_key+states_key_offset:
          // age
          if (mLastAction==Never)
            aPropValue->setNull();
          else
            aPropValue->setDoubleValue((double)(MainLoop::now()-mLastAction)/Second);
          return true;
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Settings properties
        case group_key+settings_key_offset:
          setGroup((DsGroup)aPropValue->int32Value());
          // for unchangeably paired (rocker) buttons, automatically change group on counterpart
          if (mFixedButtonMode==buttonMode_rockerDown_pairWith1 || mFixedButtonMode==buttonMode_rockerUp_pairWith1) {
            // also change group in button1
            OLOG(LOG_NOTICE,"paired button group changed in button0 -> also changed in button1");
            ButtonBehaviourPtr bb = mDevice.getButton(1); if (bb) bb->setGroup((DsGroup)aPropValue->int32Value());
          }
          else if (mFixedButtonMode==buttonMode_rockerDown_pairWith0 || mFixedButtonMode==buttonMode_rockerUp_pairWith0) {
            // also change group in button0
            OLOG(LOG_NOTICE,"paired button group changed in button1 -> also changed in button0");
            ButtonBehaviourPtr bb = mDevice.getButton(0); if (bb) bb->setGroup((DsGroup)aPropValue->int32Value());
          }
          return true;
        case mode_key+settings_key_offset: {
          DsButtonMode m = (DsButtonMode)aPropValue->int32Value();
          if (m!=buttonMode_inactive && mFixedButtonMode!=buttonMode_inactive) {
            // only one particular mode (aside from inactive) is allowed.
            m = mFixedButtonMode;
          }
          setPVar(mButtonMode, m);
          return true;
        }
        case function_key+settings_key_offset:
          setFunction((DsButtonFunc)aPropValue->int32Value());
          // for unchangeably paired (rocker) buttons, automatically change function on counterpart
          if (mFixedButtonMode==buttonMode_rockerDown_pairWith1 || mFixedButtonMode==buttonMode_rockerUp_pairWith1) {
            // also change function in button1
            OLOG(LOG_NOTICE,"paired button function changed in button0 -> also changed in button1");
            ButtonBehaviourPtr bb = mDevice.getButton(1); if (bb) bb->setFunction((DsButtonFunc)aPropValue->int32Value());
          }
          else if (mFixedButtonMode==buttonMode_rockerDown_pairWith0 || mFixedButtonMode==buttonMode_rockerUp_pairWith0) {
            // also change function in button0
            OLOG(LOG_NOTICE,"paired button function changed in button1 -> also changed in button0");
            ButtonBehaviourPtr bb = mDevice.getButton(0); if (bb) bb->setFunction((DsButtonFunc)aPropValue->int32Value());
          }
          return true;
        case channel_key+settings_key_offset:
          setChannel((DsChannelType)aPropValue->int32Value());
          return true;
        case setsLocalPriority_key+settings_key_offset:
          setSetsLocalPriority(aPropValue->boolValue());
          return true;
        case callsPresent_key+settings_key_offset:
          setCallsPresent(aPropValue->boolValue());
          return true;
        case buttonActionMode_key+settings_key_offset:
          setPVar(mButtonActionMode, (VdcButtonActionMode)aPropValue->uint8Value());
          return true;
        case buttonActionId_key+settings_key_offset:
          setPVar(mButtonActionId, aPropValue->uint8Value());
          return true;
        case stateMachineMode_key+settings_key_offset:
          setPVar(mStateMachineMode, (ButtonStateMachineMode)aPropValue->uint8Value());
          return true;
        case longFunctionDelay_key+settings_key_offset:
          setPVar(mLongFunctionDelay, (MLMicroSeconds)(aPropValue->doubleValue()*Second));
          return true;
        #if ENABLE_JSONBRIDGEAPI
        case bridgeExclusive_key+settings_key_offset:
          // volatile, does not make settings dirty
          mBridgeExclusive = aPropValue->boolValue();
          return true;
        #endif
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


// MARK: - description/shortDesc


string ButtonBehaviour::description()
{
  string s = string_format("%s behaviour", shortDesc().c_str());
  string_format_append(s, "\n- buttonID: %d, buttonType: %d, buttonElementID: %d", mButtonID, mButtonType, mButtonElementID);
  string_format_append(s, "\n- buttonChannel: %d, buttonFunc: %d, buttonmode/LTMODE: %d", mButtonChannel, mButtonFunc, mButtonMode);
  s.append(inherited::description());
  return s;
}

