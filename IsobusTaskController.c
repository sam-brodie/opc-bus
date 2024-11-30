#include "IsobusCommon.h"
#include "IsobusTaskController.h"
////#include "IsobusOpcUaLayer.h"
#include <canlib.h>
#include <openssl/blowfish.h>
#include <stdint.h>
#include "IsobusOpcUaLayer.h"


int tc_rekey(unsigned char* bf_key_val, int bf_key_len_bytes, bool isTc2EcuNotEcu2Tc)
{
    if (bf_key_len_bytes != 16)
        return -1;

    if(isTc2EcuNotEcu2Tc)
        BF_set_key(&bf_key_tc2ecu, bf_key_len_bytes, bf_key_val);
    else
        BF_set_key(&bf_key_ecu2TC, bf_key_len_bytes, bf_key_val);

    return 0;
}


canStatus sendRateWithEncryption(uint32_t rate, uint16_t element_num, uint16_t DDI)
{
    uint8_t destAddr = g_saOfOtherCf;
    uint8_t srcAddr = g_mySa;


    //BF_KEY* myWritingKey = (g_mySa == MY_SERVER_SA) ? &bf_key_ecu2TC : &bf_key_tc2ecu;
    BF_KEY* myWritingKey;
    if (g_mySa == MY_SERVER_SA)
        myWritingKey = &bf_key_ecu2TC;
    else
        myWritingKey = &bf_key_tc2ecu;

    const uint8_t command = 0x03; //value command (Table B.1)
    element_num = element_num << 4; //element num is now in most sig 12 bits, bits 0-3 are dont care
    uint8_t* rate_bytes = (uint8_t*)&rate;
    uint8_t frameData_plaintext[8];

    frameData_plaintext[0] = (element_num % 0x00F0) + command; //TODO: put rate etc to correct bytes
    frameData_plaintext[1] = element_num >> 8;
    frameData_plaintext[2] = (DDI & 0x00FF);
    frameData_plaintext[3] = (DDI & 0xFF00) >> 8;
    frameData_plaintext[4] = rate_bytes[0];
    frameData_plaintext[5] = rate_bytes[1];
    frameData_plaintext[6] = rate_bytes[2];
    frameData_plaintext[7] = rate_bytes[3];

    unsigned char data_toSend_encrypted[8];
    BF_ecb_encrypt(frameData_plaintext, data_toSend_encrypted, myWritingKey, BF_ENCRYPT);


    long frameId = 0x0CCB0000 + (destAddr << 8) + srcAddr;
    const int waitMs = 50;
    canStatus stat = sendAndWaitSyncForce8Bytes(frameId, data_toSend_encrypted, waitMs);

    return stat;
}





canStatus sendAndWaitSyncForce8Bytes(const unsigned int id, unsigned char* msg, int waitMs)
{
    if (waitMs == 0)
        waitMs = 0xFFFFFFFF;

    canStatus write_stat = canWrite(g_hnd, id, msg, 8, canMSG_EXT);
    //if (stat != canOK) 
    //    return stat;

    canWriteSync(g_hnd, waitMs); //0xFFFFFFFF is ulimited
    return write_stat;
}