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

#ifndef __p44utils__httpclient__
#define __p44utils__httpclient__

#include "p44utils_common.hpp"

#if ENABLE_NETATMO_V2

#include "httpcomm.hpp"
#include "operationqueue.hpp"


namespace p44 {

using namespace std;


namespace httputils {

  /// Decode chunked data transfer
  /// @note use it when response header contains 'Transfer-Encoding: chunked'
  string decodeChunkData(const string& aChunkedData);

  /// Get the line from input stream, convert hex to int
  /// @return returns -1 when conversion failed, otherwise chunk size
  int getChunkSize(std::istringstream& iss);

}

  class HttpClient;

  /// Http operation
  class HttpOperation : public Operation
  {
    typedef Operation inherited;

    protected:
    HttpClient &httpClient;               ///< reference to communication class
    string method;                        ///< http method
    string url;                           ///< path to be called
    string requestBody;                   ///< data to be sent
    string response;                      ///< data received
    bool completed;                       ///< completion flag
    ErrorPtr error;                       ///< communication error
    HttpCommCB resultHandler;             ///< callback to be executed at the end of operation


    protected:
    /// Send http request and perform intermidiate data processing if needed
    /// @note call processAnswer in request callback to store received data
    virtual void sendRequest()=0;

    /// Store received data and request error, mark operation as completed
    virtual void processAnswer(const string& aResponse, ErrorPtr aError);

    /// Derived operation methods
    virtual bool initiate();
    virtual bool hasCompleted();
    virtual OperationPtr finalize();
    virtual void abortOperation(ErrorPtr aError);

  public:
    /// Operation constructor
    /// @param aHttpClient reference to Http client
    /// @param aMethod Http method
    /// @param aUrl full web address
    /// @param aRequestBody data to be sent
    /// @param aResultHandler callback to be executed when operation is completed
    HttpOperation(HttpClient &aHttpClient, const string& aMethod, const string& aUrl, const string& aRequestBody, HttpCommCB aResultHandler);
    virtual ~HttpOperation(){}

  };
  typedef boost::intrusive_ptr<HttpOperation> HttpOperationPtr;


  /// wrapper for http client communication with request queue
  class HttpClient : public OperationQueue
  {
      typedef OperationQueue inherited;

      HttpComm httpApi;     //< http client api

    public:
      HttpClient();
      virtual ~HttpClient(){}

      // Get reference to http client api
      HttpComm& getHttpApi() { return httpApi; }

  };



} // namespace p44

#endif // ENABLE_NETATMO_V2
#endif /* defined(__p44utils__httpclient__) */
