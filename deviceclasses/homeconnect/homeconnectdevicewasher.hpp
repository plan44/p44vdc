//
//  Copyright (c) 2017 digitalSTROM.org, Zurich, Switzerland
//
//  Author: Pawel Kochanowski <pawel.kochanowski@digitalstrom.com>
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

#ifndef __p44vdc__homeconnectdevicewasher__
#define __p44vdc__homeconnectdevicewasher__

#include "homeconnectdevice.hpp"

#if ENABLE_HOMECONNECT

namespace p44 {

class HomeConnectDeviceWasher: public HomeConnectDevice
{
  typedef HomeConnectDevice inherited;

  typedef enum {
    temperature_Cold,
    temperature_GC20,
    temperature_GC30,
    temperature_GC40,
    temperature_GC50,
    temperature_GC60,
    temperature_GC70,
    temperature_GC80,
    temperature_GC90,
    temperature_Num
  } Temperature;

  typedef enum {
    spinSpeed_Off,
    spinSpeed_RPM400,
    spinSpeed_RPM600,
    spinSpeed_RPM800,
    spinSpeed_RPM1000,
    spinSpeed_RPM1200,
    spinSpeed_RPM1400,
    spinSpeed_RPM1600,
    spinSpeed_Num
  } SpinSpeed;

  static const char* temperatureNames[temperature_Num];
  static const char* spinSpeedNames[spinSpeed_Num];

  EnumValueDescriptorPtr temperatureProp;
  EnumValueDescriptorPtr spinSpeedProp;

  virtual bool configureDevice() P44_OVERRIDE;
  virtual void stateChanged(DeviceStatePtr aChangedState, DeviceEventsList &aEventsToPush) P44_OVERRIDE;
  virtual void handleEventTypeNotify(const string& aKey, JsonObjectPtr aValue) P44_OVERRIDE;

  void addAction(const string& aName, const string& aDescription, const string& aApiCommandTemplate, ValueDescriptorPtr aParameter, ValueDescriptorPtr aSpinSpeed);
  EnumValueDescriptorPtr createEnumDescriptor(string aName, int aMaxValue, int aDefValue, const char** aEnumNames);

public:
  HomeConnectDeviceWasher(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord);
  virtual ~HomeConnectDeviceWasher();

  virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;
};

} /* namespace p44 */

#endif // ENABLE_HOMECONNECT

#endif /* __p44vdc__homeconnectdevicewasher__ */
