//
//  Copyright (c) 2016 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

using namespace std;

namespace p44 {


  class SingleDevice;
  class DeviceAction;
  class DeviceState;

  /// value descriptor / validator / value extractor
  /// The value descriptor can describe a parameter via read-only properties,
  /// can check values for conforming to the description and convert values for using in code.
  /// This is an abstract base class, actual validation/conversion is implemented in subclasses
  class ValueDescriptor : public PropertyContainer
  {
    typedef PropertyContainer inherited;

    friend class DeviceAction;
    friend class DeviceState;

  protected:

    /// base class constructor for subclasses
    /// @param aValueType the value type of this value (descibing computing and physical value type)
    /// @param aHasDefault true if the parameter has a default value (meaning it can be omitted in calls)
    ValueDescriptor(VdcValueType aValueType, bool aHasDefault) : valueType(aValueType), hasValue(aHasDefault) {};

  public:

    /// Using external values as action parameter - checks and conversions
    /// @{

    /// checks if aApiValue conforms to the parameter definition
    /// @param aApiValue API value containing a value to be used for this parameter
    /// @return true if the value conforms, false if not
    /// @note providing no value conforms when the parameter has a default value
    virtual bool conforms(ApiValuePtr aApiValue) { return false; /* base class does not conform to anything */ };

    /// checks if aApiValue is a value suitable for this param and return it as double value
    /// @param aApiValue API value containing a value to be used for this parameter
    /// @return double value
    /// @note aApiValue should be checked with conforms() before trying to extract value
    /// @note if no aApiValue is passed, and there is a default value, the default value is returned
    virtual double doubleValue(ApiValuePtr aApiValue) { return 0; /* dummy result */ };

    /// checks if aApiValue is a value suitable for this param and return it as int value
    /// @param aApiValue API value containing a value to be used for this parameter
    /// @return int value
    /// @note aApiValue should be checked with conforms() before trying to extract value
    /// @note if no aApiValue is passed, and there is a default value, the default value is returned
    virtual int intValue(ApiValuePtr aApiValue) { return 0; /* dummy result */ };

    /// checks if aApiValue is a value suitable for this param and return it as string value
    /// @param aApiValue API value containing a value to be used for this parameter
    /// @return string value
    /// @note aApiValue should be checked with conforms() before trying to extract value
    /// @note if no aApiValue is passed, and there is a default value, the default value is returned
    virtual string stringValue(ApiValuePtr aApiValue) { return ""; /* dummy result */ };

    /// @}

    /// Setting state and state parameter value to allow query via API and property pushing
    /// @{

    /// set double value
    /// @param aValue the double value to set
    virtual void setDoubleValue(double aValue) { /* NOP in base class */ };


    /// set int value
    /// @param aValue the int value to set
    virtual void setIntValue(int aValue) { /* NOP in base class */ };

    /// set string value
    /// @param aValue the string value to set
    virtual void setStringValue(const string aValue) { /* NOP in base class */ };

    /// @}


  protected:

    bool hasValue; ///< set if the parameter has a value. For action params, this is the default value. For state/states params this is the actual value
    VdcValueType valueType; ///< the type of the parameter

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);
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

  public:

    /// constructor for a numeric parameter, which can be any of the physical unit types, bool, int, numeric enum or generic double
    NumericValueDescriptor(VdcValueType aValueType, double aMin, double aMax, double aResolution, bool aHasDefault, double aDefaultValue = 0) :
      inherited(aValueType, aHasDefault), min(aMin), max(aMax), resolution(aResolution), value(aDefaultValue) {};

    // check and return values for this parameter
    virtual bool conforms(ApiValuePtr aApiValue);
    virtual double doubleValue(ApiValuePtr aApiValue);
    virtual int intValue(ApiValuePtr aApiValue);

    // set the value (for updating states)
    virtual void setDoubleValue(double aValue) { value = aValue; };
    virtual void setIntValue(int aValue) { value = aValue; };

  protected:

    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);

  };


  /// parameter descriptor subclass for text parameters
  class TextValueDescriptor P44_FINAL : public ValueDescriptor
  {
    typedef ValueDescriptor inherited;

    string value; ///< the (default) value

  public:

    /// constructor for a text string parameter
    TextValueDescriptor(bool aHasDefault, const string aDefaultValue = "") :
      inherited(valueType_text, aHasDefault), value(aDefaultValue) {};

    // check and return values for this parameter
    virtual bool conforms(ApiValuePtr aApiValue);
    virtual string stringValue(ApiValuePtr aApiValue);

    // set the value (for updating states)
    virtual void setStringValue(const string aValue) { value = aValue; };

  protected:

    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);
    
  };


  /// parameter descriptor subclass for enumeration parameters, describing parameter via a list of possible values
  class EnumValueDescriptor P44_FINAL : public ValueDescriptor
  {
    typedef ValueDescriptor inherited;

    typedef pair<string, uint32_t> EnumDesc;
    typedef vector<EnumDesc> EnumVector;

    EnumVector enumDescs; ///< text to enum value mapping pairs
    uint32_t value; ///< the (default) enum value

  public:

    /// constructor for a text enumeration parameter
    EnumValueDescriptor(bool aHasDefault) : inherited(valueType_textenum, aHasDefault) {};

    /// add a enum value
    /// @param aEnumText the text
    /// @param aEnumValue the numeric value corresponding to the text.
    /// @param aIsDefault if set, this is considered the default value
    void addEnum(const char *aEnumText, int aEnumValue, bool aIsDefault = false);

    // check and return values for this parameter
    virtual bool conforms(ApiValuePtr aApiValue);
    virtual int intValue(ApiValuePtr aApiValue);

    // set the value (for updating states)
    virtual void setIntValue(int aValue) { value = aValue; };

  protected:

    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyContainerPtr getContainer(PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain);
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);
    
  };


  class ValueList P44_FINAL : public PropertyContainer
  {
    typedef PropertyContainer inherited;

    typedef pair<string, ValueDescriptorPtr> ValueEntry;
    typedef vector<ValueEntry> ValuesVector;

    ValuesVector values;

  public:

    /// add a value (descriptor)
    /// @param aValueDesc a value descriptor object.
    void addValueDescriptor(const string aValueName, ValueDescriptorPtr aValueDesc);

  protected:

    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyContainerPtr getContainer(PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain);
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);

  };
  typedef boost::intrusive_ptr<ValueList> ValueListPtr;



  class DeviceAction : public PropertyContainer
  {
    typedef PropertyContainer inherited;


    /// a descriptive string for the action (for logs and debugging)
    string actionDescription;
    /// the parameter descriptions of this action
    ValueListPtr actionParams;

  public:

    /// create the action
    /// @param aSingleDevice the single device this action belongs to
    /// @param aDescription a description string for the action.
    DeviceAction(SingleDevice &aSingleDevice, const string aDescription);

    /// call the action
    /// @param aParams an ApiValue of type apivalue_object, expected to
    ///   contain parameters matching the actual parameters available in the action
    /// @note this public method will verify that the parameter name and values match the action's parameter description
    ///   and prevent calling subclass' performCall() when parameters are not ok.
    void call(ApiValuePtr aParams);

  protected:

    /// action implementation.
    /// @param aParams an ApiValue of type apivalue_object, containing ALL parameters in the description, either from defaults
    ///   or from overriding values specified to the call() method.
    /// @note this method is usually overridden by specific device subclasses actually implementing functionality
    virtual void performCall(ApiValuePtr aParams) { /* NOP in base class */ };

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyContainerPtr getContainer(PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain);
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);
  };
  typedef boost::intrusive_ptr<DeviceAction> DeviceActionPtr;




  class DeviceActions P44_FINAL : public PropertyContainer
  {
    typedef PropertyContainer inherited;

    typedef map<string, DeviceActionPtr> DeviceActionMap;

    DeviceActionMap deviceActions;

  public:

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyContainerPtr getContainer(PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain);
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);
  };
  typedef boost::intrusive_ptr<DeviceActions> DeviceActionsPtr;




  class CustomAction : public PropertyContainer
  {
    typedef PropertyContainer inherited;

    /// references the standard action on which this custom action is based
    DeviceActionPtr action;

    /// the parameters stored in this custom action.
    /// @note these parameters will be used at call() unless overridden by parameters explicitly passed to call().
    ///   call() will then pass these to action->call() for execution of a standard action with custom parameters.
    ApiValuePtr storedParams;

    /// if true, this custom action belongs to the predefined set of actions for this device
    bool predefined;

  public:

    /// call the custom action
    /// @param aParams an ApiValue of type apivalue_object, may be used to override stored parameters
    void call(ApiValuePtr aParams);

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyContainerPtr getContainer(PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain);
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);

    // persistence implementation
    virtual const char *tableName();
    virtual size_t numKeyDefs();
    virtual const FieldDefinition *getKeyDef(size_t aIndex);
    virtual size_t numFieldDefs();
    virtual const FieldDefinition *getFieldDef(size_t aIndex);
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP);
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags);

  };
  typedef boost::intrusive_ptr<CustomAction> CustomActionPtr;



  class CustomActions P44_FINAL : public PropertyContainer
  {
    typedef PropertyContainer inherited;

    typedef map<string, CustomActionPtr> CustomActionMap;

    CustomActionMap customActions;

  public:

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyContainerPtr getContainer(PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain);
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);

    /// load custom actions
    virtual ErrorPtr loadChildren() P44_FINAL;
    /// save custom actions
    virtual ErrorPtr saveChildren() P44_FINAL;
    /// delete custom actions
    virtual ErrorPtr deleteChildren() P44_FINAL;

    /// load default set or overriding custom actions from files
    void loadActionsFromFiles();

  };
  typedef boost::intrusive_ptr<CustomActions> CustomActionsPtr;




  class DeviceState : public PropertyContainer
  {
    typedef PropertyContainer inherited;

    ValueDescriptorPtr stateDescriptor;
    ValueListPtr stateParams;


    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyContainerPtr getContainer(PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain);
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);

  };
  typedef boost::intrusive_ptr<DeviceState> DeviceStatePtr;



  class DeviceStates P44_FINAL : public PropertyContainer
  {
    typedef PropertyContainer inherited;

    typedef map<string, DeviceStatePtr> DeviceStateMap;

    DeviceStateMap deviceStates;

  public:

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyContainerPtr getContainer(PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain);
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);

  };
  typedef boost::intrusive_ptr<DeviceStates> DeviceStatesPtr;





  /// class representing a digitalSTROM "single" device.
  /// This is a device which is normally not used in zone/group context and thus
  /// usually not (very much) using scenes. It provides "actions" and "states"
  /// to provide access to its features.
  class SingleDevice : public Device
  {
    typedef Device inherited;

    friend class DeviceActions;
    friend class CustomActions;
    friend class DeviceStates;

  protected:

    DeviceActionsPtr deviceActions; /// the device's standard actions
    DeviceActionsPtr customActions; /// the device's custom actions
    DeviceStatesPtr deviceStates; /// the device's states

    ValueListPtr deviceProperties; /// the device's specific properties


  public:
    SingleDevice(Vdc *aVdcP);
    virtual ~SingleDevice();


    /// @name persistence
    /// @{

    /// load parameters from persistent DB
    /// @note this is usually called from the device container when device is added (detected)
    virtual ErrorPtr load();

    /// save unsaved parameters to persistent DB
    /// @note this is usually called from the device container in regular intervals
    virtual ErrorPtr save();

    /// forget any parameters stored in persistent DB
    virtual ErrorPtr forget();

    // check if any settings are dirty
    virtual bool isDirty();

    // make all settings clean (not to be saved to DB)
    virtual void markClean();

    // load additional settings from files
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
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams);

    /// call action on this device
    /// @param aActionName the name of the action to call.
    /// @param aActionParams the action parameters (can be null)
    /// @return ok or error
    ErrorPtr callDeviceAction(const string aActionName, ApiValuePtr aActionParams);
    

    /// @}

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyContainerPtr getContainer(PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain);

  private:


  };




} // namespace p44


#endif /* defined(__p44vdc__singledevice__) */
