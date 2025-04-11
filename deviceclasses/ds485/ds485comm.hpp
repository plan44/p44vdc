//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__ds485comm__
#define __p44vdc__ds485comm__

#include "p44vdc_common.hpp"

#if ENABLE_DS485DEVICES

#include "dsuid.hpp"
#include "ds485-client.h"

using namespace std;

namespace p44 {

  class Ds485Comm;

  typedef boost::intrusive_ptr<Ds485Comm> Ds485CommPtr;
  class Ds485Comm final : public P44LoggingObj
  {
    typedef P44LoggingObj inherited;

    ds485ClientHandle_t mDs485Client;
    ds485c_callbacks mDs485Callbacks;

    string mConnSpec; ///< ds485 connection spec

  public:

    Ds485Comm();
    virtual ~Ds485Comm();

    virtual string contextType() const P44_OVERRIDE { return "DS485"; }

    void setConnectionSpecification(const char *aConnectionSpec, uint16_t aDefaultPort);

    void start(StatusCB aCompletedCB);
    void stop();

    /// @name callback receivers
    /// @{
    int linkStateChanged(bool aActive);
    int busMemberChanged(DsUidPtr aDsUid, bool aJoined);
    int containerReceived(const ds485_container_t *container);
    /// @}

  private:

    /// @name ds485 interaction
    /// @{

    //ErrorPtr sendMessage

    /// @}

  };

} // namespace p44


#endif // ENABLE_DS485DEVICES
#endif // __p44vdc__ds485comm__
