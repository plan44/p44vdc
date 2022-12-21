//
//  Copyright (c) 2013-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__analogiodevice__
#define __p44vdc__analogiodevice__

#include "device.hpp"

#if ENABLE_STATIC

#include "analogio.hpp"
#include "staticvdc.hpp"

using namespace std;

namespace p44 {

  class StaticVdc;
  class AnalogIODevice;
  typedef boost::intrusive_ptr<AnalogIODevice> AnalogIODevicePtr;

  class AnalogIODevice : public StaticDevice
  {
    typedef StaticDevice inherited;

    typedef enum {
      analogio_unknown,
      analogio_dimmer,
      analogio_rgbdimmer,
      analogio_cwwwdimmer,
      analogio_valve,
      analogio_sensor
    } AnalogIoType;

    AnalogIoPtr analogIO; // brighness for single channel, red for RGB
    AnalogIoPtr analogIO2; // green for RGB
    AnalogIoPtr analogIO3; // blue for RGB
    AnalogIoPtr analogIO4; // white for RGBW

    AnalogIoType analogIOType;

    MLTicket timerTicket; // for output transitions and input poll
    double scale; ///< scaling factor for analog sensors (native value will be multiplied by this)
    double offset; ///< offset for analog sensors (reported value = native*scale+offset)

  public:
    AnalogIODevice(StaticVdc *aVdcP, const string &aDeviceConfig);
    virtual ~AnalogIODevice();

    /// device type identifier
    /// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const { return "analogio"; };

    /// description of object, mainly for debug and logging
    /// @return textual description of object
    virtual string description();

    /// Get extra info (plan44 specific) to describe the addressable in more detail
    /// @return string, single line extra info describing aspects of the device not visible elsewhere
    virtual string getExtraInfo();

    /// @name interaction with subclasses, actually representing physical I/O
    /// @{

    /// apply all pending channel value updates to the device's hardware
    /// @note this is the only routine that should trigger actual changes in output values. It must consult all of the device's
    ///   ChannelBehaviours and check isChannelUpdatePending(), and send new values to the device hardware. After successfully
    ///   updating the device hardware, channelValueApplied() must be called on the channels that had isChannelUpdatePending().
    /// @param aCompletedCB if not NULL, must be called when values are applied
    /// @param aForDimming hint for implementations to optimize dimming, indicating that change is only an increment/decrement
    ///   in a single channel (and not switching between color modes etc.)
    virtual void applyChannelValues(SimpleCB aDoneCB, bool aForDimming);

    /// @}


    /// @name identification of the addressable entity
    /// @{

    /// @return human readable model name/short description
    virtual string modelName();

    /// @}

  protected:

    void deriveDsUid();

  private:

    void analogInputPoll(MLTimer &aTimer, MLMicroSeconds aNow);
    virtual void applyChannelValueSteps(bool aForDimming);

  };

} // namespace p44

#endif // ENABLE_STATIC
#endif // __p44vdc__analogiodevice__
