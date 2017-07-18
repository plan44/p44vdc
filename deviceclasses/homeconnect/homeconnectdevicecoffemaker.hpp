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

#ifndef __p44vdc__homeconnectdevicecoffemaker__
#define __p44vdc__homeconnectdevicecoffemaker__

#include "homeconnectdevice.hpp"

#if ENABLE_HOMECONNECT

namespace p44 {

class HomeConnectDeviceCoffeMaker: public HomeConnectDevice
{
  typedef HomeConnectDevice inherited;

  bool standalone; ///< set if this is the standalone model (which does not understand ConsumerProducts.CoffeeMaker.Option.CoffeeTemperature)
  EnumValueDescriptorPtr beanAmountProp;
  ValueDescriptorPtr fillQuantityProp;

  virtual bool configureDevice() P44_OVERRIDE;
  virtual void stateChanged(DeviceStatePtr aChangedState, DeviceEventsList &aEventsToPush) P44_OVERRIDE;
  virtual void handleEventTypeNotify(const string& aKey, JsonObjectPtr aValue) P44_OVERRIDE;

  void addAction(const string& aActionName,
                 const string& aDescription,
                 const string& aProgramName,
                 double aFillAmountMin,
                 double aFillAmountMax,
                 double aFillAmountResolution,
                 double aFillAmountDefault);
public:
  HomeConnectDeviceCoffeMaker(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord);
  virtual ~HomeConnectDeviceCoffeMaker();

  virtual string oemModelGUID() P44_OVERRIDE;
};

} /* namespace p44 */

#endif // ENABLE_HOMECONNECT

#endif /* __p44vdc__homeconnectdevicecoffemaker__ */
