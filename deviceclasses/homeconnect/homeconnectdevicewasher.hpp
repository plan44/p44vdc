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

namespace p44 {

class HomeConnectDeviceWasher: public HomeConnectDevice
{
  typedef HomeConnectDevice inherited;

  virtual bool configureDevice() P44_OVERRIDE;
public:
  HomeConnectDeviceWasher(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord);
  virtual ~HomeConnectDeviceWasher();
};

} /* namespace p44 */

#endif /* __p44vdc__homeconnectdevicewasher__ */
