//
//  Copyright (c) 2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__localcontroller__
#define __p44vdc__localcontroller__

#include "device.hpp"
#include "vdchost.hpp"


#if ENABLE_LOCALCONTROLLER

using namespace std;

namespace p44 {

  class ZoneDescriptor;
  class ZoneList;
  class VdcHost;


  /// zone descriptor
  /// holds information about a zone
  class ZoneDescriptor : public PropertyContainer, public PersistentParams
  {
    typedef PropertyContainer inherited;
    typedef PersistentParams inheritedParams;
    friend class ZoneList;

    DsZoneID zoneID; ///< global dS zone ID, zero = "all" zone
    string zoneName; ///< the name of the zone
    uint32_t deviceCount; ///< number of devices using this zone

  public:

    ZoneDescriptor();
    virtual ~ZoneDescriptor();

    /// get the name
    /// @return name of this zone
    string getName() const { return zoneName; };

    /// get the zoneID
    /// @return ID of this zone
    int getZoneId() const { return zoneID; };

    /// register as in-use or non-in-use-any-more by a device
    void usedByDevice(DevicePtr aDevice, bool aInUse);

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

    // persistence implementation
    virtual const char *tableName() P44_OVERRIDE;
    virtual size_t numKeyDefs() P44_OVERRIDE;
    virtual const FieldDefinition *getKeyDef(size_t aIndex) P44_OVERRIDE;
    virtual size_t numFieldDefs() P44_OVERRIDE;
    virtual const FieldDefinition *getFieldDef(size_t aIndex) P44_OVERRIDE;
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP) P44_OVERRIDE;
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags) P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<ZoneDescriptor> ZoneDescriptorPtr;


  /// zone list
  /// list of known zones
  class ZoneList : public PropertyContainer
  {
    typedef PropertyContainer inherited;

  public:

    typedef vector<ZoneDescriptorPtr> ZonesVector;

    ZonesVector zones;

    /// load zones
    ErrorPtr load();
    
    /// save zones
    ErrorPtr save();

    /// get zone by ID
    /// @param aZoneId zone to look up
    /// @param aCreateNewIfNotExisting if true, a zone is created on the fly when none exists for the given ID
    /// @return zone or NULL if zoneID is not known (and none created)
    ZoneDescriptorPtr getZoneById(DsZoneID aZoneId, bool aCreateNewIfNotExisting = false);

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_FINAL P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyAccessMode aMode, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_FINAL P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<ZoneList> ZoneListPtr;



  /// local controller
  /// manages local zones, scenes, triggers
  class LocalController : public PropertyContainer
  {
    typedef PropertyContainer inherited;

    VdcHost &vdcHost; ///< local reference to vdc host

    ZoneList localZones; ///< the local zones


  public:

    LocalController(VdcHost &aVdcHost);
    virtual ~LocalController();

    /// @name following vdchost activity
    /// @{

    /// called when vdc host event occurs
    /// @param aActivity the activity that occurred at the vdc host level
    void processGlobalEvent(VdchostEvent aActivity);

    /// called when button is clicked (including long click and release events)
    /// @param aButtonBehaviour the button behaviour from which the click originates
    /// @param aClickType the click type
    /// @return true if click could be handled
    bool processButtonClick(ButtonBehaviour &aButtonBehaviour, DsClickType aClickType);

    /// device was added
    /// @param aDevice device being added
    void deviceAdded(DevicePtr aDevice);

    /// device was removed
    /// @param aDevice that will be removed
    void deviceRemoved(DevicePtr aDevice);

    /// vdchost has started running normally
    void startRunning();

    /// load settings
    ErrorPtr load();

    /// save settings
    ErrorPtr save();

    /// @}



  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_FINAL P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_FINAL P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<LocalController> LocalControllerPtr;






} // namespace p44

#endif // ENABLE_LOCALCONTROLLER
#endif // __p44vdc__localcontroller__
