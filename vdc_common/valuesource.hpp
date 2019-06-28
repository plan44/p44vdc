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

#ifndef __p44vdc__valuesource__
#define __p44vdc__valuesource__

#include "p44vdc_common.hpp"
#include "vdcapi.hpp"
#include "expressions.hpp"

using namespace std;

namespace p44 {


  class ValueSource;

  typedef enum {
    valueevent_confirmed, // value confirmed (but not changed)
    valueevent_changed, // value has changed
    valueevent_removed // value has been removed and may no longer be referenced
  } ValueListenerEvent;

  typedef boost::function<void (ValueSource &aValueSource, ValueListenerEvent aEvent)> ValueListenerCB;

  typedef multimap<void *,ValueListenerCB> ListenerMap;

  /// @note this class does NOT derive from P44Obj, so it can be added as "interface" using multiple-inheritance
  class ValueSource
  {

    // map of listeners
    ListenerMap listeners;

  public:

    /// constructor
    ValueSource();

    /// destructor
    virtual ~ValueSource();

    /// return true only if enabled for being used
    /// @return true if enabled for use (e.g. non-app buttons are not enabled)
    virtual bool isEnabled() { return true; /* enabled by default */ };

    /// get id - unique at least in the vdhost's scope
    /// @return id string, not containing = or : characters
    virtual string getSourceId() = 0;

    /// get descriptive name (for using in selection lists)
    /// @return descriptive name string
    virtual string getSourceName() = 0;

    /// get value
    /// @return the current value
    virtual double getSourceValue() = 0;

    /// get last update
    /// @return the timestamp of when the source was last updated. Never means that there is no current value
    virtual MLMicroSeconds getSourceLastUpdate() = 0;

    /// get operation level (how good/critical the operation state of the underlying device is)
    /// @return the operation level (0..100) of the value source
    virtual int getSourceOpLevel() = 0;

    /// add listener
    /// @param aCallback will be called when value has changed, or disappears
    /// @param aListener unique identification of the listener (usually its memory address)
    void addSourceListener(ValueListenerCB aCallback, void *aListener);

    /// remove listener
    /// @param aListener unique identification of the listener (usually its memory address)
    void removeSourceListener(void *aListener);

  protected:

    /// notify all listeners
    void notifyListeners(ValueListenerEvent aEvent);

  };



  class ValueSourceMapper
  {
    typedef map<string, ValueSource *> ValueSourcesMap;
    ValueSourcesMap valueMap;

  public:

    ValueSourceMapper();
    virtual ~ValueSourceMapper();

    /// forget current value mappings, unsubscribe from all value observations
    void forgetMappings();

    /// parse mapping definition string
    /// @param aMappingDefs string associating simple alias names with valuedefs IDs
    ///    Syntax is: <valuealias>:<valuesourceid> [, <valuealias>:valuesourceid> ...]
    /// @param aValueCallback will be called when any of the mapped values changes or is deleted
    /// @param aMigratedValueDefsP if not NULL, this string will be set empty if no migration is needed,
    ///    and contain the migrated valuedefs otherwise
    /// @note will cause current mappings to get overwritten (forgetMapping is called implicitly)
    /// @result returns true if all definitions could be mapped, false otherwise
    bool parseMappingDefs(const string &aValueDefs, ValueListenerCB aCallback, string *aMigratedValueDefsP = NULL);

    /// find value source by alias
    /// @param aAlias alias name
    /// @return NULL if not found, (temporary!) pointer to value source otherwise
    /// @note ValueSource pointer returned is not refcounted, valuesource object might
    ///   get deleted when control is passed to mainloop
    ValueSource* valueSourceByAlias(const string aAlias);

    /// get value (or meta information such as .age, .oplevel and .valid subfields) of mapped value source
    /// @param aVarSpec variable 
    ExpressionValue valueLookup(const string aVarSpec);

    /// get info about all mapped sources (everything needed for editing mappingdefs)
    /// @param the api object to add info for mappings to
    /// @return true if information could be added
    bool getMappedSourcesInfo(ApiValuePtr aInfoObject);




  };



} // namespace p44

#endif /* defined(__p44vdc__valuesource__) */
