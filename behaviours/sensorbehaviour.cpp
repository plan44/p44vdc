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

using namespace p44;

SensorBehaviour::SensorBehaviour(Device &aDevice) :
  inherited(aDevice),
  // persistent settings
  sensorGroup(group_black_variable), // default to joker
  minPushInterval(2*Second), // do not push more often than every 2 seconds
  changesOnlyInterval(0), // report every sensor update (even if value unchanged)
  // state
  lastUpdate(Never),
  lastPush(Never),
  currentValue(0),
  contextId(-1)
{
  // set dummy default hardware default configuration
  setHardwareSensorConfig(sensorType_none, usage_undefined, 0, 100, 1, 15*Second, 20*Minute);
}


void SensorBehaviour::setHardwareSensorConfig(VdcSensorType aType, VdcUsageHint aUsage, double aMin, double aMax, double aResolution, MLMicroSeconds aUpdateInterval, MLMicroSeconds aAliveSignInterval, MLMicroSeconds aDefaultChangesOnlyInterval)
{
  sensorType = aType;
  sensorUsage = aUsage;
  min = aMin;
  max = aMax;
  resolution = aResolution;
  updateInterval = aUpdateInterval;
  aliveSignInterval = aAliveSignInterval;
  // default only, devices once created will have this as a persistent setting
  changesOnlyInterval = aDefaultChangesOnlyInterval;
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
  VALUE_UNIT(valueUnit_voltampere, unitScaling_1), ///< Electric Consumption in VA
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
  VALUE_UNIT(valueUnit_literpersecond, unitScaling_1), ///< Water flow rate in liters/second
  VALUE_UNIT(valueUnit_meter, unitScaling_1), ///< Length in meters
  VALUE_UNIT(valueUnit_gram, unitScaling_1), ///< mass in grams
  VALUE_UNIT(valueUnit_second, unitScaling_1), ///< duration in seconds
};


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
  int fracDigits = (int)(-log(resolution)/log(10)+0.99);
  if (fracDigits<0) fracDigits=0;
  return string_format("%0.*f..%0.*f", fracDigits, min, fracDigits, max);
}


string SensorBehaviour::getStatusText()
{
  if (hasDefinedState()) {
    int fracDigits = (int)(-log(resolution)/log(10)+0.99);
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


void SensorBehaviour::updateSensorValue(double aValue, double aMinChange, bool aPush, int32_t aContextId, const char *aContextMsg)
{
  MLMicroSeconds now = MainLoop::now();
  bool changedValue = false;
  // always update age, even if value itself may not have changed
  lastUpdate = now;
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
  BLOG(changedValue ? LOG_NOTICE : LOG_INFO, "Sensor[%zu] '%s' reports %s value = %0.3f %s", index, hardwareName.c_str(), changedValue ? "NEW" : "same", aValue, getSensorUnitText().c_str());
  if (contextId>=0 || !contextMsg.empty()) {
    LOG(LOG_INFO, "- contextId=%d, contextMsg='%s'", contextId, contextMsg.c_str());
  }
  if (changedValue) {
    currentValue = aValue;
  }
  // possibly push
  if (aPush) {
    pushSensor(changedValue);
  }
  // notify listeners
  notifyListeners(changedValue ? valueevent_changed : valueevent_confirmed);
}



bool SensorBehaviour::pushSensor(bool aChanged, bool aAlways)
{
  MLMicroSeconds now = MainLoop::now();
  if (aChanged || now>lastPush+changesOnlyInterval) {
    // changed value or last push with same value long enough ago
    if (aAlways || lastPush==Never || now>lastPush+minPushInterval) {
      // push the new value
      if (pushBehaviourState()) {
        lastPush = now;
        return true;
      }
      else {
        BLOG(LOG_NOTICE, "Sensor[%zu] '%s' could not be pushed", index, hardwareName.c_str());
      }
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
    if (aPush) {
      // push invalidation (primitive clients not capable of NULL will at least see value==0)
      pushSensor(true, true);
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



// MARK: ===== persistence implementation


// SQLIte3 table name to store these parameters to
const char *SensorBehaviour::tableName()
{
  return "SensorSettings";
}


// data field definitions

static const size_t numFields = 3;

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
}


// bind values to passed statement
void SensorBehaviour::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, sensorGroup);
  aStatement.bind(aIndex++, (long long int)minPushInterval);
  aStatement.bind(aIndex++, (long long int)changesOnlyInterval);
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
  };
  return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
}


// settings properties

enum {
  group_key,
  minPushInterval_key,
  changesOnlyInterval_key,
  numSettingsProperties
};


int SensorBehaviour::numSettingsProps() { return numSettingsProperties; }
const PropertyDescriptorPtr SensorBehaviour::getSettingsDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numSettingsProperties] = {
    { "group", apivalue_uint64, group_key+settings_key_offset, OKEY(sensor_key) },
    { "minPushInterval", apivalue_double, minPushInterval_key+settings_key_offset, OKEY(sensor_key) },
    { "changesOnlyInterval", apivalue_double, changesOnlyInterval_key+settings_key_offset, OKEY(sensor_key) },
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
    { "contextid", apivalue_uint64, contextid_key+states_key_offset, OKEY(sensor_key) },
    { "contextmsg", apivalue_string, contextmsg_key+states_key_offset, OKEY(sensor_key) },
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
          aPropValue->setDoubleValue(min);
          return true;
        case max_key+descriptions_key_offset:
          aPropValue->setDoubleValue(max);
          return true;
        case resolution_key+descriptions_key_offset:
          aPropValue->setDoubleValue(resolution);
          return true;
        case updateInterval_key+descriptions_key_offset:
          aPropValue->setDoubleValue((double)updateInterval/Second);
          return true;
        case aliveSignInterval_key+descriptions_key_offset:
          aPropValue->setDoubleValue((double)aliveSignInterval/Second);
          return true;
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
  string_format_append(s, "\n- minimal interval between pushes: %lld mS", minPushInterval/MilliSecond);
  s.append(inherited::description());
  return s;
}

