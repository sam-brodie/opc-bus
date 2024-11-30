/* This work is licensed under a Creative Commons CCZero 1.0 Universal License.
 * See http://creativecommons.org/publicdomain/zero/1.0/ for more information. */

#include "IsobusOpcUaLayer.h"
#include "AMX_MODEL_NODE_DEFINES.h"
#include <canlib.h>

#include <chrono>
#include <thread>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Iphlpapi.lib")

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "user32.lib")

#pragma comment(lib, "advapi32.lib")


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


#include <signal.h>
#include <stdlib.h>

#include "common.h"
#include <iostream>


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

    ///* Load certificate and private key */
    //UA_ByteString certificate = loadFile("../../../testCerts/samClient_3_1.crt");
    //UA_ByteString privateKey = loadFile("../../../testCerts/sam_CA_root_private_key.pem");

    ///* Load the trustList. Load revocationList is not supported now */
    //size_t trustListSize = 1;
    //UA_STACKARRAY(UA_ByteString, trustList, trustListSize + 1);
    //trustList[0] = loadFile("../../../testCerts/sam_CA_root_3.crt");

    //size_t revocationListSize = 1;
    ////UA_ByteString* revocationList = NULL;
    //UA_STACKARRAY(UA_ByteString, revocationList, revocationListSize + 1);
    //revocationList[0] = loadFile("../../../testCerts/sam_CA_root_2_CRL.pem");

    int userChoice;
    std::cout << "Enter a number for which array to subscribe to (1, 10, 100, 1000, 10000): ";
    std::cin >> userChoice;

    UA_Client* client = UA_Client_new();
    UA_ClientConfig* cc = UA_Client_getConfig(client);
    //UA_ClientConfig_setDefault(UA_Client_getConfig(client));
    
    /*cc->securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
    cc->securityPolicyUri = UA_STRING_ALLOC("http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256");*/

    cc->subscriptionInactivityCallback = subscriptionInactivityCallback;

    //UA_StatusCode retval = UA_ClientConfig_setISOBUSEncryption(cc, certificate, privateKey,
    //    trustList, trustListSize,
    //    revocationList, revocationListSize);


    UA_StatusCode retval = UA_ClientConfig_setISOBUS(UA_Client_getConfig(client));

    if (retval != UA_STATUSCODE_GOOD) {
        UA_LOG_FATAL(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
            "Failed to set NONencryption.");
        UA_Client_delete(client);
        return EXIT_FAILURE;
    }

   /* UA_ByteString_clear(&certificate);
    UA_ByteString_clear(&privateKey);
    for (size_t deleteCount = 0; deleteCount < trustListSize; deleteCount++) {
        UA_ByteString_clear(&trustList[deleteCount]);
    }*/

    /* Secure client connect */
    cc->securityMode = UA_MESSAGESECURITYMODE_NONE; /* require encryption */

    //const char* endpointUrl = "opc.tcp://localhost:4840";
    retval = UA_Client_connect(client, "opc.tcp://localhost:4840");
    if (retval != UA_STATUSCODE_GOOD) {
        UA_Client_delete(client);
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////////
    UA_NodeId nodeId_to_subscribe;
    switch (userChoice)
    {
    case 1:
        nodeId_to_subscribe = UA_NODEID_NUMERIC(NS_AMX, ID_AMX_1);
        break;
    case 10:
        nodeId_to_subscribe = UA_NODEID_NUMERIC(NS_AMX, ID_AMX_10);
        break;
    case 100:
        nodeId_to_subscribe = UA_NODEID_NUMERIC(NS_AMX, ID_AMX_100);
        break;
    case 1000:
        nodeId_to_subscribe = UA_NODEID_NUMERIC(NS_AMX, ID_AMX_1K);
        break;
    case 10000:
        nodeId_to_subscribe = UA_NODEID_NUMERIC(NS_AMX, ID_AMX_10K);
        break;
    default:
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
            "Invalid choice. Subscribing to ID_AMX_10");
        nodeId_to_subscribe = UA_NODEID_NUMERIC(NS_AMX, ID_AMX_10);
        break;
    }
    //
    setupSubscription(nodeId_to_subscribe, client);


    ////////////////////////////////////////////////////////////////////////////////


    for (int i = 0; i < 50; i++) {
        UA_Client_run_iterate(client, 1000);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        //std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    /* Clean up */
    //UA_Variant_clear(&value);
    UA_Client_delete(client);
    return retval == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}

