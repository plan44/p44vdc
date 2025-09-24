//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__devicesettings__
#define __p44vdc__devicesettings__

#include "persistentparams.hpp"

#include "dsdefs.h"

using namespace std;

namespace p44 {

  class Device;
  class DeviceSettings;
  class DsScene;


  /// Base class for persistent settings common to all devices.
  /// @note This class can be used as-is for devices without a scene table (such as pure inputs and sensors),
  ///   but it also is the base class for SceneDeviceSettings which implements a scene table on top of it.
  class DeviceSettings : public PersistentParams, public P44Obj
  {
    typedef PersistentParams inherited;
    friend class Device;
    friend class DsScene;

  protected:

    Device &mDevice;

  public:
    DeviceSettings(Device &aDevice);
    virtual ~DeviceSettings() {}; // important for multiple inheritance!

    /// global dS zone ID, zero if no zone assigned
    DsZoneID mZoneID;

    #if ENABLE_JSONBRIDGEAPI
    // IMPORTANT: these are mapped to bits0..3 of device global flags, with all input flags inverted such that
    //   old settings which had only a single flag at bit 0 keep the meaning of "bridge everything".
    //   So THESE BIT POSITIONS ARE PART OF PERSISTENT SETTINGS, DO NOT CHANGE!
    typedef enum {
      bridge_none = 0,
      bridge_output = 0x01, // stored 1:1 as bit 0 in device global flag word
      bridge_sensors = 0x02, // stored inverted as bit 1 in device global flag word
      bridge_inputs = 0x04, // stored inverted as bit 2 in device global flag word
      bridge_buttons = 0x08, // stored inverted as bit 3 in device global flag word
      bridge_all = 0x0F, // bridge all behaviours
      bridge_flags_mask = 0x0F // all flags mask
    } BridgingFlags;

    /// allow bridging via bridge API
    BridgingFlags mBridgingFlags;
    #endif // ENABLE_JSONBRIDGEAPI

    // persistence implementation
    virtual const char *tableName();
    virtual size_t numFieldDefs();
    virtual const FieldDefinition *getFieldDef(size_t aIndex);
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP);
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags);

    /// @}

  };
  typedef boost::intrusive_ptr<DeviceSettings> DeviceSettingsPtr;

  
} // namespace p44


#endif /* defined(__p44vdc__devicesettings__) */
