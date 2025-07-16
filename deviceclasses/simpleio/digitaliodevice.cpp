//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "digitaliodevice.hpp"

#if ENABLE_STATIC

#include "buttonbehaviour.hpp"
#include "binaryinputbehaviour.hpp"
#include "lightbehaviour.hpp"
#include "shadowbehaviour.hpp"

using namespace p44;

#define INPUT_DEBOUNCE_TIME (25*MilliSecond)

DigitalIODevice::DigitalIODevice(StaticVdc *aVdcP, const string &aDeviceConfig) :
  StaticDevice((Vdc *)aVdcP),
  mDigitalIoType(digitalio_unknown)
{
  // Config is:
  //  <pin(s) specification>:<behaviour mode>
  //  - where pin specification describes the actual I/Os to be used (see DigitialIO)
  // last : separates behaviour from pin specification (so pins specs containing colons are possible, such as OW LEDs)
  size_t i = aDeviceConfig.rfind(":");
  string ioname = aDeviceConfig;
  string upName;
  string downName;
  if (i!=string::npos) {
    ioname = aDeviceConfig.substr(0,i);
    string mode = aDeviceConfig.substr(i+1,string::npos);
    // Still handle old-style inverting with !-prefixed mode (because Web-UI created those, we don't want to break them)
    if (mode[0]=='!') {
      ioname.insert(0, "/");
      mode.erase(0,1);
    }
    if (mode=="button")
      mDigitalIoType = digitalio_button;
    else if (mode=="input")
      mDigitalIoType = digitalio_input;
    else if (mode=="light")
      mDigitalIoType = digitalio_light;
    else if (mode=="relay") {
      mDigitalIoType = digitalio_relay;
    } else if (mode=="blind") {
      // ioname = "<upPinSpec>:<downPinSpec>"
      size_t t = ioname.find(":");
      if (t != string::npos) {
        mDigitalIoType = digitalio_blind;
        upName = ioname.substr(0, t);
        downName = ioname.substr(t+1, string::npos);
      } else {
        LOG(LOG_ERR, "Illegal output specification for blinds: %s", ioname.c_str());
      }
    }
    else {
      LOG(LOG_ERR, "unknown digital IO type: %s", mode.c_str());
    }
  }
  // basically act as black device so we can configure colors
  if (mDigitalIoType==digitalio_button) {
    mColorClass = class_black_joker;
    // Standard device settings without scene table
    installSettings();
    // Digital input as button
    mButtonInput = ButtonInputPtr(new ButtonInput(ioname.c_str()));
    mButtonInput->setButtonHandler(boost::bind(&DigitalIODevice::buttonHandler, this, _1, _2), true);
    // - create one button input
    ButtonBehaviourPtr b = ButtonBehaviourPtr(new ButtonBehaviour(*this,"")); // automatic id
    b->setHardwareButtonConfig(0, buttonType_undefined, buttonElement_center, false, 0, 1); // not combinable, but mode not restricted
    b->setHardwareName("digitalin");
    b->setGroup(group_yellow_light); // pre-configure for light
    addBehaviour(b);
  }
  else if (mDigitalIoType==digitalio_input) {
    mColorClass = class_black_joker;
    // Standard device settings without scene table
    installSettings();
    // Digital input as binary input (AKM, automation block type)
    mDigitalInput = DigitalIoPtr(new DigitalIo(ioname.c_str(), false));
    mDigitalInput->setInputChangedHandler(boost::bind(&DigitalIODevice::inputHandler, this, _1), INPUT_DEBOUNCE_TIME, 0); // edge detection if possible, mainloop idle poll otherwise
    // - create one binary input
    BinaryInputBehaviourPtr b = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*this,"")); // automatic id
    b->setHardwareInputConfig(binInpType_none, usage_undefined, true, Never, Never);
    b->setHardwareName("digitalin");
    addBehaviour(b);
    // make sure we sample the actual input state right at the beginning
    b->updateInputState(mDigitalInput->isSet());
  }
  else if (mDigitalIoType==digitalio_light) {
    // Digital output as light on/off switch
    mColorClass = class_yellow_light;
    mIndicatorOutput = IndicatorOutputPtr(new IndicatorOutput(ioname.c_str(), false));
    // - use light settings, which include a scene table
    installSettings(DeviceSettingsPtr(new LightDeviceSettings(*this)));
    // - add simple single-channel light behaviour
    LightBehaviourPtr l = LightBehaviourPtr(new LightBehaviour(*this));
    l->setHardwareOutputConfig(outputFunction_switch, outputmode_binary, usage_undefined, false, -1);
    l->setHardwareName("digitalout");
    addBehaviour(l);
  }
  else if (mDigitalIoType==digitalio_relay) {
    mColorClass = class_black_joker;
    // - standard device settings with scene table
    installSettings(DeviceSettingsPtr(new SceneDeviceSettings(*this)));
    // Digital output
    mIndicatorOutput = IndicatorOutputPtr(new IndicatorOutput(ioname.c_str(), false));
    // - add generic output behaviour
    OutputBehaviourPtr o = OutputBehaviourPtr(new OutputBehaviour(*this));
    o->setHardwareOutputConfig(outputFunction_switch, outputmode_binary, usage_undefined, false, -1);
    o->setHardwareName("digitalout");
    o->setGroupMembership(group_black_variable, true); // put into joker group by default
    o->addChannel(ChannelBehaviourPtr(new DigitalChannel(*o, "relay")));
    addBehaviour(o);
  }
  else if (mDigitalIoType==digitalio_blind) {
    mColorClass = class_grey_shadow;
    installSettings(DeviceSettingsPtr(new ShadowDeviceSettings(*this)));
    mBlindsOutputUp = DigitalIoPtr(new DigitalIo(upName.c_str(), true, false));
    mBlindsOutputDown = DigitalIoPtr(new DigitalIo(downName.c_str(), true, false));
    ShadowBehaviourPtr s = ShadowBehaviourPtr(new ShadowBehaviour(*this));
    s->setHardwareName("dual_digitalout");
    s->setHardwareOutputConfig(outputFunction_positional, outputmode_gradual, usage_room, false, -1);
    s->setDeviceParams(shadowdevice_rollerblind, false, 500*MilliSecond);
    s->mPosition->setFullRangeTime(40*Second);
    s->mPosition->syncChannelValue(100, false, true); // assume fully up at beginning
    addBehaviour(s);
  }
	deriveDsUid();
}


void DigitalIODevice::buttonHandler(bool aNewState, MLMicroSeconds aTimestamp)
{
	ButtonBehaviourPtr b = getButton(0);
	if (b) {
		b->updateButtonState(aNewState);
	}
}


void DigitalIODevice::inputHandler(bool aNewState)
{
  BinaryInputBehaviourPtr b = getInput(0);
  if (b) {
    b->updateInputState(aNewState);
  }
}


void DigitalIODevice::applyChannelValues(SimpleCB aDoneCB, bool aForDimming)
{
  LightBehaviourPtr lightBehaviour = getOutput<LightBehaviour>();
  ShadowBehaviourPtr shadowBehaviour = getOutput<ShadowBehaviour>();
  if (lightBehaviour) {
    // light
    if (lightBehaviour->brightnessNeedsApplying()) {
      mIndicatorOutput->set(lightBehaviour->brightnessForHardware(true)); // final value
      lightBehaviour->brightnessApplied(); // confirm having applied the value
    }
  }
  else if (shadowBehaviour) {
    // ask shadow behaviour to start movement sequence
    shadowBehaviour->applyBlindChannels(boost::bind(&DigitalIODevice::changeMovement, this, _1, _2), aDoneCB, aForDimming);
    return;
  }
  else if (getOutput()) {
    // simple switch output, activates at 50% of possible output range
    ChannelBehaviourPtr ch = getOutput()->getChannelByIndex(0);
    if (ch->needsApplying()) {
      mIndicatorOutput->set(ch->getChannelValueBool());
      ch->channelValueApplied();
    }
  }
  inherited::applyChannelValues(aDoneCB, aForDimming);
}


void DigitalIODevice::syncChannelValues(SimpleCB aDoneCB)
{
  ShadowBehaviourPtr sb = getOutput<ShadowBehaviour>();
  if (sb) {
    sb->syncBlindState();
  }
  if (aDoneCB) aDoneCB();
}


void DigitalIODevice::dimChannel(ChannelBehaviourPtr aChannel, VdcDimMode aDimMode, bool aDoApply)
{
  // start dimming
  ShadowBehaviourPtr sb = getOutput<ShadowBehaviour>();
  if (sb && aDoApply) {
    // no channel check, there's only global dimming of the blind, no separate position/angle
    sb->dimBlind(boost::bind(&DigitalIODevice::changeMovement, this, _1, _2), aDimMode);
  } else {
    inherited::dimChannel(aChannel, aDimMode, aDoApply);
  }
}


void DigitalIODevice::changeMovement(SimpleCB aDoneCB, int aNewDirection)
{
  if (aNewDirection == 0) {
    // stop
    mBlindsOutputUp->set(false);
    mBlindsOutputDown->set(false);
  } else if (aNewDirection > 0) {
    mBlindsOutputDown->set(false);
    mBlindsOutputUp->set(true);
  } else {
    mBlindsOutputUp->set(false);
    mBlindsOutputDown->set(true);
  }
  if (aDoneCB) {
    aDoneCB();
  }
}

string DigitalIODevice::blindsName() const
{
  if (mBlindsOutputUp && mBlindsOutputDown) {
    return string_format("%s+%s", mBlindsOutputUp->getName().c_str(), mBlindsOutputDown->getName().c_str());
  }
  return "";
}


void DigitalIODevice::deriveDsUid()
{
  // vDC implementation specific UUID:
  //   UUIDv5 with name = classcontainerinstanceid::ioname[:ioname ...]
  DsUid vdcNamespace(DSUID_P44VDC_NAMESPACE_UUID);
  string s = mVdcP->vdcInstanceIdentifier();
  s += ':';
  if (mButtonInput) { s += ":"; s += mButtonInput->getName(); }
  if (mIndicatorOutput) { s += ":"; s += mIndicatorOutput->getName(); }
  if (mDigitalInput) { s += ":"; s += mDigitalInput->getName(); }
  if (mBlindsOutputUp && mBlindsOutputDown) { s += ":"; s += blindsName(); }
  mDSUID.setNameInSpace(s, vdcNamespace);
}


string DigitalIODevice::modelName()
{
  switch (mDigitalIoType) {
    case digitalio_button: return "Button digital input";
    case digitalio_input: return "Binary digital input";
    case digitalio_light: return "Light controlling output";
    case digitalio_relay: return "Relay controlling output";
    case digitalio_blind: return "Blind controlling output";
    default: return "Digital I/O";
  }
}


string DigitalIODevice::getExtraInfo()
{
  if (mButtonInput)
    return string_format("Button: %s\n", mButtonInput->getName().c_str());
  else if (mDigitalInput)
    return string_format("Input: %s\n", mDigitalInput->getName().c_str());
  else if (mIndicatorOutput)
    return string_format("Output: %s\n", mIndicatorOutput->getName().c_str());
  else if (mBlindsOutputUp && mBlindsOutputDown)
    return string_format("Outputs: %s\n", blindsName().c_str());
  else
    return "?";
}



string DigitalIODevice::description()
{
  string s = inherited::description();
  if (mButtonInput)
    string_format_append(s, "\n- Button at Digital IO '%s'", mButtonInput->getName().c_str());
  if (mDigitalInput)
    string_format_append(s, "\n- Input at Digital IO '%s'", mDigitalInput->getName().c_str());
  if (mIndicatorOutput)
    string_format_append(s, "\n- Switch output at Digital IO '%s'", mIndicatorOutput->getName().c_str());
  if (mBlindsOutputUp && mBlindsOutputDown)
    string_format_append(s, "\n Blinds output at Digital IO %s", blindsName().c_str());
  return s;
}


#endif // ENABLE_STATIC

