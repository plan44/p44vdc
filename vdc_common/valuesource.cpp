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

#include "valuesource.hpp"
#include "vdchost.hpp"

using namespace p44;

// MARK: - ValueSource

ValueSource::ValueSource()
{
}


ValueSource::~ValueSource()
{
  // inform all of the listeners that the value is gone, unless app is about to terminate
  if (Application::isRunning()) {
    notifyListeners(valueevent_removed);
  }
  listeners.clear();
}


void ValueSource::addSourceListener(ValueListenerCB aCallback, void *aListener)
{
  listeners.insert(make_pair(aListener, aCallback));
}


void ValueSource::removeSourceListener(void *aListener)
{
  listeners.erase(aListener);
}



void ValueSource::notifyListeners(ValueListenerEvent aEvent)
{
  if (aEvent==valueevent_removed) {
    // neeed to operate on a copy of the map because removal could cause callbacks to add/remove
    ListenerMap tempMap = listeners;
    for (ListenerMap::iterator pos=tempMap.begin(); pos!=tempMap.end(); ++pos) {
      ValueListenerCB cb = pos->second;
      cb(*this, aEvent);
    }
  }
  else {
    // optimisation - no need to copy the map
    for (ListenerMap::iterator pos=listeners.begin(); pos!=listeners.end(); ++pos) {
      ValueListenerCB cb = pos->second;
      cb(*this, aEvent);
    }
  }
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
  for (ValueSourcesMap::iterator pos = valueMap.begin(); pos!=valueMap.end(); ++pos) {
    pos->second->removeSourceListener(this);
  }
  valueMap.clear();
}


ValueSource* ValueSourceMapper::valueSourceByAlias(const string aAlias)
{
  ValueSourcesMap::iterator pos = valueMap.find(aAlias);
  if (pos==valueMap.end()) {
    return NULL;
  }
  return pos->second;
}


bool ValueSourceMapper::parseMappingDefs(const string &aValueDefs, ValueListenerCB aCallback, string *aMigratedValueDefsP)
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
        // - add listener
        vs->addSourceListener(aCallback, this);
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


bool ValueSourceMapper::valueLookup(ExpressionValue &aValue, const string aVarSpec)
{
  // value specfications can be simple valuesource alias names, or alias names with sub-field specifications:
  // alias               returns the value of the valuesource itself
  // alias.valid         returns 1 if valuesource has a valid value, 0 otherwise
  // alias.oplevel       returns the operation level of the valuesource (0..100%)
  // alias.age           returns the age of the valuesource's value in seconds
  string subfield;
  string name;
  size_t i = aVarSpec.find('.');
  if (i!=string::npos) {
    subfield = aVarSpec.substr(i+1);
    name = aVarSpec.substr(0,i);
  }
  else {
    name = aVarSpec;
  }
  ValueSource* vs = valueSourceByAlias(name);
  if (vs==NULL) {
    // not found
    return false;
  }
  // value found
  if (subfield.empty()) {
    // value itself is requested
    if (vs->getSourceLastUpdate()!=Never) {
      aValue = ExpressionValue(vs->getSourceValue());
      return true;
    }
  }
  else if (subfield=="valid") {
    aValue = ExpressionValue(vs->getSourceLastUpdate()!=Never ? 1 : 0);
    return true;
  }
  else if (subfield=="oplevel") {
    int lvl = vs->getSourceOpLevel();
    if (lvl>=0) {
      aValue = ExpressionValue(lvl);
      return true;
    }
    // otherwise: no known value
  }
  else if (subfield=="age") {
    if (vs->getSourceLastUpdate()!=Never) {
      aValue = ExpressionValue(((double)(MainLoop::now()-vs->getSourceLastUpdate()))/Second);
      return true;
    }
  }
  else {
    aValue = ExpressionValue::errValue(ExpressionError::NotFound, "Unknown subfield '%s' for alias '%s'", subfield.c_str(), name.c_str());
    return true;
  }
  // no value (yet)
  aValue = ExpressionValue::errValue(ExpressionError::Null, "'%s' has no known value yet", aVarSpec.c_str());
  return true;
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
