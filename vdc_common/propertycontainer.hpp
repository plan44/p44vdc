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

#ifndef __p44vdc__propertycontainer__
#define __p44vdc__propertycontainer__

#include "vdcapi.hpp"

using namespace std;

namespace p44 {

  class PropertyContainer;

  struct PropertyDescriptor;


  #define OKEY(x) ((intptr_t)&x) ///< macro to define unique object keys by using address of a variable

  #define PROPINDEX_NONE -1 ///< special value to signal "no next descriptor" for getDescriptorByName

  /// type for const tables describing static properties
  typedef struct PropertyDescription {
    const char *propertyName; ///< name of the property
    uint16_t propertyType; ///< type of the property value
    size_t fieldKey; ///< key for accessing the property within its container. (size_t to allow using offset into struct)
    intptr_t objectKey; ///< identifier for object this property belongs to (for properties spread over sublcasses). If propflag_container is set, this specifies the key for the contained objects, not the property itself
  } PropertyDescription;

  typedef enum {
    access_read,
    access_write,
    access_write_preload,
    access_delete // only for accessField of isDeleteable() properties
  } PropertyAccessMode;

  typedef enum {
    proptype_mask = 0x3F,
    propflag_container = 0x80, ///< is a container
    propflag_nowildcard = 0x40, ///< don't recurse into this container when addressed via wildcard
    propflag_deletable = 0x100, ///< can be deleted by writing NULL to it
    propflag_needsreadprep = 0x200, ///< needs to be prepared before reading
    propflag_needswriteprep = 0x400, ///< needs to be prepared before writing
  } PropertyFlags;


  typedef boost::intrusive_ptr<PropertyDescriptor> PropertyDescriptorPtr;

  /// description of a property
  class PropertyDescriptor : public P44Obj
  {
  public:
    bool rootOfObject;

    /// constructor
    PropertyDescriptor(PropertyDescriptorPtr aParentDescriptor) : parentDescriptor(aParentDescriptor), rootOfObject(false) {};
    /// the parent descriptor (NULL at root level of DsAdressables)
    PropertyDescriptorPtr parentDescriptor;
    /// API version
    virtual int getApiVersion() const { return (parentDescriptor ? parentDescriptor->getApiVersion() : 0); };
    /// name of the property
    virtual const char *name() const = 0;
    /// type of the property
    virtual ApiValueType type() const = 0;
    /// access index/key of the property  (size_t to allow using offset into struct)
    virtual size_t fieldKey() const = 0;
    /// identifies the container (form an API perspective, not C++ class as returned by getContainer())
    /// - in case the property is a container itself, the objectkey identifies that container
    /// - in case the property is a leaf value, the objectkey identifies the container of the property
    /// @note the mechanism allows having more than one single API object within one C++ object
    /// @note the type is a intptr_t to allow memory addresses to be used as object identifiers
    virtual intptr_t objectKey() const = 0;
    /// is array container
    virtual bool isArrayContainer() const = 0;
    /// is deletable
    virtual bool isDeletable() const { return false; /* usually not */ };
    /// needs preparation before accessing
    virtual bool needsPreparation(PropertyAccessMode aMode) const { return false; /* usually not */ };
    /// will be shown in wildcard queries
    virtual bool isWildcardAddressable() const { return true; };
    /// was created by the current write action
    virtual bool wasCreatedNew() const { return false; };
    /// acts as root of a C++ class hierarchy
    bool isRootOfObject() const { return rootOfObject; };
    /// checks
    bool hasObjectKey(char &aMemAddrObjectKey) { return (objectKey()==(intptr_t)&aMemAddrObjectKey); };
    bool hasObjectKey(intptr_t aIntObjectKey) { return (objectKey()==aIntObjectKey); };
    bool isStructured() { return type()==apivalue_object || isArrayContainer(); };
  };


  /// description of the root of any property access
  class RootPropertyDescriptor : public PropertyDescriptor
  {
    typedef PropertyDescriptor inherited;
    int apiVersion;
  public:
    RootPropertyDescriptor(int aApiVersion) : inherited(PropertyDescriptorPtr()) { rootOfObject = true; apiVersion = aApiVersion; };
    virtual const char *name() const P44_OVERRIDE { return "<root>"; };
    virtual ApiValueType type() const P44_OVERRIDE { return apivalue_object; };
    virtual size_t fieldKey() const P44_OVERRIDE { return 0; };
    virtual intptr_t objectKey() const P44_OVERRIDE { return 0; };
    virtual bool isArrayContainer() const P44_OVERRIDE { return false; };
    virtual int getApiVersion() const P44_OVERRIDE { return apiVersion; };
  };


  /// description of a static property (usually a named field described via a PropertyDescription in a const table)
  class StaticPropertyDescriptor : public PropertyDescriptor
  {
    typedef PropertyDescriptor inherited;
    const PropertyDescription *descP;

  public:

    /// create from const table entry
    StaticPropertyDescriptor(const PropertyDescription *aDescP, PropertyDescriptorPtr aParentDescriptor) :
      inherited(aParentDescriptor),
      descP(aDescP)
    {};

    virtual const char *name() const P44_OVERRIDE { return descP->propertyName; }
    virtual ApiValueType type() const P44_OVERRIDE { return (ApiValueType)((descP->propertyType) & proptype_mask); }
    virtual size_t fieldKey() const P44_OVERRIDE { return descP->fieldKey; }
    virtual intptr_t objectKey() const P44_OVERRIDE { return descP->objectKey; }
    virtual bool isArrayContainer() const P44_OVERRIDE { return descP->propertyType & propflag_container; };
    virtual bool isWildcardAddressable() const P44_OVERRIDE { return (descP->propertyType & propflag_nowildcard)==0; };
    virtual bool needsPreparation(PropertyAccessMode aMode) const P44_OVERRIDE { return (descP->propertyType & (aMode==access_read ? propflag_needsreadprep : propflag_needswriteprep))!=0; };
  };


  /// description of a dynamic property (such as an element of a container, created on the fly when accessed)
  class DynamicPropertyDescriptor : public PropertyDescriptor
  {
    typedef PropertyDescriptor inherited;

  public:
    DynamicPropertyDescriptor(PropertyDescriptorPtr aParentDescriptor) :
      inherited(aParentDescriptor),
      arrayContainer(false),
      deletable(false),
      needsReadPrep(false),
      needsWritePrep(false),
      createdNew(false)
    {};
    string propertyName; ///< name of the property
    ApiValueType propertyType; ///< type of the property value
    size_t propertyFieldKey; ///< key for accessing the property within its container
    intptr_t propertyObjectKey; ///< identifier for container
    bool arrayContainer;
    bool deletable;
    bool needsReadPrep;
    bool needsWritePrep;
    bool createdNew; ///< set for properties that were created new

    virtual const char *name() const P44_OVERRIDE { return propertyName.c_str(); }
    virtual ApiValueType type() const P44_OVERRIDE { return propertyType; }
    virtual size_t fieldKey() const P44_OVERRIDE { return propertyFieldKey; }
    virtual intptr_t objectKey() const P44_OVERRIDE { return propertyObjectKey; }
    virtual bool isArrayContainer() const P44_OVERRIDE { return arrayContainer; };
    virtual bool isDeletable() const P44_OVERRIDE { return deletable; };
    virtual bool needsPreparation(PropertyAccessMode aMode) const P44_OVERRIDE { return aMode==access_read ? needsReadPrep : needsWritePrep; };
    virtual bool wasCreatedNew() const P44_OVERRIDE { return createdNew; };
};




  typedef boost::intrusive_ptr<PropertyContainer> PropertyContainerPtr;

  typedef boost::function<void (ApiValuePtr aResultObject, ErrorPtr aError)> PropertyAccessCB;

  class PropertyPrep
  {
    friend class PropertyContainer;

    PropertyContainerPtr target;
    PropertyDescriptorPtr propertyDescriptor;

    PropertyPrep(PropertyContainerPtr aTarget, PropertyDescriptorPtr aDescriptor) : target(aTarget), propertyDescriptor(aDescriptor) {};
  };

  typedef list<PropertyPrep> PropertyPrepList;
  typedef boost::shared_ptr<PropertyPrepList> PropertyPrepListPtr;



  /// Base class for objects providing API properties
  /// Implements generic mechanisms to handle accessing elements and subtrees of named propeties.
  /// There is no strict relation between C++ classes of the framework and the property tree;
  /// a single C++ class can implement multiple levels of the property tree.
  /// PropertyContainer is also designed to allow subclasses adding property fields to the fields
  /// provided by base classes, without modifications of the base class.
  class PropertyContainer : public P44Obj
  {

  public:

    /// @name property access API
    /// @{

    /// read or write property
    /// @param aMode access mode (see PropertyAccessMode: read, write or write preload)
    /// @param aQueryObject the object defining the read or write query
    /// @param aDomain the access domain
    /// @param aApiVersion the API version relevant for this property access
    ///   (but will internally be repaced by a RootPropertyDescriptor)
    /// @param aAccessCompleteCB will be called when property access is complete. Callback's aError
    ///   returns Error 501 if property is unknown, 403 if property exists but cannot be accessed, 415 if value type is incompatible with the property
    void accessProperty(PropertyAccessMode aMode, ApiValuePtr aQueryObject, int aDomain, int aApiVersion, PropertyAccessCB aAccessCompleteCB);

    /// @}

    /// read properties from CSV formatted text
    /// @param aDomain the domain for which to access properties (different APIs might have different properties for the same PropertyContainer)
    /// @param aOnlyExplicitlyOverridden if set, only properties prefixed with an exclamation mark are applied
    /// @param aCSVCursor must point to a CSV formatted text which is parsed for propertypath/value pairs
    /// @param aTextSourceName (file)name of where the text comes from, for logging error messages
    /// @param aLineNo line number within the text source, for logging error messages
    /// @return true if some settings were applied
    bool readPropsFromCSV(int aDomain, bool aOnlyExplicitlyOverridden, const char *&aCSVCursor, const char *aTextSourceName, int aLineNo);

  protected:

    /// @name methods that should be overriden in concrete subclasses to access properties
    /// @{

    /// @return the number of properties in this container
    /// @param aDomain the domain for which to access properties (different APIs might have different properties for the same PropertyContainer)
    /// @param aParentDescriptor the descriptor of the parent property, never NULL
    ///   Allows single C++ class to handle multiple levels of nested property tree objects
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) { return 0; }

    /// get property descriptor by index.
    /// @param aPropIndex property index, 0..numProps()-1
    /// @param aDomain the domain for which to access properties (different APIs might have different properties for the same PropertyContainer)
    /// @param aParentDescriptor the descriptor of the parent property, never NULL
    /// @return pointer to property descriptor or NULL if aPropIndex is out of range
    /// @note base class always returns NULL, which means no properties
    /// @note implementation does not need to check aPropIndex, it will always be within the range set by numProps()
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) { return NULL; }

    /// get next property descriptor by name
    /// @param aPropMatch a plain property name, or a *-terminated wildcard, or a indexed access specifier #n, or empty for matching all
    /// @param aStartIndex on input: the property index to start searching, on exit: the next PropertyDescriptor to check.
    ///   When the search is exhausted, aStartIndex is set to PROPINDEX_NONE to signal there is no next property to check
    /// @param aDomain the domain for which to access properties (different APIs might have different properties for the same PropertyContainer)
    /// @param aMode access mode (containers that allow inserting will allow write-access to not-yet-existing container elements)
    /// @param aParentDescriptor the descriptor of the parent property, never NULL
    /// @return pointer to property descriptor or NULL if aPropIndex is out of range
    /// @note base class provides a default implementation which uses numProps/getDescriptorByIndex and compares names.
    ///   Subclasses may override this to more efficiently access array-like containers where aPropMatch can directly be used
    ///   to find an element (without iterating through all indices).
    virtual PropertyDescriptorPtr getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyAccessMode aMode, PropertyDescriptorPtr aParentDescriptor);

    /// get subcontainer for a apivalue_object property
    /// @param aPropertyDescriptor descriptor for a structured (object) property. Call might modify this pointer such as setting it to
    ///   NULL when new container is the root of another DsAdressable. The resulting pointer will be passed to the container's access
    ///   methods as aParentDescriptor.
    /// @param aDomain the domain for which to access properties. Call might modify the domain such that it fits
    ///   to the accessed container. For example, one container might support different sets of properties
    ///   (like description/settings/states for DsBehaviours)
    /// @return PropertyContainer representing the property or property array element
    /// @note base class always returns NULL, which means no structured or proxy properties
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) { return NULL; };

    /// prepare access to a property (for example if the property needs I/O to update its value before being read).
    /// @param aMode access mode (see PropertyAccessMode: read, write, write preload or delete)
    /// @param aPropertyDescriptor decriptor that signalled a need for preparation
    /// @param aPreparedCB will be called when property is ready to be accessed with aMode
    /// @note this base class implementation just calls aPreparedCB with success status.
    virtual void prepareAccess(PropertyAccessMode aMode, PropertyDescriptorPtr aPropertyDescriptor, StatusCB aPreparedCB);

    /// This is called when access to a property that needed preparation (see prepareAccess()) has been accessed.
    /// This can be used e.g. to free properties generated on the fly at prepareAccess(), or to perform
    /// @param aMode access mode (see PropertyAccessMode: read, write, write preload or delete)
    /// @param aPropertyDescriptor decriptor that signalled a need for preparation
    /// @note this is always called for to-be prepared properties, even if access itself was not successful
    /// @note this base class implementation does nothing
    virtual void finishAccess(PropertyAccessMode aMode, PropertyDescriptorPtr aPropertyDescriptor);

    /// access single field in this container
    /// @param aMode access mode (see PropertyAccessMode: read, write, write preload or delete)
    /// @param aPropValue JsonObject with a single value
    /// @param aPropertyDescriptor decriptor for a single value field in this container
    /// @return false if value could not be accessed
    /// @note this base class always returns false, as it does not have any properties implemented
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) { return false; };

    /// post-process written properties in subcontainers. This is called after a property write access has
    /// compleded successfully in a subcontainer (as returned by this object's getContainer()), and can be used to commit container
    /// wide transactions etc.
    /// @param aMode the property access mode (write or write_preload - for the latter, container might want to prevent committing, such as for MOC channel updates)
    /// @param aPropertyDescriptor decriptor for a structured (object) property
    /// @param aDomain the domain in which the write access happened
    /// @param aInContainer the container object that was accessed
    virtual ErrorPtr writtenProperty(PropertyAccessMode aMode, PropertyDescriptorPtr aPropertyDescriptor, int aDomain, PropertyContainerPtr aInContainer) { return ErrorPtr(); }

    /// @}

    /// @name utility methods
    /// @{

    /// internally read or write property
    /// @param aMode access mode (see PropertyAccessMode: read, write or write preload)
    /// @param aQueryObject the object defining the read or write query
    /// @param aResultObject for read, must be an object
    /// @param aDomain the access domain
    /// @param aParentDescriptor the descriptor of the parent property, must not be NULL
    /// @param aPreparationList if not NULL, this list will be filled with property descriptors that need preparation before accessing.
    ///   Otherwise, properties are assumed to be prepared already and will be accessed directly
    /// @return Error 501 if property is unknown, 403 if property exists but cannot be accessed, 415 if value type is incompatible with the property
    ErrorPtr accessPropertyInternal(PropertyAccessMode aMode, ApiValuePtr aQueryObject, ApiValuePtr aResultObject, int aDomain, PropertyDescriptorPtr aParentDescriptor, PropertyPrepListPtr aPreparationList);

    /// parse aPropmatch for numeric index (both plain number and #n are allowed, plus empty and "*" wildcards)
    /// @param aPropMatch property name to match
    /// @param aStartIndex current index, will be set to next matching index or PROPINDEX_NONE
    ///   if no next index available for this aPropMatch
    /// @return true if aPropMatch actually specifies a numeric name, false if aPropMatch is a wildcard
    ///   (The #n notation is not considered a numeric name!)
    bool getNextPropIndex(string aPropMatch, int &aStartIndex);

    /// @param aPropMatch property name to match
    /// @return true if aPropMatch specifies a name (vs. "*"/"" or "#n")
    bool isNamedPropSpec(string &aPropMatch);

    /// @param aPropMatch property name to match
    /// @return true if aPropMatch specifies a match-all wildcard ("*" or "")
    bool isMatchAll(string &aPropMatch);

    /// utility method to get next property descriptor in numerically addressed containers by numeric name
    /// @param aPropMatch a match-all wildcard (* or empty), a numeric name or indexed access specifier #n
    /// @param aStartIndex on input: the property index to start searching, on exit: the next PropertyDescriptor to check.
    ///   When the search is exhausted, aStartIndex is set to PROPINDEX_NONE to signal there is no next property to check
    /// @param aDomain the domain for which to access properties (different APIs might have different properties for the same PropertyContainer)
    /// @param aParentDescriptor the descriptor of the parent property, never NULL
    /// @param aObjectKey the object key the resulting descriptor should have
    /// @return pointer to property descriptor or NULL if aPropIndex is out of range
    /// @note the returned descriptor will have its fieldKey set to the index position of the to-be-accessed element,
    ///   and its type inherited from the parent descriptor
    PropertyDescriptorPtr getDescriptorByNumericName(
      string aPropMatch, int &aStartIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor,
      intptr_t aObjectKey
    );

    /// @}

  private:

    void prepareNext(PropertyPrepListPtr aPrepList, PropertyAccessMode aMode, ApiValuePtr aQueryObject, int aDomain, PropertyDescriptorPtr aParentDescriptor, PropertyAccessCB aAccessCompleteCB, ErrorPtr aError);



  };
  
} // namespace p44

#endif /* defined(__p44vdc__propertycontainer__) */
