//
//  Copyright (c) 2015-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "enoceanvld.hpp"

#if ENABLE_ENOCEAN

#include "enoceanvdc.hpp"


using namespace p44;


// MARK: ===== VLD device specifications


using namespace EnoceanSensors;


// TODO: %%% no real profiles yet"
const p44::EnoceanSensorDescriptor enoceanVLDdescriptors[] = {
  // variant,func,type, SD,primarygroup,  channelGroup,                  behaviourType,         behaviourParam,         usage,              min,  max,MSB,     LSB,  updateIv,aliveSignIv, handler,     typeText, unitText

  // terminator
  { 0, 0,    0,    0, class_black_joker,  group_black_variable,          behaviour_undefined, 0, usage_undefined, 0, 0, 0, 0, 0, 0, NULL /* NULL for extractor function terminates list */, NULL, NULL },
};




// MARK: ===== EnoceanVLDDevice


EnoceanVLDDevice::EnoceanVLDDevice(EnoceanVdc *aVdcP) :
  inherited(aVdcP)
{
}


// static device creator function
EnoceanDevicePtr createVLDDeviceFunc(EnoceanVdc *aVdcP)
{
  return EnoceanDevicePtr(new EnoceanVLDDevice(aVdcP));
}


// static factory method
EnoceanDevicePtr EnoceanVLDDevice::newDevice(
  EnoceanVdc *aVdcP,
  EnoceanAddress aAddress,
  EnoceanSubDevice &aSubDeviceIndex,
  EnoceanProfile aEEProfile, EnoceanManufacturer aEEManufacturer,
  bool aSendTeachInResponse
) {
  EnoceanDevicePtr newDev; // none so far
  // check for specialized handlers for certain profiles first
//  if (EEP_PURE(aEEProfile)==0xA52001) {
//    // Note: Profile has variants (with and without temperature sensor)
//    // use specialized handler for output functions of heating valve (valve value, summer/winter, prophylaxis)
//    newDev = EnoceanA52001Handler::newDevice(aVdcP, aAddress, aSubDeviceIndex, aEEProfile, aEEManufacturer, aSendTeachInResponse);
//  }
//  else
  {
    // check table based sensors, might create more than one device
// TODO: %%% no real profiles yet"
//    newDev = EnoceanSensorHandler::newDevice(aVdcP, createVLDDeviceFunc, enoceanVLDdescriptors, aAddress, aSubDeviceIndex, aEEProfile, aEEManufacturer, aSendTeachInResponse);
  }
  return newDev;
}




// MARK: ===== EnoceanVLDDevice profile variants


//static const ProfileVariantEntry profileVariantsVLD[] = {
//  { 0, 0, 0, NULL } // terminator
//};


const ProfileVariantEntry *EnoceanVLDDevice::profileVariantsTable()
{
  return NULL; // none for now
  // return profileVariantsVLD;
}


#endif // ENABLE_ENOCEAN
