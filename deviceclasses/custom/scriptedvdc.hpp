//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2021-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__scriptedvdc__
#define __p44vdc__scriptedvdc__

#include "p44vdc_common.hpp"

#if ENABLE_SCRIPTED

#include "customdevice.hpp"
#include "jsonobject.hpp"
#include "p44script.hpp"

using namespace std;

namespace p44 {

  class ScriptedVdc;
  class ScriptedDevice;


  /// class for independent persistence of implementation details
  /// @note cannot be in DeviceSettings, because these are behaviour-related, not
  ///    implementation related
  class ScriptedDeviceImplementation : public PersistentParams
  {
    typedef PersistentParams inherited;
    friend class ScriptedDevice;

    ScriptedDevice &mScriptedDevice; ///< the related scripted device
    ScriptSource mScript; ///< the (p44script) device implementation
    ScriptMainContextPtr mContext; ///< context for implementation script
    MLTicket mRestartTicket; ///< the implementation

  protected:

    ScriptedDeviceImplementation(ScriptedDevice &aScriptedDevice);

    // persistence implementation
    virtual const char *tableName() P44_OVERRIDE;
    virtual size_t numFieldDefs() P44_OVERRIDE;
    virtual const FieldDefinition *getFieldDef(size_t aIndex) P44_OVERRIDE;
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP) P44_OVERRIDE;
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags) P44_OVERRIDE;

  };


  /// persistence for scripted device container
  class ScriptedDevicePersistence : public SQLite3Persistence  {
    typedef SQLite3Persistence inherited;
  protected:
    /// Get DB Schema creation/upgrade SQL statements
    virtual string dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion);
  };


  /// represents the objects related to a ledchaindevice
  class ScriptedDeviceLookup : public BuiltInMemberLookup
  {
    typedef BuiltInMemberLookup inherited;
    ScriptedDevice& mScriptedDevice;
  public:
    ScriptedDeviceLookup(ScriptedDevice& aScriptedDevice);
    ScriptedDevice& scriptedDevice() { return mScriptedDevice; };
  };


  typedef boost::intrusive_ptr<ScriptedDevice> ScriptedDevicePtr;
  class ScriptedDevice : public CustomDevice, public EventSource
  {
    typedef CustomDevice inherited;
    friend class ScriptedVdc;
    friend class ScriptedDeviceLookup;

    ScriptedDeviceLookup mScriptedDeviceLookup;
    ScriptedDeviceImplementation mImplementation;

    string mDefaultUniqueId; ///< the default unique ID, generated at creation
    long long mScriptedDeviceRowID; ///< the ROWID this device was created from (0=none)
    string mInitMessageText; ///< the init message text (for reference)

  public:

    ScriptedDevice(Vdc *aVdcP, const string aDefaultUniqueId, bool aSimpleText);
    virtual ~ScriptedDevice();

    ScriptedVdc &getScriptedVdc();

    /// @return a new script object representing this device. Derived device classes might return different types of device object.
    virtual ScriptObjPtr newDeviceObj() P44_OVERRIDE;

    /// device level API methods (p44 specific, JSON only, for debugging script implementations)
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams) P44_OVERRIDE;

    /// send message from implementation to device
    ErrorPtr sendDeviceMesssage(JsonObjectPtr aMessage);

  protected:

    /// (re)start the device Implementation script
    void restartImplementation();

    /// stop implementation script
    void stopImplementation();

    /// initializes the physical device for being used
    /// @param aFactoryReset if set, the device will be inititalized as thoroughly as possible (factory reset, default settings etc.)
    /// @note this is called before interaction with dS system starts
    /// @note implementation should call inherited when complete, so superclasses could chain further activity
    virtual void initializeDevice(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    /// disconnect device. If presence is represented by data stored in the vDC rather than
    /// detection of real physical presence on a bus, this call must clear the data that marks
    /// the device as connected to this vDC (such as a learned-in EnOcean button).
    /// For devices where the vDC can be *absolutely certain* that they are still connected
    /// to the vDC AND cannot possibly be connected to another vDC as well, this call should
    /// return false.
    /// @param aForgetParams if set, not only the connection to the device is removed, but also all parameters related to it
    ///   such that in case the same device is re-connected later, it will not use previous configuration settings, but defaults.
    /// @param aDisconnectResultHandler will be called to report true if device could be disconnected,
    ///   false in case it is certain that the device is still connected to this and only this vDC
    /// @note at the time aDisconnectResultHandler is called, the only owner left for the device object might be the
    ///   aDevice argument to the DisconnectCB handler.
    virtual void disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler) P44_OVERRIDE;

    /// check if device can be disconnected by software (i.e. Web-UI)
    /// @return true if device might be disconnectable by the user via software (i.e. web UI)
    /// @note devices returning true here might still refuse disconnection on a case by case basis when
    ///   operational state does not allow disconnection.
    /// @note devices returning false here might still be disconnectable using disconnect() triggered
    ///   by vDC API "remove" method.
    virtual bool isSoftwareDisconnectable() P44_OVERRIDE { return true; };


    /// load parameters from persistent DB
    /// @note this is usually called from the device container when device is added (detected), before initializeDevice() and after identifyDevice()
    virtual ErrorPtr load() P44_OVERRIDE;

    /// save unsaved parameters to persistent DB
    /// @note this is usually called from the device container in regular intervals
    virtual ErrorPtr save() P44_OVERRIDE;

    /// forget any parameters stored in persistent DB
    virtual ErrorPtr forget() P44_OVERRIDE;

    // check if any settings are dirty
    virtual bool isDirty() P44_OVERRIDE;

    // make all settings clean (not to be saved to DB)
    virtual void markClean() P44_OVERRIDE;

    /// return a default unique id for the device
    virtual string defaultUniqueId() P44_OVERRIDE { return mDefaultUniqueId; };

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

  private:

    void implementationEnds(ScriptObjPtr aResult);

    virtual void sendDeviceApiJsonMessage(JsonObjectPtr aMessage) P44_OVERRIDE;
    virtual void sendDeviceApiSimpleMessage(string aMessage) P44_OVERRIDE;

  };


  typedef boost::intrusive_ptr<ScriptedVdc> ScriptedVdcPtr;
  class ScriptedVdc : public CustomVdc
  {
    typedef CustomVdc inherited;
    friend class ScriptedDevice;

    ScriptedDevicePersistence mDb;

  public:
    ScriptedVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag);

    void initialize(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    virtual const char *vdcClassIdentifier() const P44_OVERRIDE;

    /// scan for (collect) devices and add them to the vdc
    virtual void scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags) P44_OVERRIDE;

    /// @return human readable, language independent suffix to explain vdc functionality.
    ///   Will be appended to product name to create modelName() for vdcs
    virtual string vdcModelSuffix() const P44_OVERRIDE { return "scripted"; }

    /// get supported rescan modes for this vDC. This indicates (usually to a web-UI) which
    /// of the flags to collectDevices() make sense for this vDC.
    /// @return a combination of rescanmode_xxx bits
    virtual int getRescanModes() const P44_OVERRIDE { return rescanmode_exhaustive; }; // only exhaustive makes sense

    /// identify the vdc to the user in some way
    /// @param aDuration if !=Never, this is how long the identification should be recognizable. If this is \<0, the identification should stop
    /// @note usually, this would be a LED or buzzer in the vdc device (bridge, gateway etc.)
    virtual void identifyToUser(MLMicroSeconds aDuration) P44_OVERRIDE;

    /// vdc level methods (p44 specific, JSON only, for configuring scripted devices)
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams) P44_OVERRIDE;

  private:

    ScriptedDevicePtr addScriptedDevice(const string aScptDevId, JsonObjectPtr aInitObj, ErrorPtr &aErr);

  };

} // namespace p44


#endif // ENABLE_SCRIPTED
#endif // __p44vdc__scriptedvdc__
