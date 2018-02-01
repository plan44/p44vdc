//
//  Copyright (c) 2017 digitalSTROM.org, Zurich, Switzerland
//
//  Author: Krystian Heberlein <krystian.heberlein@digitalstrom.com>
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

#include "httpclient.hpp"

#if ENABLE_NETATMO_V2

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 7

#include "logger.hpp"
#include <string>
#include <sstream>


using namespace p44;


// MARK: ===== httputils


int httputils::getChunkSize(std::istringstream& iss)
{
  std::string chunkSizeStr;
  int chunkSize = 0;

  // read chunk size
  if (std::getline(iss, chunkSizeStr)) {
    trimWhiteSpace(chunkSizeStr);
    chunkSize = std::strtoul(chunkSizeStr.c_str(), NULL, 16);
    LOG(LOG_DEBUG, "chunk size: %d", chunkSize);
    return chunkSize;
  }

  return -1;
}


std::string httputils::decodeChunkData(const std::string& aChunkedData)
{
  std::istringstream iss (aChunkedData);
  std::string chunkSizeStr;
  int chunkSize = 0;

  // read chunk size
  chunkSize = getChunkSize(iss);

  std::string decodedData = "";

  while (chunkSize >= 0) {
    // get next character and check if is valid
    char c = iss.get();
    if (!iss.good()) break;

    if (c == '\r') {
      string line;
      c = iss.get();
      chunkSize = getChunkSize(iss);
      c = iss.get();
      if (chunkSize <= 0) break;
    }
    chunkSize--;
    decodedData += c;
  }

  LOG(LOG_DEBUG, "Decoded chunked data: '%s' chunk data left: '%d'", decodedData.c_str(), chunkSize);

  return decodedData;
}


// MARK: ===== HttpOperation


HttpOperation::HttpOperation(
    HttpClient &aHttpClient,
    const string& aMethod,
    const string& aUrl,
    const string& aRequestBody,
    HttpCommCB aResultHandler
) :
  httpClient(aHttpClient),
  method(aMethod),
  url(aUrl),
  requestBody(aRequestBody),
  resultHandler(aResultHandler),
  completed(false)
{

}


bool HttpOperation::initiate()
{
  if (!canInitiate())
    return false;
  // can initiate, so process a request
  sendRequest();
  // mark operation as initiated
  return inherited::initiate();
}


void HttpOperation::processAnswer(const string& aResponse, ErrorPtr aError)
{
  // save response data and error
  error = aError;
  response = aResponse;
  // mark as completed
  completed = true;
}


bool HttpOperation::hasCompleted()
{
  return completed;
}


OperationPtr HttpOperation::finalize()
{
  if (resultHandler) {
    resultHandler(response, error);
    resultHandler = NULL; // call once only
  }
  httpClient.processOperations();
  return inherited::finalize();
}


void HttpOperation::abortOperation(ErrorPtr aError)
{
  if (!aborted) {
    if (!completed) {
      // cancel if request is not completed
      httpClient.getHttpApi().cancelRequest();
    }
    if (resultHandler && aError) {
      resultHandler({}, aError);
      resultHandler = NULL; // call once only
    }
  }
  inherited::abortOperation(aError);
}


// MARK: ===== HttpClient


HttpClient::HttpClient() :
  inherited(MainLoop::currentMainLoop()),
  httpApi(MainLoop::currentMainLoop())
{
  httpApi.isMemberVariable();
}

#endif // ENABLE_NETATMO_V2
