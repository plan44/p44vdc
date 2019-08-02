//
//  Copyright (c) 1-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
  if (aEvaluatorConfig=="rocker")
    evaluatorType = evaluator_rocker;
  else if (aEvaluatorConfig=="input")
    evaluatorType = evaluator_input;
  else if (aEvaluatorConfig=="internal" || aEvaluatorConfig=="internalinput") // "internal" must be still recognized for backwards compatibility with existing settings!
    evaluatorType = evaluator_internalinput;
  else if (aEvaluatorConfig=="internalaction")
    evaluatorType = evaluator_internalaction;
  else if (sscanf(aEvaluatorConfig.c_str(), "sensor:%d:%d", &sensorType, &sensorUsage)==2)
    evaluatorType = evaluator_sensor;
  else if (sscanf(aEvaluatorConfig.c_str(), "internalsensor:%d:%d", &sensorType, &sensorUsage)==2)
    evaluatorType = evaluator_internalsensor;
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
      ALOG(LOG_ERR, "Error deleting evaluator: %s", getEvaluatorVdc().db.error()->description().c_str());
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
    ALOG(LOG_INFO, "CheckEvaluator:");
    ApiValuePtr varDefs = checkResult->newObject();
    if (valueMapper.getMappedSourcesInfo(varDefs)) {
      checkResult->add("varDefs", varDefs);
    }
    // Conditions
    ApiValuePtr cond;
    ExpressionValue res;
    // - on condition (or calculation for sensors)
    cond = checkResult->newObject();
    res = evaluatorSettings()->onCondition.evaluateNow();
    cond->add("expression", checkResult->newString(evaluatorSettings()->onCondition.getExpression()));
    if (res.isOk()) {
      cond->add("result", cond->newDouble(res.v));
      LOG(LOG_INFO, "- onCondition '%s' -> %f", evaluatorSettings()->onCondition.getExpression().c_str(), res.v);
    }
    else {
      cond->add("error", cond->newString(res.err->getErrorMessage()));
      if (!res.err->isError(ExpressionError::domain(), ExpressionError::Null)) cond->add("at", cond->newUint64(res.pos));
    }
    checkResult->add("onCondition", cond);
    if (evaluatorType!=evaluator_sensor || evaluatorType!=evaluator_internalsensor) {
      // - off condition
      cond = checkResult->newObject();
      res = evaluatorSettings()->offCondition.evaluateNow();
      cond->add("expression", checkResult->newString(evaluatorSettings()->offCondition.getExpression()));
      if (res.isOk()) {
        cond->add("result", cond->newDouble(res.v));
        LOG(LOG_INFO, "- offCondition '%s' -> %f", evaluatorSettings()->offCondition.getExpression().c_str(), res.v);
      }
      else {
        cond->add("error", cond->newString(res.err->getErrorMessage()));
        if (!res.err->isError(ExpressionError::domain(), ExpressionError::Null)) cond->add("at", cond->newUint64(res.pos));
      }
      checkResult->add("offCondition", cond);
    }
    // return the result
    aRequest->sendResult(checkResult);
    return ErrorPtr();
  }
  else if (aMethod=="x-p44-testEvaluatorAction") {
    ApiValuePtr vp = aParams->get("result");
    Tristate state = currentState;
    if (vp) {
      state = vp->boolValue() ? yes : no;
    }
    // now test
    return Error::ok(executeAction(state));
  }
  else {
    return inherited::handleMethod(aRequest, aMethod, aParams);
  }
}



#define REPARSE_DELAY (30*Second)

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
    ALOG(LOG_NOTICE, "Migrating definitions to new id (rather than index) based form");
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
    ALOG(LOG_INFO, "value source '%s' reports value %f", aValueSource.getSourceName().c_str(), aValueSource.getSourceValue());
    if (reporting) {
      ALOG(LOG_WARNING, "value source '%s' is part of cyclic reference -> not evaluating any further", aValueSource.getSourceName().c_str());
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
  AFOCUSLOG("----- Starting expression evaluation: '%s'", aEvalCtx.getExpression().c_str());
  ExpressionValue res = aEvalCtx.evaluateNow(aEvalMode, aScheduleReEval);
  if (res.isOk()) {
    // evaluation successful
    AFOCUSLOG("===== expression result: '%s' = %f = %s", aEvalCtx.getExpression().c_str(), res.v, res.v>0 ? "true" : "false");
    return res.v>0 ? yes : no;
  }
  else {
    ALOG(LOG_INFO,"Expression '%s' evaluation error: %s", aEvalCtx.getExpression().c_str(), res.err->text());
    return undefined;
  }
}



ErrorPtr EvaluatorDevice::handleReEvaluationResult(bool aIsOffCondition, ExpressionValue aEvaluationResult, EvaluationContext &aContext)
{
  if (evaluatorType==evaluator_sensor || evaluatorType==evaluator_internalsensor) {
    // sensor evaluator was re-evaluated
    // protect against state updates triggering evaluation again via cyclic references
    SensorBehaviourPtr s = getSensor(0);
    if (s) {
      reporting = true;
      if (aEvaluationResult.isOk()) {
        AFOCUSLOG("===== sensor expression result: '%s' = %f", evaluatorSettings()->onCondition.getExpression().c_str(), aEvaluationResult.v);
        s->updateSensorValue(aEvaluationResult.v);
      }
      else {
        ALOG(LOG_INFO,"Sensor expression '%s' evaluation error: %s", evaluatorSettings()->onCondition.getExpression().c_str(), aEvaluationResult.err->text());
        s->invalidateSensorValue();
      }
    }
    // done reporting, critical phase is over
    reporting = false;
  }
  else {
    // binary evaluator expression was re-evaluated
    Tristate b;
    if (aEvaluationResult.isOk()) {
      // evaluation successful
      AFOCUSLOG("===== timed re-evaluation: '%s' = %f = %s", aContext.getExpression().c_str(), aEvaluationResult.v, aEvaluationResult.v>0 ? "true" : "false");
      b = aEvaluationResult.v>0 ? yes : no;
    }
    else {
      ALOG(LOG_INFO,"Expression '%s' re-evaluation error: %s", aContext.getExpression().c_str(), aEvaluationResult.err->text());
      b = undefined;
    }
    if (aIsOffCondition) currentOff = b;
    else currentOn = b;
    calculateEvaluatorState(currentState, aContext.getEvalMode());
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
      ALOG(LOG_INFO, "Initial evaluation (after startup or expression changes) -> delays inactive");
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
    ALOG(LOG_INFO, "onCondition '%s' evaluates to %s", evaluatorSettings()->onCondition.getExpression().c_str(), currentOn==undefined ? "<undefined>" : (currentOn==yes ? "true -> switching ON" : "false"));
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
        ALOG(LOG_INFO, "- ON condition not yet met long enough -> must remain stable another %.2f seconds", (double)(metAt-now)/Second);
        evaluatorSettings()->onCondition.scheduleLatestEvaluation(metAt);
        return;
      }
    }
  }
  if (!decisionMade && aRefState!=no) {
    // on or unknown: check for switching off
    ALOG(LOG_INFO, "offCondition '%s' evaluates to %s", evaluatorSettings()->offCondition.getExpression().c_str(), currentOff==undefined ? "<undefined>" : (currentOff==yes ? "true -> switching OFF" : "false"));
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
        ALOG(LOG_INFO, "- OFF condition not yet met long enough -> must remain stable another %.2f seconds", (double)(metAt-now)/Second);
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
      case evaluator_internalaction: {
        // execute action
        executeAction(currentState);
        break;
      }
      default: break;
    }
    // done reporting, critical phase is over
    reporting = false;
  }
}



// MARK: - EvaluatorExpressionContext


EvaluatorExpressionContext::EvaluatorExpressionContext(EvaluatorDevice &aEvaluator, const GeoLocation& aGeoLocation) :
  inherited(aGeoLocation),
  evaluator(aEvaluator)
{
}



ExpressionValue EvaluatorExpressionContext::valueLookup(const string &aName)
{
  return evaluator.valueMapper.valueLookup(aName);
}


ExpressionValue EvaluatorDevice::actionValueLookup(Tristate aCurrentState, const string aName)
{
  if (aName=="result") {
    return ExpressionValue(aCurrentState==yes ? 1 : 0);
  }
  else {
    return valueMapper.valueLookup(aName);
  }
}



// MARK: - actions


ErrorPtr EvaluatorDevice::executeAction(Tristate aState)
{
  ErrorPtr err;
  if (aState==undefined) return err; // NOP
  // basic action syntax:
  //   <action>
  // or:
  //   <onaction>|<offaction>
  string action = evaluatorSettings()->action;
  size_t p = action.find('|');
  if (p!=string::npos) {
    // separate on and off actions
    if (aState==yes) {
      action = evaluatorSettings()->action.substr(0,p); // first part is onAction
    }
    else {
      action = evaluatorSettings()->action.substr(p+1); // second part is offAction
    }
  }
  if (action.empty()) return TextError::err("No action defined for evaluator %s condition", aState==yes ? "ON" : "OFF");
  // single action syntax:
  //   <command>:<commandparams>
  // available commands:
  // - getURL:<url>
  // - postURL:<url>;<raw postdata>
  // - putURL:<url>;<raw putdata>
  string cmd;
  string cmdparams;
  string method;
  string url;
  string data;
  string user;
  string password;
  if (keyAndValue(action, cmd, cmdparams, ':')) {
    if (cmd=="getURL") {
      method = "GET";
      url = cmdparams;
    }
    else if (cmd=="postURL" || cmd=="putURL") {
      if (keyAndValue(cmdparams, url, data, ';')) {
        method = cmd=="putURL" ? "PUT" : "POST";
        ErrorPtr serr = substituteExpressionPlaceholders(
          data,
          boost::bind(&EvaluatorDevice::actionValueLookup, this, aState, _1),
          NULL, // TODO: use proper context with timer functions
          "null"
        );
        if (Error::notOK(serr)) {
          serr->prefixMessage("placeholder substitution error in POST/PUT data: ");
          if (serr->isError(ExpressionError::domain(), ExpressionError::Syntax)) {
            return serr; // do not continue with syntax error
          }
          if (Error::isOK(err)) err = serr; // only report first error
        }
      }
    }
    else {
      err = TextError::err("Unknown action '%s'", cmd.c_str());
    }
    if (!method.empty()) {
      ErrorPtr serr = substituteExpressionPlaceholders(
        url,
        boost::bind(&EvaluatorDevice::actionValueLookup, this, aState, _1),
        NULL,
        "null"
      );
      if (Error::notOK(serr)) {
        serr->prefixMessage("placeholder substitution error in URL: ");
        if (serr->isError(ExpressionError::domain(), ExpressionError::Syntax)) {
          return serr; // do not continue with syntax error
        }
        if (Error::isOK(err)) err = serr; // only report first error
      }
      if (!httpAction) httpAction = HttpCommPtr(new HttpComm(MainLoop::currentMainLoop()));
      splitURL(url.c_str(), NULL, NULL, NULL, &user, &password);
      httpAction->setHttpAuthCredentials(user, password);
      ALOG(LOG_NOTICE, "issuing %s to %s %s", method.c_str(), url.c_str(), data.c_str());
      if (!httpAction->httpRequest(
        url.c_str(),
        boost::bind(&EvaluatorDevice::httpActionDone, this, _1, _2),
        method.c_str(),
        data.c_str()
      )) {
        err = TextError::err("could not issue http request");
      }
    }
  }
  return err;
}


void EvaluatorDevice::httpActionDone(const string &aResponse, ErrorPtr aError)
{
  ALOG(LOG_INFO, "http action returns '%s', error = %s", aResponse.c_str(), Error::text(aError));
}


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
  action_key,
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
    { "x-p44-action", apivalue_string, action_key, OKEY(evaluatorDevice_key) },
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
        case onCondition_key: aPropValue->setStringValue(evaluatorSettings()->onCondition.getExpression()); return true;
        case offCondition_key: aPropValue->setStringValue(evaluatorSettings()->offCondition.getExpression()); return true;
        case minOnTime_key: aPropValue->setDoubleValue((double)(evaluatorSettings()->minOnTime)/Second); return true;
        case minOffTime_key: aPropValue->setDoubleValue((double)(evaluatorSettings()->minOffTime)/Second); return true;
        case action_key: aPropValue->setStringValue(evaluatorSettings()->action); return true;
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
          if (evaluatorSettings()->onCondition.setExpression(aPropValue->stringValue())) {
            evaluatorSettings()->markDirty();
            changedConditions();  // changed conditions, re-evaluate output
          }
          return true;
        case offCondition_key:
          if (evaluatorSettings()->offCondition.setExpression(aPropValue->stringValue())) {
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
        case action_key:
          evaluatorSettings()->setPVar(evaluatorSettings()->action, aPropValue->stringValue());
          return true;
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}



// MARK: - settings


EvaluatorDeviceSettings::EvaluatorDeviceSettings(EvaluatorDevice &aEvaluator) :
  inherited(aEvaluator),
  onCondition(aEvaluator, aEvaluator.getVdcHost().geolocation),
  offCondition(aEvaluator, aEvaluator.getVdcHost().geolocation),
  minOnTime(0), // trigger immediately
  minOffTime(0) // trigger immediately
{
  onCondition.isMemberVariable();
  offCondition.isMemberVariable();
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
    { "action", SQLITE_TEXT },
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
  onCondition.setExpression(nonNullCStr(aRow->get<const char *>(aIndex++)));
  offCondition.setExpression(nonNullCStr(aRow->get<const char *>(aIndex++)));
  aRow->getCastedIfNotNull<MLMicroSeconds, long long int>(aIndex++, minOnTime);
  aRow->getCastedIfNotNull<MLMicroSeconds, long long int>(aIndex++, minOffTime);
  action.assign(nonNullCStr(aRow->get<const char *>(aIndex++)));
}


// bind values to passed statement
void EvaluatorDeviceSettings::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, varDefs.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, onCondition.getExpression().c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, offCondition.getExpression().c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, (long long int)minOnTime);
  aStatement.bind(aIndex++, (long long int)minOffTime);
  aStatement.bind(aIndex++, action.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
}



#endif // ENABLE_EVALUATORS
