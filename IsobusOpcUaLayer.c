#include "IsobusCommon.h"
#include "IsobusOpcUaLayer.h"
//#include "IsobusTaskController.h"

#include <canlib.h>

#include "ua_securechannel.h"
#include <open62541/server_config_default.h>

#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/network_tcp.h>
#ifdef UA_ENABLE_WEBSOCKET_SERVER
#include <open62541/network_ws.h>
#endif
#include <open62541/plugin/accesscontrol_default.h>
#include <open62541/plugin/nodestore_default.h>
#include <open62541/plugin/log_stdout.h>
#include <open62541/plugin/pki_default.h>
#include <open62541/plugin/securitypolicy_default.h>
#include <open62541/server_config_default.h>

//edited from the original ua_config_default.c (which was published with CCZero licence)


//TODO: this is a redefinition from ua_config_default.c
#define MANUFACTURER_NAME "open62541"
#define PRODUCT_NAME "open62541 OPC UA Server"
#define PRODUCT_URI "http://open62541.org"
#define APPLICATION_NAME "open62541-based OPC UA Application"
#define APPLICATION_URI_SERVER "urn:open62541.server.application"
#define APPLICATION_URI "urn:open62541.client.application"

#define STRINGIFY(arg) #arg
#define VERSION(MAJOR, MINOR, PATCH, LABEL) \
    STRINGIFY(MAJOR) "." STRINGIFY(MINOR) "." STRINGIFY(PATCH) LABEL


static const size_t usernamePasswordsSize = 2;
static UA_UsernamePasswordLogin usernamePasswords[2] = {
    {UA_STRING_STATIC("user1"), UA_STRING_STATIC("password")},
    {UA_STRING_STATIC("user2"), UA_STRING_STATIC("password1")} };

typedef struct ISOBUSClientConnection {
    struct addrinfo hints, * server;
    UA_DateTime connStart;
    UA_String endpointUrl;
    UA_UInt32 timeout;
} ISOBUSClientConnection;

typedef struct {
    const UA_Logger* logger;
    UA_UInt16 port;
    UA_UInt16 maxConnections;
    UA_SOCKET serverSockets[FD_SETSIZE];
    UA_UInt16 serverSocketsSize;
    LIST_HEAD(, ConnectionEntry) connections;
    UA_UInt16 connectionsSize;
} ServerNetworkLayerISOBUS;

#define MAXBACKLOG     100
#define NOHELLOTIMEOUT 120000 /* timeout in ms before close the connection
                               * if server does not receive Hello Message */

typedef struct ConnectionEntry {
    UA_Connection connection;
    LIST_ENTRY(ConnectionEntry) pointers;
} ConnectionEntry;



canStatus readRateWithEncryption(uint32_t* rate, uint16_t* element_num, uint16_t* DDI)
{
    uint8_t destAddr = g_mySa;
    uint8_t srcAddr = g_saOfOtherCf;
    BF_KEY* myReadingKey;
    if(g_mySa == MY_SERVER_SA)
        myReadingKey = &bf_key_tc2ecu;
    else
        myReadingKey = &bf_key_ecu2TC;

    long expected_inMsgId = 0x0CCB0000 + (destAddr << 8) + srcAddr;;
    uint8_t frameData_encrypted[8] = { 0 };
    unsigned int inLen = 0;

    canStatus stat = canReadSpecific(g_hnd, expected_inMsgId, frameData_encrypted, &inLen,
        NULL, NULL);

    if (!g_tc2ecu_using_bf_enc)
        return -49;

    if (stat != canOK)
        return stat;

    uint8_t frameData_plaintext[8];
    BF_ecb_encrypt(frameData_encrypted, frameData_plaintext, myReadingKey, BF_DECRYPT);

    uint8_t rate_bytes[4]; // Example byte array
    uint8_t command;

    command = frameData_plaintext[0] & 0x0F;
    if (command != 0x03)
        return canERR__RESERVED;

    *element_num = (uint16_t)(frameData_plaintext[1] << 4) + (uint16_t)(frameData_plaintext[0] >> 4);
    *DDI = (uint16_t)frameData_plaintext[2] + (uint16_t)(frameData_plaintext[3] << 8);
    rate_bytes[0] = frameData_plaintext[4];
    rate_bytes[1] = frameData_plaintext[5];
    rate_bytes[2] = frameData_plaintext[6];
    rate_bytes[3] = frameData_plaintext[7];

    *rate = *(uint32_t*)rate_bytes;

    return stat;
}

UA_StatusCode
UA_ClientConfig_setISOBUS(UA_ClientConfig* config) {

    g_mySa = MY_CLIENT_SA;
    g_saOfOtherCf = MY_SERVER_SA;

    config->timeout = 5000;
    config->secureChannelLifeTime = 10 * 60 * 1000; /* 10 minutes */

    if (!config->logger.log) {
        config->logger = UA_Log_Stdout_withLevel(UA_LOGLEVEL_INFO);
    }

    if (config->sessionLocaleIdsSize > 0 && config->sessionLocaleIds) {
        UA_Array_delete(config->sessionLocaleIds, config->sessionLocaleIdsSize, &UA_TYPES[UA_TYPES_LOCALEID]);
    }
    config->sessionLocaleIds = NULL;
    config->sessionLocaleIds = 0;

    config->localConnectionConfig = UA_ConnectionConfig_default;

    /* Certificate Verification that accepts every certificate. Can be
     * overwritten when the policy is specialized. */
    UA_CertificateVerification_AcceptAll(&config->certificateVerification);
    UA_LOG_WARNING(&config->logger, UA_LOGCATEGORY_USERLAND,
        "AcceptAll Certificate Verification. "
        "Any remote certificate will be accepted.");

    /* With encryption enabled, the applicationUri needs to match the URI from
     * the certificate */
    config->clientDescription.applicationUri = UA_STRING_ALLOC(APPLICATION_URI);
    config->clientDescription.applicationType = UA_APPLICATIONTYPE_CLIENT;

    if (config->securityPoliciesSize > 0) {
        UA_LOG_ERROR(&config->logger, UA_LOGCATEGORY_NETWORK,
            "Could not initialize a config that already has SecurityPolicies");
        return UA_STATUSCODE_BADINTERNALERROR;
    }

    config->securityPolicies = (UA_SecurityPolicy*)UA_malloc(sizeof(UA_SecurityPolicy));
    if (!config->securityPolicies)
        return UA_STATUSCODE_BADOUTOFMEMORY;
    UA_StatusCode retval = UA_SecurityPolicy_None(config->securityPolicies,
        UA_BYTESTRING_NULL, &config->logger);
    if (retval != UA_STATUSCODE_GOOD) {
        UA_free(config->securityPolicies);
        config->securityPolicies = NULL;
        return retval;
    }
    config->securityPoliciesSize = 1;


    // ISOBUS PROJECT interesting this is where we will put the can message to indicate connection is ready
    config->initConnectionFunc = UA_ClientConnectionISOBUS_init; /* for async client */
    config->pollConnectionFunc = UA_ClientConnectionISOBUS_poll; /* for async connection */

    config->customDataTypes = NULL;
    config->stateCallback = NULL;
    config->connectivityCheckInterval = 0;

    config->requestedSessionTimeout = 1200000; /* requestedSessionTimeout */

    config->inactivityCallback = NULL;
    config->clientContext = NULL;

#ifdef UA_ENABLE_SUBSCRIPTIONS
    config->outStandingPublishRequests = 10;
    config->subscriptionInactivityCallback = NULL;
#endif

    return UA_STATUSCODE_GOOD;
}

UA_Connection
UA_ClientConnectionISOBUS_init(UA_ConnectionConfig config, const UA_String endpointUrl,
    UA_UInt32 timeout, const UA_Logger* logger) {
    UA_initialize_architecture_network();

    UA_Connection connection;
    memset(&connection, 0, sizeof(UA_Connection));

    connection.state = UA_CONNECTIONSTATE_OPENING;
    connection.sockfd = UA_INVALID_SOCKET;
    connection.send = connection_write_ISOBUS;
    connection.recv = connection_ISOBUS_recv; // ISOBUS PROJECT We have made a new function that can be used already for the server. connection_ISOBUS_recv
    connection.close = ClientNetworkLayerISOBUS_close;
    connection.free = ClientNetworkLayerISOBUS_free;
    connection.getSendBuffer = connection_getsendbuffer;
    connection.releaseSendBuffer = connection_releasesendbuffer;
    connection.releaseRecvBuffer = connection_releaserecvbuffer;

    ISOBUSClientConnection* isobusClientConnection = (ISOBUSClientConnection*)
        UA_malloc(sizeof(ISOBUSClientConnection));
    if (!isobusClientConnection) {
        connection.state = UA_CONNECTIONSTATE_CLOSED;
        return connection;
    }
    memset(isobusClientConnection, 0, sizeof(ISOBUSClientConnection));
    connection.handle = (void*)isobusClientConnection;
    isobusClientConnection->timeout = timeout;
    UA_String hostnameString = UA_STRING_NULL;
    UA_String pathString = UA_STRING_NULL;
    UA_UInt16 port = 0;
    char hostname[512];
    isobusClientConnection->connStart = UA_DateTime_nowMonotonic();
    UA_String_copy(&endpointUrl, &isobusClientConnection->endpointUrl);

    UA_StatusCode parse_retval =
        UA_parseEndpointUrl(&endpointUrl, &hostnameString, &port, &pathString);
    if (parse_retval != UA_STATUSCODE_GOOD || hostnameString.length > 511) {
        UA_LOG_WARNING(logger, UA_LOGCATEGORY_NETWORK,
            "Server url is invalid: %.*s",
            (int)endpointUrl.length, endpointUrl.data);
        connection.state = UA_CONNECTIONSTATE_CLOSED;
        return connection;
    }
    memcpy(hostname, hostnameString.data, hostnameString.length);
    hostname[hostnameString.length] = 0;

    if (port == 0) {
        port = 4840;
        UA_LOG_INFO(logger, UA_LOGCATEGORY_NETWORK,
            "No port defined, using default port %" PRIu16, port);
    }

    //memset(&tcpClientConnection->hints, 0, sizeof(tcpClientConnection->hints));
    //tcpClientConnection->hints.ai_family = AF_UNSPEC;
    //tcpClientConnection->hints.ai_socktype = SOCK_STREAM;
    //char portStr[6];
    //UA_snprintf(portStr, 6, "%d", port);
    //int error = UA_getaddrinfo(hostname, portStr, &tcpClientConnection->hints,
    //    &tcpClientConnection->server);
    //if (error != 0 || !tcpClientConnection->server) {
    //    UA_LOG_SOCKET_ERRNO_GAI_WRAP(UA_LOG_WARNING(logger, UA_LOGCATEGORY_NETWORK,
    //        "DNS lookup of %s failed with error %d - %s",
    //        hostname, error, errno_str));
    //    connection.state = UA_CONNECTIONSTATE_CLOSED;
    //    return connection;
    //}

    /* Return connection with state UA_CONNECTIONSTATE_OPENING */
    return connection;
}

UA_StatusCode
UA_ClientConnectionISOBUS_poll(UA_Connection* connection, UA_UInt32 timeout,
    const UA_Logger* logger) {
    if (connection->state == UA_CONNECTIONSTATE_CLOSED)
        return UA_STATUSCODE_BADDISCONNECT;
    if (connection->state == UA_CONNECTIONSTATE_ESTABLISHED)
        return UA_STATUSCODE_GOOD;

    /* Connection timeout? */
    ISOBUSClientConnection* isobusConnection = (ISOBUSClientConnection*)connection->handle;
    if (isobusConnection == NULL) {
        connection->state = UA_CONNECTIONSTATE_CLOSED;
        return UA_STATUSCODE_BADDISCONNECT;  // some thing is wrong
    }
    if ((UA_Double)(UA_DateTime_nowMonotonic() - isobusConnection->connStart)
    > (UA_Double)isobusConnection->timeout * UA_DATETIME_MSEC) {
        UA_LOG_WARNING(logger, UA_LOGCATEGORY_NETWORK, "Timed out");
        ClientNetworkLayerISOBUS_close(connection);
        return UA_STATUSCODE_BADDISCONNECT;
    }

    if (connection->state == UA_CONNECTIONSTATE_OPENING)
        //if (g_hnd == -1)
    {
        canStatus stat;
        if (g_hnd == -1)
            UA_setupIsobusChannel(&g_hnd, &stat, 0, canOPEN_ACCEPT_VIRTUAL);
        // send first bye of numOfChanschar sendSomething[1] = { 0 };
        char sendSomething[1] = { 0x0A };
        // send handshake id and wait for reply
        canWriteWait(g_hnd, INIT_HANDSHAKE_ID, sendSomething, 1, canMSG_EXT, 0xFFFFFFFF);
        stat = canReadSyncSpecific(g_hnd, INIT_HANDSHAKE_ID, 10000);
        if (stat != canOK) {
            UA_LOG_WARNING(logger, UA_LOGCATEGORY_NETWORK,
                "CAN handshake failed with error");
            return UA_STATUSCODE_BADDISCONNECT;
        }
        else {
            connection->state = UA_CONNECTIONSTATE_ESTABLISHED;
        }

    }

    return UA_STATUSCODE_GOOD;

}

void UA_setupIsobusChannel(canHandle* hnd, canStatus* stat, const int channel_number,
    const int flags) {
    // Next, we open up the channel and receive a handle to it. Depending on what
// devices you have connected to your computer, you might want to change the
// channel number. The canOPEN_ACCEPT_VIRTUAL flag means that it is ok to
// open the selected channel, even if it is on a virtual device.
    *hnd = 1;
    canHandle x = canOpenChannel(channel_number, flags);
    *hnd = x;
    //*hnd = canOpenChannel(channel_number, flags);

    // If the call to canOpenChannel is successful, it will return an integer
  // which is greater than or equal to zero. However, is something goes wrong,
  // it will return an error status which is a negative number.
    if (*hnd < 0) {
        // To check for errors and print any possible error message, we can use the
        // Check method.
        UA_Isobus_Check("canOpenChannel", (canStatus)*hnd);
        // and then exit the program.
        exit(1);
    }

    printf("Setting bitrate and going bus on\n");
    // Once we have successfully opened a channel, we need to set its bitrate. We
    // do this using canSetBusParams, which takes the handle and the desired
    // bitrate (another enumerable) as parameters. The rest of the parameters are
    // ignored since we are using a predefined bitrate.
    *stat = canSetBusParams(*hnd, canBITRATE_250K, 0, 0, 0, 0, 0);
    UA_Isobus_Check("canSetBusParams", *stat);
    // Next, take the channel on bus using the canBusOn method. This needs to be
    // done before we can send a message.
    *stat = canBusOn(*hnd);
    UA_Isobus_Check("canBusOn", *stat);
}

void UA_Isobus_Check(const char* info, canStatus stat) {
    if (stat != canOK) {
        char buf[50];
        buf[0] = '\0';
        canGetErrorText(stat, buf, sizeof(buf));
        //printf("%s: failed, stat=%d (%s)\n", info, (int)stat, buf);
        printf("%s: failed, stat=%d (%s)\n", info, (int)stat, buf);
        OutputDebugStringA(buf);
    }
}

static UA_StatusCode
connection_write_ISOBUS(UA_Connection* connection, UA_ByteString* buf) {
    if (connection->state == UA_CONNECTIONSTATE_CLOSED) {
        UA_ByteString_clear(buf);
        return UA_STATUSCODE_BADCONNECTIONCLOSED;
    }



    /* Send the full buffer. This may require several calls to send */

    //TODO, source address etc should be part of the connection
    // TODO pick a proper pgn
    int sa = g_mySa;
    int da = g_saOfOtherCf;
    int pgnTosend = 0x123456;
    int status = BlockingTpSendTo(sa, (char*)buf->data, buf->length, pgnTosend, da);
    if (status < 0)
    {
        connection->close(connection);
        UA_ByteString_clear(buf);
        return UA_STATUSCODE_BADCONNECTIONCLOSED;
    }

    // leave this here commented in case we awant it

    //size_t nWritten = 0;
    //do {
    //    ssize_t n = 0;
    //    do {
    //        size_t bytes_to_send = buf->length - nWritten;
    //        /*n = UA_send(connection->sockfd,
    //            (const char*)buf->data + nWritten,
    //            bytes_to_send, flags);*/
    //        n = BlockingTpSendTo(sa, (const char*)buf->data + nWritten, bytes_to_send, pgnTosend, da);
    //        if (n < 0) {
    //            if (UA_ERRNO != UA_INTERRUPTED && UA_ERRNO != UA_AGAIN) {
    //                connection->close(connection);
    //                UA_ByteString_clear(buf);
    //                return UA_STATUSCODE_BADCONNECTIONCLOSED;
    //            }
    //            int poll_ret;
    //            do {
    //                poll_ret = UA_poll(poll_fd, 1, 1000);
    //            } while (poll_ret == 0 || (poll_ret < 0 && UA_ERRNO == UA_INTERRUPTED));
    //        }
    //    } while (n < 0);

    //    nWritten += (size_t)n;
    //} while (nWritten < buf->length);

    /* Free the buffer */
    UA_ByteString_clear(buf);
    return UA_STATUSCODE_GOOD;
}

// ISOBUS TRANSPORT PROTOCOL FUNCTION 
int BlockingTpSendTo(int sa, char* data, int len, int pgnTosend, long da)
{
    int prio = 7;

    int pos = 0;

    //int numPacketsRequired = (len + 1784) / 1786; //https://www.reddit.com/r/C_Programming/comments/gqpuef/how_to_round_up_the_answer_of_an_integer_division/

    int  remainingLenForThisTpGroup = len;
    char outMsgData_Tp[8] = { 0 };
    long outMsgId_Tp;

    long inMsgId_tp = 0;
    char inMsgData_tp[8] = { 0 };
    unsigned int inLen = 0;
    long inPgn = 0;
    int timeoutloops = 9999;
    canStatus stat;
    if (len < 1785)
    {

        outMsgId_Tp = ((long)da << 8) | (long)sa | ((long)TP_CM_PGN << 8) | ((long)prio << 26);

        outMsgData_Tp[0] = 16; //RTS
        outMsgData_Tp[1] = len & 0xff;
        outMsgData_Tp[2] = (char)(len >> 8);  //TODO: needs swapping or not?
        if (remainingLenForThisTpGroup > 1785)
            outMsgData_Tp[3] = 255;
        else
            outMsgData_Tp[3] = (char)((remainingLenForThisTpGroup + 6) / 7); //https://www.reddit.com/r/C_Programming/comments/gqpuef/how_to_round_up_the_answer_of_an_integer_division/
        outMsgData_Tp[4] = 0xFF; //no limit - Maximum number of packets that can be sent in response to one CTS.
        outMsgData_Tp[5] = pgnTosend & 0xff;
        outMsgData_Tp[6] = (pgnTosend >> 8) & 0xff;
        outMsgData_Tp[7] = (pgnTosend >> 16) & 0xff;


        // do nortmal TP
        // send CM RTS
        canWriteSync(g_hnd, 0xFFFFFFFF);
        stat = canWrite(g_hnd, outMsgId_Tp, outMsgData_Tp, 8, canMSG_EXT); //begin
        canWriteSync(g_hnd, 0xFFFFFFFF);

        // wait CM CTS
        timeoutloops = 300;
        do
        {
            stat = canReadWait(g_hnd, &inMsgId_tp, inMsgData_tp, &inLen,
                NULL, NULL, 10); // 10 ms timeout
            inPgn = (inMsgId_tp >> 8) & 0x03FF00;
            if (stat == canERR_NOMSG)
            {
                stat = canWrite(g_hnd, outMsgId_Tp, outMsgData_Tp, 8, canMSG_EXT);
                --timeoutloops;
            }
            if (inPgn == TP_CM_PGN && inMsgData_tp[0] == 16)
            {
                g_blocked_by_sender_on_other_side = true;
                return -1; // other one is trying to send
            }
        } while (!(inPgn == TP_CM_PGN && inMsgData_tp[0] == 17) && timeoutloops > 0);

        if (timeoutloops <= 0)
            return -1;

        // send data (will always be max packets because using 2 pcs)
        outMsgData_Tp[0] = 0;
        char final_message[8] = { 255  , 255 , 255 , 255 , 255 , 255 , 255, 255 };
        long outMsgId_dt = ((long)da << 8) | (long)sa | ((long)TP_DT_PGN << 8) | ((long)prio << 26);

        int bytesInOneTp = 1785;

        do
        {
            outMsgData_Tp[0]++;

            if (remainingLenForThisTpGroup > 7)
            {
                memcpy(&outMsgData_Tp[1], &data[pos], 7);
            }
            else {
                memcpy(&outMsgData_Tp[1], &final_message[1], 7); //fill with ff
                memcpy(&outMsgData_Tp[1], &data[pos], remainingLenForThisTpGroup); //overwite with whatever data is left
            }

            stat = canWrite(g_hnd, outMsgId_dt, outMsgData_Tp, 8, canMSG_EXT);
            //canWriteSync(canHnd, 0xFFFFFFFF);
            remainingLenForThisTpGroup -= 7;
            pos += 7;
            bytesInOneTp -= 7;
        } while (remainingLenForThisTpGroup > 0 && bytesInOneTp > 0);


        // wait CM EOMA
        timeoutloops = 9999;
        do
        {
            stat = canReadWait(g_hnd, &inMsgId_tp, inMsgData_tp, &inLen,
                NULL, NULL, 10); // 10 ms timeout
            inPgn = (inMsgId_tp >> 8) & 0x03FF00;
        } while (!(inPgn == TP_CM_PGN && inMsgData_tp[0] == 19) && --timeoutloops > 0);

        if (timeoutloops <= 0)
            return -2;

    }
    else
    {
        // do extended TP
        // send RTS
        outMsgId_Tp = ((long)da << 8) | (long)sa | ((long)ETP_CM_PGN << 8) | ((long)prio << 26);
        outMsgData_Tp[0] = 20; // extended RTS
        outMsgData_Tp[1] = len & 0xff; //bytes to send
        outMsgData_Tp[2] = (char)(len >> 8);
        outMsgData_Tp[3] = (char)(len >> 16);
        outMsgData_Tp[4] = (char)(len >> 24);
        outMsgData_Tp[5] = pgnTosend & 0xff;
        outMsgData_Tp[6] = (pgnTosend >> 8) & 0xff;
        outMsgData_Tp[7] = (pgnTosend >> 16) & 0xff;

        stat = canWrite(g_hnd, outMsgId_Tp, outMsgData_Tp, 8, canMSG_EXT);

        // wait CTS
        do
        {

            stat = canReadWait(g_hnd, &inMsgId_tp, inMsgData_tp, &inLen,
                NULL, NULL, 10); // 10 ms timeout
            inPgn = (inMsgId_tp >> 8) & 0x03FF00;
        } while (!(inPgn == ETP_CM_PGN && inMsgData_tp[0] == 21) &&
            --timeoutloops > 0);

        if (timeoutloops <= 0)
            return -11;

        if (stat != canOK)
            return -12;

        timeoutloops = 1000;

        uint64_t offset = 0;
        uint8_t numPacketsToApplyOffset = 255;
        int bytesInOneTp;
        while (remainingLenForThisTpGroup > 0)
        {
            bytesInOneTp = 1785;
            outMsgData_Tp[0] = 0;
            // send DPO Data Packet Offset
            char outDpoData[8] = { 0 };
            outDpoData[0] = 22; //  Data Packet Offset
            outDpoData[1] = numPacketsToApplyOffset; // Num packets to apply offset (1 to 255)
            outDpoData[2] = offset & 0xff;
            outDpoData[3] = (char)(offset >> 8);
            outDpoData[4] = (char)(offset >> 16);
            outDpoData[5] = pgnTosend & 0xff;
            outDpoData[6] = (pgnTosend >> 8) & 0xff;
            outDpoData[7] = (pgnTosend >> 16) & 0xff;
            canWriteSync(g_hnd, 0xFFFFFFFF);
            stat = canWrite(g_hnd, outMsgId_Tp, outDpoData, 8, canMSG_EXT);
            canWriteSync(g_hnd, 0xFFFFFFFF);
            // send data
            char final_message[8] = { 255  , 255 , 255 , 255 , 255 , 255 , 255, 255 };
            long outMsgId_dt = ((long)da << 8) | (long)sa | ((long)ETP_DT_PGN << 8) | ((long)prio << 26);


            do
            {
                outMsgData_Tp[0]++;

                if (remainingLenForThisTpGroup > 7)
                {
                    memcpy(&outMsgData_Tp[1], &data[pos], 7);
                }
                else {
                    memcpy(&outMsgData_Tp[1], &final_message[1], 7); //fill with ff
                    memcpy(&outMsgData_Tp[1], &data[pos], remainingLenForThisTpGroup); //overwite with whatever data is left
                }

                stat = canWrite(g_hnd, outMsgId_dt, outMsgData_Tp, 8, canMSG_EXT);
                //canWriteSync(canHnd, 0xFFFFFFFF);
                remainingLenForThisTpGroup -= 7;
                pos += 7;
                bytesInOneTp -= 7;
            } while (remainingLenForThisTpGroup > 0 && bytesInOneTp > 0);

            offset += numPacketsToApplyOffset;
            // wait another CTS if there is more data we need to send 
            inPgn = 0;
            timeoutloops = 99999;
            if (remainingLenForThisTpGroup > 0) {
                inMsgData_tp[0] = 0;

                while (!(inPgn == ETP_CM_PGN && inMsgData_tp[0] == 21)
                    && --timeoutloops > 0)
                {
                    stat = canReadWait(g_hnd, &inMsgId_tp, inMsgData_tp, &inLen,
                        NULL, NULL, 10); // 10 ms timeout
                    inPgn = (inMsgId_tp >> 8) & 0x03FF00;
                }

                if (timeoutloops <= 0)
                    return -15;
            }
        }
        // wait EOMA
        timeoutloops = 99999;
        do
        {
            stat = canReadWait(g_hnd, &inMsgId_tp, inMsgData_tp, &inLen,
                NULL, NULL, 200); // 10 ms timeout
            inPgn = (inMsgId_tp >> 8) & 0x03FF00;
        } while (!(inPgn != ETP_CM_PGN && inMsgData_tp[0] != 23) && --timeoutloops > 0);
        canWriteSync(g_hnd, 0xFFFFFFFF);
        if (timeoutloops <= 0)
            return-3;
    }

    return 1;
}

static UA_StatusCode
connection_ISOBUS_recv(UA_Connection* connection, UA_ByteString* response,
    UA_UInt32 timeout) {
    if (connection->state == UA_CONNECTIONSTATE_CLOSED)
        return UA_STATUSCODE_BADCONNECTIONCLOSED;

    /* Listen on the socket for the given timeout until a message arrives */
    UA_UInt32 timeout_ms = timeout;
    canStatus stat = canReadSync(g_hnd, timeout_ms); //TODO: check specifically if someone is opening a TP connection with us?
    //fd_set fdset;
    //FD_ZERO(&fdset);
    //UA_fd_set(connection->sockfd, &fdset);
    //UA_UInt32 timeout_usec = timeout * 1000;
    //struct timeval tmptv = { (long int)(timeout_usec / 1000000),
    //                        (int)(timeout_usec % 1000000) };
    //int resultsize = UA_select(connection->sockfd + 1, &fdset, NULL, NULL, &tmptv);

    /* No result */
    if (stat == canERR_TIMEOUT)
        return UA_STATUSCODE_GOODNONCRITICALTIMEOUT;

    if (stat != canOK) {
        /* The call to select was interrupted. Act as if it timed out. */
        //if (UA_ERRNO == UA_INTERRUPTED)
        //    return UA_STATUSCODE_GOODNONCRITICALTIMEOUT;

        /* The error cannot be recovered. Close the connection. */
        connection->close(connection);
        return UA_STATUSCODE_BADCONNECTIONCLOSED;
    }

    // ISOBUS PROJECT if response length is 0, the default buffer size is allocated
    // if the allocation is already done (response length is something other than 0), done further allocation is done
    UA_Boolean internallyAllocated = !response->length;

    /* Allocate the buffer  */
    if (internallyAllocated) {
        size_t bufferSize = 16384; /* Use as default for a new SecureChannel */
        UA_SecureChannel* channel = connection->channel;
        if (channel && channel->config.recvBufferSize > 0)
            bufferSize = channel->config.recvBufferSize;
        UA_StatusCode res = UA_ByteString_allocBuffer(response, bufferSize);
        if (res != UA_STATUSCODE_GOOD)
            return res;
    }

    /* Get the received packet(s) */
    //ssize_t ret = UA_recv(connection->sockfd, (char*)response->data, response->length, 0);
    int maxLen = response->length;
    long incomingPgn = 0;
    long senderSa = 0; // set by recv function
    long mySa = g_mySa; //TODO: make source address a global (different for client and server)
    ssize_t ret = BlockingTpRecieveFrom((char*)response->data, maxLen, &incomingPgn, &senderSa, mySa);
    int retryCounter = 200;
    while ((ret == -1 || ret == -2 || ret == -13) && retryCounter-- > 0)
    {
        ret = BlockingTpRecieveFrom((char*)response->data, maxLen, &incomingPgn, &senderSa, mySa);
    }

    /* The remote side closed the connection */
    if (ret == -1 || ret == -2) {
        if (internallyAllocated)
            UA_ByteString_clear(response);
        return UA_STATUSCODE_GOOD;
    }

    /* Error case */
    if (ret < 0) {
        if (internallyAllocated)
            UA_ByteString_clear(response);
        if (UA_ERRNO == UA_INTERRUPTED || (timeout > 0) ?
            false : (UA_ERRNO == UA_EAGAIN || UA_ERRNO == UA_WOULDBLOCK))
            return UA_STATUSCODE_GOOD; /* statuscode_good but no data -> retry */
        connection->close(connection);
        return UA_STATUSCODE_BADCONNECTIONCLOSED;
    }

    /* Set the length of the received buffer */
    response->length = (size_t)ret;
    return UA_STATUSCODE_GOOD;
}

int BlockingTpRecieveFrom(char* data, int maxLen, long* incomingPgn, long* sendersSa, long mySa)
{
    int prio = 7;
    long sa = mySa;
    unsigned int pos = 0;

    int numPacketsRequired;

    //int remainingLenForThisTpGroup = len;
    char outMsgData_Tp[8] = { 0 };
    long outMsgId_Tp;

    long inMsgId_tp = 0;
    uint8_t inMsgData_tp[8] = { 0 };
    long inPgn = 0;
    unsigned int inLen = 0;
    int timeoutloops = 99999;
    unsigned int incomingBytesLength;
    canStatus stat;

    // recieve RTS or extrended RTS
    do {
        stat = canReadWait(g_hnd, &inMsgId_tp, inMsgData_tp, &inLen,
            NULL, NULL, 10); // 100 ms timeout


        if (inLen != 8 || stat != canOK)
            return -1;

        inPgn = (inMsgId_tp >> 8) & 0x03FF00;
        if (inPgn != TP_CM_PGN && inPgn != ETP_CM_PGN)
            return -2;

        if (inPgn == TP_CM_PGN) {
            if (inMsgData_tp[0] != 16)
                return -2;
        }
        else {
            if (inMsgData_tp[0] != 20)
                return -2;
        }

    } while (g_blocked_by_sender_on_other_side);

    g_blocked_by_sender_on_other_side = false;

    *sendersSa = (inMsgId_tp & 0xFF);
    auto da = *sendersSa;
    *incomingPgn = (long)inMsgData_tp[5] + ((long)inMsgData_tp[6] << 8) + ((long)inMsgData_tp[7] << 16);

    int new_pos = 0;

    if (inPgn == TP_CM_PGN) // normal tp
    {
        if (inMsgData_tp[0] != 16)
            return -3;

        // read RTS msg
        numPacketsRequired = (inMsgData_tp[3] + 1784) / 1786; //https://www.reddit.com/r/C_Programming/comments/gqpuef/how_to_round_up_the_answer_of_an_integer_division/
        incomingBytesLength = ((unsigned int)inMsgData_tp[2] & 0x00FF) * 256;
        incomingBytesLength += ((unsigned int)inMsgData_tp[1] & 0x00FF);
        uint8_t remainingPackets = min(inMsgData_tp[3], inMsgData_tp[4]);
        uint8_t original_remainingPackets = remainingPackets;

        //send CTS
        outMsgId_Tp = ((long)da << 8) | (long)sa | ((long)TP_CM_PGN << 8) | ((long)prio << 26);
        outMsgData_Tp[0] = 17; // CTS
        outMsgData_Tp[1] = remainingPackets; //bytes to send
        outMsgData_Tp[2] = 1;
        outMsgData_Tp[3] = 0xff;
        outMsgData_Tp[4] = 0xff;
        outMsgData_Tp[5] = *incomingPgn & 0xff;
        outMsgData_Tp[6] = (*incomingPgn >> 8) & 0xff;
        outMsgData_Tp[7] = (*incomingPgn >> 16) & 0xff;

        stat = canWrite(g_hnd, outMsgId_Tp, outMsgData_Tp, 8, canMSG_EXT);

        // recieve TP.DT with data repeatedly
        while (remainingPackets > 0)
        {


            stat = canReadWait(g_hnd, &inMsgId_tp, inMsgData_tp, &inLen,
                NULL, NULL, 1000); // 1000 ms timeout

            if (stat != canOK)
                return -4;

            // if a new connection management is started then we can't continue
            if (((inMsgId_tp) & 0x03FFFFFF) == ((TP_CM_PGN << 8) | 0xFF00 | *sendersSa))
            {
                return -5;

            }
            else if ((inMsgId_tp & 0x03FFFFFF) ==
                ((TP_DT_PGN << 8) | (mySa << 8) | *sendersSa))// msg is data
            {
                --remainingPackets;
                //sometimes packets are out of order (only on virtual CAn bus maybe???)
                // solve this by writing to the data array at the position of the data as indicated in the 1st byte of can msg. Also need ot account for large BAM messages over 255 packets
                new_pos = (7 * ((unsigned char)inMsgData_tp[0] - 1));
                memcpy(&data[new_pos], &inMsgData_tp[1], 7);
                pos += 7;
            }
        }
        // send another CTS if needed (not needed because we will both have max buffer of 255)
        // recieve more data if needed (not needed)
        // send CM_EOMA
        outMsgData_Tp[0] = 19; // EOMA
        outMsgData_Tp[1] = incomingBytesLength & 0xff;; // bytes recieved
        outMsgData_Tp[2] = (incomingBytesLength >> 8) & 0xff;;
        outMsgData_Tp[3] = original_remainingPackets;
        outMsgData_Tp[4] = 0xff;
        stat = canWrite(g_hnd, outMsgId_Tp, outMsgData_Tp, 8, canMSG_EXT);
        canWriteSync(g_hnd, 0xFFFFFFFF);
        if (pos >= incomingBytesLength)
            return incomingBytesLength;
        else
            return -8;

    }
    else // extended tp
    {
        if (inMsgData_tp[0] != 20)
            return -13;
        // read RTS msg
        numPacketsRequired;
        incomingBytesLength = ((unsigned int)inMsgData_tp[4] & 0x00FF) * 256 * 256 * 256;
        incomingBytesLength += ((unsigned int)inMsgData_tp[3] & 0x00FF) * 256 * 256;
        incomingBytesLength += ((unsigned int)inMsgData_tp[2] & 0x00FF) * 256;
        incomingBytesLength += ((unsigned int)inMsgData_tp[1] & 0x00FF);
        auto totalremainingPackets = (incomingBytesLength + 6) / 7;
        auto numTpRoundsRequired =
            (incomingBytesLength - 1 + (7 * 255)) / (7 * 255); //https://www.reddit.com/r/C_Programming/comments/gqpuef/how_to_round_up_the_answer_of_an_integer_division/

        uint64_t dpo_offset = 0;

        // repeat send CTS -> wait DPO -> read data:
        uint64_t nextPacketNum = 1;

        for (int i = numTpRoundsRequired; i > 0; i--)
        {
            auto remainingPacketsThisTpRound = min(255, totalremainingPackets);
            // send extended CTS
            outMsgId_Tp = ((long)da << 8) | (long)sa | ((long)ETP_CM_PGN << 8) | ((long)prio << 26);
            outMsgData_Tp[0] = 21; // extended CTS
            outMsgData_Tp[1] = 0xff; //bytes to send (255)
            outMsgData_Tp[2] = nextPacketNum & 0xff; //next packet number to send
            outMsgData_Tp[3] = (nextPacketNum >> 8) & 0xff;
            outMsgData_Tp[4] = (nextPacketNum >> 16) & 0xff;
            outMsgData_Tp[5] = *incomingPgn & 0xff;
            outMsgData_Tp[6] = (*incomingPgn >> 8) & 0xff;
            outMsgData_Tp[7] = (*incomingPgn >> 16) & 0xff;
            canWriteSync(g_hnd, 0xFFFFFFFF);
            stat = canWrite(g_hnd, outMsgId_Tp, outMsgData_Tp, 8, canMSG_EXT); //begin broadcast
            canWriteSync(g_hnd, 0xFFFFFFFF);

            // wait DPO
            do
            {
                stat = canReadWait(g_hnd, &inMsgId_tp, inMsgData_tp, &inLen,
                    NULL, NULL, 10); // 10 ms timeout
                inPgn = (inMsgId_tp >> 8) & 0x03FF00;
            } while (!(inPgn == ETP_CM_PGN && inMsgData_tp[0] == 22) && --timeoutloops > 0);

            if (timeoutloops <= 0)
                return -15;

            timeoutloops = 9999;
            //TODO: actually should look at the DPO data

            // recieve some data
            while (remainingPacketsThisTpRound > 0)
            {
                if (pos > maxLen)
                    return -6;

                stat = canReadWait(g_hnd, &inMsgId_tp, inMsgData_tp, &inLen,
                    NULL, NULL, 10000); // 10000 ms timeout

                if (stat != canOK)
                    return -4;

                if ((inMsgId_tp & 0x03FFFFFF) ==
                    ((ETP_DT_PGN << 8) | (mySa << 8) | *sendersSa))
                {// msg is data
                    --remainingPacketsThisTpRound;
                    --totalremainingPackets;
                    //sometimes packets are out of order (only on virtual CAn bus maybe???)
                    // solve this by writing to the data array at the position of the data as indicated in the 1st byte of can msg. Also need ot account for large BAM messages over 255 packets
                    new_pos = (7 * ((unsigned char)inMsgData_tp[0] - 1))
                        + ((numTpRoundsRequired - i) * 255 * 7);
                    nextPacketNum++;

                    memcpy(&data[new_pos], &inMsgData_tp[1], 7);
                    pos += 7;
                }
            }

        }
        //////



        // send extended EOMA
        outMsgId_Tp = ((long)da << 8) | (long)sa | ((long)ETP_CM_PGN << 8) | ((long)prio << 26);
        outMsgData_Tp[0] = 23; // extended EOMA
        outMsgData_Tp[1] = incomingBytesLength & 0xff;
        outMsgData_Tp[2] = incomingBytesLength >> 8;
        outMsgData_Tp[3] = incomingBytesLength >> 16;
        outMsgData_Tp[4] = incomingBytesLength >> 24;
        outMsgData_Tp[5] = *incomingPgn & 0xff;
        outMsgData_Tp[6] = (*incomingPgn >> 8) & 0xff;
        outMsgData_Tp[7] = (*incomingPgn >> 16) & 0xff;
        canWriteSync(g_hnd, 0xFFFFFFFF);
        stat = canWrite(g_hnd, outMsgId_Tp, outMsgData_Tp, 8, canMSG_EXT); //begin broadcast
        canWriteSync(g_hnd, 0xFFFFFFFF);

        if (pos >= incomingBytesLength)
            return incomingBytesLength;
        else
            return -18;

    }
}

static void
ClientNetworkLayerISOBUS_close(UA_Connection* connection) {
    // TODO error checking of canbusoff
    connection->state = UA_CONNECTIONSTATE_CLOSED;
}

static void
ClientNetworkLayerISOBUS_free(UA_Connection* connection) {
    if (!connection->handle)
        return;

    //canClose(g_hnd);
    //g_hnd = -1;
    ISOBUSClientConnection* isobusConnection = (ISOBUSClientConnection*)connection->handle;
    UA_String_clear(&isobusConnection->endpointUrl);
    UA_free(isobusConnection);
    connection->handle = NULL;
}

/*same as the original function. not changed for isobus*/
static UA_StatusCode
connection_getsendbuffer(UA_Connection* connection,
    size_t length, UA_ByteString* buf) {
    UA_SecureChannel* channel = connection->channel;
    if (channel && channel->config.sendBufferSize < length)
        return UA_STATUSCODE_BADCOMMUNICATIONERROR;
    return UA_ByteString_allocBuffer(buf, length);
}

/*same as the original function. not changed for isobus*/
static void
connection_releasesendbuffer(UA_Connection* connection,
    UA_ByteString* buf) {
    UA_ByteString_clear(buf);
}

/*same as the original function. not changed for isobus*/
static void
connection_releaserecvbuffer(UA_Connection* connection,
    UA_ByteString* buf) {
    UA_ByteString_clear(buf);
}

// server stuff


/* Struct initialization works across ANSI C/C99/C++ if it is done when the
 * variable is first declared. Assigning values to existing structs is
 * heterogeneous across the three. */
static UA_INLINE UA_UInt32Range
UA_UINT32RANGE(UA_UInt32 min, UA_UInt32 max) {
    UA_UInt32Range range = { min, max };
    return range;
}

static UA_INLINE UA_DurationRange
UA_DURATIONRANGE(UA_Duration min, UA_Duration max) {
    UA_DurationRange range = { min, max };
    return range;
}


// commented, moved to header
//static UA_INLINE UA_StatusCode
//UA_ServerConfig_setISOBUS(UA_ServerConfig* config) {
//    return UA_ServerConfig_setMinimalISOBUS(config, 4840, NULL);
//}
//
//static UA_INLINE UA_StatusCode
//UA_ServerConfig_setMinimalISOBUS(UA_ServerConfig* config, UA_UInt16 portNumber,
//    const UA_ByteString* certificate) {
//    return UA_ServerConfig_setMinimalCustomBuffer_ISOBUS(config, portNumber,
//        certificate, 0, 0);
//}

UA_StatusCode
UA_ServerConfig_setMinimalCustomBuffer_ISOBUS(UA_ServerConfig* config, UA_UInt16 portNumber,
    const UA_ByteString* certificate,
    UA_UInt32 sendBufferSize,
    UA_UInt32 recvBufferSize) {

    g_saOfOtherCf = MY_CLIENT_SA;
    g_mySa = MY_SERVER_SA;

    if (!config)
        return UA_STATUSCODE_BADINVALIDARGUMENT;

    UA_StatusCode retval = setDefaultConfig(config);
    if (retval != UA_STATUSCODE_GOOD) {
        UA_ServerConfig_clean(config);
        return retval;
    }

    retval = addISOBUSNetworkLayers(config, portNumber, sendBufferSize, recvBufferSize);
    if (retval != UA_STATUSCODE_GOOD) {
        UA_ServerConfig_clean(config);
        return retval;
    }

    /* Allocate the SecurityPolicies */
    retval = UA_ServerConfig_addSecurityPolicyNone(config, certificate);
    if (retval != UA_STATUSCODE_GOOD) {
        UA_ServerConfig_clean(config);
        return retval;
    }

    /* Initialize the Access Control plugin */
    retval = UA_AccessControl_default(config, true, NULL,
        &config->securityPolicies[config->securityPoliciesSize - 1].policyUri,
        usernamePasswordsSize, usernamePasswords);
    if (retval != UA_STATUSCODE_GOOD) {
        UA_ServerConfig_clean(config);
        return retval;
    }

    /* Allocate the endpoint */
    retval = UA_ServerConfig_addEndpoint(config, UA_SECURITY_POLICY_NONE_URI,
        UA_MESSAGESECURITYMODE_NONE);
    if (retval != UA_STATUSCODE_GOOD) {
        UA_ServerConfig_clean(config);
        return retval;
    }

    UA_LOG_WARNING(&config->logger, UA_LOGCATEGORY_USERLAND,
        "AcceptAll Certificate Verification. "
        "Any remote certificate will be accepted.");

    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
addISOBUSNetworkLayers(UA_ServerConfig* conf, UA_UInt16 portNumber,
    UA_UInt32 sendBufferSize, UA_UInt32 recvBufferSize) {
    return UA_ServerConfig_addNetworkLayerISOBUS(conf, portNumber, sendBufferSize, recvBufferSize);
}

UA_StatusCode
UA_ServerConfig_addNetworkLayerISOBUS(UA_ServerConfig* conf, UA_UInt16 portNumber,
    UA_UInt32 sendBufferSize, UA_UInt32 recvBufferSize) {
    /* Add a network layer */
    UA_ServerNetworkLayer* tmp = (UA_ServerNetworkLayer*)
        UA_realloc(conf->networkLayers,
            sizeof(UA_ServerNetworkLayer) * (1 + conf->networkLayersSize));
    if (!tmp)
        return UA_STATUSCODE_BADOUTOFMEMORY;
    conf->networkLayers = tmp;

    UA_ConnectionConfig config = UA_ConnectionConfig_default;
    if (sendBufferSize > 0)
        config.sendBufferSize = sendBufferSize;
    if (recvBufferSize > 0)
        config.recvBufferSize = recvBufferSize;

    conf->networkLayers[conf->networkLayersSize] =
        UA_ServerNetworkLayerISOBUS(config, portNumber, 0);
    if (!conf->networkLayers[conf->networkLayersSize].handle)
        return UA_STATUSCODE_BADOUTOFMEMORY;
    conf->networkLayersSize++;

    return UA_STATUSCODE_GOOD;
}

UA_ServerNetworkLayer
UA_ServerNetworkLayerISOBUS(UA_ConnectionConfig config, UA_UInt16 port,
    UA_UInt16 maxConnections) {
    UA_ServerNetworkLayer nl;
    memset(&nl, 0, sizeof(UA_ServerNetworkLayer));
    nl.clear = ServerNetworkLayerISOBUS_clear;
    nl.localConnectionConfig = config;
    nl.start = ServerNetworkLayerISOBUS_start;
    nl.listen = ServerNetworkLayerISOBUS_listen;
    nl.stop = ServerNetworkLayerISOBUS_stop;
    nl.handle = NULL;

    ServerNetworkLayerISOBUS* layer = (ServerNetworkLayerISOBUS*)
        UA_calloc(1, sizeof(ServerNetworkLayerISOBUS));
    if (!layer)
        return nl;
    nl.handle = layer;

    layer->port = port;
    layer->maxConnections = maxConnections;

    return nl;
}

/* run only when the server is stopped */
static void
ServerNetworkLayerISOBUS_clear(UA_ServerNetworkLayer* nl) {
    ServerNetworkLayerISOBUS* layer = (ServerNetworkLayerISOBUS*)nl->handle;
    UA_String_clear(&nl->discoveryUrl);

    /* Hard-close and remove remaining connections. The server is no longer
     * running. So this is safe. */
    ConnectionEntry* e, * e_tmp;
    LIST_FOREACH_SAFE(e, &layer->connections, pointers, e_tmp) {
        LIST_REMOVE(e, pointers);
        layer->connectionsSize--;
        //UA_close(e->connection.sockfd);
        UA_free(e);
        if (nl->statistics) {
            nl->statistics->currentConnectionCount--;
        }
    }

    /* Free the layer */
    UA_free(layer);
}

static UA_StatusCode
ServerNetworkLayerISOBUS_start(UA_ServerNetworkLayer* nl, const UA_Logger* logger,
    const UA_String* customHostname) {

    //UA_initialize_architecture_network();

    ServerNetworkLayerISOBUS* layer = (ServerNetworkLayerISOBUS*)nl->handle;
    layer->logger = logger;

    ///* Get addrinfo of the server and create server sockets */
    //char hostname[512];
    //if (customHostname->length) {
    //    if (customHostname->length >= sizeof(hostname))
    //        return UA_STATUSCODE_BADOUTOFMEMORY;
    //    memcpy(hostname, customHostname->data, customHostname->length);
    //    hostname[customHostname->length] = '\0';
    //}

    //char portno[6];
    //UA_snprintf(portno, 6, "%d", layer->port);

//    struct addrinfo hints, * res;
//    memset(&hints, 0, sizeof hints);
//#if UA_IPV6
//    hints.ai_family = AF_UNSPEC; /* allow IPv4 and IPv6 */
//#else
//    hints.ai_family = AF_INET;   /* enforce IPv4 only */
//#endif
//    hints.ai_socktype = SOCK_STREAM;
//    hints.ai_flags = AI_PASSIVE;
//#ifdef AI_ADDRCONFIG
//    hints.ai_flags |= AI_ADDRCONFIG;
//#endif
//    hints.ai_protocol = IPPROTO_TCP;
    //int retcode = UA_getaddrinfo(customHostname->length ? hostname : NULL,
    //    portno, &hints, &res);
    //if (retcode != 0) {
    //    UA_LOG_SOCKET_ERRNO_GAI_WRAP(UA_LOG_WARNING(layer->logger, UA_LOGCATEGORY_NETWORK,
    //        "getaddrinfo lookup of %s failed with error %d - %s", hostname, retcode, errno_str));
    //    return UA_STATUSCODE_BADINTERNALERROR;
    //}

    /* There might be serveral addrinfos (for different network cards,
     * IPv4/IPv6). Add a server socket for all of them. */
     //struct addrinfo* ai = res;
     //for (layer->serverSocketsSize = 0;
     //    layer->serverSocketsSize < FD_SETSIZE && ai != NULL;
     //    ai = ai->ai_next) {
     //    addServerSocket(layer, ai);
     //}
     //UA_freeaddrinfo(res);

     //if (layer->serverSocketsSize == 0) {
     //    return UA_STATUSCODE_BADCOMMUNICATIONERROR;
     //}

     /* Get the discovery url from the hostname */
     // ISOBUS COMMENT, we're hardcoding the discoveryurl so we dont use the IP hostname
    UA_String du = UA_STRING_NULL;
    char discoveryUrlBuffer[256] = { "ISOBUS_discover" };
    //if (customHostname->length) {
        // ISOBUS TODO, do we want form opc.tcp://%.*s:%d/
        //du.length = (size_t)UA_snprintf(discoveryUrlBuffer, 255, "opc.tcp://%.*s:%d/",
        //    (int)customHostname->length, customHostname->data,
        //    layer->port);
    du.length = 16;
    du.data = (UA_Byte*)discoveryUrlBuffer;
    //}
    //else {
    //    char hostnameBuffer[256];
    //    if (UA_gethostname(hostnameBuffer, 255) == 0) {
    //        du.length = (size_t)UA_snprintf(discoveryUrlBuffer, 255, "opc.tcp://%s:%d/",
    //            hostnameBuffer, layer->port);
    //        du.data = (UA_Byte*)discoveryUrlBuffer;
    //    }
    //    else {
    //        UA_LOG_ERROR(layer->logger, UA_LOGCATEGORY_NETWORK, "Could not get the hostname");
    //        return UA_STATUSCODE_BADINTERNALERROR;
    //    }
    //}
    UA_String_copy(&du, &nl->discoveryUrl);

    // ignore hostname
    // ignore layer->port
    // ignore discovery url

    // &nl->discoveryUrl == "isobusURL";

    canStatus stat;
    UA_setupIsobusChannel(&g_hnd, &stat, 0, canOPEN_ACCEPT_VIRTUAL);
    // TODO error checking of canbusoff

    UA_LOG_INFO(layer->logger, UA_LOGCATEGORY_NETWORK,
        "I hardcoded the dicoverurl/n/rISOBUS network layer listening on %.*s",
        (int)nl->discoveryUrl.length, nl->discoveryUrl.data);
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
ServerNetworkLayerISOBUS_listen(UA_ServerNetworkLayer* nl, UA_Server* server,
    UA_UInt16 timeout) {
    /* Every open socket can generate two jobs */
    ServerNetworkLayerISOBUS* layer = (ServerNetworkLayerISOBUS*)nl->handle;



    /* Accept new connections via the server sockets */

    //ISOBUS TODO should this block????

    canStatus stat = canReadSyncSpecific(g_hnd, INIT_HANDSHAKE_ID, 0);
    if (stat == canOK) // if the handshake msg is there
    {
        unsigned int len;
        unsigned int time;
        unsigned long flags;
        unsigned char p[8] = { 0 };
        canReadSpecific(g_hnd, INIT_HANDSHAKE_ID, (void*)p, &len, &flags, &time);
        char sendSomething[1] = { 0 };
        stat = canWriteWait(g_hnd, INIT_HANDSHAKE_ID, (void*)&sendSomething, 1, canMSG_EXT, 0xFFFFFFFF);
        if (stat != canOK)
            return UA_STATUSCODE_GOOD; // Do I want to return good?

        // TODO
        UA_LOG_TRACE(layer->logger, UA_LOGCATEGORY_NETWORK,
            "Connection | ServerNetworkLayerISOBUS_listen | New ISOBUS connection on server sockeyt");

        // TODO isobus THIS WILL CAUSE ERRORS IF THE CLIENT SENDS TOO MANY connections init msgs
        //if (g_hnd == -1)
        ServerNetworkLayerISOBUS_add(nl, layer);

        return UA_STATUSCODE_GOOD; // Do I want to return good?
    }
    else if (g_hnd == -1 || stat != canERR_TIMEOUT)
    {
        g_hnd = canOpenChannel(0, canOPEN_ACCEPT_VIRTUAL);
        printf("1");
        return UA_STATUSCODE_GOOD;
    }

    // need to intercept TC messages here otherwise the TP function removes them from the queue
    uint32_t rate; uint16_t element_num; uint16_t DDI;
    if (g_tc2ecu_using_bf_enc)
    {
        stat = readRateWithEncryption(&rate, &element_num, &DDI);
        if (stat == canOK)
            setMemTCThings(rate, element_num, DDI);
    }

    /* Read from established sockets */
    ConnectionEntry* e, * e_tmp;
    UA_DateTime now = UA_DateTime_nowMonotonic();
    LIST_FOREACH_SAFE(e, &layer->connections, pointers, e_tmp) {
        if ((e->connection.state == UA_CONNECTIONSTATE_OPENING) &&
            (now > (e->connection.openingDate + (NOHELLOTIMEOUT * UA_DATETIME_MSEC)))) {
            UA_LOG_INFO(layer->logger, UA_LOGCATEGORY_NETWORK,
                "Connection | ServerNetworkLayerISOBUS_listen | Closed by the server (no Hello Message)");
            LIST_REMOVE(e, pointers);
            layer->connectionsSize--;
            UA_close(e->connection.sockfd);
            UA_Server_removeConnection(server, &e->connection);
            if (nl->statistics) {
                nl->statistics->connectionTimeoutCount++;
                nl->statistics->currentConnectionCount--;
            }
            continue;
        }


        UA_LOG_TRACE(layer->logger, UA_LOGCATEGORY_NETWORK,
            "Connection | ServerNetworkLayerISOBUS_listen | Activity on ISOBUS");

        UA_ByteString buf = UA_BYTESTRING_NULL;
        UA_StatusCode retval = connection_ISOBUS_recv(&e->connection, &buf, 0);

        if (retval == UA_STATUSCODE_GOOD) {
            /* Process packets */
            UA_Server_processBinaryMessage(server, &e->connection, &buf);
            connection_releaserecvbuffer(&e->connection, &buf);
        }
        else if (retval == UA_STATUSCODE_BADCONNECTIONCLOSED) {
            /* The socket is shutdown but not closed */
            UA_LOG_INFO(layer->logger, UA_LOGCATEGORY_NETWORK,
                "Connection %i | Closed",
                (int)(e->connection.sockfd));
            LIST_REMOVE(e, pointers);
            layer->connectionsSize--;
            UA_close(e->connection.sockfd);
            UA_Server_removeConnection(server, &e->connection);
            if (nl->statistics) {
                nl->statistics->currentConnectionCount--;
            }
        }
    }
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
ServerNetworkLayerISOBUS_add(UA_ServerNetworkLayer* nl, ServerNetworkLayerISOBUS* layer)
{
    /* Allocate and initialize the connection */
    ConnectionEntry* e = (ConnectionEntry*)UA_malloc(sizeof(ConnectionEntry));
    if (!e) {
        return UA_STATUSCODE_BADOUTOFMEMORY;
    }

    UA_Connection* c = &e->connection;
    memset(c, 0, sizeof(UA_Connection));
    c->sockfd = 0;
    c->handle = layer;
    c->send = connection_write_ISOBUS;
    c->close = ServerNetworkLayerISOBUS_close;
    c->free = ServerNetworkLayerTCP_freeConnection; // just calls free(connection)
    c->getSendBuffer = connection_getsendbuffer;
    c->releaseSendBuffer = connection_releasesendbuffer;
    c->releaseRecvBuffer = connection_releaserecvbuffer;
    c->state = UA_CONNECTIONSTATE_OPENING;
    c->openingDate = UA_DateTime_nowMonotonic();

    layer->connectionsSize++;

    /* Add to the linked list */
    LIST_INSERT_HEAD(&layer->connections, e, pointers);
    if (nl->statistics) {
        nl->statistics->currentConnectionCount++;
        nl->statistics->cumulatedConnectionCount++;
    }
    return UA_STATUSCODE_GOOD;
}

static void
ServerNetworkLayerISOBUS_close(UA_Connection* connection) {
    //canStatus stat = canBusOff(g_hnd);
    // TODO error checking of canbusoff
    //g_hnd = -1;
    connection->state = UA_CONNECTIONSTATE_CLOSED;
}

static void
ServerNetworkLayerTCP_freeConnection(UA_Connection* connection) {
    UA_free(connection);
}

static void
ServerNetworkLayerISOBUS_stop(UA_ServerNetworkLayer* nl, UA_Server* server) {
    ServerNetworkLayerISOBUS* layer = (ServerNetworkLayerISOBUS*)nl->handle;
    UA_LOG_INFO(layer->logger, UA_LOGCATEGORY_NETWORK,
        "Shutting down the ISOBUS network layer (if this was implemented)");

    /* Close the server sockets */
    for (UA_UInt16 i = 0; i < layer->serverSocketsSize; i++) {
        UA_shutdown(layer->serverSockets[i], 2);
        UA_close(layer->serverSockets[i]);
    }
    layer->serverSocketsSize = 0;

    /* Close open connections */
    ConnectionEntry* e;
    LIST_FOREACH(e, &layer->connections, pointers)
        ServerNetworkLayerTCP_close(&e->connection);

    ///* Run recv on client sockets. This picks up the closed sockets and frees
    // * the connection. */
    //ServerNetworkLayerTCP_listen(nl, server, 0);

    UA_deinitialize_architecture_network();
}

/* This performs only 'shutdown'. 'close' is called when the shutdown
 * socket is returned from select. */
static void
ServerNetworkLayerTCP_close(UA_Connection* connection) {
    if (connection->state == UA_CONNECTIONSTATE_CLOSED)
        return;
    UA_shutdown((UA_SOCKET)connection->sockfd, 2);
    connection->state = UA_CONNECTIONSTATE_CLOSED;
}

static UA_StatusCode
setDefaultConfig(UA_ServerConfig* conf) {
    if (!conf)
        return UA_STATUSCODE_BADINVALIDARGUMENT;

    if (conf->nodestore.context == NULL)
        UA_Nodestore_HashMap(&conf->nodestore);

    /* --> Start setting the default static config <-- */
    /* Allow user to set his own logger */
    if (!conf->logger.log)
        conf->logger = UA_Log_Stdout_;

    conf->shutdownDelay = 0.0;

    /* Server Description */
    UA_BuildInfo_clear(&conf->buildInfo);
    conf->buildInfo.productUri = UA_STRING_ALLOC(PRODUCT_URI);
    conf->buildInfo.manufacturerName = UA_STRING_ALLOC(MANUFACTURER_NAME);
    conf->buildInfo.productName = UA_STRING_ALLOC(PRODUCT_NAME);
    conf->buildInfo.softwareVersion =
        UA_STRING_ALLOC(VERSION(UA_OPEN62541_VER_MAJOR, UA_OPEN62541_VER_MINOR,
            UA_OPEN62541_VER_PATCH, UA_OPEN62541_VER_LABEL));
#ifdef UA_PACK_DEBIAN
    conf->buildInfo.buildNumber = UA_STRING_ALLOC("deb");
#else
    conf->buildInfo.buildNumber = UA_STRING_ALLOC(__DATE__ " " __TIME__);
#endif
    conf->buildInfo.buildDate = UA_DateTime_now();

    UA_ApplicationDescription_clear(&conf->applicationDescription);
    conf->applicationDescription.applicationUri = UA_STRING_ALLOC(APPLICATION_URI_SERVER);
    conf->applicationDescription.productUri = UA_STRING_ALLOC(PRODUCT_URI);
    conf->applicationDescription.applicationName =
        UA_LOCALIZEDTEXT_ALLOC("en", APPLICATION_NAME);
    conf->applicationDescription.applicationType = UA_APPLICATIONTYPE_SERVER;
    /* conf->applicationDescription.gatewayServerUri = UA_STRING_NULL; */
    /* conf->applicationDescription.discoveryProfileUri = UA_STRING_NULL; */
    /* conf->applicationDescription.discoveryUrlsSize = 0; */
    /* conf->applicationDescription.discoveryUrls = NULL; */

#ifdef UA_ENABLE_DISCOVERY_MULTICAST
    UA_MdnsDiscoveryConfiguration_clear(&conf->mdnsConfig);
    conf->mdnsInterfaceIP = UA_STRING_NULL;
# if !defined(UA_HAS_GETIFADDR)
    conf->mdnsIpAddressList = NULL;
    conf->mdnsIpAddressListSize = 0;
# endif
#endif

    /* Custom DataTypes */
    /* conf->customDataTypesSize = 0; */
    /* conf->customDataTypes = NULL; */

    /* Networking */
    /* conf->networkLayersSize = 0; */
    /* conf->networkLayers = NULL; */
    /* conf->customHostname = UA_STRING_NULL; */
    /* conf->customHostname = UA_STRING_NULL; */

    /* Endpoints */
    /* conf->endpoints = {0, NULL}; */

    /* Certificate Verification that accepts every certificate. Can be
     * overwritten when the policy is specialized. */
    UA_CertificateVerification_AcceptAll(&conf->certificateVerification);

    /* * Global Node Lifecycle * */
    /* conf->nodeLifecycle.constructor = NULL; */
    /* conf->nodeLifecycle.destructor = NULL; */
    /* conf->nodeLifecycle.createOptionalChild = NULL; */
    /* conf->nodeLifecycle.generateChildNodeId = NULL; */
    conf->modellingRulesOnInstances = UA_TRUE;

    /* Limits for SecureChannels */
    conf->maxSecureChannels = 40;
    conf->maxSecurityTokenLifetime = 10 * 60 * 1000; /* 10 minutes */

    /* Limits for Sessions */
    conf->maxSessions = 100;
    conf->maxSessionTimeout = 60.0 * 60.0 * 1000.0; /* 1h */

#ifdef UA_ENABLE_SUBSCRIPTIONS
    /* Limits for Subscriptions */
    conf->publishingIntervalLimits = UA_DURATIONRANGE(100.0, 3600.0 * 1000.0);
    conf->lifeTimeCountLimits = UA_UINT32RANGE(3, 15000);
    conf->keepAliveCountLimits = UA_UINT32RANGE(1, 100);
    conf->maxNotificationsPerPublish = 1000;
    conf->enableRetransmissionQueue = true;
    conf->maxRetransmissionQueueSize = 0; /* unlimited */
# ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
    conf->maxEventsPerNode = 0; /* unlimited */
# endif

    /* Limits for MonitoredItems */
    conf->samplingIntervalLimits = UA_DURATIONRANGE(50.0, 24.0 * 3600.0 * 1000.0);
    conf->queueSizeLimits = UA_UINT32RANGE(1, 100);
#endif

#ifdef UA_ENABLE_DISCOVERY
    conf->discoveryCleanupTimeout = 60 * 60;
#endif

#ifdef UA_ENABLE_HISTORIZING
    /* conf->accessHistoryDataCapability = UA_FALSE; */
    /* conf->maxReturnDataValues = 0; */

    /* conf->accessHistoryEventsCapability = UA_FALSE; */
    /* conf->maxReturnEventValues = 0; */

    /* conf->insertDataCapability = UA_FALSE; */
    /* conf->insertEventCapability = UA_FALSE; */
    /* conf->insertAnnotationsCapability = UA_FALSE; */

    /* conf->replaceDataCapability = UA_FALSE; */
    /* conf->replaceEventCapability = UA_FALSE; */

    /* conf->updateDataCapability = UA_FALSE; */
    /* conf->updateEventCapability = UA_FALSE; */

    /* conf->deleteRawCapability = UA_FALSE; */
    /* conf->deleteEventCapability = UA_FALSE; */
    /* conf->deleteAtTimeDataCapability = UA_FALSE; */
#endif

#if UA_MULTITHREADING >= 100
    conf->maxAsyncOperationQueueSize = 0;
    conf->asyncOperationTimeout = 120000; /* Async Operation Timeout in ms (2 minutes) */
#endif

    /* --> Finish setting the default static config <-- */

    return UA_STATUSCODE_GOOD;
}


#ifdef UA_ENABLE_ENCRYPTION
UA_StatusCode
UA_ClientConfig_setISOBUSEncryption(UA_ClientConfig* config,
    UA_ByteString localCertificate, UA_ByteString privateKey,
    const UA_ByteString* trustList, size_t trustListSize,
    const UA_ByteString* revocationList, size_t revocationListSize) {
    UA_StatusCode retval = UA_ClientConfig_setISOBUS(config);
    if (retval != UA_STATUSCODE_GOOD)
        return retval;

    retval = UA_CertificateVerification_Trustlist(&config->certificateVerification,
        trustList, trustListSize,
        NULL, 0,
        revocationList, revocationListSize);
    if (retval != UA_STATUSCODE_GOOD)
        return retval;

    /* Populate SecurityPolicies */
    UA_SecurityPolicy* sp = (UA_SecurityPolicy*)
        UA_realloc(config->securityPolicies, sizeof(UA_SecurityPolicy) * 5);
    if (!sp)
        return UA_STATUSCODE_BADOUTOFMEMORY;
    config->securityPolicies = sp;

    retval = UA_SecurityPolicy_Basic128Rsa15(&config->securityPolicies[config->securityPoliciesSize],
        localCertificate, privateKey, &config->logger);
    if (retval == UA_STATUSCODE_GOOD) {
        ++config->securityPoliciesSize;
    }
    else {
        UA_LOG_WARNING(&config->logger, UA_LOGCATEGORY_USERLAND,
            "Could not add SecurityPolicy#Basic128Rsa15 with error code %s",
            UA_StatusCode_name(retval));
    }

    retval = UA_SecurityPolicy_Basic256(&config->securityPolicies[config->securityPoliciesSize],
        localCertificate, privateKey, &config->logger);
    if (retval == UA_STATUSCODE_GOOD) {
        ++config->securityPoliciesSize;
    }
    else {
        UA_LOG_WARNING(&config->logger, UA_LOGCATEGORY_USERLAND,
            "Could not add SecurityPolicy#Basic256 with error code %s",
            UA_StatusCode_name(retval));
    }

    retval = UA_SecurityPolicy_Basic256Sha256(&config->securityPolicies[config->securityPoliciesSize],
        localCertificate, privateKey, &config->logger);
    if (retval == UA_STATUSCODE_GOOD) {
        ++config->securityPoliciesSize;
    }
    else {
        UA_LOG_WARNING(&config->logger, UA_LOGCATEGORY_USERLAND,
            "Could not add SecurityPolicy#Basic256Sha256 with error code %s",
            UA_StatusCode_name(retval));
    }

    retval = UA_SecurityPolicy_Aes128Sha256RsaOaep(&config->securityPolicies[config->securityPoliciesSize],
        localCertificate, privateKey, &config->logger);
    if (retval == UA_STATUSCODE_GOOD) {
        ++config->securityPoliciesSize;
    }
    else {
        UA_LOG_WARNING(&config->logger, UA_LOGCATEGORY_USERLAND,
            "Could not add SecurityPolicy#Aes128Sha256RsaOaep with error code %s",
            UA_StatusCode_name(retval));
    }

    if (config->securityPoliciesSize == 0) {
        UA_free(config->securityPolicies);
        config->securityPolicies = NULL;
    }

    return UA_STATUSCODE_GOOD;
}

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
    size_t revocationListSize) {
    // UA_StatusCode retval = setDefaultConfig(conf);
    UA_StatusCode retval = UA_ServerConfig_setISOBUS(conf);
    // UA_ServerConfig_setISOBUS
    if (retval != UA_STATUSCODE_GOOD) {
        UA_ServerConfig_clean(conf);
        return retval;
    }

    retval = UA_CertificateVerification_Trustlist(&conf->certificateVerification,
        trustList, trustListSize,
        issuerList, issuerListSize,
        revocationList, revocationListSize);
    if (retval != UA_STATUSCODE_GOOD)
        return retval;

    /*retval = addISOBUSNetworkLayers(conf, portNumber, 0, 0);
    if (retval != UA_STATUSCODE_GOOD) {
        UA_ServerConfig_clean(conf);
        return retval;
    }*/

    retval = UA_ServerConfig_addAllSecurityPolicies(conf, certificate, privateKey);
    if (retval != UA_STATUSCODE_GOOD) {
        UA_ServerConfig_clean(conf);
        return retval;
    }

    UA_CertificateVerification accessControlVerification;
    retval = UA_CertificateVerification_Trustlist(&accessControlVerification,
        trustList, trustListSize,
        issuerList, issuerListSize,
        revocationList, revocationListSize);
    retval |= UA_AccessControl_default(conf, true, &accessControlVerification,
        &conf->securityPolicies[conf->securityPoliciesSize - 1].policyUri,
        usernamePasswordsSize, usernamePasswords);
    if (retval != UA_STATUSCODE_GOOD) {
        UA_ServerConfig_clean(conf);
        return retval;
    }

    retval = UA_ServerConfig_addAllEndpoints(conf);
    if (retval != UA_STATUSCODE_GOOD) {
        UA_ServerConfig_clean(conf);
        return retval;
    }

    return UA_STATUSCODE_GOOD;
}

#endif