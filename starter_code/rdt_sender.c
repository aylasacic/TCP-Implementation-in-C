#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>
#include <errno.h>

#include "packet.h"
#include "common.h"

#define STDIN_FD    0
#define RETRY  120 // milliseconds
#define WINDOW_SIZE 10
#define END_ACK 1
#define MAX_EOF_RETRIES 20

int next_seqno = 0;
int send_base = 0;
int dup_ack_count = 0;
int last_ack = -1;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer; 
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;  
tcp_packet *window[WINDOW_SIZE];

void start_timer()
{
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}

void resend_packets(int sig) {
    VLOG(INFO, "Timeout happened, resending packets");
    if (sig == SIGALRM) {
        for (int i = 0; i < WINDOW_SIZE; i++) {
            //if (window[i] != NULL && window[i]->hdr.seqno >= send_base) {
                if (sendto(sockfd, window[i], TCP_HDR_SIZE + get_data_size(window[i]), 0,
                           (const struct sockaddr *)&serveraddr, serverlen) < 0) {
                    error("sendto");
                }
            //} else {break;}
        }
        start_timer();
    }
}


void stop_timer()
{
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}

/*
 * init_timer: Initialize timer
 * delay: delay in milliseconds
 * sig_handler: signal handler function for re-sending unACKed packets
 */
void init_timer(int delay, void (*sig_handler)(int)) 
{
    signal(SIGALRM, resend_packets);
    timer.it_interval.tv_sec = delay / 1000;    // sets an interval of the timer
    timer.it_interval.tv_usec = (delay % 1000) * 1000;  
    timer.it_value.tv_sec = delay / 1000;       // sets an initial value
    timer.it_value.tv_usec = (delay % 1000) * 1000;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
}

int main (int argc, char **argv)
{
    int portno, len;
    char *hostname;
    char buffer[DATA_SIZE];
    FILE *fp;

    /* check command line arguments */
    if (argc != 4) {
        fprintf(stderr, "usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    fp = fopen(argv[3], "r");
    if (fp == NULL) {
        error(argv[3]);
    }

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    /* initialize server server details */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serverlen = sizeof(serveraddr);

    /* covert host into network byte order */
    if (inet_aton(hostname, &serveraddr.sin_addr) == 0) {
        fprintf(stderr, "ERROR, invalid host %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);

    // Stop and wait protocol

    init_timer(RETRY, resend_packets);
    next_seqno = 0;
    while (1) {
    int can_send_more = 1;  /* flag to control the sending of packets */
        while (can_send_more) {
            if (next_seqno >= send_base + WINDOW_SIZE * DATA_SIZE) {
                can_send_more = 0;  /* stop sending if the window is full */
                break;
            }
            len = fread(buffer, 1, DATA_SIZE, fp);
            if (len <= 0) {
                can_send_more = 0;  /* stop sending if no more data to read */
                break;
            }
        sndpkt = make_packet(len); // Create packet with data
        memcpy(sndpkt->data, buffer, len); // Copy data into packet
        sndpkt->hdr.seqno = next_seqno; // Set sequence number
	
	/* clear buffer space from previous transmission */
	if(window[(next_seqno / DATA_SIZE) % WINDOW_SIZE] != NULL){
                free(window[(next_seqno / DATA_SIZE) % WINDOW_SIZE]);
            }

        VLOG(DEBUG, "SENDING PACKET: %d", next_seqno);

        if (sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, 
                   (const struct sockaddr *)&serveraddr, serverlen) < 0) {
            error("sendto");
        }
	
	/* set next packet in window */
        window[(next_seqno / DATA_SIZE) % WINDOW_SIZE] = sndpkt;
	/* set next sequence number to sequence number plus the length of read DATA_SIZE*/
        next_seqno += len; // Update next sequence number
	
	/* 
	   start the timer if this is the first packet in the current window.
	   this condition checks if the send_base (smallest unacknowledged sequence number)
	   is equal to the sequence number of the just sent packet (next_seqno - len).
	   if true, it means the window was empty before this packet, so we need to start
	   the timer to handle potential retransmissions in case the packet is lost.
	 */
        if (send_base == next_seqno - len) {
            start_timer();
        }
    }

    if (recvfrom(sockfd, buffer, MSS_SIZE, 0, 
                 (struct sockaddr *)&serveraddr, (socklen_t *)&serverlen) < 0) {
        error("recvfrom");
    }

    recvpkt = (tcp_packet *)buffer;
    assert(get_data_size(recvpkt) <= DATA_SIZE);

    /* 
	if received packed ACK number is equal to the first packet in window
	add a duplicate ack count and retransmit the packet after 3 duplicate acks

    */
    if (recvpkt->hdr.ackno == send_base) {
        VLOG(INFO, "Duplicate ACK received for %d", recvpkt->hdr.ackno);
        dup_ack_count++;
        if (dup_ack_count == 3) {
            VLOG(INFO, "3 duplicate ACKs received, retransmitting packet %d", send_base);
            if (sendto(sockfd, window[(send_base / DATA_SIZE) % WINDOW_SIZE], 
                       TCP_HDR_SIZE + get_data_size(window[(send_base / DATA_SIZE) % WINDOW_SIZE]), 0,
                       (const struct sockaddr *)&serveraddr, serverlen) < 0) {
                error("sendto");
            }
            dup_ack_count = 0; /*reset the counter after retransmission*/
            start_timer(); /*restart the timer after retransmission*/
        }
    } else if (recvpkt->hdr.ackno > send_base) {
        send_base = recvpkt->hdr.ackno;
        last_ack = recvpkt->hdr.ackno; /*update the last_ack to the current ACK number*/
        dup_ack_count = 0; /*reset the counter on new ACK*/

	/*
		if all sent packets are acknowledged (send_base equals next_seqno),
		stop the timer as there are no unacknowledged packets remaining.
		otherwise, restart the timer to continue monitoring for acks of
		the remaining unacknowledged packets.
	*/
        if (send_base == next_seqno) {
            stop_timer();
        } else {
            start_timer();
        }
    }

    /* 
	if the end of file is reached and all sent data is acknowledged,
	initiate the end-of-file (EOF) sequence.
   */
    if (feof(fp) && send_base == next_seqno) {
    VLOG(INFO, "EOF reached");
    sndpkt = make_packet(0);
    sndpkt->hdr.seqno = next_seqno;  

    int eof_retries = 0;

    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
	
    /* set a timeout for receiving ack for eof packet */
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    /* 
     	repeatedly send eof packet and wait for ack until 
     	max retries are reached 
    */
    while (eof_retries < MAX_EOF_RETRIES) {
        if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (const struct sockaddr *)&serveraddr, serverlen) < 0) {
            error("sendto");
        }
	
	// Receive acknowledgment
        int recvlen = recvfrom(sockfd, buffer, MSS_SIZE, 0, (struct sockaddr *)&serveraddr, (socklen_t *)&serverlen);
        if (recvlen < 0) {
		VLOG(INFO, "Size = %d", recvlen);
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                eof_retries++;
                VLOG(INFO, "Timeout waiting for EOF ACK, retrying... (%d/%d)", eof_retries, MAX_EOF_RETRIES);
                continue;
            } else {
                error("recvfrom");
            }
        }

        recvpkt = (tcp_packet *)buffer; // Cast received buffer to TCP packet
        VLOG(DEBUG, "Rec EOF ACK, ackno: %d | %d", recvpkt->hdr.ackno, next_seqno + 1);
	
	/* if the correct ack is received, exit successfully */
        if (recvpkt->hdr.ackno >= next_seqno + END_ACK) {  // Adjust condition to check for the correct ACK
            exit(0);
        }
    }
	
    /* 
     	if max retries are reached without receiving ack, 
     	log the failure and exit
    */

    if (eof_retries >= MAX_EOF_RETRIES) {
        VLOG(INFO, "Max EOF retries reached. File sent, but unable to obtain FIN ACK.");
	exit(0);
    } else {
        VLOG(INFO, "EOF transmission successful.");
	exit(0);
    }

    /* reset the socket to blocking mode*/
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	}

}

    fclose(fp);
    return 0;
}
