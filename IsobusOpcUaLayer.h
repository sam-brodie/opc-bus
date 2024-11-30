#pragma once
#define WIN32_LEAN_AND_MEAN


//edited from the original ua_config_default.c (which was published with CCZero licence)

#include "IsobusCommon.h"

#include <open62541/client_config_default.h>
#include <open62541/network_tcp.h>
#ifdef UA_ENABLE_WEBSOCKET_SERVER
#include <open62541/network_ws.h>
#endif

#include <open62541/server_config_default.h>


#include <open62541/types.h>


#include <canlib.h>


_UA_BEGIN_DECLS

canStatus readRateWithEncryption(uint32_t* rate, uint16_t* element_num, uint16_t* DDI);

UA_StatusCode
UA_ClientConfig_setISOBUS(UA_ClientConfig* config);

UA_Connection
UA_ClientConnectionISOBUS_init(UA_ConnectionConfig config, const UA_String endpointUrl,
    UA_UInt32 timeout, const UA_Logger* logger);

UA_StatusCode
UA_ClientConnectionISOBUS_poll(UA_Connection* connection, UA_UInt32 timeout,
    const UA_Logger* logger);

void UA_setupIsobusChannel(canHandle* hnd, canStatus* stat, const int channel_number,
    const int flags);

void UA_Isobus_Check(const char* info, canStatus stat);

static UA_StatusCode
connection_write_ISOBUS(UA_Connection* connection, UA_ByteString* buf);

int BlockingTpSendTo(int sa, char* data, int len, int pgnTosend, long da);

static UA_StatusCode
connection_ISOBUS_recv(UA_Connection* connection, UA_ByteString* response,
    UA_UInt32 timeout);

int BlockingTpRecieveFrom(char* data, int maxLen, long* incomingPgn, long* sendersSa, long mySa);

static void
ClientNetworkLayerISOBUS_close(UA_Connection* connection);

static void
ClientNetworkLayerISOBUS_free(UA_Connection* connection);

/*same as the original function. not changed for isobus*/
static UA_StatusCode
connection_getsendbuffer(UA_Connection* connection,
    size_t length, UA_ByteString* buf);

/*same as the original function. not changed for isobus*/
static void
connection_releasesendbuffer(UA_Connection* connection,
    UA_ByteString* buf);

/*same as the original function. not changed for isobus*/
static void
connection_releaserecvbuffer(UA_Connection* connection,
    UA_ByteString* buf);

// server stuff 

/* Struct initialization works across ANSI C/C99/C++ if it is done when the
 * variable is first declared. Assigning values to existing structs is
 * heterogeneous across the three. */
static UA_INLINE UA_UInt32Range
UA_UINT32RANGE(UA_UInt32 min, UA_UInt32 max);

static UA_INLINE UA_DurationRange
UA_DURATIONRANGE(UA_Duration min, UA_Duration max);

//static UA_INLINE UA_StatusCode
//UA_ServerConfig_setISOBUS(UA_ServerConfig* config);
//
//static UA_INLINE UA_StatusCode
//UA_ServerConfig_setMinimalISOBUS(UA_ServerConfig* config, UA_UInt16 portNumber,
//    const UA_ByteString* certificate);

UA_StatusCode
UA_ServerConfig_setMinimalCustomBuffer_ISOBUS(UA_ServerConfig* config, UA_UInt16 portNumber,
    const UA_ByteString* certificate,
    UA_UInt32 sendBufferSize,
    UA_UInt32 recvBufferSize);

static UA_INLINE UA_StatusCode
UA_ServerConfig_setMinimalISOBUS(UA_ServerConfig* config, UA_UInt16 portNumber,
    const UA_ByteString* certificate) {
    return UA_ServerConfig_setMinimalCustomBuffer_ISOBUS(config, portNumber,
        certificate, 0, 0);
}

static UA_INLINE UA_StatusCode
UA_ServerConfig_setISOBUS(UA_ServerConfig* config) {
    return UA_ServerConfig_setMinimalISOBUS(config, 4840, NULL);
}

static UA_StatusCode
addISOBUSNetworkLayers(UA_ServerConfig* conf, UA_UInt16 portNumber,
    UA_UInt32 sendBufferSize, UA_UInt32 recvBufferSize);

UA_StatusCode
UA_ServerConfig_addNetworkLayerISOBUS(UA_ServerConfig* conf, UA_UInt16 portNumber,
    UA_UInt32 sendBufferSize, UA_UInt32 recvBufferSize);

UA_ServerNetworkLayer
UA_ServerNetworkLayerISOBUS(UA_ConnectionConfig config, UA_UInt16 port,
    UA_UInt16 maxConnections);

/* run only when the server is stopped */
static void
ServerNetworkLayerISOBUS_clear(UA_ServerNetworkLayer* nl);

static UA_StatusCode
ServerNetworkLayerISOBUS_start(UA_ServerNetworkLayer* nl, const UA_Logger* logger,
    const UA_String* customHostname);

static UA_StatusCode
ServerNetworkLayerISOBUS_listen(UA_ServerNetworkLayer* nl, UA_Server* server,
    UA_UInt16 timeout);

//static UA_StatusCode
//ServerNetworkLayerISOBUS_add(UA_ServerNetworkLayer* nl, ServerNetworkLayerISOBUS* layer);

static void
ServerNetworkLayerISOBUS_close(UA_Connection* connection);

static void
ServerNetworkLayerTCP_freeConnection(UA_Connection* connection);

static void
ServerNetworkLayerISOBUS_stop(UA_ServerNetworkLayer* nl, UA_Server* server);

static void
ServerNetworkLayerTCP_close(UA_Connection* connection);

static UA_StatusCode
setDefaultConfig(UA_ServerConfig* conf);

#ifdef UA_ENABLE_ENCRYPTION
UA_StatusCode
UA_ClientConfig_setISOBUSEncryption(UA_ClientConfig* config,
    UA_ByteString localCertificate, UA_ByteString privateKey,
    const UA_ByteString* trustList, size_t trustListSize,
    const UA_ByteString* revocationList, size_t revocationListSize);

//UA_StatusCode
//UA_ClientConfig_setDefaultEncryption(UA_ClientConfig* config,
//    UA_ByteString localCertificate, UA_ByteString privateKey,
//    const UA_ByteString* trustList, size_t trustListSize,
//    const UA_ByteString* revocationList, size_t revocationListSize)

UA_StatusCode
UA_ServerConfig_setISOBUSWithSecurityPolicies(UA_ServerConfig* conf,
    UA_UInt16 portNumber,
    const UA_ByteString* certificate,
    const UA_ByteString* privateKey,
    const UA_ByteString* trustList,
    size_t trustListSize,
    const UA_ByteString* issuerList,
    size_t issuerListSize,
    const UA_ByteString* revocationList,
    size_t revocationListSize);

#endif

_UA_END_DECLS
