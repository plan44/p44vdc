//
//  Copyright (c) 1-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__common__
#define __p44vdc__common__

#include "p44vdc_config.hpp"

#include "p44utils_common.hpp"

#include "application.hpp"

/// vDC API version
/// 1 (aka 1.0 in JSON) : first version, used in P44-DSB-DEH versions up to 0.5.0.x
/// 2 : cleanup, no official JSON support any more, added MOC extensions
/// 3 : changed addressing of buttons, sensors, binaryinputs and channels to string ids. Indices still available for mapping
#define VDC_API_VERSION_MIN 2
#define VDC_API_VERSION_MAX 3


#endif /* defined(__p44vdc__common__) */
