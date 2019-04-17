#ifndef __TCPIP_CLIENTS_H__
#define __TCPIP_CLIENTS_H__

/* ==================================================================== */
/*  File:       vcp_clients.h                                           */
/*  Purpose:    Top level API for integration of interaction with       */
/*      external programs (clients) over TCP/IP and UART                */
/* ==================================================================== */

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_NUM_VCP_CLIENTS 2

/* VCP client types */
typedef enum {
    VCP_CLIENT_TYPE_CONFIGURATOR,
    VCP_CLIENT_TYPE_LOGGER
} vcp_client_type_t;

/* VCP transport types */
typedef enum {
    VCP_TRANSPORT_TYPE_TCPIP,
    VCP_TRANSPORT_TYPE_UART
} vcp_transport_type_t;

typedef struct vcp_server vcp_server_t;

/* Transport server interface */
typedef struct vcp_server {
    int (*open)(vcp_server_t *server, char *client_name, void *transport_params);
    int (*close)(vcp_server_t *server);
    int (*write)(vcp_server_t *server, void *buf, int len);
    int (*read)(vcp_server_t *server, void *buf, int max_len);
    void *context;
} vcp_server_t;

/* VCP client structure */
typedef struct vcp_client {
    vcp_client_type_t client_type;
    vcp_transport_type_t transport_type;    
    vcp_server_t *server;
} vcp_client_t;

/* VCP clients structure */
typedef struct {
    vcp_client_t *clients[MAX_NUM_VCP_CLIENTS]; // pointers to clients
    int num_clients;            // number of clients 
} vcp_clients_t;


/* -------------------------------------------------------------------- */
/*  Function:    vcp_clients_add                                        */
/*                                                                      */
/*  Description: This function adds and initializes the new VCP client  */
/*    Only two clients can be added by that function, one of which      */
/*    should be a logger and another should be a configurator.          */
/*    At attempt to add more then 2 clients or if both clients          */
/*    will be the same type the function will do nothing and            */  
/*    return an error.                                                  */
/*  Parameters:                                                         */
/*      client_type - client type                                       */
/*      transport_type - transport type                                 */
/*      transport_params - transport parameters                         */
/*        (for transport_type VCP_TRANSPORT_TYPE_TCPIP it is ignored)   */
/*  Returns: 0 - success, -1 - failure                                  */
/* -------------------------------------------------------------------- */
extern int vcp_clients_add(vcp_client_type_t client_type, 
    vcp_transport_type_t transport_type, void *transport_params);

/* -------------------------------------------------------------------- */
/*  Function:    vcp_clients_remove_all                                 */
/*                                                                      */
/*  Description: This function deinitializes all VCP clients and        */
/*       frees allocated resources                                      */
/*  Returns: 0 - success, -1 - failure                                  */
/* -------------------------------------------------------------------- */
extern int vcp_clients_remove_all();

/* -------------------------------------------------------------------- */
/*  Function:    vcp_clients_preproc                                    */
/*                                                                      */
/*  Description: This function reads the data from clients and          */
/*      perfoms required actions if necessary. It can be called before  */
/*      or after call of VCP processing function (vcp_process_debug for */
/*      example).                                                       */
/*  Returns: 0 - success, -1 - failure                                  */
/* -------------------------------------------------------------------- */
extern int vcp_clients_preproc();

/* -------------------------------------------------------------------- */
/*  Function:    vcp_clients_postproc                                   */
/*                                                                      */
/*  Description: This function sends the data obtained during VCP       */
/*      processing to clients. It must be called after call of VCP      */  
/*      processing function.                                            */
/*  Returns: 0 - success, -1 - failure                                  */
/* -------------------------------------------------------------------- */
extern int vcp_clients_postproc();

#ifdef __cplusplus
};
#endif

#endif // __TCPIP_CLIENTS_H__
