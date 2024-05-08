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

#include "sensorbehaviour.hpp"

#if ENABLE_RRDB
#include "rrd.h"
#endif

using namespace p44;


// MARK: - SensorBehaviour

#define DSS_SENSOR_MAX_PUSH_INTERVAL (55*Minute) // all sensors that have a aliveSignInterval!=Never must get pushed at least this often (otherwise dSS flags sensor red)

SensorBehaviour::SensorBehaviour(Device &aDevice, const string aId) :
  inherited(aDevice, aId),
  mProfileP(NULL), // the profile
  // persistent settings
  mSensorGroup(group_black_variable), // default to joker
  mMinPushInterval(30*Second), // default unless sensor type profile sets another value
  mMaxPushInterval(0),
  mChangesOnlyInterval(30*Minute), // report unchanged values only rarely by default (note: before 2021-09-16 we did not have a default limit here)
  #if !REDUCED_FOOTPRINT
  mSensorFunc(sensorFunc_standard),
  mSensorChannel(channeltype_default),
  #endif
  #if ENABLE_JSONBRIDGEAPI
  mBridgeExclusive(false),
  #endif
  // state
  #if ENABLE_RRDB
  mLoggingReady(false),
  mLastRRDBupdate(Never),
  #endif
  mLastUpdate(Never),
  mLastPush(Never),
  mCurrentValue(0),
  mLastPushedValue(0),
  mContextId(-1)
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
  VALUE_UNIT(valueUnit_percent, unitScaling_1), ///< absolute percentage 0..100% (for user dimmers)
  VALUE_UNIT(valueUnit_percent, unitScaling_1), ///< change speed in % of full range per second (for user dimmers)
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
  "percent", ///< absolute percent value
  "percent_speed", ///< relative speed in % of full range per second
};


static const SensorBehaviourProfile sensorBehaviourProfiles[] = {
  // type                      usage           evalWin    collWin          evalType                   pushIntvl  chgOnlyIntvl  trigDelta  trigMode                    trigMin trigIntvl
  // ------------------------  -------------   ---------  --------------   -------------------------  ---------  ------------  ---------  --------------------------  ------- --------------------------
  // user dials
  { sensorType_set_point,      usage_user,     0,         0,               eval_none,                 3*Second,  60*Minute,    0,         tr_absolute,                0,      0 },
  { sensorType_temperature,    usage_user,     0,         0,               eval_none,                 3*Second,  60*Minute,    0,         tr_absolute,                0,      0 },
  { sensorType_percent,        usage_user,     0,         0,               eval_none,                 Second/5,  60*Minute,    0,         tr_absolute,                0,      0 },
  { sensorType_percent_speed,  usage_user,     0,         0,               eval_none,                 Second/5,  60*Minute,    0,         tr_absolute,                0,      0 },
  // indoor context
  { sensorType_temperature,    usage_room,     0,         0,               eval_none,                 5*Minute,  60*Minute,    0.5,       tr_absolute,                -100,   1*Second /* = "immediate" */ },
  { sensorType_humidity,       usage_room,     0,         0,               eval_none,                 30*Minute, 60*Minute,    2,         tr_absolute,                -1,     1*Second /* = "immediate" */ },
  { sensorType_illumination,   usage_room,     5*Minute,  10*Second,       eval_timeweighted_average, 5*Minute,  60*Minute,    0,         tr_absolute,                0,      0 },
  { sensorType_gas_CO2,        usage_room,     0,         0,               eval_none,                 5*Minute,  60*Minute,    0,         tr_absolute,                0,      0 },
  { sensorType_gas_CO,         usage_room,     0,         0,               eval_none,                 5*Minute,  60*Minute,    0,         tr_absolute,                0,      0 },
  // outdoor context
  { sensorType_temperature,    usage_outdoors, 0,         0,               eval_none,                 5*Minute,  60*Minute,    0.5,       tr_absolute,                -100,   1*Second /* = "immediate" */ },
  { sensorType_humidity,       usage_outdoors, 0,         0,               eval_none,                 30*Minute, 60*Minute,    2,         tr_absolute,                -1,     1*Second /* = "immediate" */ },
  { sensorType_illumination,   usage_outdoors, 10*Minute, 20*Second,       eval_timeweighted_average, 5*Minute,  60*Minute,    0,         tr_absolute,                0,      0 },
  { sensorType_air_pressure,   usage_outdoors, 0,         0,               eval_none,                 15*Minute, 60*Minute,    0,         tr_absolute,                0,      0 },
  { sensorType_wind_speed,     usage_outdoors, 10*Minute, 1*Minute,        eval_timeweighted_average, 10*Minute, 60*Minute,    0.1,       tr_relative,                0.1,    1*Minute },
  { sensorType_wind_direction, usage_outdoors, 10*Minute, 1*Minute,        eval_timeweighted_average, 10*Minute, 60*Minute,    20,        tr_absolute,                -1,     1*Minute },
  { sensorType_gust_speed,     usage_outdoors, 3*Second,  200*MilliSecond, eval_max,                  10*Minute, 60*Minute,    0.1,       tr_relative|tr_unipolar,    0.1,    1*Second /* = "immediate" */},
  // FIXME: rule says "accumulation", but as long as sensors deliver intensity in mm/h, it is in fact a window average over an hour
  { sensorType_precipitation,  usage_outdoors, 60*Minute, 2*Minute,        eval_timeweighted_average, 60*Minute, 60*Minute,    0,         tr_absolute,                0,      0 },

  // Plan44 additions
  // - some sensors are overactive in reporting their unchanged supply voltage, 4 times a day is enough!.
  //   Report changes with standard rate (30sec) though, because there are generic input devices using this sensor type for other than battery.
  { sensorType_supplyVoltage,  usage_undefined,0,         0,               eval_none,                 30*Second, 6*Hour,       0,         tr_absolute,                0,      0 },

  // terminator
  { sensorType_none,           usage_undefined,0,         0,               eval_none,                 0,         0,            0,         tr_absolute,                0,      0 },
};






void SensorBehaviour::setHardwareSensorConfig(VdcSensorType aType, VdcUsageHint aUsage, double aMin, double aMax, double aResolution, MLMicroSeconds aUpdateInterval, MLMicroSeconds aAliveSignInterval, MLMicroSeconds aDefaultChangesOnlyInterval)
{
  mSensorType = aType;
  mSensorUsage = aUsage;
  mMin = aMin;
  mMax = aMax;
  mResolution = aResolution;
  mUpdateInterval = aUpdateInterval;
  mAliveSignInterval = aAliveSignInterval;
  mMaxPushInterval = mAliveSignInterval==Never ? Never : DSS_SENSOR_MAX_PUSH_INTERVAL; // sensors without any update guarantee do not need to fake regular pushes
  armInvalidator();
  mProfileP = NULL;
  // default only, devices once created will have this as a persistent setting
  mChangesOnlyInterval = aDefaultChangesOnlyInterval; // default in case sensor profile does not override this
  // look for sensor behaviour profile
  const SensorBehaviourProfile *sbpP = sensorBehaviourProfiles;
  while (sbpP->type!=sensorType_none) {
    if (mSensorType==sbpP->type && mSensorUsage==sbpP->usage) {
      // sensor type/usage has a behaviour profile, use it
      LOG(LOG_INFO, "Activated sensor processing/filtering profile for '%s' (usage %d)", sensorTypeIds[mSensorType], mSensorUsage);
      mProfileP = sbpP;
      // get settings defaults
      if (mProfileP->pushIntvl>0) {
        mMinPushInterval = mProfileP->pushIntvl;
      }
      if (mProfileP->chgOnlyIntvl>0) {
        mChangesOnlyInterval = mProfileP->chgOnlyIntvl; // also report updates with no change after this time
      }
    }
    ++sbpP; // next
  }
}


string SensorBehaviour::getAutoId()
{
  return sensorTypeIds[mSensorType];
}


bool SensorBehaviour::isBridgeExclusive()
{
  #if ENABLE_JSONBRIDGEAPI
  return mDevice.isBridged() && mBridgeExclusive;
  #else
  return false;
  #endif
}


ValueUnit SensorBehaviour::getSensorUnit()
{
  if (mSensorType>=numVdcSensorTypes) return VALUE_UNIT(valueUnit_none, unitScaling_1);
  return sensorTypeUnits[mSensorType];
}


string SensorBehaviour::getSensorUnitText()
{
  return valueUnitName(getSensorUnit(), true);
}


string SensorBehaviour::getSensorRange()
{
  if (mMin==mMax) {
    return ""; // undefined range
  }
  int fracDigits = 2; // default if no resolution defined
  if (mResolution!=0) {
    fracDigits = (int)(-::log(mResolution)/::log(10)+0.99);
  }
  if (fracDigits<0) fracDigits=0;
  return string_format("%0.*f..%0.*f", fracDigits, mMin, fracDigits, mMax);
}


string SensorBehaviour::getStatusText()
{
  if (hasDefinedState()) {
    int fracDigits = 2; // default if no resolution defined
    if (mResolution!=0) {
      fracDigits = (int)(-::log(mResolution)/::log(10)+0.99);
    }
    if (fracDigits<0) fracDigits=0;
    return string_format("%0.*f %s", fracDigits, mCurrentValue, getSensorUnitText().c_str());
  }
  return inherited::getStatusText();
}


void SensorBehaviour::setSensorNameWithRange(const char *aName)
{
  setHardwareName(string_format("%s, %s %s", aName, getSensorRange().c_str(), getSensorUnitText().c_str()));
}


void SensorBehaviour::setFilter(WinEvalMode aEvalType, MLMicroSeconds aWindowTime, MLMicroSeconds aDataPointCollTime)
{
  if (aEvalType==eval_none) {
    mFilter.reset(); // remove, standard filter (if any) will be re-installed at next datapoint
  }
  else {
    mFilter = WindowEvaluatorPtr(new WindowEvaluator(aWindowTime, aDataPointCollTime, aEvalType));
  }
}


void SensorBehaviour::updateEngineeringValue(long aEngineeringValue, bool aPush, int32_t aContextId, const char *aContextMsg)
{
  double newCurrentValue = mMin+(aEngineeringValue*mResolution);
  updateSensorValue(newCurrentValue, -1, aPush, aContextId, aContextMsg);
}


void SensorBehaviour::armInvalidator()
{
  mInvalidatorTicket.cancel();
  if (mAliveSignInterval!=Never) {
    // this sensor can time out, schedule invalidation
    mInvalidatorTicket.executeOnce(boost::bind(&SensorBehaviour::invalidateSensorValue, this, true), mAliveSignInterval);
  }
}


void SensorBehaviour::updateSensorValue(double aValue, double aMinChange, bool aPush, int32_t aContextId, const char *aContextMsg)
{
  MLMicroSeconds now = MainLoop::now();
  bool changedValue = false;
  // always update age, even if value itself may not have changed
  mLastUpdate = now;
  armInvalidator();
  // update context
  if (mContextId!=aContextId) {
    mContextId = aContextId;
    changedValue = true;
  };
  if (mContextMsg!=nonNullCStr(aContextMsg)) {
    mContextMsg = nonNullCStr(aContextMsg);
    changedValue = true;
  };
  // update value
  if (aMinChange<0) aMinChange = mResolution/2;
  if (fabs(aValue - mCurrentValue) > aMinChange) changedValue = true;
  OLOG(changedValue ? LOG_NOTICE : LOG_INFO, "reports %s value = %0.3f %s", changedValue ? "NEW" : "same", aValue, getSensorUnitText().c_str());
  if (mContextId>=0 || !mContextMsg.empty()) {
    LOG(LOG_INFO, "- contextId=%d, contextMsg='%s'", mContextId, mContextMsg.c_str());
  }
  if (changedValue) {
    // check for filtering
    if (mFilter || (mProfileP && mProfileP->evalType!=eval_none)) {
      // process values through filter
      if (!mFilter) {
        // no filter exists yet, but profile needs a filter, create it now
        mFilter = WindowEvaluatorPtr(new WindowEvaluator(mProfileP->evalWin, mProfileP->collWin, mProfileP->evalType));
      }
      mFilter->addValue(aValue, now);
      double v = mFilter->evaluate();
      // re-evaluate changed flag after filtering
      if (fabs(v - mCurrentValue) > mResolution/2) changedValue = true;
      OLOG(changedValue ? LOG_NOTICE : LOG_INFO, "calculates %s filtered value = %0.3f %s", changedValue ? "NEW" : "same", v, getSensorUnitText().c_str());
      mCurrentValue = v;
    }
    else {
      // just assign new current value
      mCurrentValue = aValue;
    }
  }
  // possibly let localcontroller process it
  #if ENABLE_LOCALCONTROLLER
  // also let vdchost know for local dimmer dial handling etc., but only changes!
  // TODO: maybe more elegant solution for this
  if (!isBridgeExclusive() && changedValue) {
    mDevice.getVdcHost().checkForLocalSensorHandling(*this, mCurrentValue);
  }
  #endif
  // possibly push
  if (aPush) {
    pushSensor(false);
  }
  #if ENABLE_P44SCRIPT
  sendValueEvent();
  #endif
  // possibly log value
  #if ENABLE_RRDB
  logSensorValue(now, aValue, mCurrentValue, mLastPushedValue);
  #endif
}



bool SensorBehaviour::pushSensor(bool aAlways)
{
  MLMicroSeconds now = MainLoop::now();
  bool doPush = aAlways || mLastPush==Never; // always push if asked for or when there's no previous value
  if (!doPush) {
    // Note: when we get here, mLastPush and mLastPushedValue are always valid
    bool changed = mCurrentValue!=mLastPushedValue;
    if (now>mLastPush+mMinPushInterval) {
      // Minimal push interval is over -> push...
      // - if value has changed or
      // - if maxPushInterval has passed or
      // - if changesOnlyInterval has been exceeded
      // - if aliveSignInterval is defined and has been exceeded
      doPush =
        changed || // when changed (compared to last push)
        (mMaxPushInterval!=Never && now>mLastPush+mMaxPushInterval) || // when interval for max push interval has passed (similar, but not quite the same as changesOnlyInterval)
        now>mLastPush+mChangesOnlyInterval || // or when interval for not reporting unchanged values has passed
        (mAliveSignInterval!=Never && now>mLastUpdate+mAliveSignInterval); // or in case sensor declares a heartbeat interval
    }
    else if (mProfileP) {
      // Minimal push interval is NOT over, check extra trigger conditions
      if (mProfileP->trigDelta!=0 && now>mLastPush+mProfileP->trigIntvl) {
        // Trigger interval is over -> push if conditions are met
        double nowDelta = mCurrentValue-mLastPushedValue;
        double minDelta;
        if (mProfileP->trigMode&tr_relative) {
          // relative mode: delta is relative to last pushed value, trigMin indicates minimal absolute DELTA value
          minDelta = mLastPushedValue*mProfileP->trigDelta;
          doPush = fabs(nowDelta) > mProfileP->trigMin;
        }
        else {
          // absolute mode: delta is absolute, trigMin indicates minimal CURRENT value
          minDelta = mProfileP->trigDelta;
          doPush = mCurrentValue > mProfileP->trigMin;
        }
        if (doPush) {
          if (mProfileP->trigMode&tr_unipolar) {
            // unipolar check: deltas must have same sign
            doPush = (minDelta<0)==(nowDelta<0);
          }
          if (doPush) {
            doPush = fabs(nowDelta)>fabs(minDelta);
          }
        }
        if (doPush) {
          OLOG(LOG_INFO, "meets SOD conditions (%0.3f -> %0.3f %s) to push now", mLastPushedValue, mCurrentValue, getSensorUnitText().c_str());
        }
      }
    }
    if (!doPush && changed) {
      // we have a pending change, but rules don't allow to push now. Make sure final state gets pushed later
      OLOG(LOG_INFO, "- changes too quickly, cannot push update now, but final state will be pushed after minPushInterval");
      mUpdateTicket.executeOnceAt(boost::bind(&SensorBehaviour::reportFinalValue, this), mLastPush+mMinPushInterval);
    }
  }
  if (doPush) {
    // push the new value
    if (pushBehaviourState(true, true)) {
      mLastPush = now;
      mLastPushedValue = mCurrentValue;
      OLOG(LOG_NOTICE, "successfully pushed value = %0.3f %s", mLastPushedValue, getSensorUnitText().c_str());
      if (hasDefinedState() && mMaxPushInterval>0) {
        // schedule re-push of defined value
        mUpdateTicket.executeOnce(boost::bind(&SensorBehaviour::pushSensor, this, true), mMaxPushInterval);
      }
      return true;
    }
    else if (mDevice.isPublicDS() || mDevice.isBridged()) {
      OLOG(LOG_NOTICE, "could not be pushed");
    }
  }
  return false;
}


void SensorBehaviour::reportFinalValue()
{
  // push the current value (after awaiting minPushInterval)
  if (pushBehaviourState(true, true)) {
    OLOG(LOG_NOTICE, "now pushed finally settled value (%0.3f %s) after awaiting minPushInterval", mCurrentValue, getSensorUnitText().c_str());
    mLastPush = MainLoop::currentMainLoop().now();
    mLastPushedValue = mCurrentValue;
  }
}


void SensorBehaviour::invalidateSensorValue(bool aPush)
{
  if (mLastUpdate!=Never) {
    // currently valid -> invalidate
    mLastUpdate = Never;
    mCurrentValue = 0;
    mUpdateTicket.cancel();
    OLOG(LOG_NOTICE, "reports value no longer available");
    if (aPush) {
      // push invalidation (primitive clients not capable of NULL will at least see value==0)
      pushSensor(true);
    }
    // notify listeners
    #if ENABLE_P44SCRIPT
    sendValueEvent();
    #endif
  }
}


bool SensorBehaviour::hasCurrentValue(MLMicroSeconds aMaxAge)
{
  if (mLastUpdate==Never) return false; // no value at all
  MLMicroSeconds now = MainLoop::now();
  return now < mLastUpdate+aMaxAge;
}


bool SensorBehaviour::hasDefinedState()
{
  return mLastUpdate!=Never;
}


void SensorBehaviour::revalidateState()
{
  if (hasDefinedState()) {
    // re-arm invalidator
    armInvalidator();
  }
}


#if ENABLE_P44SCRIPT
// MARK: - value source implementation


string SensorBehaviour::getSourceId()
{
  return string_format("%s_S%s", mDevice.getDsUid().getString().c_str(), getId().c_str());
}


string SensorBehaviour::getSourceName()
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
  return mDevice.opStateLevel();
}

#endif // ENABLE_P44SCRIPT


#if ENABLE_RRDB
// MARK: - RRD sensor value logging



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
  if (!mLoggingReady && !mRRDBconfig.empty() && mRRDBfile.empty()) {
    // configured but not yet prepared to log
    // Note: not ready and rrdbfile NOT empty means that we could not start logging due to a problem (-> no op)
    // always need to parse config first to get update statement, maybe to (re-)create file
    ArgsVector cfgArgs;
    const char *p = mRRDBconfig.c_str();
    string arg;
    bool autoRaw = false;
    bool autoFiltered = false;
    bool autoPushed = false;
    bool autoRRA = false;
    bool autoStep = true;
    bool autoUpdate = true;
    long step = 1;
    string consolidationFunc = "AVERAGE";
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
        // forward "--step" and the argument (no autostep)
        cfgArgs.push_back(arg);
        cfgArgs.push_back(stp);
      }
      else if (arg=="auto") {
        // fully automatic sample of the current (possibly filtered) value, with standard RRAs
        autoFiltered = true;
        autoRRA = true;
      }
      else if (arg=="autods") {
        // fully automatic datasource, but no rra
        // - just record filtered
        autoFiltered = true;
      }
      else if (arg.substr(0,7)=="autods:") {
        // automatic datasources
        // Syntax autods:[R][F][P]  for raw, filtered, pushed
        autoRaw = arg.find("R",7)!=string::npos;
        autoFiltered = arg.find("F",7)!=string::npos;
        autoPushed = arg.find("P",7)!=string::npos;
      }
      else if (arg=="autorra") {
        // automatic "reasonable" RRAs
        autoRRA = true;
      }
      else if (arg.substr(0,8)=="autorra:") {
        // automatic "reasonable" RRAs with custom consodlidation function
        // Syntax: autorra:CF
        consolidationFunc = arg.substr(8);
        autoRRA = true;
      }
      else if (arg.substr(0,7)=="update:") {
        // update statement in case no autods is in use (any autods use will create a update string automatically)
        // Syntax update:<rrdb update string with %R, %F and %P placeholders>
        autoUpdate = false;
        mRRDBupdate = arg.substr(7); // rest of string is considered update string
      }
      else {
        // is regular RRD create argument, use as-is
        cfgArgs.push_back(arg);
      }
    }
    // in any case, we need the update statement
    mLoggingReady = true; // assume true
    if (autoUpdate) {
      mRRDBupdate = "N";
      if (autoRaw) mRRDBupdate += ":%R";
      if (autoFiltered) mRRDBupdate += ":%F";
      if (autoPushed) mRRDBupdate += ":%P";
      if (mRRDBupdate.size()<4) {
        OLOG(LOG_WARNING, "Cannot create RRD update string, missing 'auto..' or 'update' config");
        mRRDBupdate = "";
        mLoggingReady = false; // no point in trying
      }
    }
    // use or create rrd file
    mRRDBfile = Application::sharedApplication()->tempPath(mRRDBpath);
    if (mRRDBpath.empty() || mRRDBfile[mRRDBfile.size()-1]=='/') {
      // auto-generate filename
      pathstring_format_append(mRRDBfile, "Log_%s.rrd", getSourceId().c_str());
    }
    struct stat st;
    if (mLoggingReady && stat(mRRDBfile.c_str(), &st)<0 && errno==ENOENT) {
      // does not exist yet, create new
      mLoggingReady = false; // not any more, only if we succeed in creating new file it'll get set again
      string dsname = string_format("%.17s", sensorTypeIds[mSensorType]); // 19 chars max for RRD data sources, we need 2 for suffix
      // at this spoint, cfgArgs vector contains explicitly specified RRD args from config
      // - now create the actual
      ArgsVector args;
      // command and filename
      args.push_back("rrdcreate");
      args.push_back(mRRDBfile);
      // always start from now
      args.push_back("--start"); args.push_back("now"); // start now
      // possibly automatic --step option
      if (autoStep) {
        step = mUpdateInterval>15*Second ? mUpdateInterval/Second : 15;
        // use step from sensor's update interval (but not faster than once in 15 seconds)
        args.push_back("--step"); args.push_back(string_format("%ld", step)); // use sensor's native interval if known, 15sec otherwise
      }
      // possibly automatic datasources
      long heartbeat = mAliveSignInterval ? mAliveSignInterval/Second : 60*60*24; // without aliveSignInterval, allow a day of no updates
      if (autoRaw) {
        args.push_back(string_format("DS:%s_R:GAUGE:%ld:%s", dsname.c_str(), heartbeat, rrdminmax(mMin, mMax).c_str()));
      }
      if (autoFiltered) {
        args.push_back(string_format("DS:%s_F:GAUGE:%ld:%s", dsname.c_str(), heartbeat, rrdminmax(mMin, mMax).c_str()));
      }
      if (autoPushed) {
        args.push_back(string_format("DS:%s_P:GAUGE:%ld:%s", dsname.c_str(), heartbeat, rrdminmax(mMin, mMax).c_str()));
      }
      if (autoRRA) {
        // choose correct consolidation function
        if (consolidationFunc.empty()) {
          consolidationFunc = "AVERAGE";
          if (mProfileP) {
            if (mProfileP->evalType==eval_max) consolidationFunc = "MAX";
            else if (mProfileP->evalType==eval_min) consolidationFunc = "MIN";
          }
        }
        // now RRAs: RRA:consolidationFunc:xff:steps:rows
        args.push_back(string_format("RRA:%s:0.5:%ld:%ld", consolidationFunc.c_str(), 1l, 7*24*3600/step)); // 1:1 samples for a week (with autostep: unless updateInterval is <15 sec, see above)
        args.push_back(string_format("RRA:%s:0.5:%ld:%ld", consolidationFunc.c_str(), 3600/step, 30*24*3600*step/3600)); // hourly datapoints for 1 months (30 days)
        args.push_back(string_format("RRA:%s:0.5:%ld:%ld", consolidationFunc.c_str(), 24*3600/step, 2*365*24*3600*step/24/3600)); // daily for 2 years
      }
      // now add explicit config
      args.insert(args.end(), cfgArgs.begin(), cfgArgs.end());
      // create the rrd file from config
      if (OLOGENABLED(LOG_INFO)) {
        string a;
        for (int i=0; i<args.size(); ++i) a += args[i] + ' ';
        OLOG(LOG_INFO, "rrd: creating new RRD with args: %s", a.c_str());
      }
      int ret = rrd_call(rrd_create, args);
      if (ret==0) {
        OLOG(LOG_INFO, "rrd: successfully created new rrd file '%s'", mRRDBfile.c_str());
        mLoggingReady = true;
        mLastRRDBupdate = MainLoop::currentMainLoop().now(); // creation counts as update, make sure we don't immediately try to send first update afterwards
      }
      else {
        OLOG(LOG_ERR, "rrd: cannot create rrd file '%s': %s", mRRDBfile.c_str(), rrd_get_error());
      }
    }
    else {
      // file apparently already exists
      OLOG(LOG_INFO, "rrd: using existing file '%s'", mRRDBfile.c_str());
      mLoggingReady = true;
    }
  }
}


void SensorBehaviour::logSensorValue(MLMicroSeconds aTimeStamp, double aRawValue, double aProcessedValue, double aPushedValue)
{
  // make sure logging is prepared
  prepareLogging();
  // now actually log into file
  if (mLoggingReady && mLastRRDBupdate<aTimeStamp-10*Second) {
    mLastRRDBupdate = aTimeStamp;
    ArgsVector args;
    args.push_back("rrdupdate");
    args.push_back(mRRDBfile);
    string ud = mRRDBupdate;
    // substitute values for placeholders
    size_t i;
    i = ud.find("%T");
    if (i!=string::npos) ud.replace(i, 2, string_format("%lld", aTimeStamp/Second).c_str());
    // - raw (considered unknown when sensor is invalid)
    i = ud.find("%R");
    if (i!=string::npos) ud.replace(i, 2, rrdval(aRawValue, mLastUpdate!=Never).c_str());
    // - filtered (considered unknown when sensor is invalid)
    i = ud.find("%F");
    if (i!=string::npos) ud.replace(i, 2, rrdval(aProcessedValue, mLastUpdate!=Never).c_str());
    // - pushed (considered unknown when never pushed, or last state pushed must have been NULL because of invalidation)
    i = ud.find("%P");
    if (i!=string::npos) ud.replace(i, 2, rrdval(aPushedValue, mLastPush!=Never && mLastUpdate!=Never).c_str());
    args.push_back(ud);
    int ret = rrd_call(rrd_update, args);
    if (ret!=0) {
      OLOG(LOG_WARNING, "rrd: could not update rrd data for file '%s': %s", mRRDBfile.c_str(), rrd_get_error());
      mLoggingReady = false; // back to not initialized, might work again after recreating file
    }
  }
}

#endif // ENABLE_RRDB



// MARK: - persistence implementation


// SQLIte3 table name to store these parameters to
const char *SensorBehaviour::tableName()
{
  return "SensorSettings";
}


// data field definitions


#define RRDB_FIELDS 0
#define NON_RED_FP_FIELDS 0
#if ENABLE_RRDB
  #undef RRDB_FIELDS
  #define RRDB_FIELDS 2
#endif
#if !REDUCED_FOOTPRINT
  #undef NON_RED_FP_FIELDS
  #define NON_RED_FP_FIELDS 2
#endif

static const size_t numFields = 3+RRDB_FIELDS+NON_RED_FP_FIELDS;

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
    #if !REDUCED_FOOTPRINT
    { "sensorFunc", SQLITE_INTEGER },
    { "sensorChannel", SQLITE_INTEGER },
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
  aRow->getCastedIfNotNull<DsGroup, int>(aIndex++, mSensorGroup);
  aRow->getCastedIfNotNull<MLMicroSeconds, long long int>(aIndex++, mMinPushInterval);
  aRow->getCastedIfNotNull<MLMicroSeconds, long long int>(aIndex++, mChangesOnlyInterval);
  #if ENABLE_RRDB
  aRow->getIfNotNull(aIndex++, mRRDBconfig);
  aRow->getIfNotNull(aIndex++, mRRDBpath);
  // make sure logging is ready (if enabled at all)
  prepareLogging();
  #endif
  #if !REDUCED_FOOTPRINT
  aRow->getCastedIfNotNull<VdcSensorFunc, int>(aIndex++, mSensorFunc);
  aRow->getCastedIfNotNull<DsChannelType, int>(aIndex++, mSensorChannel);
  #endif
}


// bind values to passed statement
void SensorBehaviour::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, mSensorGroup);
  aStatement.bind(aIndex++, (long long int)mMinPushInterval);
  aStatement.bind(aIndex++, (long long int)mChangesOnlyInterval);
  #if ENABLE_RRDB
  aStatement.bind(aIndex++, mRRDBconfig.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, mRRDBpath.c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  #endif
  #if !REDUCED_FOOTPRINT
  aStatement.bind(aIndex++, mSensorFunc);
  aStatement.bind(aIndex++, mSensorChannel);
  #endif
}



// MARK: - property access

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
  maxPushInterval_key,
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
    { "maxPushInterval", apivalue_double, maxPushInterval_key+descriptions_key_offset, OKEY(sensor_key) },
    #if ENABLE_RRDB
    { "x-p44-rrdFile", apivalue_string, rrdbFile_key+descriptions_key_offset, OKEY(sensor_key) },
    #endif
  };
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}


// settings properties

enum {
  group_key,
  #if !REDUCED_FOOTPRINT
  function_key,
  channel_key,
  #endif
  minPushInterval_key,
  changesOnlyInterval_key,
  #if ENABLE_JSONBRIDGEAPI
  bridgeExclusive_key,
  #endif
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
    #if !REDUCED_FOOTPRINT
    { "function", apivalue_uint64, function_key+settings_key_offset, OKEY(sensor_key) },
    { "channel", apivalue_uint64, channel_key+settings_key_offset, OKEY(sensor_key) },
    #endif
    { "minPushInterval", apivalue_double, minPushInterval_key+settings_key_offset, OKEY(sensor_key) },
    { "changesOnlyInterval", apivalue_double, changesOnlyInterval_key+settings_key_offset, OKEY(sensor_key) },
    #if ENABLE_JSONBRIDGEAPI
    { "x-p44-bridgeExclusive", apivalue_bool, bridgeExclusive_key+settings_key_offset, OKEY(sensor_key) },
    #endif
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
          aPropValue->setUint16Value(mSensorType);
          return true;
        case sensorUsage_key+descriptions_key_offset:
          aPropValue->setUint16Value(mSensorUsage);
          return true;
        case siunit_key+descriptions_key_offset:
          aPropValue->setStringValue(valueUnitName(getSensorUnit(), false));
          return true;
        case unitsymbol_key+descriptions_key_offset:
          aPropValue->setStringValue(valueUnitName(getSensorUnit(), true));
          return true;
        case min_key+descriptions_key_offset:
          if (mMin==mMax) return false;
          aPropValue->setDoubleValue(mMin);
          return true;
        case max_key+descriptions_key_offset:
          if (mMin==mMax) return false;
          aPropValue->setDoubleValue(mMax);
          return true;
        case resolution_key+descriptions_key_offset:
          if (mResolution==0) return false;
          aPropValue->setDoubleValue(mResolution);
          return true;
        case updateInterval_key+descriptions_key_offset:
          aPropValue->setDoubleValue((double)mUpdateInterval/Second);
          return true;
        case aliveSignInterval_key+descriptions_key_offset:
          aPropValue->setDoubleValue((double)mAliveSignInterval/Second);
          return true;
        case maxPushInterval_key+descriptions_key_offset:
          aPropValue->setDoubleValue((double)mMaxPushInterval/Second);
          return true;
        #if ENABLE_RRDB
        case rrdbFile_key+descriptions_key_offset:
          if (mRRDBfile.empty()) return false; // only visible if there actually IS a file
          aPropValue->setStringValue(mRRDBfile);
          return true;
        #endif
        // Settings properties
        case group_key+settings_key_offset:
          aPropValue->setUint16Value(mSensorGroup);
          return true;
        #if !REDUCED_FOOTPRINT
        case function_key+settings_key_offset:
          aPropValue->setUint16Value(mSensorFunc);
          return true;
        case channel_key+settings_key_offset:
          aPropValue->setUint16Value(mSensorChannel);
          return true;
        #endif
        case minPushInterval_key+settings_key_offset:
          aPropValue->setDoubleValue((double)mMinPushInterval/Second);
          return true;
        case changesOnlyInterval_key+settings_key_offset:
          aPropValue->setDoubleValue((double)mChangesOnlyInterval/Second);
          return true;
        #if ENABLE_JSONBRIDGEAPI
        case bridgeExclusive_key+settings_key_offset:
          if (!mDevice.isBridged()) return false; // hide when not bridged
          aPropValue->setBoolValue(mBridgeExclusive);
          return true;
        #endif
        #if ENABLE_RRDB
        case rrdbPath_key+settings_key_offset:
          aPropValue->setStringValue(mRRDBpath);
          return true;
        case rrdbConfig_key+settings_key_offset:
          aPropValue->setStringValue(mRRDBconfig);
          return true;
        #endif
        // States properties
        case value_key+states_key_offset:
          // value
          if (!hasDefinedState())
            aPropValue->setNull();
          else
            aPropValue->setDoubleValue(mCurrentValue);
          return true;
        case age_key+states_key_offset:
          // age
          if (!hasDefinedState())
            aPropValue->setNull();
          else
            aPropValue->setDoubleValue((double)(MainLoop::now()-mLastUpdate)/Second);
          return true;
        case contextid_key+states_key_offset:
          // context ID
          if (!hasDefinedState() || mContextId<0) return false; // not available
          aPropValue->setUint32Value((int32_t)mContextId);
          return true;
        case contextmsg_key+states_key_offset:
          // context message
          if (!hasDefinedState() || mContextMsg.empty()) return false; // not available
          aPropValue->setStringValue(mContextMsg);
          return true;
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        // Settings properties
        case group_key+settings_key_offset:
          setPVar(mSensorGroup, (DsGroup)aPropValue->int32Value());
          return true;
        #if !REDUCED_FOOTPRINT
        case function_key+settings_key_offset:
          setPVar(mSensorFunc, (VdcSensorFunc)aPropValue->int32Value());
          return true;
        case channel_key+settings_key_offset:
          setPVar(mSensorChannel, (DsChannelType)aPropValue->int32Value());
          return true;
        #endif
        case minPushInterval_key+settings_key_offset:
          setPVar(mMinPushInterval, (MLMicroSeconds)(aPropValue->doubleValue()*Second));
          return true;
        case changesOnlyInterval_key+settings_key_offset:
          setPVar(mChangesOnlyInterval, (MLMicroSeconds)(aPropValue->doubleValue()*Second));
          return true;
        #if ENABLE_JSONBRIDGEAPI
        case bridgeExclusive_key+settings_key_offset:
          // volatile, does not make settings dirty
          mBridgeExclusive = aPropValue->boolValue();
          return true;
        #endif
        #if ENABLE_RRDB
        case rrdbPath_key+settings_key_offset:
          if (setPVar(mRRDBpath, aPropValue->stringValue())) {
            mRRDBfile.clear(); // force re-setup of rrdb logging
            prepareLogging();
          }
          return true;
        case rrdbConfig_key+settings_key_offset:
          if (setPVar(mRRDBconfig, aPropValue->stringValue())) {
            mRRDBfile.clear(); // force re-setup of rrdb logging
            prepareLogging();
          }
          return true;
        #endif
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}



// MARK: - description/shortDesc


string SensorBehaviour::description()
{
  string s = string_format("%s behaviour", shortDesc().c_str());
  string_format_append(s, "\n- sensor type: %d, min: %0.1f, max: %0.1f, resolution: %0.3f, interval: %lld mS", mSensorType, mMin, mMax, mResolution, mUpdateInterval/MilliSecond);
  string_format_append(s, "\n- minimal interval between pushes: %lld mS, aliveSignInterval: %lld mS", mMinPushInterval/MilliSecond, mAliveSignInterval/MilliSecond);
  s.append(inherited::description());
  return s;
}

