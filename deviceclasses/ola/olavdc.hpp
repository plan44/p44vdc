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

#ifndef __p44vdc__olavdc__
#define __p44vdc__olavdc__

#include "p44vdc_common.hpp"

#if ENABLE_OLA

#include "vdc.hpp"
#include "device.hpp"

#include <ola/DmxBuffer.h>
#include <ola/Logging.h>
#include <ola/client/StreamingClient.h>

using namespace std;

namespace p44 {

  typedef uint16_t DmxChannel;
  typedef uint8_t DmxValue;
  const DmxChannel dmxNone = 0; // no channel

  class OlaVdc;
  class OlaDevice;
  typedef boost::intrusive_ptr<OlaDevice> OlaDevicePtr;


  /// persistence for ola device container
  class OlaDevicePersistence : public SQLite3Persistence  {
    typedef SQLite3Persistence inherited;
  protected:
    /// Get DB Schema creation/upgrade SQL statements
    virtual string dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion);
  };


	typedef std::multimap<string, string> DeviceConfigMap;
	
  typedef boost::intrusive_ptr<OlaVdc> OlaVdcPtr;
  class OlaVdc : public Vdc
  {
    typedef Vdc inherited;
    friend class OlaDevice;

    OlaDevicePersistence db;

    // OLA Thread
    ChildThreadWrapperPtr olaThread;
    pthread_mutex_t olaBufferAccess;
    ola::DmxBuffer *dmxBufferP;
    ola::client::StreamingClient *olaClientP;


  public:
    OlaVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag);

    void initialize(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    virtual const char *vdcClassIdentifier() const P44_OVERRIDE;

    /// scan for (collect) devices and add them to the vdc
    virtual void scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags) P44_OVERRIDE;

    /// vdc level methods (p44 specific, JSON only, for configuring DMX/OLA devices)
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams) P44_OVERRIDE;

    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

    /// @return human readable, language independent suffix to explain vdc functionality.
    ///   Will be appended to product name to create modelName() for vdcs
    virtual string vdcModelSuffix() const P44_OVERRIDE { return "OLA/DMX512"; }

  private:

    OlaDevicePtr addOlaDevice(string aDeviceType, string aDeviceConfig);

    void olaThreadRoutine(ChildThreadWrapper &aThread);
    void setDMXChannel(DmxChannel aChannel, DmxValue aChannelValue);

  };

} // namespace p44

#endif // ENABLE_OLA
#endif // __p44vdc__olavdc__
