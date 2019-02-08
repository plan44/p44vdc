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

#ifndef __p44vdc__discovery__
#define __p44vdc__discovery__

#include "p44vdc_common.hpp"

#if !DISABLE_DISCOVERY

#include "dsuid.hpp"
#include "vdchost.hpp"


// Avahi includes
#if USE_AVAHI_CORE
// - directly using core, good for small embedded with single process using avahi
#include <avahi-core/core.h>
#include <avahi-core/publish.h>
#include <avahi-core/lookup.h>
#define AvahiService AvahiServer
//#define AvahiServiceState AvahiServerState
#define avahi_service_errno avahi_server_errno
#define avahi_add_service avahi_server_add_service
#define avahi_entry_group_commit avahi_s_entry_group_commit
#define AvahiEntryGroup AvahiSEntryGroup
#define AvahiServiceBrowser AvahiSServiceBrowser
#define AvahiServiceBrowser AvahiSServiceBrowser
#define AvahiServiceResolver AvahiSServiceResolver
#define avahi_entry_group_new avahi_s_entry_group_new
#define avahi_entry_group_reset avahi_s_entry_group_reset
#define avahi_entry_group_free avahi_s_entry_group_free
#define avahi_service_browser_new avahi_s_service_browser_new
#define avahi_service_browser_free avahi_s_service_browser_free
#define avahi_service_resolver_new avahi_s_service_resolver_new
#define avahi_service_resolver_free avahi_s_service_resolver_free
#else
// - use avahi client, desktop/larger embedded (which uses system wide avahi server, together with other clients)
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-client/lookup.h>
#define AvahiService AvahiClient
//#define AvahiServiceState AvahiClientState
#define avahi_service_errno avahi_client_errno
#define avahi_add_service(srv,eg,...) avahi_entry_group_add_service(eg,##__VA_ARGS__)
#endif

#include <avahi-core/log.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>


using namespace std;

namespace p44 {


  /// information about a service that has appeared or disappeared
  class ServiceInfo : public P44Obj
  {
    typedef P44Obj inherited;
    friend class ServiceBrowser;

  public:

    bool disappeared; ///< if set, the browsed service has disappeared
    int lookupFlags; ///< avahi browse/lookup result flags
    string name; ///< service name
    string domain; ///< domain
    string hostname; ///< hostname
    string hostaddress; ///< resolved host address
    uint16_t port; ///< port
    map<string,string> txtRecords; ///< txt records
  };
  typedef boost::intrusive_ptr<ServiceInfo> ServiceInfoPtr;



  /// callback for browser results
  /// @param aServiceInfo the result object of the service discovery
  /// @return must return true to continue looking for services or false
  typedef boost::function<bool (ServiceInfoPtr aServiceInfo)> ServiceDiscoveryCB;

  /// browser for a service
  class ServiceBrowser : public P44Obj
  {
    typedef P44Obj inherited;
    friend class DiscoveryManager;

    AvahiServiceBrowser *avahiServiceBrowser;

    ServiceDiscoveryCB serviceDiscoveryCB;

    ServiceBrowser(const char *aServiceType, ServiceDiscoveryCB aServiceDiscoveryCB);

  public:

    virtual ~ServiceBrowser();

  private:

    static void avahi_browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol, AvahiBrowserEvent event, const char *name, const char *type, const char *domain, AVAHI_GCC_UNUSED AvahiLookupResultFlags flags, void* userdata);
    void browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol, AvahiBrowserEvent event, const char *name, const char *type, const char *domain, AVAHI_GCC_UNUSED AvahiLookupResultFlags flags);
    static void avahi_resolve_callback(AvahiServiceResolver *r, AvahiIfIndex interface, AvahiProtocol protocol, AvahiResolverEvent event, const char *name, const char *type, const char *domain, const char *host_name, const AvahiAddress *a, uint16_t port, AvahiStringList *txt, AvahiLookupResultFlags flags, void* userdata);
    void resolve_callback(AvahiServiceResolver *r, AvahiIfIndex interface, AvahiProtocol protocol, AvahiResolverEvent event, const char *name, const char *type, const char *domain, const char *host_name, const AvahiAddress *a, uint16_t port, AvahiStringList *txt, AvahiLookupResultFlags flags);

  };
  typedef boost::intrusive_ptr<ServiceBrowser> ServiceBrowserPtr;


  /// Implements service announcement and discovery (via avahi) for vdc host and (if configured) a associated vdsm
  class DiscoveryManager : public P44Obj
  {
    typedef P44Obj inherited;
    friend class ServiceBrowser;

    AvahiSimplePoll *simple_poll;
    AvahiService *service;
    AvahiEntryGroup *dSEntryGroup;
    AvahiServiceBrowser *serviceBrowser;
    AvahiServiceBrowser *debugServiceBrowser;

    // publishing information
    // - common for all services
    string hostname;
    // - for dS service advertising
    VdcHostPtr vdcHost;
    bool noAuto;
    int publishWebPort;
    string publishWebPath;
    int publishSshPort;

    // state of dS advertising / scanning
    enum {
      dm_disabled, // not set up, cannot be started
      dm_setup, // set up, but not yet requested to start an advertisement
      dm_requeststart, // set up and requested to start advertising
      dm_starting, // waiting for pusblishing to happen
      dm_started, // published
    } dmState;

    MLTicket pollTicket; // timer for avahi polling
    MLTicket startupTicket; // timer for startup delay

    // private constructor, use sharedDiscoveryManager() to obtain singleton
    DiscoveryManager();

  public:

    /// get shared instance (singleton)
    static DiscoveryManager &sharedDiscoveryManager();

    virtual ~DiscoveryManager();

    /// start discovery/advertising service
    /// @param aHostname unique hostname to be published
    ErrorPtr start(
      const char *aHostname
    );

    /// stop advertising and scanning service
    void stop();

    /// test if service is up
    /// @return true if service is running
    bool serviceRunning();


    /// advertise p44vdc (or the vdsm, if same platform hosts a auxiliary vdsm and no master vdsm is found)
    /// @note can be called repeatedly to update information
    /// @param aVdcHost the device container to be published
    /// @param aNoAuto if set, the published vdc will not be automatically connected (only when explicitly whitelisted)
    /// @param aWebPort if set, a _http._tcp service will be published as well
    /// @param aWebPath the web path to publish (when aWebPort is set)
    /// @param aSshPort if set, a _ssh._tcp service will be published as well
    void advertiseDS(
      VdcHostPtr aVdcHost,
      bool aNoAuto,
      int aWebPort, const string aWebPath,
      int aSshPort
    );

    /// FIXME: remove, once other vdcs have been updated to the new signature.
    /// backwards compatible method without the aWebPath parameter and still having dummy auxvdms params
    void advertiseDS(
      VdcHostPtr aVdcHost,
      bool aNoAuto,
      int aWebPort,
      int aSshPort,
      DsUidPtr aDUMMYAuxVdsmDsUid, int aDUMMYAuxVdsmPort, bool aDUMMYAuxVdsmRunning, void *aDUMMYAuxVdsmStatusHandler, bool aDUMMYNotAuxiliary
    ) { advertiseDS(aVdcHost, aNoAuto, aWebPort, "", aSshPort); }

    /// Stop advertising DS service(s)
    void stopAdvertisingDS();

    /// Refresh DS service advertising
    void refreshAdvertisingDS();


    /// get browser for third-party service
    ServiceBrowserPtr newServiceBrowser(const char *aServiceType, ServiceDiscoveryCB aServiceDiscoveryCB);


  private:

    void startService();
    void stopService();
    void serviceStarted();
    void restartService();
    bool serviceRunning(AvahiService *aService);
    void startAdvertising(AvahiService *aService);

    void startAdvertisingDS(AvahiService *aService);
    void restartAdvertising();

    // callbacks
    static void avahi_log(AvahiLogLevel level, const char *txt);
    #if USE_AVAHI_CORE
    static void avahi_server_callback(AvahiServer *s, AvahiServerState state, void* userdata);
    static void ds_entry_group_callback(AvahiServer *s, AvahiSEntryGroup *g, AvahiEntryGroupState state, void* userdata);
    void server_callback(AvahiServer *s, AvahiServerState state);
    #else
    static void avahi_client_callback(AvahiClient *c, AvahiClientState state, void* userdata);
    static void ds_entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state, void* userdata);
    void client_callback(AvahiClient *c, AvahiClientState state);
    #endif

    void avahi_poll(MLTimer &aTicket);
    void avahi_ds_entry_group_callback(AvahiService *aService, AvahiEntryGroup *g, AvahiEntryGroupState state);

    static void avahi_debug_browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol, AvahiBrowserEvent event, const char *name, const char *type, const char *domain, AVAHI_GCC_UNUSED AvahiLookupResultFlags flags, void* userdata);
    void debug_browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol, AvahiBrowserEvent event, const char *name, const char *type, const char *domain, AVAHI_GCC_UNUSED AvahiLookupResultFlags flags);

  };

  typedef boost::intrusive_ptr<DiscoveryManager> DiscoveryManagerPtr;


} // namespace p44

#endif // !DISABLE_DISCOVERY
#endif // __p44vdc__discovery__
