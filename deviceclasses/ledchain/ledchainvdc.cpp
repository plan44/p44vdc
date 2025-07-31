//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2015-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
//  3 : Add zorder
#define LEDCHAINDEVICES_SCHEMA_MIN_VERSION 1 // minimally supported version, anything older will be deleted
#define LEDCHAINDEVICES_SCHEMA_VERSION 3 // current version

string LedChainDevicePersistence::schemaUpgradeSQL(int aFromVersion, int &aToVersion)
{
  string sql;
  if (aFromVersion==0) {
    // create table group from scratch
    // - use standard globs table for schema version
    sql = inherited::schemaUpgradeSQL(aFromVersion, aToVersion);
    // - create my tables
    sql.append(
      "DROP TABLE IF EXISTS $PREFIX_devConfigs;"
      "CREATE TABLE $PREFIX_devConfigs ("
      " firstLED INTEGER," // now x
      " numLEDs INTEGER," // now dx
      " y INTEGER,"
      " dy INTEGER,"
      " zorder INTEGER,"
      " deviceconfig TEXT"
      ");"
    );
    // reached final version in one step
    aToVersion = LEDCHAINDEVICES_SCHEMA_VERSION;
  }
  else if (aFromVersion==1) {
    // V1->V2: y/dy added
    sql =
      "ALTER TABLE $PREFIX_devConfigs ADD y INTEGER;"
      "ALTER TABLE $PREFIX_devConfigs ADD dy INTEGER;";
    // reached version 2
    aToVersion = 2;
  }
  else if (aFromVersion==2) {
    // V2->V3: zorder added
    sql =
      "ALTER TABLE $PREFIX_devConfigs ADD zorder INTEGER;";
    // reached version 3
    aToVersion = 3;
  }
  return sql;
}



LedChainVdc::LedChainVdc(int aInstanceNumber, LEDChainArrangementPtr aLedArrangement, VdcHost *aVdcHostP, int aTag) :
  Vdc(aInstanceNumber, aVdcHostP, aTag),
  mLedArrangement(aLedArrangement)
{
}


void LedChainVdc::setLogLevelOffset(int aLogLevelOffset)
{
  if (mLedArrangement) mLedArrangement->setLogLevelOffset(aLogLevelOffset);
  inherited::setLogLevelOffset(aLogLevelOffset);
}


P44LoggingObj* LedChainVdc::getTopicLogObject(const string aTopic)
{
  if (uequals(aTopic,"ledarrangement")) return mLedArrangement.get();
  // unknown at this level
  return inherited::getTopicLogObject(aTopic);
}


void LedChainVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  ErrorPtr err;
  // load persistent params for dSUID
  load();
  // initialize root view
  if (mLedArrangement) {
    PixelRect r = mLedArrangement->totalCover();
    mRootView = ViewStackPtr(new ViewStack);
    mRootView->setFrame(r);
    mRootView->setBackgroundColor(black); // stack with black background is more efficient (and there's nothing below, anyway)
    mLedArrangement->setRootView(mRootView);
    // initialize persistence
    err = initializePersistence(mDb, LEDCHAINDEVICES_SCHEMA_VERSION, LEDCHAINDEVICES_SCHEMA_MIN_VERSION);
    // Initialize chain driver
    mLedArrangement->begin(true);
  }
  // done
  if (!getVdcFlag(vdcflag_flagsinitialized)) setVdcFlag(vdcflag_hidewhenempty, true); // hide by default
  aCompletedCB(err);
}


Brightness LedChainVdc::getMinBrightness()
{
  // scale up according to scaled down maximum, and make it 0..100
  return mLedArrangement ? mLedArrangement->getMinVisibleColorIntensity()*100.0/255.0 : 0;
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


LedChainDevicePtr LedChainVdc::addLedChainDevice(int aX, int aDx, int aY, int aDy, int aZOrder, string aDeviceConfig)
{
  LedChainDevicePtr newDev;
  P44View::FramingMode autoadjust = P44View::noFraming; // not noAdjust, we DO want content resizing with frame
  if (aDx==0) autoadjust |= P44View::fillX;
  if (aDy==0) autoadjust |= P44View::fillY;
  newDev = LedChainDevicePtr(new LedChainDevice(this, aX, aDx, aY, aDy, aDeviceConfig));
  // add to container if device was created
  if (newDev) {
    // add to container
    simpleIdentifyAndAddDevice(newDev);
    // add to view
    if (newDev->mLightView->getZOrder()==0) newDev->mLightView->setZOrder(aZOrder);
    newDev->mLightView->setAutoAdjust(autoadjust);
    mRootView->setPositioningMode(P44View::noAdjust);
    mRootView->pushView(newDev->mLightView);
    // - re-render
    if (mLedArrangement) mLedArrangement->render();
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
    mRootView->removeView(dev->mLightView);
    // - re-render
    if (mLedArrangement) mLedArrangement->render();
  }
}


void LedChainVdc::scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags)
{
  // incrementally collecting static devices makes no sense. The devices are "static"!
  if (!(aRescanFlags & rescanmode_incremental)) {
    // non-incremental, re-collect all devices
    removeDevices(aRescanFlags & rescanmode_clearsettings);
    // then add those from the DB
    SQLiteTGQuery qry(mDb);
    if (Error::isOK(qry.prefixedPrepare("SELECT rowid, firstLED, numLEDs, y, dy, zorder, deviceconfig FROM $PREFIX_devConfigs ORDER BY zorder,rowid"))) {
      for (sqlite3pp::query::iterator i = qry.begin(); i != qry.end(); ++i) {
        int zorder;
        long long rowid = i->get<int>(0);
        if (!i->getIfNotNull(5, zorder)) {
          zorder = (int)rowid;
        }
        LedChainDevicePtr dev = addLedChainDevice(i->get<int>(1), i->get<int>(2), i->get<int>(3), i->get<int>(4), zorder, i->get<string>(6));
        dev->mLedChainDeviceRowID = rowid;
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
    // add a new LED chain device
    int x = 0;
    int y = 0;
    int dx = 0;
    int dy = 0;
    ApiValuePtr o;
    o = aParams->get("autosize");
    if (!o || !o->boolValue()) {
      // fixed size
      dy = 1;
      respErr = checkParam(aParams, "x", o);
      if (Error::isOK(respErr)) {
        x = o->int32Value();
        respErr = checkParam(aParams, "dx", o);
        if (Error::isOK(respErr)) {
          dx = o->int32Value();
          // optional y position and size
          o = aParams->get("y");
          if (o) y = o->int32Value();
          o = aParams->get("dy");
          if (o) dy = o->int32Value();
        }
      }
    }
    if (Error::isOK(respErr)) {
      string uid;
      respErr = checkStringParam(aParams, "uniqueId", uid);
      if (Error::isOK(respErr)) {
        string cfg;
        respErr = checkStringParam(aParams, "deviceConfig", cfg);
        if (Error::isOK(respErr)) {
          string deviceConfig = "#"+uid+':'+cfg;
          // optional name
          string name;
          checkStringParam(aParams, "name", name);
          // optional z-order
          int zorder = 0;
          o = aParams->get("z_order");
          if (o) zorder = o->int32Value();
          // try to create device
          LedChainDevicePtr dev = addLedChainDevice(x, dx, y, dy, zorder, deviceConfig);
          if (!dev) {
            respErr = WebError::webErr(500, "invalid configuration for LedChain device -> none created");
          }
          else {
            // set name
            if (name.size()>0) dev->setName(name);
            // insert into database
            respErr = mDb.prefixedExecute(
              "INSERT OR REPLACE INTO $PREFIX_devConfigs (firstLED, numLEDs, y, dy, zorder, deviceconfig) VALUES (%d, %d, %d, %d, %d, '%q')",
              x, dx, y, dy, zorder, deviceConfig.c_str()
            );
            if (Error::isOK(respErr)) {
              dev->mLedChainDeviceRowID = mDb.db().last_insert_rowid();
              // confirm
              ApiValuePtr r = aRequest->newApiValue();
              r->setType(apivalue_object);
              r->add("dSUID", r->newBinary(dev->mDSUID.getBinary()));
              r->add("rowid", r->newUint64(dev->mLedChainDeviceRowID));
              r->add("name", r->newString(dev->getName()));
              aRequest->sendResult(r);
              respErr.reset(); // make sure we don't send an extra ErrorOK
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



