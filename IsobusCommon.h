#pragma once
#include <open62541/types.h>
#include <canlib.h>
#include <openssl/blowfish.h>

static const int MY_CLIENT_SA = 0xCE;
static const int MY_SERVER_SA = 0x5E;

// CAN handle and source addresses
extern canHandle g_hnd;
extern int g_mySa; // change depending if i am with client or server
extern int g_saOfOtherCf; // change depending if i am with client or server

// hold the last rates (messy solution)
static uint32_t g_last_rate = 0;
static uint16_t g_last_element_num = 0;
static uint16_t g_last_DDI = 0;
static bool g_isValid_last_rates = false;

// CAN ID for starting connection between OPCUA client and server
extern const int INIT_HANDSHAKE_ID;

// ISOBUS Transport Protocol PGNs
const static int TP_CM_PGN = 0X00EC00; // connection management pgn
const static int TP_DT_PGN = 0X00EB00; //data transfer pgn
extern bool g_blocked_by_sender_on_other_side;    //set to true when I tried to send TP data but got stuck because other is also sending
const static int ETP_CM_PGN = 0X00C800; // extended connection management pgn
const static int ETP_DT_PGN = 0X00C700; // extended data transfer pgn
const static uint16_t DDI_SC_CONDENSED_WS_ACT = 161;
const static uint16_t DDI_SC_CONDENSED_WS_SET = 290;

// is encryption key sent

_UA_BEGIN_DECLS
extern BF_KEY bf_key_tc2ecu;
extern BF_KEY bf_key_ecu2TC;
extern bool g_tc2ecu_using_bf_enc;
extern bool g_ecu2tc_using_bf_enc;
int setMemTCThings(uint32_t rate, uint16_t element_num, uint16_t DDI);

bool readMemTCThings(uint32_t* rate, uint16_t* element_num, uint16_t* DDI);


_UA_END_DECLS