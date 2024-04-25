//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2016-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "evaluatorvdc.hpp"
#include "evaluatordevice.hpp"

#if ENABLE_EVALUATORS

using namespace p44;


// MARK: - DB and initialisation


// Version history
//  1 : First version
#define EVALUATORDEVICES_SCHEMA_MIN_VERSION 1 // minimally supported version, anything older will be deleted
#define EVALUATORDEVICES_SCHEMA_VERSION 1 // current version

string EvaluatorDevicePersistence::dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion)
{
  string sql;
  if (aFromVersion==0) {
    // create DB from scratch
    // - use standard globs table for schema version
    sql = inherited::dbSchemaUpgradeSQL(aFromVersion, aToVersion);
    // - create my tables
    sql.append(
      "CREATE TABLE evaluators ("
      " evaluatorid, config TEXT,"
      " PRIMARY KEY (evaluatorid)"
      ");"
    );
    // reached final version in one step
    aToVersion = EVALUATORDEVICES_SCHEMA_VERSION;
  }
  return sql;
}



EvaluatorVdc::EvaluatorVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  Vdc(aInstanceNumber, aVdcHostP, aTag)
{
}


void EvaluatorVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  // load persistent params for dSUID
  load();
  // load private data
  string databaseName = getPersistentDataDir();
  string_format_append(databaseName, "%s_%d.sqlite3", vdcClassIdentifier(), getInstanceNumber());
  ErrorPtr error = mDb.connectAndInitialize(databaseName.c_str(), EVALUATORDEVICES_SCHEMA_VERSION, EVALUATORDEVICES_SCHEMA_MIN_VERSION, aFactoryReset);
  if (!getVdcFlag(vdcflag_flagsinitialized)) setVdcFlag(vdcflag_hidewhenempty, true); // hide by default
  aCompletedCB(error); // return status of DB init
}



// vDC name
const char *EvaluatorVdc::vdcClassIdentifier() const
{
  return "Evaluator_Device_Container";
}


bool EvaluatorVdc::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("evaluator", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}




/// collect devices from this vDC
/// @param aCompletedCB will be called when device scan for this vDC has been completed
void EvaluatorVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  // incrementally collecting configured devices makes no sense. The devices are "static"!
  if (!(aRescanFlags & rescanmode_incremental)) {
    // non-incremental, re-collect all devices
    removeDevices(aRescanFlags & rescanmode_clearsettings);
    // add from the DB
    sqlite3pp::query qry(mDb);
    if (qry.prepare("SELECT evaluatorid, config, rowid FROM evaluators")==SQLITE_OK) {
      for (sqlite3pp::query::iterator i = qry.begin(); i != qry.end(); ++i) {
        EvaluatorDevicePtr dev = EvaluatorDevicePtr(new EvaluatorDevice(this, i->get<string>(0), i->get<string>(1)));
        if (dev) {
          dev->evaluatorDeviceRowID = i->get<int>(2);
          simpleIdentifyAndAddDevice(dev);
        }
      }
    }
  }
  // assume ok
  aCompletedCB(ErrorPtr());
}


ErrorPtr EvaluatorVdc::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="x-p44-addDevice") {
    // add a new static device
    string evaluatorType;
    respErr = checkStringParam(aParams, "evaluatorType", evaluatorType);
    if (Error::isOK(respErr)) {
      // optional name
      string name;
      checkStringParam(aParams, "name", name);
      // use current time as ID for new evaluators
      string evaluatorId = string_format("evaluator_%lld", MainLoop::now());
      // try to create device
      EvaluatorDevicePtr dev = EvaluatorDevicePtr(new EvaluatorDevice(this, evaluatorId, evaluatorType));
      if (!dev) {
        respErr = WebError::webErr(500, "invalid configuration for evaluator -> none created");
      }
      else {
        // set name
        if (name.size()>0) dev->setName(name);
        // insert into database
        if (mDb.executef(
          "INSERT OR REPLACE INTO evaluators (evaluatorId, config) VALUES ('%q','%q')",
          evaluatorId.c_str(), evaluatorType.c_str()
        )!=SQLITE_OK) {
          respErr = mDb.error("saving evaluator");
        }
        else {
          dev->evaluatorDeviceRowID = mDb.last_insert_rowid();
          simpleIdentifyAndAddDevice(dev);
          // confirm
          ApiValuePtr r = aRequest->newApiValue();
          r->setType(apivalue_object);
          r->add("dSUID", r->newBinary(dev->mDSUID.getBinary()));
          r->add("rowid", r->newUint64(dev->evaluatorDeviceRowID));
          r->add("name", r->newString(dev->getName()));
          aRequest->sendResult(r);
          respErr.reset(); // make sure we don't send an extra ErrorOK
        }
      }
    }
  }
  else {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }
  return respErr;
}


#endif // ENABLE_EVALUATORS

