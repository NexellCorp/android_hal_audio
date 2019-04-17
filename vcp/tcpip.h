#ifndef __TCPIP_H__
#define __TCPIP_H__

/* ==================================================================== */
/*  File:       tcpip.h                                                 */
/*  Purpose:    Implements TCP/IP server for one client                 */
/* ==================================================================== */

#include "vcp_clients.h"

/* TCP/IP server parameters */
typedef struct {
    unsigned short listenPort;  // desired server port
} tcpip_server_params_t;

vcp_server_t* tcpip_server_create(void);
void tcpip_server_destroy(vcp_server_t *server);

#endif // __TCPIP_H__
