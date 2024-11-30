/* This work is licensed under a Creative Commons CCZero 1.0 Universal License.
 * See http://creativecommons.org/publicdomain/zero/1.0/ for more information. */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Iphlpapi.lib")

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "user32.lib")

#pragma comment(lib, "advapi32.lib")

#include <signal.h>
#include <stdlib.h>

#include "common.h"
#include <iostream>

#include <openssl/blowfish.h>

#include "IsobusTaskController.h"
#include "IsobusOpcUaLayer.h"
#include "AMX_MODEL_NODE_DEFINES.h"
#include <canlib.h>

#include <chrono>
#include <thread>



#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_subscriptions.h>
#include <open62541/types.h>
#include <open62541/types_generated.h>
#include <open62541/types_generated_handling.h>
 //#include <open62541/network_tcp.h>

#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>
#include <open62541/plugin/create_certificate.h>
#include <open62541/plugin/securitypolicy.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include "D:/open62541_isobus/open62541_over_isobus/open62541/plugins/crypto/openssl/securitypolicy_openssl_common.h"
#include "D:/open62541_isobus/open62541_over_isobus/open62541/plugins/crypto/openssl/ua_openssl_version_abstraction.h"
#include "D:/open62541_isobus/open62541_over_isobus/open62541/plugins/include/open62541/plugin/pki_default.h"




static void
handler_currentTimeChanged(UA_Client* client, UA_UInt32 subId, void* subContext,
    UA_UInt32 monId, void* monContext, UA_DataValue* value) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "currentTime has changed!");
    if (UA_Variant_hasScalarType(&value->value, &UA_TYPES[UA_TYPES_DATETIME])) {
        UA_DateTime raw_date = *(UA_DateTime*)value->value.data;
        UA_DateTimeStruct dts = UA_DateTime_toStruct(raw_date);
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
            "date is: %02u-%02u-%04u %02u:%02u:%02u.%03u",
            dts.day, dts.month, dts.year, dts.hour, dts.min, dts.sec, dts.milliSec);
    }
}

static void
handler_arrayChanged(UA_Client* client, UA_UInt32 subId, void* subContext,
    UA_UInt32 monId, void* monContext, UA_DataValue* value) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "array has changed!");

    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "length = %d", value->value.arrayLength);

    if (UA_Variant_hasArrayType(&value->value, &UA_TYPES[UA_TYPES_DOUBLE])) {
        UA_Double raw_data = *(UA_Double*)value->value.data;
        UA_Double dataArray[10000];
        std::copy((double*)value->value.data,
            (double*)value->value.data + value->value.arrayLength, dataArray);

        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "subscribed Value: ");
        for (int i = 0; i < value->value.arrayLength; i++)
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%f", dataArray[i]);
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, " \n");
    }
}

static void
deleteSubscriptionCallback(UA_Client* client, UA_UInt32 subscriptionId, void* subscriptionContext) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
        "Subscription Id %u was deleted", subscriptionId);
}

static void
subscriptionInactivityCallback(UA_Client* client, UA_UInt32 subId, void* subContext) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Inactivity for subscription %u", subId);
}

int setupSubscription(const UA_NodeId nodeId, UA_Client* client)
{
    UA_CreateSubscriptionRequest request = UA_CreateSubscriptionRequest_default();
    UA_CreateSubscriptionResponse response =
        UA_Client_Subscriptions_create(client, request, NULL, NULL, deleteSubscriptionCallback);
    if (response.responseHeader.serviceResult == UA_STATUSCODE_GOOD)
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
            "Create subscription succeeded, id %u",
            response.subscriptionId);
    else
        return -1;

    UA_MonitoredItemCreateRequest monRequest =
        UA_MonitoredItemCreateRequest_default(nodeId);

    UA_MonitoredItemCreateResult monResponse =
        UA_Client_MonitoredItems_createDataChange(client, response.subscriptionId,
            UA_TIMESTAMPSTORETURN_BOTH, monRequest,
            NULL, handler_arrayChanged, NULL);

    if (monResponse.statusCode == UA_STATUSCODE_GOOD)
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
            "Monitoring UA_NODEID_NUMERIC(%u, %u)', id %u",
            nodeId.namespaceIndex, nodeId.identifier.numeric,
            monResponse.monitoredItemId);

    return 0;
}

// CLIENT
int main(int argc, char* argv[]) {
    canInitializeLibrary();

    

    //////////////////////////////////////
    // setup OPC UA stuff + security    //
    //////////////////////////////////////
    /* Load certificate and private key */
    UA_ByteString certificate = loadFile("../../../testCerts/samClient_3_3.crt");
    UA_ByteString privateKey = loadFile("../../../testCerts/sam_client_private_key.pem");

    /* Load the trustList. Load revocationList is not supported now */
    size_t trustListSize = 1;
    UA_STACKARRAY(UA_ByteString, trustList, trustListSize + 1);
    trustList[0] = loadFile("../../../testCerts/sam_CA_root_3.crt");

    size_t revocationListSize = 1;
    //UA_ByteString* revocationList = NULL;
    UA_STACKARRAY(UA_ByteString, revocationList, revocationListSize + 1);
    revocationList[0] = loadFile("../../../testCerts/sam_CA_root_2_CRL.pem");

    //int userChoice;
    //std::cout << "Enter a number for which array to subscribe to (1, 10, 100, 1000, 10000): ";
    //std::cin >> userChoice;

    UA_Client* client = UA_Client_new();
    UA_ClientConfig* cc = UA_Client_getConfig(client);
    //UA_ClientConfig_setDefault(UA_Client_getConfig(client));
    cc->securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
    cc->securityPolicyUri = UA_STRING_ALLOC("http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256");

    cc->subscriptionInactivityCallback = subscriptionInactivityCallback;

    UA_StatusCode retval = UA_ClientConfig_setISOBUSEncryption(cc, certificate, privateKey,
        trustList, trustListSize,
        revocationList, revocationListSize);
    if (retval != UA_STATUSCODE_GOOD) {
        UA_LOG_FATAL(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
            "Failed to set encryption.");
        UA_Client_delete(client);
        return EXIT_FAILURE;
    }

    UA_ByteString_clear(&certificate);
    UA_ByteString_clear(&privateKey);
    for (size_t deleteCount = 0; deleteCount < trustListSize; deleteCount++) {
        UA_ByteString_clear(&trustList[deleteCount]);
    }

    /* Secure client connect */
    cc->securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT; /* require encryption */



    uint8_t pseudorandom_numbers[5][16] = {
    {34, 67, 89, 123, 45, 78, 90, 12, 56, 78, 34, 67, 89, 123, 45, 78},
    {98, 23, 45, 67, 89, 12, 34, 56, 78, 90, 98, 23, 45, 67, 89, 12},
    {11, 22, 33, 44, 55, 66, 77, 88, 99, 100, 11, 22, 33, 44, 55, 66},
    {101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 101, 102, 103, 104, 105, 106},
    {210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 210, 211, 212, 213, 214, 215}
    }; // for re-keying the Blowfish encrypted messages

    int i_keys = 0;
    uint8_t pseudorandom_key[16];
    std::copy(&pseudorandom_numbers[i_keys][0], &pseudorandom_numbers[i_keys][0] + 16, pseudorandom_key);


    //const char* endpointUrl = "opc.tcp://localhost:4840";
    retval = UA_Client_connect(client, "opc.tcp://localhost:4840");
    if (retval != UA_STATUSCODE_GOOD) {
        UA_Client_delete(client);
        return EXIT_FAILURE;
    }
    //////////////////////////////////////
    // Start user code                  //
    //////////////////////////////////////


    //tc_rekey(pseudorandom_key, 16);
    ////////////////////////////////////////////////////////////////////////////////


    UA_NodeId nodeID_tc2ecu_key = UA_NODEID_NUMERIC(NS_AMX, ID_AMX_BF_KEY_tc2ecu);
    UA_NodeId nodeID_ecu2tc_key = UA_NODEID_NUMERIC(NS_AMX, ID_AMX_BF_KEY_ecu2tc);
    UA_Variant tc2ecu_keyArray;
    UA_Variant ecu2tc_keyArray;
    ;

    // write to the nodes
    UA_Variant_setArray(&tc2ecu_keyArray, pseudorandom_key, 16, &UA_TYPES[UA_TYPES_BYTE]);
    UA_Client_writeValueAttribute(client, nodeID_tc2ecu_key, &tc2ecu_keyArray);
    tc_rekey(pseudorandom_key, 16, true);
    g_tc2ecu_using_bf_enc = true;
    for (int i = 0; i < 16; i++)
        std::cout << i << "key (tc2ecu):" << (int)pseudorandom_key[i] << ", ";
    std::cout << std::endl;

    i_keys = i_keys++ % 5;
    // take next random number from the big array and rekey with it
    std::copy(&pseudorandom_numbers[i_keys][0], &pseudorandom_numbers[i_keys][0] + 16, pseudorandom_key);
    UA_Variant_setArray(&ecu2tc_keyArray, pseudorandom_key, 16, &UA_TYPES[UA_TYPES_BYTE]);
    UA_Client_writeValueAttribute(client, nodeID_ecu2tc_key, &ecu2tc_keyArray);
    tc_rekey(pseudorandom_key, 16, false);



    uint32_t rate_set = 0;
    uint32_t rate_act;
    uint16_t element_num_set = 1;
    uint16_t element_num_act;
    uint16_t DDI_set = 6;
    uint16_t DDI_act;
    uint8_t destAddr = 0; //todo: remove
    uint8_t srcAddr = 0; //todo: remove
    canStatus canStat;

    const uint16_t DDI_SC_CONDENSED_WS_ACT = 161;
    const uint16_t DDI_SC_CONDENSED_WS_SET = 290;
    const bool isTc2Ecukey = true;

    for (int i = 0; i < 10; i++)
    {
        UA_Client_run_iterate(client, 1000);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        rate_set++;

        canStat = sendRateWithEncryption(rate_set, element_num_set, DDI_set);
        if (canStat == canOK)
            std::cout << "Rate Setpoint: " << rate_set << std::endl;

        canStat = readRateWithEncryption(&rate_act, &element_num_act, &DDI_act);
        if (canStat == canOK)
            std::cout << "Rate Actual: " << rate_act << std::endl;


        //if (DDI_act == DDI_SC_CONDENSED_WS_ACT && rate_act == 0) //if all sections are off (todo: consider error/not installed)
        //{
        //    std::cout << "----------------\nREKEYING\n----------------" << std::endl;
        //    i_keys = i_keys++ % 5;
        //    // take next random number from the big array and rekey with it
        //    std::copy(&pseudorandom_numbers[i_keys][0], &pseudorandom_numbers[i_keys][0] + 16, pseudorandom_key);
        //    UA_Variant_setArray(&tc2ecu_keyArray, pseudorandom_key, 16, &UA_TYPES[UA_TYPES_BYTE]);
        //    UA_Client_writeValueAttribute(client, nodeID_tc2ecu_key, &tc2ecu_keyArray);
        //    tc_rekey(pseudorandom_key, 16, isTc2Ecukey);
        //    g_tc2ecu_using_bf_enc = true;
        //    for (int i = 0; i < 16; i++)
        //        std::cout << i << "key (tc2ecu):" << (int)pseudorandom_key[i] << ", ";
        //    std::cout << std::endl;

        //    i_keys = i_keys++ % 5;
        //    // take next random number from the big array and rekey with it
        //    std::copy(&pseudorandom_numbers[i_keys][0], &pseudorandom_numbers[i_keys][0] + 16, pseudorandom_key);
        //    UA_Variant_setArray(&ecu2tc_keyArray, pseudorandom_key, 16, &UA_TYPES[UA_TYPES_BYTE]);
        //    UA_Client_writeValueAttribute(client, nodeID_ecu2tc_key, &ecu2tc_keyArray);
        //    tc_rekey(pseudorandom_key, 16, !isTc2Ecukey);
        //}

    }
    /* Clean up */
    //UA_Variant_clear(&value);
    UA_Client_delete(client);
    return retval == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}