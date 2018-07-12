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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 0

#include "propertycontainer.hpp"

// needed to implement reading from CSV
#include "jsonvdcapi.hpp"

using namespace p44;



void PropertyContainer::accessProperty(PropertyAccessMode aMode, ApiValuePtr aQueryObject, int aDomain, int aApiVersion, PropertyAccessCB aAccessCompleteCB)
{
  // create a list for possibly needed preparations
  PropertyPrepListPtr prepList = PropertyPrepListPtr(new PropertyPrepList);
  // create root descriptor
  PropertyDescriptorPtr parentDescriptor = PropertyDescriptorPtr(new RootPropertyDescriptor(aApiVersion));
  // first attempt to access
  // - create result object of same API type as query
  ApiValuePtr result;
  if (aMode==access_read) result = aQueryObject->newObject();
  ErrorPtr err = accessPropertyInternal(aMode, aQueryObject, result, aDomain, parentDescriptor, prepList);
  if (prepList->empty()) {
    // no need for preparation, immediately call back
    if (aAccessCompleteCB) aAccessCompleteCB(result, err);
    // done
    return;
  }
  // need preparation
  prepareNext(prepList, aMode, aQueryObject, aDomain, parentDescriptor, aAccessCompleteCB, ErrorPtr());
}


void PropertyContainer::prepareNext(PropertyPrepListPtr aPrepList, PropertyAccessMode aMode, ApiValuePtr aQueryObject, int aDomain, PropertyDescriptorPtr aParentDescriptor, PropertyAccessCB aAccessCompleteCB, ErrorPtr aError)
{
  if (!Error::isOK(aError)) {
    LOG(LOG_WARNING, "- prepraration of property failed with error: %s", aError->description().c_str());
  }
  if (aPrepList->empty()) {
    // all prepared, access again
    // - create result object of same API type as query
    ApiValuePtr result;
    if (aMode==access_read) result = aQueryObject->newObject();
    ErrorPtr err = accessPropertyInternal(aMode, aQueryObject, result, aDomain, aParentDescriptor, PropertyPrepListPtr());
    // call back
    if (aAccessCompleteCB) aAccessCompleteCB(result, err);
    // done
    return;
  }
  // prepare next item on the list
  PropertyPrep prep = aPrepList->front();
  aPrepList->pop_front();
  prep.target->prepareAccess(aMode, prep.propertyDescriptor,
    boost::bind(&PropertyContainer::prepareNext, this, aPrepList, aMode, aQueryObject, aDomain, aParentDescriptor, aAccessCompleteCB, _1)
  );
}



void PropertyContainer::prepareAccess(PropertyAccessMode aMode, PropertyDescriptorPtr aPropertyDescriptor, StatusCB aPreparedCB)
{
  // nothing to prepare in base class
  if (aPreparedCB) aPreparedCB(ErrorPtr());
}


void PropertyContainer::finishAccess(PropertyAccessMode aMode, PropertyDescriptorPtr aPropertyDescriptor)
{
  // nothing to un-prepare/finalize in base class
}




ErrorPtr PropertyContainer::accessPropertyInternal(PropertyAccessMode aMode, ApiValuePtr aQueryObject, ApiValuePtr aResultObject, int aDomain, PropertyDescriptorPtr aParentDescriptor, PropertyPrepListPtr aPreparationList)
{
  ErrorPtr err;
  assert(aParentDescriptor);
  FOCUSLOG("\naccessProperty: entered with query = %s", aQueryObject->description().c_str());
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
  if (aMode==access_read) {
    if (!aResultObject)
      return Error::err<VdcApiError>(415, "accessing property for read must provide result object");
    aResultObject->setType(apivalue_object); // must be object
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
          if (aPreparationList && propDesc->needsPreparation(aMode)) {
            // collecting list of to-be-prepared properties, and this one needs prep -> add it
            aPreparationList->push_back(PropertyPrep(this, propDesc));
            FOCUSLOG("- property '%s' needs preparation -> added to preparation list (%d items now)", propDesc->name(), aPreparationList->size());
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
                  FOCUSLOG("    >>>> RECURSING into accessProperty()");
                  if (container!=this) {
                    // switching to another C++ object -> starting at root level in that object
                    containerPropDesc->rootOfObject = true;
                  }
                  if (aMode==access_read) {
                    // read needs a result object
                    ApiValuePtr resultValue = queryValue->newValue(apivalue_object);
                    err = container->accessPropertyInternal(aMode, subQuery, resultValue, containerDomain, containerPropDesc, aPreparationList);
                    if (Error::isOK(err)) {
                      // add to result with actual name (from descriptor)
                      FOCUSLOG("\n  <<<< RETURNED from accessProperty() recursion");
                      FOCUSLOG("  - accessProperty of container for '%s' returns %s", propDesc->name(), resultValue->description().c_str());
                      aResultObject->add(propDesc->name(), resultValue);
                    }
                  }
                  else {
                    // for write, just pass the query value
                    err = container->accessPropertyInternal(aMode, subQuery, ApiValuePtr(), containerDomain, containerPropDesc, aPreparationList);
                    FOCUSLOG("    <<<< RETURNED from accessProperty() recursion", propDesc->name(), container.get());
                  }
                  if ((aMode!=access_read) && Error::isOK(err)) {
                    // give this container a chance to post-process write access
                    err = writtenProperty(aMode, propDesc, aDomain, container);
                  }
                  // 404 errors are collected, but dont abort the query
                  if (Error::isError(err, VdcApiError::domain(), 404)) {
                    if (!errorMsg.empty()) errorMsg += "; ";
                    errorMsg += string_format("Error(s) accessing subproperties of '%s' : { %s }", queryName.c_str(), err->description().c_str());
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
            if (aMode==access_write && !wildcard) {
              // stop iterating after writing to non-wildcard item (because insertable containers would create multiple items otherwise)
              propIndex = PROPINDEX_NONE;
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
    #if DEBUGLOGGING
    if (aMode==access_read) {
      FOCUSLOG("- query element named '%s' now has result object: %s", queryName.c_str(), aResultObject->description().c_str());
    }
    #endif
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


// MARK: ===== reading from CSV


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
    ErrorPtr err = accessPropertyInternal(access_write, property, ApiValuePtr(), aDomain, PropertyDescriptorPtr(), PropertyPrepListPtr());
    if (!Error::isOK(err)) {
      LOG(LOG_ERR, "%s:%d - error writing property '%s': %s", aTextSourceName, aLineNo, f.c_str(), err->description().c_str());
    }
    else {
      anySettingsApplied = true;
    }
  }
  return anySettingsApplied;
}


