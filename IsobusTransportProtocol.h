#pragma once

#include <canlib.h>

#include <mutex>          // std::mutex

HANDLE ghMutex; // https://learn.microsoft.com/en-us/windows/win32/sync/using-mutex-objects



//
// Check a status code and issue an error message if the code
// isn't canOK.
//
void Check(const char* info, canStatus stat) {
    if (stat != canOK) {
        char buf[50];
        buf[0] = '\0';
        canGetErrorText(stat, buf, sizeof(buf));
        //printf("%s: failed, stat=%d (%s)\n", info, (int)stat, buf);
        printf("%s: failed, stat=%d (%s)\n", info, (int)stat, buf);
        OutputDebugStringA(buf);
    }
}


void setupChannel(canHandle* hnd, canStatus* stat, const int channel_number,
    const int flags) {
    // Next, we open up the channel and receive a handle to it. Depending on what
// devices you have connected to your computer, you might want to change the
// channel number. The canOPEN_ACCEPT_VIRTUAL flag means that it is ok to
// open the selected channel, even if it is on a virtual device.
    *hnd = 1;
    OutputDebugStringA("I am sam's debug message/n");
    canHandle x = canOpenChannel(channel_number, flags);
    *hnd = x;
    //*hnd = canOpenChannel(channel_number, flags);

    // If the call to canOpenChannel is successful, it will return an integer
  // which is greater than or equal to zero. However, is something goes wrong,
  // it will return an error status which is a negative number.
    if (*hnd < 0) {
        // To check for errors and print any possible error message, we can use the
        // Check method.
        Check("canOpenChannel", (canStatus)*hnd);
        // and then exit the program.
        exit(1);
    }

    printf("Setting bitrate and going bus on\n");
    // Once we have successfully opened a channel, we need to set its bitrate. We
    // do this using canSetBusParams, which takes the handle and the desired
    // bitrate (another enumerable) as parameters. The rest of the parameters are
    // ignored since we are using a predefined bitrate.
    *stat = canSetBusParams(*hnd, canBITRATE_250K, 0, 0, 0, 0, 0);
    Check("canSetBusParams", *stat);
    // Next, take the channel on bus using the canBusOn method. This needs to be
    // done before we can send a message.
    *stat = canBusOn(*hnd);
    Check("canBusOn", *stat);
}

void sendAndWaitSync(canHandle* hnd, canStatus* stat,
    const unsigned int id, char msg[], const int lenTotal, int waitMs) {
    //printf("\nWriting a message to the channel and waiting for it to be sent \n
    // ");
    // We send the message using canWrite. This method takes five parameters:
    // the channel handle, the message identifier, the message body, the message
    // length (in bytes) and optional flags.

    // loop to send greater than 8 bytes, one after the other
    int remainingLen = lenTotal;
    int pos = 0;
    do {
        if (remainingLen > 8)
            *stat = canWrite(*hnd, id, &msg[pos], 8, canMSG_EXT);
        else
            *stat = canWrite(*hnd, id, &msg[pos], remainingLen, canMSG_EXT);
        Check("canWrite-sendAndWaitSync", *stat);
        remainingLen -= 8;
        pos += 8;
    } while (remainingLen > 8);


    // After sending, we wait for the message to be sent, using
    // canWriteSync.
    if (waitMs != 0)
    {
        if (waitMs > 0)
            *stat = canWriteSync(*hnd, waitMs); //0xFFFFFFFF is ulimited
        else
            *stat = canWriteSync(*hnd, 0xFFFFFFFF); //0xFFFFFFFF is ulimited
        Check("canWriteSync", *stat);
    }
}

void goOffBusAndClose(canHandle* hnd, canStatus* stat) {
    printf("Going off bus and closing channel");
    // Once we are done using the channel, we go off bus using the
    // canBusOff method. It take the handle as the only argument.
    *stat = canBusOff(*hnd);
    Check("canBusOff", *stat);
    // We also close the channel using the canCloseChannel method, which take the
    // handle as the only argument.
    *stat = canClose(*hnd);
    Check("canClose", *stat);

}


#pragma once

#include <canlib.h>

class IsobusTransportProtocol
{
private:
    canStatus stat = canOK;
public:
    canHandle canHnd;
    const static int TP_CM_PGN = 0X00EC00; // connection management pgn
    const static int TP_DT_PGN = 0X00EB00; //data transfer pgn

    IsobusTransportProtocol(canHandle canbusHandle)
    {
        canHnd = canbusHandle;
    }

    // does a transport protocol send. Maybe messes up the queue if used in multithreaded
    // https://www.kvaser.com/canlib-webhelp/page_user_guide_send_recv.html#section_user_guide_send_recv_reading
    // CAN I OPEN TWO CHANNELS INSTEAD????
    int BlockingSendTo(int sa, int da, int pgnOfRequestedInfo, char* tpData, int len)
    {
        int prio = 7;

        //send TP.CM_RTS 16
        int outMsgId_cm = (da << 8) & sa & (TP_CM_PGN << 8) & (prio << 26);
        char outMsgData_cm[8] = { 0 };
        outMsgData_cm[0] = 16;
        outMsgData_cm[1] = len & 0xff;
        outMsgData_cm[2] = len >> 8;  //TODO: needs swapping or not?
        outMsgData_cm[3] = (len + 6) / 7; //https://www.reddit.com/r/C_Programming/comments/gqpuef/how_to_round_up_the_answer_of_an_integer_division/
        outMsgData_cm[4] = 0xFF; //no limit
        outMsgData_cm[5] = pgnOfRequestedInfo & 0xff;
        outMsgData_cm[6] = (pgnOfRequestedInfo >> 8) & 0xff;
        outMsgData_cm[7] = (pgnOfRequestedInfo >> 16) & 0xff;

        stat = canWrite(canHnd, outMsgId_cm, outMsgData_cm, 8, canMSG_EXT);

        //wait for TP.CM 17
        int incomingID_cm = 0; // TODO: IMPORTANT!
        char inMsgData[8] = { 0 };
        unsigned int inLen = 0;
        unsigned int inFlags = 0;
        unsigned long inTime = 0;

        stat = canERR_NOMSG;
        int cnt = 10;
        while (stat != canOK && cnt > 0) {
            stat = canReadSpecific(canHnd, incomingID_cm, inMsgData, &inLen, &inFlags, &inTime);
            if (stat == canOK)
                break;

            Sleep(50);
            cnt--;
        }
        if (stat != canOK) {
            // TODO: error
            return -1;
        }

        // TODO: check the incoming msg (pgn, mesages that can be sent etc)
        if (inLen != 8)
            return -2;

        if (inMsgData[0] != 17)
            return -3;



        //send TP.DT with data repeatedly
        int sequenceNum = 0;
        int outMsgId_dt = (da << 8) & sa & (TP_DT_PGN << 8) & (prio << 26);
        char outMsgData_dt[8] = { 0 };
        for (int i = 0; i < len; i = i + 7) {
            outMsgData_dt[0] = sequenceNum++;
            memcpy(&outMsgData_dt[1], &tpData[i], 7);
            stat = canWrite(canHnd, outMsgId_dt, outMsgData_dt, 8, canMSG_EXT);
        }

        //Wait for TP.CM (if more data needed)
        //stat = canReadSpecific(canHnd, incomingID_cm, inMsgData, &inLen, &inFlags, &inTime);
        ////check data[0] == 17

        // sned more data
        // TODO: NOT RELEVANT FOR THE OPCUA CASE BUT IS NEEDED


        // wait for CM_EOMA
        stat = canReadSpecific(canHnd, incomingID_cm, inMsgData, &inLen, &inFlags, &inTime);
        if (len != 8)
            return -1;

        if (inMsgData[0] != 19)
            return -2;

        return 0; //everything sent correctly (right)

    }

    bool isMsgTpRequestToSend(int id, char* data, int len)
    {
        // TODO: check if msg is TP.CM_RTS
        if (((id & 0x00FFFF00) == 0x00EC2B00) && (data[0] == 16)) // PGN 00EC00 TODO: should bitand with 0x03FFFF00
            return true;
        else
            return false;
    }

    int BlockingTpRecieveFrom(char* data, int maxLen, long* incomingPgn, long* sendersSa, long mySa)
    {
        int prio = 7;
        long sa = mySa;
        int pos = 0;

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

        // recieve RTS or extrended RTS
        do {
            stat = canReadWait(canHnd, &inMsgId_tp, inMsgData_tp, &inLen,
                NULL, NULL, 10); // 100 ms timeout


            if (inLen != 8 || stat != canOK)
                return -1;

            inPgn = (inMsgId_tp >> 8) & 0x03FF00;
            if (inPgn != TP_CM_PGN && inPgn != ETP_CM_PGN)
                return -2;

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

            stat = canWrite(canHnd, outMsgId_Tp, outMsgData_Tp, 8, canMSG_EXT);

            // recieve TP.DT with data repeatedly
            while (remainingPackets > 0)
            {


                stat = canReadWait(canHnd, &inMsgId_tp, inMsgData_tp, &inLen,
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
            stat = canWrite(canHnd, outMsgId_Tp, outMsgData_Tp, 8, canMSG_EXT);
            canWriteSync(canHnd, 0xFFFFFFFF);
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
                canWriteSync(canHnd, 0xFFFFFFFF);
                stat = canWrite(canHnd, outMsgId_Tp, outMsgData_Tp, 8, canMSG_EXT); //begin broadcast
                canWriteSync(canHnd, 0xFFFFFFFF);

                // wait DPO
                do
                {
                    stat = canReadWait(canHnd, &inMsgId_tp, inMsgData_tp, &inLen,
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

                    stat = canReadWait(canHnd, &inMsgId_tp, inMsgData_tp, &inLen,
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
            canWriteSync(canHnd, 0xFFFFFFFF);
            stat = canWrite(canHnd, outMsgId_Tp, outMsgData_Tp, 8, canMSG_EXT); //begin broadcast
            canWriteSync(canHnd, 0xFFFFFFFF);

            if (pos >= incomingBytesLength)
                return incomingBytesLength;
            else
                return -18;

        }
    }

    //set to true when I tried to send TP data but got stuck because other is also sending
    bool g_blocked_by_sender_on_other_side = false;

    //const static int TP_CM_PGN = 0X00EC00; // connection management pgn
    //const static int TP_DT_PGN = 0X00EB00; //data transfer pgn
    const static int ETP_CM_PGN = 0X00C800; // extended connection management pgn
    const static int ETP_DT_PGN = 0X00C700; // extended data transfer pgn
    int BlockingTpSendTo(int sa, char* data, int len, int pgnTosend, long da)
    {
        int prio = 7;

        int pos = 0;

        int numPacketsRequired = (len + 1784) / 1786; //https://www.reddit.com/r/C_Programming/comments/gqpuef/how_to_round_up_the_answer_of_an_integer_division/

        int remainingLenForThisTpGroup = len;
        char outMsgData_Tp[8] = { 0 };
        long outMsgId_Tp;

        long inMsgId_tp = 0;
        char inMsgData_tp[8] = { 0 };
        unsigned int inLen = 0;
        long inPgn = 0;
        int timeoutloops = 9999;

        if (len < 1785)
        {

            outMsgId_Tp = ((long)da << 8) | (long)sa | ((long)TP_CM_PGN << 8) | ((long)prio << 26);

            outMsgData_Tp[0] = 16; //RTS
            outMsgData_Tp[1] = len & 0xff;
            outMsgData_Tp[2] = len >> 8;  //TODO: needs swapping or not?
            if (remainingLenForThisTpGroup > 1785)
                outMsgData_Tp[3] = 255;
            else
                outMsgData_Tp[3] = (remainingLenForThisTpGroup + 6) / 7; //https://www.reddit.com/r/C_Programming/comments/gqpuef/how_to_round_up_the_answer_of_an_integer_division/
            outMsgData_Tp[4] = 0xFF; //no limit - Maximum number of packets that can be sent in response to one CTS.
            outMsgData_Tp[5] = pgnTosend & 0xff;
            outMsgData_Tp[6] = (pgnTosend >> 8) & 0xff;
            outMsgData_Tp[7] = (pgnTosend >> 16) & 0xff;


            // do nortmal TP
            // send CM RTS
            canWriteSync(canHnd, 0xFFFFFFFF);
            stat = canWrite(canHnd, outMsgId_Tp, outMsgData_Tp, 8, canMSG_EXT); //begin
            canWriteSync(canHnd, 0xFFFFFFFF);

            // wait CM CTS
            timeoutloops = 3;
            do
            {
                stat = canReadWait(canHnd, &inMsgId_tp, inMsgData_tp, &inLen,
                    NULL, NULL, 10); // 10 ms timeout
                inPgn = (inMsgId_tp >> 8) & 0x03FF00;
                if (stat == canERR_NOMSG)
                {
                    stat = canWrite(canHnd, outMsgId_Tp, outMsgData_Tp, 8, canMSG_EXT);
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

                stat = canWrite(canHnd, outMsgId_dt, outMsgData_Tp, 8, canMSG_EXT);
                //canWriteSync(canHnd, 0xFFFFFFFF);
                remainingLenForThisTpGroup -= 7;
                pos += 7;
                bytesInOneTp -= 7;
            } while (remainingLenForThisTpGroup > 0 && bytesInOneTp > 0);


            // wait CM EOMA
            timeoutloops = 9999;
            do
            {
                stat = canReadWait(canHnd, &inMsgId_tp, inMsgData_tp, &inLen,
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
            outMsgData_Tp[2] = len >> 8;
            outMsgData_Tp[3] = len >> 16;
            outMsgData_Tp[4] = len >> 24;
            outMsgData_Tp[5] = pgnTosend & 0xff;
            outMsgData_Tp[6] = (pgnTosend >> 8) & 0xff;
            outMsgData_Tp[7] = (pgnTosend >> 16) & 0xff;

            stat = canWrite(canHnd, outMsgId_Tp, outMsgData_Tp, 8, canMSG_EXT);

            // wait CTS
            do
            {

                stat = canReadWait(canHnd, &inMsgId_tp, inMsgData_tp, &inLen,
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
                outDpoData[3] = offset >> 8;
                outDpoData[4] = offset >> 16;
                outDpoData[5] = pgnTosend & 0xff;
                outDpoData[6] = (pgnTosend >> 8) & 0xff;
                outDpoData[7] = (pgnTosend >> 16) & 0xff;
                canWriteSync(canHnd, 0xFFFFFFFF);
                stat = canWrite(canHnd, outMsgId_Tp, outDpoData, 8, canMSG_EXT);
                canWriteSync(canHnd, 0xFFFFFFFF);
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

                    stat = canWrite(canHnd, outMsgId_dt, outMsgData_Tp, 8, canMSG_EXT);
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
                        stat = canReadWait(canHnd, &inMsgId_tp, inMsgData_tp, &inLen,
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
                stat = canReadWait(canHnd, &inMsgId_tp, inMsgData_tp, &inLen,
                    NULL, NULL, 200); // 10 ms timeout
                inPgn = (inMsgId_tp >> 8) & 0x03FF00;
            } while (!(inPgn != ETP_CM_PGN && inMsgData_tp[0] != 23) && --timeoutloops > 0);
            canWriteSync(canHnd, 0xFFFFFFFF);
            if (timeoutloops <= 0)
                return-3;
        }

        return 1;
    }


    int BlockingBamSend(int sa, char* data, int len, int pgnToBroadcast)
    {
        ;
        int da = 255;
        int prio = 7;

        int pos = 0;

        int numBamsRequired = (len + 1784) / 1786; //https://www.reddit.com/r/C_Programming/comments/gqpuef/how_to_round_up_the_answer_of_an_integer_division/

        int remainingLenForThisBamGroup = len;

        canWriteSync(canHnd, 0xFFFFFFFF);


        for (int i = numBamsRequired; i > 0; i--) {
            //send TP.CM_RTS 16
            long outMsgId_bam = ((long)da << 8) | (long)sa | ((long)TP_CM_PGN << 8) | ((long)prio << 26);
            char outMsgData_bam[8] = { 0 };
            outMsgData_bam[0] = 32;
            outMsgData_bam[1] = len & 0xff;
            outMsgData_bam[2] = len >> 8;  //TODO: needs swapping or not?
            if (remainingLenForThisBamGroup > 1785)
                outMsgData_bam[3] = 255;
            else
                outMsgData_bam[3] = (remainingLenForThisBamGroup + 6) / 7; //https://www.reddit.com/r/C_Programming/comments/gqpuef/how_to_round_up_the_answer_of_an_integer_division/
            outMsgData_bam[4] = i;// should be 0xFF but I will use to extend BAM to more than 1785 bytes; //no limit
            outMsgData_bam[5] = pgnToBroadcast & 0xff;
            outMsgData_bam[6] = (pgnToBroadcast >> 8) & 0xff;
            outMsgData_bam[7] = (pgnToBroadcast >> 16) & 0xff;

            stat = canWrite(canHnd, outMsgId_bam, outMsgData_bam, 8, canMSG_EXT); //begin broadcast
            canWriteSync(canHnd, 0xFFFFFFFF);
            // loop to send greater than 8 bytes, one after the other
            long outMsgId_dt = ((long)da << 8) | (long)sa | ((long)TP_DT_PGN << 8) | ((long)prio << 26);



            outMsgData_bam[0] = 0;
            char final_message[8] = { 255  , 255 , 255 , 255 , 255 , 255 , 255, 255 };


            int bytesInOneBam = 1785;

            do
            {
                outMsgData_bam[0]++;

                if (remainingLenForThisBamGroup > 7)
                {
                    memcpy(&outMsgData_bam[1], &data[pos], 7);
                }
                else {
                    memcpy(&outMsgData_bam[1], &final_message[1], 7); //fill with ff
                    memcpy(&outMsgData_bam[1], &data[pos], remainingLenForThisBamGroup); //overwite with whatever data is left
                }

                stat = canWrite(canHnd, outMsgId_dt, outMsgData_bam, 8, canMSG_EXT);
                //canWriteSync(canHnd, 0xFFFFFFFF);
                remainingLenForThisBamGroup -= 7;
                pos += 7;
                bytesInOneBam -= 7;
            } while (remainingLenForThisBamGroup > 0 && bytesInOneBam > 0);

        }

        return 0;
    }

    int BlockingRecieveBam(char* data, int maxLen, long* bamPgn, long* sendersSa)
    {
        static bool extraLargeData = false;
        int pos = 0;
        int new_pos = 0;

        long inMsgId_bam = 0;
        char inMsgData_bam[8] = { 0 };
        unsigned int inLen = 0;
        unsigned int inFlags = 0;
        unsigned long inTime = 0;

        stat = canReadWait(canHnd, &inMsgId_bam, inMsgData_bam, &inLen,
            &inFlags, &inTime, 10); // 100 ms timeout

        if (inLen != 8 || stat != canOK)
            return -1;

        long inPgn = (inMsgId_bam >> 8) & 0x03FF00;
        if (inPgn != TP_CM_PGN)
            return -2;

        if (inMsgData_bam[0] != 32)
            return -3;


        // modified bam, in byte [4] is the number of BAMs to be done
        int numBamsRequired = inMsgData_bam[4];
        unsigned int incomingBytesLength;
        incomingBytesLength = ((unsigned int)inMsgData_bam[2] & 0x00FF) * 256;
        incomingBytesLength += ((unsigned int)inMsgData_bam[1] & 0x00FF);

        long incomingPgn = (long)inMsgData_bam[5] + ((long)inMsgData_bam[6] << 8) + ((long)inMsgData_bam[7] << 16);
        *bamPgn = incomingPgn;

        unsigned int remainingPackets = ((unsigned int)inMsgData_bam[3] & 0x00FF);
        *sendersSa = (inMsgId_bam & 0xFF);

        for (int i = numBamsRequired; i > 0; i--)
        {

            // todo: add error checking
            // todo: what if Bam is abandoned and 
            if (i != numBamsRequired)
            {
                stat = canReadWait(canHnd, &inMsgId_bam, inMsgData_bam, &inLen,
                    &inFlags, &inTime, 200); // 100 ms timeout
                remainingPackets = ((unsigned int)inMsgData_bam[3] & 0x00FF);
            }

            while (remainingPackets > 0)
            {
                if (pos > maxLen)
                    return -6;

                stat = canReadWait(canHnd, &inMsgId_bam, inMsgData_bam, &inLen,
                    &inFlags, &inTime, 1000); // 1000 ms timeout

                if (stat != canOK)
                    return -4;

                // if a new connection management is started then we can't continue
                if (((inMsgId_bam) & 0x03FFFFFF) == ((TP_CM_PGN << 8) | 0xFF00 | *sendersSa))
                {
                    remainingPackets = ((unsigned int)inMsgData_bam[3] & 0x00FF);
                    --i;

                    /*if (inMsgData_bam[4] == 1 && i != 1) {
                        return -5;
                    }
                    else
                    {
                        remainingPackets = ((unsigned int)inMsgData_bam[3] & 0x00FF);
                    }*/

                }
                else if ((inMsgId_bam & 0x03FFFFFF) == ((TP_DT_PGN << 8) | 0xFF00 | *sendersSa))
                {// msg is data
                    --remainingPackets;
                    //sometimes packets are out of order (only on virtual CAn bus maybe???)
                    // solve this by writing to the data array at the position of the data as indicated in the 1st byte of can msg. Also need ot account for large BAM messages over 255 packets
                    new_pos = (7 * ((unsigned char)inMsgData_bam[0] - 1))
                        + ((numBamsRequired - i) * 255 * 7);


                    memcpy(&data[new_pos], &inMsgData_bam[1], 7);
                    pos += 7;
                }
            }

        }


        if (pos >= incomingBytesLength)
            return incomingBytesLength;
        else
            return -8;
    }
};



