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

#include "devicesettings.hpp"

#include "device.hpp"

using namespace p44;


DeviceSettings::DeviceSettings(Device &aDevice) :
  inherited(aDevice.getVdcHost().getDsParamStore()),
  mDevice(aDevice),
  #if ENABLE_JSONBRIDGEAPI
  mBridgingFlags(BridgingFlags::bridge_none),
  #endif
  mZoneID(0)
{
}


// SQLIte3 table name to store these parameters to
const char *DeviceSettings::tableName()
{
  return "DeviceSettings";
  // Note: there's a hard-coded dependency on this table being called "DeviceSettings" in the DaliBusDevice class!
}


// data field definitions

static const size_t numFields = 3;

size_t DeviceSettings::numFieldDefs()
{
  return inherited::numFieldDefs()+numFields;
}


// global device flag definitons
enum {
  // device global
  // - bits 0..3 are used for bridging flags (see BridgingFlags)
  deviceflags_bridgingFlagsMask = DeviceSettings::bridge_flags_mask, ///< bridging flags
  deviceflags_invertedBridgingFlags = DeviceSettings::bridge_flags_mask & ~DeviceSettings::bridge_output, ///< all input flags are stored inverted, output bit is stored as-is
  // - bits 4..7 are reserved for future use
  deviceflags_max = 0x0080, ///< highest device flag reserved at the root class level
};

const FieldDefinition *DeviceSettings::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numFields] = {
    { "deviceFlags", SQLITE_INTEGER },
    { "deviceName", SQLITE_TEXT }, // Note: there's a hard-coded dependency on this field being called "deviceName" in the DaliBusDevice class!
    { "zoneID", SQLITE_INTEGER }
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void DeviceSettings::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inherited::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get the field value
  uint64_t flags = aRow->getCastedWithDefault<uint64_t, long long int>(aIndex++, 0);
  mDevice.initializeName(nonNullCStr(aRow->get<const char *>(aIndex++)), true); // do not propagate to HW! do not clear already set names!
  aRow->getCastedIfNotNull<DsZoneID, int>(aIndex++, mZoneID);
  // decode my own flags
  #if ENABLE_JSONBRIDGEAPI
  mBridgingFlags = (BridgingFlags)((flags & deviceflags_bridgingFlagsMask) ^ deviceflags_invertedBridgingFlags);
  #endif
  // pass the flags out to subclass which called this superclass to get the flags (and decode themselves)
  if (aCommonFlagsP) *aCommonFlagsP = flags;
}


// bind values to passed statement
void DeviceSettings::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // encode the flags
  #if ENABLE_JSONBRIDGEAPI
  aCommonFlags = (aCommonFlags & deviceflags_bridgingFlagsMask) | (mBridgingFlags ^ deviceflags_invertedBridgingFlags);
  #endif
  // bind the fields
  aStatement.bind(aIndex++, (long long int)aCommonFlags);
  aStatement.bind(aIndex++, mDevice.getAssignedName().c_str(), false);  // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, (int)mZoneID);
}
