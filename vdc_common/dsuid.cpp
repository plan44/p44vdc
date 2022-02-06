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

#include "dsuid.hpp"
#include "fnv.hpp"

#ifdef __APPLE__
// OpenSSL is deprecated since 10.7 and not available in OSX 10.10 and later any more: using CommonCrypto instead
#include <CommonCrypto/CommonDigest.h>
#define SHA_DIGEST_LENGTH CC_SHA1_DIGEST_LENGTH
#define SHA_CTX CC_SHA1_CTX
#define SHA1_Init CC_SHA1_Init
#define SHA1_Update CC_SHA1_Update
#define SHA1_Final CC_SHA1_Final
#else
#include <openssl/sha.h>
#endif

using namespace p44;


// create empty

void DsUid::internalInit()
{
  mIdType = idtype_undefined;
  // init such that what we'd read out will be all-zero dSUID
  mIdBytes = dsuidBytes;
  memset(mRaw, 0, sizeof(mRaw));
}



DsUid::DsUid()
{
  internalInit();
}


bool DsUid::empty() const
{
  return mIdType==idtype_undefined;
}


void DsUid::clear()
{
  internalInit();
}




// Byte offset         0 1 2 3  4 5  6 7  8 9 101112131415 16
// dSUID with UUID  : xxxxxxxx-xxxx-Vxxx-Txxx-xxxxxxxxxxxx ii   (V=version, T=type/variant, ii=subdevice index)
// dSUID with EPC96 : ssssssss ssss 0000 0000 ssssssssssss ii   (ii=subdevice index)

void DsUid::setIdType(DsUidType aIdType)
{
  if (aIdType!=mIdType) {
    // new type, reset
    mIdType = aIdType;
    memset(mRaw, 0, sizeof(mRaw));
    switch (mIdType) {
      // dSUID
      case idtype_sgtin:
        mRaw[0] = SGTIN96Header;
        // fall through
      case idtype_uuid:
      case idtype_gid:
        mIdBytes = dsuidBytes;
        break;
      default:
        mIdBytes = 0; // no content
        break;
    }
  }
}


void DsUid::setSubdeviceIndex(uint8_t aSubDeviceIndex)
{
  if (mIdBytes==dsuidBytes) {
    // is a dSUID, can set subdevice index
    mRaw[16] = aSubDeviceIndex;
  }
}



// MARK: - set SGTIN based dSUID from parameters

// 1. vDC can determine GTIN and SerialNumber of Device → combine GTIN and SerialNumber to a SGTIN

// SGTIN96 binary:
//      hhhhhhhh fffpppgg gggggggg gggggggg gggggggg gggggggg gggggggg ggssssss ssssssss ssssssss ssssssss ssssssss
//      00110000 001ppp<--------- 44 bit binary GCP+ItemRef ------------><------- 38 bit serial number ----------->
// dSUID Byte index:
//         0        1         2        3       4        5        10        11      12       13       14       15


// translation table to get GCP bit length for partition value
// Note: Partition Value + 1 = number of decimal digits for item reference including indicator/pad digit
static uint8_t gcpBitLength[7] = { 40, 37, 34, 30, 27, 24, 20 };

void DsUid::setGTIN(uint64_t aGCP, uint64_t aItemRef, uint8_t aPartition)
{
  // setting GTIN switches to sgtin dSUID
  setIdType(idtype_sgtin);
  // total bit length for CGP + itemRef combined are 44bits
  uint64_t binaryGtin = aGCP<<(44-gcpBitLength[aPartition]) | aItemRef;
  // now put into bytes
  // - filter (fixed to 1), partition and upper 2 bits of binaryGtin go into raw[1]
  mRaw[1] = (0x1<<5) | ((aPartition&0x07)<<2) | (binaryGtin>>42);
  // - raw[2..5]
  mRaw[2] = (binaryGtin>>34) & 0xFF;
  mRaw[3] = (binaryGtin>>26) & 0xFF;
  mRaw[4] = (binaryGtin>>18) & 0xFF;
  mRaw[5] = (binaryGtin>>10) & 0xFF;
  // - raw[6..9] are left 0 to mark it as non-UUID
  // - raw[10..11] contain more GTIN information
  mRaw[10] = (binaryGtin>>2) & 0xFF;
  mRaw[11] = (mRaw[11] & 0x3F) | ((binaryGtin & 0x03)<<6); // combine lowest 2 bits of GTIN with highest 6 of serial
}


void DsUid::setSerial(uint64_t aSerial)
{
  // setting GTIN switches to sgtin dSUID
  setIdType(idtype_sgtin);
  mRaw[11] = (mRaw[11] & 0xC0) | ((aSerial>>32)&0x3F); // combine lowest 2 bits of GTIN with highest 6 of serial
  mRaw[12] = (aSerial>>24)&0xFF;
  mRaw[13] = (aSerial>>16)&0xFF;
  mRaw[14] = (aSerial>>8)&0xFF;
  mRaw[15] = aSerial&0xFF;
}



// MARK: - set UUID based DsUid from parameters


// 2. vDC can determine an existing UUID of Device → use existing UUID
// 3. vDC can determine a unique ID of the Device within the kind of the device → generate name based UUIDv5 with unique ID and predefined name space UUID
// 4. vDC can determine a locally unique ID: generate UUIDv5 with local ID and vDC UUID as name space
// 5. vDC can determine MAC address of Device: generate UUIDv1 (MAC based)

// Device kind Name spaces (UUID v4, randomly generated):
//   EnOcean: DSUID_ENOCEAN_NAMESPACE_UUID (0ba94a7b-7c92-4dab-b8e3-5fe09e83d0f3)


// UUID format (see RFC 4122 for details):
//
// 0ba94a7b-7c92-4dab-b8e3-5fe09e83d0f3  // example, EnOcean namespace UUID (v4, random)
//
// tltltltl-tmtm-thth-chcl-nononononono
// xxxxxxxx-xxxx-Vxxx-Txxx-xxxxxxxxxxxx
//
// where
//  tl : time_low[4]
//  tm : time_mid[2];
//  th : time_hi_and_version[2];
//  ch : clock_seq_hi_and_reserved[1];
//  cl : clock_seq_low[1];
//  no : node[6];
//
//  xxxxx : actual ID bits
//  V : 4 bit version number (v1, v4, v5 etc.)
//  T : type/variant, 2 upper bits must be 0b10 (i.e. Bit7 of clock_seq_hi_and_reserved = 1, Bit6 of clock_seq_hi_and_reserved = 0)



// Note: We do *not* store the fields above in machine byte order. Internal
//   representation in raw.uuid[] is always network byte order, so it
//   can be directly fed into hashing algorithms (RFC demands hashes are
//   always calculated over network byte order representation).


void DsUid::setNameInSpace(const string &aName, const DsUid &aNameSpace)
{
  uint8_t sha1[SHA_DIGEST_LENGTH]; // buffer for calculating SHA1
  SHA_CTX sha_context;

  // setting name in namespace switches to UUID dSUID
  setIdType(idtype_uuid);
  // calculate the hash used as basis for a UUIDv5
  SHA1_Init(&sha_context);
  // - hash the name space UUID
  SHA1_Update(&sha_context, &aNameSpace.mRaw, uuidBytes);
  // - hash the name
  SHA1_Update(&sha_context, aName.c_str(), (unsigned int)aName.size());
  SHA1_Final(sha1, &sha_context);
  // Now make UUID of it
  // - Set octets zero through 3 of the time_low field to octets zero through 3 of the hash.
  // - Set octets zero and one of the time_mid field to octets 4 and 5 of the hash.
  // - Set octets zero and one of the time_hi_and_version field to octets 6 and 7 of the hash.
  // - Set the clock_seq_hi_and_reserved field to octet 8 of the hash.
  // - Set the clock_seq_low field to octet 9 of the hash.
  // - Set octets zero through five of the node field to octets 10 through 15 of the hash.
  // ...which means: copy byte 0..15 of the sha1 into the UUID bytes 0..15
  memcpy(mRaw, sha1, uuidBytes);
  // Then:
  // - Set the four most significant bits (bits 12 through 15) of the time_hi_and_version field to the appropriate 4-bit version number from Section 4.1.3.
  // ...means: set the UUID version, is 0x5 here
  mRaw[6] = (mRaw[6] & 0x0F) | (0x5<<4);
  // - Set the two most significant bits (bits 6 and 7) of the clock_seq_hi_and_reserved to zero and one, respectively.
  // ...means: mark the UUID as RFC4122 type/variant
  mRaw[8] = (mRaw[8] & 0xC0) | (0x2<<6);
}



// MARK: - binary string (bytes) representation

bool DsUid::setAsBinary(const string &aBinary)
{
  if (aBinary.size()==dsuidBytes) {
    mIdBytes = dsuidBytes;
    memcpy(mRaw, aBinary.c_str(), mIdBytes);
    detectSubType();
    return true;
  }
  return false;
}


string DsUid::getBinary() const
{
  return string((const char *)mRaw,mIdBytes);
}



// MARK: - set/create DsUid from string representation


DsUid::DsUid(const string &aString)
{
  internalInit();
  setAsString(aString);
}


DsUid::DsUid(const char *aString)
{
  internalInit();
  setAsString(aString);
}


void DsUid::detectSubType()
{
  if (mRaw[6]==0 && mRaw[7]==0 && mRaw[8]==0 && mRaw[9]==0) {
    // EPC96, check which one
    if (mRaw[0]==SGTIN96Header)
      mIdType = idtype_sgtin;
    else if (mRaw[0]==GID96Header)
      mIdType = idtype_gid;
  }
  else {
    // UUID
    mIdType = idtype_uuid;
  }
}


bool DsUid::setAsString(const string &aString)
{
  const char *p = aString.c_str();
  int byteIndex = 0;
  uint8_t b = 0;
  bool firstNibble = true;
  bool hasDashes = false;
  char c;
  while ((c = *p++)!=0 && byteIndex<dsuidBytes) {
    if (c=='-') {
      hasDashes = true; // a dash has occurred, might be a pure UUID (without 17th byte)
      continue; // dashes allowed but ignored
    }
    c = toupper(c)-'0';
    if (c>9) c -= ('A'-'9'-1);
    if (c<0 || c>0xF)
      break; // invalid char, done
    if (firstNibble) {
      b = c<<4;
      firstNibble = false;
    }
    else {
      b |= c;
      mRaw[byteIndex++]=b;
      firstNibble = true;
    }
  }
  // determine type of dSUID
  if (byteIndex==dsuidBytes || (hasDashes && byteIndex==uuidBytes)) {
    // must be a dSUID (when read with dashes, it can also be a pure UUID without the subdevice index byte)
    mIdType = idtype_other;
    mIdBytes = dsuidBytes;
    // - determine subtype
    detectSubType();
    if (byteIndex==uuidBytes)
      mRaw[16] = 0; // specified as pure UUID, set subdevice index == 0
  }
  else {
    // unknown format
    setIdType(idtype_undefined);
    return false;
  }
  return true;
}



// MARK: - getting dSUID string representation



string DsUid::getString() const
{
  string s;
  for (int i=0; i<mIdBytes; i++) {
    string_format_append(s, "%02X", mRaw[i]);
  }
  return s;
}


// MARK: - comparison


bool DsUid::operator== (const DsUid &aDsUid) const
{
  if (mIdType!=aDsUid.mIdType) return false;
  return memcmp(mRaw, aDsUid.mRaw, mIdBytes)==0;
}


bool DsUid::operator< (const DsUid &aDsUid) const
{
  if (mIdType==aDsUid.mIdType)
    return memcmp(mRaw, aDsUid.mRaw, mIdBytes)<0;
  else
    return mIdType<aDsUid.mIdType;
}


// MARK: - utilities


void DsUid::xorDsUidIntoMix(string &aMix, bool aHashSubDeviceIndex)
{
  string b = getBinary(); // will always be of size dsuidBytes
  if (aHashSubDeviceIndex && mIdBytes==dsuidBytes) {
    // add a fnv hash over the complete dsuid including subdevice index into the last 4 bytes
    // (this reduces collision probability when mixing multiple subdevices from the same device)
    Fnv32 fnv;
    fnv.addBytes(b.size(), (uint8_t *)b.c_str());
    uint32_t h = fnv.getHash();
    for (size_t i=dsuidBytes-4; i<dsuidBytes; i++) {
      b[i] = b[i] ^ (h&0xFF);
      h = h>>8;
    }
  }
  if (aMix.empty()) {
    aMix = b;
  }
  else {
    // xor into mix, order of mixed components does not matter
    for (size_t i=0; i<dsuidBytes; i++) {
      if (i<aMix.size())
        aMix[i] = aMix[i] ^ b[i];
      else
        aMix.append(1, b[i]); // mix was too short, append extra chars
    }
  }
}














