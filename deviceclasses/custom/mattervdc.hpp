//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2015-2026 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__mattervdc__
#define __p44vdc__mattervdc__

#include "p44vdc_common.hpp"

#if ENABLE_MATTER

#include "externalvdc.hpp"

using namespace std;

namespace p44 {

  class MatterVdc;
  class MatterDevice;

  typedef boost::intrusive_ptr<ExternalDeviceConnector> ExternalDeviceConnectorPtr;

  typedef boost::intrusive_ptr<MatterDevice> MatterDevicePtr;
  class MatterDevice : public ExternalDevice
  {
    typedef ExternalDevice inherited;

    friend class MatterVdc;

  public:

    MatterDevice(Vdc *aVdcP, ExternalDeviceConnectorPtr aDeviceConnector, string aTag);
    virtual ~MatterDevice();

    MatterVdc &getMatterVdc();

  protected:

  };


  typedef boost::intrusive_ptr<MatterVdc> MatterVdcPtr;
  class MatterVdc: public ExternalVdc
  {
    typedef ExternalVdc inherited;
    friend class MatterDevice;

    ExternalDeviceConnectorPtr mMatterConnector; ///< for now, only one connector

  public:

    MatterVdc(int aInstanceNumber, const string &aSocketPathOrPort, bool aNonLocal, VdcHost *aVdcHostP, int aTag);

    virtual void initialize(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    virtual const char *vdcClassIdentifier() const P44_OVERRIDE;

    /// vdc level methods
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams) P44_OVERRIDE;

    /// scan for (collect) devices and add them to the vdc
    virtual void scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags) P44_OVERRIDE;

    /// @return human readable, language independent suffix to explain vdc functionality.
    ///   Will be appended to product name to create modelName() for vdcs
    virtual string vdcModelSuffix() const P44_OVERRIDE { return "matter"; }

    /// get supported rescan modes for this vDC. This indicates (usually to a web-UI) which
    /// of the flags to collectDevices() make sense for this vDC.
    /// @return a combination of rescanmode_xxx bits
    virtual int getRescanModes() const P44_OVERRIDE { return rescanmode_exhaustive; }; // only exhaustive makes sense

  protected:

    virtual SocketCommPtr deviceApiConnectionHandler(SocketCommPtr aServerSocketCommP) P44_OVERRIDE;

    virtual void connectionClosed(ExternalDeviceConnector& aConnector) P44_OVERRIDE;


  };

} // namespace p44


#endif // ENABLE_MATTER
#endif // !__p44vdc__mattervdc__
