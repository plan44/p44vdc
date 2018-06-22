//
//  Copyright (c) 2017 digitalSTROM.org, Zurich, Switzerland
//
//  Author: Pawel Kochanowski <pawel.kochanowski@digitalstrom.com>
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

#include "homeconnectdevicedryer.hpp"
#include "homeconnectaction.hpp"

#if ENABLE_HOMECONNECT

#include "homeconnectaction.hpp"

namespace p44 {

namespace
{
  static const string DRYER_CONFIG_FILE_NAME = HOMECONNECT_CONFIG_FILE_NAME_BASE + "Dryer";
}

HomeConnectDeviceDryer::HomeConnectDeviceDryer(HomeConnectVdc *aVdcP, JsonObjectPtr aHomeApplicanceInfoRecord) :
    inherited(aVdcP, aHomeApplicanceInfoRecord, DRYER_CONFIG_FILE_NAME)
{
  HomeConnectDeviceSettingsPtr settings = new HomeConnectDeviceSettings(*this);
  settings->fireAction = "Stop";

  installSettings(settings);
}

HomeConnectDeviceDryer::~HomeConnectDeviceDryer()
{
  // TODO Auto-generated destructor stub
}

void HomeConnectDeviceDryer::configureDevice(StatusCB aStatusCB)
{
  addProgramNameProperty();
  // configure operation mode
  OperationModeConfiguration omConfig = { 0 };
  omConfig.hasInactive = false;
  omConfig.hasReady = true;
  omConfig.hasDelayedStart = true;
  omConfig.hasRun = true;
  omConfig.hasPause = true;
  omConfig.hasActionrequired = true;
  omConfig.hasFinished = true;
  omConfig.hasError = true;
  omConfig.hasAborting = false;
  configureOperationModeState(omConfig);

  // configure remote control
  RemoteControlConfiguration rcConfig = { 0 };
  rcConfig.hasControlInactive = true;
  rcConfig.hasControlActive = true;
  rcConfig.hasStartActive = true;
  configureRemoteControlState(rcConfig);

  // configure door state
  DoorStateConfiguration dsConfig = { 0 };
  dsConfig.hasOpen = true;
  dsConfig.hasClosed = true;
  dsConfig.hasLocked = false;
  configureDoorState(dsConfig);

  // configure program status properties
  ProgramStatusConfiguration progStatusConfig = { 0 };
  progStatusConfig.hasElapsedTime = false;
  progStatusConfig.hasRemainingTime = true;
  progStatusConfig.hasProgres = true;
  configureProgramStatus(progStatusConfig);

  EventConfiguration eventConfig = { 0 };
  eventConfig.hasAlarmClockElapsed = false;
  eventConfig.hasLocallyOperated = true;
  eventConfig.hasProgramAborted = false;
  eventConfig.hasProgramFinished = true;
  eventConfig.hasProgramStarted = true;
  configureEvents(eventConfig);

  EnumValueDescriptorPtr dryingTargetCottonSynthetic = EnumValueDescriptorPtr(new EnumValueDescriptor("DryingTarget", true));
  int i = 0;
  dryingTargetCottonSynthetic->addEnum("IronDry", i++);
  dryingTargetCottonSynthetic->addEnum("CupboardDry", i++, true);
  dryingTargetCottonSynthetic->addEnum("CupboardDryPlus", i);

  EnumValueDescriptorPtr dryingTargetMix = EnumValueDescriptorPtr(new EnumValueDescriptor("DryingTarget", true));
  i = 0;
  dryingTargetMix->addEnum("IronDry", i++);
  dryingTargetMix->addEnum("CupboardDry", i, true);

  addAction("Cotton",    "Cotton",    "Cotton",    dryingTargetCottonSynthetic);
  addAction("Synthetic", "Synthetic", "Synthetic", dryingTargetCottonSynthetic);
  addAction("Mix",       "Mix",       "Mix",       dryingTargetMix);

  addDefaultStopAction();


  dryingTargetProp = EnumValueDescriptorPtr(new EnumValueDescriptor("DryingTarget", true));
  i = 0;
  dryingTargetProp->addEnum("IronDry", i++, false);
  dryingTargetProp->addEnum("CupboardDry", i++, false);
  dryingTargetProp->addEnum("CupboardDryPlus", i++, false);

  deviceProperties->addProperty(dryingTargetProp);
  aStatusCB(Error::ok());
}

void HomeConnectDeviceDryer::stateChanged(DeviceStatePtr aChangedState, DeviceEventsList &aEventsToPush)
{
  inherited::stateChanged(aChangedState, aEventsToPush);
}

void HomeConnectDeviceDryer::handleEventTypeNotify(const string& aKey, JsonObjectPtr aValue)
{
  ALOG(LOG_INFO, "Dryer Event 'NOTIFY' - item: %s, %s", aKey.c_str(), aValue ? aValue->c_strValue() : "<none>");

  if (aKey == "LaundryCare.Dryer.Option.DryingTarget") {
    string value = (aValue != NULL) ? aValue->stringValue() : "";
    dryingTargetProp->setStringValueCaseInsensitive(removeNamespace(value));
    return;
  }

  inherited::handleEventTypeNotify(aKey, aValue);
}

void HomeConnectDeviceDryer::addAction(const string& aActionName, const string& aDescription, const string& aProgramName, ValueDescriptorPtr aParameter)
{
  HomeConnectProgramBuilder builder("LaundryCare.Dryer.Program." + aProgramName);
  builder.addOption("LaundryCare.Dryer.Option.DryingTarget", "\"LaundryCare.Dryer.EnumType.DryingTarget.@{DryingTarget}\"");

  HomeConnectActionPtr action = HomeConnectActionPtr(new HomeConnectRunProgramAction(*this,
                                                                                     *operationModeDescriptor,
                                                                                     aActionName,
                                                                                     aDescription,
                                                                                     builder.build()));
  action->addParameter(aParameter);
  deviceActions->addAction(action);
}

bool HomeConnectDeviceDryer::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("homeconnect_dryer", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}

} /* namespace p44 */

#endif // ENABLE_HOMECONNECT
