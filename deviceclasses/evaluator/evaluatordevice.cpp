//
//  Copyright (c) 2016-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
  conditionMetSince(Never),
  onConditionMet(false),
  evaluating(false),
  evalMode(evalmode_normal)
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
  forgetValueDefs();
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
  parseValueDefs();
  // done
  inherited::initializeDevice(aCompletedCB, aFactoryReset);
}



ErrorPtr EvaluatorDevice::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  if (aMethod=="x-p44-checkEvaluator") {
    // Check the evaluator
    ApiValuePtr checkResult = aRequest->newApiValue();
    checkResult->setType(apivalue_object);
    // - value defs
    parseValueDefs(); // reparse
    ALOG(LOG_INFO, "CheckEvaluator:");
    ApiValuePtr valueDefs = checkResult->newObject();
    for (ValueSourcesMap::iterator pos = valueMap.begin(); pos!=valueMap.end(); ++pos) {
      ApiValuePtr val = valueDefs->newObject();
      MLMicroSeconds lastupdate = pos->second->getSourceLastUpdate();
      val->add("description", val->newString(pos->second->getSourceName()));
      if (lastupdate==Never) {
        val->add("age", val->newNull());
        val->add("value", val->newNull());
      }
      else {
        val->add("age", val->newDouble((double)(MainLoop::now()-lastupdate)/Second));
        val->add("value", val->newDouble(pos->second->getSourceValue()));
      }
      valueDefs->add(pos->first,val); // variable name
      LOG(LOG_INFO, "- '%s' ('%s') = %f", pos->first.c_str(), pos->second->getSourceName().c_str(), pos->second->getSourceValue());
    }
    checkResult->add("valueDefs", valueDefs);
    // Conditions
    ApiValuePtr cond;
    ExpressionValue res;
    // - on condition (or calculation for sensors)
    cond = checkResult->newObject();
    res = calcEvaluatorExpression(evaluatorSettings()->onCondition);
    if (res.isOk()) {
      cond->add("result", cond->newDouble(res.v));
      LOG(LOG_INFO, "- onCondition '%s' -> %f", evaluatorSettings()->onCondition.c_str(), res.v);
    }
    else {
      cond->add("error", cond->newString(res.err->getErrorMessage()));
    }
    checkResult->add("onCondition", cond);
    if (evaluatorType!=evaluator_sensor || evaluatorType!=evaluator_internalsensor) {
      // - off condition
      cond = checkResult->newObject();
      res = calcEvaluatorExpression(evaluatorSettings()->offCondition);
      if (res.isOk()) {
        cond->add("result", cond->newDouble(res.v));
        LOG(LOG_INFO, "- offCondition '%s' -> %f", evaluatorSettings()->offCondition.c_str(), res.v);
      }
      else {
        cond->add("error", cond->newString(res.err->getErrorMessage()));
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



void EvaluatorDevice::forgetValueDefs()
{
  for (ValueSourcesMap::iterator pos = valueMap.begin(); pos!=valueMap.end(); ++pos) {
    pos->second->removeSourceListener(this);
  }
  valueMap.clear();
}



#define REPARSE_DELAY (30*Second)

void EvaluatorDevice::parseValueDefs()
{
  valueParseTicket.cancel();
  forgetValueDefs(); // forget previous mappings
  string &valueDefs = evaluatorSettings()->valueDefs;
  string newValueDefs; // re-created value defs using sensor ids rather than indices, for migration
  // syntax:
  //  <valuealias>:<valuesourceid> [, <valuealias>:valuesourceid> ...]
  ALOG(LOG_INFO, "Parsing variable definitions");
  bool foundall = true;
  size_t i = 0;
  while(i<valueDefs.size()) {
    size_t e = valueDefs.find(":", i);
    if (e!=string::npos) {
      string valuealias = valueDefs.substr(i,e-i);
      i = e+1;
      size_t e2 = valueDefs.find_first_of(", \t\n\r", i);
      if (e2==string::npos) e2 = valueDefs.size();
      string valuesourceid = valueDefs.substr(i,e2-i);
      // search source
      ValueSource *vs = getVdcHost().getValueSourceById(valuesourceid);
      if (vs) {
        // value source exists
        // - add myself as listener
        vs->addSourceListener(boost::bind(&EvaluatorDevice::dependentValueNotification, this, _1, _2), this);
        // - add source to my map
        valueMap[valuealias] = vs;
        LOG(LOG_INFO, "- Variable '%s' connected to source '%s'", valuealias.c_str(), vs->getSourceName().c_str());
        string_format_append(newValueDefs, "%s:%s", valuealias.c_str(), vs->getSourceId().c_str());
      }
      else {
        ALOG(LOG_WARNING, "Value source id '%s' not found -> variable '%s' currently undefined", valuesourceid.c_str(), valuealias.c_str());
        string_format_append(newValueDefs, "%s:%s", valuealias.c_str(), valuesourceid.c_str());
        foundall = false;
      }
      // skip delimiters
      i = valueDefs.find_first_not_of(", \t\n\r", e2);
      if (i==string::npos) i = valueDefs.size();
      newValueDefs += valueDefs.substr(e2,i-e2);
    }
    else {
      ALOG(LOG_ERR, "missing ':' in value definition");
      break;
    }
  }
  // migrate old definitions (when re-created definitions are not equal to stored ones)
  // Note: even migrate partially, when not all defs could be resolved yet
  if (evaluatorSettings()->valueDefs!=newValueDefs) {
    ALOG(LOG_NOTICE, "Migrating definitions to new id (rather than index) based form");
    evaluatorSettings()->setPVar(evaluatorSettings()->valueDefs, newValueDefs);
  }
  if (!foundall) {
    // schedule a re-parse later
    valueParseTicket.executeOnce(boost::bind(&EvaluatorDevice::parseValueDefs, this), REPARSE_DELAY);
  }
  else {
    // run an initial evaluation to calculate default values and possibly start timers
    evaluateConditions(currentState, evalmode_initial);
  }
}


void EvaluatorDevice::dependentValueNotification(ValueSource &aValueSource, ValueListenerEvent aEvent)
{
  if (aEvent==valueevent_removed) {
    // a value has been removed, update my map
    parseValueDefs();
  }
  else {
    ALOG(LOG_INFO, "value source '%s' reports value %f", aValueSource.getSourceName().c_str(), aValueSource.getSourceValue());
    if (evaluating) {
      ALOG(LOG_WARNING, "value source '%s' is part of cyclic reference -> not evaluating any further", aValueSource.getSourceName().c_str());
    }
    else {
      evaluateConditions(currentState, evalmode_normal);
    }
  }
}


void EvaluatorDevice::changedConditions()
{
  conditionMetSince = Never;
  onConditionMet = false;
  evaluateConditions(undefined, evalmode_initial);
}


#define MIN_RETRIGGER_SECONDS 10

ExpressionValue EvaluatorDevice::evaluateFunction(const string &aName, const FunctionArgumentVector &aArgs)
{
  if (aName=="testlater" && aArgs.size()>=2 && aArgs.size()<=3) {
    // testlater(seconds, timedtest [, retrigger])   return "invalid" now, re-evaluate after given seconds and return value of test then. If repeat is true then, the timer will be re-scheduled
    bool retrigger = false;
    if (aArgs.size()>=3) retrigger = aArgs[2].isOk() && aArgs[2].v>0;
    if (evalMode!=evalmode_timed || retrigger) {
      // (re-)setup timer
      double secs = aArgs[0].v;
      if (retrigger && secs<MIN_RETRIGGER_SECONDS) {
        // prevent too frequent re-triggering that could eat up too much cpu
        ALOG(LOG_WARNING, "testlater() requests too fast retriggering (%.1f seconds), allowed minimum is %.1f seconds", secs, (double)MIN_RETRIGGER_SECONDS);
        secs = MIN_RETRIGGER_SECONDS;
      }
      ALOG(LOG_INFO, "testlater() function schedules re-evaluation in %.1f seconds", secs);
      testlaterTicket.executeOnce(boost::bind(&EvaluatorDevice::evaluateConditionsLater, this), secs*Second);
    }
    if (evalMode==evalmode_timed) {
      // evaluation runs because timer has expired, return test result
      return ExpressionValue(aArgs[1].v);
    }
    else {
      // timer not yet expired, return undefined
      return ExpressionError::errValue(ExpressionError::Null, "testlater() not yet ready");
    }
  }
  else if (aName=="initial" && aArgs.size()==0) {
    // initial()  returns true if this is a "initial" run of the evaluator, meaning after startup or expression changes
    return ExpressionValue(evalMode==evalmode_initial);
  }
  // no such function
  return ExpressionError::errValue(ExpressionError::NotFound, "not found"); // just signals caller to try builtin functions
}


void EvaluatorDevice::evaluateConditionsLater()
{
  // important: passed reference state must be current state of NOW (not of when the timer was triggered!)
  evaluateConditions(currentState, evalmode_timed);
}



void EvaluatorDevice::evaluateConditions(Tristate aRefState, EvalMode aEvalMode)
{
  evalMode = aEvalMode;
  if (evalMode==evalmode_timed) {
    ALOG(LOG_INFO, "testlater() timer expired - now re-evaluating");
  }
  if (evaluatorType==evaluator_sensor || evaluatorType==evaluator_internalsensor) {
    // just update the sensor value
    ExpressionValue res = calcEvaluatorExpression(evaluatorSettings()->onCondition);
    // protect against state updates triggering evaluation again via cyclic references
    SensorBehaviourPtr s = getSensor(0);
    if (s) {
      evaluating = true;
      if (res.isOk()) {
        AFOCUSLOG("===== sensor expression result: '%s' = %f", evaluatorSettings()->onCondition.c_str(), res.v);
        s->updateSensorValue(res.v);
      }
      else {
        ALOG(LOG_INFO,"Sensor expression '%s' evaluation error: %s", evaluatorSettings()->onCondition.c_str(), res.err->description().c_str());
        s->invalidateSensorValue();
      }
    }
    // done reporting, critical phase is over
    evaluating = false;
  }
  else {
    // evaluate binary state and report it
    if (evalMode==evalmode_initial) {
      ALOG(LOG_INFO, "Initial evaluation (after startup or expression changes) -> delays inactive");
    }
    Tristate prevState = currentState;
    bool decisionMade = false;
    MLMicroSeconds now = MainLoop::currentMainLoop().now();
    evaluateTicket.cancel();
    // always evaluate both conditions because they could contain testlater() calls that need to be triggered
    Tristate on = evaluateBoolean(evaluatorSettings()->onCondition);
    Tristate off = evaluateBoolean(evaluatorSettings()->offCondition);
    // now derive decision
    if (!decisionMade && aRefState!=yes) {
      // off or unknown: check for switching on
      ALOG(LOG_INFO, "onCondition '%s' evaluates to %s", evaluatorSettings()->onCondition.c_str(), on==undefined ? "<undefined>" : (on==yes ? "true -> switching ON" : "false"));
      if (on!=yes) {
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
        if (now>=metAt || evalMode==evalmode_initial) {
          // condition met long enough or initial evaluation that always applies immediately
          currentState = yes;
          decisionMade = true;
        }
        else {
          // condition not met long enough yet, need to re-check later
          ALOG(LOG_INFO, "- ON condition not yet met long enough -> must remain stable another %.2f seconds", (double)(metAt-now)/Second);
          evaluateTicket.executeOnceAt(boost::bind(&EvaluatorDevice::evaluateConditions, this, aRefState, aEvalMode), metAt);
          return;
        }
      }
    }
    if (!decisionMade && aRefState!=no) {
      // on or unknown: check for switching off
      ALOG(LOG_INFO, "offCondition '%s' evaluates to %s", evaluatorSettings()->offCondition.c_str(), off==undefined ? "<undefined>" : (off==yes ? "true -> switching OFF" : "false"));
      if (off!=yes) {
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
        if (now>=metAt || evalMode==evalmode_initial) {
          // condition met long enough or initial evaluation that always applies immediately
          currentState = no;
          decisionMade = true;
        }
        else {
          // condition not met long enough yet, need to re-check later
          ALOG(LOG_INFO, "- OFF condition not yet met long enough -> must remain stable another %.2f seconds", (double)(metAt-now)/Second);
          evaluateTicket.executeOnceAt(boost::bind(&EvaluatorDevice::evaluateConditions, this, aRefState, aEvalMode), metAt);
          return;
        }
      }
    }
    if (decisionMade && currentState!=undefined) {
      // protect against state updates triggering evaluation again via cyclic references
      evaluating = true;
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
      evaluating = false;
    }
  }
}


Tristate EvaluatorDevice::evaluateBoolean(string aExpression)
{
  AFOCUSLOG("----- Starting expression evaluation: '%s'", aExpression.c_str());
  ExpressionValue res = calcEvaluatorExpression(aExpression);
  if (res.isOk()) {
    // evaluation successful
    AFOCUSLOG("===== expression result: '%s' = %f = %s", aExpression.c_str(), res.v, res.v>0 ? "true" : "false");
    return res.v>0 ? yes : no;
  }
  else {
    ALOG(LOG_INFO,"Expression '%s' evaluation error: %s", aExpression.c_str(), res.err->description().c_str());
    return undefined;
  }
}


ExpressionValue EvaluatorDevice::calcEvaluatorExpression(string &aExpression)
{
  return evaluateExpression(
    aExpression,
    boost::bind(&EvaluatorDevice::valueLookup, this, _1),
    boost::bind(&EvaluatorDevice::evaluateFunction, this, _1, _2)
  );
}


ExpressionValue EvaluatorDevice::valueLookup(const string aName)
{
  // values can be simple sensor names, or sensor names with sub-field specifications:
  // sensor               returns the value of the sensor
  // sensor.valid         returns 1 if sensor has a valid value, 0 otherwise
  // sensor.oplevel       returns the operation level of the sensor (0..100%)
  // sensor.age           returns the age of the sensor value in seconds
  string subfield;
  string name;
  size_t i = aName.find('.');
  if (i!=string::npos) {
    subfield = aName.substr(i+1);
    name = aName.substr(0,i);
  }
  else {
    name = aName;
  }
  ValueSourcesMap::iterator pos = valueMap.find(name);
  if (pos==valueMap.end()) {
    return ExpressionError::errValue(ExpressionError::NotFound, "Undefined sensor '%s'", name.c_str());
  }
  // value found
  if (subfield.empty()) {
    // value itself is requested
    if (pos->second->getSourceLastUpdate()!=Never) {
      return ExpressionValue(pos->second->getSourceValue());
    }
  }
  else if (subfield=="valid") {
    return ExpressionValue(pos->second->getSourceLastUpdate()!=Never ? 1 : 0);
  }
  else if (subfield=="oplevel") {
    int lvl = pos->second->getSourceOpLevel();
    if (lvl>=0) return ExpressionValue(lvl);
    // otherwise: no known value
  }
  else if (subfield=="age") {
    if (pos->second->getSourceLastUpdate()!=Never) {
      return ExpressionValue(((double)(MainLoop::now()-pos->second->getSourceLastUpdate()))/Second);
    }
  }
  else {
    return ExpressionError::errValue(ExpressionError::NotFound, "Unknown subfield '%s' for value '%s'", subfield.c_str(), name.c_str());
  }
  // no value (yet)
  return ExpressionError::errValue(ExpressionError::Null, "'%s' has no known value yet", aName.c_str());
}


ExpressionValue EvaluatorDevice::actionValueLookup(Tristate aCurrentState, const string aName)
{
  if (aName=="result") {
    return ExpressionValue(aCurrentState==yes ? 1 : 0);
  }
  else {
    return valueLookup(aName);
  }
}



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
          boost::bind(&EvaluatorDevice::evaluateFunction, this, _1, _2),
          "null"
        );
        if (!Error::isOK(serr)) {
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
        boost::bind(&EvaluatorDevice::evaluateFunction, this, _1, _2),
        "null"
      );
      if (!Error::isOK(serr)) {
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
  ALOG(LOG_INFO, "http action returns '%s', error = %s", aResponse.c_str(), Error::text(aError).c_str());
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


// MARK: ===== property access

enum {
  evaluatorType_key,
  valueDefs_key,
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
    { "x-p44-valueDefs", apivalue_string, valueDefs_key, OKEY(evaluatorDevice_key) },
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
        case valueDefs_key: aPropValue->setStringValue(evaluatorSettings()->valueDefs); return true;
        case onCondition_key: aPropValue->setStringValue(evaluatorSettings()->onCondition); return true;
        case offCondition_key: aPropValue->setStringValue(evaluatorSettings()->offCondition); return true;
        case minOnTime_key: aPropValue->setDoubleValue((double)(evaluatorSettings()->minOnTime)/Second); return true;
        case minOffTime_key: aPropValue->setDoubleValue((double)(evaluatorSettings()->minOffTime)/Second); return true;
        case action_key: aPropValue->setStringValue(evaluatorSettings()->action); return true;
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        case valueDefs_key:
          if (evaluatorSettings()->setPVar(evaluatorSettings()->valueDefs, aPropValue->stringValue()))
            parseValueDefs(); // changed valueDefs, re-parse them
          return true;
        case onCondition_key:
          if (evaluatorSettings()->setPVar(evaluatorSettings()->onCondition, aPropValue->stringValue()))
            changedConditions();  // changed conditions, re-evaluate output
          return true;
        case offCondition_key:
          if (evaluatorSettings()->setPVar(evaluatorSettings()->offCondition, aPropValue->stringValue()))
            changedConditions();  // changed conditions, re-evaluate output
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


// MARK: ===== settings


EvaluatorDeviceSettings::EvaluatorDeviceSettings(Device &aDevice) :
  inherited(aDevice),
  minOnTime(0), // trigger immediately
  minOffTime(0) // trigger immediately
{
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
    { "valueDefs", SQLITE_TEXT },
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
  valueDefs.assign(nonNullCStr(aRow->get<const char *>(aIndex++)));
  onCondition.assign(nonNullCStr(aRow->get<const char *>(aIndex++)));
  offCondition.assign(nonNullCStr(aRow->get<const char *>(aIndex++)));
  aRow->getCastedIfNotNull<MLMicroSeconds, long long int>(aIndex++, minOnTime);
  aRow->getCastedIfNotNull<MLMicroSeconds, long long int>(aIndex++, minOffTime);
  action.assign(nonNullCStr(aRow->get<const char *>(aIndex++)));
}


// bind values to passed statement
void EvaluatorDeviceSettings::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, valueDefs.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, onCondition.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, offCondition.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, (long long int)minOnTime);
  aStatement.bind(aIndex++, (long long int)minOffTime);
  aStatement.bind(aIndex++, action.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
}



#endif // ENABLE_EVALUATORS
