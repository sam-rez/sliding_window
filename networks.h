//
//  networks.h
//  program3
//
//  Created by Sam on 2/12/16.
//  Copyright © 2016 Sam. All rights reserved.
//

#ifndef networks_h
#define networks_h

#define MAX_LEN 1500
#define START_SEQ_NUM 1

enum FLAG
{
    DATA, NOT_USED, ACK, SREJ, NOT_USED2, FNAME, FNAME_OK, FNAME_BAD, END_OF_FILE, CRC_ERROR = -1
};



enum SELECT{
    SET_NULL, NOT_NULL
};


typedef struct connection Connection;


struct connection{
    int32_t sk_num;
    struct sockaddr_in remote;
    uint32_t len;
};


int32_t udp_server(int optional_port_number);
int32_t udp_client_setup(char * hostname, uint16_t port_num, Connection * connection);
int32_t select_call(int32_t socket_num, int32_t seconds, int32_t microseconds, int32_t set_null);
int32_t send_buf(uint8_t * buf, uint32_t len, Connection * connection,
                 uint8_t flag, uint32_t seq_num, uint8_t * packet);
int32_t recv_buf(uint8_t * buf, uint32_t len, uint32_t recv_sk_num,Connection * connection,
                  uint8_t *flag, int32_t *seq_num);


#endif /* networks_h */
