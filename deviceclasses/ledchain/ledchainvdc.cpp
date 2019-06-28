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
#define LEDCHAINDEVICES_SCHEMA_MIN_VERSION 1 // minimally supported version, anything older will be deleted
#define LEDCHAINDEVICES_SCHEMA_VERSION 1 // current version

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
      " firstLED INTEGER,"
      " numLEDs INTEGER,"
      " deviceconfig TEXT"
      ");"
    );
    // reached final version in one step
    aToVersion = LEDCHAINDEVICES_SCHEMA_VERSION;
  }
  return sql;
}



LedChainVdc::LedChainVdc(int aInstanceNumber, const string aChainSpec, VdcHost *aVdcHostP, int aTag) :
  Vdc(aInstanceNumber, aVdcHostP, aTag),
  renderStart(0),
  renderEnd(0),
  maxOutValue(128) // by default, allow only half of max intensity (for full intensity a ~200 LED chain needs 70W power supply!)
{
  // parse chain specification
  // Syntax: [chaintype:[leddevicename:]]numberOfLeds
  ledType = LEDChainComm::ledtype_ws281x; // assume WS2812/13
  string chaintype;
  string rest = aChainSpec;
  if (keyAndValue(rest, chaintype, rest, ':')) {
    // chain type specified
    if (chaintype=="SK6812") {
      ledType = LEDChainComm::ledtype_sk6812;
    }
    else if (chaintype=="P9823") {
      ledType = LEDChainComm::ledtype_p9823;
    }
    // there might be a LED device name
    keyAndValue(rest, ledChainDevice, rest, ':');
  }
  // now there should be a number of LEDs
  if (sscanf(rest.c_str(), "%d", &numLedsInChain)!=1) {
    numLedsInChain = 200; // default
  }
}


void LedChainVdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  ErrorPtr err;
  // initialize database
  string databaseName = getPersistentDataDir();
  string_format_append(databaseName, "%s_%d.sqlite3", vdcClassIdentifier(), getInstanceNumber());
  err = db.connectAndInitialize(databaseName.c_str(), LEDCHAINDEVICES_SCHEMA_VERSION, LEDCHAINDEVICES_SCHEMA_MIN_VERSION, aFactoryReset);
  // Initialize chain driver
  ws281xcomm = LEDChainCommPtr(new LEDChainComm(ledType, ledChainDevice, numLedsInChain));
  ws281xcomm->begin();
  // trigger a full chain rendering
  triggerRenderingRange(0, numLedsInChain);
  // done
  aCompletedCB(ErrorPtr());
}


#define MIN_RENDER_INTERVAL (5*MilliSecond)

void LedChainVdc::triggerRenderingRange(uint16_t aFirst, uint16_t aNum)
{
  if (!renderTicket) {
    // no rendering pending, initialize range
    renderStart = aFirst;
    renderEnd = aFirst+aNum;
  }
  else {
    // enlarge range
    if (aFirst<renderStart) renderStart = aFirst;
    if (aFirst+aNum>renderEnd) renderEnd = aFirst+aNum;
  }
  if (!renderTicket) {
    renderTicket.executeOnce(boost::bind(&LedChainVdc::render, this), MIN_RENDER_INTERVAL);
  }
}


Brightness LedChainVdc::getMinBrightness()
{
  // scale up according to scaled down maximum, and make it 0..100
  return ws281xcomm->getMinVisibleColorIntensity()*100.0/(double)maxOutValue;
}


bool LedChainVdc::hasWhite()
{
  // so far, only SK6812 have white
  return ledType==LEDChainComm::ledtype_sk6812;
}



static inline void increase(uint8_t &aByte, uint8_t aAmount, uint8_t aMax = 255)
{
  uint16_t r = aByte+aAmount;
  if (r>aMax)
    aByte = aMax;
  else
    aByte = (uint8_t)r;
}


void LedChainVdc::render()
{
  renderTicket = 0; // done
  for (uint16_t i=renderStart; i<renderEnd; i++) {
    // TODO: optimize asking only devices active in this range
    // for now, just ask all
    uint8_t r, g, b, w;
    uint8_t rv=0, gv=0, bv=0, wv=0; // composed
    for (LedChainDeviceList::iterator pos = sortedSegments.begin(); pos!=sortedSegments.end(); ++pos) {
      double opacity = (*pos)->getLEDColor(i, r, g, b, w);
      if (opacity>0) {
        increase(rv, opacity*r);
        increase(gv, opacity*g);
        increase(bv, opacity*b);
        increase(wv, opacity*w);
      }
    }
    ws281xcomm->setColorDimmed(i, rv, gv, bv, wv, maxOutValue); // not more than maximum brightness allowed
  }
  // transfer to hardware
  ws281xcomm->show();
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


// Binary predicate that, taking two values of the same type of those contained in the list,
// returns true if the first argument goes before the second argument in the strict weak ordering
// it defines, and false otherwise.
bool LedChainVdc::segmentCompare(LedChainDevicePtr aFirst, LedChainDevicePtr aSecond)
{
  return aFirst->firstLED < aSecond->firstLED;
}


LedChainDevicePtr LedChainVdc::addLedChainDevice(uint16_t aFirstLED, uint16_t aNumLEDs, string aDeviceConfig)
{
  LedChainDevicePtr newDev;
  newDev = LedChainDevicePtr(new LedChainDevice(this, aFirstLED, aNumLEDs, aDeviceConfig));
  // add to container if device was created
  if (newDev) {
    // add to container
    simpleIdentifyAndAddDevice(newDev);
    // add to my list and sort
    sortedSegments.push_back(newDev);
    sortedSegments.sort(segmentCompare);
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
    // - remove device from sorted segments list
    for (LedChainDeviceList::iterator pos = sortedSegments.begin(); pos!=sortedSegments.end(); ++pos) {
      if (*pos==aDevice) {
        sortedSegments.erase(pos);
        triggerRenderingRange(0,numLedsInChain); // fully re-render to remove deleted light immediately
        break;
      }
    }
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
    if (qry.prepare("SELECT firstLED, numLEDs, deviceconfig, rowid FROM devConfigs ORDER BY firstLED")==SQLITE_OK) {
      for (sqlite3pp::query::iterator i = qry.begin(); i != qry.end(); ++i) {
        LedChainDevicePtr dev =addLedChainDevice(i->get<int>(0), i->get<int>(1), i->get<string>(2));
        dev->ledChainDeviceRowID = i->get<int>(3);
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
    string deviceConfig;
    ApiValuePtr o;
    uint16_t firstLED, numLEDs;
    respErr = checkParam(aParams, "firstLED", o);
    if (Error::isOK(respErr)) {
      firstLED = o->int16Value();
      respErr = checkParam(aParams, "numLEDs", o);
      if (Error::isOK(respErr)) {
        numLEDs = o->int16Value();
        respErr = checkStringParam(aParams, "deviceConfig", deviceConfig);
        if (Error::isOK(respErr)) {
          // optional name
          string name;
          checkStringParam(aParams, "name", name);
          // try to create device
          LedChainDevicePtr dev = addLedChainDevice(firstLED, numLEDs, deviceConfig);
          if (!dev) {
            respErr = WebError::webErr(500, "invalid configuration for LedChain device -> none created");
          }
          else {
            // set name
            if (name.size()>0) dev->setName(name);
            // insert into database
            if(db.executef(
              "INSERT OR REPLACE INTO devConfigs (firstLED, numLEDs, deviceconfig) VALUES (%d, %d,'%q')",
              firstLED, numLEDs, deviceConfig.c_str()
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
  else {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }
  return respErr;
}

#endif // ENABLE_LEDCHAIN



