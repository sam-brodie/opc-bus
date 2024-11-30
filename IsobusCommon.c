#include "IsobusCommon.h"

canHandle g_hnd = -1;
int g_mySa = 0; // change depending if i am with client or server
int g_saOfOtherCf = 0; // change depending if i am with client or server
bool g_blocked_by_sender_on_other_side = false;    //set to true when I tried to send TP data but got stuck because other is also sending
const int INIT_HANDSHAKE_ID = 0x1CFEFEDE;

bool g_tc2ecu_using_bf_enc = false;
bool g_ecu2tc_using_bf_enc = false;
BF_KEY bf_key_tc2ecu;
BF_KEY bf_key_ecu2TC;

int setMemTCThings(uint32_t rate, uint16_t element_num, uint16_t DDI)
{
    g_last_rate = rate;
    g_last_element_num = element_num;
    g_last_DDI = DDI;
    g_isValid_last_rates = true;
    return 0;
}

bool readMemTCThings(uint32_t* rate, uint16_t* element_num, uint16_t* DDI)
{
    bool ret_dataIsValid = g_isValid_last_rates;
    *rate = g_last_rate;
    *element_num = g_last_element_num;
    *DDI = g_last_DDI;
    g_isValid_last_rates = false;
    return ret_dataIsValid;
}