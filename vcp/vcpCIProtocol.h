
#ifndef VCPCIPROT_H
#define VCPCIPROT_H

/* ================================================================================ */
/*  File:       vcpCIProtocol.h                                                     */
/*  Purpose:    Implements Alango VCP configurator <-> VCP processor interface      */
/*              via COM port                                                        */
/*  Description: This file contains VCP-CI protocol API definitions and description.*/
/*      Storage-related and transport-related APIs are stubs, for simulation and    */
/*      illustration purpose only.                                                  */
/*      VCP-processing related APIs are partly stubs, must be fixed according to    */
/*      the user's version of VCP lib                                               */
/* ================================================================================ */

#ifdef __cplusplus
extern "C" {
#endif

/* OpCodes of predefined commands */
#define VCPCI_CMD_GET_VERSION  0
#define VCPCI_CMD_APPLY        1
#define VCPCI_CMD_STORE        2
#define VCPCI_CMD_RESET        3
#define VCPCI_CMD_READ         4  

/* Length of profile, in 16-bits words */
#define VCPCI_PROFILE_LEN     (sizeof(vcpTProfile) / 2)

/* VCP CI packet formats */
typedef struct {
    short Length;
    short OpCode;
    short ProfileNum;
    short Data[0];
} VcpCI_Command_t;

    /* ---- VCPCI messaging error codes ---- */
#define VCPCI_OK                    0        /* no error */
#define VCPCI_UNKNOWN_OPCODE       -1        /* Cannot process message - unknown opcode */
#define VCPCI_WRONG_LENGTH         -2        /* Actual message length differs from message 1st word */
#define VCPCI_WRONG_PROFILE_NUM    -3        /* Profile with this number cannot be applied|stored */

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
int VCPCI_GetMessageBuildReply(short* InMessage, int InLenW, short* OutMessage, bool VerifyProfNum);
#ifdef __cplusplus
}
#endif

#endif /* VCPCIPROT_H */

