// vcpCICmdImpl : Stubs of VCPCI commands implementation.
// The user should implement all the actions marked "TODO"
// with respect to his actual transport lib and VCP profile(s) storage methods

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "bool.h"

#include "vcp-api.h"
#include "vcpCICmdImpl.h"
#include "spf-postapi.h"

extern PROFILE_TYPE(t) *base_profile();
vcp_profile_t * get_base_profile(void){ return base_profile(); }
 
vcpBinProfileT VCPprofiles[MAX_PROFILES];    //This is a dummy storage of VCP profile(s)

/* Length of profile, in 16-bits words */
#define VCPCI_PROFILE_LEN     (sizeof(vcpBinProfileT) / 2)

short DummyDefaultProfile[259] = {
0x0034, 0x001b, 0x0000, 0x00fd, 0x0014, 0x0036, 0x001f, 0x004a,0x0002, 0x0069, 0x0002, 0x006b, 0x0002, 0x006d, 0x0000, 0x0000,
0x0012, 0x006f, 0x000d, 0x0081, 0x0002, 0x008e, 0x0002, 0x0090,0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
0x0000, 0x0000, 0x0011, 0x0092, 0x001f, 0x00a3, 0x0000, 0x0000,0x0000, 0x0000, 0x0006, 0x00c2, 0x0000, 0x0000, 0x0009, 0x00c8,
0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x001c, 0x00d1,0x0010, 0x00ed, 0x0034, 0x3e80, 0x0000, 0x0080, 0x1f40, 0x0800,
0x05aa, 0x0ff6, 0x05aa, 0x0000, 0x01e0, 0x0100, 0x63ff, 0x0100,0x0100, 0x7fff, 0x7fff, 0x1b68, 0x033b, 0x221e, 0x0000, 0x0000,
0x1f40, 0x0dac, 0xfff7, 0xffa0, 0xfffd, 0x0000, 0xfff7, 0xfff4,0x0006, 0x0000, 0x5fff, 0xffa0, 0x3fff, 0x02bc, 0x0064, 0x0bb8,
0x0000, 0x0000, 0xfffa, 0x3fff, 0x0000, 0x0000, 0x3fff, 0xffff,0x0000, 0x01f4, 0x1770, 0x0000, 0x7332, 0x0000, 0x0000, 0x0000,
0x0000, 0x0000, 0x0000, 0x0010, 0x0000, 0x00fa, 0x03c9, 0x03e8,0x04c4, 0x0f23, 0x0721, 0x16f3, 0x078d, 0x2ee0, 0x078d, 0x3c8c,
0x043f, 0x5b4f, 0x0360, 0x7bc7, 0x0288, 0x0000, 0x0080, 0x0000,0x0000, 0x0100, 0x0100, 0x01ff, 0x0148, 0x7fff, 0x00c8, 0x0000,
0x2000, 0x7148, 0x0000, 0x0032, 0x0000, 0x0032, 0x0000, 0x1f40,0x1770, 0x0021, 0x0021, 0x0000, 0x0001, 0x7fff, 0x0000, 0x0000,
0x6666, 0x0001, 0x0064, 0x0001, 0x0064, 0x0001, 0x0000, 0x0000,0x02ee, 0x0021, 0x0021, 0x00a4, 0x02d1, 0x0001, 0x0800, 0x4026,
0x0000, 0x2d6a, 0x2d6a, 0x0000, 0x198a, 0x000a, 0x0000, 0x7fff,0x0000, 0x0000, 0x0000, 0x3fff, 0x0008, 0x0000, 0x7fff, 0x0bb8,
0x0fa0, 0x0bb8, 0x0fa0, 0x0000, 0x0000, 0x0001, 0x0004, 0x0000,0x00fa, 0x0800, 0x7bc7, 0x016c, 0x0000, 0x5a9d, 0x4026, 0x001e,
0x00fa, 0x00c8, 0x7fff, 0x0003, 0x2000, 0x0000, 0x0032, 0x4026,0x7fff, 0x0000, 0x0000, 0x0032, 0x4026, 0x7fff, 0x0000, 0x0000,
0x0032, 0x4026, 0x7fff, 0x0000, 0x0000, 0x0032, 0x4026, 0x7fff,0x0000, 0x7fff, 0x01f4, 0x7fff, 0x02ee, 0x7fff, 0x01f4, 0x7fff,
0x05dc, 0x0101, 0x0501, 0x0601, 0x0701, 0x0000, 0x0000, 0x0000,0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
0x0000, 0x0000, 0x0000,
};

int CurrentProfileNumber;
vcpBinProfileT* pVcp;      //current VCP profile pointer
vcp_profile_t* pProfileStruct = NULL;
mem_reg_t     VcpReg[NUM_MEM_REGIONS];
void *smr[NUM_MEM_REGIONS];

void* memvcp0 = NULL;


/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/
/*        envelope profile processing functions                               */
/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/
err_t vcpObjMemSizeFlat(vcp_profile_t *pVcpProfile,  unsigned int *piObjSize) 
{
    err_t ret = {0, 0, 0};
    unsigned int smem = 16000;
    void *mem;
    int i;

    for (i = 0; i < NUM_MEM_REGIONS; i++)
    {
        if (VcpReg[i].mem != NULL)
            free(VcpReg[i].mem);
    }

    smem = vcp_get_hook_size();
    mem = malloc(smem);

    ret = vcp_get_mem_size(pVcpProfile, VcpReg, mem); // it sets reg.mem = 0

    free(mem);

    if( ret.err )
    {
        *piObjSize = 0;
    }

    return ret;
}

void vcpInitObj(vcp_profile_t *pProfile, mem_reg_t *r) 
{
    vcp_init(pProfile, r);
}

err_t vcpCheckProfile(vcp_profile_t *pProfile)
{
    err_t ret = {0,0,0};
    return ret = vcp_check_profile(pProfile);
}

//binmem - binary profile as obtained from APPLY\STORE
short profilemem[2048];

vcp_profile_t *vcpBin2Profile(short *pBinProfile)
{
    vcp_profile_t *p;
    int datasize;
    read_binary_prof_rt(pBinProfile);
    p = get_base_profile();
    //size just for checking
    datasize = create_binary_profile_rt(p,profilemem);
    compute_crc16(p);   //???
    return p;
}

/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/
/*        here start command processing functions                       */
/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/



/* -------------------------------------------------------------------- */
/*  Function:    GetVCPProfileVerNumber                                 */
/*                                                                      */
/*  Description: The function implements VCPCI_CMD_GET_VERSION          */
/* -------------------------------------------------------------------- */
int GetVCPProfileVerNumber() 
{
    return pVcp->Profile[0];
}

/* -------------------------------------------------------------------- */
/*  Function:    GetVCPProfileAddress                                   */
/*                                                                      */
/*  Description: The function returns storage address of profile        */
/*               number ProfileNum                                      */
/* -------------------------------------------------------------------- */
short* GetVCPProfileAddress(int ProfileNum) 
{
    return (void*)(&VCPprofiles[ProfileNum]);
}


/* -------------------------------------------------------------------- */
/*  Function:    SWReset                                                */
/*                                                                      */
/*  Description: The function implements VCPCI_CMD_RESET                */
/* -------------------------------------------------------------------- */
void SWReset()
{
    //this is a stub!
    printf("Resetting... (press any key to exit)"); 
//    getch();
    //TODO : implement actual pre-reset actions and software reset here 

    exit(0);
}


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
err_t  ApplyStoreProfile(short* ProfileData, int ProfileNum, bool Store) 
{
    err_t vcpError;
    unsigned int Size;
    int i;

    /* check the vadidity of the received profile */
    pProfileStruct = vcpBin2Profile(ProfileData);
    vcpError = vcpCheckProfile (pProfileStruct);
    if (vcpError.err != 0)
        return    vcpError;
    vcpError = vcpObjMemSizeFlat(pProfileStruct, &Size);
    if(vcpError.err != 0)  
        return    vcpError;
    else {
        for (i = 0; i < NUM_MEM_REGIONS; i++)
        {
#if defined(__ARM_NEON__)
            posix_memalign(&VcpReg[i].mem, 64, VcpReg[i].size);
            smr[i] = VcpReg[i].mem;
#else
            VcpReg[i].mem = smr[i] = (void *)malloc(VcpReg[i].size);
#endif
            fprintf(stderr, "I need %d bytes of memory in memory region %d to work.\n", VcpReg[i].size, i + 1);
        }
    }

    //trying to apply profile (TODO: check VCP lib for exact procedure)
    if (! vcpError.err) {
        vcpInitObj(pProfileStruct, VcpReg);

        printf("VCP profile number %d applied successfully", ProfileNum); 
        if(Store){    //if the command was VCPCI_CMD_STORE
            printf(", VCP profile number %d stored successfully", ProfileNum); 
            //TODO: Replace it by an actual procedure of storing the VCP profile in your application 
            memcpy((void*)(&VCPprofiles[ProfileNum]), (void*)ProfileData, 2*(ProfileData[3]+4));
        }
        printf("\n");
    }
    return vcpError;
}

/* -------------------------------------------------------------------- */
/*  Function:    GetVCPProfileNumber                                    */
/*                                                                      */
/*  Description: The function returns sequential number of currently    */
/*     used VCP profile in multi-profile application                    */
/* -------------------------------------------------------------------- */
int GetVCPProfileNumber() { return CurrentProfileNumber; }

/* -------------------------------------------------------------------- */
/*  Function:    InitProfiles                                           */
/*                                                                      */
/*  Description: The function initializes work with multiple profiles   */
/*               and sets current profile to 0                          */
/*(if your application uses only one profile, the # will stay 0 forever)*/
/* -------------------------------------------------------------------- */

void InitProfiles()
{
    err_t ErrorVcp;
    CurrentProfileNumber = 0;
    int i;

    for (i = 0; i < NUM_MEM_REGIONS; i++)
        VcpReg[i].mem == NULL;

    //TODO: the following line is a stub. 
    //Replace it by an actual procedure of obtaining the contents 
    //        of VCP profile #0 in your storage 
    memcpy(&VCPprofiles[0], DummyDefaultProfile, VCPCI_PROFILE_LEN);

    //this is NOT a stub
    pVcp = &VCPprofiles[CurrentProfileNumber];

    //init profile #0 in order to be able to run the processing loop
    ErrorVcp =  ApplyStoreProfile((short *)pVcp, CurrentProfileNumber, false);
    //Error is here just in case. If the lib is OK and versions of lib and profile fit, there shouldn't be error

}


