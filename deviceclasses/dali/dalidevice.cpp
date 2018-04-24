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


#include "dalidevice.hpp"

#if ENABLE_DALI

#include "dalivdc.hpp"
#include "colorlightbehaviour.hpp"

#if ENABLE_DALI_INPUTS
#include "buttonbehaviour.hpp"
#include "sensorbehaviour.hpp"
#include "binaryinputbehaviour.hpp"
#endif

#include <math.h>


using namespace p44;


// MARK: ===== DaliBusDevice

DaliBusDevice::DaliBusDevice(DaliVdc &aDaliVdc) :
  daliVdc(aDaliVdc),
  dimRepeaterTicket(0),
  isDummy(false),
  isPresent(false),
  lampFailure(false),
  currentTransitionTime(Infinite), // invalid
  currentDimPerMS(0), // none
  currentFadeRate(0xFF), currentFadeTime(0xFF), // unlikely values
  supportsLED(false),
  dt6LinearDim(false),
  supportsDT8(false),
  dt8Color(false),
  dt8CT(false),
  currentColorMode(colorLightModeNone),
  currentXorCT(0),
  currentY(0),
  currentR(0),
  currentG(0),
  currentB(0),
  currentW(0),
  currentA(0)
{
  // make sure we always have at least a dummy device info
  deviceInfo = DaliDeviceInfoPtr(new DaliDeviceInfo);
}


/* Snippet to show brightness/DALI arcpower conversion tables in both directions

  dt6LinearDim = false;
  printf("\n\n===== Arcpower 0..254 -> brightness\n");
  for (int a=0; a<255; a++) {
    double b = arcpowerToBrightness(a);
    printf("arcpower = %3d/0x%02X  -> brightness = %3d/0x%02X = dS-Brightness = %7.3f%%\n", a, a, (int)(b*2.54), (int)(b*2.54), b);
  }
  printf("\n\n===== Brightness byte 0..254 -> arcpower 0..254\n");
  for (int k=0; k<255; k++) {
    double b = (double)k/2.54;
    uint8_t a = brightnessToArcpower(b);
    printf("dS-Brightness = %7.3f%% == brightness = %3d/0x%02X -> arcpower = %3d/0x%02X\n", b, k, k, a, a);
  }

  printf("\n\n---- for Numbers\n");
  for (int a=0; a<255; a++) {
    double b = arcpowerToBrightness(a);
    int a2 = brightnessToArcpower(b);
    printf("%d\t%f\t%d\n", a, b, a2);
  }

*/


string DaliBusDevice::description()
{
  string s = deviceInfo->description();
  if (supportsLED) {
    s += "\n- supports device type 6 (LED)";
    if (dt6LinearDim) {
      s += " -> using linear dimming curve";
    }
  }
  if (supportsDT8) string_format_append(s, "\n- supports device type 8 (color), features:%s%s [RGBWAF:%d] [Primary Colors:%d]", dt8CT ? " [Tunable white]" : "", dt8Color ? " [CIE x/y]" : "", dt8RGBWAFchannels, dt8RPrimaryColors);
  return s;
}



void DaliBusDevice::setDeviceInfo(DaliDeviceInfoPtr aDeviceInfo)
{
  // store the info record
  if (!aDeviceInfo)
    aDeviceInfo = DaliDeviceInfoPtr(new DaliDeviceInfo); // always have one, even if it's only a dummy!
  deviceInfo = aDeviceInfo; // assign
  deriveDsUid(); // derive dSUID from it
}


void DaliBusDevice::clearDeviceInfo()
{
  deviceInfo->clear();
  deriveDsUid();
}


void DaliBusDevice::deriveDsUid()
{
  if (isDummy) return;
  // vDC implementation specific UUID:
  #if OLD_BUGGY_CHKSUM_COMPATIBLE
  if (deviceInfo->devInfStatus==DaliDeviceInfo::devinf_maybe) {
    // assume we can use devInf to derive dSUID from
    deviceInfo->devInfStatus = DaliDeviceInfo::devinf_solid;
    // but only actually use it if there is no device entry for the shortaddress-based dSUID with a non-zero name
    // (as this means the device has been already actively used/configured with the shortaddr-dSUID)
    // - calculate the short address based dSUID
    DsUid shortAddrBasedDsUid;
    dsUidForDeviceInfoStatus(shortAddrBasedDsUid, DaliDeviceInfo::devinf_notForID);
    // - check for named device in database consisting of this dimmer with shortaddr based dSUID
    //   Note that only single dimmer device are checked for, composite devices will not have this compatibility mechanism
    sqlite3pp::query qry(daliVdc.getVdcHost().getDsParamStore());
    // Note: this is a bit ugly, as it has the device settings table name hard coded
    string sql = string_format("SELECT deviceName FROM DeviceSettings WHERE parentID='%s'", shortAddrBasedDsUid.getString().c_str());
    if (qry.prepare(sql.c_str())==SQLITE_OK) {
      sqlite3pp::query::iterator i = qry.begin();
      if (i!=qry.end()) {
        // the length of the name
        string n = nonNullCStr(i->get<const char *>(0));
        if (n.length()>0) {
          // shortAddr based device has already been named. So keep that, and don't generate a devInf based dSUID
          deviceInfo->devInfStatus = DaliDeviceInfo::devinf_notForID;
          LOG(LOG_WARNING, "DaliBusDevice shortaddr %d kept with shortaddr-based dSUID because it is already named: '%s'", deviceInfo->shortAddress, n.c_str());
        }
      }
    }
  }
  #endif // OLD_BUGGY_CHKSUM_COMPATIBLE
  dsUidForDeviceInfoStatus(dSUID, deviceInfo->devInfStatus);
}


void DaliBusDevice::dsUidForDeviceInfoStatus(DsUid &aDsUid, DaliDeviceInfo::DaliDevInfStatus aDevInfStatus)
{
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  string s;
  if (aDevInfStatus==DaliDeviceInfo::devinf_solid) {
    // uniquely identified by GTIN+Serial, but unknown partition value:
    // - Proceed according to dS rule 2:
    //   "vDC can determine GTIN and serial number of Device → combine GTIN and
    //    serial number to form a GS1-128 with Application Identifier 21:
    //    "(01)<GTIN>(21)<serial number>" and use the resulting string to
    //    generate a UUIDv5 in the GS1-128 name space"
    s = string_format("(01)%llu(21)%llu", deviceInfo->gtin, deviceInfo->serialNo);
  }
  else {
    // not uniquely identified by devInf (or shortaddr based version already in use):
    // - generate id in vDC namespace
    //   UUIDv5 with name = classcontainerinstanceid::daliShortAddrDecimal
    s = daliVdc.vdcInstanceIdentifier();
    string_format_append(s, "::%d", deviceInfo->shortAddress);
  }
  aDsUid.setNameInSpace(s, vdcNamespace);
}




void DaliBusDevice::registerDeviceType(uint8_t aDeviceType)
{
  LOG(LOG_INFO, "DALI bus device with shortaddr %d supports device type %d", deviceInfo->shortAddress, aDeviceType);
  switch (aDeviceType) {
    case 6: // DALI DT6 is LED support
      supportsLED = true;
      break;
    case 8: // DALI DT8 is color support
      supportsDT8 = true;
      break;
  }
}



void DaliBusDevice::queryFeatureSet(StatusCB aCompletedCB)
{
  // query device type(s) - i.e. availability of extended command sets
  daliVdc.daliComm->daliSendQuery(
    deviceInfo->shortAddress,
    DALICMD_QUERY_DEVICE_TYPE,
    boost::bind(&DaliBusDevice::deviceTypeResponse, this, aCompletedCB, _1, _2, _3)
  );
}


void DaliBusDevice::deviceTypeResponse(StatusCB aCompletedCB, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
{
  // device type query response.
  if (Error::isOK(aError) && !aNoOrTimeout) {
    // special case is 0xFF, which means device supports multiple types
    if (aResponse==0xFF) {
      // need to query all possible types
      probeDeviceType(aCompletedCB, 0); // start with 0
      return;
    }
    // check device type
    registerDeviceType(aResponse);
  }
  // done with device type, check groups now
  queryDTFeatures(aCompletedCB);
}


void DaliBusDevice::probeDeviceType(StatusCB aCompletedCB, uint8_t aNextDT)
{
  if (aNextDT>10) {
    // all device types checked
    // done with device type, check groups now
    queryDTFeatures(aCompletedCB);
    return;
  }
  // query next device type
  daliVdc.daliComm->daliSend(DALICMD_ENABLE_DEVICE_TYPE, aNextDT);
  daliVdc.daliComm->daliSendQuery(
    deviceInfo->shortAddress,
    DALICMD_QUERY_EXTENDED_VERSION,
    boost::bind(&DaliBusDevice::probeDeviceTypeResponse, this, aCompletedCB, aNextDT, _1, _2, _3)
  );
}


void DaliBusDevice::probeDeviceTypeResponse(StatusCB aCompletedCB, uint8_t aNextDT, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
{
  // extended version type query response.
  if (Error::isOK(aError) && !aNoOrTimeout) {
    // extended version response
    // check device type
    registerDeviceType(aNextDT);
  }
  // query next device type
  aNextDT++;
  probeDeviceType(aCompletedCB, aNextDT);
}


void DaliBusDevice::queryDTFeatures(StatusCB aCompletedCB)
{
  if (supportsDT8) {
    daliVdc.daliComm->daliSendQuery(
      deviceInfo->shortAddress,
      DALICMD_DT8_QUERY_COLOR_FEATURES,
      boost::bind(&DaliBusDevice::dt8FeaturesResponse, this, aCompletedCB, _1, _2, _3)
    );
    return;
  }
  if (aCompletedCB) aCompletedCB(ErrorPtr());
}


void DaliBusDevice::dt8FeaturesResponse(StatusCB aCompletedCB, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
{
  // extended version type query response.
  if (Error::isOK(aError) && !aNoOrTimeout) {
    // DT8 features response
    dt8Color = (aResponse & 0x01)!=0; // x/y color model capable
    dt8CT = (aResponse & 0x02)!=0; // mired color temperature capable
    dt8RPrimaryColors = (aResponse>>2) & 0x07; // bits 2..4 is the number of primary color channels available
    dt8RGBWAFchannels = (aResponse>>5) & 0x07; // bits 5..7 is the number of RGBWAF channels available
    LOG(LOG_INFO, "- DALI DT8 bus device with shortaddr %d: features byte = 0x%02X", deviceInfo->shortAddress, aResponse);
  }
  if (aCompletedCB) aCompletedCB(ErrorPtr());
}




void DaliBusDevice::getGroupMemberShip(DaliGroupsCB aDaliGroupsCB, DaliAddress aShortAddress)
{
  daliVdc.daliComm->daliSendQuery(
    aShortAddress,
    DALICMD_QUERY_GROUPS_0_TO_7,
    boost::bind(&DaliBusDevice::queryGroup0to7Response, this, aDaliGroupsCB, aShortAddress, _1, _2, _3)
  );
}


void DaliBusDevice::queryGroup0to7Response(DaliGroupsCB aDaliGroupsCB, DaliAddress aShortAddress, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
{
  uint16_t groupBitMask = 0; // no groups yet
  if (Error::isOK(aError) && !aNoOrTimeout) {
    groupBitMask = aResponse;
  }
  // anyway, query other half
  daliVdc.daliComm->daliSendQuery(
    aShortAddress,
    DALICMD_QUERY_GROUPS_8_TO_15,
    boost::bind(&DaliBusDevice::queryGroup8to15Response, this, aDaliGroupsCB, aShortAddress, groupBitMask, _1, _2, _3)
  );
}


void DaliBusDevice::queryGroup8to15Response(DaliGroupsCB aDaliGroupsCB, DaliAddress aShortAddress, uint16_t aGroupBitMask, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
{
  // group 8..15 membership result
  if (Error::isOK(aError) && !aNoOrTimeout) {
    aGroupBitMask |= ((uint16_t)aResponse)<<8;
  }
  if (aDaliGroupsCB) aDaliGroupsCB(aGroupBitMask, aError);
}



void DaliBusDevice::initialize(StatusCB aCompletedCB, uint16_t aUsedGroupsMask)
{
  // make sure device is in none of the used groups
  if (aUsedGroupsMask==0) {
    // no groups in use at all, continue to initializing features
    initializeFeatures(aCompletedCB);
    return;
  }
  // need to query current groups
  getGroupMemberShip(boost::bind(&DaliBusDevice::groupMembershipResponse, this, aCompletedCB, aUsedGroupsMask, deviceInfo->shortAddress, _1, _2), deviceInfo->shortAddress);
}


void DaliBusDevice::checkGroupMembership(StatusCB aCompletedCB, uint16_t aUsedGroupsMask)
{
  // make sure device is in none of the used groups
  if (aUsedGroupsMask==0) {
    // no groups in use at all, just return
    if (aCompletedCB) aCompletedCB(ErrorPtr());
    return;
  }
  // need to query current groups
  getGroupMemberShip(boost::bind(&DaliBusDevice::groupMembershipResponse, this, aCompletedCB, aUsedGroupsMask, deviceInfo->shortAddress, _1, _2), deviceInfo->shortAddress);
}


void DaliBusDevice::groupMembershipResponse(StatusCB aCompletedCB, uint16_t aUsedGroupsMask, DaliAddress aShortAddress, uint16_t aGroups, ErrorPtr aError)
{
  // remove groups that are in use on the bus
  if (Error::isOK(aError)) {
    for (int g=0; g<16; ++g) {
      if (aUsedGroupsMask & aGroups & (1<<g)) {
        // single device is member of a group in use -> remove it
        LOG(LOG_INFO, "- removing single DALI bus device with shortaddr %d from group %d", aShortAddress, g);
        daliVdc.daliComm->daliSendConfigCommand(aShortAddress, DALICMD_REMOVE_FROM_GROUP|g);
      }
    }
  }
  // initialize features now
  initializeFeatures(aCompletedCB);
}



void DaliBusDevice::initializeFeatures(StatusCB aCompletedCB)
{
  if (isDummy) {
    if (aCompletedCB) aCompletedCB(ErrorPtr());
    return;
  }
  /* TODO: For now, we don't use linear dimming curve because the only unit we've seen with DT6 support is flawed,
     (standard dimming up/down commands no longer work when linear dimming is enabled, and minimum/current arcpower readout is wrong).
     On the other side, there's not much benefit from linear dimming curve (now our log dimming curve is finally correct...).
  // initialize DT6 linear dimming curve if available
  if (supportsLED) {
    // single device or group supports DT6 -> use linear dimming curve
    daliVdc.daliComm->daliSendDtrAndConfigCommand(deviceInfo->shortAddress, DALICMD_DT6_SELECT_DIMMING_CURVE, 1); // linear dimming curve
    dt6LinearDim = true;
  }
  else {
    if (isGrouped()) {
      // not all (or maybe none) of the devices in the group support DT6 -> make sure all other devices use standard dimming curve even if they know DT6
      // Note: non DT6-devices will just ignore the following command
      daliVdc.daliComm->daliSendDtrAndConfigCommand(deviceInfo->shortAddress, DALICMD_DT6_SELECT_DIMMING_CURVE, 0); // standard logarithmic dimming curve
      dt6LinearDim = false;
    }
  }
  */
  if (supportsLED) {
    // TODO: for now, make sure we use the standard dimming curve
    daliVdc.daliComm->daliSendDtrAndConfigCommand(deviceInfo->shortAddress, DALICMD_DT6_SELECT_DIMMING_CURVE, 0); // standard logarithmic dimming curve
    dt6LinearDim = false;
  }
  if (aCompletedCB) aCompletedCB(ErrorPtr());
}




void DaliBusDevice::updateParams(StatusCB aCompletedCB)
{
  if (isDummy) {
    aCompletedCB(ErrorPtr());
    return;
  }
  // query actual arc power level
  daliVdc.daliComm->daliSendQuery(
    addressForQuery(),
    DALICMD_QUERY_ACTUAL_LEVEL,
    boost::bind(&DaliBusDevice::queryActualLevelResponse, this, aCompletedCB, _1, _2, _3)
  );
}


void DaliBusDevice::queryActualLevelResponse(StatusCB aCompletedCB, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
{
  currentBrightness = 0; // default to 0
  if (Error::isOK(aError) && !aNoOrTimeout) {
    isPresent = true; // answering a query means presence
    // this is my current arc power, save it as brightness for dS system side queries
    currentBrightness = arcpowerToBrightness(aResponse);
    LOG(LOG_INFO, "DaliBusDevice: retrieved current dimming level: arc power = %d, brightness = %0.1f", aResponse, currentBrightness);
  }
  // next: query the minimum dimming level
  daliVdc.daliComm->daliSendQuery(
    addressForQuery(),
    DALICMD_QUERY_PHYSICAL_MINIMUM_LEVEL,
    boost::bind(&DaliBusDevice::queryMinLevelResponse, this, aCompletedCB, _1, _2, _3)
  );
}


void DaliBusDevice::queryMinLevelResponse(StatusCB aCompletedCB, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
{
  minBrightness = 0; // default to 0
  if (Error::isOK(aError) && !aNoOrTimeout) {
    isPresent = true; // answering a query means presence
    // this is my current arc power, save it as brightness for dS system side queries
    minBrightness = arcpowerToBrightness(aResponse);
    LOG(LOG_INFO, "DaliBusDevice: retrieved minimum dimming level: arc power = %d, brightness = %0.1f", aResponse, minBrightness);
  }
  if (supportsDT8) {
    // more queries on DT8 devices:
    // - color status
    daliVdc.daliComm->daliSendQuery(
      addressForQuery(),
      DALICMD_DT8_QUERY_COLOR_STATUS,
      boost::bind(&DaliBusDevice::queryColorStatusResponse, this, aCompletedCB, _1, _2, _3)
    );
    return;
  }
  aCompletedCB(aError);
}


void DaliBusDevice::queryColorStatusResponse(StatusCB aCompletedCB, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
{
  if (Error::isOK(aError) && !aNoOrTimeout) {
    // current mode
    if (aResponse & 0x10) {
      // CIE x/y is active
      currentColorMode = colorLightModeXY;
      // - query X
      daliVdc.daliComm->daliSendDtrAnd16BitQuery(
        addressForQuery(),
        DALICMD_DT8_QUERY_COLOR_VALUE, 0, // DTR==0 -> X coordinate
        boost::bind(&DaliBusDevice::queryXCoordResponse, this, aCompletedCB, _1, _2)
      );
      return;
    }
    else if (aResponse & 0x20) {
      // CT is active
      currentColorMode = colorLightModeCt;
      // - query CT
      daliVdc.daliComm->daliSendDtrAnd16BitQuery(
        addressForQuery(),
        DALICMD_DT8_QUERY_COLOR_VALUE, 2, // DTR==2 -> CT value
        boost::bind(&DaliBusDevice::queryCTResponse, this, aCompletedCB, _1, _2)
      );
      return;
    }
    // TODO: implement
//    else if (aResponse & 0x40) {
//      // Primary N is active
//      currentColorMode = colorLightModeCt;
//      // - query CT
//      daliVdc.daliComm->daliSendDtrAnd16BitQuery(
//        addressForQuery(),
//        DALICMD_DT8_QUERY_COLOR_VALUE, 2, // DTR==2 -> CT value
//        boost::bind(&DaliBusDevice::queryCTResponse,this, aCompletedCB, _1, _2)
//      );
//      return;
//    }
    else if (aResponse & 0x80) {
      // RGBWA(F) is active
      currentColorMode = colorLightModeRGBWA;
      currentW = 0;
      currentA = 0;
      // - query RGBWA (no F supported, WF optional, RGB mandatory)
      if (dt8RGBWAFchannels>=3) {
        daliVdc.daliComm->daliSendDtrAnd16BitQuery(
          addressForQuery(),
          DALICMD_DT8_QUERY_COLOR_VALUE, 233, // DTR==233..237 -> R,G,B,W,A Dimlevels
          boost::bind(&DaliBusDevice::queryRGBWAFResponse, this, aCompletedCB, 0, _1, _2)
        );
        return;
      }
    }
  }
  // no more queries
  aCompletedCB(aError);
}


void DaliBusDevice::queryXCoordResponse(StatusCB aCompletedCB, uint16_t aResponse16, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    if (aResponse16==0xFFFF) {
      currentColorMode = colorLightModeNone;
    }
    else {
      currentXorCT = aResponse16;
      // also query Y
      daliVdc.daliComm->daliSendDtrAnd16BitQuery(
        addressForQuery(),
        DALICMD_DT8_QUERY_COLOR_VALUE, 1, // DTR==1 -> Y coordinate
        boost::bind(&DaliBusDevice::queryYCoordResponse, this, aCompletedCB, _1, _2)
      );
      return;
    }
  }
  aCompletedCB(aError);
}


void DaliBusDevice::queryYCoordResponse(StatusCB aCompletedCB, uint16_t aResponse16, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    currentY = aResponse16;
    LOG(LOG_INFO, "DaliBusDevice: DT8 - is in CIE X/Y color mode, X=%.3f, Y=%.3f", (double)currentXorCT/65536, (double)currentY/65536);
  }
  aCompletedCB(aError);
}


void DaliBusDevice::queryCTResponse(StatusCB aCompletedCB, uint16_t aResponse16, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    if (aResponse16==0xFFFF) {
      currentColorMode = colorLightModeNone;
    }
    else {
      currentXorCT = aResponse16;
      LOG(LOG_INFO, "DaliBusDevice: DT8 - is in Tunable White mode, CT=%hd mired", currentXorCT);
    }
  }
  aCompletedCB(aError);
}


void DaliBusDevice::queryRGBWAFResponse(StatusCB aCompletedCB, uint16_t aResIndex, uint16_t aResponse16, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    // save answer
    aResponse16 = aResponse16>>8; // MSB contains 8-bit dim level data, LSB is always 0
    switch (aResIndex) {
      case 0 : currentR = aResponse16; break;
      case 1 : currentG = aResponse16; break;
      case 2 : currentB = aResponse16; break;
      case 3 : currentW = aResponse16; break;
      case 4 : currentA = aResponse16; break;
    }
  }
  else {
    LOG(LOG_DEBUG, "DaliBusDevice: querying DT8 color value %d returned error: %s", aResIndex, aError->description().c_str());
  }
  aResIndex++;
  if (aResIndex>=dt8RGBWAFchannels) {
    // all values queried
    LOG(LOG_INFO, "DaliBusDevice: DT8 - is in RGBWAF mode, R=%d, G=%d, B=%d, W=%d, A=%d", currentR, currentG, currentB, currentW, currentA);
  }
  else {
    // query next component
    daliVdc.daliComm->daliSendDtrAnd16BitQuery(
      addressForQuery(),
      DALICMD_DT8_QUERY_COLOR_VALUE, 233+aResIndex, // DTR==233..237 -> R,G,B,W,A Dimlevels
      boost::bind(&DaliBusDevice::queryRGBWAFResponse, this, aCompletedCB, aResIndex, _1, _2)
    );
    return;
  }
  aCompletedCB(aError);
}





void DaliBusDevice::updateStatus(StatusCB aCompletedCB)
{
  if (isDummy) {
    if (aCompletedCB) aCompletedCB(ErrorPtr());
    return;
  }
  // query the device for status
  daliVdc.daliComm->daliSendQuery(
    addressForQuery(),
    DALICMD_QUERY_STATUS,
    boost::bind(&DaliBusDevice::queryStatusResponse, this, aCompletedCB, _1, _2, _3)
  );
}


void DaliBusDevice::queryStatusResponse(StatusCB aCompletedCB, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
{
  if (Error::isOK(aError) && !aNoOrTimeout) {
    isPresent = true; // answering a query means presence
    // check status bits
    // - bit1 = lamp failure
    lampFailure = aResponse & 0x02;
  }
  else {
    isPresent = false; // no correct status -> not present
  }
  // done updating status
  if (aCompletedCB) aCompletedCB(aError);
}



void DaliBusDevice::setTransitionTime(MLMicroSeconds aTransitionTime)
{
  if (isDummy) return;
  if (currentTransitionTime==Infinite || currentTransitionTime!=aTransitionTime) {
    uint8_t tr = 0; // default to 0
    if (aTransitionTime>0) {
      // Fade time: T = 0.5 * SQRT(2^X) [seconds] -> x = ln2((T/0.5)^2) : T=0.25 [sec] -> x = -2, T=10 -> 8.64
      double h = (((double)aTransitionTime/Second)/0.5);
      h = h*h;
      h = log(h)/log(2);
      tr = h>1 ? (uint8_t)h : 1;
      LOG(LOG_DEBUG, "DaliDevice: new transition time = %.1f mS, calculated FADE_TIME setting = %f (rounded %d)", (double)aTransitionTime/MilliSecond, h, (int)tr);
    }
    if (tr!=currentFadeTime || currentTransitionTime==Infinite) {
      LOG(LOG_DEBUG, "DaliDevice: setting DALI FADE_TIME to %d", (int)tr);
      daliVdc.daliComm->daliSendDtrAndConfigCommand(deviceInfo->shortAddress, DALICMD_STORE_DTR_AS_FADE_TIME, tr);
      currentFadeTime = tr;
    }
    currentTransitionTime = aTransitionTime;
  }
}


void DaliBusDevice::setBrightness(Brightness aBrightness)
{
  if (isDummy) return;
  if (currentBrightness!=aBrightness) {
    currentBrightness = aBrightness;
    uint8_t power = brightnessToArcpower(aBrightness);
    LOG(LOG_INFO, "Dali dimmer at shortaddr=%d: setting new brightness = %0.2f, arc power = %d", (int)deviceInfo->shortAddress, aBrightness, (int)power);
    daliVdc.daliComm->daliSendDirectPower(deviceInfo->shortAddress, power);
  }
}


void DaliBusDevice::setDefaultBrightness(Brightness aBrightness)
{
  if (isDummy) return;
  if (aBrightness<0) aBrightness = currentBrightness; // use current brightness
  uint8_t power = brightnessToArcpower(aBrightness);
  LOG(LOG_INFO, "Dali dimmer at shortaddr=%d: setting default/failure brightness = %0.2f, arc power = %d", (int)deviceInfo->shortAddress, aBrightness, (int)power);
  daliVdc.daliComm->daliSendDtrAndConfigCommand(deviceInfo->shortAddress, DALICMD_STORE_DTR_AS_POWER_ON_LEVEL, power);
  daliVdc.daliComm->daliSendDtrAndConfigCommand(deviceInfo->shortAddress, DALICMD_STORE_DTR_AS_FAILURE_LEVEL, power);
}



bool DaliBusDevice::setColorParams(ColorLightMode aMode, double aCieXorCT, double aCieY)
{
  bool changed = false;
  if (supportsDT8) {
    if (currentColorMode!=aMode) {
      changed = true; // change in mode always means change in parameter
      currentColorMode = aMode;
    }
    if (currentColorMode==colorLightModeCt) {
      if (changed || currentXorCT!=aCieXorCT) {
        currentXorCT = aCieXorCT; // 1:1 in mired
        changed = true;
        currentY = 0;
        if (dt8CT) {
          daliVdc.daliComm->daliSend16BitValueAndCommand(deviceInfo->shortAddress, DALICMD_DT8_SET_TEMP_CT, currentXorCT);
        }
      }
    }
    else {
      uint16_t x = aCieXorCT*65536;
      uint16_t y = aCieY*65536;
      if (changed || currentXorCT!=x || currentY!=y) {
        currentXorCT = x;
        currentY = y;
        changed = true;
        if (dt8Color) {
          daliVdc.daliComm->daliSend16BitValueAndCommand(deviceInfo->shortAddress, DALICMD_DT8_SET_TEMP_XCOORD, currentXorCT);
          daliVdc.daliComm->daliSend16BitValueAndCommand(deviceInfo->shortAddress, DALICMD_DT8_SET_TEMP_YCOORD, currentY);
        }
      }
    }
  }
  return changed;
}


bool DaliBusDevice::setRGBWAParams(uint8_t aR, uint8_t aG, uint8_t aB, uint8_t aW, uint8_t aA)
{
  bool changed = false;
  if (supportsDT8) {
    if (currentColorMode!=colorLightModeRGBWA) {
      changed = true; // change in mode always means change in parameter
      currentColorMode = colorLightModeRGBWA;
    }
    if (changed || aR!=currentR || aG!=currentG || aB!=currentB) {
      currentR = aR;
      currentG = aG;
      currentB = aB;
      // set the mode (channel control)
      daliVdc.daliComm->daliSendDtrAndCommand(deviceInfo->shortAddress, DALICMD_DT8_SET_TEMP_RGBWAF_CTRL, 0x0<<6); // all not linked, channel control
      // set the color values
      daliVdc.daliComm->daliSend3x8BitValueAndCommand(deviceInfo->shortAddress, DALICMD_DT8_SET_TEMP_RGB, currentR, currentG, currentB);
      if (dt8RGBWAFchannels>3) {
        if (changed || aW!=currentW || aA!=currentA) {
          currentW = aW;
          currentA = aA;
          daliVdc.daliComm->daliSend3x8BitValueAndCommand(deviceInfo->shortAddress, DALICMD_DT8_SET_TEMP_WAF, currentW, currentA, 0); // no F
        }
      }
      changed = true;
    }
  }
  return changed;
}




void DaliBusDevice::activateColorParams()
{
  if (supportsDT8) {
    daliVdc.daliComm->daliSendCommand(deviceInfo->shortAddress, DALICMD_DT8_ACTIVATE);
  }
}




uint8_t DaliBusDevice::brightnessToArcpower(Brightness aBrightness)
{
  if (aBrightness<0) aBrightness = 0;
  if (aBrightness>100) aBrightness = 100;
  // 0..254, 255 is MASK and is reserved to stop fading
  if (aBrightness==0) return 0; // special case
  if (dt6LinearDim)
    return aBrightness*2.54; // linear 0..254
  else
    return (uint8_t)((double)(log10(aBrightness)+1.0)*(253.0/3)+1); // logarithmic
}



Brightness DaliBusDevice::arcpowerToBrightness(int aArcpower)
{
  if (aArcpower==0) return 0; // special off case
  if (dt6LinearDim)
    return (double)aArcpower/2.54; // linear 1..254
  else
    return pow(10, ((double)aArcpower-1)/(253.0/3)-1); // logarithmic
}





// MARK: ===== Optimized DALI dimming implementation



void DaliBusDevice::dimPrepare(VdcDimMode aDimMode, double aDimPerMS, StatusCB aCompletedCB)
{
  if (!isDummy && !aDimMode==dimmode_stop) {
    // - configure new fade rate if current does not match
    if (aDimPerMS!=currentDimPerMS) {
      currentDimPerMS = aDimPerMS;
      //   Fade rate: R = 506/SQRT(2^X) [steps/second] -> x = ln2((506/R)^2) : R=44 [steps/sec] -> x = 7
      double h = 506.0/(currentDimPerMS*1000);
      h = log(h*h)/log(2);
      uint8_t fr = h>0 ? (uint8_t)h : 0;
      LOG(LOG_DEBUG, "DaliDevice: new dimming rate = %f steps/second, calculated FADE_RATE setting = %f (rounded %d)", currentDimPerMS*1000, h, fr);
      if (fr!=currentFadeRate) {
        LOG(LOG_INFO, "DaliDevice shortaddr %d: setting DALI FADE_RATE to %d for dimming at %f steps/second", deviceInfo->shortAddress, fr, currentDimPerMS*1000);
        currentFadeRate = fr;
        daliVdc.daliComm->daliSendDtrAndConfigCommand(deviceInfo->shortAddress, DALICMD_STORE_DTR_AS_FADE_RATE, fr, boost::bind(&DaliBusDevice::dimPrepared, this, aCompletedCB, _1));
        return;
      }
    }
  }
  if (aCompletedCB) aCompletedCB(ErrorPtr());
}


void DaliBusDevice::dimPrepared(StatusCB aCompletedCB, ErrorPtr aError)
{
  if (aCompletedCB) aCompletedCB(aError);
}



void DaliBusDevice::dim(VdcDimMode aDimMode, double aDimPerMS)
{
  if (isDummy) return;
  MainLoop::currentMainLoop().cancelExecutionTicket(dimRepeaterTicket); // stop any previous dimming activity
  // Use DALI UP/DOWN dimming commands
  if (aDimMode==dimmode_stop) {
    // stop dimming - send MASK
    daliVdc.daliComm->daliSendDirectPower(deviceInfo->shortAddress, DALIVALUE_MASK);
  }
  else {
    // prepare dimming and then call dimRepeater
    dimPrepare(aDimMode, aDimPerMS, boost::bind(&DaliBusDevice::dimStart, this, deviceInfo->shortAddress, aDimMode==dimmode_up ? DALICMD_UP : DALICMD_DOWN));
  }
}


void DaliBusDevice::dimStart(DaliAddress aDaliAddress, DaliCommand aCommand)
{
  MainLoop::currentMainLoop().executeTicketOnce(dimRepeaterTicket, boost::bind(&DaliBusDevice::dimRepeater, this, aDaliAddress, aCommand, _1));
}


void DaliBusDevice::dimRepeater(DaliAddress aDaliAddress, DaliCommand aCommand, MLTimer &aTimer)
{
  daliVdc.daliComm->daliSendCommand(aDaliAddress, aCommand);
  // schedule next command
  // - DALI UP and DOWN run 200mS, but can be repeated earlier
  //   Note: DALI bus speed limits commands to 120Bytes/sec max, i.e. about 20 per 200mS, i.e. max 10 lamps dimming
  MainLoop::currentMainLoop().retriggerTimer(aTimer, 200*MilliSecond);
}



// MARK: ===== DaliBusDeviceGroup (multiple DALI devices, addressed as a group, forming single channel dimmer)


DaliBusDeviceGroup::DaliBusDeviceGroup(DaliVdc &aDaliVdc, uint8_t aGroupNo) :
  inherited(aDaliVdc),
  groupMaster(DaliBroadcast)
{
  mixID.erase(); // no members yet
  // assume max features, will be reduced to what all group members are capable in addDaliBusDevice()
  supportsLED = true;
  supportsDT8 = true;
  dt8Color = true;
  dt8CT = true;
  // set the group address to use
  deviceInfo->shortAddress = aGroupNo|DaliGroup;
}


void DaliBusDeviceGroup::addDaliBusDevice(DaliBusDevicePtr aDaliBusDevice)
{
  // add the ID to the mix
  LOG(LOG_NOTICE, "- DALI bus device with shortaddr %d is grouped in DALI group %d", aDaliBusDevice->deviceInfo->shortAddress, deviceInfo->shortAddress & DaliGroupMask);
  aDaliBusDevice->dSUID.xorDsUidIntoMix(mixID, false);
  // if this is the first valid device, use it as master
  if (groupMaster==DaliBroadcast && !aDaliBusDevice->isDummy) {
    // this is the master device
    LOG(LOG_INFO, "- DALI bus device with shortaddr %d is master of the group (queried for brightness, mindim)", aDaliBusDevice->deviceInfo->shortAddress);
    groupMaster = aDaliBusDevice->deviceInfo->shortAddress;
  }
  // reduce features to common denominator for all group members
  if (!aDaliBusDevice->supportsLED) supportsLED = false;
  if (!aDaliBusDevice->supportsDT8) supportsDT8 = false;
  if (!aDaliBusDevice->dt8Color) supportsDT8 = false;
  if (!aDaliBusDevice->dt8CT) dt8CT = false;
  // add member
  groupMembers.push_back(aDaliBusDevice->deviceInfo->shortAddress);
}


string DaliBusDeviceGroup::description()
{
  string g;
  for (DaliComm::ShortAddressList::iterator pos = groupMembers.begin(); pos!=groupMembers.end(); ++pos) {
    if (!g.empty()) g +=", ";
    string_format_append(g, "%02d", *pos);
  }
  string s = "\n- DALI group - device bus addresses: " + g;
  s + inherited::description();
  return s;
}



void DaliBusDeviceGroup::initialize(StatusCB aCompletedCB, uint16_t aUsedGroupsMask)
{
  DaliComm::ShortAddressList::iterator pos = groupMembers.begin();
  initNextGroupMember(aCompletedCB, pos);
}


void DaliBusDeviceGroup::initNextGroupMember(StatusCB aCompletedCB, DaliComm::ShortAddressList::iterator aNextMember)
{
  if (aNextMember!=groupMembers.end()) {
    // another member, query group membership, then adjust if needed
    // need to query current groups
    getGroupMemberShip(
      boost::bind(&DaliBusDeviceGroup::groupMembershipResponse, this, aCompletedCB, aNextMember, _1, _2),
      *aNextMember
    );
  }
  else {
    // group membership is now configured correctly
    // Now we can initialize the features for the entire group
    initializeFeatures(aCompletedCB);
  }
}


void DaliBusDeviceGroup::groupMembershipResponse(StatusCB aCompletedCB, DaliComm::ShortAddressList::iterator aNextMember, uint16_t aGroups, ErrorPtr aError)
{
  uint8_t groupNo = deviceInfo->shortAddress & DaliGroupMask;
  // make sure device is member of the group
  if ((aGroups & (1<<groupNo))==0) {
    // is not yet member of this group -> add it
    LOG(LOG_INFO, "- making DALI bus device with shortaddr %d member of group %d", *aNextMember, groupNo);
    daliVdc.daliComm->daliSendConfigCommand(*aNextMember, DALICMD_ADD_TO_GROUP|groupNo);
  }
  // remove from all other groups
  aGroups &= ~(1<<groupNo); // do not remove again from target group
  for (groupNo=0; groupNo<16; groupNo++) {
    if (aGroups & (1<<groupNo)) {
      // device is member of a group it shouldn't be in -> remove it
      LOG(LOG_INFO, "- removing DALI bus device with shortaddr %d from group %d", *aNextMember, groupNo);
      daliVdc.daliComm->daliSendConfigCommand(*aNextMember, DALICMD_REMOVE_FROM_GROUP|groupNo);
    }
  }
  // done adding this member to group
  // - check if more to process
  ++aNextMember;
  initNextGroupMember(aCompletedCB, aNextMember);
}




/// derive the dSUID from collected device info
void DaliBusDeviceGroup::deriveDsUid()
{
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  // use xored IDs of group members as base for creating UUIDv5 in vdcNamespace
  dSUID.setNameInSpace("daligroup:"+mixID, vdcNamespace);
}


// MARK: ===== DaliOutputDevice (base class)


DaliOutputDevice::DaliOutputDevice(DaliVdc *aVdcP) :
  Device((Vdc *)aVdcP),
  transitionTicket(0)
{
  // DALI output devices are always light (in this implementation, at least)
  setColorClass(class_yellow_light);
}


DaliVdc &DaliOutputDevice::daliVdc()
{
  return *(static_cast<DaliVdc *>(vdcP));
}


ErrorPtr DaliOutputDevice::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  if (aMethod=="x-p44-ungroupDevice") {
    // Remove this device from the installation, forget the settings
    return daliVdc().ungroupDevice(this, aRequest);
  }
  if (aMethod=="x-p44-saveAsDefault") {
    // save the current brightness as default DALI brightness (at powerup or failure)
    saveAsDefaultBrightness();
    // confirm done
    aRequest->sendResult(ApiValuePtr());
    return ErrorPtr();
  }
  else {
    return inherited::handleMethod(aRequest, aMethod, aParams);
  }
}


#define MAX_SINGLE_STEP_TRANSITION_TIME (45.3*Second) // fade time 13
#define SLOW_TRANSITION_STEP_TIME (8*Second) // fade time 8

void DaliOutputDevice::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  LightBehaviourPtr l = getOutput<LightBehaviour>();
  bool withColor = false;
  if (l && needsToApplyChannels()) {
    // abort previous transition
    MainLoop::currentMainLoop().cancelExecutionTicket(transitionTicket);
    // brightness transition time is relevant for the whole transition
    MLMicroSeconds transitionTime = l->transitionTimeToNewBrightness();
    if (l->brightnessNeedsApplying()) {
      l->brightnessTransitionStep(); // init brightness transition
    }
    // see if we also need a color transition
    ColorLightBehaviourPtr cl = getOutput<ColorLightBehaviour>();
    if (cl && cl->deriveColorMode()) {
      // colors will change as well
      cl->colorTransitionStep(); // init color transition
      withColor = true;
    }
    // prepare transition time
    MLMicroSeconds stepTime = transitionTime;
    double stepSize = 1;
    if (stepTime>MAX_SINGLE_STEP_TRANSITION_TIME) {
      stepTime = SLOW_TRANSITION_STEP_TIME;
      stepSize = (double)stepTime/transitionTime;
    }
    // apply transition (step) time
    setTransitionTime(stepTime);
    // start transition
    applyChannelValueSteps(aForDimming, withColor, stepSize);
    // transition is initiated
    if (cl) {
      cl->appliedColorValues();
    }
    l->brightnessApplied();
  }
  // always consider apply done, even if transition is still running
  inherited::applyChannelValues(aDoneCB, aForDimming);
}



// MARK: ===== DaliSingleControllerDevice (single channel)


DaliSingleControllerDevice::DaliSingleControllerDevice(DaliVdc *aVdcP) :
  DaliOutputDevice(aVdcP)
{
}


bool DaliSingleControllerDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Note: setting up behaviours late, because we want the brightness dimmer already assigned for the hardware name
  if (daliController->supportsDT8) {
    // see if we need to do the RGBWA conversion here
    if (!daliController->dt8Color && !daliController->dt8CT && daliController->dt8RGBWAFchannels>=3) {
      // set up dS behaviour for RGB(WA) light
      installSettings(DeviceSettingsPtr(new ColorLightDeviceSettings(*this)));
      // set the behaviour
      RGBColorLightBehaviourPtr rgbl = RGBColorLightBehaviourPtr(new RGBColorLightBehaviour(*this, false));
      rgbl->setHardwareOutputConfig(outputFunction_colordimmer, outputmode_gradual, usage_undefined, true, 0); // DALI lights are always dimmable, no power known
      rgbl->setHardwareName("DALI DT8 RGB(WA) light");
      rgbl->initMinBrightness(0.4); // min brightness is 0.4 (~= 1/256)
      addBehaviour(rgbl);
    }
    else {
      // set up dS behaviour for color or CT light
      installSettings(DeviceSettingsPtr(new ColorLightDeviceSettings(*this)));
      // set the behaviour
      bool ctOnly = daliController->dt8CT && !daliController->dt8Color;
      ColorLightBehaviourPtr cl = ColorLightBehaviourPtr(new ColorLightBehaviour(*this, ctOnly));
      cl->setHardwareOutputConfig(cl->isCtOnly() ? outputFunction_ctdimmer : outputFunction_colordimmer, outputmode_gradual, usage_undefined, true, 0); // DALI lights are always dimmable, no power known
      cl->setHardwareName(string_format("DALI DT8 %s light", cl->isCtOnly() ? "tunable white" : "color"));
      cl->initMinBrightness(0.4); // min brightness is 0.4 (~= 1/256)
      addBehaviour(cl);
    }
  }
  else {
    // set up dS behaviour for simple channel DALI dimmer
    // - use light settings, which include a scene table
    installSettings(DeviceSettingsPtr(new LightDeviceSettings(*this)));
    // - set the behaviour
    LightBehaviourPtr l = LightBehaviourPtr(new LightBehaviour(*this));
    l->setHardwareOutputConfig(outputFunction_dimmer, outputmode_gradual, usage_undefined, true, 160); // DALI ballasts are always dimmable, // TODO: %%% somewhat arbitrary 2*80W max wattage
    if (daliTechnicalType()==dalidevice_group)
      l->setHardwareName(string_format("DALI dimmer group # %d",daliController->deviceInfo->shortAddress & DaliGroupMask));
    else
      l->setHardwareName(string_format("DALI dimmer @ %d",daliController->deviceInfo->shortAddress));
    addBehaviour(l);
  }
  // - derive the DsUid
  deriveDsUid();
  // done
  return true; // simple identification, callback will not be called
}


bool DaliSingleControllerDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (daliController->supportsDT8) {
    if (getIcon("dali_color", aIcon, aWithData, aResolutionPrefix))
      return true;
  }
  else {
    if (getIcon("dali_dimmer", aIcon, aWithData, aResolutionPrefix))
      return true;
  }
  // no specific icon found
  return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


string DaliSingleControllerDevice::getExtraInfo()
{
  return "DALI "+DaliComm::formatDaliAddress(daliController->deviceInfo->shortAddress);
}


int DaliSingleControllerDevice::opStateLevel()
{
  return !daliController->lampFailure && daliController->isPresent ? 100 : 0;
}


string DaliSingleControllerDevice::getOpStateText()
{
  string t;
  string sep;
  if (daliController->lampFailure) {
    t += "lamp failure";
    sep = ", ";
  }
  if (!daliController->isPresent) {
    t += sep + "not present";
    sep = ", ";
  }
  return t;
}




void DaliSingleControllerDevice::initializeDevice(StatusCB aCompletedCB, bool aFactoryReset)
{
  // - sync cached channel values from actual device
  daliController->updateParams(boost::bind(&DaliSingleControllerDevice::daliControllerSynced, this, aCompletedCB, aFactoryReset, _1));
}


void DaliSingleControllerDevice::daliControllerSynced(StatusCB aCompletedCB, bool aFactoryReset, ErrorPtr aError)
{
  processUpdatedParams(aError);
  // continue with initialisation in superclasses
  inherited::initializeDevice(aCompletedCB, aFactoryReset);
}


void DaliSingleControllerDevice::processUpdatedParams(ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    RGBColorLightBehaviourPtr rgbl = getOutput<RGBColorLightBehaviour>();
    if (rgbl) {
      // save raw RGB(WA)
      if (daliController->dt8RGBWAFchannels>3) {
        if (daliController->dt8RGBWAFchannels>4) {
          // RGBWA
          rgbl->setRGBWA(daliController->currentR, daliController->currentG, daliController->currentB, daliController->currentW, daliController->currentA, 255);
        }
        else {
          // RGBW
          rgbl->setRGBW(daliController->currentR, daliController->currentG, daliController->currentB, daliController->currentW, 255);
        }
      }
      else {
        rgbl->setRGB(daliController->currentR, daliController->currentG, daliController->currentB, 255);
      }
      // color tone is set, now sync back current brightness, as RGBWA values are not absolute, but relative to brightness
      getOutput()->getChannelByIndex(0)->syncChannelValue(daliController->currentBrightness);
    }
    else {
      // save brightness now
      getOutput()->getChannelByIndex(0)->syncChannelValue(daliController->currentBrightness);
      // initialize the light behaviour with the minimal dimming level
      LightBehaviourPtr l = getOutput<LightBehaviour>();
      l->initMinBrightness(daliController->minBrightness);
      ColorLightBehaviourPtr cl = getOutput<ColorLightBehaviour>();
      if (cl) {
        // also synchronize color information
        cl->colorMode = daliController->currentColorMode;
        if (daliController->currentColorMode==colorLightModeCt) {
          // - tunable white mode
          cl->ct->syncChannelValue(daliController->currentXorCT);
        }
        else if (daliController->currentColorMode==colorLightModeXY) {
          // - X/Y color mode
          cl->cieX->syncChannelValue((double)daliController->currentXorCT/65536);
          cl->cieY->syncChannelValue((double)daliController->currentY/65536);
        }
      }
    }
  }
  else {
    LOG(LOG_ERR, "DaliDevice: error getting state/params from dimmer: %s", aError->description().c_str());
  }
}



void DaliSingleControllerDevice::checkPresence(PresenceCB aPresenceResultHandler)
{
  // query the device
  daliController->updateStatus(boost::bind(&DaliSingleControllerDevice::checkPresenceResponse, this, aPresenceResultHandler));
}


void DaliSingleControllerDevice::checkPresenceResponse(PresenceCB aPresenceResultHandler)
{
  // present if a proper YES (without collision) received
  aPresenceResultHandler(daliController->isPresent);
}



void DaliSingleControllerDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  checkPresence(boost::bind(&DaliSingleControllerDevice::disconnectableHandler, this, aForgetParams, aDisconnectResultHandler, _1));
}

void DaliSingleControllerDevice::disconnectableHandler(bool aForgetParams, DisconnectCB aDisconnectResultHandler, bool aPresent)
{
  if (!aPresent) {
    // call inherited disconnect
    inherited::disconnect(aForgetParams, aDisconnectResultHandler);
  }
  else {
    // not disconnectable
    if (aDisconnectResultHandler) {
      aDisconnectResultHandler(false);
    }
  }
}


void DaliSingleControllerDevice::applyChannelValueSteps(bool aForDimming, bool aWithColor, double aStepSize)
{
  LightBehaviourPtr l = getOutput<LightBehaviour>();
  bool needactivation = false;
  bool neednewbrightness = true; // assume brightness change, if color only, this will be cleared in color handling code below
  bool moreSteps = false;
  ColorLightBehaviourPtr cl;
  if (aWithColor) {
    cl = getOutput<ColorLightBehaviour>();
    moreSteps = cl->colorTransitionStep(aStepSize);
    neednewbrightness = l->brightness->inTransition(); // could be color transition only
    RGBColorLightBehaviourPtr rgbl = getOutput<RGBColorLightBehaviour>();
    if (rgbl) {
      // DALI controller needs direct RGBWA(F)
      double r=0,g=0,b=0,w=0,a=0;
      if (daliController->dt8RGBWAFchannels>3) {
        // RGBW or RGBWA
        if (daliController->dt8RGBWAFchannels>4) {
          // RGBWA
          rgbl->getRGBWA(r, g, b, w, a, 127, true);
          if (!aForDimming) {
            ALOG(LOG_INFO, "DALI composite RGBWA: R=%d, G=%d, B=%d, W=%d, A=%d", (int)r, (int)g, (int)b, (int)w, (int)a);
          }
        }
        else {
          // RGBW
          rgbl->getRGBW(r, g, b, w, 127, true);
          if (!aForDimming) {
            ALOG(LOG_INFO, "DALI composite RGBW: R=%d, G=%d, B=%d, W=%d", (int)r, (int)g, (int)b, (int)w);
          }
        }
      }
      else {
        // RGB
        rgbl->getRGB(r, g, b, 127, true);
        if (!aForDimming) {
          ALOG(LOG_INFO, "DALI composite RGB: R=%d, G=%d, B=%d", (int)r, (int)g, (int)b);
        }
      }
      needactivation = daliController->setRGBWAParams(r, g, b, w, a);
    }
    else {
      // DALI controller understands Cie and/or Ct directly
      if (daliController->dt8Color && (cl->colorMode!=colorLightModeCt || !daliController->dt8CT)) {
        // color is requested or CT is requested but controller cannot do CT natively -> send CieX/Y
        double cieX, cieY;
        cl->getCIExy(cieX, cieY); // transitional values calculated from actually changed original channels transitioning
        needactivation = daliController->setColorParams(colorLightModeXY, cieX, cieY);
      }
      else {
        // CT is requested and controller can do CT natively, or color is requested but controller can ONLY do CT
        double mired;
        cl->getCT(mired); // transitional value calculated from actually changed original channels transitioning
        needactivation = daliController->setColorParams(colorLightModeCt, mired);
      }
    }
  }
  // handle brightness
  if (neednewbrightness || needactivation) {
    // update actual dimmer value
    if (l->brightnessTransitionStep(aStepSize)) moreSteps = true;
    daliController->setBrightness(l->brightnessForHardware());
  }
  // activate color params in case brightness has not changed or device is not in auto-activation mode (we don't set this)
  if (needactivation) {
    daliController->activateColorParams();
  }
  // now schedule next step (if any)
  if (moreSteps) {
    // not yet complete, schedule next step
    transitionTicket = MainLoop::currentMainLoop().executeOnce(
      boost::bind(&DaliSingleControllerDevice::applyChannelValueSteps, this, aForDimming, aWithColor, aStepSize),
      SLOW_TRANSITION_STEP_TIME
    );
    return; // will be called later again
  }
}



// optimized DALI dimming implementation
void DaliSingleControllerDevice::dimChannel(ChannelBehaviourPtr aChannel, VdcDimMode aDimMode, bool aDoApply)
{
  // start dimming
  if (aChannel) {
    if (aChannel->getChannelType()==channeltype_brightness) {
      // start dimming
      if (aDoApply) {
        ALOG(LOG_INFO,
          "dimChannel (DALI): channel '%s' %s",
          aChannel->getName(),
          aDimMode==dimmode_stop ? "STOPS dimming" : (aDimMode==dimmode_up ? "starts dimming UP" : "starts dimming DOWN")
        );
        daliController->dim(aDimMode, aChannel->getDimPerMS());
      }
      // in all cases, we need to query current brightness after dimming
      if (aDimMode==dimmode_stop) {
        // retrieve end status
        daliController->updateParams(boost::bind(&DaliSingleControllerDevice::dimEndStateRetrieved, this, _1));
      }
    }
    else {
      // not my channel, use generic implementation
      inherited::dimChannel(aChannel, aDimMode, aDoApply);
    }
  }
}


void DaliSingleControllerDevice::dimEndStateRetrieved(ErrorPtr aError)
{
  processUpdatedParams(aError);
}


void DaliSingleControllerDevice::saveAsDefaultBrightness()
{
  daliController->setDefaultBrightness(-1);
}


void DaliSingleControllerDevice::setTransitionTime(MLMicroSeconds aTransitionTime)
{
  daliController->setTransitionTime(aTransitionTime);
}


bool DaliSingleControllerDevice::prepareForOptimizedSet(NotificationDeliveryStatePtr aDeliveryState)
{
  // check general exclude reasons
  if (
    !daliController || // safety - need a controller to optimize
    daliController->isGrouped() || // already grouped devices cannot be optimized
    daliController->supportsDT8 // FIXME: exclude DT8 for now until we can test color scenes with a sample device
  ) {
    return false;
  }
  // check notification-specific conditions
  if (aDeliveryState->optimizedType==ntfy_callscene) {
    // scenes are generally optimizable
    return true;
  }
  else if (aDeliveryState->optimizedType==ntfy_dimchannel) {
    // only brightness dimming optimizable for now
    return currentDimChannel && currentDimChannel->getChannelType()==channeltype_brightness;
  }
}




void DaliSingleControllerDevice::deriveDsUid()
{
  // single channel dimmer just uses dSUID derived from single DALI bus device
  dSUID = daliController->dSUID;
}


string DaliSingleControllerDevice::modelName()
{
  string s = "DALI";
  if (daliController->supportsDT8) {
    if (daliController->dt8Color) s += " color";
    if (daliController->dt8CT) s += " tunable white";
  }
  else if (daliController->supportsLED) {
    s += " LED";
  }
  s += " dimmer";
  if (daliTechnicalType()==dalidevice_group) s+= " group";
  return s;
}





string DaliSingleControllerDevice::hardwareGUID()
{
  if (daliController->deviceInfo->devInfStatus<=DaliDeviceInfo::devinf_only_gtin)
    return ""; // none
  // return as GS1 element strings
  // Note: GTIN/Serial will be reported even if it could not be used for deriving dSUID (e.g. devinf_maybe/devinf_notForID cases)
  return string_format("gs1:(01)%llu(21)%llu", daliController->deviceInfo->gtin, daliController->deviceInfo->serialNo);
}


string DaliSingleControllerDevice::hardwareModelGUID()
{
  if (daliController->deviceInfo->gtin==0)
    return ""; // none
  // return as GS1 element strings with Application Identifier 01=GTIN
  return string_format("gs1:(01)%llu", daliController->deviceInfo->gtin);
}


string DaliSingleControllerDevice::oemGUID()
{
  if (daliController->deviceInfo->oem_gtin==0 || daliController->deviceInfo->oem_serialNo==0)
    return ""; // none
  // return as GS1 element strings with Application Identifiers 01=GTIN and 21=Serial
  return string_format("gs1:(01)%llu(21)%llu", daliController->deviceInfo->oem_gtin, daliController->deviceInfo->oem_serialNo);
}


string DaliSingleControllerDevice::oemModelGUID()
{
  if (daliController->deviceInfo->oem_gtin==0)
    return ""; // none
  // return as GS1 element strings with Application Identifier 01=GTIN
  return string_format("gs1:(01)%llu", daliController->deviceInfo->oem_gtin);
}


string DaliSingleControllerDevice::description()
{
  string s = inherited::description();
  s.append(daliController->description());
  return s;
}


// MARK: ===== DaliCompositeDevice (multi-channel color lamp)


DaliCompositeDevice::DaliCompositeDevice(DaliVdc *aVdcP) :
  DaliOutputDevice(aVdcP)
{
}


bool DaliCompositeDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Note: setting up behaviours late, because we want the brightness dimmer already assigned for the hardware name
  // set up dS behaviour for color lights, which include a color scene table
  installSettings(DeviceSettingsPtr(new ColorLightDeviceSettings(*this)));
  // set the behaviour
  bool ctOnly = dimmers[dimmer_white] && dimmers[dimmer_amber] && !dimmers[dimmer_red];
  RGBColorLightBehaviourPtr cl = RGBColorLightBehaviourPtr(new RGBColorLightBehaviour(*this, ctOnly));
  cl->setHardwareOutputConfig(outputFunction_colordimmer, outputmode_gradual, usage_undefined, true, 0); // DALI lights are always dimmable, no power known
  cl->setHardwareName(string_format("DALI composite color light"));
  cl->initMinBrightness(0.4); // min brightness is 0.4 (~= 1/256)
  addBehaviour(cl);
  // now derive dSUID
  deriveDsUid();
  // done
  return true; // simple identification, callback will not be called
}


int DaliCompositeDevice::opStateLevel()
{
  int l = 100;
  for (DimmerIndex idx=dimmer_red; idx<numDimmers; idx++) {
    if (dimmers[idx]) {
      if (dimmers[idx]->deviceInfo && dimmers[idx]->deviceInfo->devInfStatus!=DaliDeviceInfo::devinf_solid) {
        l = 90; // is not a recommended device, does not have unique ID
      }
      if (dimmers[idx]->isDummy) {
        l = 20; // not seen on last bus scan, might be glitch
      }
      if (!dimmers[idx]->isPresent) {
        l = 40; // not completely fatal, might recover on next ping
      }
      if (dimmers[idx]->lampFailure) {
        l = 0;
      }
    }
  }
  return l;
}


string DaliCompositeDevice::getOpStateText()
{
  bool missing = false;
  bool failure = false;
  bool incomplete = false;
  bool noUniqueId = false;
  for (DimmerIndex idx=dimmer_red; idx<numDimmers; idx++) {
    if (dimmers[idx]) {
      if (dimmers[idx]->deviceInfo && dimmers[idx]->deviceInfo->devInfStatus!=DaliDeviceInfo::devinf_solid) {
        noUniqueId = true;
      }
      if (!dimmers[idx]->isPresent) {
        missing = true;
      }
      if (dimmers[idx]->lampFailure) {
        failure = true;
      }
      if (dimmers[idx]->isDummy) {
        incomplete = true;
      }
    }
  }
  string t;
  string sep;
  if (failure) {
    t += "lamp failure";
    sep = ", ";
  }
  if (missing) {
    t += sep + "not present";
    sep = ", ";
  }
  if (incomplete) {
    t += sep + "incomplete";
    sep = ", ";
  }
  if (noUniqueId) {
    t += sep + "Missing S/N";
    sep = ", ";
  }
  return t;
}





bool DaliCompositeDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  RGBColorLightBehaviourPtr cl = getOutput<RGBColorLightBehaviour>();
  if (getIcon(cl->isCtOnly() ? "dali_ct" : "dali_color", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


string DaliCompositeDevice::getExtraInfo()
{
  string s = "DALI short addresses: ";
  if (dimmers[dimmer_red]) {
    string_format_append(s, " Red:%d", dimmers[dimmer_red]->deviceInfo->shortAddress);
  }
  if (dimmers[dimmer_green]) {
    string_format_append(s, " Green:%d", dimmers[dimmer_green]->deviceInfo->shortAddress);
  }
  if (dimmers[dimmer_blue]) {
    string_format_append(s, " Blue:%d", dimmers[dimmer_blue]->deviceInfo->shortAddress);
  }
  if (dimmers[dimmer_white]) {
    string_format_append(s, " White:%d", dimmers[dimmer_white]->deviceInfo->shortAddress);
  }
  if (dimmers[dimmer_amber]) {
    string_format_append(s, " Warmwhite/amber:%d", dimmers[dimmer_amber]->deviceInfo->shortAddress);
  }
  return s;
}


bool DaliCompositeDevice::addDimmer(DaliBusDevicePtr aDimmerBusDevice, string aDimmerType)
{
  if (aDimmerType=="R")
    dimmers[dimmer_red] = aDimmerBusDevice;
  else if (aDimmerType=="G")
    dimmers[dimmer_green] = aDimmerBusDevice;
  else if (aDimmerType=="B")
    dimmers[dimmer_blue] = aDimmerBusDevice;
  else if (aDimmerType=="W")
    dimmers[dimmer_white] = aDimmerBusDevice;
  else if (aDimmerType=="A")
    dimmers[dimmer_amber] = aDimmerBusDevice;
  else
    return false; // cannot add
  return true; // added ok
}



void DaliCompositeDevice::initializeDevice(StatusCB aCompletedCB, bool aFactoryReset)
{
  // - sync cached channel values from actual devices
  updateNextDimmer(aCompletedCB, aFactoryReset, dimmer_red, ErrorPtr());
}


void DaliCompositeDevice::updateNextDimmer(StatusCB aCompletedCB, bool aFactoryReset, DimmerIndex aDimmerIndex, ErrorPtr aError)
{
  if (!Error::isOK(aError)) {
    LOG(LOG_ERR, "DaliCompositeDevice: error getting state/params from dimmer#%d: %s", aDimmerIndex-1, aError->description().c_str());
  }
  while (aDimmerIndex<numDimmers) {
    DaliBusDevicePtr di = dimmers[aDimmerIndex];
    // process this dimmer if it exists
    if (di) {
      di->updateParams(boost::bind(&DaliCompositeDevice::updateNextDimmer, this, aCompletedCB, aFactoryReset, aDimmerIndex+1, _1));
      return; // return now, will be called again when update is complete
    }
    aDimmerIndex++; // next
  }
  // all updated (not necessarily successfully) if we land here
  RGBColorLightBehaviourPtr cl = getOutput<RGBColorLightBehaviour>();
  double r = dimmers[dimmer_red] ? dimmers[dimmer_red]->currentBrightness : 0;
  double g = dimmers[dimmer_green] ? dimmers[dimmer_green]->currentBrightness : 0;
  double b = dimmers[dimmer_blue] ? dimmers[dimmer_blue]->currentBrightness : 0;
  if (dimmers[dimmer_white]) {
    double w = dimmers[dimmer_white]->currentBrightness;
    if (dimmers[dimmer_amber]) {
      double a = dimmers[dimmer_amber]->currentBrightness;
      // could be CT only
      if (cl->isCtOnly()) {
        // treat as two-channel tunable white
        cl->setCWWW(w, a, 100); // dali dimmers use abstracted 0..100% brightness
      }
      else {
        // RGBWA
        cl->setRGBWA(r, g, b, w, a, 100); // dali dimmers use abstracted 0..100% brightness
      }
    }
    else {
      // RGBW
      cl->setRGBW(r, g, b, w, 100); // dali dimmers use abstracted 0..100% brightness
    }
  }
  else {
    cl->setRGB(r, g, b, 100); // dali dimmers use abstracted 0..100% brightness
  }
  // complete - continue with initialisation in superclasses
  inherited::initializeDevice(aCompletedCB, aFactoryReset);
}



DaliBusDevicePtr DaliCompositeDevice::firstBusDevice()
{
  for (DimmerIndex idx=dimmer_red; idx<numDimmers; idx++) {
    if (dimmers[idx]) {
      return dimmers[idx];
    }
  }
  return DaliBusDevicePtr(); // none
}



void DaliCompositeDevice::checkPresence(PresenceCB aPresenceResultHandler)
{
  // assuming all channels in the same physical device, check only first one
  DaliBusDevicePtr dimmer = firstBusDevice();
  if (dimmer) {
    dimmer->updateStatus(boost::bind(&DaliCompositeDevice::checkPresenceResponse, this, aPresenceResultHandler, dimmer));
    return;
  }
  // no dimmer -> not present
  aPresenceResultHandler(false);
}


void DaliCompositeDevice::checkPresenceResponse(PresenceCB aPresenceResultHandler, DaliBusDevicePtr aDimmer)
{
  // present if a proper YES (without collision) received
  aPresenceResultHandler(aDimmer->isPresent);
}



void DaliCompositeDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  checkPresence(boost::bind(&DaliCompositeDevice::disconnectableHandler, this, aForgetParams, aDisconnectResultHandler, _1));
}

void DaliCompositeDevice::disconnectableHandler(bool aForgetParams, DisconnectCB aDisconnectResultHandler, bool aPresent)
{
  if (!aPresent) {
    // call inherited disconnect
    inherited::disconnect(aForgetParams, aDisconnectResultHandler);
  }
  else {
    // not disconnectable
    if (aDisconnectResultHandler) {
      aDisconnectResultHandler(false);
    }
  }
}


void DaliCompositeDevice::applyChannelValueSteps(bool aForDimming, bool aWithColor, double aStepSize)
{
  RGBColorLightBehaviourPtr cl = getOutput<RGBColorLightBehaviour>();
  bool moreSteps = cl->colorTransitionStep(aStepSize);
  if (cl->brightnessTransitionStep(aStepSize)) moreSteps = true;
  // RGB lamp, get components
  double r=0,g=0,b=0,w=0,a=0;
  if (dimmers[dimmer_white]) {
    // RGBW, RGBWA or CT-only
    if (dimmers[dimmer_amber]) {
      // RGBWA or CT
      if (cl->isCtOnly()) {
        // CT
        cl->getCWWW(w, a, 100); // dali dimmers use abstracted 0..100% brightness as input
        if (!aForDimming) {
          ALOG(LOG_INFO, "DALI composite CWWW: CW=%d, WW=%d", (int)w, (int)a);
        }
      }
      else {
        // RGBWA
        cl->getRGBWA(r, g, b, w, a, 100); // dali dimmers use abstracted 0..100% brightness as input
        if (!aForDimming) {
          ALOG(LOG_INFO, "DALI composite RGBWA: R=%d, G=%d, B=%d, W=%d, A=%d", (int)r, (int)g, (int)b, (int)w, (int)a);
        }
      }
    }
    else {
      cl->getRGBW(r, g, b, w, 100); // dali dimmers use abstracted 0..100% brightness as input
      if (!aForDimming) {
        ALOG(LOG_INFO, "DALI composite RGBW: R=%d, G=%d, B=%d, W=%d", (int)r, (int)g, (int)b, (int)w);
      }
    }
  }
  else {
    // RGB
    cl->getRGB(r, g, b, 100); // dali dimmers use abstracted 0..100% brightness as input
    if (!aForDimming) {
      ALOG(LOG_INFO, "DALI composite RGB: R=%d, G=%d, B=%d", (int)r, (int)g, (int)b);
    }
  }
  // apply new values
  if (dimmers[dimmer_red]) dimmers[dimmer_red]->setBrightness(r);
  if (dimmers[dimmer_green]) dimmers[dimmer_green]->setBrightness(g);
  if (dimmers[dimmer_blue]) dimmers[dimmer_blue]->setBrightness(b);
  if (dimmers[dimmer_white]) dimmers[dimmer_white]->setBrightness(w);
  if (dimmers[dimmer_amber]) dimmers[dimmer_amber]->setBrightness(a);
  if (moreSteps) {
    // not yet complete, schedule next step
    transitionTicket = MainLoop::currentMainLoop().executeOnce(
      boost::bind(&DaliCompositeDevice::applyChannelValueSteps, this, aForDimming, aWithColor, aStepSize),
      SLOW_TRANSITION_STEP_TIME
    );
    return; // will be called later again
  }
}


/// save brightness as default for DALI dimmer to use after powerup and at failure
/// @param aBrightness new brightness to set
void DaliCompositeDevice::saveAsDefaultBrightness()
{
  dimmers[dimmer_red]->setDefaultBrightness(-1);
  dimmers[dimmer_green]->setDefaultBrightness(-1);
  dimmers[dimmer_blue]->setDefaultBrightness(-1);
  if (dimmers[dimmer_white]) dimmers[dimmer_white]->setDefaultBrightness(-1);
}


void DaliCompositeDevice::setTransitionTime(MLMicroSeconds aTransitionTime)
{
  // set transition time for all dimmers to brightness transition time
  if (dimmers[dimmer_red]) dimmers[dimmer_red]->setTransitionTime(aTransitionTime);
  if (dimmers[dimmer_green]) dimmers[dimmer_green]->setTransitionTime(aTransitionTime);
  if (dimmers[dimmer_blue]) dimmers[dimmer_blue]->setTransitionTime(aTransitionTime);
  if (dimmers[dimmer_white]) dimmers[dimmer_white]->setTransitionTime(aTransitionTime);
  if (dimmers[dimmer_amber]) dimmers[dimmer_amber]->setTransitionTime(aTransitionTime);
}


void DaliCompositeDevice::deriveDsUid()
{
  // Multi-channel DALI devices construct their ID from UUIDs of the DALI devices involved,
  // but in a way that allows re-assignment of R/G/B without changing the dSUID
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  string mixID;
  for (DimmerIndex idx=dimmer_red; idx<numDimmers; idx++) {
    if (dimmers[idx]) {
      // use this dimmer's dSUID as part of the mix
      dimmers[idx]->dSUID.xorDsUidIntoMix(mixID, false);
    }
  }
  // use xored ID as base for creating UUIDv5 in vdcNamespace
  dSUID.setNameInSpace("dalicombi:"+mixID, vdcNamespace);
}


string DaliCompositeDevice::hardwareGUID()
{
  DaliBusDevicePtr dimmer = firstBusDevice();
  if (!dimmer || dimmer->deviceInfo->gtin==0 || dimmer->deviceInfo->serialNo==0)
    return ""; // none
  // return as GS1 element strings
  return string_format("gs1:(01)%llu(21)%llu", dimmer->deviceInfo->gtin, dimmer->deviceInfo->serialNo);
}


string DaliCompositeDevice::hardwareModelGUID()
{
  DaliBusDevicePtr dimmer = firstBusDevice();
  if (!dimmer || dimmer->deviceInfo->gtin==0)
    return ""; // none
  // return as GS1 element strings with Application Identifier 01=GTIN
  return string_format("gs1:(01)%llu", dimmer->deviceInfo->gtin);
}


string DaliCompositeDevice::oemGUID()
{
  DaliBusDevicePtr dimmer = firstBusDevice();
  if (!dimmer || dimmer->deviceInfo->oem_gtin==0|| dimmer->deviceInfo->oem_serialNo==0)
    return ""; // none
  // return as GS1 element strings with Application Identifiers 01=GTIN and 21=Serial
  return string_format("gs1:(01)%llu(21)%llu", dimmer->deviceInfo->oem_gtin, dimmer->deviceInfo->oem_serialNo);
}


string DaliCompositeDevice::oemModelGUID()
{
  DaliBusDevicePtr dimmer = firstBusDevice();
  if (!dimmer || dimmer->deviceInfo->oem_gtin==0)
    return ""; // none
  // return as GS1 element strings with Application Identifier 01=GTIN
  return string_format("gs1:(01)%llu", dimmer->deviceInfo->oem_gtin);
}



string DaliCompositeDevice::description()
{
  string s = inherited::description();
  DaliBusDevicePtr dimmer = firstBusDevice();
  if (dimmer) s.append(dimmer->description());
  return s;
}


#if ENABLE_DALI_INPUTS


// MARK: ===== DaliInputDevice

DaliInputDevice::DaliInputDevice(DaliVdc *aVdcP, const string aDaliInputConfig, DaliAddress aBaseAddress) :
  inherited(aVdcP),
  daliInputDeviceRowID(0),
  releaseTicket(0)
{
  baseAddress = aBaseAddress;
  numAddresses = 1;
  // decode config
  string type = aDaliInputConfig;
  // TODO: add more options
  if (type=="button") {
    inputType = input_button;
  }
  else if (type=="rocker") {
    inputType = input_rocker;
  }
  else if (type=="motion") {
    inputType = input_motion;
  }
  else if (type=="illumination") {
    inputType = input_illumination;
  }
  else if (type=="bistable") {
    inputType = input_bistable;
  }
  else {
    // default to pulse input
    inputType = input_pulse;
  }
  // create behaviours
  if (inputType==input_button) {
    // Simple single button device
    colorClass = class_black_joker;
    // - standard device settings without scene table
    installSettings();
    // - create one button input
    ButtonBehaviourPtr bb = ButtonBehaviourPtr(new ButtonBehaviour(*this,"")); // automatic id
    bb->setHardwareButtonConfig(0, buttonType_undefined, buttonElement_center, false, 0, 1); // mode not restricted
    bb->setGroup(group_yellow_light); // pre-configure for light
    bb->setHardwareName("button");
    addBehaviour(bb);
  }
  else if (inputType==input_rocker) {
    // Two-way Rocker Button device
    numAddresses = 2;
    colorClass = class_black_joker;
    // - standard device settings without scene table
    installSettings();
    // - create down button (index 0)
    ButtonBehaviourPtr bb = ButtonBehaviourPtr(new ButtonBehaviour(*this,"")); // automatic id
    bb->setHardwareButtonConfig(0, buttonType_2way, buttonElement_down, false, 1, 0); // counterpart up-button has buttonIndex 1, fixed mode
    bb->setGroup(group_yellow_light); // pre-configure for light
    bb->setHardwareName("down");
    addBehaviour(bb);
    // - create up button (index 1)
    bb = ButtonBehaviourPtr(new ButtonBehaviour(*this,"")); // automatic id
    bb->setHardwareButtonConfig(0, buttonType_2way, buttonElement_up, false, 0, 0); // counterpart down-button has buttonIndex 0, fixed mode
    bb->setGroup(group_yellow_light); // pre-configure for light
    bb->setHardwareName("up");
    addBehaviour(bb);
  }
  else if (inputType==input_motion) {
    // Standard device settings without scene table
    colorClass = class_red_security;
    installSettings();
    // - create one binary input
    BinaryInputBehaviourPtr ib = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*this,"")); // automatic id
    ib->setHardwareInputConfig(binInpType_motion, usage_undefined, true, Never, Never);
    ib->setHardwareName("motion");
    addBehaviour(ib);
  }
  else if (inputType==input_illumination) {
    // Standard device settings without scene table
    colorClass = class_yellow_light;
    installSettings();
    // - create one binary input
    BinaryInputBehaviourPtr ib = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*this,"")); // automatic id
    ib->setHardwareInputConfig(binInpType_light, usage_undefined, true, Never, Never);
    ib->setHardwareName("light");
    addBehaviour(ib);
  }
  else if (inputType==input_bistable || inputType==input_pulse) {
    // Standard device settings without scene table
    colorClass = class_black_joker;
    installSettings();
    // - create one binary input
    BinaryInputBehaviourPtr ib = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*this,"")); // automatic id
    ib->setHardwareInputConfig(binInpType_none, usage_undefined, true, Never, Never);
    ib->setHardwareName("input");
    addBehaviour(ib);
  }
  else {
    ALOG(LOG_ERR, "unknown device type");
  }
  // mark used groups/scenes
  for (int i=0; i<numAddresses; i++) {
    daliVdc().markUsed(baseAddress+i, true);
  }
}



bool DaliInputDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // derive dSUID
  deriveDsUid();
  return true; // simple identification, callback will not be called
}


DaliVdc &DaliInputDevice::daliVdc()
{
  return *(static_cast<DaliVdc *>(vdcP));
}


bool DaliInputDevice::isSoftwareDisconnectable()
{
  return true;
}


void DaliInputDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  ALOG(LOG_DEBUG, "disconnecting DALI input device with rowid=%lld", daliInputDeviceRowID);
  // clear learn-in data from DB
  if (daliInputDeviceRowID) {
    if(daliVdc().db.executef("DELETE FROM inputDevices WHERE rowid=%lld", daliInputDeviceRowID)!=SQLITE_OK) {
      ALOG(LOG_ERR, "deleting DALI input device: %s", daliVdc().db.error()->description().c_str());
    }
    for (int i=0; i<numAddresses; i++) {
      daliVdc().markUsed(baseAddress+i, false);
    }
  }
  // disconnection is immediate, so we can call inherited right now
  inherited::disconnect(aForgetParams, aDisconnectResultHandler);
}



#define BUTTON_RELEASE_TIMEOUT (100*MilliSecond)
#define INPUT_RELEASE_TIMEOUT (2*Second)

bool DaliInputDevice::checkDaliEvent(uint8_t aEvent, uint8_t aData1, uint8_t aData2)
{
  int refindex = -1; // none
  int inpindex = -1; // none
  bool on = false;
  bool off = false;
  if (aEvent==EVENT_CODE_FOREIGN_FRAME) {
    DaliAddress a = DaliComm::addressFromDaliResponse(aData1);
    if (a!=NoDaliAddress) {
      // evaluate forward frame
      // - special check for scene call
      if (baseAddress & DaliScene) {
        // we are listening to scene calls
        if ((aData1 & 0x01) && (aData2 & 0xF0)==DALICMD_GO_TO_SCENE) {
          refindex = aData2 & 0x0F;
          aData2 = DALICMD_GO_TO_SCENE; // normalize to allow catching command below
          // if used as bistable binary input, consider scenes 0..7 as off, 8..15 as on
          on = refindex>=8;
          off = !on;
        }
      }
      else if (baseAddress & DaliGroup) {
        // we are listening to group calls
        if (a & DaliGroup) refindex = a & DaliGroupMask;
      }
      else {
        // we are listening to single addresses
        refindex = a & DaliAddressMask;
      }
      // now refindex = referenced address/group/scene
      if (refindex>=0) {
        // check if in range
        int baseindex = baseAddress & DaliAddressMask;
        if (refindex>=baseindex && refindex<baseindex+numAddresses) {
          inpindex = refindex-baseindex;
        }
      }
      if (inpindex>=0) {
        // check type of command
        if ((aData1 & 0x01)==0) {
          // direct arc, always pulse
          on = aData2 > 128; // at least half -> on
          off = aData2==0; // -> off
        }
        else {
          switch (aData2) {
            case DALICMD_RECALL_MAX_LEVEL:
            // dimming via DALI button not supported because bus timing is too tight for both directions
            //case DALICMD_ON_AND_STEP_UP:
            //case DALICMD_STEP_UP:
            //case DALICMD_UP:
              on = true;
              break;
            case DALICMD_OFF:
            case DALICMD_RECALL_MIN_LEVEL:
            // dimming via DALI button not supported because bus timing is too tight for both directions
            //case DALICMD_DOWN:
            //case DALICMD_STEP_DOWN:
            //case DALICMD_STEP_DOWN_AND_OFF:
              off = true;
              break;
            default:
              break;
          }
        }
        // now process
        if (on || off) {
          switch (inputType) {
            case input_button:
            case input_rocker:
            {
              ButtonBehaviourPtr bb = getButton(inpindex);
              bb->buttonAction(true);
              releaseTicket = MainLoop::currentMainLoop().executeOnce(boost::bind(&DaliInputDevice::buttonReleased, this, inpindex), BUTTON_RELEASE_TIMEOUT);
              break;
            }
            case input_pulse:
            {
              BinaryInputBehaviourPtr ib = getInput(inpindex);
              ib->updateInputState(1);
              releaseTicket = MainLoop::currentMainLoop().executeOnce(boost::bind(&DaliInputDevice::inputReleased, this, inpindex), INPUT_RELEASE_TIMEOUT);
              break;
            }
            case input_motion:
            case input_illumination:
            case input_bistable:
            default:
            {
              BinaryInputBehaviourPtr ib = getInput(inpindex);
              ib->updateInputState(on ? 1 : 0);
              break;
            }
          }
        }
        return true; // consumed
      }
    }
  }
  // not handled
  return false;
}



void DaliInputDevice::buttonReleased(int aButtonNo)
{
  releaseTicket = 0;
  ButtonBehaviourPtr bb = getButton(aButtonNo);
  bb->buttonAction(false);
}



void DaliInputDevice::inputReleased(int aInputNo)
{
  releaseTicket = 0;
  BinaryInputBehaviourPtr ib = getInput(aInputNo);
  ib->updateInputState(0);
}




/// @return human readable model name/short description
string DaliInputDevice::modelName()
{
  string m = "DALI ";
  switch (inputType) {
    case input_button:
      m += "button";
      break;
    case input_rocker:
      m += "two-way rocker button";
      break;
    case input_motion:
      m += "motion sensor";
      break;
    case input_illumination:
      m += "illumination sensor";
      break;
    case input_bistable:
      m += "bistable input";
      break;
    case input_pulse:
    default:
      m += "pulse input";
      break;
  }
  return m;
}


bool DaliInputDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  const char *iconname = NULL;
  bool colored = false;
  switch (inputType) {
    case input_button:
      iconname = "dali_button";
      colored = true;
      break;
    case input_rocker:
      iconname = "dali_rocker";
      colored = true;
      break;
    case input_motion:
    case input_illumination:
      iconname = "dali_sensor";
      break;
    default:
      break;
  }
  if (iconname) {
    if (colored) {
      if (getClassColoredIcon(iconname, getDominantColorClass(), aIcon, aWithData, aResolutionPrefix)) return true;
    }
    else {
      if (getIcon(iconname, aIcon, aWithData, aResolutionPrefix)) return true;
    }
  }
  // no specific icon found
  return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);

}


string DaliInputDevice::getExtraInfo()
{
  string e = "DALI ";
  e += DaliComm::formatDaliAddress(baseAddress);
  if (numAddresses>1) string_format_append(e, " + %d more", numAddresses-1);
  return e;
}


void DaliInputDevice::deriveDsUid()
{
  // vDC implementation specific UUID:
  //   UUIDv5 with name = classcontainerinstanceid::baseAddress:inputType
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  string s = vdcP->vdcInstanceIdentifier();
  string_format_append(s, "::%u:%u", baseAddress, inputType);
  dSUID.setNameInSpace(s, vdcNamespace);
}




#endif // ENABLE_DALI_INPUTS


#endif // ENABLE_DALI

