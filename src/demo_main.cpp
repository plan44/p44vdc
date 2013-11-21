/*
 * demo_main.cpp
 * vdcd
 *
 *      Author: Lukas Zeller / luz@plan44.ch
 *   Copyright: 2012-2013 by plan44.ch/luz
 */


#include "application.hpp"

#include "device.hpp"
#include "devicecontainer.hpp"

#include "demodevicecontainer.hpp"
#include "upnpdevicecontainer.hpp"

#define DEFAULT_USE_MODERN_DSIDS 0 // 0: no, 1: yes

#define DEFAULT_VDSMSERVICE "8440"
#define DEFAULT_DBDIR "/tmp"

#define DEFAULT_LOGLEVEL LOG_NOTICE

#define MAINLOOP_CYCLE_TIME_uS 20000 // 20mS

using namespace p44;


class DemoVdc : public CmdLineApp
{
  typedef CmdLineApp inherited;

  // the device container
  DeviceContainer deviceContainer;

public:

  DemoVdc()
  {
  }

  virtual int main(int argc, char **argv)
  {
    const char *usageText =
      "Usage: %1$s [options]\n";
    const CmdLineOptionDescriptor options[] = {
      { 0  , "modernids",     true,  "enabled;1=use modern (GS1/UUID based) 34 hex dsUIDs, 0=classic 24 hex dsids" },
      { 'C', "vdsmport",      true,  "port;port number/service name for vdSM to connect to (default=" DEFAULT_VDSMSERVICE ")" },
      { 'i', "vdsmnonlocal",  false, "allow vdSM connections from non-local clients" },
      { 'l', "loglevel",      true,  "level;set max level of log message detail to show on stdout" },
      { 0  , "errlevel",      true,  "level;set max level for log messages to go to stderr as well" },
      { 0  , "dontlogerrors", false, "don't duplicate error messages (see --errlevel) on stdout" },
      { 's', "sqlitedir",     true,  "dirpath;set SQLite DB directory (default = " DEFAULT_DBDIR ")" },
      { 'h', "help",          false, "show this text" },
      { 0, NULL } // list terminator
    };

    // parse the command line, exits when syntax errors occur
    setCommandDescriptors(usageText, options);
    parseCommandLine(argc, argv);

    // log level?
    int loglevel = DEFAULT_LOGLEVEL;
    getIntOption("loglevel", loglevel);
    SETLOGLEVEL(loglevel);
    int errlevel = LOG_ERR;
    getIntOption("errlevel", errlevel);
    SETERRLEVEL(errlevel, !getOption("dontlogerrors"));

    // Init the device container root object
    // - set DB dir
    const char *dbdir = DEFAULT_DBDIR;
    getStringOption("sqlitedir", dbdir);
    deviceContainer.setPersistentDataDir(dbdir);
    // - set dSUID mode
    int modernids = DEFAULT_USE_MODERN_DSIDS;
    getIntOption("modernids", modernids);
    deviceContainer.setIdMode(modernids!=0);
    // - set up server for vdSM to connect to
    const char *vdsmport = (char *) DEFAULT_VDSMSERVICE;
    getStringOption("vdsmport", vdsmport);
    deviceContainer.vdcApiServer.setConnectionParams(NULL, vdsmport, SOCK_STREAM, AF_INET);
    deviceContainer.vdcApiServer.setAllowNonlocalConnections(getOption("vdsmnonlocal"));

    // Now add device class(es)
    // - the demo device (dimmer value output to console as bar of hashes ######) class
    DemoDeviceContainerPtr demoDeviceContainer = DemoDeviceContainerPtr(new DemoDeviceContainer(1));
    deviceContainer.addDeviceClassContainer(demoDeviceContainer);
    // - the UPnP skeleton device from the developer days 2013 hackaton
    UpnpDeviceContainerPtr upnpDeviceContainer = UpnpDeviceContainerPtr(new UpnpDeviceContainer(1));
    deviceContainer.addDeviceClassContainer(upnpDeviceContainer);

    // now start running the mainloop
    return run();
  }

  virtual void initialize()
  {
    // - initialize the device container
    deviceContainer.initialize(boost::bind(&DemoVdc::initialized, this, _1), false); // no factory reset
  }


  virtual void initialized(ErrorPtr aError)
  {
    if (!Error::isOK(aError)) {
      // cannot initialize, this is a fatal error
      LOG(LOG_ERR, "Cannot initialize device container - fatal error\n");
      terminateApp(EXIT_FAILURE);
    }
    else {
      // init ok, collect devices
      deviceContainer.collectDevices(boost::bind(&DemoVdc::devicesCollected, this, _1), false, false); // no forced full scan (only if needed)
    }
  }

  virtual void devicesCollected(ErrorPtr aError)
  {
    if (Error::isOK(aError)) {
      LOG(LOG_INFO, deviceContainer.description().c_str());
    }
    else {
      LOG(LOG_ERR, "Cannot collect devices - fatal error\n");
      terminateApp(EXIT_FAILURE);
    }
  }

};


int main(int argc, char **argv)
{
  // prevent debug output before application.main scans command line
  SETLOGLEVEL(LOG_EMERG);
  SETERRLEVEL(LOG_EMERG, false); // messages, if any, go to stderr
  // create the mainloop
  SyncIOMainLoop::currentMainLoop().setLoopCycleTime(MAINLOOP_CYCLE_TIME_uS);
  // create app with current mainloop
  static DemoVdc application;
  // pass control
  return application.main(argc, argv);
}