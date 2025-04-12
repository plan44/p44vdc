//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2024 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#define FOCUSLOGLEVEL 7

#include "ds485device.hpp"

#if ENABLE_DS485DEVICES

#include "ds485vdc.hpp"

#include "outputbehaviour.hpp"
#include "buttonbehaviour.hpp"
#include "binaryinputbehaviour.hpp"
#include "sensorbehaviour.hpp"

using namespace p44;


Ds485Device::Ds485Device(Ds485Vdc *aVdcP, DsUid& aDsmDsUid, uint16_t aDevId) :
  inherited((Vdc *)aVdcP),
  mDsmDsUid(aDsmDsUid),
  mDevId(aDevId)
{
  mDsmDsUid.isMemberVariable();
}


Ds485Device::~Ds485Device()
{
}


bool Ds485Device::identifyDevice(IdentifyDeviceCB aIdentifyCB)
{
  // Nothing to do to identify for now
  return true; // simple identification, callback will not be called
}


Ds485Vdc &Ds485Device::getDs485Vdc()
{
  return *(static_cast<Ds485Vdc *>(mVdcP));
}


string Ds485Device::deviceTypeIdentifier() const
{
  return "ds485";
}


string Ds485Device::modelName()
{
  return "digitalSTROM device";
}


string Ds485Device::description()
{
  string s = inherited::description();
  string_format_append(s, "\n- tbd...");
  return s;
}


// MARK: - local method/notification handling

ErrorPtr Ds485Device::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  // TODO: no speical handling yet
  return inherited::handleMethod(aRequest, aMethod, aParams);
}


void Ds485Device::handleNotification(const string &aNotification, ApiValuePtr aParams, StatusCB aExaminedCB)
{
  // TODO: no speical handling yet
  inherited::handleNotification(aNotification, aParams, aExaminedCB);
}


void Ds485Device::initializeDevice(StatusCB aCompletedCB, bool aFactoryReset)
{
  // TODO: no speical handling yet
  inherited::initializeDevice(aCompletedCB, aFactoryReset);
}

#endif // ENABLE_DS485DEVICES
