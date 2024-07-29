//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2021-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "scriptedvdc.hpp"

#if ENABLE_SCRIPTED

#include "jsonvdcapi.hpp"

using namespace p44;


// MARK: - ScriptedDeviceObj

using namespace P44Script;

class ScriptedDeviceObj : public DeviceObj
{
  typedef DeviceObj inherited;
public:
  ScriptedDeviceObj(DevicePtr aDevice);
  ScriptedDevicePtr scriptedDevice() { return boost::dynamic_pointer_cast<ScriptedDevice>(device()); }
};


// message()
// message(messagetosend)
FUNC_ARG_DEFS(message, { objectvalue|text|optionalarg } );
static void message_func(BuiltinFunctionContextPtr f)
{
  ScriptedDeviceObj* d = dynamic_cast<ScriptedDeviceObj*>(f->thisObj().get());
  assert(d);
  if (f->numArgs()==0) {
    // return the value source to receive messages from vDC
    f->finish(new OneShotEventNullValue(d->scriptedDevice().get(), "vdc message"));
  }
  else {
    // send messages to vdc
    ErrorPtr err = d->scriptedDevice()->sendDeviceMesssage(f->arg(0)->jsonValue());
    if (Error::notOK(err)) {
      f->finish(new ErrorValue(err));
    }
    else {
      f->finish();
    }
  }
}


static const BuiltinMemberDescriptor scriptedDeviceMembers[] = {
  FUNC_DEF_W_ARG(message, executable|text|null),
  { NULL } // terminator
};

ScriptedDeviceLookup::ScriptedDeviceLookup(ScriptedDevice& aScriptedDevice) :
  inherited(scriptedDeviceMembers),
  mScriptedDevice(aScriptedDevice)
{
}

static BuiltInMemberLookup* sharedScriptedDeviceMemberLookupP = NULL;

ScriptedDeviceObj::ScriptedDeviceObj(DevicePtr aDevice) : inherited(aDevice)
{
  registerSharedLookup(sharedScriptedDeviceMemberLookupP, scriptedDeviceMembers);
};


// MARK: - ScriptedDevice


ScriptedDevice::ScriptedDevice(Vdc *aVdcP, const string aDefaultUniqueId, bool aSimpleText) :
  inherited(aVdcP, aSimpleText),
  mDefaultUniqueId(aDefaultUniqueId),
  mScriptedDeviceRowID(0),
  mScriptedDeviceLookup(*this),
  mImplementation(*this)
{

  mScriptedDeviceLookup.isMemberVariable();
  mTypeIdentifier = "scripted";
  mModelNameString = "custom script device";
  mIconBaseName = "scpt";
}


ScriptedVdc &ScriptedDevice::getScriptedVdc()
{
  return *(static_cast<ScriptedVdc *>(mVdcP));
}


ScriptObjPtr ScriptedDevice::newDeviceObj()
{
  return new ScriptedDeviceObj(this);
}


ScriptedDevice::~ScriptedDevice()
{
  mImplementation.mScript.runCommand(stop);
  OLOG(LOG_DEBUG, "destructed");
}


void ScriptedDevice::willBeAdded()
{
  mImplementation.mScript.setScriptHostUid(string_format("scripteddev_%s.implementation", getDsUid().getString().c_str()));
  inherited::willBeAdded();
}


void ScriptedDevice::initializeDevice(StatusCB aCompletedCB, bool aFactoryReset)
{
  mImplementation.mScript.runCommand(restart);
  inherited::initializeDevice(aCompletedCB, aFactoryReset);
}


void ScriptedDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  mImplementation.mScript.runCommand(stop);
  if (mScriptedDeviceRowID) {
    if(getScriptedVdc().mDb.executef("DELETE FROM scriptedDevices WHERE rowid=%lld", mScriptedDeviceRowID)==SQLITE_OK) {
      if (aForgetParams) mImplementation.mScript.deleteSource(); // make sure script gets deleted
    }
    else {
      OLOG(LOG_ERR, "Error deleting scripted device: %s", getScriptedVdc().mDb.error()->description().c_str());
    }
  }
  // disconnection is immediate, so we can call inherited right now
  inherited::disconnect(aForgetParams, aDisconnectResultHandler);
}


ScriptObjPtr ScriptedDeviceImplementation::runScriptCommand(ScriptCommand aScriptCommand)
{
  EvaluationFlags flags = stopall; // main script must always be running only once, so stopping all before start and restart
  ScriptObjPtr ret;
  switch(aScriptCommand) {
    case P44Script::debug:
      flags |= singlestep;
    case P44Script::start:
    case P44Script::restart:
      SOLOG(mScriptedDevice, LOG_NOTICE, "(Re-)starting device implementation script");
      mRestartTicket.cancel();
      mContext->clearVars(); // clear vars and (especially) context local handlers
      ret = mScript.run(stopall, boost::bind(&ScriptedDeviceImplementation::implementationEnds, this, _1), ScriptObjPtr(), Infinite);
      break;
    case P44Script::stop:
      SOLOG(mScriptedDevice, LOG_NOTICE, "Stopping device implementation script");
      mRestartTicket.cancel();
      if (!mContext->abort(stopall, new ErrorValue(ScriptError::Aborted, "device implementation script stopped"))) {
        // nothing to abort, make sure handlers are gone (otherwise, they will get cleared in implementationEnds())
        mContext->clearVars();
      }
      break;
    default:
      ret = mScript.defaultCommandImplementation(aScriptCommand, NoOP, ScriptObjPtr());
  }
  return ret;
}


#define IMPLEMENTATION_RESTART_DELAY (20*Second)

void ScriptedDeviceImplementation::implementationEnds(ScriptObjPtr aResult)
{
  if (mScript.empty()) {
    // no restart if nothing programmed yet
    SOLOG(mScriptedDevice, LOG_ERR, "Custom device has no implementation script (yet)");
    return;
  }
  SOLOG(mScriptedDevice, aResult && aResult->isErr() ? LOG_WARNING : LOG_NOTICE, "device implementation script finished running, result=%s", ScriptObj::describe(aResult).c_str());
  if (
    aResult &&
    Error::isDomain(aResult->errorValue(), ScriptError::domain()) &&
    aResult->errorValue()->getErrorCode()>=ScriptError::FatalErrors
  ) {
    mContext->clearVars(); // clear vars and (especially) context local handlers
    return; // fatal error, no auto-restart
  }
  if (aResult && aResult->errorValue()->isOK() && mScriptedDevice.hasSinks()) return; // script ends w/o error, and monitors messages -> ok
  if (aResult && aResult->hasType(numeric) && aResult->boolValue()) return; // returning explicit trueish means no restart needed, as well
  // retry in a while
  SOLOG(mScriptedDevice, LOG_NOTICE, "Will restart implementation in %lld seconds", IMPLEMENTATION_RESTART_DELAY/Second);
  mRestartTicket.executeOnce(boost::bind(&ScriptedDeviceImplementation::restartImplementation, this), IMPLEMENTATION_RESTART_DELAY);
}

void ScriptedDeviceImplementation::restartImplementation()
{
  mScript.runCommand(restart);
}



ErrorPtr ScriptedDevice::sendDeviceMesssage(JsonObjectPtr aMessage)
{
  if (mSimpletext) {
    if (!aMessage || !aMessage->isType(json_type_string)) {
      return TextError::err("simple protocol mode: messages must be text");
    }
    string msg = trimWhiteSpace(aMessage->stringValue());
    OLOG(LOG_INFO, "device -> ScriptedVdc (simple) message received: %s", msg.c_str());
    string key;
    string val;
    if (!keyAndValue(msg, key, val, '=')) {
      key = msg; // just message...
      val.clear(); // no value
    }
    return processSimpleMessage(key, val);
  }
  else {
    JsonObjectPtr o;
    if (!aMessage || !aMessage->get("message", o)) {
      return TextError::err("json protcol mode: missing 'message' field");
    }
    OLOG(LOG_INFO, "device -> ScriptedVdc (JSON) message received: %s", aMessage->json_c_str());
    return processJsonMessage(o->stringValue(), aMessage);
  }
}


void ScriptedDevice::sendDeviceApiJsonMessage(JsonObjectPtr aMessage)
{
  // now show and send
  OLOG(LOG_INFO, "device <- ScriptedVdc (JSON) message sent: %s", aMessage->c_strValue());
  sendEvent(ScriptObj::valueFromJSON(aMessage));
}


void ScriptedDevice::sendDeviceApiSimpleMessage(string aMessage)
{
  OLOG(LOG_INFO, "device <- ScriptedVdc (simple) message sent: %s", aMessage.c_str());
  sendEvent(new StringValue(aMessage));
}


// MARK: - custom methods

ErrorPtr ScriptedDevice::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  if (aMethod=="x-p44-restartImpl") {
    // re-run the device implementation script
    mImplementation.mScript.runCommand(restart);
    return Error::ok();
  }
  if (aMethod=="x-p44-stopImpl") {
    // stop the device implementation script
    mImplementation.mScript.runCommand(stop);
    return Error::ok();
  }
  if (aMethod=="x-p44-checkImpl") {
    // check the implementation script for syntax errors (but do not re-start it)
    ScriptObjPtr res = mImplementation.mScript.syntaxcheck();
    ApiValuePtr checkResult = aRequest->newApiValue();
    checkResult->setType(apivalue_object);
    if (!res || !res->isErr()) {
      OLOG(LOG_NOTICE, "Checked implementation script: syntax OK");
      checkResult->add("result", checkResult->newNull());
    }
    else {
      OLOG(LOG_NOTICE, "Error in implementation: %s", res->errorValue()->text());
      checkResult->add("error", checkResult->newString(res->errorValue()->getErrorMessage()));
      SourceCursor* cursor = res->cursor();
      if (cursor) {
        checkResult->add("at", checkResult->newUint64(cursor->textpos()));
        checkResult->add("line", checkResult->newUint64(cursor->lineno()));
        checkResult->add("char", checkResult->newUint64(cursor->charpos()));
      }
    }
    aRequest->sendResult(checkResult);
    return ErrorPtr();
  }
  return inherited::handleMethod(aRequest, aMethod, aParams);
}


// MARK: - implementation persistence

ErrorPtr ScriptedDevice::load()
{
  ErrorPtr err = inherited::load();
  err = mImplementation.loadFromStore(mDSUID.getString().c_str());
  if (Error::notOK(err)) OLOG(LOG_ERR,"Error loading implementation: %s", err->text());
  return err;
}


ErrorPtr ScriptedDevice::save()
{
  ErrorPtr err = mImplementation.saveToStore(mDSUID.getString().c_str(), false); // only one record per device
  if (Error::notOK(err)) OLOG(LOG_ERR,"Error saving implementation: %s", err->text());
  return inherited::save();
}


bool ScriptedDevice::isDirty()
{
  // check the device settings
  if (mImplementation.isDirty()) return true;
  return inherited::isDirty();
}


void ScriptedDevice::markClean()
{
  // check the device settings
  mImplementation.markClean();
  inherited::markClean();
}


ErrorPtr ScriptedDevice::forget()
{
  // delete the device settings
  mImplementation.deleteFromStore();
  return inherited::forget();
}


// MARK: - property access

enum {
  initmessage_key,
  implementation_key,
  implementationId_key,
  numProperties
};

static char scriptedDevice_key;


int ScriptedDevice::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  // Note: only add my own count when accessing root level properties!!
  if (aParentDescriptor->isRootOfObject()) {
    // Accessing properties at the Device (root) level, add mine
    return inherited::numProps(aDomain, aParentDescriptor)+numProperties;
  }
  // just return base class' count
  return inherited::numProps(aDomain, aParentDescriptor);
}


PropertyDescriptorPtr ScriptedDevice::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  static const PropertyDescription properties[numProperties] = {
    { "x-p44-initmessage", apivalue_string, initmessage_key, OKEY(scriptedDevice_key) },
    { "x-p44-implementation", apivalue_string, implementation_key, OKEY(scriptedDevice_key) },
    { "x-p44-implementationId", apivalue_string, implementationId_key, OKEY(scriptedDevice_key) },
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
bool ScriptedDevice::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(scriptedDevice_key)) {
    if (aMode==access_read) {
      // read properties
      switch (aPropertyDescriptor->fieldKey()) {
        case initmessage_key: aPropValue->setStringValue(mInitMessageText); return true;
        case implementation_key: aPropValue->setStringValue(mImplementation.mScript.getSource()); return true;
        case implementationId_key: aPropValue->setStringValue(mImplementation.mScript.getSourceUid()); return true;
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor->fieldKey()) {
        case implementation_key:
          if (mImplementation.mScript.setAndStoreSource(aPropValue->stringValue())) {
            mImplementation.markDirty();
          }
          return true;
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}



// MARK: - ScriptedDeviceImplementation


ScriptedDeviceImplementation::ScriptedDeviceImplementation(ScriptedDevice &aScriptedDevice) :
  inherited(aScriptedDevice.getVdcHost().getDsParamStore()),
  mScriptedDevice(aScriptedDevice),
  mScript(sourcecode|regular, "implementation", nullptr, &mScriptedDevice) // do not keep vars, only one main thread!
{
  mContext = StandardScriptingDomain::sharedDomain().newContext(mScriptedDevice.newDeviceObj());
  mScript.setSharedMainContext(mContext);
  mScript.setScriptCommandHandler(boost::bind(&ScriptedDeviceImplementation::runScriptCommand, this, _1));
  // script uid will be set at load
}



const char *ScriptedDeviceImplementation::tableName()
{
  return "ScriptedDeviceImplementations";
}


// data field definitions

static const size_t numFields = 1;

size_t ScriptedDeviceImplementation::numFieldDefs()
{
  return inherited::numFieldDefs()+numFields;
}


const FieldDefinition *ScriptedDeviceImplementation::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numFields] = {
    { "implementation", SQLITE_TEXT }
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numFields)
    return &dataDefs[aIndex];
  return NULL;
}


void ScriptedDeviceImplementation::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inherited::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get the field values
  mScript.loadSource(nonNullCStr(aRow->get<const char *>(aIndex++)));
}


void ScriptedDeviceImplementation::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, mScript.getSourceToStoreLocally().c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
}


// MARK: - ScriptedDevicePersistence


// Version history
//  1 : First version
#define SCRIPTEDDEVICES_SCHEMA_MIN_VERSION 1 // minimally supported version, anything older will be deleted
#define SCRIPTEDDEVICES_SCHEMA_VERSION 1 // current version

string ScriptedDevicePersistence::dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion)
{
  string sql;
  if (aFromVersion==0) {
    // create DB from scratch
    // - use standard globs table for schema version
    sql = inherited::dbSchemaUpgradeSQL(aFromVersion, aToVersion);
    // - create my tables
    sql.append(
      "CREATE TABLE scriptedDevices ("
      " scptdevid, initJSON TEXT,"
      " PRIMARY KEY (scptdevid)"
      ");"
    );
    // reached final version in one step
    aToVersion = SCRIPTEDDEVICES_SCHEMA_VERSION;
  }
  return sql;
}



// MARK: - scripted device container

ScriptedVdc::ScriptedVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  CustomVdc(aInstanceNumber, aVdcHostP, aTag)
{
  // set default icon base name
  mIconBaseName = "vdc_scpt";
}


const char *ScriptedVdc::vdcClassIdentifier() const
{
  return "Scripted_Device_Container";
}


void ScriptedVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  // load persistent params for dSUID
  load();
  // load private data
  string databaseName = getPersistentDataDir();
  string_format_append(databaseName, "%s_%d.sqlite3", vdcClassIdentifier(), getInstanceNumber());
  ErrorPtr error = mDb.connectAndInitialize(databaseName.c_str(), SCRIPTEDDEVICES_SCHEMA_VERSION, SCRIPTEDDEVICES_SCHEMA_MIN_VERSION, aFactoryReset);
  if (!getVdcFlag(vdcflag_flagsinitialized)) setVdcFlag(vdcflag_hidewhenempty, true); // hide by default
  aCompletedCB(error); // return status of DB init
}



void ScriptedVdc::identifyToUser(MLMicroSeconds aDuration)
{
  if (mForwardIdentify) {
    // TODO: %%% send "VDCIDENTIFY" or maybe "vdc:IDENTIFY"
    //   to all connectors - we need to implement a connector list for that
    OLOG(LOG_WARNING, "vdc level identify forwarding not yet implemented")
  }
  else {
    inherited::identifyToUser(aDuration);
  }
}



ScriptedDevicePtr ScriptedVdc::addScriptedDevice(const string aScptDevId, JsonObjectPtr aInitObj, ErrorPtr &aErr)
{
  ScriptedDevicePtr newDev;
  // Scripted objects have a per-device protocol flag
  bool simpletext = CustomDevice::checkSimple(aInitObj, aErr);
  if (Error::isOK(aErr)) {
    newDev = ScriptedDevicePtr(new ScriptedDevice(this, aScptDevId, simpletext));
    // configure it
    aErr = newDev->configureDevice(aInitObj);
    if (Error::isOK(aErr)) {
      // device configured, add it now
      if (!simpleIdentifyAndAddDevice(newDev)) {
        aErr = TextError::err("device could not be added (duplicate uniqueid could be a reason, see log)");
        newDev.reset(); // forget it
      }
    }
    else {
      newDev.reset(); // forget it
    }
  }
  return newDev;
}




/// collect devices from this vDC
/// @param aCompletedCB will be called when device scan for this vDC has been completed
void ScriptedVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  ErrorPtr err;
  // incrementally collecting static devices makes no sense. The devices are "static"!
  if (!(aRescanFlags & rescanmode_incremental)) {
    // non-incremental, re-collect all devices
    removeDevices(aRescanFlags & rescanmode_clearsettings);
    // create devices from initJSON in the database
    sqlite3pp::query qry(mDb);
    if (qry.prepare("SELECT scptdevid, initJSON, rowid FROM scriptedDevices")==SQLITE_OK) {
      for (sqlite3pp::query::iterator i = qry.begin(); i != qry.end(); ++i) {
        string initTxt = i->get<string>(1);
        JsonObjectPtr init = JsonObject::objFromText(initTxt.c_str(), -1, &err, true);
        ScriptedDevicePtr dev;
        if (init) {
          dev = addScriptedDevice(i->get<string>(0).c_str(), init, err);
        }
        if (dev) {
          dev->mScriptedDeviceRowID = i->get<int>(2);
          dev->mInitMessageText = initTxt;
        }
        else {
          OLOG(LOG_ERR, "Cannot create device rowid=%d: %s", i->get<int>(2), Error::text(err));
        }
      }
    }
  }
  // return last error, if any
  aCompletedCB(err);
}



ErrorPtr ScriptedVdc::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="x-p44-addDevice") {
    // add a new scripted device
    ApiValuePtr initParam;
    JsonApiValuePtr j;
    respErr = checkParam(aParams, "init", initParam);
    if (Error::isOK(respErr)) {
      JsonObjectPtr initJSON;
      string initMsg;
      if (initParam->isType(apivalue_string)) {
        // string containing JSON source, comments allowed
        initMsg =  initParam->stringValue();
        initJSON = JsonObject::objFromText(initMsg.c_str(), -1, &respErr, true);
        if (!Error::isOK(respErr)) respErr->prefixMessage("parsing JSON ");
      }
      else if (((j = dynamic_pointer_cast<JsonApiValue>(initParam)) && j->isType(apivalue_object) )) {
        initJSON = j->jsonObject();
        initMsg = initJSON->json_c_str(); // render as text for saving
      }
      else {
        respErr = WebError::webErr(500, "init must be JSON object (as string or API object)");
      }
      if (Error::isOK(respErr)) {
        // use current time as ID for new scripted devices
        string scptDevId = string_format("scripted_%lld", MainLoop::now());
        // try to create device
        ScriptedDevicePtr dev = addScriptedDevice(scptDevId, initJSON, respErr);
        if (dev) {
          // insert into database
          if(mDb.executef(
            "INSERT OR REPLACE INTO scriptedDevices (scptdevid,initJSON) VALUES ('%q','%q')",
            scptDevId.c_str(),
            initMsg.c_str()
          )!=SQLITE_OK) {
            respErr = mDb.error("saving scripted device init message");
          }
          else {
            dev->mScriptedDeviceRowID = mDb.last_insert_rowid();
            dev->mInitMessageText = initMsg;
            // confirm
            ApiValuePtr r = aRequest->newApiValue();
            r->setType(apivalue_object);
            r->add("dSUID", r->newBinary(dev->mDSUID.getBinary()));
            r->add("rowid", r->newUint64(dev->mScriptedDeviceRowID));
            r->add("name", r->newString(dev->getName()));
            aRequest->sendResult(r);
            respErr.reset(); // make sure we don't send an extra ErrorOK
          }
        }
      }
    }
  }
  else {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }
  return respErr;
}




#endif // ENABLE_SCRIPTED
