//
//  Copyright (c) 2013-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__dalivdc__
#define __p44vdc__dalivdc__

#include "p44vdc_common.hpp"

#if ENABLE_DALI

#include "vdc.hpp"

#include "dalicomm.hpp"
#include "dalidevice.hpp"

using namespace std;

namespace p44 {

  class DaliVdc;

  
  typedef boost::intrusive_ptr<DaliVdc> DaliVdcPtr;

  typedef std::list<DaliBusDevicePtr> DaliBusDeviceList;
  typedef std::map<uint8_t, DaliDeviceInfoPtr> DaliDeviceInfoMap;
  #if ENABLE_DALI_INPUTS
  typedef std::list<DaliInputDevicePtr> DaliInputDeviceList;
  #endif

  typedef boost::shared_ptr<DaliBusDeviceList> DaliBusDeviceListPtr;


  /// persistence for enocean device container
  class DaliPersistence : public SQLite3Persistence
  {
    typedef SQLite3Persistence inherited;
  protected:
    /// Get DB Schema creation/upgrade SQL statements
    virtual string dbSchemaUpgradeSQL(int aFromVersion, int &aToVersion);
  };


  class DaliVdc : public Vdc
  {
    typedef Vdc inherited;
    friend class DaliInputDevice;
    friend class DaliBusDevice;

		DaliPersistence db;
    DaliDeviceInfoMap deviceInfoCache;

    uint16_t usedDaliGroupsMask; ///< bitmask of DALI groups in use by optimizer or manually created composite devices
    uint16_t usedDaliScenesMask; ///< bitmask of DALI scenes in use by optimizer or input devices

    MLTicket groupDimTicket; ///< timer for group dimming
    MLTicket recollectDelayTicket; ///< timer for delayed recollect

    #if ENABLE_DALI_INPUTS
    DaliInputDeviceList inputDevices;
    #endif

  public:
    DaliVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag);

    virtual ~DaliVdc();

		void initialize(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    // the DALI communication object
    DaliCommPtr daliComm;

    virtual const char *vdcClassIdentifier() const P44_OVERRIDE;

    #if SELFTESTING_ENABLED
    /// perform self test
    /// @param aCompletedCB will be called when self test is done, returning ok or error
    virtual void selfTest(StatusCB aCompletedCB) P44_OVERRIDE;
    #endif

    /// get supported rescan modes for this vDC
    /// @return a combination of rescanmode_xxx bits
    virtual int getRescanModes() const P44_OVERRIDE;

    /// scan for (collect) devices and add them to the vdc
    virtual void scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags) P44_OVERRIDE;

    /// vdc level methods (p44 specific, JSON only, for configuring multichannel RGB(W) devices)
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams) P44_OVERRIDE;

    /// @return human readable, language independent suffix to explain vdc functionality.
    ///   Will be appended to product name to create modelName() for vdcs
    virtual string vdcModelSuffix() const P44_OVERRIDE { return "DALI"; }

    /// ungroup a previously grouped device
    /// @param aDevice the device to ungroup
    /// @param aRequest the API request that causes the ungroup, will be sent an OK when ungrouping is complete
    /// @return error if not successful
    ErrorPtr ungroupDevice(DaliOutputDevicePtr aDevice, VdcApiRequestPtr aRequest);

    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

  protected:

    /// @name Implementation methods for native scene and grouped dimming support
    /// @{

    /// this is called once for every native action in use, after startup after existing cache entries have been
    /// read from persistent storage. This allows vDC implementations to know which native scenes/groups are
    /// in use by the optimizer without needing private bookkeeping.
    /// @param aNativeActionId a ID of a native action that is in use by the optimizer
    virtual ErrorPtr announceNativeAction(const string aNativeActionId) P44_OVERRIDE;

    /// execute native action (scene call, dimming operation)
    /// @param aStatusCB must be called to return status. Must return NULL when action was applied.
    ///   Can return Error::OK to signal action was not applied and request device-by-device apply.
    /// @param aNativeActionId the ID of the native action (scene, group) that must be used
    /// @param aDeliveryState can be inspected to obtain details about the affected devices, actionVariant etc.
    virtual void callNativeAction(StatusCB aStatusCB, const string aNativeActionId, NotificationDeliveryStatePtr aDeliveryState) P44_OVERRIDE;

    /// create/reserve new native action
    /// @param aStatusCB must be called to return status. If not ok, aOptimizerEntry must not be changed.
    /// @param aOptimizerEntry the optimizer entry. If a new action is created, the nativeActionId must be updated to the new actionid.
    ///   If creating the native action causes configuration changes in the native device, lastNativeChange should be updated, too.
    /// @param aDeliveryState can be inspected to obtain details such as list of affected devices etc.
    virtual void createNativeAction(StatusCB aStatusCB, OptimizerEntryPtr aOptimizerEntry, NotificationDeliveryStatePtr aDeliveryState) P44_OVERRIDE;

    /// update native action
    /// @param aStatusCB must be called to return status.
    /// @param aOptimizerEntry the optimizer entry. If configuration has changed in the native device, lastNativeChange should be updated.
    /// @param aDeliveryState can be inspected to obtain details such as list of affected devices etc.
    virtual void updateNativeAction(StatusCB aStatusCB, OptimizerEntryPtr aOptimizerEntry, NotificationDeliveryStatePtr aDeliveryState) P44_OVERRIDE;

    /// free native action
    /// @param aNativeActionId a ID of a native action that should be removed
    virtual ErrorPtr freeNativeAction(const string aNativeActionId) P44_OVERRIDE;

    /// @}

  private:

    void removeLightDevices(bool aForget);

    void deviceListReceived(StatusCB aCompletedCB, DaliComm::ShortAddressListPtr aDeviceListPtr, DaliComm::ShortAddressListPtr aUnreliableDeviceListPtr, ErrorPtr aError);
    void queryNextDev(DaliBusDeviceListPtr aBusDevices, DaliBusDeviceList::iterator aNextDev, StatusCB aCompletedCB, ErrorPtr aError);
    void initializeNextDimmer(DaliBusDeviceListPtr aDimmerDevices, uint16_t aGroupsInUse, DaliBusDeviceList::iterator aNextDimmer, StatusCB aCompletedCB, ErrorPtr aError);
    void createDsDevices(DaliBusDeviceListPtr aDimmerDevices, StatusCB aCompletedCB);
    void deviceInfoReceived(DaliBusDeviceListPtr aBusDevices, DaliBusDeviceList::iterator aNextDev, StatusCB aCompletedCB, DaliDeviceInfoPtr aDaliDeviceInfoPtr, ErrorPtr aError);
    void deviceInfoValid(DaliBusDeviceListPtr aBusDevices, DaliBusDeviceList::iterator aNextDev, StatusCB aCompletedCB, DaliDeviceInfoPtr aDaliDeviceInfoPtr);
    void deviceFeaturesQueried(DaliBusDeviceListPtr aBusDevices, DaliBusDeviceList::iterator aNextDev, StatusCB aCompletedCB);
    void recollectDevices(StatusCB aCompletedCB);

    void groupCollected(VdcApiRequestPtr aRequest);

    void loadLocallyUsedGroupsAndScenes();
    void markUsed(DaliAddress aSceneOrGroup, bool aUsed);
    void removeMemberships(DaliAddress aSceneOrGroup);

    ErrorPtr groupDevices(VdcApiRequestPtr aRequest, ApiValuePtr aParams);
    ErrorPtr daliScan(VdcApiRequestPtr aRequest, ApiValuePtr aParams);
    ErrorPtr daliCmd(VdcApiRequestPtr aRequest, ApiValuePtr aParams);
    void bridgeCmdSent(VdcApiRequestPtr aRequest, uint8_t aResp1, uint8_t aResp2, ErrorPtr aError);

    ErrorPtr daliSummary(VdcApiRequestPtr aRequest, ApiValuePtr aParams);
    void daliSummaryScanDone(VdcApiRequestPtr aRequest, DaliComm::ShortAddressListPtr aShortAddressListPtr, DaliComm::ShortAddressListPtr aUnreliableShortAddressListPtr, ErrorPtr aError);
    bool daliAddressSummary(DaliAddress aDaliAddress, ApiValuePtr aInfo);
    bool daliBusDeviceSummary(DaliAddress aDaliAddress, ApiValuePtr aInfo);
    bool daliInfoSummary(DaliDeviceInfoPtr aDeviceInfo, ApiValuePtr aInfo);

    typedef boost::shared_ptr<std::string> StringPtr;
    void daliScanNext(VdcApiRequestPtr aRequest, DaliAddress aShortAddress, StringPtr aResult);
    void handleDaliScanResult(VdcApiRequestPtr aRequest, DaliAddress aShortAddress, StringPtr aResult, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError);

    void groupDimPrepared(StatusCB aStatusCB, DaliAddress aDaliAddress, NotificationDeliveryStatePtr aDeliveryState, ErrorPtr aError);
    void groupDimRepeater(DaliAddress aDaliAddress, uint8_t aCommand, MLTimer &aTimer);
    void nativeActionDone(StatusCB aStatusCB, ErrorPtr aError);

    #if SELFTESTING_ENABLED
    void testScanDone(StatusCB aCompletedCB, DaliComm::ShortAddressListPtr aShortAddressListPtr, DaliComm::ShortAddressListPtr aUnreliableShortAddressListPtr, ErrorPtr aError);
    void testRW(StatusCB aCompletedCB, DaliAddress aShortAddr, uint8_t aTestByte);
    void testRWResponse(StatusCB aCompletedCB, DaliAddress aShortAddr, uint8_t aTestByte, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError);
    #endif

    #if ENABLE_DALI_INPUTS
    void daliEventHandler(uint8_t aEvent, uint8_t aData1, uint8_t aData2);
    DaliInputDevicePtr addInputDevice(const string aConfig, DaliAddress aDaliBaseAddress);
    ErrorPtr addDaliInput(VdcApiRequestPtr aRequest, ApiValuePtr aParams);
    ErrorPtr getDaliInputAddrs(VdcApiRequestPtr aRequest, ApiValuePtr aParams);
    #endif

  };

} // namespace p44

#endif // ENABLE_DALI
#endif // __p44vdc__dalivdc__
