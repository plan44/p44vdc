//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2017-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__zfvdc__
#define __p44vdc__zfvdc__

#include "p44vdc_common.hpp"

#if ENABLE_ZF

#include "vdc.hpp"
#include "sqlite3persistence.hpp"

#include "zfcomm.hpp"
#include "zfdevice.hpp"


using namespace std;

namespace p44 {

  class ZfError : public Error
  {
    typedef enum {
      OK,
      DeviceLearned,
      DeviceUnlearned,
      NoKnownProfile,
      LearnTimeout,
      LearnAborted,
      numErrorCodes
    } ErrorCodes;
    static const char *domain() { return "ZF"; }
    virtual const char *getErrorDomain() const P44_OVERRIDE { return ZfError::domain(); };
    ZfError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
    #if ENABLE_NAMED_ERRORS
  protected:
    virtual const char* errorName() const P44_OVERRIDE { return errNames[getErrorCode()]; };
  private:
    static constexpr const char* const errNames[numErrorCodes] = {
      "OK",
      "DeviceLearned",
      "DeviceUnlearned",
      "NoKnownProfile",
      "LearnTimeout",
      "LearnAborted",
    };
    #endif // ENABLE_NAMED_ERRORS
  };


  typedef std::multimap<ZfAddress, ZfDevicePtr> ZfDeviceMap;


  /// persistence for Zf device container
  class ZfPersistence : public SQLite3TableGroup
  {
    typedef SQLite3TableGroup inherited;
  protected:
    /// Get DB Schema creation/upgrade SQL statements
    virtual string schemaUpgradeSQL(int aFromVersion, int &aToVersion);
  };


  class ZfVdc;
  typedef boost::intrusive_ptr<ZfVdc> ZfVdcPtr;
  class ZfVdc : public Vdc
  {
    friend class ZfDevice;
    typedef Vdc inherited;

    bool mLearningMode;
    Tristate mOnlyEstablish;

    ZfDeviceMap mZfDevices; ///< local map linking ZfDeviceID to devices

		ZfPersistence mDb;

  public:

    ZfVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag);

    /// set the log level offset on this logging object (and possibly contained sub-objects)
    /// @param aLogLevelOffset the new log level offset
    virtual void setLogLevelOffset(int aLogLevelOffset) P44_OVERRIDE;
		
    /// get logging object for a named topic
    virtual P44LoggingObj* getTopicLogObject(const string aTopic) P44_OVERRIDE;

    virtual void initialize(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    // the Zf communication object
    ZfComm mZfComm;

    virtual const char *vdcClassIdentifier() const P44_OVERRIDE;

//    /// perform self test
//    /// @param aCompletedCB will be called when self test is done, returning ok or error
//    virtual void selfTest(StatusCB aCompletedCB) P44_OVERRIDE;

    /// scan for (collect) devices and add them to the vdc
    virtual void scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags) P44_OVERRIDE;

    /// vdc level methods (p44 specific, JSON only)
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams) P44_OVERRIDE;

    /// @param aForget if set, all parameters stored for the device (if any) will be deleted. Note however that
    ///   the devices are not disconnected (=unlearned) by this.
    virtual void removeDevices(bool aForget) P44_OVERRIDE;

    /// @return human readable, language independent suffix to explain vdc functionality.
    ///   Will be appended to product name to create modelName() for vdcs
    virtual string vdcModelSuffix() const P44_OVERRIDE { return "ZF"; }

//    /// @return hardware GUID in URN format to identify hardware as uniquely as possible
//    /// - zfaddress:XXXXXXXX = 8 hex digits ZF device address
//    virtual string hardwareGUID() P44_OVERRIDE { return string_format("zfaddress:%08X", zfComm.modemAddress()); };

    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

  protected:

    /// add device to container (already known device, already stored in DB)
    /// @return false if aZfDevice dSUID is already known and thus was *not* added
    bool addKnownDevice(ZfDevicePtr aZfDevice);

    /// add newly learned device to ZF container (and remember it in DB)
    /// @return false if aZfDevice dSUID is already known and thus was *not* added
    bool addAndRememberDevice(ZfDevicePtr aZfDevice);

    /// un-pair devices by physical device address
    /// @param aZfAddress address for which to disconnect and forget all physical devices
    /// @param aForgetParams if set, associated dS level configuration will be cleared such that
    ///   after reconnect the device will appear with default config
    /// @param aFromIndex starting subdevice index, defaults to 0
    /// @param aNumIndices how many subdevice index positions (0 = all)
    void unpairDevicesByAddress(ZfAddress aZfAddress, bool aForgetParams, ZfSubDevice aFromIndex=0, ZfSubDevice aNumIndices=0);

    /// set container learn mode
    /// @param aEnableLearning true to enable learning mode
    /// @param aDisableProximityCheck true to disable proximity check (e.g. minimal RSSI requirement for some radio devices)
    /// @param aOnlyEstablish set this to yes to only learn in, to no to only learn out or to undefined to allow both learn-in and out.
    /// @note learn events (new devices found or devices removed) must be reported by calling reportLearnEvent() on VdcHost.
    virtual void setLearnMode(bool aEnableLearning, bool aDisableProximityCheck, Tristate aOnlyEstablish) P44_OVERRIDE;

  protected:

    /// remove device
    /// @param aDevice device to remove (possibly only part of a multi-function physical device)
    virtual void removeDevice(DevicePtr aDevice, bool aForget) P44_OVERRIDE;

  private:

    void handlePacket(ZfPacketPtr aPacket, ErrorPtr aError);
    Tristate processLearn(ZfPacketPtr aPacket);
    void dispatchPacket(ZfPacketPtr aPacket);

  };

} // namespace p44

#endif // ENABLE_ZF
#endif // __p44vdc__zfvdc__
