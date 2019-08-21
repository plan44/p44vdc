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

#include "ledchainvdc.hpp"

#if ENABLE_LEDCHAIN

#include "ledchaindevice.hpp"

using namespace p44;



// MARK: - DB and initialisation


// Version history
//  1 : First version
//  2 : Add y/dy
#define LEDCHAINDEVICES_SCHEMA_MIN_VERSION 1 // minimally supported version, anything older will be deleted
#define LEDCHAINDEVICES_SCHEMA_VERSION 2 // current version

string LedChainDevicePersistence::dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion)
{
  string sql;
  if (aFromVersion==0) {
    // create DB from scratch
    // - use standard globs table for schema version
    sql = inherited::dbSchemaUpgradeSQL(aFromVersion, aToVersion);
    // - create my tables
    sql.append(
      "CREATE TABLE devConfigs ("
      " firstLED INTEGER," // now x
      " numLEDs INTEGER," // now dx
      " y INTEGER,"
      " dy INTEGER,"
      " deviceconfig TEXT"
      ");"
    );
    // reached final version in one step
    aToVersion = LEDCHAINDEVICES_SCHEMA_VERSION;
  }
  else if (aFromVersion==1) {
    // V1->V2: groupNo added
    sql =
      "ALTER TABLE devConfigs ADD y INTEGER;"
      "ALTER TABLE devConfigs ADD dy INTEGER;";
    // reached version 2
    aToVersion = 2;
  }
  return sql;
}



LedChainVdc::LedChainVdc(int aInstanceNumber, StringVector aLedChainConfigs, VdcHost *aVdcHostP, int aTag) :
  Vdc(aInstanceNumber, aVdcHostP, aTag)
{
  ledArrangement.isMemberVariable();
  for (StringVector::iterator pos = aLedChainConfigs.begin(); pos!=aLedChainConfigs.end(); ++pos) {
    ledArrangement.addLEDChain(*pos);
  }
}


void LedChainVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  ErrorPtr err;
  // initialize root view
  PixelRect r = ledArrangement.totalCover();
  rootView = ViewStackPtr(new ViewStack);
  rootView->setFrame(r);
  rootView->setBackgroundColor(transparent);
  ledArrangement.setRootView(rootView);
  // initialize database
  string databaseName = getPersistentDataDir();
  string_format_append(databaseName, "%s_%d.sqlite3", vdcClassIdentifier(), getInstanceNumber());
  err = db.connectAndInitialize(databaseName.c_str(), LEDCHAINDEVICES_SCHEMA_VERSION, LEDCHAINDEVICES_SCHEMA_MIN_VERSION, aFactoryReset);
  // Initialize chain driver
  ledArrangement.begin(true);
  // done
  aCompletedCB(ErrorPtr());
}


Brightness LedChainVdc::getMinBrightness()
{
  // scale up according to scaled down maximum, and make it 0..100
  return ledArrangement.getMinVisibleColorIntensity()*100.0/(double)ledArrangement.getMaxOutValue();
}


bool LedChainVdc::hasWhite()
{
  // so far, only SK6812 have white
  return ledArrangement.hasWhite();
}



bool LedChainVdc::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("vdc_rgbchain", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


// vDC name
const char *LedChainVdc::vdcClassIdentifier() const
{
  return "LedChain_Device_Container";
}


LedChainDevicePtr LedChainVdc::addLedChainDevice(int aX, int aDx, int aY, int aDy, string aDeviceConfig)
{
  LedChainDevicePtr newDev;
  newDev = LedChainDevicePtr(new LedChainDevice(this, aX, aDx, aY, aDy, aDeviceConfig));
  // add to container if device was created
  if (newDev) {
    // add to container
    simpleIdentifyAndAddDevice(newDev);
    // add to view
    rootView->setPositioningMode(P44View::noAdjust);
    rootView->pushView(newDev->lightView);
    // - re-render
    ledArrangement.render();
    return boost::dynamic_pointer_cast<LedChainDevice>(newDev);
  }
  // none added
  return LedChainDevicePtr();
}


void LedChainVdc::removeDevice(DevicePtr aDevice, bool aForget)
{
  LedChainDevicePtr dev = boost::dynamic_pointer_cast<LedChainDevice>(aDevice);
  if (dev) {
    // - remove single device from superclass
    inherited::removeDevice(aDevice, aForget);
    // - remove device's view
    rootView->removeView(dev->lightView);
    // - re-render
    ledArrangement.render();
  }
}


void LedChainVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  // incrementally collecting static devices makes no sense. The devices are "static"!
  if (!(aRescanFlags & rescanmode_incremental)) {
    // non-incremental, re-collect all devices
    removeDevices(aRescanFlags & rescanmode_clearsettings);
    // then add those from the DB
    sqlite3pp::query qry(db);
    if (qry.prepare("SELECT rowid, firstLED, numLEDs, y, dy, deviceconfig FROM devConfigs ORDER BY firstLED,y")==SQLITE_OK) {
      for (sqlite3pp::query::iterator i = qry.begin(); i != qry.end(); ++i) {
        LedChainDevicePtr dev = addLedChainDevice(i->get<int>(1), i->get<int>(2), i->get<int>(3), i->get<int>(4), i->get<string>(5));
        dev->ledChainDeviceRowID = i->get<int>(0);
      }
    }
  }
  // assume ok
  aCompletedCB(ErrorPtr());
}


ErrorPtr LedChainVdc::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="x-p44-addDevice") {
    // add a new LED chain segment device
    ApiValuePtr o;
    int x, dx;
    int y = 0;
    int dy = 1;
    respErr = checkParam(aParams, "x", o);
    if (Error::isOK(respErr)) {
      x = o->int32Value();
      respErr = checkParam(aParams, "dx", o);
      if (Error::isOK(respErr)) {
        dx = o->int32Value();
        string uid;
        respErr = checkStringParam(aParams, "uniqueId", uid);
        if (Error::isOK(respErr)) {
          string cfg;
          respErr = checkStringParam(aParams, "deviceConfig", cfg);
          if (Error::isOK(respErr)) {
            string deviceConfig = "#"+uid+':'+cfg;
            // optional y position and size
            o = aParams->get("y");
            if (o) y = o->int32Value();
            o = aParams->get("dy");
            if (o) dy = o->int32Value();
            // optional name
            string name;
            checkStringParam(aParams, "name", name);
            // try to create device
            LedChainDevicePtr dev = addLedChainDevice(x, dx, y, dy, deviceConfig);
            if (!dev) {
              respErr = WebError::webErr(500, "invalid configuration for LedChain device -> none created");
            }
            else {
              // set name
              if (name.size()>0) dev->setName(name);
              // insert into database
              if(db.executef(
                "INSERT OR REPLACE INTO devConfigs (firstLED, numLEDs, y, dy, deviceconfig) VALUES (%d, %d, %d, %d, '%q')",
                x, dx, y, dy, deviceConfig.c_str()
              )!=SQLITE_OK) {
                respErr = db.error("saving LED chain segment params");
              }
              else {
                dev->ledChainDeviceRowID = db.last_insert_rowid();
                // confirm
                ApiValuePtr r = aRequest->newApiValue();
                r->setType(apivalue_object);
                r->add("dSUID", r->newBinary(dev->dSUID.getBinary()));
                r->add("rowid", r->newUint64(dev->ledChainDeviceRowID));
                r->add("name", r->newString(dev->getName()));
                aRequest->sendResult(r);
                respErr.reset(); // make sure we don't send an extra ErrorOK
              }
            }
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

#endif // ENABLE_LEDCHAIN



