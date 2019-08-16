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

#ifndef __p44vdc__ledchainvdc__
#define __p44vdc__ledchainvdc__

#include "p44vdc_common.hpp"

#if ENABLE_LEDCHAIN

#include "vdc.hpp"
#include "device.hpp"
#include "colorlightbehaviour.hpp"

#include "ledchaincomm.hpp"

#include "viewstack.hpp"

using namespace std;

namespace p44 {

  class LedChainVdc;
  class LedChainDevice;
  typedef boost::intrusive_ptr<LedChainDevice> LedChainDevicePtr;


  /// persistence for LedChain device container
  class LedChainDevicePersistence : public SQLite3Persistence  {
    typedef SQLite3Persistence inherited;
  protected:
    /// Get DB Schema creation/upgrade SQL statements
    virtual string dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion);
  };

  typedef vector<string> StringVector;

  typedef boost::intrusive_ptr<LedChainVdc> LedChainVdcPtr;
  class LedChainVdc : public Vdc
  {
    typedef Vdc inherited;
    friend class LedChainDevice;

    LedChainDevicePersistence db;
    LEDChainArrangement ledArrangement;
    uint8_t maxOutValue;
    ViewStackPtr rootView;

    typedef std::list<LedChainDevicePtr> LedChainDeviceList;

  public:
  
    LedChainVdc(int aInstanceNumber, StringVector aLedChainConfigs, VdcHost *aVdcHostP, int aTag);

    void initialize(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    virtual const char *vdcClassIdentifier() const P44_OVERRIDE;

    /// scan for (collect) devices and add them to the vdc
    virtual void scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags) P44_OVERRIDE;

    void removeDevice(DevicePtr aDevice, bool aForget) P44_OVERRIDE;

    /// set max output value to send to WS2812 LEDs. This is like a global brightness limit, to prevent LED chain
    /// power supply overload
    /// @param aMaxOutValue max output value, 0..255.
    void setMaxOutValue(uint8_t aMaxOutValue) { maxOutValue = aMaxOutValue; };

    /// get minimum brigthness for dimming
    /// @return minimum brightness that will just barely keep the LEDs on
    Brightness getMinBrightness();

    /// get minimum brigthness for dimming
    /// @return true if LEDs have a separate white channel
    bool hasWhite();

    /// some containers (statically defined devices for example) should be invisible for the dS system when they have no
    /// devices.
    /// @return if true, this vDC should not be announced towards the dS system when it has no devices
    virtual bool invisibleWhenEmpty() P44_OVERRIDE { return true; }

    /// vdc level methods (p44 specific, JSON only, for creating LED chain devices)
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
    virtual string vdcModelSuffix() const P44_OVERRIDE { return "Smart LED Chains"; }

  private:

    LedChainDevicePtr addLedChainDevice(int aX, int aDx, int aY, int aDy, string aDeviceConfig);

    void render();

  };

} // namespace p44

#endif // ENABLE_LEDCHAIN
#endif // __p44vdc__ledchainvdc__
