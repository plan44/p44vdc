//
//  Copyright (c) 2013-2016 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

  /// parameter descriptor / validator / value extractor
  /// The parameter descriptor can describe a parameter via read-only properties,
  /// can check values for conforming to the description and convert values for using in code.
  /// This is an abstract base class, actual validation/conversion is implemented in subclasses
  class ParameterDescriptor : public PropertyContainer
  {
    typedef PropertyContainer inherited;

    friend class DeviceAction;
    friend class DeviceState;

  protected:

    /// base class constructor for subclasses
    /// @param aValueType the value type of this parameter (descibing computing and physical value type)
    /// @param aHasDefault true if the parameter has a default value (meaning it can be omitted in calls)
    ParameterDescriptor(VdcValueType aValueType, bool aHasDefault) : valueType(aValueType), hasDefault(aHasDefault) {};

  public:

    /// checks if aApiValue conforms to the parameter definition
    /// @param aApiValue API value containing a value to be used for this parameter
    /// @return true if the value conforms, false if not
    virtual bool conforms(ApiValuePtr aApiValue) { return false; /* base class does not conform to anything */ };

    /// checks if aApiValue is a value suitable for this param and return it as double value
    /// @param aApiValue API value containing a value to be used for this parameter
    /// @return double value
    /// @note aApiValue should be checked with conforms() before trying to extract value
    virtual double doubleValue(ApiValuePtr aApiValue) { return 0; /* dummy result */ };

    /// checks if aApiValue is a value suitable for this param and return it as int value
    /// @param aApiValue API value containing a value to be used for this parameter
    /// @return int value
    /// @note aApiValue should be checked with conforms() before trying to extract value
    virtual int intValue(ApiValuePtr aApiValue) { return 0; /* dummy result */ };

    /// checks if aApiValue is a value suitable for this param and return it as string value
    /// @param aApiValue API value containing a value to be used for this parameter
    /// @return string value
    /// @note aApiValue should be checked with conforms() before trying to extract value
    virtual string stringValue(ApiValuePtr aApiValue) { return ""; /* dummy result */ };

  protected:

    bool hasDefault; ///< set if the parameter has a default value
    string paramName; ///< the name of the parameter
    VdcValueType valueType; ///< the type of the parameter

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyContainerPtr getContainer(PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain);
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);
  };
  typedef boost::intrusive_ptr<ParameterDescriptor> ParameterDescriptorPtr;


  /// parameter descriptor subclass for numeric parameters, describing parameter via min/max/resolution
  class NumericParameter : public ParameterDescriptor
  {
    typedef ParameterDescriptor inherited;

    double min;
    double max;
    double resolution;
    double defaultValue;

  public:

    /// constructor for a numeric parameter, which can be any of the physical unit types, bool, int, numeric enum or generic double
    NumericParameter(VdcValueType aValueType, double aMin, double aMax, double aResolution, bool aHasDefault, double aDefaultValue = 0) :
      inherited(aValueType, aHasDefault), min(aMin), max(aMax), resolution(aResolution), defaultValue(aDefaultValue) {};

    virtual bool conforms(ApiValuePtr aApiValue);
    virtual double doubleValue(ApiValuePtr aApiValue);
    virtual int intValue(ApiValuePtr aApiValue);

  protected:

    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);

  };


  /// parameter descriptor subclass for text parameters
  class TextParameter : public ParameterDescriptor
  {
    typedef ParameterDescriptor inherited;

    string defaultValue;

  public:

    /// constructor for a text string parameter
    TextParameter(bool aHasDefault, const string aDefaultValue = "") :
      inherited(valueType_text, aHasDefault), defaultValue(aDefaultValue) {};

    virtual bool conforms(ApiValuePtr aApiValue);
    virtual string stringValue(ApiValuePtr aApiValue);

  protected:

    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);
    
  };


  /// parameter descriptor subclass for enumeration parameters, describing parameter via a list of possible values
  class EnumParameter : public ParameterDescriptor
  {
    typedef ParameterDescriptor inherited;

    typedef map<string, uint32_t> EnumValuesMap;

    EnumValuesMap enumValues;

    uint32_t defaultValue;

  public:

    /// constructor for a text enumeration parameter
    /// @param aHasDefault if true, the first enum value is also considered the default
    EnumParameter(bool aHasDefault);

    /// add a enum value
    /// @param aEnumText the text
    /// @param aEnumValue the numeric value corresponding to the text.
    void addEnum(const char *aEnumText, int aEnumValue);

    virtual bool conforms(ApiValuePtr aApiValue);
    virtual int intValue(ApiValuePtr aApiValue);

  protected:

    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);
    
  };



  class DeviceAction : public PropertyContainer
  {
    typedef PropertyContainer inherited;

    typedef map<string, ParameterDescriptorPtr> ParameterDescriptorMap;

    /// the name of the action
    string actionName;
    /// a descriptive string for the action (for logs and debugging)
    string actionDescription;
    /// the parameter descriptions of this action
    ParameterDescriptorMap actionParams;

  public:

    /// create the action
    /// @param aSingleDevice the single device this action belongs to
    /// @param aActionName the name of the action
    /// @param aDescription a description string for the action.
    DeviceAction(SingleDevice &aSingleDevice, const string aActionName, const string aDescription);

    /// add a parameter (descriptor)
    /// @param aParam a parameter descriptor object. Note that the same parameter descriptor might be used by multiple actions and states
    void addParameter(ParameterDescriptorPtr aParam);

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
    virtual PropertyDescriptorPtr getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyContainerPtr getContainer(PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain);
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);
  };
  typedef boost::intrusive_ptr<DeviceAction> DeviceActionPtr;




  class DeviceActions : public PropertyContainer
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

  public:

    /// call the custom action
    /// @param aParams an ApiValue of type apivalue_object, may be used to override stored parameters
    void call(ApiValuePtr aParams);

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
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



  class CustomActions : public PropertyContainer
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

    #error "add params and paramdef for state"

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyContainerPtr getContainer(PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain);
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);

  };
  typedef boost::intrusive_ptr<DeviceState> DeviceStatePtr;



  class DeviceStates : public PropertyContainer
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

    /// called to let device handle device-level notification
    /// @param aMethod the notification
    /// @param aParams the parameters object
    /// @note the parameters object always contains the dSUID parameter which has been
    ///   used already to route the notification to this device.
    virtual void handleNotification(const string &aMethod, ApiValuePtr aParams);

    /// call action on this device
    /// @param aActionName the name of the action to call.
    /// @param aActionParams the action parameters (can be null)
    void callDeviceAction(const string aActionName, ApiValuePtr aActionParams);

    /// @}

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyContainerPtr getContainer(PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain);
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);
    virtual ErrorPtr writtenProperty(PropertyAccessMode aMode, PropertyDescriptorPtr aPropertyDescriptor, int aDomain, PropertyContainerPtr aContainer);

  private:


  };




} // namespace p44


#endif /* defined(__p44vdc__singledevice__) */
