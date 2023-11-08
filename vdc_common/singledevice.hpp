//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2016-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__singledevice__
#define __p44vdc__singledevice__

#include "device.hpp"
#include "outputbehaviour.hpp"

#include "jsonobject.hpp"
#include "valueunits.hpp"
#include "valuedescriptor.hpp"

using namespace std;

namespace p44 {

  class SingleDevice;
  class DeviceAction;
  class DeviceState;
  class DeviceEvent;


  /// DeviceAction models a command with parameters that can be executed on the device
  /// customActions (and as a read-only special case, standardActions) are always based
  /// on a DeviceAction plus some parameter values overriding the DeviceAction's defaults.
  class DeviceAction : public PropertyContainer
  {
    typedef PropertyContainer inherited;

    friend class DeviceActions;
    friend class DynamicDeviceActions;
    friend class ActionMacro;
    friend class CustomAction;
    friend class StandardAction;

  protected:

    string actionId; ///< id of the action (key in the container object)
    string actionDescription; ///< a descriptive string for the action (for logs and debugging)
    string actionTitle; ///< for dynamic actions: the title of the action in user's language (assigned by the user by a UI of the device itself, immutable from dS)
    string actionCategory;
    ValueListPtr actionParams; ///< the parameter descriptions of this action

    SingleDevice *singleDeviceP; ///< the single device this action belongs to

  public:

    /// create the device action
    /// @param aSingleDevice the single device this action belongs to
    /// @param aId the ID of the action (key in the container)
    /// @param aDescription a description string for the action (for log files primarily)
    /// @param aTitle a user language description/name for the action, usually set by the user via direct device interaction (UI, apps, ...)
    DeviceAction(SingleDevice &aSingleDevice, const string aId, const string aDescription, const string aTitle = "", const string aCategory = "");

    /// get id
    string getId() { return actionId; };

    /// get action title
    string getActionTitle() { return actionTitle; };

    ValueListPtr getActionParams() { return actionParams; }

    /// add parameter
    /// @param aValueDesc a value descriptor object.
    /// @param aMandatory if set, parameter must be explicitly specified
    virtual void addParameter(ValueDescriptorPtr aValueDesc, bool aMandatory = false);

    /// call the action
    /// @param aParams an ApiValue of type apivalue_object, expected to
    ///   contain parameters matching the actual parameters available in the action
    /// @param aCompletedCB must be called when call has completed
    /// @note this public method will verify that the parameter name and values match the action's parameter description
    ///   and prevent calling subclass' performCall() when parameters are not ok.
    void call(ApiValuePtr aParams, StatusCB aCompletedCB);

    /// Allow non-conforming parameters to be treated as NULL (and used as such if NULL is allowed for that param)
    /// @return true if non-conforming params should be converted to NULL internally
    virtual bool nonConformingAsNull() { return false; }; // off by default

  protected:

    /// action implementation.
    /// @param aParams an ApiValue of type apivalue_object, containing ALL parameters in the description (in internal
    ///   format, which means enums as unsigned ints, not text). These are obtained either from defaults
    ///   or from overriding values specified to the call() method.
    /// @param aCompletedCB must be called when call has completed
    /// @note this method is usually overridden by specific device subclasses actually implementing functionality
    /// @note base class just returns 
    virtual void performCall(ApiValuePtr aParams, StatusCB aCompletedCB);

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;
  };
  typedef boost::intrusive_ptr<DeviceAction> DeviceActionPtr;


  /// DeviceActions is the container of all actions that are statically and natively
  /// (i.e. always, unless vdc implementation or device itself changes) available in a device.
  /// The set of DeviceActions cannot change during a vDC API session.
  class DeviceActions : public PropertyContainer
  {
    typedef PropertyContainer inherited;
    friend class SingleDevice;

  protected:

    typedef vector<DeviceActionPtr> ActionsVector;

    ActionsVector deviceActions;

  public:

    /// call an action
    /// @param aActionId name of the action to call
    /// @param aParams an ApiValue of type apivalue_object, expected to
    ///   contain parameters matching the actual parameters available in the action
    /// @param aCompletedCB will be called when call has completed
    /// @return true if action exists and was executed (or failed with action-level error), false if this action does not exist
    /// @note this public method will verify that the parameter name and values match the action's parameter description
    ///   and prevent calling subclass' performCall() when parameters are not ok.
    bool call(const string aActionId, ApiValuePtr aParams, StatusCB aCompletedCB);

    /// get action
    /// @param aActionId id of the action to get
    DeviceActionPtr getAction(const string aActionId);

    /// add an action (at device setup time only)
    /// @param aAction the action
    void addAction(DeviceActionPtr aAction);

    /// @param aHashedString append model relevant strings to this value for creating modelUID() hash
    void addToModelUIDHash(string &aHashedString);

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE P44_FINAL;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_OVERRIDE P44_FINAL;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE P44_FINAL;
    
  };
  typedef boost::intrusive_ptr<DeviceActions> DeviceActionsPtr;


  /// DynamicDeviceActions is the container of actions that are currently available in
  /// the device, but might change because they are based on customer changeable
  /// information such as purchase of vendor-provided recipe books, subscribed video services
  /// available radio stations, playlists etc.
  /// The set of DeviceActions can change during a vDC API session - if this happens,
  /// Changes will be alerted upstream via vDC API push notification.
  class DynamicDeviceActions P44_FINAL : public DeviceActions
  {
    typedef DeviceActions inherited;

  public:

    /// compare existing actions with the list, get actions to remove and actions to add or change
    /// @note action has changed when action title changed
    /// @param aActions current set of actions
    void updateDynamicActions(ActionsVector &aActions);

    /// add or update a dynamic action.
    /// @note If device is announced with a vDC API client (vdSM), the changed action description will be pushed)
    /// @param aAction the action to add (if its actionId is new) or update (if its actionId already exists)
    void addOrUpdateDynamicAction(DeviceActionPtr aAction);

    /// remove a dynamic device action.
    /// @note If device is announced with a vDC API client (vdSM), the removal will be pushed (empty action description)
    /// @param aAction the action
    void removeDynamicAction(DeviceActionPtr aAction);

    /// @param aHashedString append model relevant strings to this value for creating modelUID() hash
    virtual void addToModelUIDHash(string &aHashedString);

  private:

    static bool compareById(DeviceActionPtr aActionL, DeviceActionPtr aActionR) { return (aActionL->getId() < aActionR->getId()); }
    static bool compareByIdAndTitle(DeviceActionPtr aActionL, DeviceActionPtr aActionR) {
      return (aActionL->getId() == aActionR->getId()) ? (aActionL->getActionTitle() < aActionR->getActionTitle()) : (aActionL->getId() < aActionR->getId()); }

    void removeDynamicActionsExcept(ActionsVector &aActions);
    void addOrUpdateDynamicActions(ActionsVector &aActions);

    bool removeActionInternal(DeviceActionPtr aAction);
    bool pushActionChange(DeviceActionPtr aAction, bool aRemoved);

  };
  typedef boost::intrusive_ptr<DynamicDeviceActions> DynamicDeviceActionsPtr;


  /// ActionMacro is a generic "Macro" which refers to a DeviceAction and
  /// optionally carries some parameter values to apply to the DeviceAction when
  /// the ActionMacro is called.
  class ActionMacro : public PropertyContainer
  {
  protected:

    typedef PropertyContainer inheritedProps;

    SingleDevice &singleDevice; ///< the single device this custom action belongs to

    string actionId; ///< the ID of the action (key in the container)
    string actionTitle; ///< the user-faced name of the action
    uint32_t flags; ///< flags

    /// references the DeviceAction on which this custom action is based
    DeviceActionPtr action;

    /// the parameters stored in this custom action.
    /// @note these parameters will be used at call() unless overridden by parameters explicitly passed to call().
    ///   call() will then pass these to action->call() for execution of a standard action with custom parameters.
    ApiValuePtr storedParams;

    /// if true, this custom action belongs to the predefined set of actions for this device
    bool predefined;

  public:

    /// create the action macro
    /// @param aSingleDevice the single device this custom action belongs to
    ActionMacro(SingleDevice &aSingleDevice);

    /// configure the macro
    /// @param aDeviceActionId the ID of the device action to call when the macro is called
    /// @param aParams the parameters (optional) to be passed to the actione when the macro is called
    ErrorPtr configureMacro(const string aDeviceActionId, JsonObjectPtr aParams);

    /// call the custom action
    /// @param aParams an ApiValue of type apivalue_object, may be used to override stored parameters
    /// @param aCompletedCB must be called when call has completed
    void call(ApiValuePtr aParams, StatusCB aCompletedCB);

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

    // parameter validation
    ErrorPtr validateParams(ApiValuePtr aParams, ApiValuePtr aValidatedParams, bool aSkipInvalid);

  };


  /// CustomAction is a ActionMacro that can be created/modified/deleted via the
  /// vDC API and eventually by the user
  class CustomAction P44_FINAL : public ActionMacro, public PersistentParams
  {
    typedef PersistentParams inheritedParams;
    typedef ActionMacro inherited;

    friend class CustomActions;

  public:

    /// create the custom action
    /// @param aSingleDevice the single device this custom action belongs to
    CustomAction(SingleDevice &aSingleDevice);

  protected:

    // persistence implementation
    virtual const char *tableName() P44_OVERRIDE;
    virtual size_t numKeyDefs() P44_OVERRIDE;
    virtual const FieldDefinition *getKeyDef(size_t aIndex) P44_OVERRIDE;
    virtual size_t numFieldDefs() P44_OVERRIDE;
    virtual const FieldDefinition *getFieldDef(size_t aIndex) P44_OVERRIDE;
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP) P44_OVERRIDE;
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags) P44_OVERRIDE;

    // property access implementation
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<CustomAction> CustomActionPtr;


  /// CustomActions is the container of vDC-API (=user) defined ActionMacros
  /// which are stored persistently for this device in the database, and
  /// can be created, modified and deleted via the vDC API.
  class CustomActions P44_FINAL : public PropertyContainer
  {
    typedef PropertyContainer inherited;

    typedef vector<CustomActionPtr> CustomActionsVector;

    CustomActionsVector customActions;

    SingleDevice &singleDevice; ///< the single device the custom actions belong to

  public:

    CustomActions(SingleDevice &aSingleDevice) : singleDevice(aSingleDevice) { };

    /// call a custom action
    /// @param aActionId name of the action to call
    /// @param aParams an ApiValue of type apivalue_object, can be empty or
    ///   contain parameters overriding those stored in the custom action
    /// @param aCompletedCB will be called when call has completed
    /// @return true if action exists and was executed (or failed with action-level error), false if this action does not exist
    bool call(const string aActionId, ApiValuePtr aParams, StatusCB aCompletedCB);

    /// load custom actions
    ErrorPtr load();
    /// save custom actions
    ErrorPtr save();
    /// delete custom actions
    ErrorPtr forget();
    /// check if any settings are dirty
    bool isDirty();
    /// make all settings clean (not to be saved to DB)
    void markClean();

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyAccessMode aMode, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<CustomActions> CustomActionsPtr;



  /// StandardAction is a ActionMacro that can only be read and called via the vDC API,
  /// but cannot be modified. The set of available standard action is defined via static
  /// vDC configuration (in code, or by reading config files)
  class StandardAction P44_FINAL : public ActionMacro
  {
    typedef ActionMacro inherited;
    friend class StandardActions;

  public:

    /// create the standard action
    /// @param aSingleDevice the single device this action belongs to
    /// @param aId the ID of the action (key in the container)
    /// @param aTitle a description/name for the action
    StandardAction(SingleDevice &aSingleDevice, const string aId, const string aTitle = "");

    void updateParameterValue(const string& aName, JsonObjectPtr aValue);

  };
  typedef boost::intrusive_ptr<StandardAction> StandardActionPtr;


  /// StandardActions is the container of statically (config file) defined ActionMacros
  /// which can be inspected and called, but are not writable via the vDC API.
  class StandardActions P44_FINAL : public PropertyContainer
  {
    typedef PropertyContainer inherited;

    typedef vector<StandardActionPtr> StandardActionsVector;

    StandardActionsVector standardActions;

    SingleDevice &singleDevice; ///< the single device the standard actions belong to

  public:

    StandardActions(SingleDevice &aSingleDevice) : singleDevice(aSingleDevice) { };

    /// call a standard action
    /// @param aActionId name of the action to call
    /// @param aParams an ApiValue of type apivalue_object, can be empty or
    ///   contain parameters overriding those stored in the custom action
    /// @param aCompletedCB will be called when call has completed
    /// @return true if action exists and was executed (or failed with action-level error), false if this action does not exist
    bool call(const string aActionId, ApiValuePtr aParams, StatusCB aCompletedCB);

    /// add a standard action
    /// @param aAction the standard action to add
    void addStandardAction(StandardActionPtr aAction);

    /// @param aHashedString append model relevant strings to this value for creating modelUID() hash
    void addToModelUIDHash(string &aHashedString);

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<StandardActions> StandardActionsPtr;




  class DeviceStateParams : public ValueList
  {
    typedef ValueList inherited;

  protected:

    // overrides to present param values directly instead of delegating to ValueDescriptor
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_FINAL P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_FINAL P44_OVERRIDE;

  };



  typedef boost::intrusive_ptr<DeviceState> DeviceStatePtr;
  typedef boost::intrusive_ptr<DeviceEvent> DeviceEventPtr;
  typedef list<DeviceEventPtr> DeviceEventsList;

  /// handler that will be called before pushing a state change, allows adding events to the push message
  /// @param aChangedState the DeviceState which has changed.
  typedef boost::function<void (DeviceStatePtr aChangedState, DeviceEventsList &aEventsToPush)> DeviceStateWillPushCB;

  class DeviceState : public PropertyContainer
  {
    typedef PropertyContainer inherited;

    friend class DeviceStates;

    string stateId; ///< the id (key in the container) of this state
    ValueDescriptorPtr stateDescriptor; ///< the value (descriptor) for the state itself
    DeviceStateWillPushCB willPushHandler; ///< called when state is about to get pushed to get associated events
    string stateDescription; ///< a text description for the state, for logs and simple UI purposes
    MLMicroSeconds updateInterval; ///< approximate time interval for state updates
    MLMicroSeconds lastPush; ///< when the state was last pushed

  protected:

    SingleDevice *singleDeviceP; ///< the single device this state belongs to

  public:

    /// create the state
    /// @param aSingleDevice the single device this state belongs to
    /// @param aStateId the id (key in the container) of the state.
    /// @param aDescription a description string for the state.
    /// @param aStateDescriptor value descriptor for the state
    /// @param aWillPushHandler will be called just before pushing the state, allows handler to add events
    DeviceState(SingleDevice &aSingleDevice, const string aStateId, const string aDescription, ValueDescriptorPtr aStateDescriptor, DeviceStateWillPushCB aWillPushHandler);

    /// access value
    ValueDescriptorPtr value() { return stateDescriptor; };

    /// get id
    string getId() { return stateId; };

    /// push the state via pushNotification
    /// @return true if push could actually be delivered (i.e. a vDC API client is connected and receiving pushes)
    /// @note will call the willPushHandler to collect possibly associated events
    bool push();

    /// push the state via pushNotification along with an event
    /// @param aEvent a single event to push along with the state
    /// @return true if push could actually be delivered (i.e. a vDC API client is connected and receiving pushes)
    /// @note will also call the willPushHandler to collect possibly more associated events
    bool pushWithEvent(DeviceEventPtr aEvent);

    /// push the state via pushNotification along with multiple events
    /// @param aEventList a list of events to send along with the pushed state
    /// @return true if push could actually be delivered (i.e. a vDC API client is connected and receiving pushes)
    /// @note will also call the willPushHandler to collect possibly more associated events
    bool pushWithEvents(DeviceEventsList aEventList);

    /// set update interval (descriptive, no functionality at the DeviceState level)
    /// @aUpdateInterval how often the state is updated (approximately)
    void setUpdateInterval(MLMicroSeconds aUpdateInterval) { updateInterval = aUpdateInterval; };

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

  };



  class DeviceStates P44_FINAL : public PropertyContainer
  {
    typedef PropertyContainer inherited;

    typedef vector<DeviceStatePtr> StatesVector;

    StatesVector deviceStates;

  public:

    /// add a state (at device setup time only)
    /// @param aState the action
    void addState(DeviceStatePtr aState);

    /// get state (for applying updates)
    /// @param aStateId id of the state to get
    DeviceStatePtr getState(const string aStateId);

    /// @param aHashedString append model relevant strings to this value for creating modelUID() hash
    void addToModelUIDHash(string &aHashedString);

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<DeviceStates> DeviceStatesPtr;



  class DeviceEvent : public PropertyContainer
  {
    typedef PropertyContainer inherited;

    friend class DeviceState;
    friend class DeviceEvents;

    string eventId; ///< the id (key in the container) of this event
    string eventDescription; ///< a text description for the event, for logs and simple UI purposes

  protected:

    SingleDevice *singleDeviceP; ///< the single device this event belongs to

  public:

    /// create the event
    /// @param aSingleDevice the single device this event belongs to
    /// @param aEventId the id (key in the container) of the event.
    /// @param aDescription a description string for the event.
    DeviceEvent(SingleDevice &aSingleDevice, const string aEventId, const string aDescription);

    /// get id
    string getId() { return eventId; };

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

  };



  class DeviceEvents P44_FINAL : public PropertyContainer
  {
    typedef PropertyContainer inherited;

    typedef vector<DeviceEventPtr> EventsVector;

    EventsVector deviceEvents;

  protected:

    SingleDevice *singleDeviceP; ///< the single device the events belong to

  public:

    DeviceEvents(SingleDevice &aSingleDevice) : singleDeviceP(&aSingleDevice) { };

    /// add a event (at device setup time only)
    /// @param aEvent the event
    void addEvent(DeviceEventPtr aEvent);

    /// get event (for triggering/sending it via push)
    /// @param aEventId id of the state to get
    DeviceEventPtr getEvent(const string aEventId);

    /// push the event via pushNotification
    /// @param aEventId the ID of the event to push
    /// @return true if push could actually be delivered (i.e. a vDC API client is connected and receiving pushes)
    bool pushEvent(const string aEventId);

    /// push the event via pushNotification
    /// @param aEvent a single event to push along with the state
    /// @return true if push could actually be delivered (i.e. a vDC API client is connected and receiving pushes)
    bool pushEvent(DeviceEventPtr aEvent);

    /// push the event via pushNotification
    /// @param aEventList a list of events to send along with the pushed state
    /// @return true if push could actually be delivered (i.e. a vDC API client is connected and receiving pushes)
    bool pushEvents(DeviceEventsList aEventList);


    /// @param aHashedString append model relevant strings to this value for creating modelUID() hash
    void addToModelUIDHash(string &aHashedString);

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    
  };
  typedef boost::intrusive_ptr<DeviceEvents> DeviceEventsPtr;





  /// handler that will be called when writing a property values via the API causes its value to change
  /// @param aChangedProperty the ValueDescriptor which has changed.
  typedef boost::function<void (ValueDescriptorPtr aChangedProperty)> PropertyChangedCB;


  /// handler that will be called when writing a property values via the API causes its value to change
  /// @param aChangedProperty the ValueDescriptor which has changed.
  typedef boost::function<void (ValueDescriptorPtr aChangedProperty, StatusCB aFetchDoneCB)> PropertyFetchedCB;


  /// class representing the device-specific properties
  class DeviceProperties P44_FINAL : public ValueList
  {
    typedef ValueList inherited;

    PropertyChangedCB propertyChangeHandler; ///< called when property has been changed via the API
    PropertyFetchedCB propertyFetchHandler; ///< called when property is requested via API that has the needsFetch flag set

  protected:

    SingleDevice *singleDeviceP; ///< the single device the events belong to

  public:

    DeviceProperties(SingleDevice &aSingleDevice) : singleDeviceP(&aSingleDevice), propertyChangeHandler(NoOP) {};

    /// set a property change handler
    void setPropertyChangedHandler(PropertyChangedCB aPropertyChangedHandler) { propertyChangeHandler = aPropertyChangedHandler; };

    /// set a property prepare handler
    void setPropertyFetchHandler(PropertyFetchedCB aPropertyFetchHandler) { propertyFetchHandler = aPropertyFetchHandler; };

    /// add a property (at device setup time only)
    /// @param aPropertyDesc value descriptor describing the property
    /// @param aReadOnly if set, the property cannot be written from the API and will report a "readonly:true" field in the description
    /// @param aNeedsFetch if set, a fetch operation must occor before the property can be read
    /// @param aNullAllowed if set, null can be written to the property 
    void addProperty(ValueDescriptorPtr aPropertyDesc, bool aReadOnly = false, bool aNeedsFetch = false, bool aNullAllowed = false);

    /// get property (for internally accessing/changing it)
    /// @param aPropertyId name of the property to get
    ValueDescriptorPtr getProperty(const string aPropertyId);

    /// push the event via pushNotification
    /// @param aPropertyDesc the property to push
    /// @return true if push could actually be delivered (i.e. a vDC API client is connected and receiving pushes)
    bool pushProperty(ValueDescriptorPtr aPropertyDesc);

    /// @param aHashedString append model relevant strings to this value for creating modelUID() hash
    void addToModelUIDHash(string &aHashedString);

  protected:

    // overrides to present property values directly instead of delegating to ValueDescriptor
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual void prepareAccess(PropertyAccessMode aMode, PropertyDescriptorPtr aPropertyDescriptor, StatusCB aPreparedCB) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<DeviceProperties> DevicePropertiesPtr;



  #define SCENECMD_DEVICE_ACTION "deviceaction" ///< name of the device action scene command


  class ActionOutputBehaviour : public OutputBehaviour
  {
    typedef OutputBehaviour inherited;

  public:

    ActionOutputBehaviour(Device &aDevice);

    /// check for presence of model feature (flag in dSS visibility matrix)
    /// @param aFeatureIndex the feature to check for
    /// @return yes if this output behaviour has the feature, no if (explicitly) not, undefined if asked entity does not know
    virtual Tristate hasModelFeature(DsModelFeatures aFeatureIndex) P44_OVERRIDE;

  protected:

    // the behaviour type
    virtual BehaviourType getType() const P44_OVERRIDE { return behaviour_actionOutput; };

    // suppress standard behaviour properties of outputBehaviour
    virtual int numDescProps() P44_OVERRIDE { return 0; };
    virtual int numSettingsProps() P44_OVERRIDE { return 0; };
    virtual int numStateProps() P44_OVERRIDE { return 0; };

  };
  typedef boost::intrusive_ptr<ActionOutputBehaviour> ActionOutputBehaviourPtr;




  /// class representing a Digital Strom "single" device.
  /// This is a device which is normally not used in zone/group context and thus
  /// usually not (very much) using scenes. It provides "actions" and "states"
  /// to provide access to its features.
  class SingleDevice : public Device
  {
    typedef Device inherited;

    friend class DeviceActions;
    friend class CustomActions;
    friend class StandardActions;
    friend class ActionMacro;
    friend class CustomAction;
    friend class StandardAction;
    friend class DeviceStates;
    friend class DeviceEvents;

  protected:

    DeviceActionsPtr deviceActions; ///< the device's standard actions
    DynamicDeviceActionsPtr dynamicDeviceActions; ///< the device's dynamic (device-side user customizable/added) actions
    CustomActionsPtr customActions; ///< the device's custom actions (user defined macros)
    StandardActionsPtr standardActions; ///< the device's standard actions (predefined macros)
    DeviceStatesPtr deviceStates; ///< the device's states
    DeviceEventsPtr deviceEvents; ///< the device's events

    DevicePropertiesPtr deviceProperties; ///< the device's specific properties


  public:
    SingleDevice(Vdc *aVdcP, bool aEnableAsSingleDevice = true);
    virtual ~SingleDevice();

    /// can be called later in the device construction process for devices that may not always need single device behaviour,
    /// when constructor was called with aEnableAsSingleDevice==false.
    /// This using SingleDevice as a base class without always introducing the overhead of the single device mechanisms
    /// Note: this can be called multiple times (but will not have any further effect after the first time)
    void enableAsSingleDevice();

    /// @name interaction with subclasses, actually representing physical I/O
    /// @{

    /// prepare for calling a scene on the device level
    /// @param aScene the scene that is to be called
    /// @return true if scene preparation is ok and call can continue. If false, no further action will be taken
    /// @note this is called BEFORE scene values are recalled
    virtual bool prepareSceneCall(DsScenePtr aScene) P44_OVERRIDE;

    /// @}



    /// @name persistence
    /// @{

    /// load parameters from persistent DB
    /// @note this is usually called from the device container when device is added (detected)
    virtual ErrorPtr load() P44_OVERRIDE;

    /// save unsaved parameters to persistent DB
    /// @note this is usually called from the device container in regular intervals
    virtual ErrorPtr save() P44_OVERRIDE;

    /// forget any parameters stored in persistent DB
    virtual ErrorPtr forget() P44_OVERRIDE;

    /// check if any settings are dirty
    virtual bool isDirty() P44_OVERRIDE;

    /// make all settings clean (not to be saved to DB)
    virtual void markClean() P44_OVERRIDE;

    /// @}


    /// dynamically configure actions, states, events and properties from JSON
    /// @param aJSONConfig a JSON object containing the configuration
    /// @return ok or parsing error
    ErrorPtr configureFromJSON(JsonObjectPtr aJSONConfig);

    /// load standard actions from JSON
    /// @param aJSONConfig a JSON object possibly containing:
    ///   - an object called "standardActions" of which each field describes a standard action
    ///     to be added.
    ///   - a boolean field called "autoAddStandardActions"; if it is true,
    ///     autoAddStandardActions() is called (to avoid listing std actions that are
    ///     just proxies for device actions without any modified parameters)
    /// @return ok or parsing error
    /// @note this is not part of configureFromJSON because loading standard actions
    ///   requires all deviceActions be present. Depending on how the device is
    ///   implemented, deviceActions might come from additional sources than
    ///   just the config, and possibly from sources only getting available after
    ///   calling configureFromJSON(), or from sources determined by other criteria
    ///   than a hardware specific config file (e.g. a device class specific file)
    ErrorPtr standardActionsFromJSON(JsonObjectPtr aJSONConfig);

    /// automatically add all device actions as standard actions
    void autoAddStandardActions();


    /// create Standard Actions based on Actions
    /// @param aDeviceActions - vector of Actions which will be a basis for Standard Action
    void autoAddStandardActions(const DeviceActions::ActionsVector& aDeviceActions);

    /// dynamically configure dynamic action (add/change/remove)
    /// @note this can be used by device implementations for adding/changing dynamic actions while device is operational.
    /// @param aJSONConfig JSON config object for creating or updating an action. If NULL or null json object, action will be deleted
    /// @return ok or parsing error
    ErrorPtr updateDynamicActionFromJSON(const string aActionId, JsonObjectPtr aJSONConfig);


    /// @name API implementation
    /// @{

    /// called by VdcHost to handle methods directed to a dSUID
    /// @param aRequest this is the request to respond to
    /// @param aMethod the method
    /// @param aParams the parameters object
    /// @return NULL if method implementation has or will take care of sending a reply (but make sure it
    ///   actually does, otherwise API clients will hang or run into timeouts)
    ///   Returning any Error object, even if ErrorOK, will cause a generic response to be returned.
    /// @note the parameters object always contains the dSUID parameter which has been
    ///   used already to route the method call to this DsAddressable.
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams) P44_OVERRIDE;

    /// @}

  protected:

    /// @param aHashedString append model relevant strings to this value for creating modelUID() hash
    virtual void addToModelUIDHash(string &aHashedString) P44_OVERRIDE;

    /// call a action
    /// @param aActionId name of the custom or device action to call
    /// @param aParams an ApiValue of type apivalue_object, can be empty or
    ///   contain parameters overriding defaults for the called action
    /// @param aCompletedCB will be called when call has completed or failed
    void call(const string aActionId, ApiValuePtr aParams, StatusCB aCompletedCB);

    /// @name factory methods for elements configured via dynamic JSON config
    /// @{

    /// creates a device action
    /// @param aAction will be assigned the new action
    /// @param aJSONConfig JSON config object for an action. Implementation can fetch specific params from it
    /// @note other params see DeviceAction constructor
    /// @return ok or parsing error
    virtual ErrorPtr actionFromJSON(DeviceActionPtr &aAction, JsonObjectPtr aJSONConfig, const string aActionId, const string aDescription, const string aCategory);

    /// creates a dynamic device action
    /// @param aAction will be assigned the new dynamic action
    /// @param aJSONConfig JSON config object for an action. Implementation can fetch specific params from it
    /// @note other params see DynamicDeviceAction constructor
    /// @return ok or parsing error
    virtual ErrorPtr dynamicActionFromJSON(DeviceActionPtr &aAction, JsonObjectPtr aJSONConfig, const string aActionId, const string aDescription, const string aTitle, const string aCategory);

    /// creates a device action parameter
    /// @param aParameter will be assigned the new dynamic action
    /// @param aJSONConfig JSON config object for an action parameter.
    /// @param aParamName name of the parameter
    /// @return ok or parsing error
    virtual ErrorPtr parameterFromJSON(ValueDescriptorPtr &aParameter, JsonObjectPtr aJSONConfig, const string aParamName);

    /// creates a device state
    /// @param aState will be assigned the new state
    /// @param aJSONConfig JSON config object for a state. Implementation can fetch specific params from it
    /// @note other params see DeviceState constructor
    /// @return ok or parsing error
    virtual ErrorPtr stateFromJSON(DeviceStatePtr &aState, JsonObjectPtr aJSONConfig, const string aStateId, const string aDescription, ValueDescriptorPtr aStateDescriptor);

    /// creates a device event
    /// @param aJSONConfig JSON config object for a event. Implementation can fetch specific params from it
    /// @note other params see DeviceEvent constructor
    /// @return ok or parsing error
    virtual ErrorPtr eventFromJSON(DeviceEventPtr &aEvent, JsonObjectPtr aJSONConfig, const string aEventId, const string aDescription);

    /// creates a device property
    /// @param aJSONConfig JSON config object for a event. Implementation can fetch specific params from it
    /// @return ok or parsing error
    virtual ErrorPtr propertyFromJSON(ValueDescriptorPtr &aProperty, JsonObjectPtr aJSONConfig, const string aPropName);

    /// @}


    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_OVERRIDE;

    void enableStandardActions();

  private:

    void invokeDeviceActionComplete(VdcApiRequestPtr aRequest, ErrorPtr aError);
    void sceneInvokedActionComplete(ErrorPtr aError);
    ErrorPtr addActionFromJSON(bool aDynamic, JsonObjectPtr aJSONConfig, const string aActionId, bool aPush);
    JsonObjectPtr getParametersFromActionDefaults(DeviceActionPtr aAction);

  };


  // MARK: -== misc utils

  /// factory method to create value descriptor from JSON descriptor
  /// @param aJSONConfig JSON config object for a value descriptor (used as parameter)
  ErrorPtr parseValueDesc(ValueDescriptorPtr &aValueDesc, JsonObjectPtr aJSONConfig, const string aParamName);

  /// function that fills enum value descriptor with values provided in JSON
  /// @param aValues Json object that contains enum values (as array)
  /// @param aEnumDesc descriptor to be filled with JSON data
  void fillEnumDescriptor(JsonObject& aValues, EnumValueDescriptor& aEnumDesc);



} // namespace p44


#endif /* defined(__p44vdc__singledevice__) */
