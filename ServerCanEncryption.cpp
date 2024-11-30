
#include "IsobusOpcUaLayer.h"
#include <canlib.h>

#include "AMX_MODEL_NODE_DEFINES.h"

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Iphlpapi.lib")

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "user32.lib")

#pragma comment(lib, "advapi32.lib")

#include <open62541/client_config_default.h>

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
#include "amxModel.h"

void server_init_arrays(UA_Server* server)
{
UA_Variant value;

//init array with 10000 elements (0 to 10000)
UA_Double dataArray[10000];
for (int i = 0; i < 10000; i++)
    dataArray[i] = double(i);

// write to the nodes
UA_Variant_setArray(&value, &dataArray, 1, &UA_TYPES[UA_TYPES_DOUBLE]);
UA_NodeId currentNodeId = UA_NODEID_NUMERIC(NS_AMX, ID_AMX_1);
UA_Server_writeValue(server, currentNodeId, value);

UA_Variant_setArray(&value, &dataArray, 10, &UA_TYPES[UA_TYPES_DOUBLE]);
currentNodeId = UA_NODEID_NUMERIC(NS_AMX, ID_AMX_10);
UA_Server_writeValue(server, currentNodeId, value);

UA_Variant_setArray(&value, &dataArray, 100, &UA_TYPES[UA_TYPES_DOUBLE]);
currentNodeId = UA_NODEID_NUMERIC(NS_AMX, ID_AMX_100);
UA_Server_writeValue(server, currentNodeId, value);

UA_Variant_setArray(&value, &dataArray, 1000, &UA_TYPES[UA_TYPES_DOUBLE]);
currentNodeId = UA_NODEID_NUMERIC(NS_AMX, ID_AMX_1K);
UA_Server_writeValue(server, currentNodeId, value);

UA_Variant_setArray(&value, &dataArray, 10000, &UA_TYPES[UA_TYPES_DOUBLE]);
currentNodeId = UA_NODEID_NUMERIC(NS_AMX, ID_AMX_10K);
UA_Server_writeValue(server, currentNodeId, value);



}

UA_Boolean running = true;
static void stopHandler(int sig) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "received ctrl-c");
    running = false;
}

// SERVER
int main(int argc, char* argv[]) {
    canInitializeLibrary();

    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);
    UA_ByteString certificate = UA_BYTESTRING_NULL;
    UA_ByteString privateKey = UA_BYTESTRING_NULL;

    /* Load certificate and private key */
    certificate = loadFile("../../../testCerts/samServer_3_3.crt");
    privateKey = loadFile("../../../testCerts/sam_server_private_key.pem");

    /* Load the trustList. Load revocationList is not supported now */
    size_t trustListSize = 1;
    //UA_ByteString* trustList = NULL;
    UA_STACKARRAY(UA_ByteString, trustList, trustListSize + 1);
    trustList[0] = loadFile("../../../testCerts/sam_CA_root_3.crt");

    /* Loading of an issuer list, not used in this application */
    size_t issuerListSize = 1;
    //UA_ByteString* issuerList = NULL;
    UA_STACKARRAY(UA_ByteString, issuerList, issuerListSize + 1);
    issuerList[0] = loadFile("../../../testCerts/sam_CA_root_3.crt");

    size_t revocationListSize = 1;
    //UA_ByteString* revocationList = NULL;
    UA_STACKARRAY(UA_ByteString, revocationList, revocationListSize + 1);
    revocationList[0] = loadFile("../../../testCerts/sam_CA_root_2_CRL.pem");

    UA_Server* server = UA_Server_new();
    UA_ServerConfig* config = UA_Server_getConfig(server);

    UA_StatusCode retval =
        UA_ServerConfig_setISOBUSWithSecurityPolicies(config, 4840,
            &certificate, &privateKey,
            trustList, trustListSize,
            issuerList, issuerListSize,
            revocationList, revocationListSize);


    retval = UA_CertificateVerification_Trustlist(&config->certificateVerification,
        trustList, trustListSize,
        issuerList, issuerListSize,
        revocationList, revocationListSize);

    UA_ByteString_clear(&certificate);
    UA_ByteString_clear(&privateKey);
    for (size_t i = 0; i < trustListSize; i++)
        UA_ByteString_clear(&trustList[i]);
    if (retval != UA_STATUSCODE_GOOD)
        goto cleanup;

    /* create nodes from nodeset */
    if (amxModel(server) != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Could not add the example nodeset. "
            "Check previous output for any error.");
        retval = UA_STATUSCODE_BADUNEXPECTEDERROR;
    }

    if (!running)
        goto cleanup; /* received ctrl-c already */

    server_init_arrays(server);

    retval = UA_Server_run(server, &running);


cleanup:
    UA_Server_delete(server);
    return retval == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}