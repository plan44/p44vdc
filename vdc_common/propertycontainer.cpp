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
#define FOCUSLOGLEVEL 7

#include "propertycontainer.hpp"

// needed to implement reading from CSV
#include "jsonvdcapi.hpp"

using namespace p44;


PropertyDescriptorPtr PropertyContainer::getContainerRootDescriptor(int aApiVersion)
{
  // default to standard root descriptor
  return PropertyDescriptorPtr(new RootPropertyDescriptor(aApiVersion));
}


void PropertyContainer::accessProperty(PropertyAccessMode aMode, ApiValuePtr aQueryObject, int aDomain, int aApiVersion, PropertyAccessCB aAccessCompleteCB)
{
  assert(aAccessCompleteCB);
  // create a list for possibly needed preparations
  PropertyPrepListPtr prepList = PropertyPrepListPtr(new PropertyPrepList);
  // create root descriptor
  PropertyDescriptorPtr parentDescriptor = getContainerRootDescriptor(aApiVersion);
  // first attempt to access
  // - create result object of same API type as query
  ApiValuePtr result = aQueryObject->newNull(); // write might return a object containing ids of inserted container elements
  if (aMode==access_read) result->setType(apivalue_object); // read always needs a structured object
  ErrorPtr err = accessPropertyInternal(aMode, aQueryObject, result, aDomain, parentDescriptor, prepList, false);
  if (Error::notOK(err) || prepList->empty()) {
    // error or no need for preparation, immediately call back
    aAccessCompleteCB(result, err);
    // done
    return;
  }
  // preplist has at least one item here
  prepareNext(prepList, aMode, aQueryObject, aDomain, aAccessCompleteCB, result);
}


void PropertyContainer::prepareNext(PropertyPrepListPtr aPrepList, PropertyAccessMode aMode, ApiValuePtr aQueryObject, int aDomain, PropertyAccessCB aAccessCompleteCB, ApiValuePtr aFinalResult)
{
  if (aPrepList->empty()) {
    // all done
    FOCUSLOG("- end of preplist: reporting final result = %s", aFinalResult->description().c_str());
    aAccessCompleteCB(aFinalResult, ErrorPtr());
    return;
  }
  // process next item in list
  PropertyPrep prep = aPrepList->front();
  if (prep.descriptor->isRootOfObject()) {
    // root objects are "prepared" by calling their (likely overridden) accessProperty recursively
    FOCUSLOG("- recursive accessProperty() with preliminary overall result = %s", aFinalResult->description().c_str());
    prep.target->accessProperty(aMode, prep.subquery, aDomain, prep.descriptor->getApiVersion(),
      boost::bind(&PropertyContainer::subqueryDone, this, aPrepList, aMode, aQueryObject, aDomain, aAccessCompleteCB, aFinalResult, _1, _2)
    );
  }
  else {
    // non-root, prepare
    prep.target->prepareAccess(aMode, prep,
      boost::bind(&PropertyContainer::prepareDone, this, aPrepList, aMode, aQueryObject, aDomain, aAccessCompleteCB, aFinalResult, _1)
    );
  }
}


void PropertyContainer::subqueryDone(PropertyPrepListPtr aPrepList, PropertyAccessMode aMode, ApiValuePtr aQueryObject, int aDomain, PropertyAccessCB aAccessCompleteCB, ApiValuePtr aFinalResult, ApiValuePtr aResult, ErrorPtr aError)
{
  // process async subquery result
  if (Error::notOK(aError)) {
    // error in subquery: report immediately
    aPrepList->clear();
    aAccessCompleteCB(nullptr, aError);
    return;
  }
  PropertyPrep prepped = aPrepList->front();
  // save subquery result if any
  if (aResult) {
    FOCUSLOG("- subquery result = %s", aResult->description().c_str());
    FOCUSLOG("- inserting as '%s' in object: %s", prepped.insertAs.c_str(), prepped.insertIn->description().c_str());
    prepped.insertIn->add(prepped.insertAs.c_str(), aResult);
    FOCUSLOG("- preliminary overall result is now = %s", aFinalResult->description().c_str());
  }
  // done with the prep entry
  aPrepList->pop_front();
  // check next
  prepareNext(aPrepList, aMode, aQueryObject, aDomain, aAccessCompleteCB, aFinalResult);
}


void PropertyContainer::prepareDone(PropertyPrepListPtr aPrepList, PropertyAccessMode aMode, ApiValuePtr aQueryObject, int aDomain, PropertyAccessCB aAccessCompleteCB, ApiValuePtr aFinalResult, ErrorPtr aError)
{
  // prepared
  if (Error::notOK(aError)) {
    // error preparing: report immediately
    aPrepList->clear();
    aAccessCompleteCB(nullptr, aError);
    return;
  }
  int apiVersion = aPrepList->front().descriptor->getApiVersion();
  // done with the prep entry
  aPrepList->pop_front();
  // check if this is was last preparation
  if (aPrepList->empty() || aPrepList->front().descriptor->isRootOfObject()) {
    // no more items or next is a async root subquery:
    FOCUSLOG("- all non-root properties prepared, re-running query now, remaining preparations (root re-runs) = %zu", aPrepList->size());
    // - reset final result
    if (aMode==access_read) aFinalResult = aQueryObject->newObject();
    // re-access with all preparation requests fulfilled (but not yet possible async recursions into accessProperty()
    ErrorPtr err = accessPropertyInternal(aMode, aQueryObject, aFinalResult, aDomain, getContainerRootDescriptor(apiVersion), aPrepList, true);
  }
  // check next
  prepareNext(aPrepList, aMode, aQueryObject, aDomain, aAccessCompleteCB, aFinalResult);
}


void PropertyContainer::prepareAccess(PropertyAccessMode aMode, PropertyPrep& aPrepInfo, StatusCB aPreparedCB)
{
  // nothing to prepare in base class
  if (aPreparedCB) aPreparedCB(ErrorPtr());
}


void PropertyContainer::finishAccess(PropertyAccessMode aMode, PropertyDescriptorPtr aPropertyDescriptor)
{
  // nothing to un-prepare/finalize in base class
}


ErrorPtr PropertyContainer::accessPropertyInternal(PropertyAccessMode aMode, ApiValuePtr aQueryObject, ApiValuePtr aResultObject, int aDomain, PropertyDescriptorPtr aParentDescriptor, PropertyPrepListPtr aPreparationList, bool aPrepared)
{
  ErrorPtr err;
  assert(aParentDescriptor);
  FOCUSLOG("\naccessProperty: entered %swith query = %s", aPrepared ? "PREPARED " : "", aQueryObject->description().c_str());
  FOCUSLOG("- parentDescriptor '%s' (%s, %s), fieldKey=%zu, objectKey=%ld",
    aParentDescriptor->name(),
    aParentDescriptor->isStructured() ? "structured" : "scalar",
    aParentDescriptor->isRootOfObject() ? "rootOfObject" : "sublevel",
    aParentDescriptor->fieldKey(),
    aParentDescriptor->objectKey()
  );
  // for reading, NULL query is like query { "":NULL }
  if (aQueryObject->isNull() && aMode==access_read) {
    aQueryObject->setType(apivalue_object);
    aQueryObject->add("", aQueryObject->newValue(apivalue_null));
  }
  // aApiObject must be of type apivalue_object
  if (!aQueryObject->isType(apivalue_object))
    return Error::err<VdcApiError>(415, "Query or Value written must be object");
  // result object
  if (!aResultObject) {
    return Error::err<VdcApiError>(415, "accessing property must provide result object");
  }
  // Iterate trough elements of query object
  aQueryObject->resetKeyIteration();
  string queryName;
  ApiValuePtr queryValue;
  string errorMsg;
  while (aQueryObject->nextKeyValue(queryName, queryValue)) {
    FOCUSLOG("- starting to process query element named '%s' : %s", queryName.c_str(), queryValue->description().c_str());
    if (aMode==access_read && queryName=="#") {
      // asking for number of elements at this level -> generate and return int value
      queryValue = queryValue->newValue(apivalue_int64); // integer
      queryValue->setInt32Value(numProps(aDomain, aParentDescriptor));
      aResultObject->add(queryName, queryValue);
    }
    else {
      // accessing an element or series of elements at this level
      bool wildcard = isMatchAll(queryName);
      // - find all descriptor(s) for this queryName
      PropertyDescriptorPtr propDesc;
      int propIndex = 0;
      bool foundone = false;
      do {
        propDesc = getDescriptorByName(queryName, propIndex, aDomain, aMode, aParentDescriptor);
        if (propDesc) {
          foundone = true; // found at least one descriptor for this query element
          FOCUSLOG("  - processing descriptor '%s' (%s), fieldKey=%zu, objectKey=%lu", propDesc->name(), propDesc->isStructured() ? "structured" : "scalar", propDesc->fieldKey(), propDesc->objectKey());
          // check if property needs preparation before being accessed
          if (aPreparationList && propDesc->needsPreparation(aMode) && !aPrepared) {
            // collecting list of to-be-prepared properties, and this one needs prep -> add it
            // IMPORTANT: push simple preparations first
            aPreparationList->push_front(PropertyPrep(this, propDesc, queryValue, aResultObject, propDesc->name()));
            FOCUSLOG("- property '%s' needs preparation -> added to preparation list (%zu items now)", propDesc->name(), aPreparationList->size());
            if (aMode==access_read) {
              // read access: return NULL for to-be-prepared properties
              aResultObject->add(propDesc->name(), queryValue->newNull());
            }
          }
          else {
            // actually access by descriptor
            if (aMode==access_write && propDesc->isDeletable() && queryValue->isNull()) {
              // assigning NULL means deleting (possibly entire substructure)
              if (!accessField(access_delete, queryValue, propDesc)) { // delete
                err = Error::err<VdcApiError>(403, "Cannot delete '%s'", propDesc->name());
              }
            }
            else if (propDesc->isStructured()) {
              ApiValuePtr subQuery;
              // property is a container. Now check the value
              if (queryValue->isType(apivalue_object)) {
                subQuery = queryValue; // query specifies next level, just use it
              }
              else if ((aMode==access_write||aMode==access_write_preload) && queryValue->isNull()) {
                // non-deleteable structured value just assigned null -> prevent recursion
                err = Error::err<VdcApiError>(403, "Cannot delete or invalidate '%s'", propDesc->name());
              }
              else if (queryName!="*" && (!wildcard || propDesc->isWildcardAddressable())) {
                // don't recurse deeper when query name is "*" or property is not wildcard-addressable
                // special case is "*" as leaf in query - only recurse if it is not present
                // - autocreate subquery
                subQuery = queryValue->newValue(apivalue_object);
                subQuery->add("", queryValue->newValue(apivalue_null));
              }
              if (subQuery) {
                // addressed property is a container by itself -> recurse
                // - get the PropertyContainer
                int containerDomain = aDomain; // default to same, but getContainer may modify it
                PropertyDescriptorPtr containerPropDesc = propDesc;
                PropertyContainerPtr container = getContainer(containerPropDesc, containerDomain);
                if (container) {
                  FOCUSLOG("  - container for '%s' is 0x%p", propDesc->name(), container.get());
                  if (container!=this) {
                    // switching to another C++ object -> starting at root level in that object
                    // - let the object itself produce its root descriptor
                    containerPropDesc = container->getContainerRootDescriptor(propDesc->getApiVersion());
                    FOCUSLOG("  - container is not the same object");
                  }
                  // - check new root descriptor for preparation and async re-access (e.g. for proxy)
                  if (aPreparationList && containerPropDesc->isRootOfObject() && containerPropDesc->needsPreparation(aMode)) {
                    // must re-access this later
                    // IMPORTANT: push object re-access preparations last
                    aPreparationList->push_back(PropertyPrep(container, containerPropDesc, subQuery, aResultObject, propDesc->name()));
                    FOCUSLOG("- object '%s' needs recursive async property access -> added to preparation list (%zu items now)", propDesc->name(), aPreparationList->size());
                  }
                  else if (aMode==access_read) {
                    // read needs a result object
                    FOCUSLOG("    >>>> RECURSING into accessPropertyInternal()");
                    ApiValuePtr resultValue = queryValue->newValue(apivalue_object);
                    err = container->accessPropertyInternal(aMode, subQuery, resultValue, containerDomain, containerPropDesc, aPreparationList, aPrepared);
                    if (Error::isOK(err)) {
                      // add to result with actual name (from descriptor)
                      FOCUSLOG("\n  <<<< RETURNED from accessProperty() recursion");
                      FOCUSLOG("  - accessProperty of container for '%s' returns %s", propDesc->name(), resultValue->description().c_str());
                      aResultObject->add(propDesc->name(), resultValue);
                    }
                  }
                  else {
                    // for write, just pass the query value and the (non-hierarchic) result object
                    err = container->accessPropertyInternal(aMode, subQuery, aResultObject, containerDomain, containerPropDesc, aPreparationList, aPrepared);
                    FOCUSLOG("    <<<< RETURNED from accessPropertyInternal() recursion");
                  }
                  if ((aMode!=access_read) && Error::isOK(err)) {
                    // give this container a chance to post-process write access
                    err = writtenProperty(aMode, propDesc, aDomain, container);
                  }
                  // 404 errors are collected, but dont abort the query
                  if (Error::isError(err, VdcApiError::domain(), 404)) {
                    if (!errorMsg.empty()) errorMsg += "; ";
                    errorMsg += string_format("Error(s) accessing subproperties of '%s' : { %s }", queryName.c_str(), err->text());
                    err.reset(); // forget the error on this level
                  }
                }
              }
            }
            else {
              // addressed (and known by descriptor!) property is a simple value field -> access it
              if (aMode==access_read) {
                // read access: create a new apiValue and have it filled
                ApiValuePtr fieldValue = queryValue->newValue(propDesc->type()); // create a value of correct type to get filled
                bool accessOk = accessField(aMode, fieldValue, propDesc); // read
                // for read, not getting an OK from accessField means: property does not exist (even if known per descriptor),
                // so it will not be added to the result
                if (accessOk) {
                  // add to result with actual name (from descriptor)
                  aResultObject->add(propDesc->name(), fieldValue);
                }
                FOCUSLOG("    - accessField for '%s' returns %s", propDesc->name(), fieldValue->description().c_str());
              }
              else {
                // write access
                // - writing NULL to a leaf is ok only if the leaf is marked deletable
                if (queryValue->isNull() && !propDesc->isDeletable()) {
                  err = Error::err<VdcApiError>(403, "Writing null to '%s' is not allowed", propDesc->name());
                }
                else if (!accessField(aMode, queryValue, propDesc)) { // write
                  err = Error::err<VdcApiError>(403, "Write access to '%s' denied", propDesc->name());
                }
              }
            }
            if (propDesc->needsPreparation(aMode)) {
              // unprepare (e.g. allow implementations to free resources created for accessing a property)
              FOCUSLOG("- property '%s' access with preparation complete -> unpreparing", propDesc->name());
              finishAccess(aMode, propDesc);
            }
            if (aMode==access_write) {
              // return id of created objects
              if (propDesc->wasCreatedNew()) {
                aResultObject->setType(apivalue_array);
                ApiValuePtr inserted = aResultObject->newObject();
                if (propDesc->parentDescriptor) inserted->add("insertedin", inserted->newString(propDesc->parentDescriptor->name()));
                inserted->add("element", inserted->newString(propDesc->name()));
                aResultObject->arrayAppend(inserted);
              }
              // stop iterating after writing to non-wildcard item (because insertable containers would create multiple items otherwise)
              if (!wildcard) {
                propIndex = PROPINDEX_NONE;
              }
            }
          } // actual access, not preparation
        }
        else {
          // no descriptor found for this query element
          // Note: this means that property is not KNOWN, which IS not the same as getting false from accessField
          //   (latter means that property IS known, but has no value in the context it was queried)
          //   HOWEVER, for the vdc API it was decided that for reading, these cases should NOT be
          //   distinguished for getProperty. If a property does not exist for either reason, the return tree just does
          //   no contain that property.
          //   Also note that not having a property is not the same as not having a property VALUE:
          //   latter case will explicitly return a NULL value
          if (!wildcard && !foundone && aMode!=access_read) {
            // query did address a specific property but it is unknown -> report as error (for read AND write!)
            // - collect error message, but do not abort query processing
            if (!errorMsg.empty()) errorMsg += "; ";
            errorMsg += string_format("Unknown property '%s' -> ignored", queryName.c_str());
          }
        }
      } while (Error::isOK(err) && propIndex!=PROPINDEX_NONE);
    }
    // now generate error if we have collected a non-empty error message
    if (!errorMsg.empty()) {
      err = Error::err_str<VdcApiError>(404, errorMsg);
    }
    FOCUSLOG("- query element named '%s' now has result object: %s", queryName.c_str(), aResultObject->description().c_str());
  }
  return err;
}


bool PropertyContainer::isMatchAll(string &aPropMatch)
{
  return aPropMatch=="*" || aPropMatch.empty();
}



bool PropertyContainer::isNamedPropSpec(string &aPropMatch)
{
  if (isMatchAll(aPropMatch)) return false; // matchall is not named access
  if (aPropMatch[0]=='#') return false; // #n is not named access
  return true; // everything else can be named access
}


bool PropertyContainer::getNextPropIndex(string aPropMatch, int &aStartIndex)
{
  if (isMatchAll(aPropMatch)) {
    // next property to return is just the aStartIndex we are on as-is
    return false; // no specific numeric index
  }
  // get index from numeric specification
  bool numericName = true;
  int currentIndex = aStartIndex;
  const char *s = aPropMatch.c_str();
  if (*s=='#') { s++; numericName = false; }
  if (sscanf(s, "%d", &aStartIndex)==1) {
    // index found, must be higher or same as than current start
    if (aStartIndex<currentIndex)
      aStartIndex = PROPINDEX_NONE; // index out of range
    return numericName;
  }
  aStartIndex = PROPINDEX_NONE; // no valid index specified
  return false; // invalid numeric name does not count as name
}




// default implementation based on numProps/getDescriptorByIndex
// Derived classes with array-like container may directly override this method for more efficient access
PropertyDescriptorPtr PropertyContainer::getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyAccessMode aMode, PropertyDescriptorPtr aParentDescriptor)
{
  int n = numProps(aDomain, aParentDescriptor);
  if (aStartIndex<n && aStartIndex!=PROPINDEX_NONE) {
    // aPropMatch syntax
    // - simple name to match a specific property
    // - empty name or only "*" to match all properties. At this level, there's no difference, but empty causes deep traversal, * does not
    // - name part with a trailing asterisk: wildcard.
    // - #n to access n-th property
    PropertyDescriptorPtr propDesc;
    bool wildcard = false; // assume no wildcard
    if (aPropMatch.empty()) {
      wildcard = true; // implicit wildcard, empty name counts like "*"
    }
    else if (aPropMatch[aPropMatch.size()-1]=='*') {
      wildcard = true; // explicit wildcard at end of string
      aPropMatch.erase(aPropMatch.size()-1); // remove the wildcard char
    }
    else if (aPropMatch[0]=='#') {
      // special case 2 for reading: #n to access n-th subproperty
      int newIndex = n; // set out of range by default
      if (sscanf(aPropMatch.c_str()+1, "%d", &newIndex)==1) {
        // name does not matter, pick item at newIndex unless below current start
        wildcard = true;
        aPropMatch.clear();
        if(newIndex>=aStartIndex)
          aStartIndex = newIndex; // not yet passed this index in iteration -> use it
        else
          aStartIndex = n; // already passed -> make out of range
      }
    }
    while (aStartIndex<n) {
      propDesc = getDescriptorByIndex(aStartIndex, aDomain, aParentDescriptor);
      // skip non-existent ones (might happen if subclass suppresses some properties)
      if (propDesc) {
        // check for match
        if (wildcard && aPropMatch.size()==0)
          break; // shortcut for "match all" case
        // match beginning
        if (
          (!wildcard && aPropMatch==propDesc->name()) || // complete match
          (wildcard && (strncmp(aPropMatch.c_str(),propDesc->name(),aPropMatch.size())==0)) // match of name's beginning
        ) {
          break; // this entry matches
        }
      }
      // next
      aStartIndex++;
    }
    if (aStartIndex<n) {
      // found a descriptor
      // - determine next index
      aStartIndex++;
      if (aStartIndex>=n)
        aStartIndex=PROPINDEX_NONE;
      // - return the descriptor
      return propDesc;
    }
  }
  // failure
  // no more descriptors
  aStartIndex=PROPINDEX_NONE;
  return PropertyDescriptorPtr(); // no descriptor
}


PropertyDescriptorPtr PropertyContainer::getDescriptorByNumericName(
  string aPropMatch, int &aStartIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor,
  intptr_t aObjectKey
)
{
  PropertyDescriptorPtr propDesc;
  getNextPropIndex(aPropMatch, aStartIndex);
  int n = numProps(aDomain, aParentDescriptor);
  if (aStartIndex!=PROPINDEX_NONE && aStartIndex<n) {
    // within range, create descriptor
    DynamicPropertyDescriptor *descP = new DynamicPropertyDescriptor(aParentDescriptor);
    descP->propertyName = string_format("%d", aStartIndex);
    descP->propertyType = aParentDescriptor->type();
    descP->propertyFieldKey = aStartIndex;
    descP->propertyObjectKey = aObjectKey;
    propDesc = PropertyDescriptorPtr(descP);
    // advance index
    aStartIndex++;
  }
  if (aStartIndex>=n)
    aStartIndex = PROPINDEX_NONE;
  return propDesc;
}


// MARK: - reading from CSV

#if ENABLE_SETTINGS_FROM_FILES

bool PropertyContainer::readPropsFromCSV(int aDomain, bool aOnlyExplicitlyOverridden, const char *&aCSVCursor, const char *aTextSourceName, int aLineNo)
{
  bool anySettingsApplied = false;
  string f;
  const char *fp;
  // process properties
  while (nextCSVField(aCSVCursor, f)) {
    // skip empty fields and those starting with #, allowing to format and comment CSV a bit (align properties)
    if (f.empty() || f[0]=='#') {
      // skip this field
      continue;
    }
    // get related value
    string v;
    // use same separator as used to terminate property name (these never contain comma, semicolon or TAB!)
    if (!nextCSVField(aCSVCursor, v, *(aCSVCursor-1))) {
      // no value
      LOG(LOG_ERR, "%s:%d - missing value for '%s'", aTextSourceName, aLineNo, f.c_str());
      break;
    }
    // create write access tree
    fp = f.c_str();
    string part;
    ApiValuePtr property = ApiValuePtr(new JsonApiValue);
    property->setType(apivalue_object);
    ApiValuePtr proplvl = property;
    // check override for this particular property
    bool overridden = false;
    if (*fp=='!') {
      fp++;
      overridden = true; // explicit override
    }
    // check if we should apply it
    if (aOnlyExplicitlyOverridden && !overridden) {
      // skip this property
      continue;
    }
    // now apply
    while (nextPart(fp, part, '/')) {
      if (*fp) {
        // not last part, add another query level
        ApiValuePtr nextlvl = proplvl->newValue(apivalue_object);
        proplvl->add(part, nextlvl);
        proplvl = nextlvl;
      }
      else {
        // last part, assign value
        ApiValuePtr val;
        if (v.find_first_not_of("-0123456789.")==string::npos) {
          // numeric
          double nv = 0;
          sscanf(v.c_str(), "%lf", &nv);
          if (v.find('.')!=string::npos) {
            // float
            val = proplvl->newDouble(nv);
          }
          else {
            // integer
            val = proplvl->newInt64(nv);
          }
        }
        else if (v.size()>0 && v[0]=='{') {
          // structured JSON object
          JsonObjectPtr j = JsonObject::objFromText(v.c_str());
          val = JsonApiValue::newValueFromJson(j);
        }
        else {
          // just string
          val = proplvl->newString(v);
        }
        proplvl->add(part, val);
        break;
      }
    }
    // now access that property (note: preparation is not checked so properties must be writable without preparation)
    ErrorPtr err = accessPropertyInternal(access_write, property, ApiValuePtr(), aDomain, PropertyDescriptorPtr(), PropertyPrepListPtr(), false);
    if (Error::notOK(err)) {
      LOG(LOG_ERR, "%s:%d - error writing property '%s': %s", aTextSourceName, aLineNo, f.c_str(), err->text());
    }
    else {
      anySettingsApplied = true;
    }
  }
  return anySettingsApplied;
}

#endif // ENABLE_SETTINGS_FROM_FILES
