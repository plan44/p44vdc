//
//  Copyright (c) 2016-2022 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "valuesource.hpp"
#include "vdchost.hpp"

using namespace p44;

// MARK: - ValueSource

ValueSource::ValueSource()
{
}


void ValueSource::sendValueEvent()
{
  if (!hasSinks()) return; // optimisation
  sendEvent(new ValueSourceObj(this));
}

// MARK: - ValueSourceMapper

ValueSourceMapper::ValueSourceMapper()
{
}


ValueSourceMapper::~ValueSourceMapper()
{
  forgetMappings();
}


void ValueSourceMapper::forgetMappings()
{
  valueMap.clear();
}


ValueSource* ValueSourceMapper::valueSourceByAlias(const string aAlias) const
{
  ValueSourcesMap::const_iterator pos = valueMap.find(aAlias);
  if (pos==valueMap.end()) {
    return NULL;
  }
  return pos->second;
}


bool ValueSourceMapper::parseMappingDefs(const string &aValueDefs, string *aMigratedValueDefsP)
{
  LOG(LOG_INFO, "Parsing alias to value source mappings");
  forgetMappings(); // forget previous mappings
  string newValueDefs; // re-created value defs using sensor ids rather than indices, for migration
  // syntax:
  //  <valuealias>:<valuesourceid> [, <valuealias>:valuesourceid> ...]
  bool foundall = true;
  size_t i = 0;
  while(i<aValueDefs.size()) {
    size_t e = aValueDefs.find(":", i);
    if (e!=string::npos) {
      string valuealias = aValueDefs.substr(i,e-i);
      i = e+1;
      size_t e2 = aValueDefs.find_first_of(", \t\n\r", i);
      if (e2==string::npos) e2 = aValueDefs.size();
      string valuesourceid = aValueDefs.substr(i,e2-i);
      // search source
      ValueSource *vs = VdcHost::sharedVdcHost()->getValueSourceById(valuesourceid);
      if (vs) {
        // value source exists
        // - add source to my map
        valueMap[valuealias] = vs;
        LOG(LOG_INFO, "- alias '%s' connected to source '%s'", valuealias.c_str(), vs->getSourceName().c_str());
        string_format_append(newValueDefs, "%s:%s", valuealias.c_str(), vs->getSourceId().c_str());
      }
      else {
        LOG(LOG_WARNING, "Value source id '%s' not found -> alias '%s' currently undefined", valuesourceid.c_str(), valuealias.c_str());
        string_format_append(newValueDefs, "%s:%s", valuealias.c_str(), valuesourceid.c_str());
        foundall = false;
      }
      // skip delimiters
      i = aValueDefs.find_first_not_of(", \t\n\r", e2);
      if (i==string::npos) i = aValueDefs.size();
      newValueDefs += aValueDefs.substr(e2,i-e2);
    }
    else {
      LOG(LOG_ERR, "missing ':' in mapping definition");
      break;
    }
  }
  if (aMigratedValueDefsP) {
    aMigratedValueDefsP->clear();
    if (newValueDefs!=aValueDefs) {
      *aMigratedValueDefsP = newValueDefs;
    }
  }
  return foundall;
}


ValueSourceObj::ValueSourceObj(ValueSource* aValueSourceP) :
  inherited(aValueSourceP->getSourceValue()),
  mLastUpdate(aValueSourceP->getSourceLastUpdate()),
  mOpLevel(aValueSourceP->getSourceOpLevel())
{
  mEventSource = dynamic_cast<EventSource*>(aValueSourceP);
}


string ValueSourceObj::getAnnotation() const
{
  return mLastUpdate==Never ? "unknown hardware state" : "value source";
}


TypeInfo ValueSourceObj::getTypeInfo() const
{
  return (mLastUpdate==Never ? null : numeric)|freezable|keeporiginal;
}


EventSource *ValueSourceObj::eventSource() const
{
  return mEventSource;
}

const ScriptObjPtr ValueSourceObj::memberByName(const string aName, TypeInfo aMemberAccessFlags)
{
  ScriptObjPtr val;
  if (uequals(aName, "age")) {
    if (mLastUpdate!=Never) val = new NumericValue((double)(MainLoop::now()-mLastUpdate)/Second);
    else val = new AnnotatedNullValue("unseen");
  }
  else if (uequals(aName, "valid")) {
    val = new NumericValue(mLastUpdate!=Never);
  }
  else if (uequals(aName, "oplevel")) {
    if (mOpLevel>=0) val = new NumericValue(mOpLevel);
    else val = new AnnotatedNullValue("unknown");
  }
  return val;
}


ScriptObjPtr ValueSourceMapper::memberByNameFrom(ScriptObjPtr aThisObj, const string aName, TypeInfo aTypeRequirements) const
{
  ScriptObjPtr vsMember;
  ValueSource* vs = valueSourceByAlias(aName);
  if (vs) {
    vsMember = new ValueSourceObj(vs);
  }
  return vsMember;
}


bool ValueSourceMapper::getMappedSourcesInfo(ApiValuePtr aInfoObject)
{
  if (!aInfoObject || !aInfoObject->isType(apivalue_object)) return false;
  for (ValueSourcesMap::iterator pos = valueMap.begin(); pos!=valueMap.end(); ++pos) {
    ApiValuePtr val = aInfoObject->newObject();
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
    aInfoObject->add(pos->first,val); // variable name
    LOG(LOG_INFO, "- '%s' ('%s') = %f", pos->first.c_str(), pos->second->getSourceName().c_str(), pos->second->getSourceValue());
  }
  return true;
}


string ValueSourceMapper::shortDesc() const
{
  if (valueMap.empty()) return "<no values>";
  string s;
  const char *sep = "";
  for (ValueSourcesMap::const_iterator pos = valueMap.begin(); pos!=valueMap.end(); ++pos) {
    s += sep;
    sep = ", ";
    string_format_append(s, "%s=", pos->first.c_str());
    MLMicroSeconds lastupdate = pos->second->getSourceLastUpdate();
    if (lastupdate==Never) {
      s += "UNDEFINED";
    }
    else {
      string_format_append(s, "%.3f", pos->second->getSourceValue());
    }
  }
  return s;
}
