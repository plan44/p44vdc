//
//  Copyright (c) 2016-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
  evaluatorID(aEvaluatorID),
  evaluatorType(evaluator_unknown),
  currentState(undefined),
  currentOn(undefined),
  currentOff(undefined),
  conditionMetSince(Never),
  onConditionMet(false),
  reporting(false)
{
  // Config is:
  //  <behaviour mode>
  int st, su;
  if (aEvaluatorConfig=="rocker")
    evaluatorType = evaluator_rocker;
  else if (aEvaluatorConfig=="input")
    evaluatorType = evaluator_input;
  else if (aEvaluatorConfig=="internal" || aEvaluatorConfig=="internalinput") // "internal" must be still recognized for backwards compatibility with existing settings!
    evaluatorType = evaluator_internalinput;
  #if EXPRESSION_SCRIPT_SUPPORT
  else if (aEvaluatorConfig=="internalaction")
    evaluatorType = evaluator_internalaction;
  #endif
  else if (sscanf(aEvaluatorConfig.c_str(), "sensor:%d:%d", &st, &su)==2) {
    evaluatorType = evaluator_sensor;
    sensorType = (VdcSensorType)st;
    sensorUsage = (VdcUsageHint)su;
  }
  else if (sscanf(aEvaluatorConfig.c_str(), "internalsensor:%d:%d", &st, &su)==2) {
    evaluatorType = evaluator_internalsensor;
    sensorType = (VdcSensorType)st;
    sensorUsage = (VdcUsageHint)su;
  }
  else {
    LOG(LOG_ERR, "unknown evaluator type: %s", aEvaluatorConfig.c_str());
  }
  // install our specific settings
  installSettings(DeviceSettingsPtr(new EvaluatorDeviceSettings(*this)));
  // create "inputs" that will deliver the evaluator's result
  if (evaluatorType==evaluator_rocker) {
    // Simulate Two-way Rocker Button device
    // - defaults to black (generic button)
    colorClass = class_black_joker;
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
  else if (evaluatorType==evaluator_input || evaluatorType==evaluator_internalinput) {
    // Standard device settings without scene table (internal differs only from not getting announced with vdsm)
    colorClass = class_black_joker;
    // - create one binary input
    BinaryInputBehaviourPtr b = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*this,"evalresult"));
    b->setHardwareInputConfig(binInpType_none, usage_undefined, true, Never, Never);
    b->setHardwareName("evaluation result");
    addBehaviour(b);
  }
  else if (evaluatorType==evaluator_sensor  || evaluatorType==evaluator_internalsensor) {
    // Standard device settings without scene table (internal differs only from not getting announced with vdsm)
    colorClass = class_black_joker;
    // - create one sensor
    SensorBehaviourPtr s = SensorBehaviourPtr(new SensorBehaviour(*this,"evalresult"));
    s->setHardwareSensorConfig(sensorType, sensorUsage, 0, 0, 0, 100*MilliSecond, 0);
    s->setHardwareName("evaluation result");
    addBehaviour(s);
  }
  deriveDsUid();
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
  // public if it's not an internal-only evaluator
  return evaluatorType!=evaluator_internalinput && evaluatorType!=evaluator_internalsensor && evaluatorType!=evaluator_internalaction;
}


EvaluatorVdc &EvaluatorDevice::getEvaluatorVdc()
{
  return *(static_cast<EvaluatorVdc *>(vdcP));
}


void EvaluatorDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  // clear learn-in data from DB
  if (evaluatorDeviceRowID) {
    if(getEvaluatorVdc().db.executef("DELETE FROM evaluators WHERE rowid=%lld", evaluatorDeviceRowID)!=SQLITE_OK) {
      OLOG(LOG_ERR, "Error deleting evaluator: %s", getEvaluatorVdc().db.error()->description().c_str());
    }
  }
  // disconnection is immediate, so we can call inherited right now
  inherited::disconnect(aForgetParams, aDisconnectResultHandler);
}





string EvaluatorDevice::modelName()
{
  switch (evaluatorType) {
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
    if (valueMapper.getMappedSourcesInfo(varDefs)) {
      checkResult->add("varDefs", varDefs);
    }
    // Conditions
    ApiValuePtr cond;
    ExpressionValue res;
    // - on condition (or calculation for sensors)
    cond = checkResult->newObject();
    res = evaluatorSettings()->onCondition.evaluateSynchronously(evalmode_initial);
    cond->add("expression", checkResult->newString(evaluatorSettings()->onCondition.getCode()));
    if (res.isOK()) {
      cond->add("result", cond->newExpressionValue(res));
      cond->add("text", cond->newString(res.stringValue()));
      LOG(LOG_INFO, "- onCondition '%s' -> %s", evaluatorSettings()->onCondition.getCode(), res.stringValue().c_str());
    }
    else {
      cond->add("error", cond->newString(res.error()->getErrorMessage()));
      cond->add("at", cond->newUint64(evaluatorSettings()->onCondition.getPos()));
    }
    checkResult->add("onCondition", cond);
    if (evaluatorType!=evaluator_sensor || evaluatorType!=evaluator_internalsensor) {
      // - off condition
      cond = checkResult->newObject();
      res = evaluatorSettings()->offCondition.evaluateSynchronously(evalmode_initial);
      cond->add("expression", checkResult->newString(evaluatorSettings()->offCondition.getCode()));
      if (res.isOK()) {
        cond->add("result", cond->newExpressionValue(res));
        cond->add("text", cond->newString(res.stringValue()));
        LOG(LOG_INFO, "- offCondition '%s' -> %s", evaluatorSettings()->offCondition.getCode(), res.stringValue().c_str());
      }
      else {
        cond->add("error", cond->newString(res.error()->getErrorMessage()));
        cond->add("at", cond->newUint64(evaluatorSettings()->offCondition.getPos()));
      }
      checkResult->add("offCondition", cond);
    }
    // return the result
    aRequest->sendResult(checkResult);
    return ErrorPtr();
  }
  #if EXPRESSION_SCRIPT_SUPPORT
  else if (aMethod=="x-p44-testEvaluatorAction") {
    ApiValuePtr vp = aParams->get("result");
    Tristate state = currentState;
    if (vp) {
      state = vp->boolValue() ? yes : no;
    }
    // now test
    executeAction(state, boost::bind(&EvaluatorDevice::testActionExecuted, this, aRequest, _1));
    return ErrorPtr();
  }
  else if (aMethod=="x-p44-stopEvaluatorAction") {
    evaluatorSettings()->action.abort(false);
    return Error::ok();
  }
  #endif
  else {
    return inherited::handleMethod(aRequest, aMethod, aParams);
  }
}


#if EXPRESSION_SCRIPT_SUPPORT

void EvaluatorDevice::testActionExecuted(VdcApiRequestPtr aRequest, ExpressionValue aEvaluationResult)
{
  ApiValuePtr testResult = aRequest->newApiValue();
  testResult->setType(apivalue_object);
  if (aEvaluationResult.isOK()) {
    testResult->add("result", testResult->newExpressionValue(aEvaluationResult));
  }
  else {
    testResult->add("error", testResult->newString(aEvaluationResult.error()->getErrorMessage()));
    testResult->add("at", testResult->newUint64(evaluatorSettings()->action.getPos()));
  }
  aRequest->sendResult(testResult);
}

#endif // EXPRESSION_SCRIPT_SUPPORT


#define REPARSE_DELAY (30*Second)

void EvaluatorDevice::handleGlobalEvent(VdchostEvent aEvent)
{
  if (aEvent==vdchost_devices_initialized) {
    parseVarDefs();
  }
  else if (aEvent==vdchost_network_reconnected || aEvent==vdchost_timeofday_changed) {
    // network coming up might change local time
    if (!valueParseTicket) {
      // Note: if variable re-parsing is already scheduled, this will re-evaluate anyway
      //   Otherwise: have condition re-evaluated (because it possibly contain references to local time)
      valueParseTicket.executeOnce(boost::bind(&EvaluatorDevice::evaluateConditions, this, currentState, evalmode_timed), REPARSE_DELAY);
    }
  }
  inherited::handleGlobalEvent(aEvent);
}


void EvaluatorDevice::parseVarDefs()
{
  valueParseTicket.cancel();
  string newValueDefs; // re-created value defs using sensor ids rather than indices, for migration
  bool foundall = valueMapper.parseMappingDefs(
    evaluatorSettings()->varDefs,
    boost::bind(&EvaluatorDevice::dependentValueNotification, this, _1, _2),
    &newValueDefs
  );
  if (!newValueDefs.empty()) {
    // migrate old definitions (when re-created definitions are not equal to stored ones)
    // Note: even migrate partially, when not all defs could be resolved yet
    OLOG(LOG_NOTICE, "Migrating definitions to new id (rather than index) based form");
    evaluatorSettings()->setPVar(evaluatorSettings()->varDefs, newValueDefs);
  }
  if (!foundall) {
    // schedule a re-parse later
    valueParseTicket.executeOnce(boost::bind(&EvaluatorDevice::parseVarDefs, this), REPARSE_DELAY);
  }
  else {
    // run an initial evaluation to calculate default values and possibly schedule timed re-evaluations
    evaluateConditions(currentState, evalmode_initial);
  }
}


void EvaluatorDevice::dependentValueNotification(ValueSource &aValueSource, ValueListenerEvent aEvent)
{
  if (aEvent==valueevent_removed) {
    // a value has been removed, update my map
    parseVarDefs();
  }
  else {
    OLOG(LOG_INFO, "value source '%s' reports value %f", aValueSource.getSourceName().c_str(), aValueSource.getSourceValue());
    if (reporting) {
      OLOG(LOG_WARNING, "value source '%s' is part of cyclic reference -> not evaluating any further", aValueSource.getSourceName().c_str());
    }
    else {
      evaluateConditions(currentState, evalmode_externaltrigger);
    }
  }
}


void EvaluatorDevice::changedConditions()
{
  conditionMetSince = Never;
  onConditionMet = false;
  evaluateConditions(undefined, evalmode_initial);
}


Tristate EvaluatorDevice::evaluateBooleanNow(EvaluationContext &aEvalCtx, EvalMode aEvalMode, bool aScheduleReEval)
{
  FOCUSOLOG("----- Starting expression evaluation: '%s'", aEvalCtx.getCode());
  ExpressionValue res = aEvalCtx.evaluateSynchronously(aEvalMode, aScheduleReEval);
  if (res.isValue()) {
    // evaluation successful
    FOCUSOLOG("===== expression result: '%s' = %s = %s", aEvalCtx.getCode(), res.stringValue().c_str(), res.boolValue() ? "true" : "false");
    return res.boolValue() ? yes : no;
  }
  else {
    OLOG(LOG_INFO,"Expression '%s' evaluation undefined: %s", aEvalCtx.getCode(), res.stringValue().c_str());
    return undefined;
  }
}



ErrorPtr EvaluatorDevice::handleReEvaluationResult(bool aIsOffCondition, ExpressionValue aEvaluationResult, EvaluationContext &aContext)
{
  if (evaluatorType==evaluator_sensor || evaluatorType==evaluator_internalsensor) {
    // sensor evaluator was re-evaluated
    SensorBehaviourPtr s = getSensor(0);
    if (s) {
      // protect against state updates triggering evaluation again via cyclic references
      reporting = true;
      if (aEvaluationResult.isValue()) {
        FOCUSOLOG("===== sensor expression result: '%s' = '%s' = %f", evaluatorSettings()->onCondition.getCode(), aEvaluationResult.stringValue().c_str(), aEvaluationResult.numValue());
        s->updateSensorValue(aEvaluationResult.numValue());
      }
      else {
        OLOG(LOG_INFO,"Sensor expression '%s' evaluation status: %s", evaluatorSettings()->onCondition.getCode(), aEvaluationResult.stringValue().c_str());
        s->invalidateSensorValue();
      }
      // done reporting, critical phase is over
      reporting = false;
    }
  }
  else {
    // binary evaluator expression was re-evaluated
    Tristate b;
    if (aEvaluationResult.isValue()) {
      // evaluation successful
      FOCUSOLOG("===== timed re-evaluation: '%s' = %s = %s", aContext.getCode(), aEvaluationResult.stringValue().c_str(), aEvaluationResult.boolValue() ? "true" : "false");
      b = aEvaluationResult.boolValue() ? yes : no;
    }
    else {
      OLOG(LOG_INFO,"Expression '%s' re-evaluation status: %s", aContext.getCode(), aEvaluationResult.stringValue().c_str());
      b = undefined;
    }
    if (aIsOffCondition) currentOff = b;
    else currentOn = b;
    calculateEvaluatorState(currentState, evalmode_timed);
  }
  return ErrorPtr();
}



void EvaluatorDevice::evaluateConditions(Tristate aRefState, EvalMode aEvalMode)
{
  if (evaluatorType==evaluator_sensor || evaluatorType==evaluator_internalsensor) {
    // trigger updating the sensor value
    evaluatorSettings()->onCondition.triggerEvaluation(aEvalMode);
    // callback will handle everything
  }
  else {
    // evaluate binary state and report it
    if (aEvalMode==evalmode_initial) {
      OLOG(LOG_INFO, "Initial evaluation (after startup or expression changes) -> delays inactive");
    }
    // always evaluate both conditions because they could contain timed subexpressions that need to be scheduled
    currentOn = evaluateBooleanNow(evaluatorSettings()->onCondition, aEvalMode, true);
    currentOff = evaluateBooleanNow(evaluatorSettings()->offCondition, aEvalMode, true);
    calculateEvaluatorState(aRefState, aEvalMode);
  }
}


void EvaluatorDevice::calculateEvaluatorState(Tristate aRefState, EvalMode aEvalMode)
{
  // now derive decision
  Tristate prevState = currentState;
  bool decisionMade = false;
  MLMicroSeconds now = MainLoop::currentMainLoop().now();
  evaluateTicket.cancel();
  if (!decisionMade && aRefState!=yes) {
    // off or unknown: check for switching on
    OLOG(LOG_INFO, "onCondition '%s' evaluates to %s", evaluatorSettings()->onCondition.getCode(), currentOn==undefined ? "<undefined>" : (currentOn==yes ? "true -> switching ON" : "false"));
    if (currentOn!=yes) {
      // not met now -> reset if we are currently timing this condition
      if (onConditionMet) conditionMetSince = Never;
    }
    else {
      if (!onConditionMet || conditionMetSince==Never) {
        // we see this condition newly met now
        onConditionMet = true; // seen ON condition met
        conditionMetSince = now;
      }
      // check timing
      MLMicroSeconds metAt = conditionMetSince+evaluatorSettings()->minOnTime;
      if (now>=metAt || aEvalMode==evalmode_initial) {
        // condition met long enough or initial evaluation that always applies immediately
        currentState = yes;
        decisionMade = true;
      }
      else {
        // condition not met long enough yet, need to re-check later
        OLOG(LOG_INFO, "- ON condition not yet met long enough -> must remain stable another %.2f seconds", (double)(metAt-now)/Second);
        evaluatorSettings()->onCondition.scheduleLatestEvaluation(metAt);
        return;
      }
    }
  }
  if (!decisionMade && aRefState!=no) {
    // on or unknown: check for switching off
    OLOG(LOG_INFO, "offCondition '%s' evaluates to %s", evaluatorSettings()->offCondition.getCode(), currentOff==undefined ? "<undefined>" : (currentOff==yes ? "true -> switching OFF" : "false"));
    if (currentOff!=yes) {
      // not met now -> reset if we are currently timing this condition
      if (!onConditionMet) conditionMetSince = Never;
    }
    else {
      if (onConditionMet || conditionMetSince==Never) {
        // we see this condition newly met now
        onConditionMet = false; // seen OFF condition met
        conditionMetSince = now;
      }
      // check timing
      MLMicroSeconds metAt = conditionMetSince+evaluatorSettings()->minOffTime;
      if (now>=metAt || aEvalMode==evalmode_initial) {
        // condition met long enough or initial evaluation that always applies immediately
        currentState = no;
        decisionMade = true;
      }
      else {
        // condition not met long enough yet, need to re-check later
        OLOG(LOG_INFO, "- OFF condition not yet met long enough -> must remain stable another %.2f seconds", (double)(metAt-now)/Second);
        evaluatorSettings()->offCondition.scheduleLatestEvaluation(metAt);
        return;
      }
    }
  }
  if (decisionMade && currentState!=undefined) {
    // protect against state updates triggering evaluation again via cyclic references
    reporting = true;
    // report it
    switch (evaluatorType) {
      case evaluator_input :
      case evaluator_internalinput :
      {
        BinaryInputBehaviourPtr b = getInput(0);
        if (b) {
          b->updateInputState(currentState==yes);
        }
        break;
      }
      case evaluator_rocker : {
        if (currentState!=prevState) {
          // virtually click up or down button
          ButtonBehaviourPtr b = getButton(currentState==no ? 0 : 1);
          if (b) {
            b->sendClick(ct_tip_1x);
          }
        }
        break;
      }
      #if EXPRESSION_SCRIPT_SUPPORT
      case evaluator_internalaction: {
        // execute action
        executeAction(currentState, boost::bind(&EvaluatorDevice::actionExecuted, this, _1));
        break;
      }
      #endif
      default: break;
    }
    // done reporting, critical phase is over
    reporting = false;
  }
}

#if EXPRESSION_SCRIPT_SUPPORT

void EvaluatorDevice::actionExecuted(ExpressionValue aEvaluationResult)
{
  OLOG(LOG_INFO, "evaluator action script completed with result: '%s', error: %s", aEvaluationResult.stringValue().c_str(), Error::text(aEvaluationResult.error()));
}

#endif // EXPRESSION_SCRIPT_SUPPORT


// MARK: - EvaluatorExpressionContext


EvaluatorExpressionContext::EvaluatorExpressionContext(EvaluatorDevice &aEvaluator, const GeoLocation* aGeoLocationP) :
  inherited(aGeoLocationP),
  evaluator(aEvaluator)
{
}


bool EvaluatorExpressionContext::valueLookup(const string &aName, ExpressionValue &aResult)
{
  if (strucmp(aName.c_str(),"oncondition")==0) {
    aResult.setBool(evaluator.currentOn);
    return true;
  }
  if (evaluator.valueMapper.valueLookup(aResult, aName)) return true;
  return inherited::valueLookup(aName, aResult);
}


#if EXPRESSION_SCRIPT_SUPPORT

// MARK: - EvaluatorActionContext


EvaluatorActionContext::EvaluatorActionContext(EvaluatorDevice &aEvaluator, const GeoLocation* aGeoLocationP) :
  inherited(aGeoLocationP),
  evaluator(aEvaluator),
  execState(undefined)
{
}

bool EvaluatorActionContext::valueLookup(const string &aName, ExpressionValue &aResult)
{
  if (strucmp(aName.c_str(),"result")==0) {
    aResult.setBool(execState==yes);
    return true;
  }
  if (evaluator.valueMapper.valueLookup(aResult, aName)) return true;
  return inherited::valueLookup(aName, aResult);
}


bool EvaluatorActionContext::evaluateAsyncFunction(const string &aFunc, const FunctionArguments &aArgs, bool &aNotYielded)
{
  if (HttpComm::evaluateAsyncHttpFunctions(this, aFunc, aArgs, aNotYielded, &httpAction)) return true;
  return inherited::evaluateAsyncFunction(aFunc, aArgs, aNotYielded);
}


bool EvaluatorActionContext::abort(bool aDoCallBack)
{
  if (httpAction) {
    httpAction->cancelRequest();
  }
  return inherited::abort(aDoCallBack);
}



// MARK: - actions

#define LEGACY_ACTIONS_REWRITING 1

ErrorPtr EvaluatorDevice::executeAction(Tristate aState, EvaluationResultCB aResultCB)
{
  ErrorPtr err;
  if (aState==undefined) return err; // NOP
  #if LEGACY_ACTIONS_REWRITING
  const char *p = evaluatorSettings()->action.getCode();
  while (*p && (*p==' ' || *p=='\t')) p++;
  if (strucmp(p, "getURL:", 7)==0 || strucmp(p, "postURL:", 8)==0 || strucmp(p, "putURL:", 7)==0) {
    // old syntax, convert
    string actionScript;
    // basic action syntax:
    //   <action>
    // or:
    //   <onaction>|<offaction>
    string singleaction;
    bool isOn = true; // on comes first
    while (nextPart(p, singleaction, '|')) {
      if (*p=='|') {
        if (singleaction.empty()) {
          // off action only
          actionScript = "if (result==false) ";
          isOn = false;
          p++;
        }
        else {
          // no condition for action
          isOn = false;
        }
      }
      else if (isOn) {
        // on action is first action
        actionScript = "if (result==true) ";
      }
      else {
        actionScript += " else ";
      }
      // single action syntax:
      //   <command>:<commandparams>
      // available commands:
      // - getURL:<url>
      // - postURL:<url>;<raw postdata>
      // - putURL:<url>;<raw putdata>
      string cmd;
      string cmdparams;
      string url;
      string data;
      if (keyAndValue(singleaction, cmd, cmdparams, ':')) {
        actionScript += cmd + '(';
        if (cmd=="getURL") {
          url = cmdparams;
        }
        else {
          keyAndValue(cmdparams, url, data, ';');
        }
        url = shellQuote(url);
        // replace @{expr} by '"+ string(expr) + "'
        size_t i = 0;
        while ((i = url.find("@{",i))!=string::npos) {
          size_t e = url.find("}",i+2);
          string expr = "\"+string(" + url.substr(i+2,e==string::npos ? e : e-2-i) +")+\"";
          url.replace(i, e-i+1, expr);
        }
        actionScript += url;
        if (!data.empty()) {
          data = shellQuote(data);
          // replace @{expr} by '"+ string(expr) + "'
          size_t i = 0;
          while ((i = data.find("@{",i))!=string::npos) {
            size_t e = data.find("}",i+2);
            string expr = "\"+string(" + data.substr(i+2,e==string::npos ? e : e-2-i) +")+\"";
            data.replace(i, e-i+1, expr);
          }
          actionScript += ", " + data;
        }
        actionScript += ");";
      }
      isOn = false;
    } // while legacy actions
    LOG(LOG_WARNING, "rewritten legacy action: '%s' -> '%s'", evaluatorSettings()->action.getCode(), actionScript.c_str());
    evaluatorSettings()->action.setCode(actionScript);
    evaluatorSettings()->markDirty();
  }
  #endif // LEGACY_ACTIONS_REWRITING
  evaluatorSettings()->action.execState = aState;
  evaluatorSettings()->action.execute(true, aResultCB);
  return err;
}

#endif // EXPRESSION_SCRIPT_SUPPORT



void EvaluatorDevice::deriveDsUid()
{
  // vDC implementation specific UUID:
  //   UUIDv5 with name = classcontainerinstanceid::evaluatorID
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  string s = vdcP->vdcInstanceIdentifier();
  s += "::" + evaluatorID;
  dSUID.setNameInSpace(s, vdcNamespace);
}


string EvaluatorDevice::description()
{
  string s = inherited::description();
  if (evaluatorType==evaluator_rocker)
    string_format_append(s, "\n- evaluation controls simulated 2-way-rocker button");
  if (evaluatorType==evaluator_input)
    string_format_append(s, "\n- evaluation controls binary input");
  return s;
}


string EvaluatorDevice::getEvaluatorType()
{
  switch (evaluatorType) {
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
  #if EXPRESSION_SCRIPT_SUPPORT
  action_key,
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
    #if EXPRESSION_SCRIPT_SUPPORT
    { "x-p44-action", apivalue_string, action_key, OKEY(evaluatorDevice_key) },
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
        case varDefs_key: aPropValue->setStringValue(evaluatorSettings()->varDefs); return true;
        case onCondition_key: aPropValue->setStringValue(evaluatorSettings()->onCondition.getCode()); return true;
        case offCondition_key: aPropValue->setStringValue(evaluatorSettings()->offCondition.getCode()); return true;
        case minOnTime_key: aPropValue->setDoubleValue((double)(evaluatorSettings()->minOnTime)/Second); return true;
        case minOffTime_key: aPropValue->setDoubleValue((double)(evaluatorSettings()->minOffTime)/Second); return true;
        #if EXPRESSION_SCRIPT_SUPPORT
        case action_key: aPropValue->setStringValue(evaluatorSettings()->action.getCode()); return true;
        #endif
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        case varDefs_key:
          if (evaluatorSettings()->setPVar(evaluatorSettings()->varDefs, aPropValue->stringValue()))
            parseVarDefs(); // changed varDefs, re-parse them
          return true;
        case onCondition_key:
          if (evaluatorSettings()->onCondition.setCode(aPropValue->stringValue())) {
            evaluatorSettings()->markDirty();
            changedConditions();  // changed conditions, re-evaluate output
          }
          return true;
        case offCondition_key:
          if (evaluatorSettings()->offCondition.setCode(aPropValue->stringValue())) {
            evaluatorSettings()->markDirty();
            changedConditions();  // changed conditions, re-evaluate output
          }
          return true;
        case minOnTime_key:
          if (evaluatorSettings()->setPVar(evaluatorSettings()->minOnTime, (MLMicroSeconds)(aPropValue->doubleValue()*Second)))
            changedConditions();  // changed conditions, re-evaluate output
          return true;
        case minOffTime_key:
          if (evaluatorSettings()->setPVar(evaluatorSettings()->minOffTime, (MLMicroSeconds)(aPropValue->doubleValue()*Second)))
            changedConditions();  // changed conditions, re-evaluate output
          return true;
        #if EXPRESSION_SCRIPT_SUPPORT
        case action_key:
          if (evaluatorSettings()->action.setCode(aPropValue->stringValue())) {
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


EvaluatorDeviceSettings::EvaluatorDeviceSettings(EvaluatorDevice &aEvaluator) :
  inherited(aEvaluator),
  onCondition(aEvaluator, &aEvaluator.getVdcHost().geolocation),
  offCondition(aEvaluator, &aEvaluator.getVdcHost().geolocation),
  #if EXPRESSION_SCRIPT_SUPPORT
  action(aEvaluator, &aEvaluator.getVdcHost().geolocation),
  #endif
  minOnTime(0), // trigger immediately
  minOffTime(0) // trigger immediately
{
  onCondition.isMemberVariable();
  onCondition.setContextInfo("onCondition", &device);
  offCondition.isMemberVariable();
  offCondition.setContextInfo("offCondition", &device);
  #if EXPRESSION_SCRIPT_SUPPORT
  action.isMemberVariable();
  action.setContextInfo("action", &device);
  #endif
  onCondition.setEvaluationResultHandler(boost::bind(&EvaluatorDevice::handleReEvaluationResult, &aEvaluator, false, _1, _2));
  offCondition.setEvaluationResultHandler(boost::bind(&EvaluatorDevice::handleReEvaluationResult, &aEvaluator, true, _1, _2));
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
    { "action", SQLITE_TEXT }, // note: this is a dummy if we don't have EXPRESSION_SCRIPT_SUPPORT
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
  varDefs.assign(nonNullCStr(aRow->get<const char *>(aIndex++)));
  onCondition.setCode(nonNullCStr(aRow->get<const char *>(aIndex++)));
  offCondition.setCode(nonNullCStr(aRow->get<const char *>(aIndex++)));
  aRow->getCastedIfNotNull<MLMicroSeconds, long long int>(aIndex++, minOnTime);
  aRow->getCastedIfNotNull<MLMicroSeconds, long long int>(aIndex++, minOffTime);
  #if EXPRESSION_SCRIPT_SUPPORT
  action.setCode(nonNullCStr(aRow->get<const char *>(aIndex++)));
  #else
  oldAction = nonNullCStr(aRow->get<const char *>(aIndex++));
  #endif
}


// bind values to passed statement
void EvaluatorDeviceSettings::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, varDefs.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, onCondition.getCode(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, offCondition.getCode(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, (long long int)minOnTime);
  aStatement.bind(aIndex++, (long long int)minOffTime);
  #if EXPRESSION_SCRIPT_SUPPORT
  aStatement.bind(aIndex++, action.getCode(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  #else
  aStatement.bind(aIndex++, oldAction.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  #endif
}



#endif // ENABLE_EVALUATORS
