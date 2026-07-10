//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2024-2026 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__proxyvdc__
#define __p44vdc__proxyvdc__

#include "p44vdc_common.hpp"

#if ENABLE_PROXYDEVICES

#if !defined(PROXY_DNSSD_DISCOVERY) && !DISABLE_DISCOVERY
  #define PROXY_DNSSD_DISCOVERY 1
#endif


#if PROXY_DNSSD_DISCOVERY
  #include "dnssd.hpp"
#endif

#include "vdc.hpp"
#include "proxydevice.hpp"

#include "p44bridgeapi.hpp"

using namespace std;

namespace p44 {

  class ProxyVdc;
  class ProxyDevice;

  /// persistence for proxy device container
  class ProxyPersistence : public SQLite3TableGroup
  {
    typedef SQLite3TableGroup inherited;
  protected:
    /// Get DB Schema creation/upgrade SQL statements
    virtual string schemaUpgradeSQL(int aFromVersion, int &aToVersion);
  };


  typedef boost::intrusive_ptr<ProxyVdc> ProxyVdcPtr;
  class ProxyVdc final : public Vdc
  {
    typedef Vdc inherited;
    friend class ProxyDevice;

    P44BridgeApi mBridgeApi;
    bool mProxiedDSUID;
    string mProxiedDeviceSerial;
    string mProxiedDeviceConfigUrl;
    StatusCB mInitialisationCompleteCB;
    MLTicket mInitialisationTimeout;
    bool mProxiedDeviceReached;
    bool mConfirmed; ///< confirmed proxy origin
    uint64_t mRowId; ///< rowid in global proxy table

    static int mNextInstanceNumber;
    static ProxyPersistence* mDbP;

  public:

    /// instantiate a proxy vdc for each of the specified proxies
    /// @param aProxiesSpecification comma separate list of "host[:port]" specifications of bridgeAPI hosts
    ///   or the string "dnssd" to discover available proxies via DNS-SD
    static void instantiateProxies(const string aProxiesSpecification, VdcHost *aVdcHostP, int aTag);

    ProxyVdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag);

    virtual ~ProxyVdc();

    /// check if confirmed for operation (scannable)
    virtual bool isConfirmed() P44_OVERRIDE { return mConfirmed; }

    /// access to the shared persistence instance for all ProxyVdcs
    static ProxyPersistence& sharedDb(VdcHost& aVdcHost);

    /// @name P44BridgeApi
    /// @{

    /// @return the P44 bridge API for this vdc
    P44BridgeApi& api() { return mBridgeApi; };

    /// @brief Set up connection parameters for the P44 bridge API
    /// @param aApiHost the host name of the P44 bridge API server
    /// @param aApiService the "service name" (at this time: port number only) of the P44 bridge API server
    void setAPIParams(const string aApiHost, const string aApiService);

    /// @}

    /// initialize vdc.
    /// @note this implementation will query the proxyied device and then change it's dSUID
    ///   which is allowed to happen before aCompletedCB is called. vdhost will re-map the
    ///   already registered vdc to its new dSUID.
    virtual void initialize(StatusCB aCompletedCB, bool aFactoryReset) P44_OVERRIDE;

    // derive dSUID
    // Note: base class implementation derives dSUID from vdcInstanceIdentifier()
    //   This is what most vDCs actually use, but some vDCs might want/need to derived their ID from
    //   data obtained at initialize() and might override this method to generate a updated dSUID
    //   when called after initialize()
    virtual void deriveDsUid() P44_OVERRIDE;

    virtual const char *vdcClassIdentifier() const P44_OVERRIDE;

    /// vdc level methods
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams) P44_OVERRIDE;

    /// @return hardware GUID in URN format to identify the hardware INSTANCE as uniquely as possible
    virtual string hardwareGUID() const P44_OVERRIDE;

    /// @return URL for Web-UI (for access from local LAN)
    virtual string webuiURLString() const P44_OVERRIDE;

    /// get supported rescan modes for this vDC. This indicates (usually to a web-UI) which
    /// of the flags to collectDevices() make sense for this vDC.
    /// @return a combination of rescanmode_xxx bits
    virtual int getRescanModes() const P44_OVERRIDE;

    /// scan for (collect) devices and add them to the vdc
    virtual void scanForDevices(StatusCB aCompletedCB, RescanMode aRescanFlags) P44_OVERRIDE;

    /// @return true if vdc is configured for having/collecting devices
    virtual bool isConfigured() P44_OVERRIDE;

    /// @return human readable, language independent suffix to explain vdc functionality.
    ///   Will be appended to product name to create modelName() for vdcs
    virtual string vdcModelSuffix() const P44_OVERRIDE { return "Proxy"; }

    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

    /// deliver (forward) notifications to devices in one call instead of forwarding on device level
    virtual void deliverToDevicesAudience(DsAddressablesList aAudience, VdcApiConnectionPtr aApiConnection, const string &aNotification, ApiValuePtr aParams) P44_OVERRIDE;

  private:

    #if PROXY_DNSSD_DISCOVERY
    static bool p44BridgeApiDiscoveryHandler(ErrorPtr aError, DnsSdServiceInfoPtr aServiceInfo, VdcHost *aVdcHostP, int aTag);
    #endif

    void initialisationTimeout();
    void acknowledgeInitialisation(ErrorPtr aStatus);
    void bridgeApiConnectedHandler(ErrorPtr aStatus);
    void bridgeApiIDQueryHandler(ErrorPtr aError, JsonObjectPtr aJsonMsg);
    void bridgeApiCollectQueryHandler(StatusCB aCompletedCB, ErrorPtr aError, JsonObjectPtr aJsonMsg);

    void bridgeApiNotificationHandler(ErrorPtr aError, JsonObjectPtr aJsonMsg);

    ProxyDevicePtr addProxyDevice(JsonObjectPtr aDeviceJSON);

    bool handleBridgeLevelNotification(const string aNotification, JsonObjectPtr aParams);

    void confirmedAndCollected(VdcApiRequestPtr aRequest, ErrorPtr aError);


  };

} // namespace p44


#endif // ENABLE_PROXYDEVICES
#endif // __p44vdc__proxyvdc__
