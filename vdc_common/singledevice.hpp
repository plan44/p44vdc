//
//  Copyright (c) 2016-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

using namespace std;

namespace p44 {


  class SingleDevice;
  class DeviceAction;
  class DeviceState;
  class DeviceEvent;

  /// value descriptor / validator / value extractor
  /// The value descriptor can describe a parameter via read-only properties,
  /// can check values for conforming to the description and convert values for using in code.
  /// This is an abstract base class, actual validation/conversion is implemented in subclasses
  class ValueDescriptor : public PropertyContainer
  {
    typedef PropertyContainer inherited;

    friend class ValueList;

  protected:

    /// base class constructor for subclasses
    /// @param aName the name of this value
    /// @param aValueType the value type of this value (describing computing type)
    /// @param aValueUnit the value unit if this value (describing physical unit type and scaling)
    /// @param aHasDefault true if the parameter has a non-null default value
    ValueDescriptor(const string aName, VdcValueType aValueType, VdcValueUnit aValueUnit, bool aHasDefault);

  public:

    /// Using external values as action parameter - checks and conversions
    /// @{

    /// checks if aApiValue conforms to the parameter definition
    /// @param aApiValue API value containing a value to be used for this value. No value at all (NULL) counts as conforming.
    /// @param aMakeInternal if set, the value is converted to internal format (relevant for enums, to get them as numeric value)
    /// @return NULL if the value conforms, API error describing what's wrong if not
    virtual ErrorPtr conforms(ApiValuePtr aApiValue, bool aMakeInternal = false) = 0;

    /// get the name
    /// @return name of this value
    string getName() const { return valueName; }

    /// get the time of last update (even if no change)
    /// @return the time of last update or Never if value has never been set so far
    MLMicroSeconds getLastUpdate() { return lastUpdate; };

    /// get the time of last change
    /// @return the time of last change or Never if value has never been set so far
    MLMicroSeconds getLastChange() { return lastChange; };


    /// get the (default) value into an ApiValue
    /// @param aApiValue the API value to write the value to
    /// @param aAsInternal if set, the value is returned in internal format (relevant for enums, to get them as numeric value)
    /// @param aPrevious if set, the previous value is returned
    /// @return true if there is a (default) value that could be assigned to aApiValue, false otherwise (aApiValue will be untouched)
    virtual bool getValue(ApiValuePtr aApiValue, bool aAsInternal = false, bool aPrevious = false) = 0;

    /// get the value as string
    /// @param aAsInternal if set, the value is returned in internal format (relevant for enums, to get them as numeric value)
    /// @param aPrevious if set, the previous value is returned
    /// @return string representation of the current value
    string getStringValue(bool aAsInternal = false, bool aPrevious = false);

    /// get a double value
    /// @param aAsInternal if set, the value is returned in internal format (relevant for enums, to get them as numeric value)
    /// @param aPrevious if set, the previous value is returned
    double getDoubleValue(bool aAsInternal = false, bool aPrevious = false);

    /// get a double value
    /// @param aAsInternal if set, the value is returned in internal format (relevant for enums, to get them as numeric value)
    /// @param aPrevious if set, the previous value is returned
    int32_t getInt32Value(bool aAsInternal = false, bool aPrevious = false);


    /// get the name
    /// @return the name
    string getName() { return valueName; };

    /// @}

    /// Setting state and state parameter value to allow query via API and property pushing
    /// @{

    /// set double value
    /// @param aValue the double value to set
    /// @return true if set value differs from previous value
    virtual bool setDoubleValue(double aValue) { return false; /* NOP in base class */ };

    /// update double value for significant changes only
    /// @param aValue the new value from the sensor, in physical units according to siUnit
    /// @param aMinChange what minimum change the new value must have compared to last reported value
    ///   to be treated as a change. Default is -1, which means half the declared resolution.
    /// @return true if set value differs enough from previous value and was actually updated
    virtual bool updateDoubleValue(double aValue, double aMinChange = -1)  { return false; /* NOP in base class */ };

    /// set int value
    /// @param aValue the int value to set
    /// @return true if set value differs from previous value
    virtual bool setInt32Value(int32_t aValue) { return false; /* NOP in base class */ };

    /// set string value
    /// @param aValue the string value to set
    /// @return true if set value differs from previous value
    virtual bool setStringValue(const string aValue) { return false; /* NOP in base class */ };

    /// set API value
    /// @param aValue the value to set, already converted to internal format (for text enums)
    /// @return true if set value differs from previous value
    /// @note conforms() should be applied to aValue first to make sure value is ok to set and is converted to internal value
    bool setValue(ApiValuePtr aValue);

    /// make value invalid, reported as NULL when accessed via properties
    /// @return true if value was valid before (i.e. became invalid now)
    bool invalidate();

    /// set "defaultvalue" flag
    void setIsDefault(bool aIsDefault) { isDefaultValue = aIsDefault; };

    /// set "readonly" flag
    void setReadOnly(bool aReadOnly) { readOnly = aReadOnly; };

    /// check readonly flag
    bool isReadOnly() { return readOnly; }

    /// check default value flag (value itself can still be NULL)
    bool isDefault() { return isDefaultValue; }


    /// @}

    /// Utilities
    /// @{

    /// get name of a given VdcValueType
    /// @param aValueType the value type to get the name for
    /// @return value type name
    static string valueTypeName(VdcValueType aValueType);

    /// get value type from a given string
    /// @param aValueTypeName a value type name string
    /// @return value type (valueType_unknown when string does not match)
    static VdcValueType stringToValueType(const string aValueTypeName);


    /// get unit name or symbol for a given VdcValueUnit
    /// @param aValueUnit the value unit to get the name for
    /// @param aAsSymbol if set, the name is returned as symbol (m), otherwise as full text (meter)
    /// @return unit name or symbol including scaling
    static string valueUnitName(VdcValueUnit aValueUnit, bool aAsSymbol);

    /// get value unit from a given string
    /// @param aValueUnitName a value unit specification string (consisting of unit and optional scaling prefix)
    /// @return value unit (unit_unknown when string does not match)
    static VdcValueUnit stringToValueUnit(const string aValueUnitName);

    /// @}


  protected:

    string valueName; ///< the name of the value
    bool hasValue; ///< set if there is a stored value. For action params, this is the default value. For state/states params this is the actual value
    bool isDefaultValue; ///< set if the value stored is the default value
    bool readOnly; ///< set if the value cannot be written
    VdcValueType valueType; ///< the technical type of the value
    VdcValueUnit valueUnit; ///< the unit+scaling of the value
    MLMicroSeconds lastUpdate; ///< when the value was last updated
    MLMicroSeconds lastChange; ///< when the value was last changed

    /// set last update
    /// @param aLastUpdate time of last update, can be Never (or Infinite to use current now())
    /// @return true if this update caused hasValue to be changed from false to true
    bool setLastUpdate(MLMicroSeconds aLastUpdate=Infinite);

    /// report if changed
    /// @param aChanged true if value has changed
    /// @return just returns aChanged
    bool setChanged(bool aChanged);


    /// notify that this value has just updated
    /// @note this must be called by setters just after updating update time, current and previous values.
    ///   it is used to possibly trigger events and property pushes
    void notifyUpdate();

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;
  };
  typedef boost::intrusive_ptr<ValueDescriptor> ValueDescriptorPtr;


  /// parameter descriptor subclass for numeric parameters, describing parameter via min/max/resolution
  class NumericValueDescriptor P44_FINAL : public ValueDescriptor
  {
    typedef ValueDescriptor inherited;

    double min; ///< maximum allowed value
    double max; ///< minimum allowed value
    double resolution; ///< resolution
    double value; ///< the (default) value
    double previousValue; ///< the previous value

  public:

    /// constructor for a numeric parameter, which can be any of the physical unit types, bool, int, numeric enum or generic double
    NumericValueDescriptor(const string aName, VdcValueType aValueType, VdcValueUnit aValueUnit, double aMin, double aMax, double aResolution, bool aHasDefault = false, double aDefaultValue = 0) :
      inherited(aName, aValueType, aValueUnit, aHasDefault), min(aMin), max(aMax), resolution(aResolution), value(aDefaultValue) {};

    virtual ErrorPtr conforms(ApiValuePtr aApiValue, bool aMakeInternal = false) P44_OVERRIDE;
    virtual bool getValue(ApiValuePtr aApiValue, bool aAsInternal = false, bool aPrevious = false) P44_OVERRIDE;

    virtual bool setDoubleValue(double aValue) P44_OVERRIDE;
    virtual bool updateDoubleValue(double aValue, double aMinChange = -1) P44_OVERRIDE;

    virtual bool setInt32Value(int32_t aValue) P44_OVERRIDE;

  protected:

    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

  };


  /// parameter descriptor subclass for text parameters
  class TextValueDescriptor P44_FINAL : public ValueDescriptor
  {
    typedef ValueDescriptor inherited;

    string value; ///< the (default) value
    string previousValue; ///< the previous value

  public:

    /// constructor for a text string parameter
    TextValueDescriptor(const string aName, bool aHasDefault = false, const string aDefaultValue = "") :
      inherited(aName, valueType_string, valueUnit_none, aHasDefault), value(aDefaultValue) {};

    virtual ErrorPtr conforms(ApiValuePtr aApiValue, bool aMakeInternal = false) P44_OVERRIDE;
    virtual bool getValue(ApiValuePtr aApiValue, bool aAsInternal = false, bool aPrevious = false) P44_OVERRIDE;

    virtual bool setStringValue(const string aValue) P44_OVERRIDE;
    
  };


  /// parameter descriptor subclass for enumeration parameters, describing parameter via a list of possible values
  class EnumValueDescriptor P44_FINAL : public ValueDescriptor
  {
    typedef ValueDescriptor inherited;

    typedef pair<string, uint32_t> EnumDesc;
    typedef vector<EnumDesc> EnumVector;

    EnumVector enumDescs; ///< text to enum value mapping pairs
    uint32_t value; ///< the (default) enum value
    uint32_t previousValue; ///< the previous value
    bool noInternalValue; ///< the internal value is not exposed, getValue() always returns external (text) value

  public:

    /// constructor for a text enumeration parameter
    EnumValueDescriptor(const string aName, bool aNoInternalValue=false) : inherited(aName, valueType_enumeration, valueUnit_none, false), noInternalValue(aNoInternalValue) {};

    /// add a enum value
    /// @param aEnumText the text
    /// @param aEnumValue the numeric value corresponding to the text.
    /// @param aIsDefault if set, this is considered the default value
    void addEnum(const char *aEnumText, int aEnumValue, bool aIsDefault = false);

    virtual ErrorPtr conforms(ApiValuePtr aApiValue, bool aMakeInternal = false) P44_OVERRIDE;
    virtual bool getValue(ApiValuePtr aApiValue, bool aAsInternal = false, bool aPrevious = false) P44_OVERRIDE;

    virtual bool setInt32Value(int32_t aValue) P44_OVERRIDE;
    virtual bool setStringValue(const string aValue) P44_OVERRIDE;

  protected:

    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<EnumValueDescriptor> EnumValueDescriptorPtr;


  class ValueList : public PropertyContainer
  {
    typedef PropertyContainer inherited;

  public:

    typedef vector<ValueDescriptorPtr> ValuesVector;

    ValuesVector values;

    /// add a value (descriptor)
    /// @param aValueDesc a value descriptor object.
    void addValue(ValueDescriptorPtr aValueDesc);

    /// get value (for applying updates)
    /// @param aName name of the value(descriptor) to get
    ValueDescriptorPtr getValue(const string aName);


  protected:

    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_FINAL P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_FINAL P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<ValueList> ValueListPtr;



  class DeviceAction : public PropertyContainer
  {
    typedef PropertyContainer inherited;

    friend class DeviceActions;
    friend class CustomAction;

  protected:

    string actionId; ///< id of the action (key in the container object)
    string actionDescription; ///< a descriptive string for the action (for logs and debugging)
    ValueListPtr actionParams; ///< the parameter descriptions of this action

    SingleDevice *singleDeviceP; ///< the single device this action belongs to

  public:

    /// create the action
    /// @param aSingleDevice the single device this action belongs to
    /// @param aId the ID of the action (key in the container)
    /// @param aDescription a description string for the action.
    DeviceAction(SingleDevice &aSingleDevice, const string aId, const string aDescription);

    /// add parameter
    /// @param aValueDesc a value descriptor object.
    /// @param aMandatory if set, parameter must be explicitly specified and does not have an implicit default (not even NULL)
    void addParameter(ValueDescriptorPtr aValueDesc, bool aMandatory = false);

    /// call the action
    /// @param aParams an ApiValue of type apivalue_object, expected to
    ///   contain parameters matching the actual parameters available in the action
    /// @param aCompletedCB must be called when call has completed
    /// @note this public method will verify that the parameter name and values match the action's parameter description
    ///   and prevent calling subclass' performCall() when parameters are not ok.
    void call(ApiValuePtr aParams, StatusCB aCompletedCB);

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




  class DeviceActions P44_FINAL : public PropertyContainer
  {
    typedef PropertyContainer inherited;

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

    /// get state (for applying updates)
    /// @param aActionId id of the state to get
    DeviceActionPtr getAction(const string aActionId);

    /// add an action (at device setup time only)
    /// @param aAction the action
    void addAction(DeviceActionPtr aAction);

    /// @param aHashedString append model relevant strings to this value for creating modelUID() hash
    void addToModelUIDHash(string &aHashedString);

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    
  };
  typedef boost::intrusive_ptr<DeviceActions> DeviceActionsPtr;




  class CustomAction P44_FINAL : public PropertyContainer, public PersistentParams
  {
    typedef PersistentParams inheritedParams;
    typedef PropertyContainer inheritedProps;

    friend class CustomActions;

    SingleDevice &singleDevice; ///< the single device this custom action belongs to

    string actionId; ///< the ID of the action (key in the container)
    string actionTitle; ///< the user-faced name of the action
    uint32_t flags; ///< flags

    /// references the standard action on which this custom action is based
    DeviceActionPtr action;

    /// the parameters stored in this custom action.
    /// @note these parameters will be used at call() unless overridden by parameters explicitly passed to call().
    ///   call() will then pass these to action->call() for execution of a standard action with custom parameters.
    ApiValuePtr storedParams;

    /// if true, this custom action belongs to the predefined set of actions for this device
    bool predefined;

  public:

    /// create the custom action
    /// @param aSingleDevice the single device this custom action belongs to
    CustomAction(SingleDevice &aSingleDevice);

    /// call the custom action
    /// @param aParams an ApiValue of type apivalue_object, may be used to override stored parameters
    /// @param aCompletedCB must be called when call has completed
    void call(ApiValuePtr aParams, StatusCB aCompletedCB);

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

  private:

    ErrorPtr validateParams(ApiValuePtr aParams, ApiValuePtr aValidatedParams, bool aSkipInvalid);

  };
  typedef boost::intrusive_ptr<CustomAction> CustomActionPtr;



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
    /// load default set or overriding custom actions from files
    void loadActionsFromFiles();

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyAccessMode aMode, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

  };
  typedef boost::intrusive_ptr<CustomActions> CustomActionsPtr;



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

  /// class representing the device-specific properties
  class DeviceProperties P44_FINAL : public ValueList
  {
    typedef ValueList inherited;

    PropertyChangedCB propertyChangeHandler; ///< called when property has been changed via the API

  protected:

    SingleDevice *singleDeviceP; ///< the single device the events belong to

  public:

    DeviceProperties(SingleDevice &aSingleDevice) : singleDeviceP(&aSingleDevice), propertyChangeHandler(NULL) {};

    /// set a property change handler
    void setPropertyChangedHandler(PropertyChangedCB aPropertyChangedHandler) { propertyChangeHandler = aPropertyChangedHandler; };

    /// add a property (at device setup time only)
    /// @param aPropertyDesc value descriptor describing the property
    /// @param aReadOnly if set, the property cannot be written from the API and will report a "readonly:true" field in the description
    void addProperty(ValueDescriptorPtr aPropertyDesc, bool aReadOnly = false);

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
    virtual BehaviourType getType() P44_OVERRIDE { return behaviour_actionOutput; };

    // suppress standard behaviour properties of outputBehaviour
    virtual int numDescProps() P44_OVERRIDE { return 0; };
    virtual int numSettingsProps() P44_OVERRIDE { return 0; };
    virtual int numStateProps() P44_OVERRIDE { return 0; };

  };
  typedef boost::intrusive_ptr<ActionOutputBehaviour> ActionOutputBehaviourPtr;





  /// class representing a digitalSTROM "single" device.
  /// This is a device which is normally not used in zone/group context and thus
  /// usually not (very much) using scenes. It provides "actions" and "states"
  /// to provide access to its features.
  class SingleDevice : public Device
  {
    typedef Device inherited;

    friend class DeviceActions;
    friend class CustomActions;
    friend class CustomAction;
    friend class DeviceStates;
    friend class DeviceEvents;

  protected:

    DeviceActionsPtr deviceActions; ///< the device's standard actions
    CustomActionsPtr customActions; ///< the device's custom actions
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

    /// load additional settings from files
    void loadSettingsFromFiles();

    /// @}


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

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_OVERRIDE;

  private:

    void invokeDeviceActionComplete(VdcApiRequestPtr aRequest, ErrorPtr aError);
    void sceneInvokedActionComplete(ErrorPtr aError);

  };



  // MARK: ======= misc utils


  /// callback function for substitutePlaceholders()
  /// @param aValue the contents of this is looked up and possibly replaced
  /// @return ok or error
  typedef boost::function<ErrorPtr (string &aValue)> ValueLookupCB;

  /// substitute "@{xxx}" type placeholders in string
  /// @param aString string to replace placeholders in
  /// @param aValueLookupCB this will be called to have variable names looked up
  ErrorPtr substitutePlaceholders(string &aString, ValueLookupCB aValueLookupCB);




} // namespace p44


#endif /* defined(__p44vdc__singledevice__) */
