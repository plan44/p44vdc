//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2016-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#define FOCUSLOGLEVEL IFNOTREDUCEDFOOTPRINT(7)

#include "evaluatordevice.hpp"

#if ENABLE_EVALUATORS

#include "evaluatorvdc.hpp"

#include "buttonbehaviour.hpp"
#include "binaryinputbehaviour.hpp"
#include "sensorbehaviour.hpp"

using namespace p44;


EvaluatorDevice::EvaluatorDevice(EvaluatorVdc *aVdcP, const string &aEvaluatorID, const string &aEvaluatorConfig) :
  inherited((Vdc *)aVdcP),
  evaluatorDeviceRowID(0),
  mEvaluatorID(aEvaluatorID),
  mEvaluatorType(evaluator_unknown),
  mEvaluatorState(undefined),
  #if !ENABLE_P44SCRIPT
  currentOn(undefined),
  currentOff(undefined),
  conditionMetSince(Never),
  onConditionMet(false),
  #endif
  mReporting(false)
{
#if ENABLE_P44SCRIPT
  mValueMapper.isMemberVariable();
#endif
  // Config is:
  //  <behaviour mode>
  int st, su;
  if (aEvaluatorConfig=="rocker")
    mEvaluatorType = evaluator_rocker;
  else if (aEvaluatorConfig=="input")
    mEvaluatorType = evaluator_input;
  else if (aEvaluatorConfig=="internal" || aEvaluatorConfig=="internalinput") // "internal" must be still recognized for backwards compatibility with existing settings!
    mEvaluatorType = evaluator_internalinput;
#if P44SCRIPT_FULL_SUPPORT
  else if (aEvaluatorConfig=="internalaction")
    mEvaluatorType = evaluator_internalaction;
#endif
  else if (sscanf(aEvaluatorConfig.c_str(), "sensor:%d:%d", &st, &su)==2) {
    mEvaluatorType = evaluator_sensor;
    mSensorType = (VdcSensorType)st;
    mSensorUsage = (VdcUsageHint)su;
  }
  else if (sscanf(aEvaluatorConfig.c_str(), "internalsensor:%d:%d", &st, &su)==2) {
    mEvaluatorType = evaluator_internalsensor;
    mSensorType = (VdcSensorType)st;
    mSensorUsage = (VdcUsageHint)su;
  }
  else {
    LOG(LOG_ERR, "unknown evaluator type: %s", aEvaluatorConfig.c_str());
  }
  // install our specific settings
  installSettings(DeviceSettingsPtr(new EvaluatorDeviceSettings(*this, mEvaluatorType==evaluator_sensor  || mEvaluatorType==evaluator_internalsensor)));
  // create "inputs" that will deliver the evaluator's result
  if (mEvaluatorType==evaluator_rocker) {
    // Simulate Two-way Rocker Button device
    // - defaults to black (generic button)
    mColorClass = class_black_joker;
    // - create down button (index 0)
    ButtonBehaviourPtr b = ButtonBehaviourPtr(new ButtonBehaviour(*this,"evaldown"));
    b->setHardwareButtonConfig(0, buttonType_2way, buttonElement_down, false, 1, 0); // counterpart up-button has buttonIndex 1, fixed mode
    b->setHardwareName("off condition met");
    b->setGroup(group_black_variable); // pre-configure for app button
    addBehaviour(b);
    // - create up button (index 1)
    b = ButtonBehaviourPtr(new ButtonBehaviour(*this,"evalup"));
    b->setHardwareButtonConfig(0, buttonType_2way, buttonElement_up, false, 0, 0); // counterpart down-button has buttonIndex 0, fixed mode
    b->setHardwareName("on condition met");
    b->setGroup(group_black_variable); // pre-configure for app button
    addBehaviour(b);
  }
  else if (mEvaluatorType==evaluator_input || mEvaluatorType==evaluator_internalinput) {
    // Standard device settings without scene table (internal differs only from not getting announced with vdsm)
    mColorClass = class_black_joker;
    // - create one binary input
    BinaryInputBehaviourPtr b = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*this,"evalresult"));
    b->setHardwareInputConfig(binInpType_none, usage_undefined, true, Never, Never);
    b->setHardwareName("evaluation decision");
    addBehaviour(b);
  }
  else if (mEvaluatorType==evaluator_sensor  || mEvaluatorType==evaluator_internalsensor) {
    // Standard device settings without scene table (internal differs only from not getting announced with vdsm)
    mColorClass = class_black_joker;
    // - create one sensor
    SensorBehaviourPtr s = SensorBehaviourPtr(new SensorBehaviour(*this,"evalresult"));
    s->setHardwareSensorConfig(mSensorType, mSensorUsage, 0, 0, 0, 100*MilliSecond, 0);
    s->setHardwareName("calculated sensor result");
    addBehaviour(s);
  }
  deriveDsUid();
}


void EvaluatorDevice::willBeAdded()
{
  // set script ids based on dSUID now
  #if P44SCRIPT_FULL_SUPPORT
  evaluatorSettings()->mAction.setScriptSourceUid(string_format("eval_%s.action", getDsUid().getString().c_str()));
  #endif
}


EvaluatorDevice::~EvaluatorDevice()
{
}


bool EvaluatorDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Nothing to do to identify for now
  return true; // simple identification, callback will not be called
}




bool EvaluatorDevice::isPublicDS()
{
  return
    mEvaluatorType!=evaluator_internalinput && mEvaluatorType!=evaluator_internalsensor && mEvaluatorType!=evaluator_internalaction && // not internal-only...
    inherited::isPublicDS(); // ...and base class has dS enabled
}


EvaluatorVdc &EvaluatorDevice::getEvaluatorVdc()
{
  return *(static_cast<EvaluatorVdc *>(mVdcP));
}


void EvaluatorDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  // clear learn-in data from DB
  if (evaluatorDeviceRowID) {
    if(getEvaluatorVdc().mDb.executef("DELETE FROM evaluators WHERE rowid=%lld", evaluatorDeviceRowID)!=SQLITE_OK) {
      OLOG(LOG_ERR, "Error deleting evaluator: %s", getEvaluatorVdc().mDb.error()->description().c_str());
    }
  }
  // disconnection is immediate, so we can call inherited right now
  inherited::disconnect(aForgetParams, aDisconnectResultHandler);
}


string EvaluatorDevice::modelName()
{
  switch (mEvaluatorType) {
    case evaluator_rocker: return "evaluated up/down button";
    case evaluator_input: return "evaluated input";
    case evaluator_internalinput: return "internal on/off signal";
    case evaluator_internalaction: return "evaluated action trigger";
    case evaluator_sensor: return "evaluated sensor";
    case evaluator_internalsensor: return "internal sensor value";
    default: break;
  }
  return "";
}



bool EvaluatorDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("evaluator", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


void EvaluatorDevice::initializeDevice(StatusCB aCompletedCB, bool aFactoryReset)
{
  // try connecting to values now. In case not all values are found, this will be re-executed later
  parseVarDefs();
  // done
  inherited::initializeDevice(aCompletedCB, aFactoryReset);
}


ErrorPtr EvaluatorDevice::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  if (aMethod=="x-p44-checkEvaluator") {
    // Check the evaluator
    ApiValuePtr checkResult = aRequest->newApiValue();
    checkResult->setType(apivalue_object);
    // - variable definitions
    parseVarDefs(); // reparse
    OLOG(LOG_INFO, "CheckEvaluator:");
    ApiValuePtr varDefs = checkResult->newObject();
    if (mValueMapper.getMappedSourcesInfo(varDefs)) {
      checkResult->add("varDefs", varDefs);
    }
    // Conditions
    ApiValuePtr cond;
    ScriptObjPtr res;
    // - on condition (or calculation for sensors)
    cond = checkResult->newObject();
    res = evaluatorSettings()->mOnCondition.run(initial|synchronously, NoOP, ScriptObjPtr(), 2*Second);
    cond->add("expression", checkResult->newString(evaluatorSettings()->mOnCondition.getSource()));
    if (!res->isErr()) {
      cond->add("result", cond->newScriptValue(res));
      cond->add("text", cond->newString(res->defined() ? res->stringValue() : res->getAnnotation()));
      LOG(LOG_INFO, "- onCondition '%s' -> %s", evaluatorSettings()->mOnCondition.getSource().c_str(), ScriptObj::describe(res).c_str());
    }
    else {
      cond->add("error", cond->newString(res->errorValue()->getErrorMessage()));
      SourceCursor* cursor = res->cursor();
      if (cursor) {
        cond->add("at", cond->newUint64(cursor->textpos()));
        cond->add("line", cond->newUint64(cursor->lineno()));
        cond->add("char", cond->newUint64(cursor->charpos()));
      }
    }
    checkResult->add("onCondition", cond);
    if (mEvaluatorType!=evaluator_sensor && mEvaluatorType!=evaluator_internalsensor) {
      // - off condition
      cond = checkResult->newObject();
      cond->add("expression", checkResult->newString(evaluatorSettings()->mOffCondition.getSource()));
      if (evaluatorSettings()->mOffCondition.empty()) {
        res.reset();
        LOG(LOG_INFO, "- offCondition is empty -> disabled");
      }
      else {
        res = evaluatorSettings()->mOffCondition.run(initial|synchronously, NoOP, ScriptObjPtr(), 2*Second);
        if (!res->isErr()) {
          cond->add("result", cond->newScriptValue(res));
          cond->add("text", cond->newString(res->defined() ? res->stringValue() : res->getAnnotation()));
          LOG(LOG_INFO, "- offCondition '%s' -> %s", evaluatorSettings()->mOffCondition.getSource().c_str(), ScriptObj::describe(res).c_str());
        }
        else {
          cond->add("error", cond->newString(res->errorValue()->getErrorMessage()));
          SourceCursor* cursor = res->cursor();
          if (cursor) {
            cond->add("at", cond->newUint64(cursor->textpos()));
            cond->add("line", cond->newUint64(cursor->lineno()));
            cond->add("char", cond->newUint64(cursor->charpos()));
          }
        }
      }
      checkResult->add("offCondition", cond);
    }
    // return the result
    aRequest->sendResult(checkResult);
    return ErrorPtr();
  }
  #if P44SCRIPT_FULL_SUPPORT
  else if (aMethod=="x-p44-testEvaluatorAction") {
    ApiValuePtr vp = aParams->get("result");
    Tristate state = mEvaluatorState;
    if (vp) {
      state = vp->boolValue() ? yes : no;
    }
    // now test
    evaluatorSettings()->mEvaluatorContext->setMemberByName("result", new BoolValue(state==yes));
    evaluatorSettings()->mAction.run(stopall, boost::bind(&EvaluatorDevice::testActionExecuted, this, aRequest, _1), ScriptObjPtr(), Infinite);
    return ErrorPtr();
  }
  else if (aMethod=="x-p44-stopEvaluatorAction") {
    evaluatorSettings()->mEvaluatorContext->abort(stopall, new ErrorValue(ScriptError::Aborted, "evaluator action stopped"));
    return Error::ok();
  }
  #endif // P44SCRIPT_FULL_SUPPORT
  else {
    return inherited::handleMethod(aRequest, aMethod, aParams);
  }
}


#if P44SCRIPT_FULL_SUPPORT

void EvaluatorDevice::testActionExecuted(VdcApiRequestPtr aRequest, ScriptObjPtr aResult)
{
  ApiValuePtr testResult = aRequest->newApiValue();
  testResult->setType(apivalue_object);
  if (!aResult->isErr()) {
    testResult->add("result", testResult->newScriptValue(aResult));
  }
  else {
    testResult->add("error", testResult->newString(aResult->errorValue()->getErrorMessage()));
    SourceCursor* cursor = aResult->cursor();
    if (cursor) {
      testResult->add("at", testResult->newUint64(cursor->textpos()));
      testResult->add("line", testResult->newUint64(cursor->lineno()));
      testResult->add("char", testResult->newUint64(cursor->charpos()));
    }
  }
  aRequest->sendResult(testResult);
}

#endif // P44SCRIPT_FULL_SUPPORT


#define REPARSE_DELAY (30*Second)

void EvaluatorDevice::handleGlobalEvent(VdchostEvent aEvent)
{
  if (aEvent==vdchost_devices_initialized) {
    parseVarDefs();
  }
  else if (aEvent==vdchost_network_reconnected || aEvent==vdchost_timeofday_changed) {
    // network coming up might change local time
    if (!mValueParseTicket) {
      // Note: if variable re-parsing is already scheduled, this will re-evaluate anyway
      //   Otherwise: have condition re-evaluated (because it possibly contain references to local time)
      mValueParseTicket.executeOnce(boost::bind(&EvaluatorDevice::evaluateConditions, this, (EvaluationFlags)P44Script::timed), REPARSE_DELAY);
    }
  }
  inherited::handleGlobalEvent(aEvent);
}


void EvaluatorDevice::parseVarDefs()
{
  mValueParseTicket.cancel();
  string newValueDefs; // re-created value defs using sensor ids rather than indices, for migration
  bool foundall = mValueMapper.parseMappingDefs(
    evaluatorSettings()->mVarDefs,
    &newValueDefs
  );
  if (!newValueDefs.empty()) {
    // migrate old definitions (when re-created definitions are not equal to stored ones)
    // Note: even migrate partially, when not all defs could be resolved yet
    OLOG(LOG_NOTICE, "Migrating definitions to new id (rather than index) based form");
    evaluatorSettings()->setPVar(evaluatorSettings()->mVarDefs, newValueDefs);
  }
  if (!foundall) {
    // schedule a re-parse later
    OLOG(LOG_WARNING, "not all value mappings could be resolved now, retrying later...");
    mValueParseTicket.executeOnce(boost::bind(&EvaluatorDevice::parseVarDefs, this), REPARSE_DELAY);
  }
  else {
    // run an initial evaluation to calculate default values and possibly schedule timed re-evaluations
    evaluateConditions(P44Script::timed);
  }
}


void EvaluatorDevice::changedConditions()
{
  mEvaluatorState = undefined;
  evaluateConditions(P44Script::initial);
}


void EvaluatorDevice::evaluateConditions(EvaluationFlags aRunMode)
{
  evaluatorSettings()->mOnCondition.evaluate(aRunMode);
  if (!evaluatorSettings()->mOffCondition.empty()) evaluatorSettings()->mOffCondition.evaluate(aRunMode);
}


void EvaluatorDevice::handleTrigger(bool aOnCondition, ScriptObjPtr aResult)
{
  if (mEvaluatorType==evaluator_sensor || mEvaluatorType==evaluator_internalsensor) {
    // sensor evaluator was re-evaluated
    SensorBehaviourPtr s = getSensor(0);
    if (s) {
      // protect against state updates triggering evaluation again via cyclic references
      mReporting = true;
      if (aResult->defined()) {
        FOCUSOLOG("===== sensor expression result: '%s' = '%s' = %f", evaluatorSettings()->mOnCondition.getSource().c_str(), aResult->stringValue().c_str(), aResult->doubleValue());
        s->updateSensorValue(aResult->doubleValue());
      }
      else {
        OLOG(LOG_INFO,"Sensor expression '%s' evaluation status: %s", evaluatorSettings()->mOnCondition.getSource().c_str(), aResult->stringValue().c_str());
        s->invalidateSensorValue();
      }
      // done reporting, critical phase is over
      mReporting = false;
    }
  }
  else {
    // binary evaluator expression was re-evaluated
    Tristate newConditionState = undefined;
    if (aResult->defined()) newConditionState = aResult->boolValue() ? yes : no;
    // now derive decision
    Tristate prevState = mEvaluatorState;
    bool decisionMade = false;
    if (!decisionMade && prevState!=yes && aOnCondition) {
      // off or unknown, and on condition has changed: check for switching on
      OLOG(LOG_INFO, "onCondition '%s' evaluates to %s", evaluatorSettings()->mOnCondition.getSource().c_str(), newConditionState==undefined ? "<undefined>" : (newConditionState==yes ? "true -> switching ON" : "false"));
      if (newConditionState==yes) {
        mEvaluatorState = yes;
        decisionMade = true;
      }
    }
    if (!decisionMade && prevState!=no && !aOnCondition) {
      // on or unknown, and off condition has changed: check for switching off
      OLOG(LOG_INFO, "offCondition '%s' evaluates to %s", evaluatorSettings()->mOffCondition.getSource().c_str(), newConditionState==undefined ? "<undefined>" : (newConditionState==yes ? "true -> switching OFF" : "false"));
      if (newConditionState==yes) {
        mEvaluatorState = no;
        decisionMade = true;
      }
    }
    // one condition side getting false while the other side is ALREADY true must be handled, too
    if (!decisionMade && newConditionState==no) {
      // check if the opposite is true
      Tristate otherConditionState = (aOnCondition ? evaluatorSettings()->mOffCondition : evaluatorSettings()->mOnCondition).currentBoolState();
      if (otherConditionState==yes) {
        mEvaluatorState = aOnCondition ? no : yes; // the OTHER condition causes a state change!
        if (mEvaluatorState!=prevState) {
          OLOG(LOG_INFO, "%sCondition was already true while %sCondition gets false -> switching %s", aOnCondition ? "off" : "on", aOnCondition ? "on" : "off", mEvaluatorState==yes ? "ON" : "OFF");
          decisionMade = true;
        }
      }
    }
    if (mEvaluatorState!=undefined) {
      // re-check opposite condition as "triggered" in case it is static (such as default fallbacks to true or false)
      MainLoop::currentMainLoop().executeNow(boost::bind(
        &TriggerSource::evaluate,
        aOnCondition ? evaluatorSettings()->mOffCondition : evaluatorSettings()->mOnCondition,
        (EvaluationFlags)triggered
      ));
      // report new decision
      if (decisionMade) {
        // protect against state updates triggering evaluation again via cyclic references
        mReporting = true;
        // give some context info
        OLOG(LOG_NOTICE, "new evaluation: %s based on %s values: %s",
          mEvaluatorState==yes ? "TRUE" : "FALSE",
          prevState==undefined ? "new" : "timing and",
          mValueMapper.shortDesc().c_str()
        );
        // report it
        switch (mEvaluatorType) {
          case evaluator_input :
          case evaluator_internalinput :
          {
            BinaryInputBehaviourPtr b = getInput(0);
            if (b) {
              b->updateInputState(mEvaluatorState==yes);
            }
            break;
          }
          case evaluator_rocker : {
            if (mEvaluatorState!=prevState) {
              // virtually click up or down button
              ButtonBehaviourPtr b = getButton(mEvaluatorState==no ? 0 : 1);
              if (b) {
                b->sendClick(ct_tip_1x);
              }
            }
            break;
          }
          #if P44SCRIPT_FULL_SUPPORT
          case evaluator_internalaction: {
            // execute actions (but let trigger evaluation IN SAME CONTEXT actually finish first)
            MainLoop::currentMainLoop().executeNow(boost::bind(&EvaluatorDevice::executeActions, this));
            break;
          }
          #endif // P44SCRIPT_FULL_SUPPORT
          default: break;
        }
        // done reporting, critical phase is over
        mReporting = false;
      }
    }
  }
}


#if P44SCRIPT_FULL_SUPPORT

void EvaluatorDevice::executeActions()
{
  evaluatorSettings()->mEvaluatorContext->setMemberByName("result", new BoolValue(mEvaluatorState==yes));
  evaluatorSettings()->mAction.run(inherit, boost::bind(&EvaluatorDevice::actionExecuted, this, _1), ScriptObjPtr(), Infinite);
}


void EvaluatorDevice::actionExecuted(ScriptObjPtr aResult)
{
  OLOG(LOG_INFO, "evaluator action script completed with result: %s", ScriptObj::describe(aResult).c_str());
  if (evaluatorSettings()->mOffCondition.empty()) {
    // there is no off condition, so we just set the state back to NO
    OLOG(LOG_INFO, "offCondition is empty for action evaluator: one-shot behaviour, re-evaluate trigger condition");
    // give trigger condition chance to see changes done by action script, i.e. to become false
    // (but because currentState is still YES, this cannot cause a re-trigger regardless of what is the result
    evaluatorSettings()->mOnCondition.evaluate();
    // only now do we reset the evaluator state, so NEXT trigger evaluation would be able to re-trigger
    mEvaluatorState = no;
  }
}

#endif // P44SCRIPT_FULL_SUPPORT


void EvaluatorDevice::deriveDsUid()
{
  // vDC implementation specific UUID:
  //   UUIDv5 with name = classcontainerinstanceid::evaluatorID
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  string s = mVdcP->vdcInstanceIdentifier();
  s += "::" + mEvaluatorID;
  mDSUID.setNameInSpace(s, vdcNamespace);
}


string EvaluatorDevice::description()
{
  string s = inherited::description();
  if (mEvaluatorType==evaluator_rocker)
    string_format_append(s, "\n- evaluation controls simulated 2-way-rocker button");
  if (mEvaluatorType==evaluator_input)
    string_format_append(s, "\n- evaluation controls binary input");
  return s;
}


string EvaluatorDevice::getEvaluatorType()
{
  switch (mEvaluatorType) {
    case evaluator_rocker: return "rocker";
    case evaluator_input: return "input";
    case evaluator_internalinput: return "internalinput";
    case evaluator_internalaction: return "internalaction";
    case evaluator_sensor: return "sensor";
    case evaluator_internalsensor: return "internalsensor";
    case evaluator_unknown:
    default:
      return "unknown";
  }
}


// MARK: - property access

enum {
  evaluatorType_key,
  varDefs_key,
  onCondition_key,
  offCondition_key,
  minOnTime_key,
  minOffTime_key,
  #if P44SCRIPT_FULL_SUPPORT
  action_key,
  actionId_key,
  #endif
  numProperties
};

static char evaluatorDevice_key;


int EvaluatorDevice::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // Note: only add my own count when accessing root level properties!!
  if (aParentDescriptor->isRootOfObject()) {
    // Accessing properties at the Device (root) level, add mine
    return inherited::numProps(aDomain, aParentDescriptor)+numProperties;
  }
  // just return base class' count
  return inherited::numProps(aDomain, aParentDescriptor);
}


PropertyDescriptorPtr EvaluatorDevice::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numProperties] = {
    { "x-p44-evaluatorType", apivalue_string, evaluatorType_key, OKEY(evaluatorDevice_key) },
    { "x-p44-varDefs", apivalue_string, varDefs_key, OKEY(evaluatorDevice_key) },
    { "x-p44-onCondition", apivalue_string, onCondition_key, OKEY(evaluatorDevice_key) },
    { "x-p44-offCondition", apivalue_string, offCondition_key, OKEY(evaluatorDevice_key) },
    { "x-p44-minOnTime", apivalue_double, minOnTime_key, OKEY(evaluatorDevice_key) },
    { "x-p44-minOffTime", apivalue_double, minOffTime_key, OKEY(evaluatorDevice_key) },
    #if P44SCRIPT_FULL_SUPPORT
    { "x-p44-action", apivalue_string, action_key, OKEY(evaluatorDevice_key) },
    { "x-p44-actionId", apivalue_string, actionId_key, OKEY(evaluatorDevice_key) },
    #endif
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
bool EvaluatorDevice::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(evaluatorDevice_key)) {
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        case evaluatorType_key: aPropValue->setStringValue(getEvaluatorType()); return true;
        case varDefs_key: aPropValue->setStringValue(evaluatorSettings()->mVarDefs); return true;
        case onCondition_key: aPropValue->setStringValue(evaluatorSettings()->mOnCondition.getSource()); return true;
        case offCondition_key: aPropValue->setStringValue(evaluatorSettings()->mOffCondition.getSource()); return true;
        case minOnTime_key: aPropValue->setDoubleValue((double)(evaluatorSettings()->mOnCondition.getTriggerHoldoff())/Second); return true;
        case minOffTime_key: aPropValue->setDoubleValue((double)(evaluatorSettings()->mOffCondition.getTriggerHoldoff())/Second); return true;
        #if P44SCRIPT_FULL_SUPPORT
        case action_key: aPropValue->setStringValue(evaluatorSettings()->mAction.getSource()); return true;
        case actionId_key: aPropValue->setStringValue(evaluatorSettings()->mAction.scriptSourceUid()); return true;
        #endif
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        case varDefs_key:
          if (evaluatorSettings()->setPVar(evaluatorSettings()->mVarDefs, aPropValue->stringValue()))
            parseVarDefs(); // changed varDefs, re-parse them
          return true;
        case onCondition_key:
          if (evaluatorSettings()->mOnCondition.setTriggerSource(aPropValue->stringValue(), true)) {
            evaluatorSettings()->markDirty();
          }
          return true;
        case offCondition_key:
          if (evaluatorSettings()->mOffCondition.setTriggerSource(aPropValue->stringValue(), true)) {
            evaluatorSettings()->markDirty();
          }
          return true;
        case minOnTime_key:
          if (evaluatorSettings()->mOnCondition.setTriggerHoldoff((MLMicroSeconds)(aPropValue->doubleValue()*Second), true)) {
            evaluatorSettings()->markDirty();
          }
          return true;
        case minOffTime_key:
          if (evaluatorSettings()->mOffCondition.setTriggerHoldoff((MLMicroSeconds)(aPropValue->doubleValue()*Second), true)) {
            evaluatorSettings()->markDirty();
          }
          return true;
        #if P44SCRIPT_FULL_SUPPORT
        case action_key:
          if (evaluatorSettings()->mAction.setAndStoreSource(aPropValue->stringValue())) {
            evaluatorSettings()->markDirty();
          }
          return true;
        #endif
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}



// MARK: - settings


EvaluatorDeviceSettings::EvaluatorDeviceSettings(EvaluatorDevice &aEvaluator, bool aIsSensor) :
  inherited(aEvaluator)
  // Note: conditions are synchronously evaluated, but action might be running when a condition wants evaluation, so we allow concurrent evaluation in that case
  ,mOnCondition("onCondition", nullptr, &mDevice, boost::bind(&EvaluatorDevice::handleTrigger, &aEvaluator, true, _1), aIsSensor ? onChange : onChangingBoolRisingHoldoffOnly, Never, expression|synchronously|keepvars|concurrently)
  ,mOffCondition("offCondition", nullptr, &mDevice, boost::bind(&EvaluatorDevice::handleTrigger, &aEvaluator, false, _1), aIsSensor ? inactive : onChangingBoolRisingHoldoffOnly, Never, expression|synchronously|keepvars|concurrently)
  #if P44SCRIPT_FULL_SUPPORT
  // Only thing that might run when action tries to run is an earlier invocation of the action.
  // However this might be a previous on-action, while the new action is a NOP off-action, so both must be allowed to run concurrently
  ,mAction(scriptbody|regular|keepvars|concurrently, "action", "%C (evaluator action)", &mDevice)
  #endif
{
  mEvaluatorContext = mOnCondition.domain()->newContext(); // common context for triggers and action
  mEvaluatorContext->registerMemberLookup(&aEvaluator.mValueMapper);
  mOnCondition.setSharedMainContext(mEvaluatorContext);
  mOffCondition.setSharedMainContext(mEvaluatorContext);
  #if P44SCRIPT_FULL_SUPPORT
  mAction.setSharedMainContext(mEvaluatorContext);
  #endif
}


const char *EvaluatorDeviceSettings::tableName()
{
  return "EvaluatorDeviceSettings";
}


// data field definitions

static const size_t numFields = 6;

size_t EvaluatorDeviceSettings::numFieldDefs()
{
  return inherited::numFieldDefs()+numFields;
}


const FieldDefinition *EvaluatorDeviceSettings::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numFields] = {
    { "valueDefs", SQLITE_TEXT }, // historically called "valueDefs", kept for DB backwards compatibility
    { "onCondition", SQLITE_TEXT },
    { "offCondition", SQLITE_TEXT },
    { "minOnTime", SQLITE_INTEGER },
    { "minOffTime", SQLITE_INTEGER },
    { "action", SQLITE_TEXT }, // note: this is a dummy if we don't have P44SCRIPT_FULL_SUPPORT
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void EvaluatorDeviceSettings::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inherited::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get the field values
  mVarDefs.assign(nonNullCStr(aRow->get<const char *>(aIndex++)));
  mOnCondition.setTriggerSource(nonNullCStr(aRow->get<const char *>(aIndex++)), false); // do not initialize at load yet
  mOffCondition.setTriggerSource(nonNullCStr(aRow->get<const char *>(aIndex++)), false); // do not initialize at load yet
  mOnCondition.setTriggerHoldoff(aRow->getCastedWithDefault<MLMicroSeconds, long long int>(aIndex++, Never), false); // do not initialize at load yet
  mOffCondition.setTriggerHoldoff(aRow->getCastedWithDefault<MLMicroSeconds, long long int>(aIndex++, Never), false); // do not initialize at load yet
  #if P44SCRIPT_FULL_SUPPORT
  mAction.loadSource(nonNullCStr(aRow->get<const char *>(aIndex++)));
  #else
  mOldAction = nonNullCStr(aRow->get<const char *>(aIndex++));
  #endif
}


// bind values to passed statement
void EvaluatorDeviceSettings::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, mVarDefs.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, mOnCondition.getSource().c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, mOffCondition.getSource().c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, (long long int)mOnCondition.getTriggerHoldoff());
  aStatement.bind(aIndex++, (long long int)mOffCondition.getTriggerHoldoff());
  #if P44SCRIPT_FULL_SUPPORT
  aStatement.bind(aIndex++, mAction.getSourceToStoreLocally().c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  #else
  aStatement.bind(aIndex++, mOldAction.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  #endif
}


#endif // ENABLE_EVALUATORS
