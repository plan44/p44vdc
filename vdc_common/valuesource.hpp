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

#ifndef __p44vdc__valuesource__
#define __p44vdc__valuesource__

#include "p44vdc_common.hpp"

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
    ~ValueSource();

    /// get descriptive name (for using in selection lists)
    virtual string getSourceName() = 0;

    /// get value
    virtual double getSourceValue() = 0;

    /// get last update
    virtual MLMicroSeconds getSourceLastUpdate() = 0;

    /// get operation level (how good/critical the operation state of the underlying device is)
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




} // namespace p44

#endif /* defined(__p44vdc__valuesource__) */
