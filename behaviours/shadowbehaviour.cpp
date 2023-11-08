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
#define FOCUSLOGLEVEL IFNOTREDUCEDFOOTPRINT(6)

#include "shadowbehaviour.hpp"


using namespace p44;


// MARK: - ShadowScene


ShadowScene::ShadowScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo) :
  inherited(aSceneDeviceSettings, aSceneNo),
  mAngle(0)
{
}


// MARK: - shadow scene values/channels


double ShadowScene::sceneValue(int aChannelIndex)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  if (cb->getChannelType()==channeltype_shade_angle_outside) {
    return mAngle;
  }
  return inherited::sceneValue(aChannelIndex);
}


void ShadowScene::setSceneValue(int aChannelIndex, double aValue)
{
  ChannelBehaviourPtr cb = getDevice().getChannelByIndex(aChannelIndex);
  if (cb->getChannelType()==channeltype_shade_angle_outside) {
    setPVar(mAngle, aValue);
    return;
  }
  inherited::setSceneValue(aChannelIndex, aValue);
}


// MARK: - shadow scene persistence

const char *ShadowScene::tableName()
{
  return "ShadowScenes";
}

// data field definitions

static const size_t numShadowSceneFields = 1;

size_t ShadowScene::numFieldDefs()
{
  return inherited::numFieldDefs()+numShadowSceneFields;
}


const FieldDefinition *ShadowScene::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numShadowSceneFields] = {
    { "angle", SQLITE_FLOAT }
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numShadowSceneFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void ShadowScene::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inherited::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get the fields
  mAngle = aRow->get<double>(aIndex++);
}


/// bind values to passed statement
void ShadowScene::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, mAngle);
}



// MARK: - default shadow scene

void ShadowScene::setDefaultSceneValues(SceneNo aSceneNo)
{
  // set the common simple scene defaults
  inherited::setDefaultSceneValues(aSceneNo);
  // Add special shadow behaviour
  switch (aSceneNo) {
    case PANIC:
    case SMOKE:
    case HAIL:
    case FIRE:
      // Panic, Smoke, Hail, Fire: open
      setDontCare(false);
      value = 100;
      break;
    case ABSENT:
    case PRESENT:
    case SLEEPING:
    case WAKE_UP:
    case STANDBY:
    case AUTO_STANDBY:
    case DEEP_OFF:
    case ALARM1:
    case WATER:
    case GAS:
    case WIND:
    case RAIN:
      setDontCare(true);
      break;
    case PRESET_2:
    case PRESET_12:
    case PRESET_22:
    case PRESET_32:
    case PRESET_42:
      // For some reason, Preset 2 is not 75%, but also 100% for shade devices.
      value = 100;
      break;
  }
  // by default, angle is 0 and don'tCare
  mAngle = 0;
  ShadowBehaviourPtr shadowBehaviour = boost::dynamic_pointer_cast<ShadowBehaviour>(getOutputBehaviour());
  if (shadowBehaviour) {
    setSceneValueFlags(shadowBehaviour->mAngle->getChannelIndex(), valueflags_dontCare, true);
  }
  markClean(); // default values are always clean (but setSceneValueFlags sets dirty)
}


// MARK: - ShadowJalousieScene


ShadowJalousieScene::ShadowJalousieScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo) :
inherited(aSceneDeviceSettings, aSceneNo)
{
}


void ShadowJalousieScene::setDefaultSceneValues(SceneNo aSceneNo)
{
  // set the common simple scene defaults
  inherited::setDefaultSceneValues(aSceneNo);
  // Add special shadow behaviour
  switch (aSceneNo) {
    case WIND:
      setDontCare(false);
      value = 100;
      break;
  }
  markClean(); // default values are always clean (but setSceneValueFlags sets dirty)
}


// MARK: - ShadowJalousieScene


ShadowAwningScene::ShadowAwningScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo) :
inherited(aSceneDeviceSettings, aSceneNo)
{
}


void ShadowAwningScene::setDefaultSceneValues(SceneNo aSceneNo)
{
  // set the common simple scene defaults
  inherited::setDefaultSceneValues(aSceneNo);
  // Add special shadow behaviour
  switch (aSceneNo) {
    case ABSENT:
    case SLEEPING:
    case DEEP_OFF:
    case WIND:
    case RAIN:
      setDontCare(false);
      value = 100;
      break;
  }
  markClean(); // default values are always clean (but setSceneValueFlags sets dirty)
}


// MARK: - ShadowDeviceSettings with default shadow scenes factory


ShadowDeviceSettings::ShadowDeviceSettings(Device &aDevice) :
  inherited(aDevice)
{
};


DsScenePtr ShadowDeviceSettings::newDefaultScene(SceneNo aSceneNo)
{
  ShadowScenePtr shadowScene = ShadowScenePtr(new ShadowScene(*this, aSceneNo));
  shadowScene->setDefaultSceneValues(aSceneNo);
  // return it
  return shadowScene;
}


// MARK: - ShadowJalousieDeviceSetting with default shadow scenes factory


ShadowJalousieDeviceSetting::ShadowJalousieDeviceSetting(Device &aDevice) :
inherited(aDevice)
{
};


DsScenePtr ShadowJalousieDeviceSetting::newDefaultScene(SceneNo aSceneNo)
{
  ShadowJalousieScenePtr shadowJalousieScene = ShadowJalousieScenePtr(new ShadowJalousieScene(*this, aSceneNo));
  shadowJalousieScene->setDefaultSceneValues(aSceneNo);
  // return it
  return shadowJalousieScene;
}


// MARK: - ShadowJalousieDeviceSetting with default shadow scenes factory


ShadowAwningDeviceSetting::ShadowAwningDeviceSetting(Device &aDevice) :
inherited(aDevice)
{
};


DsScenePtr ShadowAwningDeviceSetting::newDefaultScene(SceneNo aSceneNo)
{
  ShadowAwningScenePtr shadowAwningScene = ShadowAwningScenePtr(new ShadowAwningScene(*this, aSceneNo));
  shadowAwningScene->setDefaultSceneValues(aSceneNo);
  // return it
  return shadowAwningScene;
}



// MARK: - ShadowBehaviour

#define MIN_INTERRUPTABLE_MOVE_TIME (5*Second)
#define POSITION_TO_ANGLE_DELAY (1*Second)
#define INTER_SHORT_MOVE_DELAY (1*Second)


ShadowBehaviour::ShadowBehaviour(Device &aDevice, DsGroup aGroup) :
  inherited(aDevice),
  // hardware derived parameters
  mShadowDeviceKind(shadowdevice_jalousie),
  mMinMoveTime(200*MilliSecond),
  mMaxShortMoveTime(0),
  mMinLongMoveTime(0),
  mAbsoluteMovement(false),
  mStopDelayTime(0),
  mHasEndContacts(false),
  // persistent settings (defaults are MixWerk's)
  mOpenTime(54),
  mCloseTime(51),
  mAngleOpenTime(1),
  mAngleCloseTime(1),
  // volatile state
  mReferenceTime(Never),
  mBlindState(blind_idle),
  mMovingUp(false),
  mRunIntoEnd(false),
  mUpdateMoveTimeAtEndReached(false),
  mReferencePosition(100), // assume fully open, at top
  mReferenceAngle(100) // at top means that angle is open as well
{
  // make it member of the specified group (usually: shadow)
  setGroupMembership(aGroup, true);
  // primary output controls position
  setHardwareName("position");
  // add the channels (every shadow device has an angle so far, but roller/sun blinds dont use it)
  mPosition = ShadowPositionChannelPtr(new ShadowPositionChannel(*this));
  addChannel(mPosition);
  mAngle = ShadowAngleChannelPtr(new ShadowAngleChannel(*this));
  addChannel(mAngle);
}


void ShadowBehaviour::setDeviceParams(ShadowDeviceKind aShadowDeviceKind, bool aHasEndContacts, MLMicroSeconds aMinMoveTime, MLMicroSeconds aMaxShortMoveTime, MLMicroSeconds aMinLongMoveTime, bool aAbsoluteMovement)
{
  mShadowDeviceKind = aShadowDeviceKind;
  mHasEndContacts = aHasEndContacts;
  mMinMoveTime = aMinMoveTime;
  mMaxShortMoveTime = aMaxShortMoveTime;
  mMinLongMoveTime = aMinLongMoveTime;
  mAbsoluteMovement = aAbsoluteMovement;
}


Tristate ShadowBehaviour::hasModelFeature(DsModelFeatures aFeatureIndex)
{
  // now check for light behaviour level features
  switch (aFeatureIndex) {
    case modelFeature_transt:
      // Assumption: all shadow output devices don't transition times
      return no;
    case modelFeature_outvalue8:
      // Shade outputs are 16bit resolution and be labelled "Position", not "Value"
      return no; // suppress general 8-bit outmode assumption
    case modelFeature_shadeposition:
      // Shade output. Should be 16bit resolution and be labelled "Position", not "Value"
      return yes;
    case modelFeature_shadebladeang:
      // Jalousie also has blade angle, other kinds don't
      return mShadowDeviceKind==shadowdevice_jalousie ? yes : no;
    case modelFeature_shadeprops:
    case modelFeature_motiontimefins:
      // TODO: once dS support is here for propagating moving times etc, enable this
      // For now, shadow device property dialog makes no sense as it does not work at all
      return no;
    default:
      // not available at this level, ask base class
      return inherited::hasModelFeature(aFeatureIndex);
  }
}



// MARK: - Blind Movement Sequencer



double ShadowBehaviour::getPosition()
{
  double pos = mReferencePosition;
  if (mReferenceTime!=Never) {
    // moving, interpolate current position
    MLMicroSeconds mt = MainLoop::now()-mReferenceTime; // moving time
    if (mMovingUp)
      pos += mPosition->getMax()*mt/Second/mOpenTime; // moving up (open)
    else
      pos -= mPosition->getMax()*mt/Second/mCloseTime; // moving down (close)
  }
  // limit to range
  if (pos>mPosition->getMax()) pos = mPosition->getMax();
  else if (pos<0) pos=0;
  return pos;
}


double ShadowBehaviour::getAngle()
{
  double ang = mReferenceAngle;
  if (mReferenceTime!=Never) {
    // moving, interpolate current position
    MLMicroSeconds mt = MainLoop::now()-mReferenceTime; // moving time
    if (mMovingUp)
      ang += mAngle->getMax()*mt/Second/mAngleOpenTime; // moving up (open)
    else
      ang -= mAngle->getMax()*mt/Second/mAngleCloseTime; // moving down (close)
  }
  // limit to range
  if (ang>mAngle->getMax()) ang = mAngle->getMax();
  else if (ang<0) ang=0;
  return ang;
}


void ShadowBehaviour::moveTimerStart()
{
  mReferenceTime = MainLoop::now();
}


void ShadowBehaviour::moveTimerStop()
{
  if (mBlindState!=blind_stopping_after_turn) { // do not update position after turning
    mReferencePosition = getPosition();
  }
  mReferenceAngle = getAngle(); // do update angle because it might always change when moving
  mReferenceTime = Never;
}


void ShadowBehaviour::syncBlindState()
{
  mPosition->syncChannelValue(getPosition());
  mAngle->syncChannelValue(getAngle());
}


void ShadowBehaviour::applyBlindChannels(MovementChangeCB aMovementCB, SimpleCB aApplyDoneCB, bool aForDimming)
{
  FOCUSOLOG("Initiating blind moving sequence");
  mMovementCB = aMovementCB;
  if (mBlindState!=blind_idle) {
    // not idle
    if (aForDimming && mBlindState==blind_positioning) {
      // dimming requested while in progress of positioning
      // -> don't actually stop, just re-calculate position and timing
      mBlindState = blind_dimming;
      stopped(aApplyDoneCB);
      return;
    }
    else if (mBlindState==blind_positioning && mAngle->needsApplying() && !mPosition->needsApplying()) {
      // do not interrupt running positioning just because of angle change,
      // the angle will be (re)applied after positioning anyway
      // - just confirm applied
      if (aApplyDoneCB) aApplyDoneCB();
      // - let running state machine do the rest
      return;
    }
    // normal operation: stop first
    if (mBlindState==blind_stopping || mBlindState==blind_stopping_after_turn) {
      // already stopping, just make sure we'll apply afterwards
      mBlindState = blind_stopping_before_apply;
    }
    else {
      // something in progress, stop now
      mBlindState = blind_stopping_before_apply;
      stop(aApplyDoneCB);
    }
  }
  else {
    // can start right away
    applyPosition(aApplyDoneCB);
  }
}


void ShadowBehaviour::dimBlind(MovementChangeCB aMovementCB, VdcDimMode aDimMode)
{
  FOCUSOLOG("dimBlind called for %s", aDimMode==dimmode_up ? "UP" : (aDimMode==dimmode_down ? "DOWN" : "STOP"));
  if (aDimMode==dimmode_stop) {
    // simply stop
    mMovementCB = aMovementCB; // install new
    stop(NoOP);
  }
  else {
    if (mMovementCB) {
      // already running - just consider stopped to sample current positions
      mBlindState = blind_idle;
      stopped(NoOP);
    }
    // install new callback (likely same as before, if any)
    mMovementCB = aMovementCB;
    // prepare moving
    MLMicroSeconds stopIn;
    if (aDimMode==dimmode_up) {
      mMovingUp = true;
      stopIn = mOpenTime*Second*1.2; // max movement = fully up
    }
    else {
      mMovingUp = false;
      stopIn = mCloseTime*Second*1.2; // max movement = fully down
    }
    // start moving
    mBlindState = blind_dimming;
    startMoving(stopIn, NoOP);
  }
}




void ShadowBehaviour::stop(SimpleCB aApplyDoneCB)
{
  if (mMovementCB) {
    if (mBlindState==blind_positioning) {
      // if stopping after positioning, we might need to apply the angle afterwards
      mBlindState = blind_stopping_before_turning;
    }
    else if (mBlindState!=blind_stopping_before_apply) {
      // normal stop, unless this is a stop caused by a request to apply new values afterwards
      mBlindState = mBlindState==blind_turning ? blind_stopping_after_turn : blind_stopping;
    }
    OLOG(LOG_INFO, "Stopping all movement%s", mBlindState==blind_stopping_before_apply ? " before applying" : "");
    mMovingTicket.cancel();
    mMovementCB(boost::bind(&ShadowBehaviour::stopped, this, aApplyDoneCB, true), 0);
  }
  else {
    // no movement sequence in progress
    mBlindState = blind_idle; // just to make sure
    if (aApplyDoneCB) aApplyDoneCB();
  }
}



void ShadowBehaviour::endReached(bool aTop)
{
  // completely ignore if we don't have end contacts
  if (mHasEndContacts) {
    OLOG(LOG_INFO, "reports %s actually reached", aTop ? "top (fully rolled in)" : "bottom (fully extended)");
    // cancel timeouts that might want to stop movement
    mMovingTicket.cancel();
    // check for updating full range time
    if (mUpdateMoveTimeAtEndReached) {
      // ran full range, update time
      MLMicroSeconds fullRangeTime = MainLoop::now()-mReferenceTime;
      LOG(LOG_INFO, "- is end of a full range movement : measured move time %.1f -> updating settings", (double)fullRangeTime/Second);
      if (aTop) {
        mOpenTime = (double)fullRangeTime/Second; // update opening time
      }
      else {
        mCloseTime = (double)fullRangeTime/Second; // update closing time
      }
    }
    // update positions
    mReferenceTime = Never; // prevent re-calculation of position and angle from timing
    if (aTop) {
      // at top
      mReferencePosition = 100;
      mReferenceAngle = 100;
    }
    else {
      // at bottom
      mReferencePosition = 0;
      mReferenceAngle = 0;
    }
    // now report stopped
    stopped(mEndContactMoveAppliedCB);
  }
}


void ShadowBehaviour::stopped(SimpleCB aApplyDoneCB, bool delay)
{
  mUpdateMoveTimeAtEndReached = false; // stopping cancels full range timing update (if stop is due to end contact, measurement will be already done now)
  moveTimerStop();
  FOCUSOLOG("- calculated current blind position=%.1f%%, angle=%.1f", mReferencePosition, mReferenceAngle);
  if (delay) {
    mSequenceTicket.executeOnce(boost::bind(&ShadowBehaviour::processStopped, this, aApplyDoneCB), mStopDelayTime*Second);
  }
  else {
    processStopped(aApplyDoneCB);
  }
}


void ShadowBehaviour::processStopped(SimpleCB aApplyDoneCB)
{
  // next step depends on state
  switch (mBlindState) {
    case blind_stopping_before_apply:
      // now idle
      mBlindState = blind_idle;
      // continue with positioning
    case blind_dimming:
      // just apply new position (dimming case, move still running)
      applyPosition(aApplyDoneCB);
      break;
    case blind_stopping_before_turning:
      // after blind movement, always re-apply angle
      mSequenceTicket.executeOnce(boost::bind(&ShadowBehaviour::applyAngle, this, aApplyDoneCB), POSITION_TO_ANGLE_DELAY);
      break;
    default:
      // end of sequence
      // - confirm apply and update actual values (might have already happened at start for long moves)
      mPosition->channelValueApplied();
      mAngle->channelValueApplied();
      // - actually set positions, ends estimating transitions
      mPosition->syncChannelValue(getPosition());
      mAngle->syncChannelValue(getAngle());
      // - done
      allDone(aApplyDoneCB);
      break;
  }
}


void ShadowBehaviour::allDone(SimpleCB aApplyDoneCB)
{
  moveTimerStop();
  mMovementCB = NoOP;
  mBlindState = blind_idle;
  OLOG(LOG_INFO, "End of movement sequence, reached position=%.1f%%, angle=%.1f", mReferencePosition, mReferenceAngle);
  if (aApplyDoneCB) {
    // apply not yet confirmed
    aApplyDoneCB();
  }
  else {
    // push final state to bridges (not to dS)
    OLOG(LOG_INFO, "- was a long movement, apply confirmed earlier -> re-push output state to bridges");
    // - end simulation transitions
    mPosition->setTransitionProgress(1);
    mAngle->setTransitionProgress(1);
    reportOutputState();
  }
}



void ShadowBehaviour::applyPosition(SimpleCB aApplyDoneCB)
{
  // decide what to do next
  if (mPosition->needsApplying()) {
    FOCUSLOG("- starting position apply: %.1f -> %.1f", mReferencePosition, mPosition->getChannelValue());
    // set new position
    mTargetPosition = mPosition->getChannelValue();
    // as position changes angle, make sure we have a valid target angle (even in case it is not marked needsApplying() right now)
    mTargetAngle = mAngle->getChannelValue();
    // new position requested, calculate next move
    double dist = 0;
    double probableDist = 0;
    MLMicroSeconds stopIn = 0;
    MLMicroSeconds probablyEndsIn = 0;
    mRunIntoEnd = false;
    // full up or down always schedule full way to synchronize
    probableDist = mTargetPosition-mReferencePosition; // when our current status is correct
    if (mTargetPosition>=100) {
      // fully up, always do full cycle to synchronize position
      dist = 120; // 20% extra to fully run into end switch
      mRunIntoEnd = true; // if we have end switches, let them stop the movement
      if (mReferencePosition<=0) mUpdateMoveTimeAtEndReached = true; // full range movement, use it to update movement time
    }
    else if (mTargetPosition<=0) {
      // fully down, always do full cycle to synchronize position
      dist = -120; // 20% extra to fully run into end switch
      mRunIntoEnd = true; // if we have end switches, let them stop the movement
      if (mReferencePosition>=100) mUpdateMoveTimeAtEndReached = true; // full range movement, use it to update movement time
    }
    else {
      // somewhere in between, actually estimate distance
      dist = probableDist; // distance to move up
    }
    // calculate moving time
    if (dist>0) {
      // we'll move up
      FOCUSLOG("- currently saved open time: %.1f, angle open time: %.2f", mOpenTime, mAngleOpenTime);
      mMovingUp = true;
      stopIn = mOpenTime*Second/100.0*dist;
      probablyEndsIn = mOpenTime*Second/100.0*probableDist;
      // we only want moves which result in a defined angle -> stretch when needed
      if (stopIn<mAngleOpenTime)
        stopIn = mAngleOpenTime;
    }
    else if (dist<0) {
      // we'll move down
      FOCUSLOG("- currently saved close time: %.1f, angle close time: %.2f", mCloseTime, mAngleCloseTime);
      mMovingUp = false;
      stopIn = mCloseTime*Second/100.0*-dist;
      probablyEndsIn = mCloseTime*Second/100.0*-probableDist;
      // we only want moves which result in a defined angle -> stretch when needed
      if (stopIn<mAngleCloseTime)
        stopIn = mAngleCloseTime;
    }
    OLOG(LOG_INFO,
      "Blind position=%.1f%% requested, current=%.1f%% -> moving %s for %.3f Seconds, probably already in %.3f Seconds",
      mTargetPosition, mReferencePosition, dist>0 ? "up" : "down", (double)stopIn/Second, (double)probablyEndsIn/Second
    );
    // start moving position if not already moving (dimming case)
    if (mBlindState!=blind_positioning) {
      mBlindState = blind_positioning;
      // - start a simulating transition of the position
      mPosition->startExternallyTimedTransition(probablyEndsIn);
      startMoving(stopIn, aApplyDoneCB);
    }
  }
  else {
    // position already ok: only if angle has to change, we'll have to do anything at all
    if (mAngle->needsApplying()) {
      // apply angle separately
      mTargetAngle = mAngle->getChannelValue();
      applyAngle(aApplyDoneCB);
    }
    else {
      // nothing to do at all, confirm done
      allDone(aApplyDoneCB);
    }
  }
}


void ShadowBehaviour::applyAngle(SimpleCB aApplyDoneCB)
{
  // determine current angle (100 = fully open)
  if (mShadowDeviceKind!=shadowdevice_jalousie) {
    // ignore angle, just consider done
    allDone(aApplyDoneCB);
  }
  else if (getPosition()>=100) {
    // blind is fully up, angle is irrelevant -> consider applied
    mReferenceAngle = mTargetAngle;
    mAngle->channelValueApplied();
    allDone(aApplyDoneCB);
  }
  else {
    FOCUSLOG("- starting angle apply: %.1f -> %.1f", mReferenceAngle, mTargetAngle);
    double dist = mTargetAngle-mReferenceAngle; // distance to move up
    MLMicroSeconds stopIn = 0;
    // calculate new stop time
    if (dist>0) {
      mMovingUp = true;
      stopIn = mAngleOpenTime*Second/100.0*dist; // up
    }
    else if (dist<0) {
      mMovingUp = false;
      stopIn = mAngleCloseTime*Second/100.0*-dist; // down
    }
    // For full opened or closed, add 20% to make sure we're in sync
    if (mTargetAngle>=100 || mTargetAngle<=0) stopIn *= 1.2;
    OLOG(LOG_INFO,
      "Blind angle=%.1f%% requested, current=%.1f%% -> moving %s for %.3f Seconds",
      mTargetAngle, mReferenceAngle, dist>0 ? "up" : "down", (double)stopIn/Second
    );
    // start moving angle
    mBlindState = blind_turning;
    // - start a simulating transition of the angle
    mAngle->startExternallyTimedTransition(stopIn);
    startMoving(stopIn, aApplyDoneCB);
  }
}



void ShadowBehaviour::startMoving(MLMicroSeconds aStopIn, SimpleCB aApplyDoneCB)
{
  // determine direction
  int dir = mMovingUp ? 1 : -1;
  // check if we can do the move in one part
  if (aStopIn<mMinMoveTime) {
    // end of this move
    if (mBlindState==blind_positioning)
      mBlindState = blind_stopping_before_turning;
    stopped(aApplyDoneCB);
    return;
  }
  // actually start moving
  FOCUSLOG("- start moving into direction = %d", dir);
  // - start the movement
  mMovementCB(boost::bind(&ShadowBehaviour::moveStarted, this, aStopIn, aApplyDoneCB), dir);
}


void ShadowBehaviour::moveStarted(MLMicroSeconds aStopIn, SimpleCB aApplyDoneCB)
{
  // started
  moveTimerStart();
  // schedule stop if not moving into end positions (and end contacts are available)
  if (mHasEndContacts && mRunIntoEnd) {
    FOCUSLOG("- move started, let movement run into end contacts");
  }
  else {
    // calculate stop time and set timer
    MLMicroSeconds remaining = aStopIn;
    if (mMaxShortMoveTime>0 && aStopIn<mMinLongMoveTime && aStopIn>mMaxShortMoveTime) {
      // need multiple shorter segments
      if (remaining<2*mMinLongMoveTime && remaining>2*mMinMoveTime) {
        // evenly divide
        remaining /= 2;
        aStopIn = remaining;
      }
      else {
        // reduce by max short time move and carry over rest
        aStopIn = mMaxShortMoveTime;
        remaining-=aStopIn;
      }
      FOCUSLOG("- must restrict to %.3f Seconds now (%.3f later) to prevent starting continuous blind movement", (double)aStopIn/Second, (double)remaining/Second);
    }
    else {
      remaining = 0;
    }
    if (aStopIn>MIN_INTERRUPTABLE_MOVE_TIME) {
      // this is a long move, allow interrupting it
      // - which means that we confirm applied now
      FOCUSLOG("- is long move, should be interruptable -> confirming applied now");
      if (aApplyDoneCB) aApplyDoneCB();
      // - and prevent calling back again later
      aApplyDoneCB = NoOP;
      // schedule progress updates
      MLMicroSeconds r = outputReportInterval();
      if (r!=Never) {
        mProgressTicket.executeOnce(boost::bind(&ShadowBehaviour::progressReport, this, _2), r);
      }
    }
    FOCUSLOG("- move started, scheduling stop in %.3f Seconds", (double)aStopIn/Second);
    mMovingTicket.executeOnce(boost::bind(&ShadowBehaviour::endMove, this, remaining, aApplyDoneCB), aStopIn);
  }
}


void ShadowBehaviour::progressReport(MLMicroSeconds aNow)
{
  // issue an intermediate output channel progress report
  mPosition->updateTimedTransition(aNow, 0.9); // do not simulate progress beyond 90%
  mAngle->updateTimedTransition(aNow, 0.9); // do not simulate progress beyond 90%
  reportOutputState();
  // - reschedule
  mProgressTicket.executeOnce(boost::bind(&ShadowBehaviour::progressReport, this, _2), outputReportInterval());
}


void ShadowBehaviour::endMove(MLMicroSeconds aRemainingMoveTime, SimpleCB aApplyDoneCB)
{
  mProgressTicket.cancel();
  if (aRemainingMoveTime<=0) {
    // move is complete, regular stop
    stop(aApplyDoneCB);
  }
  else {
    // move is segmented, needs pause now and restart later
    // - stop (=start pause)
    FOCUSLOG("- end of move segment, pause now");
    mMovementCB(boost::bind(&ShadowBehaviour::movePaused, this, aRemainingMoveTime, aApplyDoneCB), 0);
  }
}


void ShadowBehaviour::movePaused(MLMicroSeconds aRemainingMoveTime, SimpleCB aApplyDoneCB)
{
  // paused, restart afterwards
  FOCUSLOG("- move paused, waiting to start next segment");
  // must update reference values between segments as well, otherwise estimate will include pause
  moveTimerStop();
  // schedule next segment
  mSequenceTicket.executeOnce(boost::bind(&ShadowBehaviour::startMoving, this, aRemainingMoveTime, aApplyDoneCB), INTER_SHORT_MOVE_DELAY);
}


// MARK: - behaviour interaction with Digital Strom system


void ShadowBehaviour::loadChannelsFromScene(DsScenePtr aScene)
{
  ShadowScenePtr shadowScene = boost::dynamic_pointer_cast<ShadowScene>(aScene);
  if (shadowScene) {
    // load position and angle from scene
    mPosition->setChannelValueIfNotDontCare(shadowScene, shadowScene->value, 0, 0, true);
    mAngle->setChannelValueIfNotDontCare(shadowScene, shadowScene->mAngle, 0, 0, true);
  }
}


void ShadowBehaviour::saveChannelsToScene(DsScenePtr aScene)
{
  ShadowScenePtr shadowScene = boost::dynamic_pointer_cast<ShadowScene>(aScene);
  if (shadowScene) {
    // save position and angle to scene
    shadowScene->setPVar(shadowScene->value, mPosition->getChannelValue());
    shadowScene->setSceneValueFlags(mPosition->getChannelIndex(), valueflags_dontCare, false);
    shadowScene->setPVar(shadowScene->mAngle, mAngle->getChannelValue());
    shadowScene->setSceneValueFlags(mAngle->getChannelIndex(), valueflags_dontCare, false);
  }
}


bool ShadowBehaviour::reapplyRestoredChannels()
{
  // only absolute movement capable devices should be restored.
  // For relative movement controlled blinds, we can assume power outage does NOT change
  // the hardware state, and re-applying would more likely mess it up rather than preserve it.
  return mAbsoluteMovement;
}


void ShadowBehaviour::performSceneActions(DsScenePtr aScene, SimpleCB aDoneCB)
{
  // none of my effects, let inherited check
  inherited::performSceneActions(aScene, aDoneCB);
}


void ShadowBehaviour::stopSceneActions()
{
  // stop
  stop(NoOP);
  // let inherited stop as well
  inherited::stopSceneActions();
}


#define IDENTITY_MOVE_TIME (Second*1.5)

void ShadowBehaviour::identifyToUser(MLMicroSeconds aDuration)
{
  mSequenceTicket.cancel();
  if (aDuration<0) {
    // stop right now
    mDevice.dimChannelForArea(mDevice.getChannelByIndex(0), dimmode_stop, -1, 0);
  }
  else {
    // move a little (once or several times, depending on duration)
    VdcDimMode dimMode = mPosition->getChannelValue()>50 ? dimmode_down : dimmode_up;
    int steps = (aDuration==Never ? 0 : aDuration/IDENTITY_MOVE_TIME/2)*2+1; // at least one repetition, forth and back
    identifyStep(dimMode, steps);
  }
}

void ShadowBehaviour::identifyStep(VdcDimMode aDimMode, int aRepetitions)
{
  mDevice.dimChannelForArea(mDevice.getChannelByIndex(0), aDimMode, -1, IDENTITY_MOVE_TIME);
  aRepetitions--;
  if (aRepetitions<0) {
    mSequenceTicket.cancel();
    return; // done
  }
  // again with reversed direction
  mSequenceTicket.executeOnce(
    boost::bind(&ShadowBehaviour::identifyStep, this, aDimMode==dimmode_up ? dimmode_down : dimmode_up, aRepetitions),
    IDENTITY_MOVE_TIME
  );
}


// MARK: - persistence implementation


const char *ShadowBehaviour::tableName()
{
  return "ShadowOutputSettings";
}


// data field definitions

static const size_t numFields = 5;

size_t ShadowBehaviour::numFieldDefs()
{
  return inherited::numFieldDefs()+numFields;
}


const FieldDefinition *ShadowBehaviour::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numFields] = {
    { "openTime", SQLITE_FLOAT },
    { "closeTime", SQLITE_FLOAT },
    { "angleOpenTime", SQLITE_FLOAT },
    { "angleCloseTime", SQLITE_FLOAT },
    { "stopDelayTime", SQLITE_FLOAT },
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void ShadowBehaviour::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inherited::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get the fields
  aRow->getIfNotNull<double>(aIndex++, mOpenTime);
  aRow->getIfNotNull<double>(aIndex++, mCloseTime);
  aRow->getIfNotNull<double>(aIndex++, mAngleOpenTime);
  aRow->getIfNotNull<double>(aIndex++, mAngleCloseTime);
  aRow->getIfNotNull<double>(aIndex++, mStopDelayTime);
}


// bind values to passed statement
void ShadowBehaviour::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, mOpenTime);
  aStatement.bind(aIndex++, mCloseTime);
  aStatement.bind(aIndex++, mAngleOpenTime);
  aStatement.bind(aIndex++, mAngleCloseTime);
  aStatement.bind(aIndex++, mStopDelayTime);
}



// MARK: - property access


static char shadow_key;

// settings properties

enum {
  openTime_key,
  closeTime_key,
  angleOpenTime_key,
  angleCloseTime_key,
  stopDelayTime_key,
  numSettingsProperties
};


int ShadowBehaviour::numSettingsProps() { return inherited::numSettingsProps()+numSettingsProperties; }
const PropertyDescriptorPtr ShadowBehaviour::getSettingsDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numSettingsProperties] = {
    { "openTime", apivalue_double, openTime_key+settings_key_offset, OKEY(shadow_key) },
    { "closeTime", apivalue_double, closeTime_key+settings_key_offset, OKEY(shadow_key) },
    { "angleOpenTime", apivalue_double, angleOpenTime_key+settings_key_offset, OKEY(shadow_key) },
    { "angleCloseTime", apivalue_double, angleCloseTime_key+settings_key_offset, OKEY(shadow_key) },
    { "stopDelayTime", apivalue_double, stopDelayTime_key+settings_key_offset, OKEY(shadow_key) },
  };
  int n = inherited::numSettingsProps();
  if (aPropIndex<n)
    return inherited::getSettingsDescriptorByIndex(aPropIndex, aParentDescriptor);
  aPropIndex -= n;
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}


// state properties
enum {
  movingState_key,
  numStateProperties
};


int ShadowBehaviour::numStateProps() { return inherited::numStateProps()+numStateProperties; }
const PropertyDescriptorPtr ShadowBehaviour::getStateDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numStateProperties] = {
    { "movingState", apivalue_int64, movingState_key+states_key_offset, OKEY(shadow_key) },
  };
  int n = inherited::numStateProps();
  if (aPropIndex<n)
    return inherited::getStateDescriptorByIndex(aPropIndex, aParentDescriptor);
  aPropIndex -= n;
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}


// access to all fields

bool ShadowBehaviour::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(shadow_key)) {
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Settings properties
        case openTime_key+settings_key_offset: aPropValue->setDoubleValue(mOpenTime); return true;
        case closeTime_key+settings_key_offset: aPropValue->setDoubleValue(mCloseTime); return true;
        case angleOpenTime_key+settings_key_offset: aPropValue->setDoubleValue(mAngleOpenTime); return true;
        case angleCloseTime_key+settings_key_offset: aPropValue->setDoubleValue(mAngleCloseTime); return true;
        case stopDelayTime_key+settings_key_offset: aPropValue->setDoubleValue(mStopDelayTime); return true;
        // State properties
        case movingState_key+states_key_offset: aPropValue->setInt8Value(mBlindState==blind_idle ? 0 : (mMovingUp ? 1 : -1)); return true;
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Settings properties
        case openTime_key+settings_key_offset: setPVar(mOpenTime, aPropValue->doubleValue()); return true;
        case closeTime_key+settings_key_offset: setPVar(mCloseTime, aPropValue->doubleValue()); return true;
        case angleOpenTime_key+settings_key_offset: setPVar(mAngleOpenTime, aPropValue->doubleValue()); return true;
        case angleCloseTime_key+settings_key_offset: setPVar(mAngleCloseTime, aPropValue->doubleValue()); return true;
        case stopDelayTime_key+settings_key_offset: setPVar(mStopDelayTime, aPropValue->doubleValue()); return true;
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


// MARK: - description/shortDesc


string ShadowBehaviour::shortDesc()
{
  return string("Shadow");
}


string ShadowBehaviour::description()
{
  string s = string_format("%s behaviour", shortDesc().c_str());
  string_format_append(s, "\n- position = %.1f, angle = %.1f, localPriority = %d", mPosition->getChannelValue(), mAngle->getChannelValue(), hasLocalPriority());
  s.append(inherited::description());
  return s;
}







