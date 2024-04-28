//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2024 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__dmxvdc__
#define __p44vdc__dmxvdc__

#include "p44vdc_common.hpp"

#if ENABLE_OLA || ENABLE_DMX

#include "vdc.hpp"
#include "device.hpp"

#if ENABLE_OLA
  #include <ola/DmxBuffer.h>
  #include <ola/Logging.h>
  #include <ola/client/StreamingClient.h>
#endif
#if ENABLE_DMX
  #include "serialcomm.hpp"
#endif


using namespace std;

namespace p44 {

  typedef uint16_t DmxChannel;
  typedef uint8_t DmxValue;
  const DmxChannel dmxNone = 0; // no channel

  class DmxVdc;
  class DmxDevice;
  typedef boost::intrusive_ptr<DmxDevice> DmxDevicePtr;


  /// persistence for ola device container
  class DmxDevicePersistence : public SQLite3Persistence  {
    typedef SQLite3Persistence inherited;
  protected:
    /// Get DB Schema creation/upgrade SQL statements
    virtual string dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion);
  };


	typedef std::multimap<string, string> DeviceConfigMap;
	
  typedef boost::intrusive_ptr<DmxVdc> DmxVdcPtr;
  class DmxVdc : public Vdc
  {
    typedef Vdc inherited;
    friend class DmxDevice;

    DmxDevicePersistence mDb;

    bool mUseOLA;

    // DMX sender thread
    ChildThreadWrapperPtr mDMXSenderThread;
    pthread_mutex_t mDmxBufferAccess;

    #if ENABLE_OLA
    ola::DmxBuffer *mOlaDmxBufferP;
    ola::client::StreamingClient *mOlaClientP;
    uint8_t mDMXUniverse;
    #endif
    #if ENABLE_DMX
    uint8_t* mSerialDmxBufferP;
    SerialCommPtr mDmxSender;
    #endif


  public:
    DmxVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag);

    /// set connection to use for DMX
    /// @param aDmxOutputSpec can be serial interface spec or "ola[:universe]" to use OLA
    /// @param aDefaultPort the default port to use when aDmxOutputSpec is a TCP connection
    void setDmxOutput(const string aDmxOutputSpec, uint16_t aDefaultPort);

    virtual void initialize(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

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
    virtual string vdcModelSuffix() const P44_OVERRIDE { return "DMX512"; }

  private:

    DmxDevicePtr addDmxDevice(string aDeviceType, string aDeviceConfig);

    #if ENABLE_OLA
    void olaThreadRoutine(ChildThreadWrapper &aThread);
    #endif
    #if ENABLE_DMX
    void dmxThreadRoutine(ChildThreadWrapper &aThread);
    #endif

    void setDMXChannel(DmxChannel aChannel, DmxValue aChannelValue);

  };

} // namespace p44

#endif // ENABLE_OLA || ENABLE_DMX
#endif // __p44vdc__dmxvdc__
