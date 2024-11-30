#pragma once
//#include "IsobusOpcUaLayer.h"
 #define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>

#include <canlib.h>
#include <openssl/blowfish.h>
#include <stdint.h>

#include <open62541/config.h>




_UA_BEGIN_DECLS

int tc_rekey(unsigned char* bf_key_val, int bf_key_len_bytes, bool isTc2EcuNotEcu2Tc);



canStatus sendRateWithEncryption(uint32_t rate, uint16_t element_num, uint16_t DDI);


canStatus sendAndWaitSyncForce8Bytes(const unsigned int id, unsigned char* msg, int waitMs);

//int samtest(UA_Server* server)
//{
//    ServerNetworkLayerISOBUS* x = server->config.networkLayers->handle;
//    x->connections->connection.state;
//}

_UA_END_DECLS