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

		DaliPersistence db;
    DaliDeviceInfoMap deviceInfoCache;

    #if ENABLE_DALI_INPUTS
    DaliInputDeviceList inputDevices;
    #endif

  public:
    DaliVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag);

		void initialize(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    // the DALI communication object
    DaliCommPtr daliComm;

    virtual const char *vdcClassIdentifier() const P44_OVERRIDE;

    /// perform self test
    /// @param aCompletedCB will be called when self test is done, returning ok or error
    virtual void selfTest(StatusCB aCompletedCB) P44_OVERRIDE;

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
    ErrorPtr ungroupDevice(DaliDevicePtr aDevice, VdcApiRequestPtr aRequest);

    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

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

    ErrorPtr groupDevices(VdcApiRequestPtr aRequest, ApiValuePtr aParams);
    ErrorPtr daliScan(VdcApiRequestPtr aRequest, ApiValuePtr aParams);
    ErrorPtr daliCmd(VdcApiRequestPtr aRequest, ApiValuePtr aParams);

    typedef boost::shared_ptr<std::string> StringPtr;
    void daliScanNext(VdcApiRequestPtr aRequest, DaliAddress aShortAddress, StringPtr aResult);
    void handleDaliScanResult(VdcApiRequestPtr aRequest, DaliAddress aShortAddress, StringPtr aResult, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError);

    void testScanDone(StatusCB aCompletedCB, DaliComm::ShortAddressListPtr aShortAddressListPtr, DaliComm::ShortAddressListPtr aUnreliableShortAddressListPtr, ErrorPtr aError);
    void testRW(StatusCB aCompletedCB, DaliAddress aShortAddr, uint8_t aTestByte);
    void testRWResponse(StatusCB aCompletedCB, DaliAddress aShortAddr, uint8_t aTestByte, bool aNoOrTimeout, uint8_t aResponse, ErrorPtr aError);

    #if ENABLE_DALI_INPUTS
    void daliEventHandler(uint8_t aEvent, uint8_t aData1, uint8_t aData2);
    DaliInputDevicePtr addInputDevice(const string aConfig, DaliAddress aDaliBaseAddress);
    ErrorPtr addDaliInput(VdcApiRequestPtr aRequest, ApiValuePtr aParams);
    #endif

  };

} // namespace p44

#endif // ENABLE_DALI
#endif // __p44vdc__dalivdc__
