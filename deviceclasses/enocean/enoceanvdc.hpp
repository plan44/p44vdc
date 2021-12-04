//
//  Copyright (c) 2013-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__enoceanvdc__
#define __p44vdc__enoceanvdc__

#include "p44vdc_common.hpp"

#if ENABLE_ENOCEAN

#include "vdc.hpp"
#include "sqlite3persistence.hpp"

#include "enoceancomm.hpp"
#include "enoceandevice.hpp"


using namespace std;

namespace p44 {

  typedef std::multimap<EnoceanAddress, EnoceanDevicePtr> EnoceanDeviceMap;

  #if ENABLE_ENOCEAN_SECURE
  typedef std::map<EnoceanAddress, EnOceanSecurityPtr> EnoceanSecurityMap;
  #endif

  /// persistence for enocean device container
  class EnoceanPersistence : public SQLite3Persistence
  {
    typedef SQLite3Persistence inherited;
  protected:
    /// Get DB Schema creation/upgrade SQL statements
    virtual string dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion);
  };


  class EnoceanVdc;
  typedef boost::intrusive_ptr<EnoceanVdc> EnoceanVdcPtr;
  class EnoceanVdc : public Vdc
  {
    friend class EnoceanDevice;
    typedef Vdc inherited;

    bool learningMode;
    bool disableProximityCheck;
    Tristate onlyEstablish;
    bool selfTesting;

    EnoceanDeviceMap enoceanDevices; ///< local map linking EnoceanDeviceID to devices

		EnoceanPersistence db;

    #if ENABLE_ENOCEAN_SECURE
    EnoceanSecurityMap securityInfos; ///< local map of active security contexts
    #endif


  public:

    EnoceanVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag);

    /// set the log level offset on this logging object (and possibly contained sub-objects)
    /// @param aLogLevelOffset the new log level offset
    virtual void setLogLevelOffset(int aLogLevelOffset) P44_OVERRIDE;

		void initialize(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    // the Enocean communication object
    EnoceanComm enoceanComm;

    virtual const char *vdcClassIdentifier() const P44_OVERRIDE;

    #if SELFTESTING_ENABLED
    /// perform self test
    /// @param aCompletedCB will be called when self test is done, returning ok or error
    virtual void selfTest(StatusCB aCompletedCB) P44_OVERRIDE;
    #endif

    /// scan for (collect) devices and add them to the vdc
    virtual void scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags) P44_OVERRIDE;

    /// vdc level methods (p44 specific, JSON only)
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams) P44_OVERRIDE;

    /// @param aForget if set, all parameters stored for the device (if any) will be deleted. Note however that
    ///   the devices are not disconnected (=unlearned) by this.
    virtual void removeDevices(bool aForget) P44_OVERRIDE;

    /// @return human readable, language independent suffix to explain vdc functionality.
    ///   Will be appended to product name to create modelName() for vdcs
    virtual string vdcModelSuffix() const P44_OVERRIDE { return "EnOcean"; }

    /// @return human readable model version specific to that vDC
    virtual string vdcModelVersion() const P44_OVERRIDE;

    /// @return hardware GUID in URN format to identify hardware as uniquely as possible
    /// - enoceanaddress:XXXXXXXX = 8 hex digits enOcean device address
    virtual string hardwareGUID() P44_OVERRIDE { return string_format("enoceanaddress:%08X", enoceanComm.modemAddress()); };

    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

  protected:

    /// add device to container (already known device, already stored in DB)
    /// @return false if aEnoceanDevice dSUID is already known and thus was *not* added
    bool addKnownDevice(EnoceanDevicePtr aEnoceanDevice);

    /// add newly learned device to EnOcean container (and remember it in DB)
    /// @return false if aEnoceanDevice dSUID is already known and thus was *not* added
    bool addAndRememberDevice(EnoceanDevicePtr aEnoceanDevice);

    /// un-pair devices by physical device address
    /// @param aEnoceanAddress address for which to disconnect and forget all physical devices
    /// @param aEEP EEP to learn out (or 0 for any EEP with this address). Note: Variant will NOT be checked!
    /// @param aForgetParams if set, associated dS level configuration will be cleared such that
    ///   after reconnect the device will appear with default config
    /// @param aFromIndex starting subdevice index, defaults to 0
    /// @param aNumIndices how many subdevice index positions (0 = all)
    /// @return true if any device was actually matched and removed
    bool unpairDevicesByAddressAndEEP(EnoceanAddress aEnoceanAddress, EnoceanProfile aEEP, bool aForgetParams, EnoceanSubDevice aFromIndex=0, EnoceanSubDevice aNumIndices=0);

    /// set container learn mode
    /// @param aEnableLearning true to enable learning mode
    /// @param aDisableProximityCheck true to disable proximity check (e.g. minimal RSSI requirement for some radio devices)
    /// @param aOnlyEstablish set this to yes to only learn in, to no to only learn out or to undefined to allow both learn-in and out.
    /// @note learn events (new devices found or devices removed) must be reported by calling reportLearnEvent() on VdcHost.
    virtual void setLearnMode(bool aEnableLearning, bool aDisableProximityCheck, Tristate aOnlyEstablish) P44_OVERRIDE;

    /// remove device
    /// @param aDevice device to remove (possibly only part of a multi-function physical device)
    virtual void removeDevice(DevicePtr aDevice, bool aForget) P44_OVERRIDE;


    /// get security info for given sender
    /// @param aSender enocean device address
    /// @note this method has a dummy implementation when ENABLE_ENOCEAN_SECURE is not set
    /// @return the security info or NULL if none exists
    EnOceanSecurityPtr findSecurityInfoForSender(EnoceanAddress aSender);

    #if ENABLE_ENOCEAN_SECURE

    /// create new security info record for given sender
    /// @param aSender enocean device address
    /// @return the new security info
    EnOceanSecurityPtr newSecurityInfoForSender(EnoceanAddress aSender);

    /// associate security info with devices related to the sender address
    /// @note before calling this, security info record might already exist for collecting further segments of the teachin
    /// @param aSecurityInfo completely and valid security info
    /// @param aSender enocean device address
    void associateSecurityInfoWithSender(EnOceanSecurityPtr aSecurityInfo, EnoceanAddress aSender);

    /// load the security infos from DB
    void loadSecurityInfos();

    /// save the security info record
    /// @param aSecurityInfo the security info
    /// @param aEnoceanAddrss the address the info belongs to
    /// @param aRLConly only update the RLC
    /// @param aOnlyIfNeeded only save when RLC or time difference demands it (but saving flash write cycles)
    /// @return true if successfully saved
    bool saveSecurityInfo(EnOceanSecurityPtr aSecurityInfo, EnoceanAddress aEnoceanAddress, bool aRLCOnly, bool aOnlyIfNeeded);

    /// remove unused security info in case aDevice is the last subdevice of the physical enocean device
    /// @param aDevice the enocean device that is now being deleted
    void removeUnusedSecurity(EnoceanDevice &aDevice);

  private:

    /// drop (forget) security info for given sender
    /// @param aSender enocean device address
    /// @return true if successfully deleted
    /// @note this MUST NOT not be called for senders that still have devices! Use removeUnusedSecurity() instead
    bool dropSecurityInfoForSender(EnoceanAddress aSender);

    #endif

    void handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr, ErrorPtr aError);
    void handleEventPacket(Esp3PacketPtr aEsp3PacketPtr, ErrorPtr aError);
    void handleTestRadioPacket(StatusCB aCompletedCB, Esp3PacketPtr aEsp3PacketPtr, ErrorPtr aError);

    Tristate processLearn(EnoceanAddress aDeviceAddress, EnoceanProfile aEEProfile, EnoceanManufacturer aManufacturer, Tristate aTeachInfoType, EnoceanLearnType aLearnType, Esp3PacketPtr aLearnPacket, EnOceanSecurityPtr aSecurityInfo);

    ErrorPtr addProfile(VdcApiRequestPtr aRequest, ApiValuePtr aParams);
    ErrorPtr simulatePacket(VdcApiRequestPtr aRequest, ApiValuePtr aParams);
    ErrorPtr sendCommand(VdcApiRequestPtr aRequest, ApiValuePtr aParams);
    void sendCommandResponse(VdcApiRequestPtr aRequest, Esp3PacketPtr aEsp3PacketPtr, ErrorPtr aError);

  };

} // namespace p44

#endif // ENABLE_ENOCEAN
#endif // __p44vdc__enoceanvdc__
