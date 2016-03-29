//
//  server.c
//
//  Created by Sam on 2/16.
//  Copyright © 2016 Sam. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "networks.h"
#include "cpe464.h"

/*
    FLAGS: 1 = DATA, 3 = RR, 4 = SREJ, 6 = remote_fname
 */

typedef enum State STATE;

enum State{
    START, DONE, FILENAME, SEND_DATA, WINDOW_CLOSED
};



void process_client(int32_t server_sk_num, uint8_t * buf, int32_t recv_len, Connection * client);
STATE filename(Connection * client, uint8_t * buf, int32_t recv_len,
               int32_t * data_file, int32_t * buf_size, int32_t * window_size);
STATE send_data(Connection * client, uint8_t * packet, int32_t * packet_len, int32_t data_file, 
                int buf_size, int32_t * seq_num, uint8_t ** sliding_window, int32_t * bottom_of_window,
                int32_t * edge_of_window, int32_t window_size);
STATE window_closed();


int main(int argc, const char * argv[]) {

    int32_t server_sk_num = 0;
    pid_t pid = 0;
    int status = 0;
    uint8_t buf[MAX_LEN];
    Connection client;
    uint8_t flag = 0;
    int32_t seq_num = 0;
    int32_t recv_len = 0;
    int32_t optional_port_number = 0;

    printf("argc is: %d\n", argc);

    if(argc != 2 && argc != 3){
        printf("Usage: %s error-percent optional-port-number\n", argv[0]);
        exit(-1);
    }

    sendtoErr_init(atof(argv[1]), DROP_ON, FLIP_ON, DEBUG_ON, RSEED_ON);

    /* check if optional port number provided */
    if(argc == 3){
        optional_port_number = atoi(argv[2]);
    }

    server_sk_num = udp_server(optional_port_number);

    while(1){
        if(select_call(server_sk_num, 1, 0, NOT_NULL) == 1){
            recv_len = recv_buf(buf, 1000, server_sk_num, &client, &flag, &seq_num);
            if(recv_len != CRC_ERROR){
                /* fork will go here */
                if((pid = fork()) < 0){
                    perror("fork");
                    exit(-1);
                }
                //process child
                if(pid == 0){
                    process_client(server_sk_num, buf, recv_len, &client); //buf is filename
                    exit(0);
                }
            }
            //check to see if any children quit
            while(waitpid(-1, &status, WNOHANG) > 0){
                printf("processed wait\n");
            }
        }//end if selectcall
    }//end while

    return 0;
}


void process_client(int32_t server_sk_num, uint8_t * buf, int32_t recv_len, Connection * client){

    STATE state = START;
    int32_t data_file = 0;
    int32_t packet_len = 0;
    uint8_t packet[MAX_LEN];
    int32_t buf_size = 0;
    int32_t seq_num = START_SEQ_NUM;
    int32_t window_size = 0;
    int32_t i = 0;
    int32_t bottom_of_window = 1;
    int32_t edge_of_window = 1;

    while(state != DONE){
        switch (state) {
            case START:
                state = FILENAME;
                break;

            case FILENAME:
                seq_num = 1;
                state = filename(client, buf, recv_len, &data_file, &buf_size, &window_size);
                //create sliding window array
                uint8_t **sliding_window = (uint8_t **)malloc(window_size * sizeof(char*));
                for(i = 0; i < window_size; i++){
                    sliding_window[i] = (uint8_t *)malloc(buf_size * sizeof(char));
                }
                break;

            case SEND_DATA:
                //check if window is open
                if(edge_of_window - bottom_of_window == window_size){
                    state = WINDOW_CLOSED;
                    break;
                }
                state = send_data(client, packet, &packet_len, data_file, 
                                    buf_size, &seq_num, sliding_window, &bottom_of_window, 
                                    &edge_of_window, window_size);
                break;

            case WINDOW_CLOSED:
                //wait for RR
                state = window_closed();
                break;

            case DONE:
                //free sliding window
                break;

            default:
                printf("In default and you should not be here!!!\n");
                state = DONE;
                break;
        }//end switch
    }//end while
}


STATE filename(Connection * client, uint8_t * buf, int32_t recv_len,
                    int32_t * data_file, int32_t * buf_size, int32_t * window_size){

    uint8_t response[1];
    char fname[MAX_LEN];

    printf("in filename\n");
    memcpy(buf_size, buf, 4);
    memcpy(fname, &buf[4], recv_len - 8);
    memcpy(window_size, &buf[recv_len - 4], 4);

    /* create client socket to allow for processing this particular client */
    if((client->sk_num = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
        perror("filename, open client socket");
        exit(-1);
    }

    /* open file (provided by client) for reading */
    if(((*data_file) = open(fname, O_RDONLY)) < 0){
        send_buf(response, 0, client, FNAME_BAD, 0, buf);
        return DONE;
    }else{
        send_buf(response, 0, client, FNAME_OK, 0, buf);
        return SEND_DATA;
    }
}



STATE send_data(Connection * client, uint8_t * packet, int32_t * packet_len, int32_t data_file, 
                int buf_size, int32_t * seq_num, uint8_t ** sliding_window, int32_t * bottom_of_window,
                int32_t * edge_of_window, int32_t window_size){

    uint8_t buf[MAX_LEN];
    int32_t len_read = 0;
    uint8_t data_buf[MAX_LEN];
    int32_t data_len;
    uint8_t flag;

    len_read = (int)read(data_file, buf, buf_size);

    switch (len_read) {
        case -1:
            perror("send_data, read error");
            return DONE;
            break;

        case 0:
            (*packet_len) = send_buf(buf, 1, client, END_OF_FILE, *seq_num, packet);
            (*edge_of_window)++;
            //wait for final RR
            printf("File Transfer Complete.\n");
            return DONE;
            break;

        default:
            (*packet_len) = send_buf(buf, len_read, client, DATA, *seq_num, packet);
            (*seq_num)++;
            (*edge_of_window)++;
            //add to buffer, increase window_size
                /*
                for writing to sliding_window:
                    char *s = "hello world\n";
                    memcpy(*&sliding_window[1], s, buf_size);
                */
            memcpy(*&sliding_window[(*seq_num) % window_size], buf, buf_size);
            // printf("printing sliding window 0\n");
            // printf("%s\n", sliding_window[0]);
            // printf("printing sliding window 1\n");
            // printf("%s\n", sliding_window[1]);
            // printf("printing sliding window 2\n");
            // printf("%s\n", sliding_window[2]);
            // printf("printing sliding window 3\n");
            // printf("%s\n", sliding_window[3]);
            // printf("printing sliding window 4\n");
            // printf("%s\n", sliding_window[4]);

            //non blocking select for RRs or SREJ
            if(select_call(client->sk_num, 0, 0, NOT_NULL) == 1){
                    data_len = recv_buf(data_buf, 1400, client->sk_num, client, &flag, seq_num);
                if(flag == 3){
                    (*bottom_of_window) = (*seq_num); 
                }else if(flag == 4){
                    //send packet of SREJ
                    //send seq_num % window_size
                    send_buf(sliding_window[(*seq_num) % window_size], buf_size, client, DATA, *seq_num, packet);
                }
            }

            return SEND_DATA;
            break;
    }
}


STATE window_closed(){


    //select for one second waiting for RRS

        //if RRx received, update bottom of window to x
            //if RR is EOF
                //return DONE
            //else
                //return SEND_DATA
        //if SREJx received, send packet x and stay closed
            //continue
    //if no RRs received send packet of bottom of window
            //count++
            //continue
    return SEND_DATA;

}
