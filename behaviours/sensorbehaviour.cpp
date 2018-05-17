//
//  Copyright (c) 2013-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "sensorbehaviour.hpp"

#if ENABLE_RRDB
#include "rrd.h"
#endif

using namespace p44;


// MARK: ===== WindowEvaluator


WindowEvaluator::WindowEvaluator(MLMicroSeconds aWindowTime, MLMicroSeconds aDataPointCollTime, EvaluationType aEvalType) :
  windowTime(aWindowTime),
  dataPointCollTime(aDataPointCollTime),
  evalType(aEvalType)
{
}




void WindowEvaluator::addValue(double aValue, MLMicroSeconds aTimeStamp)
{
  if (aTimeStamp==Never) aTimeStamp = MainLoop::now();
  // clean away outdated datapoints
  while (!dataPoints.empty()) {
    if (dataPoints.front().timestamp<aTimeStamp-windowTime) {
      // this one is outdated (lies more than windowTime in the past), remove it
      dataPoints.pop_front();
    }
    else {
      break;
    }
  }
  // add new value
  if (!dataPoints.empty()) {
    // check if we should collect into last existing datapoint
    DataPoint &last = dataPoints.back();
    if (collStart+dataPointCollTime>aTimeStamp) {
      // still in collection time window (from start of datapoint collection
      switch (evalType) {
        case eval_max: {
          if (aValue>last.value) last.value = aValue;
          break;
        }
        case eval_min: {
          if (aValue<last.value) last.value = aValue;
          break;
        }
        case eval_timeweighted_average: {
          MLMicroSeconds timeWeight = aTimeStamp-last.timestamp; // between last subdatapoint collected into this datapoint and new timestamp
          if (collDivisor<=0 || timeWeight<=0) { // 0 or negative timeweight should not happen, safety only!
            // first section
            last.value = (last.value + aValue)/2;
            collDivisor = timeWeight;
          }
          else {
            double v = (last.value*collDivisor + aValue*timeWeight);
            collDivisor += timeWeight;
            last.value = v/collDivisor;
          }
          break;
        }
        case eval_average:
        default: {
          if (collDivisor<=0) collDivisor = 1;
          double v = (last.value*collDivisor+aValue);
          collDivisor++;
          last.value = v/collDivisor;
          break;
        }
      }
      last.timestamp = aTimeStamp; // timestamp represents most recent sample in datapoint
      return; // done
    }
  }
  // accumulation of value in previous datapoint complete (or none available at all)
  // -> start new datapoint
  DataPoint dp;
  dp.value = aValue;
  dp.timestamp = aTimeStamp;
  dataPoints.push_back(dp);
  collStart = aTimeStamp;
  collDivisor = 0;
}


double WindowEvaluator::evaluate()
{
  double result = 0;
  double divisor = 0;
  int count = 0;
  for (DataPointsList::iterator pos = dataPoints.begin(); pos != dataPoints.end(); ++pos) {
    MLMicroSeconds lastTs = Never;
    switch (evalType) {
      case eval_max: {
        if (count==0 || pos->value>result) result = pos->value;
        divisor = 1;
        break;
      }
      case eval_min: {
        if (count==0 || pos->value<result) result = pos->value;
        divisor = 1;
        break;
      }
      case eval_timeweighted_average: {
        if (count==0) {
          // the first datapoint's time weight reaches back to beginning of window
          lastTs = dataPoints.back().timestamp-windowTime;
        }
        MLMicroSeconds timeWeight = pos->timestamp-lastTs;
        result += pos->value*timeWeight;
        divisor += timeWeight;
        // next datapoint's time weight will reach back to this datapoint's time
        lastTs = pos->timestamp;
        break;
      }
      case eval_average:
      default: {
        result += pos->value;
        divisor++;
        break;
      }
    }
    count++;
  }
  return divisor ? result/divisor : 0;
}





// MARK: ===== SensorBehaviour

SensorBehaviour::SensorBehaviour(Device &aDevice, const string aId) :
  inherited(aDevice, aId),
  profileP(NULL), // the profile
  // persistent settings
  sensorGroup(group_black_variable), // default to joker
  minPushInterval(30*Second), // default unless sensor type profile sets another value
  changesOnlyInterval(0), // report every sensor update (even if value unchanged)
  // state
  #if ENABLE_RRDB
  loggingReady(false),
  lastRrdUpdate(Never),
  #endif
  lastUpdate(Never),
  lastPush(Never),
  currentValue(0),
  lastPushedValue(0),
  contextId(-1)
{
  // set dummy default hardware default configuration (no known alive sign interval!)
  setHardwareSensorConfig(sensorType_none, usage_undefined, 0, 100, 1, 15*Second, 0);
}


SensorBehaviour::~SensorBehaviour()
{
}



// The value units corresponding with the sensor type
const ValueUnit sensorTypeUnits[numVdcSensorTypes] = {
  VALUE_UNIT(valueUnit_none, unitScaling_1), ///< none
  VALUE_UNIT(valueUnit_celsius, unitScaling_1), ///< temperature in degrees celsius
  VALUE_UNIT(valueUnit_percent, unitScaling_1), ///< relative humidity in %
  VALUE_UNIT(valueUnit_lux, unitScaling_1), ///< illumination in lux
  VALUE_UNIT(valueUnit_volt, unitScaling_1), ///< supply voltage level in Volts
  VALUE_UNIT(valueUnit_ppm, unitScaling_1), ///< CO (carbon monoxide) concentration in ppm
  VALUE_UNIT(valueUnit_bequerelperm3, unitScaling_1), ///< Radon activity in Bq/m3
  VALUE_UNIT(valueUnit_none, unitScaling_1), ///< gas type sensor
  VALUE_UNIT(valueUnit_gramperm3, unitScaling_micro), ///< particles <10µm in μg/m3
  VALUE_UNIT(valueUnit_gramperm3, unitScaling_micro), ///< particles <2.5µm in μg/m3
  VALUE_UNIT(valueUnit_gramperm3, unitScaling_micro), ///< particles <1µm in μg/m3
  VALUE_UNIT(valueUnit_none, unitScaling_1), ///< room operating panel set point, 0..1
  VALUE_UNIT(valueUnit_none, unitScaling_1), ///< fan speed, 0..1 (0=off, <0=auto)
  VALUE_UNIT(valueUnit_meterpersecond, unitScaling_1), ///< wind speed in m/s
  VALUE_UNIT(valueUnit_watt, unitScaling_1), ///< Power in W
  VALUE_UNIT(valueUnit_ampere, unitScaling_1), ///< Electric current in A
  VALUE_UNIT(valueUnit_watthour, unitScaling_kilo), ///< Energy in kWh
  VALUE_UNIT(valueUnit_voltampere, unitScaling_1), ///< Apparent electric power in VA
  VALUE_UNIT(valueUnit_pascal, unitScaling_hecto), ///< Air pressure in hPa
  VALUE_UNIT(valueUnit_degree, unitScaling_1), ///< Wind direction in degrees
  VALUE_UNIT(valueUnit_bel, unitScaling_deci), ///< Sound pressure level in dB
  VALUE_UNIT(valueUnit_meterperm2, unitScaling_milli), ///< Precipitation in mm/m2
  VALUE_UNIT(valueUnit_ppm, unitScaling_1), ///< CO2 (carbon dioxide) concentration in ppm
  VALUE_UNIT(valueUnit_meterpersecond, unitScaling_1), ///< gust speed in m/S
  VALUE_UNIT(valueUnit_degree, unitScaling_1), ///< gust direction in degrees
  VALUE_UNIT(valueUnit_watt, unitScaling_1), ///< Generated power in W
  VALUE_UNIT(valueUnit_watthour, unitScaling_kilo), ///< Generated energy in kWh
  VALUE_UNIT(valueUnit_liter, unitScaling_1), ///< Water quantity in liters
  VALUE_UNIT(valueUnit_literpermin, unitScaling_1), ///< Water flow rate in liters/minute
  VALUE_UNIT(valueUnit_meter, unitScaling_1), ///< Length in meters
  VALUE_UNIT(valueUnit_gram, unitScaling_1), ///< mass in grams
  VALUE_UNIT(valueUnit_second, unitScaling_1), ///< time in seconds
};


const char *sensorTypeIds[numVdcSensorTypes] = {
  "undefined", ///< none
  "temperature", ///< temperature in degrees celsius
  "humidity", ///< relative humidity in %
  "brightness", ///< illumination in lux
  "voltage", ///< supply voltage level in Volts
  "co_concentration", ///< CO (carbon monoxide) concentration in ppm
  "radon_activity", ///< Radon activity in Bq/m3
  "gas_type", ///< gas type sensor
  "particles_st_10um", ///< particles <10µm in μg/m3
  "particles_st_2500nm", ///< particles <2.5µm in μg/m3
  "particles_st_1um", ///< particles <1µm in μg/m3
  "set_point", ///< room operating panel set point, 0..1
  "fan_speed", ///< fan speed, 0..1 (0=off, <0=auto)
  "wind_speed", ///< wind speed in m/s
  "power", ///< Power in W
  "current", ///< Electric current in A
  "energy", ///< Energy in kWh
  "apparent_power", ///< Apparent electric power in VA
  "air_pressure", ///< Air pressure in hPa
  "wind_direction", ///< Wind direction in degrees
  "sound_pressure", ///< Sound pressure level in dB
  "precipitation", ///< Precipitation in mm/m2
  "co2_concentration", ///< CO2 (carbon dioxide) concentration in ppm
  "gust_speed", ///< gust speed in m/S
  "gust_direction", ///< gust direction in degrees
  "generated_power", ///< Generated power in W
  "generated_energy", ///< Generated energy in kWh
  "water_quantity", ///< Water quantity in liters
  "water_flow", ///< Water flow rate in liters/minute
  "length", ///< Length in meters
  "mass", ///< mass in grams
  "time", ///< duration in seconds
};


static const SensorBehaviourProfile sensorBehaviourProfiles[] = {
  // type                      usage           evalWin    collWin          evalType                                    pushIntvl  chgOnlyIntvl  trigDelta  trigRel  trigMin trigIntvl
  // ------------------------  -------------   ---------  --------------   ------------------------------------------- ---------  ------------  ---------  -------  ------- --------------------------
  // indoor context
  { sensorType_temperature,    usage_room,     0,         0,               WindowEvaluator::eval_none,                 5*Minute,  60*Minute,    0.5,       false,   -100,   1*Second /* = "immediate" */ },
  { sensorType_humidity,       usage_room,     0,         0,               WindowEvaluator::eval_none,                 30*Minute, 60*Minute,    2,         false,   -1,     1*Second /* = "immediate" */ },
  { sensorType_illumination,   usage_room,     5*Minute,  10*Second,       WindowEvaluator::eval_timeweighted_average, 5*Minute,  60*Minute,    0,         false,   0,      0 },
  { sensorType_gas_CO2,        usage_room,     0,         0,               WindowEvaluator::eval_none,                 5*Minute,  60*Minute,    0,         false,   0,      0 },
  { sensorType_gas_CO,         usage_room,     0,         0,               WindowEvaluator::eval_none,                 5*Minute,  60*Minute,    0,         false,   0,      0 },
  // outdoor context
  { sensorType_temperature,    usage_outdoors, 0,         0,               WindowEvaluator::eval_none,                 5*Minute,  60*Minute,    0.5,       false,   -100,   1*Second /* = "immediate" */ },
  { sensorType_humidity,       usage_outdoors, 0,         0,               WindowEvaluator::eval_none,                 30*Minute, 60*Minute,    2,         false,   -1,     1*Second /* = "immediate" */ },
  { sensorType_illumination,   usage_outdoors, 10*Minute, 20*Second,       WindowEvaluator::eval_timeweighted_average, 5*Minute,  60*Minute,    0,         false,   0,      0 },
  { sensorType_air_pressure,   usage_outdoors, 0,         0,               WindowEvaluator::eval_none,                 15*Minute, 60*Minute,    0,         false,   0,      0 },
  { sensorType_wind_speed,     usage_outdoors, 10*Minute, 1*Minute,        WindowEvaluator::eval_timeweighted_average, 10*Minute, 60*Minute,    0.1,       true,    -1,     1*Minute },
  { sensorType_wind_direction, usage_outdoors, 10*Minute, 1*Minute,        WindowEvaluator::eval_timeweighted_average, 10*Minute, 60*Minute,    20,        false,   -1,     1*Minute },
  { sensorType_gust_speed,     usage_outdoors, 3*Second,  200*MilliSecond, WindowEvaluator::eval_max,                  10*Minute, 60*Minute,    0.1,       true,    5,      3*Second /* = "immediate" */},
  // FIXME: rule says "accumulation", but as long as sensors deliver intensity in mm/h, it is in fact a window average over an hour
  { sensorType_precipitation,  usage_outdoors, 60*Minute, 2*Minute,        WindowEvaluator::eval_timeweighted_average, 60*Minute, 60*Minute,    0,         false,   0,      0 },

  // terminator
  { sensorType_none,           usage_undefined,0,         0,               WindowEvaluator::eval_none,                 0,         0,            0,         false,   0,      0 },
};






void SensorBehaviour::setHardwareSensorConfig(VdcSensorType aType, VdcUsageHint aUsage, double aMin, double aMax, double aResolution, MLMicroSeconds aUpdateInterval, MLMicroSeconds aAliveSignInterval, MLMicroSeconds aDefaultChangesOnlyInterval)
{
  sensorType = aType;
  sensorUsage = aUsage;
  min = aMin;
  max = aMax;
  resolution = aResolution;
  updateInterval = aUpdateInterval;
  aliveSignInterval = aAliveSignInterval;
  armInvalidator();
  profileP = NULL;
  // default only, devices once created will have this as a persistent setting
  changesOnlyInterval = aDefaultChangesOnlyInterval; // default in case sensor profile does not override this
  // look for sensor behaviour profile
  const SensorBehaviourProfile *sbpP = sensorBehaviourProfiles;
  while (sbpP->type!=sensorType_none) {
    if (sensorType==sbpP->type && sensorUsage==sbpP->usage) {
      // sensor type/usage has a behaviour profile, use it
      LOG(LOG_INFO, "Activated sensor processing/filtering profile for '%s' (usage %d)", sensorTypeIds[sensorType], sensorUsage);
      profileP = sbpP;
      // get settings defaults
      if (profileP->pushIntvl>0) minPushInterval = profileP->pushIntvl;
      if (profileP->chgOnlyIntvl>0) changesOnlyInterval = profileP->chgOnlyIntvl;
    }
    ++sbpP; // next
  }
}


string SensorBehaviour::getAutoId()
{
  return sensorTypeIds[sensorType];
}



ValueUnit SensorBehaviour::getSensorUnit()
{
  if (sensorType>=numVdcSensorTypes) return VALUE_UNIT(valueUnit_none, unitScaling_1);
  return sensorTypeUnits[sensorType];
}


string SensorBehaviour::getSensorUnitText()
{
  return valueUnitName(getSensorUnit(), true);
}


string SensorBehaviour::getSensorRange()
{
  if (min==max) {
    return ""; // undefined range
  }
  int fracDigits = 2; // default if no resolution defined
  if (resolution!=0) {
    fracDigits = (int)(-log(resolution)/log(10)+0.99);
  }
  if (fracDigits<0) fracDigits=0;
  return string_format("%0.*f..%0.*f", fracDigits, min, fracDigits, max);
}


string SensorBehaviour::getStatusText()
{
  if (hasDefinedState()) {
    int fracDigits = 2; // default if no resolution defined
    if (resolution!=0) {
      fracDigits = (int)(-log(resolution)/log(10)+0.99);
    }
    if (fracDigits<0) fracDigits=0;
    return string_format("%0.*f %s", fracDigits, currentValue, getSensorUnitText().c_str());
  }
  return inherited::getStatusText();
}


void SensorBehaviour::setSensorNameWithRange(const char *aName)
{
  setHardwareName(string_format("%s, %s %s", aName, getSensorRange().c_str(), getSensorUnitText().c_str()));
}




void SensorBehaviour::updateEngineeringValue(long aEngineeringValue, bool aPush, int32_t aContextId, const char *aContextMsg)
{
  double newCurrentValue = min+(aEngineeringValue*resolution);
  updateSensorValue(newCurrentValue, -1, aPush, aContextId, aContextMsg);
}


void SensorBehaviour::armInvalidator()
{
  invalidatorTicket.cancel();
  if (aliveSignInterval!=Never) {
    // this sensor can time out, schedule invalidation
    invalidatorTicket.executeOnce(boost::bind(&SensorBehaviour::invalidateSensorValue, this, true), aliveSignInterval);
  }
}


void SensorBehaviour::updateSensorValue(double aValue, double aMinChange, bool aPush, int32_t aContextId, const char *aContextMsg)
{
  MLMicroSeconds now = MainLoop::now();
  bool changedValue = false;
  // always update age, even if value itself may not have changed
  lastUpdate = now;
  armInvalidator();
  // update context
  if (contextId!=aContextId) {
    contextId = aContextId;
    changedValue = true;
  };
  if (contextMsg!=nonNullCStr(aContextMsg)) {
    contextMsg = nonNullCStr(aContextMsg);
    changedValue = true;
  };
  // update value
  if (aMinChange<0) aMinChange = resolution/2;
  if (fabs(aValue - currentValue) > aMinChange) changedValue = true;
  BLOG(changedValue ? LOG_NOTICE : LOG_INFO, "Sensor[%zu] %s '%s' reports %s value = %0.3f %s", index, behaviourId.c_str(), getHardwareName().c_str(), changedValue ? "NEW" : "same", aValue, getSensorUnitText().c_str());
  if (contextId>=0 || !contextMsg.empty()) {
    LOG(LOG_INFO, "- contextId=%d, contextMsg='%s'", contextId, contextMsg.c_str());
  }
  if (changedValue) {
    // check for averaging
    if (profileP && profileP->evalType!=WindowEvaluator::eval_none) {
      // process values through filter
      if (!filter) {
        // need a filter, create it
        filter = WindowEvaluatorPtr(new WindowEvaluator(profileP->evalWin, profileP->collWin, profileP->evalType));
      }
      filter->addValue(aValue, now);
      double v = filter->evaluate();
      // re-evaluate changed flag after filtering
      if (fabs(v - currentValue) > resolution/2) changedValue = true;
      BLOG(changedValue ? LOG_NOTICE : LOG_INFO, "Sensor[%zu] %s '%s' calculates %s filtered value = %0.3f %s", index, behaviourId.c_str(), getHardwareName().c_str(), changedValue ? "NEW" : "same", v, getSensorUnitText().c_str());
      currentValue = v;
    }
    else {
      // just assign new current value
      currentValue = aValue;
    }
  }
  // possibly push
  if (aPush) {
    pushSensor();
  }
  // notify listeners
  notifyListeners(changedValue ? valueevent_changed : valueevent_confirmed);
  // possibly log value
  #if ENABLE_RRDB
  logSensorValue(now, aValue, currentValue, lastPushedValue);
  #endif
}



bool SensorBehaviour::pushSensor(bool aAlways)
{
  MLMicroSeconds now = MainLoop::now();
  bool doPush = aAlways || lastPush==Never; // always push if asked for or when there's no previous value
  if (!doPush) {
    // Note: when we get here, lastPush and lastPushedValue are always valid
    bool changed = currentValue!=lastPushedValue;
    if (now>lastPush+minPushInterval) {
      // Minimal push interval is over -> push...
      // - if value has changed or
      // - changesOnlyInterval has been exceeded
      doPush =
        changed || // when changed
        now>lastPush+changesOnlyInterval || // when interval for reporting anyway has passed
        (aliveSignInterval>0 && now>lastUpdate+aliveSignInterval); // and in case sensor declares a heartbeat interval
    }
    else if (profileP) {
      // Minimal push interval is NOT over, check extra trigger conditions
      if (profileP->trigDelta>0 && now>lastPush+profileP->trigIntvl) {
        // Trigger interval is over -> push if conditions are met
        doPush =
          currentValue>profileP->trigMin &&
          fabs(currentValue-lastPushedValue)>=(profileP->trigRel ? fabs(lastPushedValue*profileP->trigDelta) : profileP->trigDelta);
        if (doPush) {
          BLOG(LOG_INFO, "Sensor[%zu] %s '%s' meets SOD conditions (%0.3f ->%0.3f %s) to push now", index, behaviourId.c_str(), getHardwareName().c_str(), lastPushedValue, currentValue, getSensorUnitText().c_str());
        }
      }
    }
  }
  if (doPush) {
    // push the new value
    if (pushBehaviourState()) {
      lastPush = now;
      lastPushedValue = currentValue;
      return true;
    }
    else if (device.isPublicDS()) {
      BLOG(LOG_NOTICE, "Sensor[%zu] %s '%s' could not be pushed", index, behaviourId.c_str(), getHardwareName().c_str());
    }
  }
  return false;
}



void SensorBehaviour::invalidateSensorValue(bool aPush)
{
  if (lastUpdate!=Never) {
    // currently valid -> invalidate
    lastUpdate = Never;
    currentValue = 0;
    BLOG(LOG_NOTICE, "Sensor[%zu] %s '%s' reports value no longer available", index, behaviourId.c_str(), getHardwareName().c_str());
    if (aPush) {
      // push invalidation (primitive clients not capable of NULL will at least see value==0)
      pushSensor(true);
    }
    // notify listeners
    notifyListeners(valueevent_changed);
  }
}


bool SensorBehaviour::hasCurrentValue(MLMicroSeconds aMaxAge)
{
  if (lastUpdate==Never) return false; // no value at all
  MLMicroSeconds now = MainLoop::now();
  return now < lastUpdate+aMaxAge;
}


bool SensorBehaviour::hasDefinedState()
{
  return lastUpdate!=Never;
}





// MARK: ===== value source implementation


string SensorBehaviour::getSourceId()
{
  return string_format("%s_S%s", device.getDsUid().getString().c_str(), getId().c_str());
}


string SensorBehaviour::getSourceName()
{
  // get device name or dSUID for context
  string n = device.getAssignedName();
  if (n.empty()) {
    // use abbreviated dSUID instead
    string d = device.getDsUid().getString();
    n = d.substr(0,8) + "..." + d.substr(d.size()-2,2);
  }
  // append behaviour description
  string_format_append(n, ": %s", getHardwareName().c_str());
  return n;
}


double SensorBehaviour::getSourceValue()
{
  return getCurrentValue();
}


MLMicroSeconds SensorBehaviour::getSourceLastUpdate()
{
  return getLastUpdateTimestamp();
}


int SensorBehaviour::getSourceOpLevel()
{
  return device.opStateLevel();
}

#if ENABLE_RRDB

// MARK: ===== RRD sensor value logging



typedef std::vector<string> ArgsVector;

typedef int (*RrdFunc)(int, char **);

static int rrd_call(RrdFunc aFunc, ArgsVector &aArgs)
{
  char **argsArrayP = new char*[aArgs.size()+1];
  LOG(LOG_DEBUG, "rrd_call:");
  for (int i=0; i<aArgs.size(); ++i) {
    LOG(LOG_DEBUG, "- %s", aArgs[i].c_str());
    argsArrayP[i] = (char *)aArgs[i].c_str();
  }
  argsArrayP[aArgs.size()] = NULL;
  optind = opterr = 0; /* Because rrdtool uses getopt() */
  rrd_clear_error();
  int ret = aFunc((int)aArgs.size(), argsArrayP);
  delete[] argsArrayP;
  LOG(LOG_DEBUG, "rrd_call returns: %d", ret);
  return ret;
}


static string rrdval(double aVal, bool aValid)
{
  if (aValid)
    return string_format("%f", aVal);
  else
    return "U";
}


static string rrdminmax(double aMin, double aMax)
{
  bool valid = aMin!=aMax;
  return rrdval(aMin, valid) + ":" + rrdval(aMax, valid);
}


void SensorBehaviour::prepareLogging()
{
  if (!loggingReady && !rrdbconfig.empty() && rrdbfile.empty()) {
    // configured but not yet prepared to log
    // Note: not ready and rrdbfile NOT empty means that we could not start logging due to a problem (-> no op)
    // always need to parse config first to get update statement, maybe to (re-)create file
    ArgsVector cfgArgs;
    const char *p = rrdbconfig.c_str();
    string arg;
    bool autoRaw = false;
    bool autoFiltered = false;
    bool autoPushed = false;
    bool autoRRA = false;
    bool autoStep = true;
    bool autoUpdate = true;
    long step = 1;
    while (nextPart(p, arg, ' ')) {
      arg = trimWhiteSpace(arg);
      // catch special "macros"
      if (arg=="--step") {
        // there is a custom --step, prevent auto-generating one
        autoStep = false;
        // scan step value
        string stp;
        if (!nextPart(p, stp, ' ')) break;
        sscanf(stp.c_str(), "%ld", &step);
        // also forward --step argument (no autostep)
        cfgArgs.push_back(stp);
      }
      else if (arg=="auto") {
        // fully automatic sample of the current (possibly filtered) value, with standard RRAs
        autoFiltered = true;
        autoRRA = true;
      }
      else if (arg=="autods") {
        // fully automatic datasource, but no rra
        autoFiltered = true;
      }
      else if (arg=="autorra") {
        // automatic "reasonable" RRAs
        autoRRA = true;
      }
      else if (arg.substr(0,7)=="autods:") {
        // automatic datasources
        // Syntax autods:[R][F][P]  for raw, filtered, pushed
        autoRaw = arg.find("R",7)!=string::npos;
        autoFiltered = arg.find("F",7)!=string::npos;
        autoPushed = arg.find("P",7)!=string::npos;
      }
      else if (arg.substr(0,7)=="update:") {
        // update statement in case no autods is in use (any autods use will create a update string automatically)
        // Syntax update:<rrdb update string with %R, %F and %P placeholders>
        autoUpdate = false;
        rrdbupdate = arg.substr(7); // rest of string is considered update string
      }
      else {
        // is regular RRD create argument, use as-is
        cfgArgs.push_back(arg);
      }
    }
    // in any case, we need the update statement
    loggingReady = true; // assume true
    if (autoUpdate) {
      rrdbupdate = "N";
      if (autoRaw) rrdbupdate += ":%R";
      if (autoFiltered) rrdbupdate += ":%F";
      if (autoPushed) rrdbupdate += ":%P";
      if (rrdbupdate.size()<4) {
        BLOG(LOG_WARNING, "Cannot create RRD update string, missing 'auto..' or 'update' config");
        rrdbupdate = "";
        loggingReady = false; // no point in trying
      }
    }
    // use or create rrd file
    rrdbfile = Application::sharedApplication()->dataPath(rrdbpath);
    if (rrdbpath.empty() || rrdbfile[rrdbfile.size()-1]=='/') {
      // auto-generate filename
      pathstring_format_append(rrdbfile, "Log_%s.rrd", getSourceId().c_str());
    }
    struct stat st;
    if (loggingReady && stat(rrdbfile.c_str(), &st)<0 && errno==ENOENT) {
      // does not exist yet, create new
      loggingReady = false; // not any more, only if we succeed in creating new file it'll get set again
      string dsname = string_format("%.17s", sensorTypeIds[sensorType]); // 19 chars max for RRD data sources, we need 2 for suffix
      // at this spoint, cfgArgs vector contains explicitly specified RRD args from config
      // - now create the actual
      ArgsVector args;
      // command and filename
      args.push_back("rrdcreate");
      args.push_back(rrdbfile);
      // always start from now
      args.push_back("--start"); args.push_back("now"); // start now
      // possibly automatic --step option
      if (autoStep) {
        step = updateInterval>15*Second ? updateInterval/Second : 15;
        // use step from sensor's update interval (but not faster than once in 15 seconds)
        args.push_back("--step"); args.push_back(string_format("%ld", step)); // use sensor's native interval if known, 15sec otherwise
      }
      // possibly automatic datasources
      long heartbeat = aliveSignInterval ? aliveSignInterval/Second : step*5;
      if (autoRaw) {
        args.push_back(string_format("DS:%s_R:GAUGE:%ld:%s", dsname.c_str(), heartbeat, rrdminmax(min, max).c_str()));
      }
      if (autoFiltered) {
        args.push_back(string_format("DS:%s_F:GAUGE:%ld:%s", dsname.c_str(), heartbeat, rrdminmax(min, max).c_str()));
      }
      if (autoPushed) {
        args.push_back(string_format("DS:%s_P:GAUGE:%ld:%s", dsname.c_str(), heartbeat, rrdminmax(min, max).c_str()));
      }
      if (autoRRA) {
        // now RRAs: RRA:AVERAGE:xff:steps:rows
        args.push_back(string_format("RRA:AVERAGE:0.5:%ld:%ld", 1l, 7*24*3600/step)); // 1:1 samples for a week
        args.push_back(string_format("RRA:AVERAGE:0.5:%ld:%ld", 3600/step, 30*24*3600*step/3600)); // hourly datapoints for 1 months (30 days)
        args.push_back(string_format("RRA:AVERAGE:0.5:%ld:%ld", 24*3600/step, 2*365*24*3600*step/24/3600)); // daily for 2 years
      }
      // now add explicit config
      args.insert(args.end(), cfgArgs.begin(), cfgArgs.end());
      // create the rrd file from config
      int ret = rrd_call(rrd_create, args);
      if (ret==0) {
        BLOG(LOG_INFO, "rrd: successfully created new rrd file '%s'", rrdbfile.c_str());
        loggingReady = true;
        lastRrdUpdate = MainLoop::currentMainLoop().now(); // creation counts as update, make sure we don't immediately try to send first update afterwards
      }
      else {
        BLOG(LOG_ERR, "rrd: cannot create rrd file '%s': %s", rrdbfile.c_str(), rrd_get_error());
      }
    }
    else {
      // file apparently already exists
      BLOG(LOG_INFO, "rrd: using existing file '%s'", rrdbfile.c_str());
      loggingReady = true;
    }
  }
}


void SensorBehaviour::logSensorValue(MLMicroSeconds aTimeStamp, double aRawValue, double aProcessedValue, double aPushedValue)
{
  // make sure logging is prepared
  prepareLogging();
  // now actually log into file
  if (loggingReady && lastRrdUpdate<aTimeStamp-10*Second) {
    lastRrdUpdate = aTimeStamp;
    ArgsVector args;
    args.push_back("rrdupdate");
    args.push_back(rrdbfile);
    string ud = rrdbupdate;
    // substitute values for placeholders
    size_t i;
    i = ud.find("%T");
    if (i!=string::npos) ud.replace(i, 2, string_format("%lld", aTimeStamp/Second).c_str());
    // - raw (considered unknown when sensor is invalid)
    i = ud.find("%R");
    if (i!=string::npos) ud.replace(i, 2, rrdval(aRawValue, lastUpdate!=Never).c_str());
    // - filtered (considered unknown when sensor is invalid)
    i = ud.find("%F");
    if (i!=string::npos) ud.replace(i, 2, rrdval(aProcessedValue, lastUpdate!=Never).c_str());
    // - pushed (considered unknown when never pushed, or last state pushed must have been NULL because of invalidation)
    i = ud.find("%P");
    if (i!=string::npos) ud.replace(i, 2, rrdval(aPushedValue, lastPush!=Never && lastUpdate!=Never).c_str());
    args.push_back(ud);
    int ret = rrd_call(rrd_update, args);
    if (ret!=0) {
      BLOG(LOG_WARNING, "rrd: could not update rrd data for file '%s': %s", rrdbfile.c_str(), rrd_get_error());
      loggingReady = false; // back to not initialized, might work again after recreating file
    }
  }
}

#endif // ENABLE_RRDB



// MARK: ===== persistence implementation


// SQLIte3 table name to store these parameters to
const char *SensorBehaviour::tableName()
{
  return "SensorSettings";
}


// data field definitions


#if ENABLE_RRDB
static const size_t numFields = 5;
#else
static const size_t numFields = 3;
#endif

size_t SensorBehaviour::numFieldDefs()
{
  return inherited::numFieldDefs()+numFields;
}


const FieldDefinition *SensorBehaviour::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numFields] = {
    { "dsGroup", SQLITE_INTEGER }, // Note: don't call a SQL field "group"!
    { "minPushInterval", SQLITE_INTEGER },
    { "changesOnlyInterval", SQLITE_INTEGER },
    #if ENABLE_RRDB
    { "rrdbConfig", SQLITE_TEXT },
    { "rrdbPath", SQLITE_TEXT },
    #endif
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void SensorBehaviour::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inherited::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get the fields
  aRow->getCastedIfNotNull<DsGroup, int>(aIndex++, sensorGroup);
  aRow->getCastedIfNotNull<MLMicroSeconds, long long int>(aIndex++, minPushInterval);
  aRow->getCastedIfNotNull<MLMicroSeconds, long long int>(aIndex++, changesOnlyInterval);
  #if ENABLE_RRDB
  aRow->getIfNotNull(aIndex++, rrdbconfig);
  aRow->getIfNotNull(aIndex++, rrdbpath);
  // make sure logging is ready (if enabled at all)
  prepareLogging();
  #endif
}


// bind values to passed statement
void SensorBehaviour::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, sensorGroup);
  aStatement.bind(aIndex++, (long long int)minPushInterval);
  aStatement.bind(aIndex++, (long long int)changesOnlyInterval);
  #if ENABLE_RRDB
  aStatement.bind(aIndex++, rrdbconfig.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, rrdbpath.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  #endif
}



// MARK: ===== property access

static char sensor_key;

// description properties

enum {
  sensorType_key,
  sensorUsage_key,
  siunit_key,
  unitsymbol_key,
  min_key,
  max_key,
  resolution_key,
  updateInterval_key,
  aliveSignInterval_key,
  #if ENABLE_RRDB
  rrdbFile_key,
  #endif
  numDescProperties
};


int SensorBehaviour::numDescProps() { return numDescProperties; }
const PropertyDescriptorPtr SensorBehaviour::getDescDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numDescProperties] = {
    { "sensorType", apivalue_uint64, sensorType_key+descriptions_key_offset, OKEY(sensor_key) },
    { "sensorUsage", apivalue_uint64, sensorUsage_key+descriptions_key_offset, OKEY(sensor_key) },
    { "siunit", apivalue_string, siunit_key+descriptions_key_offset, OKEY(sensor_key) },
    { "symbol", apivalue_string, unitsymbol_key+descriptions_key_offset, OKEY(sensor_key) },
    { "min", apivalue_double, min_key+descriptions_key_offset, OKEY(sensor_key) },
    { "max", apivalue_double, max_key+descriptions_key_offset, OKEY(sensor_key) },
    { "resolution", apivalue_double, resolution_key+descriptions_key_offset, OKEY(sensor_key) },
    { "updateInterval", apivalue_double, updateInterval_key+descriptions_key_offset, OKEY(sensor_key) },
    { "aliveSignInterval", apivalue_double, aliveSignInterval_key+descriptions_key_offset, OKEY(sensor_key) },
    #if ENABLE_RRDB
    { "x-p44-rrdFile", apivalue_string, rrdbFile_key+descriptions_key_offset, OKEY(sensor_key) },
    #endif
  };
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}


// settings properties

enum {
  group_key,
  minPushInterval_key,
  changesOnlyInterval_key,
  #if ENABLE_RRDB
  rrdbPath_key,
  rrdbConfig_key,
  #endif
  numSettingsProperties
};


int SensorBehaviour::numSettingsProps() { return numSettingsProperties; }
const PropertyDescriptorPtr SensorBehaviour::getSettingsDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numSettingsProperties] = {
    { "group", apivalue_uint64, group_key+settings_key_offset, OKEY(sensor_key) },
    { "minPushInterval", apivalue_double, minPushInterval_key+settings_key_offset, OKEY(sensor_key) },
    { "changesOnlyInterval", apivalue_double, changesOnlyInterval_key+settings_key_offset, OKEY(sensor_key) },
    #if ENABLE_RRDB
    { "x-p44-rrdFilePath", apivalue_string, rrdbPath_key+settings_key_offset, OKEY(sensor_key) },
    { "x-p44-rrdConfig", apivalue_string, rrdbConfig_key+settings_key_offset, OKEY(sensor_key) },
    #endif
  };
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}

// state properties

enum {
  value_key,
  age_key,
  contextid_key,
  contextmsg_key,
  numStateProperties
};


int SensorBehaviour::numStateProps() { return numStateProperties; }
const PropertyDescriptorPtr SensorBehaviour::getStateDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numStateProperties] = {
    { "value", apivalue_double, value_key+states_key_offset, OKEY(sensor_key) },
    { "age", apivalue_double, age_key+states_key_offset, OKEY(sensor_key) },
    { "contextId", apivalue_uint64, contextid_key+states_key_offset, OKEY(sensor_key) },
    { "contextMsg", apivalue_string, contextmsg_key+states_key_offset, OKEY(sensor_key) },
  };
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}


// access to all fields

bool SensorBehaviour::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(sensor_key)) {
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Description properties
        case sensorType_key+descriptions_key_offset:
          aPropValue->setUint16Value(sensorType);
          return true;
        case sensorUsage_key+descriptions_key_offset:
          aPropValue->setUint16Value(sensorUsage);
          return true;
        case siunit_key+descriptions_key_offset:
          aPropValue->setStringValue(valueUnitName(getSensorUnit(), false));
          return true;
        case unitsymbol_key+descriptions_key_offset:
          aPropValue->setStringValue(valueUnitName(getSensorUnit(), true));
          return true;
        case min_key+descriptions_key_offset:
          if (min==max) return false;
          aPropValue->setDoubleValue(min);
          return true;
        case max_key+descriptions_key_offset:
          if (min==max) return false;
          aPropValue->setDoubleValue(max);
          return true;
        case resolution_key+descriptions_key_offset:
          if (resolution==0) return false;
          aPropValue->setDoubleValue(resolution);
          return true;
        case updateInterval_key+descriptions_key_offset:
          aPropValue->setDoubleValue((double)updateInterval/Second);
          return true;
        case aliveSignInterval_key+descriptions_key_offset:
          aPropValue->setDoubleValue((double)aliveSignInterval/Second);
          return true;
        #if ENABLE_RRDB
        case rrdbFile_key+descriptions_key_offset:
          if (rrdbfile.empty()) return false; // only visible if there actually IS a file
          aPropValue->setStringValue(rrdbfile);
          return true;
        #endif
        // Settings properties
        case group_key+settings_key_offset:
          aPropValue->setUint16Value(sensorGroup);
          return true;
        case minPushInterval_key+settings_key_offset:
          aPropValue->setDoubleValue((double)minPushInterval/Second);
          return true;
        case changesOnlyInterval_key+settings_key_offset:
          aPropValue->setDoubleValue((double)changesOnlyInterval/Second);
          return true;
        #if ENABLE_RRDB
        case rrdbPath_key+settings_key_offset:
          aPropValue->setStringValue(rrdbpath);
          return true;
        case rrdbConfig_key+settings_key_offset:
          aPropValue->setStringValue(rrdbconfig);
          return true;
        #endif
        // States properties
        case value_key+states_key_offset:
          // value
          if (!hasDefinedState())
            aPropValue->setNull();
          else
            aPropValue->setDoubleValue(currentValue);
          return true;
        case age_key+states_key_offset:
          // age
          if (!hasDefinedState())
            aPropValue->setNull();
          else
            aPropValue->setDoubleValue((double)(MainLoop::now()-lastUpdate)/Second);
          return true;
        case contextid_key+states_key_offset:
          // context ID
          if (!hasDefinedState() || contextId<0) return false; // not available
          aPropValue->setUint32Value((int32_t)contextId);
          return true;
        case contextmsg_key+states_key_offset:
          // context message
          if (!hasDefinedState() || contextMsg.empty()) return false; // not available
          aPropValue->setStringValue(contextMsg);
          return true;
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Settings properties
        case group_key+settings_key_offset:
          setPVar(sensorGroup, (DsGroup)aPropValue->int32Value());
          return true;
        case minPushInterval_key+settings_key_offset:
          setPVar(minPushInterval, (MLMicroSeconds)(aPropValue->doubleValue()*Second));
          return true;
        case changesOnlyInterval_key+settings_key_offset:
          setPVar(changesOnlyInterval, (MLMicroSeconds)(aPropValue->doubleValue()*Second));
          return true;
        #if ENABLE_RRDB
        case rrdbPath_key+settings_key_offset:
          if (setPVar(rrdbpath, aPropValue->stringValue())) {
            rrdbfile.clear(); // force re-setup of rrdb logging
          }
          return true;
        case rrdbConfig_key+settings_key_offset:
          if (setPVar(rrdbconfig, aPropValue->stringValue())) {
            rrdbfile.clear(); // force re-setup of rrdb logging
          }
          return true;
        #endif
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}



// MARK: ===== description/shortDesc


string SensorBehaviour::description()
{
  string s = string_format("%s behaviour", shortDesc().c_str());
  string_format_append(s, "\n- sensor type: %d, min: %0.1f, max: %0.1f, resolution: %0.3f, interval: %lld mS", sensorType, min, max, resolution, updateInterval/MilliSecond);
  string_format_append(s, "\n- minimal interval between pushes: %lld mS, aliveSignInterval: %lld mS", minPushInterval/MilliSecond, aliveSignInterval/MilliSecond);
  s.append(inherited::description());
  return s;
}

