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

  class ZoneList;
  class VdcHost;
  class LocalController;

  class ZoneDescriptor;
  typedef boost::intrusive_ptr<ZoneDescriptor> ZoneDescriptorPtr;

  class SceneDescriptor;
  typedef boost::intrusive_ptr<SceneDescriptor> SceneDescriptorPtr;
  typedef vector<SceneDescriptorPtr> ScenesVector;

  class SceneIdentifier;
  typedef vector<SceneIdentifier> SceneIdsVector;

  /// Scene kind flags
  enum {
    // scope
    scene_global = 0x01, ///< set for global scenes
    scene_room = 0x02, ///< set for room scenes
    scene_area = 0x04, ///< set for area scenes (together with scene_room)
    // type
    scene_preset = 0x10, ///< preset
    scene_off = 0x20, ///< off (together with scene_preset)
    scene_extended = 0x40, ///< extended
  };
  typedef uint8_t SceneKind;

  /// Scene kind
  typedef struct {
    SceneNo no;
    SceneKind kind;
    const char *actionName;
  } SceneKindDescriptor;


  /// Group kind flags
  enum {
    group_standard = 0x01, ///< standard group with direct scene calls
    group_application = 0x02, ///< joker
    group_controller = 0x04, ///< group (and scene calls) are managed by a contoller (such as heating, etc.)
    group_global = 0x08, ///< global group like security and access
    group_domain = 0x10, ///< group is a control domain (building, flat, ...)
  };
  typedef uint8_t GroupKind;

  /// Global group info
  typedef struct {
    DsGroup no;
    GroupKind kind;
    const char *name;
  } GroupDescriptor;


  /// zone state
  class ZoneState
  {
  public:

    // global state
    SceneNo lastGlobalScene; ///< last global scene called

    // Light state
    VdcDimMode lastDim; ///< last dimming direction in this room
    SceneNo lastLightScene; ///< last light scene called
    bool lightOn[5]; ///< set if light is on in this zone and area

    ZoneState();
  };


  /// zone descriptor
  /// holds information about a zone
  class ZoneDescriptor : public PropertyContainer, public PersistentParams
  {
    typedef PropertyContainer inherited;
    typedef PersistentParams inheritedParams;
    friend class ZoneList;
    friend class LocalController;

    DsZoneID zoneID; ///< global dS zone ID, zero = "all" zone
    string zoneName; ///< the name of the zone

    DeviceVector devices; ///< devices in this zone

    ZoneState zoneState; ///< current state of the zone

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

    /// get the scenes relevant for this zone
    /// @param aForGroup the group for the scene
    /// @param aRequiredKinds scene must have at least these kind flags
    /// @param aForbiddenKinds scene may not have these kind flags
    /// @return a vector of SceneIdentifier objects
    SceneIdsVector getZoneScenes(DsGroup aForGroup, SceneKind aRequiredKinds, SceneKind aForbiddenKinds);

    /// get the groups relevant for this zone
    /// @param aStandardOnly if set, only groups with standard room scenes will be reported for non-global zones
    DsGroupMask getZoneGroups(bool aStandardOnly);

    /// @return number of devices in this zone
    size_t devicesInZone() const;

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyAccessMode aMode, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual void prepareAccess(PropertyAccessMode aMode, PropertyDescriptorPtr aPropertyDescriptor, StatusCB aPreparedCB) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;
    virtual void finishAccess(PropertyAccessMode aMode, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

    // persistence implementation
    virtual const char *tableName() P44_OVERRIDE;
    virtual size_t numKeyDefs() P44_OVERRIDE;
    virtual const FieldDefinition *getKeyDef(size_t aIndex) P44_OVERRIDE;
    virtual size_t numFieldDefs() P44_OVERRIDE;
    virtual const FieldDefinition *getFieldDef(size_t aIndex) P44_OVERRIDE;
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP) P44_OVERRIDE;
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags) P44_OVERRIDE;

  };


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


  /// scene identifier
  /// lightweight, usually temporary identifier for a scene that *could* be used
  class SceneIdentifier
  {
  public:

    // identification
    SceneNo sceneNo;
    DsZoneID zoneID;
    DsGroup group;
    // name
    const SceneKindDescriptor* sceneKindP; ///< the scene kind
    string name;

    SceneIdentifier();
    SceneIdentifier(const SceneKindDescriptor &aSceneKind, DsZoneID aZone, DsGroup aGroup);
    SceneIdentifier(SceneNo aNo, DsZoneID aZone, DsGroup aGroup);
    SceneIdentifier(const string aStringId);

    /// derive scene kind from scene number
    /// @return true if this is a valid scene and scene kind could be assigned
    bool deriveSceneKind();

    /// get the scene's ID
    /// @return a string ID uniquely defining this scene in this localcontroller (zone, group, sceneNo)
    string stringId() const;

    /// get the scene's name
    string getName() const;

    /// get the action name (describing what this scene kind is form, such as "presetXY", "off" etc.)
    string getActionName() const;

    /// get the scene's kind flags
    SceneKind getKindFlags() { return sceneKindP ? sceneKindP->kind : 0; };

  };


  /// scene descriptor
  /// holds information about scene (global scope)
  class SceneDescriptor : public PropertyContainer, public PersistentParams
  {
    typedef PropertyContainer inherited;
    typedef PersistentParams inheritedParams;
    friend class SceneList;

    SceneIdentifier sceneId; ///< the scene identification

  public:

    SceneDescriptor();
    virtual ~SceneDescriptor();

    /// get the name
    /// @return name of this scene (if no name was set, the default name will be returned)
    string getSceneName() const { return sceneId.getName(); };

    /// get the action name
    /// @return action name of this scene
    string getActionName() const { return sceneId.getActionName(); };

    /// get the dS scene number
    /// @return scene number (INVALID_SCENE_NO in case of invalid scene (no kind found)
    int getSceneNo() const { return sceneId.sceneNo; };

    /// get the Zone ID
    /// @return zone ID (0 for global scenes)
    DsZoneID getZoneID() const { return sceneId.zoneID; };

    /// get the dS group number
    /// @return group number
    DsGroup getGroup() const { return sceneId.group; };

    /// get the scene's ID
    /// @return a string ID uniquely defining this scene in this localcontroller (zone, group, sceneNo)
    string getStringID() const { return sceneId.stringId(); }

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


  /// scene list
  /// list of scenes defined by user
  class SceneList : public PropertyContainer
  {
    typedef PropertyContainer inherited;

  public:

    ScenesVector scenes;

    /// load zones
    ErrorPtr load();

    /// save zones
    ErrorPtr save();

    /// get scene by identifier
    SceneDescriptorPtr getScene(const SceneIdentifier &aSceneId, bool aCreateNewIfNotExisting = false, size_t *aSceneIndexP = NULL);

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_FINAL P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyAccessMode aMode, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_FINAL P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<SceneList> SceneListPtr;


  /// local controller
  /// manages local zones, scenes, triggers
  class LocalController : public PropertyContainer
  {
    typedef PropertyContainer inherited;
    friend class ZoneDescriptor;

    VdcHost &vdcHost; ///< local reference to vdc host

    ZoneList localZones; ///< the locally used/defined zones
    SceneList localScenes; ///< the locally defined scenes

  public:

    LocalController(VdcHost &aVdcHost);
    virtual ~LocalController();

    /// @return local controller, will create one if not existing
    static LocalControllerPtr sharedLocalController();

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

    /// device changes zone
    /// @param aDevice that will change zone or has changed zone
    /// @param aFromZone current zone
    /// @param aToZone new zone
    void deviceChangesZone(DevicePtr aDevice, DsZoneID aFromZone, DsZoneID aToZone);

    /// @return total number of devices
    size_t totalDevices() const;

    /// vdchost has started running normally
    void startRunning();

    /// load settings
    ErrorPtr load();

    /// save settings
    ErrorPtr save();

    /// @}

    /// get info (name, kind) about a group
    static const GroupDescriptor* groupInfo(DsGroup aGroup);

    // localcontroller specific method handling
    bool handleLocalControllerMethod(ErrorPtr &aError, VdcApiRequestPtr aRequest,  const string &aMethod, ApiValuePtr aParams);

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_FINAL P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_FINAL P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;

  };


} // namespace p44

#endif // ENABLE_LOCALCONTROLLER
#endif // __p44vdc__localcontroller__
