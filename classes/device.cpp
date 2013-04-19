//
//  device.cpp
//  p44bridged
//
//  Created by Lukas Zeller on 18.04.13.
//  Copyright (c) 2013 plan44.ch. All rights reserved.
//

#include "device.hpp"

Device::Device() :
  registered(false),
  connected(false)
{

}


string Device::description()
{
  return string_format("Device with dsid = %s\n", dsid.getString().c_str());
}
