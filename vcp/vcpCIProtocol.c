// simul_main.cpp : Defines the entry point for the console application.
//

#include <string.h>
#include "bool.h"
#include "vcpCIProtocol.h"
#include "vcpCICmdImpl.h"
//#include "demo.h"

extern vcpBinProfileT VCPprofiles[MAX_PROFILES];

/* -------------------------------------------------------------------- */
/*  Function:    VCPCI_GetMessageBuildReply                             */
/*                                                                      */
/*  Description: The function gets a VCPCI message from configurator    */
/*              (as a pointer to byte array), processes it and creates  */
/*              an appropriate reply (to be sent to the VCP configurator*/
/*              via COM port)                                           */
/*  Parameters:                                                         */
/*      short* InMessage - pointer to incoming message buffer           */
/*      int InLenW       - number of received 16-bit words              */
/*     short* OutMessage - pointer to buffer to store the reply         */
/*    bool VerifyProfNum - true    if your application requires         */
/*                      profile number in Apply or Store command to be  */
/*                      the same as current profile number              */
/*                          false    if don't care                      */
/*  Returns:    reply length in 16-bit words if OK  (>=0)               */    
/*  VCPCI_UNKNOWN_OPCODE (-1) if message opcode (word 1) is unknown     */
/*  VCPCI_WRONG_LENGTH   (-2) if message length (word 0) doesn't fit the*/
/*                message opcode or differs from InLenW                 */
/*  VCPCI_WRONG_PROFILE_NUM(-3) if VerifyProfNum = true and profile num */
/*                                doesn't fit                           */
/* -------------------------------------------------------------------- */
int VCPCI_GetMessageBuildReply(short* InMessage, int InLenW, short* OutMessage, bool VerifyProfNum) 
{
    VcpCI_Command_t* pCommand;
    err_t vcpError;
    short Opcode, Len, ProfileNo;
    if(InLenW < 1) {
        OutMessage[0] = 0; 
        return VCPCI_WRONG_LENGTH;
    }
    if(InLenW == 1) {   //this is ping!!!
        OutMessage[0] = InMessage[0]; // pong
        return 1;
    }
    pCommand = (VcpCI_Command_t*) InMessage;
    Len = pCommand->Length;
    Opcode = pCommand->OpCode;
    switch(Opcode)
    {    
        /* --- Pre-defined opcodes realization --- */
        case VCPCI_CMD_GET_VERSION:
            /* Return VCP version */
            OutMessage[0] = 2;    //length
            OutMessage[1] = GetVCPProfileVerNumber();
            return 2;

        case VCPCI_CMD_APPLY:
        case VCPCI_CMD_STORE:
            //length < 4: cannot check profile number
            if (InLenW < (int)sizeof(VcpCI_Command_t) / 2) {
                OutMessage[0] = 0; 
                return VCPCI_WRONG_LENGTH;
            }
            ProfileNo = pCommand->ProfileNum;    

            //check profile number if required by your application
            
            if (VerifyProfNum && (ProfileNo != GetVCPProfileNumber())) 
            {
                OutMessage[0]=0; 
                return VCPCI_WRONG_PROFILE_NUM;
            }

            vcpError = ApplyStoreProfile(&(pCommand->Data[0]), GetVCPProfileNumber(), true);//(Opcode == VCPCI_CMD_STORE));

            OutMessage[0] = 4; //3;    //length
            OutMessage[1] = vcpError.err;        
            OutMessage[2] = vcpError.pid;        
            OutMessage[3] = vcpError.memb;
#if defined(SAVE_VCP_INPUT) || defined(SAVE_VCP_OUTPUT)
            vcp_sample_no = 0;
#endif
            return OutMessage[0];
        case VCPCI_CMD_READ:
            OutMessage[0] = 2 + sizeof(vcpBinProfileT) / 2;    //length
            OutMessage[1] = 0;        
            memcpy(&OutMessage[2], GetVCPProfileAddress(0), sizeof(vcpBinProfileT));
            if (OutMessage[2] >= PROFILE_VERSION) 
                OutMessage[0] = 2 + OutMessage[2 + 3] + 4;    //length is 2(opcode+pr#)+profile hdr len (=4) + profile data len (last word of hdr)
            return OutMessage[0];
        case VCPCI_CMD_RESET:
            /* System reboot - the sender shouldn't expect any reply :-)*/
            SWReset();
    }
    return VCPCI_UNKNOWN_OPCODE;
}
