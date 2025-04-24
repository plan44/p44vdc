//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
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


// MARK: - DaliBusDevice

DaliBusDevice::DaliBusDevice(DaliVdc &aDaliVdc) :
  mDaliVdc(aDaliVdc),
  mIsDummy(false),
  mIsPresent(false),
  mLampFailure(false),
  mStaleParams(true),
  mCurrentTransitionTime(Infinite), // invalid
  mCurrentDimPerMS(0), // none
  mCurrentFadeRate(0xFF), mCurrentFadeTime(0xFF), // unlikely values
  mCurrentDimMode(dimmode_stop),
  mSupportsLED(false),
  mFullySupportsDimcurve(false),
  mSupportsDT8(false),
  mDT8Color(false),
  mDT8CT(false),
  mCurrentColorMode(colorLightModeNone),
  mCurrentXorCT(0),
  mCurrentY(0),
  mCurrentR(0),
  mCurrentG(0),
  mCurrentB(0),
  mCurrentW(0),
  mCurrentA(0)
{
  // make sure we always have at least a dummy device info
  mDeviceInfo = DaliDeviceInfoPtr(new DaliDeviceInfo);
}


string DaliBusDevice::contextId() const
{
  return DaliComm::formatDaliAddress(mDeviceInfo ? mDeviceInfo->mShortAddress : NoDaliAddress).c_str();
}


int DaliBusDevice::getLogLevelOffset()
{
  if (mLogLevelOffset==0) {
    // no own offset - inherit vdc's
    return mDaliVdc.getLogLevelOffset();
  }
  return inherited::getLogLevelOffset();
}


string DaliBusDevice::description()
{
  string s = mDeviceInfo->description();
  if (mSupportsLED) {
    s += "\n- supports device type 6 (LED)";
  }
  if (mSupportsDT8) string_format_append(s, "\n- supports device type 8 (color), features:%s%s [RGBWAF:%d] [Primary Colors:%d]", mDT8CT ? " [Tunable white]" : "", mDT8Color ? " [CIE x/y]" : "", mDT8RGBWAFchannels, mDT8PrimaryColors);
  return s;
}


bool DaliBusDevice::belongsToShortAddr(DaliAddress aDaliAddress) const
{
  if ((aDaliAddress&DaliAddressTypeMask)==DaliSingle) {
    return mDeviceInfo && aDaliAddress==mDeviceInfo->mShortAddress;
  }
  return false;
}


void DaliBusDevice::daliBusDeviceSummary(ApiValuePtr aInfo) const
{
  if (mDeviceInfo) {
    // short address used to access this device (single or group)
    aInfo->add("accessAddr", aInfo->newUint64(mDeviceInfo->mShortAddress));
    // device info
    mDaliVdc.daliInfoSummary(mDeviceInfo, aInfo);
  }
  // min brightness
  aInfo->add("minBrightness", aInfo->newDouble(mMinBrightness));
  // operational state
  aInfo->add("opStateText", aInfo->newString(
    mIsDummy ? "missing" : (mIsPresent ? (mLampFailure ? "lamp failure" : "present") : "not responding")
  ));
  aInfo->add("opState", aInfo->newUint64(
    mIsDummy ? 20 : (mIsPresent ? (mLampFailure ? 50 : 100) : 0)
  ));
  // DT6
  aInfo->add("DT6", aInfo->newBool(mSupportsLED));
  // DT8
  aInfo->add("DT8", aInfo->newBool(mSupportsDT8));
  if (mSupportsDT8) {
    aInfo->add("color", aInfo->newBool(mDT8Color));
    aInfo->add("CT", aInfo->newBool(mDT8CT));
    if (mDT8PrimaryColors) aInfo->add("primaryColors", aInfo->newUint64(mDT8PrimaryColors));
    if (mDT8RGBWAFchannels) aInfo->add("RGBWAFchannels", aInfo->newUint64(mDT8RGBWAFchannels));
  }
}


void DaliBusDevice::setDeviceInfo(DaliDeviceInfoPtr aDeviceInfo)
{
  // store the info record
  if (!aDeviceInfo)
    aDeviceInfo = DaliDeviceInfoPtr(new DaliDeviceInfo); // always have one, even if it's only a dummy!
  mDeviceInfo = aDeviceInfo; // assign
  deriveDsUid(); // derive dSUID from it
}


void DaliBusDevice::invalidateDeviceInfoSerial()
{
  mDeviceInfo->invalidateSerial();
  deriveDsUid();
}


void DaliBusDevice::deriveDsUid()
{
  if (mIsDummy) return;
  // vDC implementation specific UUID:
  #if OLD_BUGGY_CHKSUM_COMPATIBLE
  if (mDeviceInfo->mDevInfStatus==DaliDeviceInfo::devinf_maybe) {
    // assume we can use devInf to derive dSUID from
    mDeviceInfo->mDevInfStatus = DaliDeviceInfo::devinf_solid;
    // but only actually use it if there is no device entry for the shortaddress-based dSUID with a non-zero name
    // (as this means the device has been already actively used/configured with the shortaddr-dSUID)
    // - calculate the short address based dSUID
    DsUid shortAddrBasedDsUid;
    dsUidForDeviceInfoStatus(shortAddrBasedDsUid, DaliDeviceInfo::devinf_notForID);
    // - check for named device in database consisting of this dimmer with shortaddr based dSUID
    //   Note that only single dimmer device are checked for, composite devices will not have this compatibility mechanism
    sqlite3pp::query qry(mDaliVdc.getVdcHost().getDsParamStore());
    // Note: this is a bit ugly, as it has the device settings table name hard coded
    string sql = string_format("SELECT deviceName FROM DeviceSettings WHERE parentID='%s'", shortAddrBasedDsUid.getString().c_str());
    if (qry.prepare(sql.c_str())==SQLITE_OK) {
      sqlite3pp::query::iterator i = qry.begin();
      if (i!=qry.end()) {
        // the length of the name
        string n = nonNullCStr(i->get<const char *>(0));
        if (n.length()>0) {
          // shortAddr based device has already been named. So keep that, and don't generate a devInf based dSUID
          mDeviceInfo->mDevInfStatus = DaliDeviceInfo::devinf_notForID;
          OLOG(LOG_WARNING, "kept with shortaddr-based dSUID because it is already named: '%s'", n.c_str());
        }
      }
    }
  }
  #endif // OLD_BUGGY_CHKSUM_COMPATIBLE
  dsUidForDeviceInfoStatus(mDSUID, mDeviceInfo->mDevInfStatus);
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
    s = string_format("(01)%llu(21)%llu", mDeviceInfo->mGtin, mDeviceInfo->mSerialNo);
  }
  else {
    // not uniquely identified by devInf (or shortaddr based version already in use):
    // - generate id in vDC namespace
    //   UUIDv5 with name = classcontainerinstanceid::daliShortAddrDecimal
    s = mDaliVdc.vdcInstanceIdentifier();
    string_format_append(s, "::%d", mDeviceInfo->mShortAddress);
  }
  aDsUid.setNameInSpace(s, vdcNamespace);
  if (!mDaliVdc.mDaliComm.mDali2LUNLock) {
    // DALI2 devices might have multiple logical units -> set subdevice index
    aDsUid.setSubdeviceIndex(mDeviceInfo->mLunIndex);
  }
  OLOG(LOG_INFO, "derives UID %s from %s", aDsUid.getString().c_str(), aDevInfStatus==DaliDeviceInfo::devinf_solid ? "devInf" : "shortAddr")
}



static const uint8_t devTypesOfInterest[] = {
  DT6_TYPE_LED,
  DT8_TYPE_COLOR,
  DT17_TYPE_DIMCURVE,
  0xFF // terminator
};

void DaliBusDevice::registerDeviceType(uint8_t aDeviceType, uint8_t aExtendedVersion)
{
  // note: getting aExtendedVersion==0 means device with only one additional type, extended version not queried
  OLOG(LOG_INFO, "supports device type %d, extended version %d.%d", aDeviceType, DALI_STD_VERS_MAJOR(aExtendedVersion), DALI_STD_VERS_MINOR(aExtendedVersion));
  switch (aDeviceType) {
    case DT6_TYPE_LED:
      mSupportsLED = true;
      break;
    case DT8_TYPE_COLOR:
      mSupportsDT8 = true;
      break;
    case DT17_TYPE_DIMCURVE:
      mFullySupportsDimcurve = true;
      break;
  }
}



void DaliBusDevice::queryFeatureSet(StatusCB aCompletedCB)
{
  // query device type(s) - i.e. availability of extended command sets
  mDaliVdc.mDaliComm.daliSendQuery(
    mDeviceInfo->mShortAddress,
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
      probeDeviceType(aCompletedCB, *devTypesOfInterest, devTypesOfInterest+1);
      return;
    }
    // single device type, query extended version
    probeDeviceType(aCompletedCB, aResponse, nullptr);
    return;
  }
  // done with device type, check groups now
  queryDTFeatures(aCompletedCB);
}


void DaliBusDevice::probeDeviceType(StatusCB aCompletedCB, uint8_t aDType, const uint8_t* aNextDtP)
{
  // query next device type
  mDaliVdc.mDaliComm.daliSend(DALICMD_ENABLE_DEVICE_TYPE, aDType);
  mDaliVdc.mDaliComm.daliSendQuery(
    mDeviceInfo->mShortAddress,
    DALICMD_QUERY_EXTENDED_VERSION,
    boost::bind(&DaliBusDevice::probeDeviceTypeResponse, this, aCompletedCB, aDType, aNextDtP, _1, _2, _3)
  );
}


void DaliBusDevice::probeDeviceTypeResponse(StatusCB aCompletedCB, uint8_t aDType, const uint8_t* aNextDtP, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
{
  // extended version type query response.
  if (Error::isOK(aError) && !aNoOrTimeout) {
    // extended version response
    // check device type
    registerDeviceType(aDType, aResponse);
  }
  // query next device type
  if (aNextDtP && *aNextDtP!=0xFF) {
    uint8_t dt = *aNextDtP++;
    probeDeviceType(aCompletedCB, dt, aNextDtP);
    return;
  }
  // all device types checked
  // done with device type, check DT features now
  queryDTFeatures(aCompletedCB);
}


void DaliBusDevice::queryDTFeatures(StatusCB aCompletedCB)
{
  if (mSupportsDT8) {
    mDaliVdc.mDaliComm.daliSendQuery(
      mDeviceInfo->mShortAddress,
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
    mDT8Color = (aResponse & 0x01)!=0; // x/y color model capable
    mDT8CT = (aResponse & 0x02)!=0; // mired color temperature capable
    mDT8PrimaryColors = (aResponse>>2) & 0x07; // bits 2..4 is the number of primary color channels available
    mDT8RGBWAFchannels = (aResponse>>5) & 0x07; // bits 5..7 is the number of RGBWAF channels available
    OLOG(LOG_INFO, "DT8 features byte = 0x%02X", aResponse);
    mDaliVdc.mDaliComm.daliSendQuery(
      mDeviceInfo->mShortAddress,
      DALICMD_DT8_QUERY_GEAR_STATUS,
      boost::bind(&DaliBusDevice::dt8GearStatusResponse, this, aCompletedCB, _1, _2, _3)
    );
    return;
  }
  if (aCompletedCB) aCompletedCB(aError);
}


void DaliBusDevice::dt8GearStatusResponse(StatusCB aCompletedCB, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
{
  // extended version type query response.
  if (Error::isOK(aError) && !aNoOrTimeout) {
    // DT8 gear status response
    mDT8AutoActivation = (aResponse & 0x01)!=0;
    OLOG(LOG_INFO, "DT8 has auto-activation on DAPC %sABLED", mDT8AutoActivation ? "EN" : "DIS");
  }
  if (aCompletedCB) aCompletedCB(aError);
}


void DaliBusDevice::getGroupMemberShip(DaliGroupsCB aDaliGroupsCB, DaliAddress aShortAddress)
{
  mDaliVdc.mDaliComm.daliSendQuery(
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
  mDaliVdc.mDaliComm.daliSendQuery(
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
  if (mIsDummy) {
    // dummies have no hardware to initialize
    if (aCompletedCB) aCompletedCB(ErrorPtr());
    return;
  }
  // make sure device is in none of the used groups
  if (aUsedGroupsMask==0) {
    // no groups in use at all, continue to initializing features
    initializeFeatures(aCompletedCB);
    return;
  }
  // need to query current groups
  getGroupMemberShip(boost::bind(&DaliBusDevice::groupMembershipResponse, this, aCompletedCB, aUsedGroupsMask, mDeviceInfo->mShortAddress, _1, _2), mDeviceInfo->mShortAddress);
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
  getGroupMemberShip(boost::bind(&DaliBusDevice::groupMembershipResponse, this, aCompletedCB, aUsedGroupsMask, mDeviceInfo->mShortAddress, _1, _2), mDeviceInfo->mShortAddress);
}


void DaliBusDevice::groupMembershipResponse(StatusCB aCompletedCB, uint16_t aUsedGroupsMask, DaliAddress aShortAddress, uint16_t aGroups, ErrorPtr aError)
{
  // remove groups that are in use on the bus
  if (Error::isOK(aError)) {
    for (int g=0; g<16; ++g) {
      if (aUsedGroupsMask & aGroups & (1<<g)) {
        // single device is member of a group in use -> remove it
        LOG(LOG_INFO, "- removing single DALI bus device with shortaddr %d from group %d", aShortAddress, g);
        mDaliVdc.mDaliComm.daliSendConfigCommand(aShortAddress, DALICMD_REMOVE_FROM_GROUP|g);
      }
    }
  }
  // initialize features now
  initializeFeatures(aCompletedCB);
}



void DaliBusDevice::initializeFeatures(StatusCB aCompletedCB)
{
  if (mIsDummy) {
    if (aCompletedCB) aCompletedCB(ErrorPtr());
    return;
  }
  if (mSupportsLED) {
    // Always use standard logarithmic dimming curve, as it gives more resolution in the low brightness levels
    // Note: the linear dimming curve is (unlike previously assumed) just direct PWM/Power output, and as such has
    //   very poor resolution at low brightness, as proper dim-to-zero needs more that 8bit PWM. Control Gear that
    //   has more than 8bit PWM/Power resolution can dim more smoothly using the standard (approx Gamma 5.5) DALI
    //   dimming curve.
    mDaliVdc.mDaliComm.daliSendDtrAndConfigCommand(mDeviceInfo->mShortAddress, DALICMD_DT6_SELECT_DIMMING_CURVE, 0); // standard logarithmic dimming curve
  }
  if (aCompletedCB) aCompletedCB(ErrorPtr());
}


void DaliBusDevice::updateParams(StatusCB aCompletedCB)
{
  mOutputSyncTicket.cancel(); // cancel possibly scheduled update
  if (mIsDummy) {
    if (aCompletedCB) aCompletedCB(ErrorPtr());
    return;
  }
  mDimRepeaterTicket.cancel(); // safety: stop dim repeater (should not be running now, but just in case)
  // query actual level
  mDaliVdc.mDaliComm.daliSendQuery(
    addressForQuery(),
    DALICMD_QUERY_ACTUAL_LEVEL,
    boost::bind(&DaliBusDevice::queryActualLevelResponse, this, aCompletedCB, _1, _2, _3)
  );
}


void DaliBusDevice::queryActualLevelResponse(StatusCB aCompletedCB, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
{
  mCurrentBrightness = 0; // default to 0
  if (Error::isOK(aError) && !aNoOrTimeout) {
    mIsPresent = true; // answering a query means presence
    // this is my current level, save it in brightness scale for dS system side queries (which will apply gamma in addition)
    mCurrentBrightness = daliLevelToBrightness(aResponse);
    OLOG(LOG_INFO, "retrieved current dimming level: DALI level = %d/0x%02X -> DALI brightness (no gamma corr) = %0.1f%%", aResponse, aResponse, mCurrentBrightness);
  }
  // next: query the minimum dimming level
  mDaliVdc.mDaliComm.daliSendQuery(
    addressForQuery(),
    DALICMD_QUERY_PHYSICAL_MINIMUM_LEVEL,
    boost::bind(&DaliBusDevice::queryMinLevelResponse, this, aCompletedCB, _1, _2, _3)
  );
}


void DaliBusDevice::queryMinLevelResponse(StatusCB aCompletedCB, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
{
  mMinBrightness = 0; // default to 0
  if (Error::isOK(aError) && !aNoOrTimeout) {
    mIsPresent = true; // answering a query means presence
    // this is the minimum dali level, in brightness scale for dS system side queries (which will apply gamma in addition)
    mMinBrightness = daliLevelToBrightness(aResponse);
    OLOG(LOG_INFO, "retrieved minimum dimming level: DALI level = %d/0x%02X -> DALI brightness (no gamma corr) = %0.1f%%", aResponse, aResponse, mMinBrightness);
  }
  if (mSupportsDT8) {
    // more queries on DT8 devices:
    // - color status
    mDaliVdc.mDaliComm.daliSendQuery(
      addressForQuery(),
      DALICMD_DT8_QUERY_COLOR_STATUS,
      boost::bind(&DaliBusDevice::queryColorStatusResponse, this, aCompletedCB, _1, _2, _3)
    );
    return;
  }
  mStaleParams = false;
  if (aCompletedCB) aCompletedCB(aError);
}


void DaliBusDevice::queryColorStatusResponse(StatusCB aCompletedCB, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
{
  if (Error::isOK(aError) && !aNoOrTimeout) {
    // current mode
    if (aResponse & 0x10) {
      // CIE x/y is active
      mCurrentColorMode = colorLightModeXY;
      // - query X
      mDaliVdc.mDaliComm.daliSendDtrAnd16BitQuery(
        addressForQuery(),
        DALICMD_DT8_QUERY_COLOR_VALUE, 0, // DTR==0 -> X coordinate
        boost::bind(&DaliBusDevice::queryXCoordResponse, this, aCompletedCB, _1, _2)
      );
      return;
    }
    else if (aResponse & 0x20) {
      // CT is active
      mCurrentColorMode = colorLightModeCt;
      // - query CT
      mDaliVdc.mDaliComm.daliSendDtrAnd16BitQuery(
        addressForQuery(),
        DALICMD_DT8_QUERY_COLOR_VALUE, 2, // DTR==2 -> CT value
        boost::bind(&DaliBusDevice::queryCTResponse, this, aCompletedCB, _1, _2)
      );
      return;
    }
    // TODO: implement
//    else if (aResponse & 0x40) {
//      // Primary N is active
//      %%% retrieve it
//      return;
//    }
    else if (aResponse & 0x80) {
      // RGBWA(F) is active
      mCurrentColorMode = colorLightModeRGBWA;
      mCurrentW = 0;
      mCurrentA = 0;
      // - query RGBWA (no F supported, WA optional, RGB mandatory)
      if (mDT8RGBWAFchannels>=3) {
        mDaliVdc.mDaliComm.daliSendDtrAnd16BitQuery(
          addressForQuery(),
          DALICMD_DT8_QUERY_COLOR_VALUE, 233, // DTR==233..237 -> R,G,B,W,A Dimlevels
          boost::bind(&DaliBusDevice::queryRGBWAFResponse, this, aCompletedCB, 0, _1, _2)
        );
        return;
      }
    }
  }
  // no more queries
  mStaleParams = false;
  if (aCompletedCB) aCompletedCB(aError);
}


void DaliBusDevice::queryXCoordResponse(StatusCB aCompletedCB, uint16_t aResponse16, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    if (aResponse16==0xFFFF) {
      mCurrentColorMode = colorLightModeNone;
    }
    else {
      mCurrentXorCT = aResponse16;
      // also query Y
      mDaliVdc.mDaliComm.daliSendDtrAnd16BitQuery(
        addressForQuery(),
        DALICMD_DT8_QUERY_COLOR_VALUE, 1, // DTR==1 -> Y coordinate
        boost::bind(&DaliBusDevice::queryYCoordResponse, this, aCompletedCB, _1, _2)
      );
      return;
    }
  }
  mStaleParams = false;
  if (aCompletedCB) aCompletedCB(aError);
}


void DaliBusDevice::queryYCoordResponse(StatusCB aCompletedCB, uint16_t aResponse16, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    mCurrentY = aResponse16;
    OLOG(LOG_INFO, "DT8 is in CIE X/Y color mode, X=%.3f, Y=%.3f", (double)mCurrentXorCT/65536, (double)mCurrentY/65536);
  }
  mStaleParams = false;
  if (aCompletedCB) aCompletedCB(aError);
}


void DaliBusDevice::queryCTResponse(StatusCB aCompletedCB, uint16_t aResponse16, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    if (aResponse16==0xFFFF) {
      mCurrentColorMode = colorLightModeNone;
    }
    else {
      mCurrentXorCT = aResponse16;
      OLOG(LOG_INFO, "DT8 is in Tunable White mode, CT=%hd mired", mCurrentXorCT);
    }
  }
  mStaleParams = false;
  if (aCompletedCB) aCompletedCB(aError);
}


void DaliBusDevice::queryRGBWAFResponse(StatusCB aCompletedCB, uint16_t aResIndex, uint16_t aResponse16, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    // save answer
    aResponse16 = aResponse16>>8; // MSB contains 8-bit dim level data, LSB is always 0
    switch (aResIndex) {
      case 0 : mCurrentR = aResponse16; break;
      case 1 : mCurrentG = aResponse16; break;
      case 2 : mCurrentB = aResponse16; break;
      case 3 : mCurrentW = aResponse16; break;
      case 4 : mCurrentA = aResponse16; break;
    }
  }
  else {
    OLOG(LOG_DEBUG, "querying DT8 color value %d returned error: %s", aResIndex, aError->text());
  }
  aResIndex++;
  if (aResIndex>=mDT8RGBWAFchannels) {
    // all values queried
    OLOG(LOG_INFO, "DT8 is in RGBWAF mode, R=%d, G=%d, B=%d, W=%d, A=%d", mCurrentR, mCurrentG, mCurrentB, mCurrentW, mCurrentA);
  }
  else {
    // query next component
    mDaliVdc.mDaliComm.daliSendDtrAnd16BitQuery(
      addressForQuery(),
      DALICMD_DT8_QUERY_COLOR_VALUE, 233+aResIndex, // DTR==233..237 -> R,G,B,W,A Dimlevels
      boost::bind(&DaliBusDevice::queryRGBWAFResponse, this, aCompletedCB, aResIndex, _1, _2)
    );
    return;
  }
  mStaleParams = false;
  if (aCompletedCB) aCompletedCB(aError);
}





void DaliBusDevice::updateStatus(StatusCB aCompletedCB)
{
  if (mIsDummy) {
    if (aCompletedCB) aCompletedCB(ErrorPtr());
    return;
  }
  mDimRepeaterTicket.cancel(); // safety: stop dim repeater (should not be running now, but just in case)
  // query the device for status
  mDaliVdc.mDaliComm.daliSendQuery(
    addressForQuery(),
    DALICMD_QUERY_STATUS,
    boost::bind(&DaliBusDevice::queryStatusResponse, this, aCompletedCB, _1, _2, _3)
  );
}


void DaliBusDevice::queryStatusResponse(StatusCB aCompletedCB, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError)
{
  if (Error::isOK(aError) && !aNoOrTimeout) {
    mIsPresent = true; // answering a query means presence
    // check status bits
    // - bit1 = lamp failure
    mLampFailure = aResponse & 0x02;
  }
  else {
    if (Error::notOK(aError)) {
      // errors are always bus level: set global error status
      mDaliVdc.setVdcError(aError);
    }
    mIsPresent = false; // no correct status -> not present
  }
  // done updating status
  if (aCompletedCB) aCompletedCB(aError);
}



void DaliBusDevice::setTransitionTime(MLMicroSeconds aTransitionTime)
{
  if (mIsDummy) return;
  if (mCurrentTransitionTime==Infinite || mCurrentTransitionTime!=aTransitionTime) {
    uint8_t tr = 0; // default to 0
    if (aTransitionTime>0) {
      // Fade time: T = 0.5 * SQRT(2^X) [seconds] -> x = ln2((T/0.5)^2) : T=0.25 [sec] -> x = -2, T=10 -> 8.64
      double h = (((double)aTransitionTime/Second)/0.5);
      h = h*h;
      h = ::log(h)/::log(2);
      tr = h>1 ? (uint8_t)h : 1;
      OLOG(LOG_DEBUG, "new transition time = %.1f mS, ln2((T/0.5)^2) = %f -> FADE_TIME = %d", (double)aTransitionTime/MilliSecond, h, (int)tr);
    }
    if (tr!=mCurrentFadeTime || mCurrentTransitionTime==Infinite) {
      OLOG(LOG_INFO, "setting DALI FADE_TIME from %d to %d (for transition time %.1f mS)", mCurrentFadeTime, (int)tr, (double)aTransitionTime/MilliSecond);
      mDaliVdc.mDaliComm.daliSendDtrAndConfigCommand(mDeviceInfo->mShortAddress, DALICMD_STORE_DTR_AS_FADE_TIME, tr);
      mCurrentFadeTime = tr;
    }
    mCurrentTransitionTime = aTransitionTime;
  }
}


bool DaliBusDevice::setBrightness(Brightness aBrightness)
{
  if (mIsDummy) return false;
  mDimRepeaterTicket.cancel(); // safety: stop dim repeater (should not be running now, but just in case)
  mOutputSyncTicket.cancel(); // setting new values now, no need to sync
  if (mCurrentBrightness!=aBrightness || mStaleParams) {
    mStaleParams = false; // consider everything applied now
    mCurrentBrightness = aBrightness;
    uint8_t power = brightnessToDaliLevel(aBrightness);
    OLOG(LOG_INFO, "setting new brightness (gamma applied) = %0.2f, DALI level = %d/0x%02X", aBrightness, (int)power, (int)power);
    mDaliVdc.mDaliComm.daliSendDirectPower(mDeviceInfo->mShortAddress, power);
    return true; // changed
  }
  return false; // not changed
}


void DaliBusDevice::setDefaultBrightness(Brightness aBrightness)
{
  if (mIsDummy) return;
  mDimRepeaterTicket.cancel(); // safety: stop dim repeater (should not be running now, but just in case)
  if (aBrightness<0) aBrightness = mCurrentBrightness; // use current brightness
  uint8_t power = brightnessToDaliLevel(aBrightness);
  OLOG(LOG_INFO, "setting default/failure brightness (gamma applied) = %0.2f, DALI level = %d/0x%02X", aBrightness, (int)power, (int)power);
  mDaliVdc.mDaliComm.daliSendDtrAndConfigCommand(mDeviceInfo->mShortAddress, DALICMD_STORE_DTR_AS_POWER_ON_LEVEL, power);
  mDaliVdc.mDaliComm.daliSendDtrAndConfigCommand(mDeviceInfo->mShortAddress, DALICMD_STORE_DTR_AS_FAILURE_LEVEL, power);
}



bool DaliBusDevice::setColorParams(ColorLightMode aMode, double aCieXorCT, double aCieY, bool aAlways)
{
  bool changed = aAlways || mStaleParams;
  mOutputSyncTicket.cancel(); // setting new values now, no need to sync
  mDimRepeaterTicket.cancel(); // safety: stop dim repeater (should not be running now, but just in case)
  if (mSupportsDT8) {
    if (mCurrentColorMode!=aMode) {
      changed = true; // change in mode always means change in parameter
      mCurrentColorMode = aMode;
    }
    if (mCurrentColorMode==colorLightModeCt) {
      if (changed || mCurrentXorCT!=aCieXorCT) {
        mCurrentXorCT = aCieXorCT; // 1:1 in mired
        changed = true;
        mCurrentY = 0;
        if (mDT8CT) {
          mDaliVdc.mDaliComm.daliSend16BitValueAndCommand(mDeviceInfo->mShortAddress, DALICMD_DT8_SET_TEMP_CT, mCurrentXorCT);
        }
      }
    }
    else {
      uint16_t x = aCieXorCT*65536;
      uint16_t y = aCieY*65536;
      if (changed || mCurrentXorCT!=x || mCurrentY!=y) {
        mCurrentXorCT = x;
        mCurrentY = y;
        changed = true;
        if (mDT8Color) {
          mDaliVdc.mDaliComm.daliSend16BitValueAndCommand(mDeviceInfo->mShortAddress, DALICMD_DT8_SET_TEMP_XCOORD, mCurrentXorCT);
          mDaliVdc.mDaliComm.daliSend16BitValueAndCommand(mDeviceInfo->mShortAddress, DALICMD_DT8_SET_TEMP_YCOORD, mCurrentY);
        }
      }
    }
  }
  return changed;
}


bool DaliBusDevice::setRGBWAParams(uint8_t aR, uint8_t aG, uint8_t aB, uint8_t aW, uint8_t aA, bool aAlways)
{
  bool changed = aAlways || mStaleParams;
  mDimRepeaterTicket.cancel(); // safety: stop dim repeater (should not be running now, but just in case)
  if (mSupportsDT8) {
    if (mCurrentColorMode!=colorLightModeRGBWA) {
      changed = true; // change in mode always means change in parameter
      mCurrentColorMode = colorLightModeRGBWA;
    }
    if (changed || aR!=mCurrentR || aG!=mCurrentG || aB!=mCurrentB || aW!=mCurrentW || aA!=mCurrentA) {
      // set the mode (channel control)
      mDaliVdc.mDaliComm.daliSendDtrAndCommand(mDeviceInfo->mShortAddress, DALICMD_DT8_SET_TEMP_RGBWAF_CTRL, 0x2<<6); // all not linked, normalised colour control
      if (changed || aR!=mCurrentR || aG!=mCurrentG || aB!=mCurrentB) {
        // set the RGB color values
        mCurrentR = aR;
        mCurrentG = aG;
        mCurrentB = aB;
        mDaliVdc.mDaliComm.daliSend3x8BitValueAndCommand(mDeviceInfo->mShortAddress, DALICMD_DT8_SET_TEMP_RGB, mCurrentR, mCurrentG, mCurrentB);
      }
      if (mDT8RGBWAFchannels>3) {
        if (changed || aW!=mCurrentW || aA!=mCurrentA) {
          // set W and A values (we don't have F)
          mCurrentW = aW;
          mCurrentA = aA;
          mDaliVdc.mDaliComm.daliSend3x8BitValueAndCommand(mDeviceInfo->mShortAddress, DALICMD_DT8_SET_TEMP_WAF, mCurrentW, mCurrentA, 0); // no F
        }
      }
      changed = true;
    }
  }
  return changed;
}


bool DaliBusDevice::setColorParamsFromChannels(ColorLightBehaviourPtr aColorLight, bool aTransitional, bool aAlways, bool aSilent)
{
  bool hasNewColors = false;
  RGBColorLightBehaviourPtr rgbl = boost::dynamic_pointer_cast<RGBColorLightBehaviour>(aColorLight);
  if (rgbl) {
    // DALI controller needs direct RGBWA(F)
    double r=0,g=0,b=0,w=0,a=0;
    if (mDT8RGBWAFchannels>3) {
      // RGBW or RGBWA
      if (mDT8RGBWAFchannels>4) {
        // RGBWA
        rgbl->getRGBWA(r, g, b, w, a, 254, true, aTransitional);
        if (!aSilent) {
          SOLOG(aColorLight->getDevice(), LOG_INFO, "DALI DT8 RGBWA: setting R=%d, G=%d, B=%d, W=%d, A=%d", (int)r, (int)g, (int)b, (int)w, (int)a);
        }
      }
      else {
        // RGBW
        rgbl->getRGBW(r, g, b, w, 254, true, aTransitional);
        if (!aSilent) {
          SOLOG(aColorLight->getDevice(), LOG_INFO, "DALI DT8 RGBW: setting R=%d, G=%d, B=%d, W=%d", (int)r, (int)g, (int)b, (int)w);
        }
      }
    }
    else {
      // RGB
      rgbl->getRGB(r, g, b, 254, true, aTransitional);
      if (!aSilent) {
        SOLOG(aColorLight->getDevice(), LOG_INFO, "DALI DT8 RGB: setting R=%d, G=%d, B=%d", (int)r, (int)g, (int)b);
      }
    }
    hasNewColors = setRGBWAParams(r, g, b, w, a, aAlways);
  }
  else {
    // DALI controller understands Cie and/or Ct directly
    if (mDT8Color && (aColorLight->mColorMode!=colorLightModeCt || !mDT8CT)) {
      // color is requested or CT is requested but controller cannot do CT natively -> send CieX/Y
      double cieX, cieY;
      aColorLight->getCIExy(cieX, cieY, aTransitional);
      if (!aSilent) {
        SOLOG(aColorLight->getDevice(), LOG_INFO, "DALI DT8 CIE: setting x=%.3f, y=%.3f", cieX, cieY);
      }
      hasNewColors = setColorParams(colorLightModeXY, cieX, cieY, aAlways);
    }
    else {
      // CT is requested and controller can do CT natively, or color is requested but controller can ONLY do CT
      double mired;
      aColorLight->getCT(mired, aTransitional);
      if (!aSilent) {
        SOLOG(aColorLight->getDevice(), LOG_INFO, "DALI DT8 CT: setting mired=%d", (int)mired);
      }
      hasNewColors = setColorParams(colorLightModeCt, mired, 0, aAlways);
    }
  }
  return hasNewColors;
}


void DaliBusDevice::activateColorParams()
{
  mDimRepeaterTicket.cancel(); // safety: stop dim repeater (should not be running now, but just in case)
  mOutputSyncTicket.cancel(); // setting new values now, no need to sync
  mStaleParams = false; // new params activated, not stale any more
  if (mSupportsDT8) {
    mDaliVdc.mDaliComm.daliSendCommand(mDeviceInfo->mShortAddress, DALICMD_DT8_ACTIVATE);
  }
}


uint8_t DaliBusDevice::brightnessToDaliLevel(Brightness aBrightness)
{
  if (aBrightness<0) aBrightness = 0;
  if (aBrightness>100) aBrightness = 100;
  // 0..254, 255 is MASK and is reserved to stop fading
  // Note: aBrightness is gamma corrected via LightBehaviour already, just scale 0..100 -> 0..254
  return aBrightness*2.54; // linear 0..254
}



Brightness DaliBusDevice::daliLevelToBrightness(int aDaliLevel)
{
  return (double)aDaliLevel/2.54;
}



// MARK: - Optimized DALI dimming implementation



void DaliBusDevice::dimPrepare(VdcDimMode aDimMode, double aDimPerMS, StatusCB aCompletedCB)
{
  if (!mIsDummy && aDimMode!=dimmode_stop) {
    // - configure new fade rate if current does not match
    if (aDimPerMS!=mCurrentDimPerMS) {
      mCurrentDimPerMS = aDimPerMS;
      //   Fade rate: R = 506/SQRT(2^X) [steps/second] -> x = ln2((506/R)^2) : R=44 [steps/sec] -> x = 7
      double h = 506.0/(mCurrentDimPerMS*1000);
      h = ::log(h*h)/::log(2);
      uint8_t fr = h>0 ? (uint8_t)h : 0;
      OLOG(LOG_DEBUG, "new dimming rate = %f steps/second, calculated FADE_RATE setting = %f (rounded %d)", mCurrentDimPerMS*1000, h, fr);
      if (fr!=mCurrentFadeRate) {
        OLOG(LOG_INFO, "setting DALI FADE_RATE to %d for dimming at %f steps/second", fr, mCurrentDimPerMS*1000);
        mCurrentFadeRate = fr;
        mDaliVdc.mDaliComm.daliSendDtrAndConfigCommand(mDeviceInfo->mShortAddress, DALICMD_STORE_DTR_AS_FADE_RATE, fr, boost::bind(&DaliBusDevice::dimPrepared, this, aCompletedCB, _1));
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
  if (mIsDummy) return;
  mDimRepeaterTicket.cancel(); // stop any previous dimming activity
  mCurrentDimMode = aDimMode;
  // Use DALI UP/DOWN dimming commands
  if (aDimMode==dimmode_stop) {
    // stop dimming - send MASK
    mDaliVdc.mDaliComm.daliSendDirectPower(mDeviceInfo->mShortAddress, DALIVALUE_MASK);
  }
  else {
    // prepare dimming and then call dimRepeater
    dimPrepare(aDimMode, aDimPerMS, boost::bind(&DaliBusDevice::dimStart, this, mDeviceInfo->mShortAddress, aDimMode==dimmode_up ? DALICMD_UP : DALICMD_DOWN));
  }
}


void DaliBusDevice::dimStart(DaliAddress aDaliAddress, DaliCommand aCommand)
{
  if (mCurrentDimMode==dimmode_stop) return; // do not execute delayed starts in case stop came before start preparation was complete
  mDimRepeaterTicket.executeOnce(boost::bind(&DaliBusDevice::dimRepeater, this, aDaliAddress, aCommand, _1));
}


void DaliBusDevice::dimRepeater(DaliAddress aDaliAddress, DaliCommand aCommand, MLTimer &aTimer)
{
  if (mCurrentDimMode==dimmode_stop) return; // safety: do not continue dimming if stopped
  mDaliVdc.mDaliComm.daliSendCommand(aDaliAddress, aCommand);
  // schedule next command
  // - DALI UP and DOWN run 200mS, but can be repeated earlier
  //   Note: DALI bus speed limits commands to 120Bytes/sec max, i.e. about 20 per 200mS, i.e. max 10 lamps dimming
  MainLoop::currentMainLoop().retriggerTimer(aTimer, 200*MilliSecond);
}



// MARK: - DaliBusDeviceGroup (multiple DALI devices, addressed as a group, forming single channel dimmer)


DaliBusDeviceGroup::DaliBusDeviceGroup(DaliVdc &aDaliVdc, uint8_t aGroupNo) :
  inherited(aDaliVdc),
  mGroupMaster(NoDaliAddress)
{
  mMixID.erase(); // no members yet
  // assume max features, will be reduced to what all group members are capable in addDaliBusDevice()
  mSupportsLED = true;
  mFullySupportsDimcurve = true;
  mSupportsDT8 = true;
  mDT8Color = true;
  mDT8CT = true;
  // set the group address to use
  mDeviceInfo->mShortAddress = aGroupNo|DaliGroup;
}


void DaliBusDeviceGroup::addDaliBusDevice(DaliBusDevicePtr aDaliBusDevice)
{
  // - when completely unlocked (no backward compatibility needed), use the improved mix which adds a FNV hash
  //   including the subdevice index, so mixing multiple dSUIDs only differing in subdevice byte
  //   don't cancel each other out completely (even #) or match the original dSUID (odd # of mixed dSUIDs)
  bool lunSafeMix = !mDaliVdc.mDaliComm.mDali2ScanLock && !mDaliVdc.mDaliComm.mDali2LUNLock;
  // add the ID to the mix
  LOG(LOG_NOTICE, "- DALI bus device %s (UID=%s) gets grouped in DALI group %d (%s UID mix)", DaliComm::formatDaliAddress(aDaliBusDevice->mDeviceInfo->mShortAddress).c_str(), aDaliBusDevice->mDSUID.getString().c_str(), mDeviceInfo->mShortAddress & DaliGroupMask, lunSafeMix ? "LUN safe" : "legacy");
  aDaliBusDevice->mDSUID.xorDsUidIntoMix(mMixID, lunSafeMix);
  // if this is the first valid device, use it as master
  if (mGroupMaster==NoDaliAddress && !aDaliBusDevice->mIsDummy) {
    // this is the master device
    LOG(LOG_INFO, "- DALI bus device %s is master of the group (queried for brightness, mindim)", DaliComm::formatDaliAddress(aDaliBusDevice->mDeviceInfo->mShortAddress).c_str());
    mGroupMaster = aDaliBusDevice->mDeviceInfo->mShortAddress;
  }
  // reduce features to common denominator for all group members
  if (!aDaliBusDevice->mSupportsLED) mSupportsLED = false;
  if (!aDaliBusDevice->mFullySupportsDimcurve) mFullySupportsDimcurve = false;
  if (!aDaliBusDevice->mSupportsDT8) mSupportsDT8 = false;
  if (!aDaliBusDevice->mDT8Color) mSupportsDT8 = false;
  if (!aDaliBusDevice->mDT8CT) mDT8CT = false;
  // add member (can be dummy with shortaddress NoDaliAddress)
  mGroupMembers.push_back(aDaliBusDevice->mDeviceInfo->mShortAddress);
}


string DaliBusDeviceGroup::description()
{
  string g;
  for (DaliComm::ShortAddressList::const_iterator pos = mGroupMembers.begin(); pos!=mGroupMembers.end(); ++pos) {
    if (!g.empty()) g +=", ";
    if (*pos==NoDaliAddress) g += "<missing>"; // dummy
    else string_format_append(g, "%02d", *pos);
  }
  string s = string_format("\n- DALI group #%d - device bus addresses: %s", mDeviceInfo->mShortAddress & DaliGroupMask, g.c_str());
  s + inherited::description();
  return s;
}


bool DaliBusDeviceGroup::belongsToShortAddr(DaliAddress aDaliAddress) const
{
  if ((aDaliAddress&DaliAddressTypeMask)==DaliGroup) {
    // looking for group membership
    return mDeviceInfo && aDaliAddress==mDeviceInfo->mShortAddress;
  }
  else if ((aDaliAddress&DaliAddressTypeMask)==DaliSingle) {
    // looking for single address, check members
    for (DaliComm::ShortAddressList::const_iterator pos = mGroupMembers.begin(); pos!=mGroupMembers.end(); ++pos) {
      if (aDaliAddress==*pos) return true;
    }
  }
  return false;
}


void DaliBusDeviceGroup::daliBusDeviceSummary(ApiValuePtr aInfo) const
{
  // general info
  inherited::daliBusDeviceSummary(aInfo);
  // group specifics
  aInfo->add("groupMasterAddr", aInfo->newUint64(mGroupMaster));
  // override devInf status, as group does not have a devInf
  // - the ID is not real, but a mix of IDs as captured at group creation
  aInfo->add("devInfStatus", aInfo->newString("group id (virtual)"));
  // - the ID is stable as long as the group exists
  //   (when members change their address, they will be reported missing in the group, but the
  //   original ID will still be included in the mix, so the mix remains stable)
  aInfo->add("reliableId", aInfo->newBool(true));
}




void DaliBusDeviceGroup::initialize(StatusCB aCompletedCB, uint16_t aUsedGroupsMask)
{
  DaliComm::ShortAddressList::iterator pos = mGroupMembers.begin();
  initNextGroupMember(aCompletedCB, pos);
}


void DaliBusDeviceGroup::initNextGroupMember(StatusCB aCompletedCB, DaliComm::ShortAddressList::iterator aNextMember)
{
  while (aNextMember!=mGroupMembers.end()) {
    // another member, query group membership, then adjust if needed
    if (*aNextMember==NoDaliAddress) {
      // skip dummies
      ++aNextMember;
      continue;
    }
    // need to query current groups
    getGroupMemberShip(
      boost::bind(&DaliBusDeviceGroup::groupMembershipResponse, this, aCompletedCB, aNextMember, _1, _2),
      *aNextMember
    );
    return;
  }
  // group membership is now configured correctly
  // Now we can initialize the features for the entire group
  initializeFeatures(aCompletedCB);
}


void DaliBusDeviceGroup::groupMembershipResponse(StatusCB aCompletedCB, DaliComm::ShortAddressList::iterator aNextMember, uint16_t aGroups, ErrorPtr aError)
{
  uint8_t groupNo = mDeviceInfo->mShortAddress & DaliGroupMask;
  // make sure device is member of the group
  if ((aGroups & (1<<groupNo))==0) {
    // is not yet member of this group -> add it
    LOG(LOG_INFO, "- making DALI bus device with shortaddr %d member of group %d", *aNextMember, groupNo);
    mDaliVdc.mDaliComm.daliSendConfigCommand(*aNextMember, DALICMD_ADD_TO_GROUP|groupNo);
  }
  // remove from all other groups
  aGroups &= ~(1<<groupNo); // do not remove again from target group
  for (groupNo=0; groupNo<16; groupNo++) {
    if (aGroups & (1<<groupNo)) {
      // device is member of a group it shouldn't be in -> remove it
      LOG(LOG_INFO, "- removing DALI bus device with shortaddr %d from group %d", *aNextMember, groupNo);
      mDaliVdc.mDaliComm.daliSendConfigCommand(*aNextMember, DALICMD_REMOVE_FROM_GROUP|groupNo);
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
  mDSUID.setNameInSpace("daligroup:"+mMixID, vdcNamespace);
}


// MARK: - DaliOutputDevice (base class)


DaliOutputDevice::DaliOutputDevice(DaliVdc *aVdcP) :
  Device((Vdc *)aVdcP)
{
  // DALI output devices are always light (in this implementation, at least)
  setColorClass(class_yellow_light);
}


DaliVdc &DaliOutputDevice::daliVdc()
{
  return *(static_cast<DaliVdc *>(mVdcP));
}


void DaliOutputDevice::daliDeviceContextSummary(ApiValuePtr aInfo) const
{
  aInfo->add("dSUID", aInfo->newString(mDSUID.getString()));
  aInfo->add("dSDeviceName", aInfo->newString(const_cast<DaliOutputDevice *>(this)->getName())); // TODO: remove once constness in getters is correct
  aInfo->add("dSDeviceModel", aInfo->newString(const_cast<DaliOutputDevice *>(this)->modelName())); // TODO: remove once constness in getters is correct
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
  else if (aMethod=="x-p44-daliSummary") {
    // summary: returns documentation about DALI bus devices related to this device
    ApiValuePtr summary = aRequest->newApiValue();
    summary->setType(apivalue_object);
    daliDeviceSummary(summary);
    aRequest->sendResult(summary);
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
  bool withBrightness = false;
  MLMicroSeconds transitionTime = 0;
  if (l && needsToApplyChannels(&transitionTime)) {
    // abort previous multi-step transition
    mTransitionTicket.cancel();
    // prepare transition time
    MLMicroSeconds trinit = -1; // assume no multistep transition needed (i.e. single step)
    MLMicroSeconds stepTime = transitionTime;
    if (stepTime>MAX_SINGLE_STEP_TRANSITION_TIME) {
      stepTime = SLOW_TRANSITION_STEP_TIME;
      trinit = 0; // need multiple steps
    }
    // apply transition (step) time to hardware
    setTransitionTime(stepTime);
    // prepare multi-step transition if needed
    if (l->brightnessNeedsApplying()) {
      l->updateBrightnessTransition(trinit); // init brightness transition
      withBrightness = true;
    }
    // see if we also need a color transition
    ColorLightBehaviourPtr cl = getOutput<ColorLightBehaviour>();
    if (cl && cl->deriveColorMode()) {
      // colors will change as well
      cl->updateColorTransition(trinit); // init color transition
      withColor = true;
    }
    // start transition
    applyChannelValueSteps(aForDimming, withColor, withBrightness);
    // transition is initiated
    if (cl) {
      cl->appliedColorValues();
    }
    l->brightnessApplied();
  }
  // always consider apply done, even if transition is still running
  inherited::applyChannelValues(aDoneCB, aForDimming);
}



// MARK: - DaliSingleControllerDevice (single channel)


DaliSingleControllerDevice::DaliSingleControllerDevice(DaliVdc *aVdcP) :
  DaliOutputDevice(aVdcP)
{
}


bool DaliSingleControllerDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Note: setting up behaviours late, because we want the brightness dimmer already assigned for the hardware name
  if (mDaliController->mSupportsDT8) {
    // see if we need to do the RGBWA conversion here
    if (!mDaliController->mDT8Color && !mDaliController->mDT8CT && mDaliController->mDT8RGBWAFchannels>=3) {
      // set up dS behaviour for RGB(WA) light
      installSettings(DeviceSettingsPtr(new ColorLightDeviceSettings(*this)));
      // set the behaviour
      RGBColorLightBehaviourPtr rgbl = RGBColorLightBehaviourPtr(new RGBColorLightBehaviour(*this, false));
      rgbl->setDefaultGamma(daliVdc().defaultGamma());
      rgbl->setHardwareOutputConfig(outputFunction_colordimmer, outputmode_gradual, usage_undefined, true, 0); // DALI lights are always dimmable, no power known
      rgbl->setHardwareName("DALI DT8 RGB(WA) light");
      rgbl->initMinBrightness(0.4); // min brightness is 0.4 (~= 1/256)
      addBehaviour(rgbl);
    }
    else {
      // set up dS behaviour for color or CT light
      installSettings(DeviceSettingsPtr(new ColorLightDeviceSettings(*this)));
      // set the behaviour
      bool ctOnly = mDaliController->mDT8CT && !mDaliController->mDT8Color;
      ColorLightBehaviourPtr cl = ColorLightBehaviourPtr(new ColorLightBehaviour(*this, ctOnly));
      cl->setDefaultGamma(daliVdc().defaultGamma());
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
    l->setDefaultGamma(daliVdc().defaultGamma());
    l->setHardwareOutputConfig(outputFunction_dimmer, outputmode_gradual, usage_undefined, true, 160); // DALI ballasts are always dimmable, // TODO: %%% somewhat arbitrary 2*80W max wattage
    if (daliTechnicalType()==dalidevice_group)
      l->setHardwareName(string_format("DALI dimmer group # %d",mDaliController->mDeviceInfo->mShortAddress & DaliGroupMask));
    else
      l->setHardwareName(string_format("DALI dimmer @ %d",mDaliController->mDeviceInfo->mShortAddress));
    addBehaviour(l);
  }
  // - derive the DsUid
  deriveDsUid();
  // done
  return true; // simple identification, callback will not be called
}


bool DaliSingleControllerDevice::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (mDaliController->mSupportsDT8) {
    if (mDaliController->mDT8Color) {
      if (getIcon("dali_color", aIcon, aWithData, aResolutionPrefix))
        return true;
    }
    else {
      if (getIcon("dali_ct", aIcon, aWithData, aResolutionPrefix))
        return true;
    }
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
  return "DALI "+DaliComm::formatDaliAddress(mDaliController->mDeviceInfo->mShortAddress);
}


int DaliSingleControllerDevice::opStateLevel()
{
  if (mDaliController->mLampFailure || !mDaliController->mIsPresent) return 0;
  if (mDaliController->mDeviceInfo->mDevInfStatus!=DaliDeviceInfo::devinf_solid && !mDaliController->isGrouped()) return 50; // is not a recommended device (but is not a group), does not have unique ID
  return 100; // everything's fine
}


string DaliSingleControllerDevice::getOpStateText()
{
  string t;
  string sep;
  if (mDaliController->mLampFailure) {
    t += "lamp failure";
    sep = ", ";
  }
  if (mDaliController->mDeviceInfo->mDevInfStatus!=DaliDeviceInfo::devinf_solid && !mDaliController->isGrouped()) {
    t += sep + "Missing S/N";
    sep = ", ";
  }
  return t;
}


void DaliSingleControllerDevice::initializeDevice(StatusCB aCompletedCB, bool aFactoryReset)
{
  // - sync cached channel values from actual device
  mDaliController->updateParams(boost::bind(&DaliSingleControllerDevice::daliControllerSynced, this, aCompletedCB, aFactoryReset, _1));
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
      if (mDaliController->mDT8RGBWAFchannels>3) {
        if (mDaliController->mDT8RGBWAFchannels>4) {
          // RGBWA
          rgbl->setRGBWA(mDaliController->mCurrentR, mDaliController->mCurrentG, mDaliController->mCurrentB, mDaliController->mCurrentW, mDaliController->mCurrentA, 255, true);
        }
        else {
          // RGBW
          rgbl->setRGBW(mDaliController->mCurrentR, mDaliController->mCurrentG, mDaliController->mCurrentB, mDaliController->mCurrentW, 255, true);
        }
      }
      else {
        rgbl->setRGB(mDaliController->mCurrentR, mDaliController->mCurrentG, mDaliController->mCurrentB, 255, true);
      }
      // color tone is set, now sync back current brightness, as RGBWA values are not absolute, but relative to brightness
      rgbl->syncBrightnessFromHardware(mDaliController->mCurrentBrightness);
    }
    else {
      // save brightness now
      // initialize the light behaviour with the minimal dimming level
      LightBehaviourPtr l = getOutput<LightBehaviour>();
      l->syncBrightnessFromHardware(mDaliController->mCurrentBrightness);  // already includes gamma correction
      l->initMinBrightness(l->outputToBrightness(mDaliController->mMinBrightness, 100)); // include gamma correction explicitly
      ColorLightBehaviourPtr cl = getOutput<ColorLightBehaviour>();
      if (cl) {
        // also synchronize color information
        cl->mColorMode = mDaliController->mCurrentColorMode;
        if (mDaliController->mCurrentColorMode==colorLightModeCt) {
          // - tunable white mode
          cl->mCt->syncChannelValue(mDaliController->mCurrentXorCT);
        }
        else if (mDaliController->mCurrentColorMode==colorLightModeXY) {
          // - X/Y color mode
          cl->mCIEx->syncChannelValue((double)mDaliController->mCurrentXorCT/65536);
          cl->mCIEy->syncChannelValue((double)mDaliController->mCurrentY/65536);
        }
      }
    }
  }
  else {
    OLOG(LOG_ERR, "Single controller: Error getting state/params from dimmer: %s", aError->text());
  }
}



void DaliSingleControllerDevice::checkPresence(PresenceCB aPresenceResultHandler)
{
  // query the device
  mDaliController->updateStatus(boost::bind(&DaliSingleControllerDevice::checkPresenceResponse, this, aPresenceResultHandler));
}


void DaliSingleControllerDevice::checkPresenceResponse(PresenceCB aPresenceResultHandler)
{
  // present if a proper YES (without collision) received
  aPresenceResultHandler(mDaliController->mIsPresent);
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


void DaliSingleControllerDevice::stopTransitions()
{
  inherited::stopTransitions();
  // stop any ongoing dimming/transitioning operation
  mDaliController->dim(dimmode_stop, 0);
}


void DaliSingleControllerDevice::applyChannelValueSteps(bool aForDimming, bool aWithColor, bool aWithBrightness)
{
  MLMicroSeconds now = MainLoop::now();
  LightBehaviourPtr l = getOutput<LightBehaviour>();
  bool needactivation = false;
  bool moreSteps = false;
  ColorLightBehaviourPtr cl;
  if (aWithColor) {
    cl = getOutput<ColorLightBehaviour>();
    moreSteps = cl->updateColorTransition(now);
    needactivation = mDaliController->setColorParamsFromChannels(cl, true, false, aForDimming); // activation needed when color has changed
  }
  // handle brightness
  if (aWithBrightness || needactivation) {
    // update actual dimmer value
    if (l->updateBrightnessTransition(now)) moreSteps = true; // brightness transition not yet complete
    else aWithBrightness = false; // brightness is done now, if there are further steps, these are color only
    bool sentDAPC = mDaliController->setBrightness(l->brightnessForHardware());
    if (sentDAPC && mDaliController->mDT8AutoActivation) needactivation = false; // prevent activating twice!
  }
  // activate color params in case brightness has not changed or device is not in auto-activation mode
  if (needactivation) {
    mDaliController->activateColorParams();
  }
  // now schedule next step (if any)
  if (moreSteps) {
    // not yet complete, schedule next step
    mTransitionTicket.executeOnce(
      boost::bind(&DaliSingleControllerDevice::applyChannelValueSteps, this, aForDimming, aWithColor, aWithBrightness),
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
      // start or stop dimming
      if (aDoApply) {
        OLOG(LOG_INFO,
          "dimChannel (DALI): channel '%s' %s",
          aChannel->getName(),
          aDimMode==dimmode_stop ? "STOPS dimming" : (aDimMode==dimmode_up ? "starts dimming UP" : "starts dimming DOWN")
        );
        mDaliController->dim(aDimMode, aChannel->getDimPerMS());
      }
      // in all cases, we need to query current brightness after dimming
      if (aDimMode==dimmode_stop) {
        // retrieve end status
        mDaliController->updateParams(boost::bind(&DaliSingleControllerDevice::outputChangeEndStateRetrieved, this, _1));
      }
    }
    else {
      // not my channel, use generic implementation
      inherited::dimChannel(aChannel, aDimMode, aDoApply);
    }
  }
}


void DaliSingleControllerDevice::sceneValuesApplied(SimpleCB aDoneCB, DsScenePtr aScene, bool aIndirectly)
{
  if (aIndirectly) {
    // some values were applied indirectly (e.g. optimized group/scene)
    // This makes the controller parameters stale
    mDaliController->mStaleParams = true;
    // We might want to read back the actual parameters now
    if (!daliVdc().getVdcFlag(vdcflag_effectSpeedOptimized)) {
      // Only if not aiming for maximum effect speed (rapid sequences of scene calls)
      // we can afford spending bus bandwidth with reading back parameters
      // (but only after a delay to make sure current operation has likely ended already)
      mDaliController->mOutputSyncTicket.executeOnce(boost::bind(&DaliBusDevice::updateParams, mDaliController, StatusCB()), 3*Second);
    }
  }
  inherited::sceneValuesApplied(aDoneCB, aScene, aIndirectly);
}


void DaliSingleControllerDevice::outputChangeEndStateRetrieved(ErrorPtr aError)
{
  processUpdatedParams(aError);
}


void DaliSingleControllerDevice::saveAsDefaultBrightness()
{
  mDaliController->setDefaultBrightness(-1);
}


void DaliSingleControllerDevice::setTransitionTime(MLMicroSeconds aTransitionTime)
{
  mDaliController->setTransitionTime(aTransitionTime);
}


bool DaliSingleControllerDevice::prepareForOptimizedSet(NotificationDeliveryStatePtr aDeliveryState)
{
  // check general exclude reasons
  if (
    !mDaliController || // safety - need a controller to optimize
    mDaliController->isGrouped() // already grouped devices cannot be optimized
  ) {
    return false;
  }
  // check notification-specific conditions
  if (aDeliveryState->mOptimizedType==ntfy_callscene) {
    // scenes are generally optimizable unless transition time is really slow and must be executed in multiple steps
    return transitionTimeForPreparedScene(true)<=MAX_SINGLE_STEP_TRANSITION_TIME;
  }
  else if (aDeliveryState->mOptimizedType==ntfy_dimchannel) {
    // only brightness dimming optimizable for now
    // TODO: extend to also optimize DT8 channels dimming
    return mCurrentDimChannel && mCurrentDimChannel->getChannelType()==channeltype_brightness;
  }
  return false;
}




void DaliSingleControllerDevice::deriveDsUid()
{
  // single channel dimmer just uses dSUID derived from single DALI bus device
  mDSUID = mDaliController->mDSUID;
}


bool DaliSingleControllerDevice::daliBusDeviceSummary(DaliAddress aDaliAddress, ApiValuePtr aInfo) const
{
  if (mDaliController&&mDaliController->belongsToShortAddr(aDaliAddress)) {
    // add device context
    daliDeviceContextSummary(aInfo);
    // add busdevice info
    mDaliController->daliBusDeviceSummary(aInfo);
    return true;
  }
  return false;
}


void DaliSingleControllerDevice::daliDeviceSummary(ApiValuePtr aInfo) const
{
  daliDeviceContextSummary(aInfo);
  ApiValuePtr di = aInfo->newObject();
  ApiValuePtr bdi = di->newObject();
  if (mDaliController) mDaliController->daliBusDeviceSummary(bdi);
  di->add("0", bdi);
  aInfo->add("dimmers", di);
}


string DaliSingleControllerDevice::modelName()
{
  string s = "DALI";
  if (mDaliController->mSupportsDT8) {
    if (mDaliController->mDT8Color || mDaliController->mDT8RGBWAFchannels>=3 || mDaliController->mDT8PrimaryColors>=3) s += " color";
    if (mDaliController->mDT8CT) s += " tunable white";
  }
  if (mDaliController->mSupportsLED) {
    s += " LED";
  }
  s += " dimmer";
  if (daliTechnicalType()==dalidevice_group) s+= " group";
  return s;
}





string DaliSingleControllerDevice::hardwareGUID()
{
  if (mDaliController->mDeviceInfo->mDevInfStatus<=DaliDeviceInfo::devinf_only_gtin)
    return ""; // none
  // return as GS1 element strings
  // Note: GTIN/Serial will be reported even if it could not be used for deriving dSUID (e.g. devinf_maybe/devinf_notForID cases)
  return string_format("gs1:(01)%llu(21)%llu", mDaliController->mDeviceInfo->mGtin, mDaliController->mDeviceInfo->mSerialNo);
}


string DaliSingleControllerDevice::hardwareModelGUID()
{
  if (mDaliController->mDeviceInfo->mGtin==0)
    return ""; // none
  // return as GS1 element strings with Application Identifier 01=GTIN
  return string_format("gs1:(01)%llu", mDaliController->mDeviceInfo->mGtin);
}


string DaliSingleControllerDevice::oemGUID()
{
  if (mDaliController->mDeviceInfo->mOemGtin==0 || mDaliController->mDeviceInfo->mOemSerialNo==0)
    return ""; // none
  // return as GS1 element strings with Application Identifiers 01=GTIN and 21=Serial
  return string_format("gs1:(01)%llu(21)%llu", mDaliController->mDeviceInfo->mOemGtin, mDaliController->mDeviceInfo->mOemSerialNo);
}


string DaliSingleControllerDevice::oemModelGUID()
{
  if (mDaliController->mDeviceInfo->mOemGtin==0)
    return ""; // none
  // return as GS1 element strings with Application Identifier 01=GTIN
  return string_format("gs1:(01)%llu", mDaliController->mDeviceInfo->mOemGtin);
}


string DaliSingleControllerDevice::description()
{
  string s = inherited::description();
  s.append(mDaliController->description());
  return s;
}


// MARK: - DaliCompositeDevice (multi-channel color lamp)


DaliCompositeDevice::DaliCompositeDevice(DaliVdc *aVdcP) :
  DaliOutputDevice(aVdcP),
  mBriCool(false)
{
}


bool DaliCompositeDevice::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Note: setting up behaviours late, because we want the brightness dimmer already assigned for the hardware name
  // set up dS behaviour for color lights, which include a color scene table
  installSettings(DeviceSettingsPtr(new ColorLightDeviceSettings(*this)));
  // set the behaviour
  bool ctOnly = mDimmers[dimmer_white] && mDimmers[dimmer_amber] && !mDimmers[dimmer_red]; // no red -> must be CT only
  RGBColorLightBehaviourPtr cl = RGBColorLightBehaviourPtr(new RGBColorLightBehaviour(*this, ctOnly));
  cl->setDefaultGamma(daliVdc().defaultGamma());
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
    if (mDimmers[idx]) {
      if (mDimmers[idx]->mDeviceInfo && mDimmers[idx]->mDeviceInfo->mDevInfStatus!=DaliDeviceInfo::devinf_solid) {
        l = 70; // is not a recommended device, does not have unique ID
      }
      if (mDimmers[idx]->mIsDummy) {
        l = 20; // not seen on last bus scan, might be glitch
      }
      if (!mDimmers[idx]->mIsPresent) {
        l = 40; // not completely fatal, might recover on next ping
      }
      if (mDimmers[idx]->mLampFailure) {
        l = 0;
      }
    }
  }
  return l;
}


string DaliCompositeDevice::getOpStateText()
{
  bool failure = false;
  bool incomplete = false;
  bool noUniqueId = false;
  for (DimmerIndex idx=dimmer_red; idx<numDimmers; idx++) {
    if (mDimmers[idx]) {
      if (mDimmers[idx]->mDeviceInfo && mDimmers[idx]->mDeviceInfo->mDevInfStatus!=DaliDeviceInfo::devinf_solid) {
        noUniqueId = true;
      }
      if (mDimmers[idx]->mLampFailure) {
        failure = true;
      }
      if (mDimmers[idx]->mIsDummy) {
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
  if (mDimmers[dimmer_red]) {
    s += "Red:"+DaliComm::formatDaliAddress(mDimmers[dimmer_red]->mDeviceInfo->mShortAddress);
  }
  if (mDimmers[dimmer_green]) {
    s += " Green:"+DaliComm::formatDaliAddress(mDimmers[dimmer_green]->mDeviceInfo->mShortAddress);
  }
  if (mDimmers[dimmer_blue]) {
    s += " Blue:"+DaliComm::formatDaliAddress(mDimmers[dimmer_blue]->mDeviceInfo->mShortAddress);
  }
  if (mDimmers[dimmer_white]) {
    if (mBriCool) s += " Brightness:";
    else s += " White:";
    s += DaliComm::formatDaliAddress(mDimmers[dimmer_white]->mDeviceInfo->mShortAddress);
  }
  if (mDimmers[dimmer_amber]) {
    if (mBriCool) s += " Coolness:";
    else s += " Warmwhite/amber:";
    s += DaliComm::formatDaliAddress(mDimmers[dimmer_amber]->mDeviceInfo->mShortAddress);
  }
  return s;
}


bool DaliCompositeDevice::addDimmer(DaliBusDevicePtr aDimmerBusDevice, string aDimmerType)
{
  if (aDimmerType=="R")
    mDimmers[dimmer_red] = aDimmerBusDevice;
  else if (aDimmerType=="G")
    mDimmers[dimmer_green] = aDimmerBusDevice;
  else if (aDimmerType=="B")
    mDimmers[dimmer_blue] = aDimmerBusDevice;
  else if (aDimmerType=="W")
    mDimmers[dimmer_white] = aDimmerBusDevice;
  else if (aDimmerType=="A")
    mDimmers[dimmer_amber] = aDimmerBusDevice;
  else if (aDimmerType=="BRI") {
    mBriCool = true; // this is a brightness (amber channel) + coolness (white channel) type light
    mDimmers[dimmer_amber] = aDimmerBusDevice;
  }
  else if (aDimmerType=="COOL") {
    mBriCool = true; // this is a brightness (amber channel) + coolness (white channel) type light
    mDimmers[dimmer_white] = aDimmerBusDevice;
  }
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
  if (Error::notOK(aError)) {
    OLOG(LOG_ERR, "CompositeDevice: error getting state/params from dimmer#%d: %s", aDimmerIndex-1, aError->text());
  }
  while (aDimmerIndex<numDimmers) {
    DaliBusDevicePtr di = mDimmers[aDimmerIndex];
    // process this dimmer if it exists
    if (di) {
      di->updateParams(boost::bind(&DaliCompositeDevice::updateNextDimmer, this, aCompletedCB, aFactoryReset, aDimmerIndex+1, _1));
      return; // return now, will be called again when update is complete
    }
    aDimmerIndex++; // next
  }
  // all updated (not necessarily successfully) if we land here
  RGBColorLightBehaviourPtr cl = getOutput<RGBColorLightBehaviour>();
  double r = mDimmers[dimmer_red] ? mDimmers[dimmer_red]->mCurrentBrightness : 0;
  double g = mDimmers[dimmer_green] ? mDimmers[dimmer_green]->mCurrentBrightness : 0;
  double b = mDimmers[dimmer_blue] ? mDimmers[dimmer_blue]->mCurrentBrightness : 0;
  if (mDimmers[dimmer_white]) {
    double w = mDimmers[dimmer_white]->mCurrentBrightness;
    if (mDimmers[dimmer_amber]) {
      double a = mDimmers[dimmer_amber]->mCurrentBrightness;
      // could be CT only
      if (cl->isCtOnly()) {
        if (mBriCool) {
          // treat as two-channel tunable white (one channel being brightness, the other CT)
          cl->setBriCool(a, w, 100);
        }
        else {
          // treat as two-channel tunable white (one channel being CW, the other WW)
          cl->setCWWW(w, a, 100); // dali dimmers use abstracted 0..100% brightness
        }
      }
      else {
        // RGBWA
        cl->setRGBWA(r, g, b, w, a, 100, false); // dali dimmers use abstracted 0..100% brightness
      }
    }
    else {
      // RGBW
      cl->setRGBW(r, g, b, w, 100, false); // dali dimmers use abstracted 0..100% brightness
    }
  }
  else {
    cl->setRGB(r, g, b, 100, false); // dali dimmers use abstracted 0..100% brightness
  }
  // complete - continue with initialisation in superclasses
  inherited::initializeDevice(aCompletedCB, aFactoryReset);
}



DaliBusDevicePtr DaliCompositeDevice::firstBusDevice()
{
  for (DimmerIndex idx=dimmer_red; idx<numDimmers; idx++) {
    if (mDimmers[idx]) {
      return mDimmers[idx];
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
  aPresenceResultHandler(aDimmer->mIsPresent);
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


void DaliCompositeDevice::stopTransitions()
{
  inherited::stopTransitions();
  // stop any ongoing dimming/transitioning operation
  if (mDimmers[dimmer_red]) mDimmers[dimmer_red]->dim(dimmode_stop, 0);
  if (mDimmers[dimmer_green]) mDimmers[dimmer_green]->dim(dimmode_stop, 0);
  if (mDimmers[dimmer_blue]) mDimmers[dimmer_blue]->dim(dimmode_stop, 0);
  if (mDimmers[dimmer_white]) mDimmers[dimmer_white]->dim(dimmode_stop, 0);
  if (mDimmers[dimmer_amber]) mDimmers[dimmer_amber]->dim(dimmode_stop, 0);
}


void DaliCompositeDevice::applyChannelValueSteps(bool aForDimming, bool aWithColor, bool aWithBrightness)
{
  MLMicroSeconds now = MainLoop::now();
  RGBColorLightBehaviourPtr cl = getOutput<RGBColorLightBehaviour>();
  bool moreSteps = cl->updateColorTransition(now);
  if (cl->updateBrightnessTransition(now)) moreSteps = true;
  // RGB lamp, get components
  double r=0,g=0,b=0,w=0,a=0;
  if (mDimmers[dimmer_white]) {
    // RGBW, RGBWA or CT-only
    if (mDimmers[dimmer_amber]) {
      // RGBWA or CT
      if (cl->isCtOnly()) {
        // CT
        if (mBriCool) {
          // treat as two-channel tunable white (one channel being brightness, the other CT)
          cl->getBriCool(a, w, 100, true); // dali dimmers use abstracted 0..100% brightness as input
          if (!aForDimming) {
            OLOG(LOG_INFO, "DALI composite BriCool: Bri=%d, Cool=%d", (int)a, (int)w);
          }
        }
        else {
          // treat as two-channel tunable white (one channel being CW, the other WW)
          cl->getCWWW(w, a, 100, true); // dali dimmers use abstracted 0..100% brightness as input
          if (!aForDimming) {
            OLOG(LOG_INFO, "DALI composite CWWW: CW=%d, WW=%d", (int)w, (int)a);
          }
        }
      }
      else {
        // RGBWA
        cl->getRGBWA(r, g, b, w, a, 100, false, true); // dali dimmers use abstracted 0..100% brightness as input
        if (!aForDimming) {
          OLOG(LOG_INFO, "DALI composite RGBWA: R=%d, G=%d, B=%d, W=%d, A=%d", (int)r, (int)g, (int)b, (int)w, (int)a);
        }
      }
    }
    else {
      cl->getRGBW(r, g, b, w, 100, false, true); // dali dimmers use abstracted 0..100% brightness as input
      if (!aForDimming) {
        OLOG(LOG_INFO, "DALI composite RGBW: R=%d, G=%d, B=%d, W=%d", (int)r, (int)g, (int)b, (int)w);
      }
    }
  }
  else {
    // RGB
    cl->getRGB(r, g, b, 100, false, true); // dali dimmers use abstracted 0..100% brightness as input
    if (!aForDimming) {
      OLOG(LOG_INFO, "DALI composite RGB: R=%d, G=%d, B=%d", (int)r, (int)g, (int)b);
    }
  }
  // apply new values
  if (mDimmers[dimmer_red]) mDimmers[dimmer_red]->setBrightness(r);
  if (mDimmers[dimmer_green]) mDimmers[dimmer_green]->setBrightness(g);
  if (mDimmers[dimmer_blue]) mDimmers[dimmer_blue]->setBrightness(b);
  if (mDimmers[dimmer_white]) mDimmers[dimmer_white]->setBrightness(w);
  if (mDimmers[dimmer_amber]) mDimmers[dimmer_amber]->setBrightness(a);
  if (moreSteps) {
    // not yet complete, schedule next step
    mTransitionTicket.executeOnce(
      boost::bind(&DaliCompositeDevice::applyChannelValueSteps, this, aForDimming, aWithColor, aWithBrightness),
      SLOW_TRANSITION_STEP_TIME
    );
    return; // will be called later again
  }
}


/// save brightness as default for DALI dimmer to use after powerup and at failure
/// @param aBrightness new brightness to set
void DaliCompositeDevice::saveAsDefaultBrightness()
{
  if (mDimmers[dimmer_red]) mDimmers[dimmer_red]->setDefaultBrightness(-1);
  if (mDimmers[dimmer_green]) mDimmers[dimmer_green]->setDefaultBrightness(-1);
  if (mDimmers[dimmer_blue]) mDimmers[dimmer_blue]->setDefaultBrightness(-1);
  if (mDimmers[dimmer_white]) mDimmers[dimmer_white]->setDefaultBrightness(-1);
  if (mDimmers[dimmer_amber]) mDimmers[dimmer_amber]->setDefaultBrightness(-1);
}


void DaliCompositeDevice::setTransitionTime(MLMicroSeconds aTransitionTime)
{
  // set transition time for all dimmers to brightness transition time
  if (mDimmers[dimmer_red]) mDimmers[dimmer_red]->setTransitionTime(aTransitionTime);
  if (mDimmers[dimmer_green]) mDimmers[dimmer_green]->setTransitionTime(aTransitionTime);
  if (mDimmers[dimmer_blue]) mDimmers[dimmer_blue]->setTransitionTime(aTransitionTime);
  if (mDimmers[dimmer_white]) mDimmers[dimmer_white]->setTransitionTime(aTransitionTime);
  if (mDimmers[dimmer_amber]) mDimmers[dimmer_amber]->setTransitionTime(aTransitionTime);
}


void DaliCompositeDevice::deriveDsUid()
{
  // Multi-channel DALI devices construct their ID from UUIDs of the DALI devices involved,
  // but in a way that allows re-assignment of R/G/B without changing the dSUID
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  string mixID;
  for (DimmerIndex idx=dimmer_red; idx<numDimmers; idx++) {
    if (mDimmers[idx]) {
      // use this dimmer's dSUID as part of the mix
      // - when completely unlocked (no backward compatibility needed), use the improved mix which adds a FNV hash
      //   including the subdevice index, so mixing multiple dSUIDs only differing in subdevice byte
      //   don't cancel each other out completely (even #) or match the original dSUID {odd # of mixed dSUIDs)
      bool lunSafeMix = !daliVdc().mDaliComm.mDali2ScanLock && !daliVdc().mDaliComm.mDali2LUNLock;
      mDimmers[idx]->mDSUID.xorDsUidIntoMix(mixID, lunSafeMix);
    }
  }
  // use xored ID as base for creating UUIDv5 in vdcNamespace
  mDSUID.setNameInSpace("dalicombi:"+mixID, vdcNamespace);
}


static const char* dimmerChannelNames[DaliCompositeDevice::numDimmers] = {
  "red",
  "green",
  "blue",
  "white/CW",
  "amber/WW/CT"
};


bool DaliCompositeDevice::daliBusDeviceSummary(DaliAddress aDaliAddress, ApiValuePtr aInfo) const
{
  for (DimmerIndex idx=dimmer_red; idx<numDimmers; idx++) {
    DaliBusDevicePtr dim = mDimmers[idx];
    if (dim && dim->belongsToShortAddr(aDaliAddress)) {
      // add device context
      daliDeviceContextSummary(aInfo);
      // add busdevice info
      aInfo->add("channel", aInfo->newString(dimmerChannelNames[idx]));
      dim->daliBusDeviceSummary(aInfo);
      return true;
    }
  }
  return false;
}


void DaliCompositeDevice::daliDeviceSummary(ApiValuePtr aInfo) const
{
  daliDeviceContextSummary(aInfo);
  ApiValuePtr di = aInfo->newObject();
  for (DimmerIndex idx=dimmer_red; idx<numDimmers; idx++) {
    ApiValuePtr bdi = di->newObject();
    DaliBusDevicePtr dim = mDimmers[idx];
    if (dim) {
      // add busdevice info
      bdi->add("channel", aInfo->newString(dimmerChannelNames[idx]));
      dim->daliBusDeviceSummary(bdi);
      di->add(string_format("%d", idx), bdi);
    }
  }
  aInfo->add("dimmers", di);
}




string DaliCompositeDevice::hardwareGUID()
{
  DaliBusDevicePtr dimmer = firstBusDevice();
  if (!dimmer || dimmer->mDeviceInfo->mGtin==0 || dimmer->mDeviceInfo->mSerialNo==0)
    return ""; // none
  // return as GS1 element strings
  return string_format("gs1:(01)%llu(21)%llu", dimmer->mDeviceInfo->mGtin, dimmer->mDeviceInfo->mSerialNo);
}


string DaliCompositeDevice::hardwareModelGUID()
{
  DaliBusDevicePtr dimmer = firstBusDevice();
  if (!dimmer || dimmer->mDeviceInfo->mGtin==0)
    return ""; // none
  // return as GS1 element strings with Application Identifier 01=GTIN
  return string_format("gs1:(01)%llu", dimmer->mDeviceInfo->mGtin);
}


string DaliCompositeDevice::oemGUID()
{
  DaliBusDevicePtr dimmer = firstBusDevice();
  if (!dimmer || dimmer->mDeviceInfo->mOemGtin==0|| dimmer->mDeviceInfo->mOemSerialNo==0)
    return ""; // none
  // return as GS1 element strings with Application Identifiers 01=GTIN and 21=Serial
  return string_format("gs1:(01)%llu(21)%llu", dimmer->mDeviceInfo->mOemGtin, dimmer->mDeviceInfo->mOemSerialNo);
}


string DaliCompositeDevice::oemModelGUID()
{
  DaliBusDevicePtr dimmer = firstBusDevice();
  if (!dimmer || dimmer->mDeviceInfo->mOemGtin==0)
    return ""; // none
  // return as GS1 element strings with Application Identifier 01=GTIN
  return string_format("gs1:(01)%llu", dimmer->mDeviceInfo->mOemGtin);
}



string DaliCompositeDevice::description()
{
  string s = inherited::description();
  for (DimmerIndex idx=dimmer_red; idx<numDimmers; idx++) {
    DaliBusDevicePtr dim = mDimmers[idx];
    string_format_append(s, "\nChannel %s", dimmerChannelNames[idx]);
    if (dim) {
      // add busdevice info
      s.append(dim->description());
    }
    else {
      // missing bus device
      s.append("\n- missing dimmer");
    }
  }
  return s;
}


#if ENABLE_DALI_INPUTS


// MARK: - DaliInputDevice

DaliInputDevice::DaliInputDevice(DaliVdc *aVdcP, const string aDaliInputConfig, DaliAddress aBaseAddress) :
  inherited(aVdcP),
  mDaliInputDeviceRowID(0)
{
  mBaseAddress = aBaseAddress;
  mNumAddresses = 1;
  // decode config
  string type = aDaliInputConfig;
  // TODO: add more options
  if (type=="button") {
    mInputType = input_button;
  }
  else if (type=="rocker") {
    mInputType = input_rocker;
  }
  else if (type=="motion") {
    mInputType = input_motion;
  }
  else if (type=="illumination") {
    mInputType = input_illumination;
  }
  else if (type=="bistable") {
    mInputType = input_bistable;
  }
  else if (type=="dimmer") {
    mInputType = input_dimmer;
  }
  else {
    // default to pulse input
    mInputType = input_pulse;
  }
  // create behaviours
  if (mInputType==input_button) {
    // Simple single button device
    mColorClass = class_black_joker;
    // - standard device settings without scene table
    installSettings();
    // - create one button input
    ButtonBehaviourPtr bb = ButtonBehaviourPtr(new ButtonBehaviour(*this,"")); // automatic id
    bb->setHardwareButtonConfig(0, buttonType_undefined, buttonElement_center, false, 0, 1); // mode not restricted
    bb->setGroup(group_yellow_light); // pre-configure for light
    bb->setHardwareName("button");
    addBehaviour(bb);
  }
  else if (mInputType==input_rocker) {
    // Two-way Rocker Button device
    mNumAddresses = 2;
    mColorClass = class_black_joker;
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
  else if (mInputType==input_motion) {
    // Standard device settings without scene table
    mColorClass = class_red_security;
    installSettings();
    // - create one binary input
    BinaryInputBehaviourPtr ib = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*this,"")); // automatic id
    ib->setHardwareInputConfig(binInpType_motion, usage_undefined, true, Never, Never);
    ib->setHardwareName("motion");
    addBehaviour(ib);
  }
  else if (mInputType==input_illumination) {
    // Standard device settings without scene table
    mColorClass = class_yellow_light;
    installSettings();
    // - create one binary input
    BinaryInputBehaviourPtr ib = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*this,"")); // automatic id
    ib->setHardwareInputConfig(binInpType_light, usage_undefined, true, Never, Never);
    ib->setHardwareName("light");
    addBehaviour(ib);
  }
  else if (mInputType==input_dimmer) {
    // Standard device settings without scene table
    mColorClass = class_yellow_light;
    installSettings();
    // - create one dimmer sensor input
    SensorBehaviourPtr sb = SensorBehaviourPtr(new SensorBehaviour(*this,"")); // automatic id
    sb->setHardwareSensorConfig(sensorType_percent, usage_user, 0, 100, 0.25, 0.25, 0); // dimmer "sensor"
    sb->setHardwareName("dimmer dial");
    addBehaviour(sb);
  }
  else if (mInputType==input_bistable || mInputType==input_pulse) {
    // Standard device settings without scene table
    mColorClass = class_black_joker;
    installSettings();
    // - create one binary input
    BinaryInputBehaviourPtr ib = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*this,"")); // automatic id
    ib->setHardwareInputConfig(binInpType_none, usage_undefined, true, Never, Never);
    ib->setHardwareName("input");
    addBehaviour(ib);
  }
  else {
    OLOG(LOG_ERR, "unknown device type");
  }
  // mark used groups/scenes
  for (int i=0; i<mNumAddresses; i++) {
    daliVdc().markUsed(mBaseAddress+i, true);
  }
}


void DaliInputDevice::freeAddresses()
{
  for (int i=0; i<mNumAddresses; i++) {
    daliVdc().removeMemberships(mBaseAddress+i);
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
  return *(static_cast<DaliVdc *>(mVdcP));
}


bool DaliInputDevice::isSoftwareDisconnectable()
{
  return true;
}


void DaliInputDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  OLOG(LOG_DEBUG, "disconnecting DALI input device with rowid=%lld", mDaliInputDeviceRowID);
  // clear learn-in data from DB
  if (mDaliInputDeviceRowID) {
    if(daliVdc().mDb.executef("DELETE FROM inputDevices WHERE rowid=%lld", mDaliInputDeviceRowID)!=SQLITE_OK) {
      OLOG(LOG_ERR, "deleting DALI input device: %s", daliVdc().mDb.error()->description().c_str());
    }
    for (int i=0; i<mNumAddresses; i++) {
      daliVdc().markUsed(mBaseAddress+i, false);
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
      if (mBaseAddress & DaliScene) {
        // we are listening to scene calls
        if ((aData1 & 0x01) && (aData2 & 0xF0)==DALICMD_GO_TO_SCENE) {
          refindex = aData2 & 0x0F;
          aData2 = DALICMD_GO_TO_SCENE; // normalize to allow catching command below
          // if used as bistable binary input, consider scenes 0..7 as off, 8..15 as on
          on = refindex>=8;
          off = !on;
        }
      }
      else if (mBaseAddress & DaliGroup) {
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
        int baseindex = mBaseAddress & DaliAddressMask;
        if (refindex>=baseindex && refindex<baseindex+mNumAddresses) {
          inpindex = refindex-baseindex;
        }
      }
      if (inpindex>=0) {
        // check type of command
        if ((aData1 & 0x01)==0) {
          // direct level
          if (mInputType==input_dimmer) {
            // just transfer the level value in brightness scale as sensor value
            double sv = 0;
            sv = (double)aData2/2.54; // convert 0..254 -> 0..100
            SensorBehaviourPtr sb = getSensor(inpindex);
            sb->updateSensorValue(sv);
          }
          else {
            // always pulse
            on = aData2 > 128; // at least half -> on
            off = aData2==0; // -> off
          }
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
          switch (mInputType) {
            case input_button:
            case input_rocker:
            {
              ButtonBehaviourPtr bb = getButton(inpindex);
              bb->updateButtonState(true);
              mReleaseTicket.executeOnce(boost::bind(&DaliInputDevice::buttonReleased, this, inpindex), BUTTON_RELEASE_TIMEOUT);
              break;
            }
            case input_pulse:
            {
              BinaryInputBehaviourPtr ib = getInput(inpindex);
              ib->updateInputState(1);
              mReleaseTicket.executeOnce(boost::bind(&DaliInputDevice::inputReleased, this, inpindex), INPUT_RELEASE_TIMEOUT);
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
  mReleaseTicket = 0;
  ButtonBehaviourPtr bb = getButton(aButtonNo);
  bb->updateButtonState(false);
}



void DaliInputDevice::inputReleased(int aInputNo)
{
  mReleaseTicket = 0;
  BinaryInputBehaviourPtr ib = getInput(aInputNo);
  ib->updateInputState(0);
}




/// @return human readable model name/short description
string DaliInputDevice::modelName()
{
  string m = "DALI ";
  switch (mInputType) {
    case input_button:
      m += "button";
      break;
    case input_dimmer:
      m += "dimmer";
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
  switch (mInputType) {
    case input_button:
      iconname = "dali_button";
      colored = true;
      break;
    case input_rocker:
      iconname = "dali_rocker";
      colored = true;
      break;
    case input_dimmer:
      iconname = "dali_dimmer";
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
  e += DaliComm::formatDaliAddress(mBaseAddress);
  if (mNumAddresses>1) string_format_append(e, " + %d more", mNumAddresses-1);
  return e;
}


void DaliInputDevice::deriveDsUid()
{
  // vDC implementation specific UUID:
  //   UUIDv5 with name = classcontainerinstanceid::baseAddress:inputType
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  string s = mVdcP->vdcInstanceIdentifier();
  string_format_append(s, "::%u:%u", mBaseAddress, mInputType);
  mDSUID.setNameInSpace(s, vdcNamespace);
}




#endif // ENABLE_DALI_INPUTS


#endif // ENABLE_DALI

