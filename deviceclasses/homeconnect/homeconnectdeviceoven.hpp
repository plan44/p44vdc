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

#ifndef __p44vdc__homeconnectdeviceoven__
#define __p44vdc__homeconnectdeviceoven__

#include "homeconnectdevice.hpp"

#if ENABLE_HOMECONNECT

namespace p44 {

class HomeConnectDeviceOven: public HomeConnectDevice
{
  typedef HomeConnectDevice inherited;

  ValueDescriptorPtr setTemperatureProp;
  ValueDescriptorPtr currentTemperatureProp;
  ValueDescriptorPtr elapsedProgramTimeProp;
  ValueDescriptorPtr remainingProgramTimeProp;
  ValueDescriptorPtr programProgressProp;

  virtual bool configureDevice() P44_OVERRIDE;
  virtual void stateChanged(DeviceStatePtr aChangedState, DeviceEventsList &aEventsToPush) P44_OVERRIDE;
  virtual void handleEventTypeNotify(string aKey, JsonObjectPtr aValue) P44_OVERRIDE;
  virtual void handleEventTypeStatus(string aKey, JsonObjectPtr aValue) P44_OVERRIDE;

  void addAction(const string& aActionName, const string& aDescription, const string& aProgramName, ValueDescriptorPtr aTemperature, ValueDescriptorPtr aDuration);
public:
  HomeConnectDeviceOven(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord);
  virtual ~HomeConnectDeviceOven();

  virtual string oemModelGUID() P44_OVERRIDE;
};

} /* namespace p44 */

#endif // ENABLE_HOMECONNECT

#endif /* __p44vdc__homeconnectdeviceoven__ */
