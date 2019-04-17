
#ifndef VCPCIIMPL_H
#define VCPCIIMPL_H

/* ==================================================================== */
/*  File:       vcpCICmdImpl.h                                          */
/*  Purpose:    Stubs of VCPCI commands implementation.                 */
/* The user should implement all the actions marked "TODO"              */
/* with respect to his actual VCP lib and storage methods               */
/* ==================================================================== */

#include "vcp-api.h"
#include "bool.h"

//***********************************************
#ifdef __cplusplus
extern "C" {
#endif

#define MAX_PROFILES    6    //replace this by actual number of profiles in your application

typedef struct vcp_linear_bin_profile {
    short Profile[2048];   //profile size is actually variable; usually much less than 2k, but theoretically might be even more
                            //this size=2k taken here for example; malloc is preferable if your architecture allows it
} vcpBinProfileT;
/* -------------------------------------------------------------------- */
/*  Function:    GetVCPProfileVerNumber                                 */
/*                                                                      */
/*  Description: The function implements VCPCI_CMD_GET_VERSION          */
/*  Returns: VCP profile version number                                 */
/* -------------------------------------------------------------------- */
int GetVCPProfileVerNumber() ;

/* -------------------------------------------------------------------- */
/*  Function:    SetVCPProfileNumber                                    */
/*                                                                      */
/*  Description: Sets CurrentProfileNumber in multi-profile application */
/* -------------------------------------------------------------------- */
// int SetVCPProfileNumber() ;    to implement later

/* -------------------------------------------------------------------- */
/*  Function:    GetVCPProfileNumber                                    */
/*                                                                      */
/*  Description: The function returns sequential number of currently    */
/*     used VCP profile in multi-profile application                    */
/* -------------------------------------------------------------------- */
int GetVCPProfileNumber() ;

/* -------------------------------------------------------------------- */
/*  Function:    GetVCPProfileAddress                                   */
/*                                                                      */
/*  Description: The function returns storage address of profile        */
/*               number ProfileNum                                      */
/* -------------------------------------------------------------------- */
short* GetVCPProfileAddress(int ProfileNum);

/* -------------------------------------------------------------------- */
/*  Function:    SWReset                                                */
/*                                                                      */
/*  Description: The function implements VCPCI_CMD_RESET                */
/* -------------------------------------------------------------------- */
void SWReset();


/* -------------------------------------------------------------------- */
/*  Function:    ApplyStoreProfile                                      */
/*                                                                      */
/*  Description:  implements VCPCI_CMD_APPLY, VCPCI_CMD_STORE           */
/*     (both commands apply the received profile; in addition           */ 
/*      VCPCI_CMD_STORE stores the profile in non-volatile memory)      */
/*  Parameters:                                                         */
/*  short* ProfileData - pointer to profile data                        */
/*       (obtained from the VCPCI_CMD_APPLY or VCPCI_CMD_STORE command) */
/*      int ProfileNum - number of profile to store                     */
/*    (if your application doesn't store multiple profiles, must be 0)  */
/*      bool Store     -    true if processing  VCPCI_CMD_STORE,        */
/*                          false if processing  VCPCI_CMD_APPLY        */
/*  Returns:    profile error structure (result of profile check and    */
/*                    initialization)                                   */
/* -------------------------------------------------------------------- */
err_t  ApplyStoreProfile(short* ProfileData, int ProfileNum, bool Store) ;


/* -------------------------------------------------------------------- */
/*  Function:    InitProfiles                                           */
/*                                                                      */
/*  Description: The function initializes work with multiple profiles   */
/*               and sets current profile to 0                          */
/*(if your application uses only one profile, the # will stay 0 forever)*/
/* -------------------------------------------------------------------- */
void InitProfiles();


void VcpProcessOneFrame();


void InitLogger();
void StopLogger(); 
void SendLoggerData(char* b, int lenWords) ;

extern vcp_profile_t* pProfileStruct;
extern mem_reg_t      VcpReg[NUM_MEM_REGIONS];

#ifdef __cplusplus
}
#endif

#endif /* VCPCIIMPL_H */

