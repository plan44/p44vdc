//
//  Copyright (c) 2013-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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



ButtonBehaviour::ButtonBehaviour(Device &aDevice, const string aId) :
  inherited(aDevice, aId),
  // persistent settings
  buttonGroup(group_yellow_light),
  buttonMode(buttonMode_inactive), // none by default, hardware should set a default matching the actual HW capabilities
  fixedButtonMode(buttonMode_inactive), // by default, mode can be set. Hardware may fix the possible mode
  buttonChannel(channeltype_default), // by default, buttons act on default channel
  buttonFunc(buttonFunc_room_preset0x), // act as room button by default
  setsLocalPriority(false),
  clickType(ct_none),
  actionMode(buttonActionMode_none),
  actionId(0),
  buttonPressed(false),
  lastAction(Never),
  callsPresent(false),
  buttonActionMode(buttonActionMode_none),
  buttonActionId(0),
  stateMachineMode(statemachine_standard),
  longFunctionDelay(t_long_function_delay) // standard dS value, might need tuning for some special (slow) hardware
{
  // set default hardware configuration
  setHardwareButtonConfig(0, buttonType_single, buttonElement_center, false, 0, 1); // not combinable, but button mode writable
  // reset the button state machine
  resetStateMachine();
}


void ButtonBehaviour::setHardwareButtonConfig(int aButtonID, VdcButtonType aType, VdcButtonElement aElement, bool aSupportsLocalKeyMode, int aCounterPartIndex, int aNumCombinables)
{
  buttonID = aButtonID;
  buttonType = aType;
  buttonElementID = aElement;
  supportsLocalKeyMode = aSupportsLocalKeyMode;
  combinables = aNumCombinables;
  // now derive default settings from hardware
  // - default to standard mode
  buttonMode = buttonMode_standard;
  // - modify for 2-way
  if (buttonType==buttonType_2way) {
    // part of a 2-way button.
    if (buttonElementID==buttonElement_up) {
      buttonMode = (DsButtonMode)((int)buttonMode_rockerUp_pairWith0+aCounterPartIndex);
    }
    else if (buttonElementID==buttonElement_down) {
      buttonMode = (DsButtonMode)((int)buttonMode_rockerDown_pairWith0+aCounterPartIndex);
    }
  }
  if (combinables==0) {
    // not combinable and limited to only this mode
    fixedButtonMode = buttonMode;
  }
}


string ButtonBehaviour::getAutoId()
{
  if (buttonType==buttonType_2way) {
    return buttonElementID==buttonElement_up ? "up" : "down";
  }
  else {
    return "button";
  }
}



void ButtonBehaviour::updateButtonState(bool aPressed)
{
  OLOG(LOG_NOTICE, "reports %s", aPressed ? "pressed" : "released");
  bool stateChanged = aPressed!=buttonPressed;
  buttonPressed = aPressed; // remember new state
  // check which statemachine to use
  if (buttonMode==buttonMode_turbo || stateMachineMode!=statemachine_standard) {
    // use custom state machine
    checkCustomStateMachine(stateChanged, MainLoop::now());
  }
  else {
    // use regular state machine
    checkStandardStateMachine(stateChanged, MainLoop::now());
  }
}


void ButtonBehaviour::injectClick(DsClickType aClickType)
{
  switch (aClickType) {
    // add clicks and tips to counter (which will expire after t_tip_timeout)
    case ct_tip_4x:
      clickCounter++;
    case ct_tip_3x:
    case ct_click_3x:
      clickCounter++;
    case ct_tip_2x:
    case ct_click_2x:
      clickCounter++;
    case ct_tip_1x:
    case ct_click_1x:
      clickCounter++;
      // report current count as tips
      state = S4_nextTipWait; // must set a state, although regular state machine is not used, to make sure valueSource reports clicks
      if (isLocalButtonEnabled() && clickCounter==1) {
        // first tip switches local output if local button is enabled
        localSwitchOutput();
      }
      else if (clickCounter<=4) {
        sendClick((DsClickType)(ct_tip_1x+clickCounter-1));
      }
      if (clickCounter<4) {
        // time out after we're sure all tips are accumulated
        buttonStateMachineTicket.executeOnce(boost::bind(&ButtonBehaviour::injectedOpComplete, this), t_tip_timeout);
      }
      else {
        // counter overflow, reset right now
        injectedOpComplete();
      }
      break;
    case ct_hold_start:
      if (clickType==ct_hold_start) {
        aClickType = ct_hold_repeat; // already started before -> treat as repeat
      }
      state = S8_awaitrelease; // must set a state, although regular state machine is not used, to make sure valueSource reports holds
      sendClick(aClickType);
      buttonStateMachineTicket.executeOnce(boost::bind(&ButtonBehaviour::holdRepeat, this), t_dim_repeat_time);
      break;
    case ct_hold_end:
      if (clickType!=ct_hold_start && clickType!=ct_hold_repeat) break; // suppress hold end when not in hold start
      sendClick(aClickType);
      injectedOpComplete();
      break;
    default:
      break;

  }
}


void ButtonBehaviour::injectedOpComplete()
{
  resetStateMachine();
  keyOpComplete();
}


void ButtonBehaviour::resetStateMachine()
{
  buttonPressed = false;
  state = S0_idle;
  clickCounter = 0;
  holdRepeats = 0;
  dimmingUp = false;
  timerRef = Never;
  buttonStateMachineTicket.cancel();
}


void ButtonBehaviour::holdRepeat()
{
  buttonStateMachineTicket.cancel();
  // button still pressed
  FOCUSOLOG("dimming in progress - sending ct_hold_repeat (repeatcount = %d)", holdRepeats);
  sendClick(ct_hold_repeat);
  holdRepeats++;
  if (holdRepeats<max_hold_repeats) {
    // schedule next repeat
    buttonStateMachineTicket.executeOnce(boost::bind(&ButtonBehaviour::holdRepeat, this), t_dim_repeat_time);
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




// plan44 "turbo" state machine which can tolerate missing a "press" or a "release" event
// Note: only to be called when button state changes
void ButtonBehaviour::checkCustomStateMachine(bool aStateChanged, MLMicroSeconds aNow)
{
  MLMicroSeconds timeSinceRef = aNow-timerRef;
  timerRef = aNow;

  if (buttonMode==buttonMode_turbo || stateMachineMode==statemachine_simple) {
    FOCUSOLOG("simple button state machine entered in state %s at reference time %d and clickCounter=%d", stateNames[state], (int)(timeSinceRef/MilliSecond), clickCounter);
    // reset click counter if tip timeout has passed since last event
    if (timeSinceRef>t_tip_timeout) {
      clickCounter = 0;
    }
    // use Idle and Awaitrelease states only to remember previous button state detected
    bool isTip = false;
    if (buttonPressed) {
      // the button was pressed right now
      // - always count button press as a tip
      isTip = true;
      // - state is now Awaitrelease
      state = S8_awaitrelease;
    }
    else {
      // the button was released right now
      // - if we haven't seen a press before, assume the press got lost and act on the release
      if (state==S0_idle) {
        isTip = true;
        // as we'll send the click event NOW, but will get no physical release from the button, we must simulate keyOpComplete()
        buttonStateMachineTicket.executeOnce(boost::bind(&ButtonBehaviour::keyOpComplete, this), t_tip_timeout);
        state = S0_idle;
      }
      else {
        // - state is now idle AGAIN after a previous click event
        state = S0_idle;
        keyOpComplete();
      }
    }
    if (isTip) {
      if (isLocalButtonEnabled() && clickCounter==0) {
        // first tip switches local output if local button is enabled
        localSwitchOutput();
      }
      else {
        // other tips are sent upstream
        sendClick((DsClickType)(ct_tip_1x+clickCounter));
        clickCounter++;
        if (clickCounter>=4) clickCounter = 0; // wrap around
      }
    }
  }
  else if (stateMachineMode==statemachine_dimmer) {
    FOCUSOLOG("dimmer button state machine entered");
    // just issue hold and stop events (e.g. for volume)
    if (aStateChanged) {
      if (isLocalButtonEnabled() && isOutputOn()) {
        // local dimming start/stop
        localDim(buttonPressed);
      }
      else {
        // not local button mode
        if (buttonPressed) {
          FOCUSOLOG("started dimming - sending ct_hold_start");
          // button just pressed
          sendClick(ct_hold_start);
          // schedule hold repeats
          holdRepeats = 0;
          buttonStateMachineTicket.executeOnce(boost::bind(&ButtonBehaviour::holdRepeat, this), t_dim_repeat_time);
        }
        else {
          // button just released
          FOCUSOLOG("stopped dimming - sending ct_hold_end");
          sendClick(ct_hold_end);
          buttonStateMachineTicket.cancel();
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
  buttonStateMachineTicket.cancel();
  MLMicroSeconds timeSinceRef = aNow-timerRef;

  FOCUSOLOG("state machine entered in state %s at reference time %d and clickCounter=%d", stateNames[state], (int)(timeSinceRef/MilliSecond), clickCounter);
  switch (state) {

    case S0_idle :
      timerRef = Never; // no timer running
      if (aStateChanged && buttonPressed) {
        clickCounter = isLocalButtonEnabled() ? 0 : 1;
        timerRef = aNow;
        state = S1_initialpress;
      }
      break;

    case S1_initialpress :
      if (aStateChanged && !buttonPressed) {
        timerRef = aNow;
        state = S5_nextPauseWait;
      }
      else if (timeSinceRef>=t_click_length) {
        state = S2_holdOrTip;
      }
      break;

    case S2_holdOrTip:
      if (aStateChanged && !buttonPressed && clickCounter==0) {
        localSwitchOutput();
        timerRef = aNow;
        clickCounter = 1;
        state = S4_nextTipWait;
      }
      else if (aStateChanged && !buttonPressed && clickCounter>0) {
        sendClick((DsClickType)(ct_tip_1x+clickCounter-1));
        timerRef = aNow;
        state = S4_nextTipWait;
      }
      else if (timeSinceRef>=longFunctionDelay) {
        // long function
        if (!isLocalButtonEnabled() || !isOutputOn()) {
          // hold
          holdRepeats = 0;
          timerRef = aNow;
          sendClick(ct_hold_start);
          state = S3_hold;
        }
        else if (isLocalButtonEnabled() && isOutputOn()) {
          // local dimming
          localDim(true); // start dimming
          state = S11_localdim;
        }
      }
      break;

    case S3_hold:
      if (aStateChanged && !buttonPressed) {
        // no packet send time, skip S15
        sendClick(ct_hold_end);
        state = S0_idle;
      }
      else if (timeSinceRef>=t_dim_repeat_time) {
        if (holdRepeats<max_hold_repeats) {
          timerRef = aNow;
          sendClick(ct_hold_repeat);
          holdRepeats++;
        }
        else {
          // early hold end reporting, still waiting for actual release of the button
          sendClick(ct_hold_end);
          state = S14_awaitrelease_timedout;
        }
      }
      break;

    case S4_nextTipWait:
      if (aStateChanged && buttonPressed) {
        timerRef = aNow;
        if (clickCounter>=4)
          clickCounter = 2;
        else
          clickCounter++;
        state = S2_holdOrTip;
      }
      else if (timeSinceRef>=t_tip_timeout) {
        state = S0_idle;
        keyOpComplete();
      }
      break;

    case S5_nextPauseWait:
      if (aStateChanged && buttonPressed) {
        timerRef = aNow;
        clickCounter = 2;
        state = S6_2ClickWait;
      }
      else if (timeSinceRef>=t_click_pause) {
        if (isLocalButtonEnabled())
          localSwitchOutput();
        else
          sendClick(ct_click_1x);
        state = S4_nextTipWait;
      }
      break;

    case S6_2ClickWait:
      if (aStateChanged && !buttonPressed) {
        timerRef = aNow;
        state = S9_2pauseWait;
      }
      else if (timeSinceRef>t_click_length) {
        state = S7_progModeWait;
      }
      break;

    case S7_progModeWait:
      if (aStateChanged && !buttonPressed) {
        sendClick(ct_tip_2x);
        timerRef = aNow;
        state = S4_nextTipWait;
      }
      else if (timeSinceRef>longFunctionDelay) {
        sendClick(ct_short_long);
        state = S8_awaitrelease;
      }
      break;

    case S9_2pauseWait:
      if (aStateChanged && buttonPressed) {
        timerRef = aNow;
        clickCounter = 3;
        state = S12_3clickWait;
      }
      else if (timeSinceRef>=t_click_pause) {
        sendClick(ct_click_2x);
        state = S4_nextTipWait;
      }
      break;

    case S12_3clickWait:
      if (aStateChanged && !buttonPressed) {
        timerRef = aNow;
        sendClick(ct_click_3x);
        state = S4_nextTipWait;
      }
      else if (timeSinceRef>=t_click_length) {
        state = S13_3pauseWait;
      }
      break;

    case S13_3pauseWait:
      if (aStateChanged && !buttonPressed) {
        timerRef = aNow;
        sendClick(ct_tip_3x);
      }
      else if (timeSinceRef>=longFunctionDelay) {
        sendClick(ct_short_short_long);
        state = S8_awaitrelease;
      }
      break;

    case S11_localdim:
      if (aStateChanged && !buttonPressed) {
        state = S0_idle;
        localDim(dimmode_stop); // stop dimming
      }
      break;

    case S8_awaitrelease:
      // normal wait for
      if (aStateChanged && !buttonPressed) {
        state = S0_idle;
        keyOpComplete();
      }
      break;
    case S14_awaitrelease_timedout:
      // silently reset the state machine, hold_end was already sent before
      if (aStateChanged && !buttonPressed) {
        state = S0_idle;
      }
      break;
  }
  FOCUSOLOG(" -->                       exit state %s with %sfurther timing needed", stateNames[state], timerRef!=Never ? "" : "NO ");
  if (timerRef!=Never) {
    // need timing, schedule calling again
    buttonStateMachineTicket.executeOnce(boost::bind(&ButtonBehaviour::checkStandardStateMachine, this, false, _2), 10*MilliSecond);
  }
}


VdcButtonElement ButtonBehaviour::localFunctionElement()
{
  if (buttonType!=buttonType_undefined) {
    // hardware defines the button
    return buttonElementID;
  }
  // default to center
  return buttonElement_center;
}


bool ButtonBehaviour::isLocalButtonEnabled()
{
  return supportsLocalKeyMode && buttonFunc==buttonFunc_device;
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
  if (buttonMode<buttonMode_rockerDown_pairWith0 || buttonMode>buttonMode_rockerUp_pairWith3) return dimmode_stop; // single button -> no direction
  return buttonMode>=buttonMode_rockerDown_pairWith0 && buttonMode<=buttonMode_rockerDown_pairWith3 ? dimmode_down : dimmode_up; // down = -1, up = 1
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
        dimmingUp = !dimmingUp; // change direction
        dm = dimmingUp ? dimmode_up : dimmode_down;
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
  // check for p44-level scene buttons
  if (buttonActionMode!=buttonActionMode_none && (aClickType==ct_tip_1x || aClickType==ct_click_1x)) {
    // trigger direct scene action for single clicks
    sendAction(buttonActionMode, buttonActionId);
    return;
  }
  // update button state
  lastAction = MainLoop::now();
  clickType = aClickType;
  actionMode = buttonActionMode_none;
  // button press is considered a (regular!) user action, have it checked globally first
  if (!mDevice.getVdcHost().signalDeviceUserAction(mDevice, true)) {
    // button press not consumed on global level, forward to upstream dS
    if (pushBehaviourState()) {
      OLOG(clickType==ct_hold_repeat ? LOG_INFO : LOG_NOTICE, "successfully pushed state = %d, clickType %d", buttonPressed, aClickType);
    }
    #if ENABLE_LOCALCONTROLLER
    #if ENABLE_P44SCRIPT
    // send event
    if (clickType!=ct_hold_repeat) sendValueEvent();
    #else
    // notify listeners
    notifyListeners(clickType!=ct_hold_repeat ? valueevent_changed : valueevent_confirmed);
    #endif
    #endif
    // also let vdchost know for local click handling
    // TODO: more elegant solution for this
    mDevice.getVdcHost().checkForLocalClickHandling(*this, aClickType);
  }
}


void ButtonBehaviour::keyOpComplete()
{
  #if ENABLE_LOCALCONTROLLER
  #if ENABLE_P44SCRIPT
  // send event
  sendValueEvent();
  #else
  // notify listeners
  notifyListeners(valueevent_changed);
  #endif
  #endif
}


bool ButtonBehaviour::hasDefinedState()
{
  return false; // buttons don't have a defined state, only actions are of interest (no delayed reporting of button states)
}


void ButtonBehaviour::sendAction(VdcButtonActionMode aActionMode, uint8_t aActionId)
{
  lastAction = MainLoop::now();
  actionMode = aActionMode;
  actionId = aActionId;
  OLOG(LOG_NOTICE, "pushes actionMode = %d, actionId %d", actionMode, actionId);
  // issue a state property push
  pushBehaviourState();
}


#if ENABLE_LOCALCONTROLLER

// MARK: - value source implementation


bool ButtonBehaviour::isEnabled()
{
  // only app buttons are available for use in local processing
  return buttonFunc==buttonFunc_app;
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
  // 0: not pressed
  // 1..4: number of clicks
  // >4 : held down
  if (state==S0_idle) return 0;
  switch (clickType) {
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
  }
}


MLMicroSeconds ButtonBehaviour::getSourceLastUpdate()
{
  return lastAction;
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
  aRow->getCastedIfNotNull<DsGroup, int>(aIndex++, buttonGroup);
  aRow->getCastedIfNotNull<DsButtonMode, int>(aIndex++, buttonMode);
  if (buttonMode!=buttonMode_inactive && fixedButtonMode!=buttonMode_inactive && buttonMode!=fixedButtonMode) {
    // force mode according to fixedButtonMode, even if settings (from older versions) say something different
    buttonMode = fixedButtonMode;
  }
  aRow->getCastedIfNotNull<DsButtonFunc, int>(aIndex++, buttonFunc);
  uint64_t flags = aRow->getWithDefault<int>(aIndex++, 0);
  aRow->getCastedIfNotNull<DsChannelType, int>(aIndex++, buttonChannel);
  aRow->getCastedIfNotNull<VdcButtonActionMode, int>(aIndex++, buttonActionMode);
  aRow->getCastedIfNotNull<uint8_t, int>(aIndex++, buttonActionId);
  if (!aRow->getCastedIfNotNull<ButtonStateMachineMode, int>(aIndex++, stateMachineMode)) {
    // no value yet for stateMachineMode -> old simpleStateMachine flag is still valid
    if (flags & buttonflag_OBSOLETE_simpleStateMachine) stateMachineMode = statemachine_simple; // flag is set, use simple state machine mode
  }
  // decode the flags
  setsLocalPriority = flags & buttonflag_setsLocalPriority;
  callsPresent = flags & buttonflag_callsPresent;
  // pass the flags out to subclasses which call this superclass to get the flags (and decode themselves)
  if (aCommonFlagsP) *aCommonFlagsP = flags;
}


// bind values to passed statement
void ButtonBehaviour::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // encode the flags
  int flags = 0;
  if (setsLocalPriority) flags |= buttonflag_setsLocalPriority;
  if (callsPresent) flags |= buttonflag_callsPresent;
  // bind the fields
  aStatement.bind(aIndex++, buttonGroup);
  aStatement.bind(aIndex++, buttonMode);
  aStatement.bind(aIndex++, buttonFunc);
  aStatement.bind(aIndex++, flags);
  aStatement.bind(aIndex++, buttonChannel);
  aStatement.bind(aIndex++, buttonActionMode);
  aStatement.bind(aIndex++, buttonActionId);
  aStatement.bind(aIndex++, stateMachineMode);
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
          aPropValue->setBoolValue(supportsLocalKeyMode);
          return true;
        case buttonID_key+descriptions_key_offset:
          aPropValue->setUint64Value(buttonID);
          return true;
        case buttonType_key+descriptions_key_offset:
          aPropValue->setUint64Value(buttonType);
          return true;
        case buttonElementID_key+descriptions_key_offset:
          aPropValue->setUint64Value(buttonElementID);
          return true;
        case combinables_key+descriptions_key_offset:
          aPropValue->setUint64Value(combinables); // 0 and 1 both mean non-combinable, but 1 means that buttonmode is still not fixed
          return true;
        // Settings properties
        case group_key+settings_key_offset:
          aPropValue->setUint16Value(buttonGroup);
          return true;
        case mode_key+settings_key_offset:
          aPropValue->setUint64Value(buttonMode);
          return true;
        case function_key+settings_key_offset:
          aPropValue->setUint64Value(buttonFunc);
          return true;
        case channel_key+settings_key_offset:
          aPropValue->setUint64Value(buttonChannel);
          return true;
        case setsLocalPriority_key+settings_key_offset:
          aPropValue->setBoolValue(setsLocalPriority);
          return true;
        case callsPresent_key+settings_key_offset:
          aPropValue->setBoolValue(callsPresent);
          return true;
        case buttonActionMode_key+settings_key_offset:
          aPropValue->setUint8Value(buttonActionMode);
          return true;
        case buttonActionId_key+settings_key_offset:
          aPropValue->setUint8Value(buttonActionId);
          return true;
        case stateMachineMode_key+settings_key_offset:
          aPropValue->setUint8Value(stateMachineMode);
          return true;
        case longFunctionDelay_key+settings_key_offset:
          aPropValue->setDoubleValue((double)longFunctionDelay/Second);
          return true;
        // States properties
        case value_key+states_key_offset:
          if (lastAction==Never)
            aPropValue->setNull();
          else
            aPropValue->setBoolValue(buttonPressed);
          return true;
        case clickType_key+states_key_offset:
          // click type is available only if last actions was a regular click
          if (actionMode!=buttonActionMode_none) return false;
          aPropValue->setUint64Value(clickType);
          return true;
        case actionMode_key+states_key_offset:
          // actionMode is available only if last actions was direct action
          if (actionMode==buttonActionMode_none) return false;
          aPropValue->setUint64Value(actionMode);
          return true;
        case actionId_key+states_key_offset:
          // actionId is available only if last actions was direct action
          if (actionMode==buttonActionMode_none) return false;
          aPropValue->setUint64Value(actionId);
          return true;
        case age_key+states_key_offset:
          // age
          if (lastAction==Never)
            aPropValue->setNull();
          else
            aPropValue->setDoubleValue((double)(MainLoop::now()-lastAction)/Second);
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
          if (fixedButtonMode==buttonMode_rockerDown_pairWith1 || fixedButtonMode==buttonMode_rockerUp_pairWith1) {
            // also change group in button1
            OLOG(LOG_NOTICE,"paired button group changed in button0 -> also changed in button1");
            ButtonBehaviourPtr bb = mDevice.getButton(1); if (bb) bb->setGroup((DsGroup)aPropValue->int32Value());
          }
          else if (fixedButtonMode==buttonMode_rockerDown_pairWith0 || fixedButtonMode==buttonMode_rockerUp_pairWith0) {
            // also change group in button0
            OLOG(LOG_NOTICE,"paired button group changed in button1 -> also changed in button0");
            ButtonBehaviourPtr bb = mDevice.getButton(0); if (bb) bb->setGroup((DsGroup)aPropValue->int32Value());
          }
          return true;
        case mode_key+settings_key_offset: {
          DsButtonMode m = (DsButtonMode)aPropValue->int32Value();
          if (m!=buttonMode_inactive && fixedButtonMode!=buttonMode_inactive) {
            // only one particular mode (aside from inactive) is allowed.
            m = fixedButtonMode;
          }
          setPVar(buttonMode, m);
          return true;
        }
        case function_key+settings_key_offset:
          setFunction((DsButtonFunc)aPropValue->int32Value());
          // for unchangeably paired (rocker) buttons, automatically change function on counterpart
          if (fixedButtonMode==buttonMode_rockerDown_pairWith1 || fixedButtonMode==buttonMode_rockerUp_pairWith1) {
            // also change function in button1
            OLOG(LOG_NOTICE,"paired button function changed in button0 -> also changed in button1");
            ButtonBehaviourPtr bb = mDevice.getButton(1); if (bb) bb->setFunction((DsButtonFunc)aPropValue->int32Value());
          }
          else if (fixedButtonMode==buttonMode_rockerDown_pairWith0 || fixedButtonMode==buttonMode_rockerUp_pairWith0) {
            // also change function in button0
            OLOG(LOG_NOTICE,"paired button function changed in button1 -> also changed in button0");
            ButtonBehaviourPtr bb = mDevice.getButton(0); if (bb) bb->setFunction((DsButtonFunc)aPropValue->int32Value());
          }
          return true;
        case channel_key+settings_key_offset:
          setPVar(buttonChannel, (DsChannelType)aPropValue->int32Value());
          return true;
        case setsLocalPriority_key+settings_key_offset:
          setPVar(setsLocalPriority, aPropValue->boolValue());
          return true;
        case callsPresent_key+settings_key_offset:
          setPVar(callsPresent, aPropValue->boolValue());
          return true;
        case buttonActionMode_key+settings_key_offset:
          setPVar(buttonActionMode, (VdcButtonActionMode)aPropValue->uint8Value());
          return true;
        case buttonActionId_key+settings_key_offset:
          setPVar(buttonActionId, aPropValue->uint8Value());
          return true;
        case stateMachineMode_key+settings_key_offset:
          setPVar(stateMachineMode, (ButtonStateMachineMode)aPropValue->uint8Value());
          return true;
        case longFunctionDelay_key+settings_key_offset:
          setPVar(longFunctionDelay, (MLMicroSeconds)(aPropValue->doubleValue()*Second));
          return true;
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
  string_format_append(s, "\n- buttonID: %d, buttonType: %d, buttonElementID: %d", buttonID, buttonType, buttonElementID);
  string_format_append(s, "\n- buttonChannel: %d, buttonFunc: %d, buttonmode/LTMODE: %d", buttonChannel, buttonFunc, buttonMode);
  s.append(inherited::description());
  return s;
}

