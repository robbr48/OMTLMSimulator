// File: PluginImplementer.cc
// Provides the implementation of methods for PluginImplementer.
#include "TLMPlugin.h"
#include "TLMCommUtil.h"
#include "PluginImplementer.h"
#include <cassert>
#include <iostream>

using std::string;
using std::vector;
using std::map;

#include <fstream>
using std::ofstream;


TLMPlugin* TLMPlugin::CreateInstance() {
    return new PluginImplementer;
}

PluginImplementer::PluginImplementer():
    Connected(false),
    ModelChecked(false),
    Interfaces(),
    ClientComm(),
    Message(),
    MapID2Ind(),
    StartTime(0.0),
    EndTime(0.0),
    MaxStep(0.0)
{    
}


PluginImplementer::~PluginImplementer() {
    
    for(vector<TLMInterface*>::iterator it = Interfaces.begin();
        it != Interfaces.end(); ++it) {
        delete (*it);
    }
}


void PluginImplementer::CheckModel() {

    if(!Connected) {
        TLMErrorLog::FatalError("Check model cannot be called before the TLM client is connected to manager");
    }

    Message.Header.MessageType =  TLMMessageTypeConst::TLM_CHECK_MODEL;

    TLMCommUtil::SendMessage(Message);
    TLMCommUtil::ReceiveMessage(Message);

    if(! Message.Header.TLMInterfaceID) {
        TLMErrorLog::FatalError("Error detected on TLM manager while checking meta model");
        TLMErrorLog::FatalError("Header id is " + TLMErrorLog::ToStdStr(int(Message.Header.TLMInterfaceID)));
    }

    ModelChecked = true;
}


// Init method. Should be called after the default constructor. It will
// initialize the object and connect to TLMManager. Will return true
// on success, false otherwize. Note that the method can be called
// only once.
bool PluginImplementer::Init( std::string model,
                              double timeStart,
                              double timeEnd,
                              double maxStep,
                              std::string ServerName) {
    if(Connected) return true;

    string::size_type colPos = ServerName.rfind(':');

    if(colPos == string::npos) {
        TLMErrorLog::Warning(string("Server name string expected <server>:<port>, got:") + ServerName);
        return false;
    }

    int port = atoi(ServerName.c_str() + colPos + 1);

    string host = ServerName.substr(0,colPos);

    if((Message.SocketHandle = ClientComm.ConnectManager(host, port)) < 0) {
        TLMErrorLog::Warning("Init failed: could not connect to TLM manager");
        return false;
    }

    TLMErrorLog::Log("Sending Component registration request");
    
    ClientComm.CreateComponentRegMessage(model, Message);
    TLMCommUtil::SendMessage(Message);
    TLMCommUtil::ReceiveMessage(Message);

    TLMErrorLog::Log(string("Got component ID: ") +
                     TLMErrorLog::ToStdStr(Message.Header.TLMInterfaceID));

    StartTime = timeStart;
    EndTime = timeEnd;
    MaxStep = maxStep;

    Connected = true;
    SetInitialized();

    return true;
}

// Init method. Should be called after the default constructor. It will
// initialize the object and connect to TLMManager. Will return true
// on success, false otherwize. Note that the method can be called
// only once.
bool PluginImplementer::InitMonitor( double timeStart,
                                     double timeEnd,
                                     double maxStep,
                                     std::string ServerName) {
    if(Connected) return true;

    string::size_type colPos = ServerName.rfind(':');

    if(colPos == string::npos) {
        TLMErrorLog::Warning(string("Server name string expected <server>:<port>, got:") + ServerName);
        return false;
    }

    int port = atoi(ServerName.c_str() + colPos + 1);

    string host = ServerName.substr(0,colPos);

#if 0
    // We wait a certain time for the Manager since initialization might take time.
    const int MAX_WAITTIME = 120; // Two minutes
    int nSecs = 0;
    while((Message.SocketHandle = ClientComm.ConnectManager(host, port)) < 0 && nSecs < MAX_WAITTIME ) {
#ifndef _MSC_VER
        sleep(1);
#else
        Sleep(1000);
#endif
        nSecs++;
    }
#else
    Message.SocketHandle = ClientComm.ConnectManager(host, port);
#endif
    
    if( Message.SocketHandle < 0 ){
        TLMErrorLog::Warning("Init failed: could not connect to TLM manager");
        return false;
    }

    StartTime = timeStart;
    EndTime = timeEnd;
    MaxStep = maxStep;

    Connected = true;

    // No model checking for monitoring connections
    ModelChecked = true;

    return true;
}

// Register TLM interface sends a registration request to TLMManager
// and returns the ID for the interface. '-1' is returned if
// the interface is not connected in the MetaModel.
int  PluginImplementer::RegisteTLMInterface( std::string name) {
    TLMErrorLog::Log(string("Register Interface ") + name );

    TLMInterface * ifc = new TLMInterface(ClientComm, name, StartTime);

    int id = ifc->GetInterfaceID();

    TLMErrorLog::Log(string("Got interface ID: ") + TLMErrorLog::ToStdStr(id));

    // Check that this interface is connected
    if(id < 0) {
        // no need for the interface object that is not connected
        delete ifc;
        return id;
    }

    // The index of the new interface:
    int idx = Interfaces.size();

    Interfaces.push_back(ifc);

    MapID2Ind[id] = idx;

    return id;
}


// ReceiveTimeData receives time-stamped data from coupled simulations.
// Since the order of messages can vary the specified interfaceID
// is used only to detect the last message expected when the function
// is called. Any number of messages to other interfaces can arrive
// before the desired message is received.
// Input:
//   interfaceID - ID of a TLM interface that triggered the request
void PluginImplementer::ReceiveTimeData(TLMInterface* reqIfc, double time)  {
    while(time > reqIfc->GetNextRecvTime()) { // while data is needed

        //      if(DataToSend.size()>0) {
        //	TLMErrorLog::Log(string("Sends ") + TLMErrorLog::ToStdStr(DataToSend.size())
        //          + " items for time= " + TLMErrorLog::ToStdStr(DataToSend.last().time));

        // Transform to global inertial system cG ans send
        //        TransformTimeDataToCG(DataToSend, reqIfc->Params);

        //	Comm.PackTimeDataMessage(InterfaceID, DataToSend, Message);
        //	TLMCommUtil::SendMessage(Message);
        //	DataToSend.resize(0);
        //}


        // Receive data untill there is info for this interface
        string mess("Interface ");
        TLMErrorLog::Log(mess + reqIfc->GetName() +
                         " needs data for time= " + TLMErrorLog::ToStdStr(time));

        double allowedMaxTime = reqIfc->GetLastSendTime() + reqIfc->GetConnParams().Delay;

        if(allowedMaxTime < time) {
            string mess("WARNING: Interface ");
            TLMErrorLog::Log(mess + reqIfc->GetName() +
                             " is NOT ALLOWED to ask data after time= " + TLMErrorLog::ToStdStr(allowedMaxTime) +
                             ". The error is: "+TLMErrorLog::ToStdStr(time - allowedMaxTime));
            break;
        }

        TLMInterface* ifc = NULL;

        do {

            // Receive a message
            if(!TLMCommUtil::ReceiveMessage(Message)) // on error leave this loop and use extrapolation
                break;

            // Get the target ID
            int id = Message.Header.TLMInterfaceID;

            // Use the ID to get to the right interface object
            int idx = GetInterfaceIndex(id);
            ifc = Interfaces[idx];

            // Unpack the message into the Interface object data structures
            ifc->UnpackTimeData(Message);

            // Received data
            TLMErrorLog::Log(string("Interface ") + ifc->GetName() + " got data until time= "
                             + TLMErrorLog::ToStdStr(ifc->GetNextRecvTime()));

        } while(ifc != reqIfc); // loop until a message for this interface arrives

        if(ifc == NULL) break; // receive error - breaking

        TLMErrorLog::Log(string("Got data until time=") +
                         TLMErrorLog::ToStdStr(ifc->GetNextRecvTime()) );
    }
}


void PluginImplementer::GetForce(int interfaceID,
                                 double time,
                                 double position[],
                                 double orientation[],
                                 double speed[],
                                 double ang_speed[],
                                 double* force)  {

    if(!ModelChecked) CheckModel();

    // Use the ID to get to the right interface object
    int idx = GetInterfaceIndex(interfaceID);
    TLMInterface* ifc = Interfaces[idx];
    
    assert(!ifc || (ifc -> GetInterfaceID() == interfaceID));

    if(!ifc) {
        for(int i = 0; i < 6; i++) {
            force[i] = 0.0;
        }

        TLMErrorLog::Warning(string("No interface in GetForce()"));

        return;
    }

    // Check if the interface expects more data from the coupled simulation
    // Receive if necessary .Note that potentially more that one receive is possible
    ReceiveTimeData( ifc, time);

    // evaluate the reaction force from the TLM connection
    ifc->GetForce(time, position, orientation, speed, ang_speed, force);
}



void PluginImplementer::SetMotion(int forceID,
                                  double time,
                                  double position[],
                                  double orientation[],
                                  double speed[],
                                  double ang_speed[]) {

    if(!ModelChecked) CheckModel();
    if(forceID < 0) return;
    // Find the interface object by its ID
    int idx = GetInterfaceIndex(forceID);
    TLMInterface* ifc = Interfaces[idx];
    assert(ifc -> GetInterfaceID() == forceID);

    if( !ifc->waitForShutdown() ){
        // Store the data into the interface object
        TLMErrorLog::Log(string("calling SetTimeData()"));
        ifc->SetTimeData(time, position, orientation, speed, ang_speed);
    }
    else {
        // Check if all interfaces wait for shutdown
        std::vector<TLMInterface*>::iterator iter;
        for( iter=Interfaces.begin() ; iter!=Interfaces.end() ; iter++ ){
            if( ! (*iter)->waitForShutdown() ) return;
        }
#ifdef _MSC_VER     
        WSACleanup(); // BZ306 fixed here
#else
        // needed anything ?
#endif     

        // If we got here, we have a shutdown request from all interfaces
        exit(10);
    }

}

// GetConnectionParams returnes the ConnectionParams for
// the specified interface ID. Interface must be registered
// first.
void PluginImplementer::GetConnectionParams(int interfaceID, TLMConnectionParams& ParamsOut) {
    
    // Use the ID to get to the right interface object
    int idx = GetInterfaceIndex(interfaceID);
    TLMInterface* ifc = Interfaces[idx];
    assert(ifc -> GetInterfaceID() == interfaceID);

    ParamsOut = ifc->GetConnParams();
}

// GetTimeData returnes the necessary time stamped information needed
// for the calculation of the reaction force at a given time.
// The function might result in a request sent to TLM manager.
void PluginImplementer::GetTimeData(int interfaceID, double time, TLMTimeData& DataOut){
    if(!ModelChecked) CheckModel();

    // Use the ID to get to the right interface object
    int idx = GetInterfaceIndex(interfaceID);
    TLMInterface* ifc = Interfaces[idx];
    assert(ifc -> GetInterfaceID() == interfaceID);

    // Check if the interface expects more data from the coupled simulation
    // Receive if necessary .Note that potentially more that one receive is possible
    ReceiveTimeData( ifc, time);

    DataOut.time = time - ifc->GetConnParams().Delay;

    ifc->GetTimeData(DataOut);

}

void PluginImplementer::GetTimeDataX(int interfaceID, double time, TLMTimeData& DataOut){
    if(!ModelChecked) CheckModel();

    // Use the ID to get to the right interface object
    int idx = GetInterfaceIndex(interfaceID);
    TLMInterface* ifc = Interfaces[idx];
    assert(ifc -> GetInterfaceID() == interfaceID);

    // Check if the interface expects more data from the coupled simulation
    // Receive if necessary .Note that potentially more that one receive is possible
    ReceiveTimeDataX( ifc, time);

    DataOut.time = time - ifc->GetConnParams().Delay;

    ifc->GetTimeData(DataOut);

}


void PluginImplementer::ReceiveTimeDataX(TLMInterface* reqIfc, double time)  {
    while(time > reqIfc->GetNextRecvTime()) { // while data is needed

        // Receive data untill there is info for this interface
        string mess("Interface ");
        TLMErrorLog::Log(mess + reqIfc->GetName() +
                         " needs data for time= " + TLMErrorLog::ToStdStr(time));
        
        TLMInterface* ifc = NULL;

        do {

            // Receive a message
            if(!TLMCommUtil::ReceiveMessage(Message)) // on error leave this loop and use extrapolation
                break;

            // Get the target ID
            int id = Message.Header.TLMInterfaceID;

            // Use the ID to get to the right interface object
            int idx = GetInterfaceIndex(id);
            ifc = Interfaces[idx];

            // Unpack the message into the Interface object data structures
            ifc->UnpackTimeData(Message);

            // Received data
            TLMErrorLog::Log(string("Interface ") + ifc->GetName() + " got data until time= "
                             + TLMErrorLog::ToStdStr(ifc->GetNextRecvTime()));

        } while(ifc != reqIfc); // loop until a message for this interface arrives

        TLMErrorLog::Log(string("Got data until time=") +
                         TLMErrorLog::ToStdStr(ifc->GetNextRecvTime()) );

        if(ifc == NULL) break; // receive error - breaking
    }
}
