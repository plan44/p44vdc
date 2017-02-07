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

#include "vdcapi.hpp"

using namespace p44;


// MARK: ===== VdcApiServer

VdcApiServer::VdcApiServer() :
  inherited(MainLoop::currentMainLoop())
{
}


void VdcApiServer::start()
{
  inherited::startServer(boost::bind(&VdcApiServer::serverConnectionHandler, this, _1), 3);
}


void VdcApiServer::stop()
{
  closeConnection();
  clearCallbacks();
}


void VdcApiServer::setConnectionStatusHandler(VdcApiConnectionCB aConnectionCB)
{
  apiConnectionStatusHandler = aConnectionCB;
}


SocketCommPtr VdcApiServer::serverConnectionHandler(SocketCommPtr aServerSocketComm)
{
  // create new connection
  VdcApiConnectionPtr apiConnection = newConnection();
  SocketCommPtr socketComm = apiConnection->socketConnection();
  socketComm->setClearHandlersAtClose(); // to make sure retain cycles are broken
  socketComm->relatedObject = apiConnection; // bind object to connection
  socketComm->setConnectionStatusHandler(boost::bind(&VdcApiServer::connectionStatusHandler, this, _1, _2));
  // return the socketComm object which handles this connection
  return socketComm;
}


void VdcApiServer::connectionStatusHandler(SocketCommPtr aSocketComm, ErrorPtr aError)
{
  if (apiConnectionStatusHandler) {
    // get connection object
    VdcApiConnectionPtr apiConnection = boost::dynamic_pointer_cast<VdcApiConnection>(aSocketComm->relatedObject);
    if (apiConnection) {
      apiConnectionStatusHandler(apiConnection, aError);
    }
  }
  if (!Error::isOK(aError)) {
    // connection failed/closed and we don't support reconnect yet
    aSocketComm->relatedObject.reset(); // detach connection object
  }
}


// MARK: ===== VdcApiConnection


void VdcApiConnection::setRequestHandler(VdcApiRequestCB aApiRequestHandler)
{
  apiRequestHandler = aApiRequestHandler;
}


void VdcApiConnection::closeConnection()
{
  if (socketConnection()) {
    socketConnection()->closeConnection();
    socketConnection()->clearCallbacks();
  }
}


ApiValuePtr VdcApiConnection::newApiValue()
{
  // ask the server
  VdcApiServerPtr srv = boost::dynamic_pointer_cast<VdcApiServer>(socketConnection()->getServerConnection());
  if (srv)
    return srv->newApiValue();
  return ApiValuePtr(); // none
}


// MARK: ===== VdcApiRequest

ErrorPtr VdcApiRequest::sendError(ErrorPtr aErrorToSend)
{
  if (!Error::isOK(aErrorToSend)) {
    return sendError((uint32_t)aErrorToSend->getErrorCode(), aErrorToSend->getErrorMessage());
  }
  return ErrorPtr();
}


ErrorPtr VdcApiRequest::sendStatus(ErrorPtr aStatusToSend)
{
  if (Error::isOK(aStatusToSend)) {
    // OK status -> return empty result
    return sendResult(ApiValuePtr());
  }
  else {
    // error status -> return error
    return sendError(aStatusToSend);
  }
}

