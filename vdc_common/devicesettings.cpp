//
//  Copyright (c) 2013-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
  mDeviceFlags(0),
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
  aRow->getIfNotNull<int>(aIndex++, mDeviceFlags);
  mDevice.initializeName(nonNullCStr(aRow->get<const char *>(aIndex++))); // do not propagate to HW!
  aRow->getCastedIfNotNull<DsZoneID, int>(aIndex++, mZoneID);
}


// bind values to passed statement
void DeviceSettings::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, mDeviceFlags);
  aStatement.bind(aIndex++, mDevice.getAssignedName().c_str(), false);  // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, (int)mZoneID);
}
