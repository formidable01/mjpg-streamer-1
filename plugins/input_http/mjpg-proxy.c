/*******************************************************************************
#                                                                              #
#      Copyright (C) 2011 Eugene Katsevman                                     #
#                                                                              #
# This program is free software; you can redistribute it and/or modify         #
# it under the terms of the GNU General Public License as published by         #
# the Free Software Foundation; version 2 of the License.                      #
#                                                                              #
# This program is distributed in the hope that it will be useful,              #
# but WITHOUT ANY WARRANTY; without even the implied warranty of               #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                #
# GNU General Public License for more details.                                 #
#                                                                              #
# You should have received a copy of the GNU General Public License            #
# along with this program; if not, write to the Free Software                  #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA    #
#                                                                              #
*******************************************************************************/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>


#include "version.h"

#include "mjpg-proxy.h"

#include "misc.h"


#define DELIMITER 2
#define HEADER 1
#define CONTENT 0
#define BUFFER_SIZE 1024 * 100
#define NETBUFFER_SIZE 1024 * 4
#define PATHBUFFER_SIZE 256
#define MAX_DELIM_SIZE 72 // Per RFC 2046
#define TRUE 1
#define FALSE 0

// TODO: this must be decoupled from mjpeg-streamer
char * BOUNDARY =     "boundary=";
char * DEFAULT_PATH = "/?action=stream";

struct extractor_state state;

void init_extractor_state(struct extractor_state * state, int reset_boundary) {
    state->length = 0;
    state->part = HEADER;
    state->last_four_bytes = 0;
    if (reset_boundary) 
        state->boundary.string = BOUNDARY;     
    search_pattern_reset(&state->boundary);
}

void init_mjpg_proxy(struct extractor_state * state) {
    state->hostname = strdup("localhost");
    state->port = strdup("8080");
    state->path = strdup(DEFAULT_PATH);

    init_extractor_state(state, TRUE);

}

// main method
// we process all incoming buffer byte per byte and extract binary data from it to state->buffer
// if boundary is detected, then callback for image processing is run
// TODO; decouple from mjpeg streamer and ensure content-length processing
//       for that, we must properly work with headers, not just detect them
void extract_data(struct extractor_state * state, char * buffer, int length) {
    int i, j;
    char * delim_buffer;
 
    j = 0; // delimiter index
    for (i = 0; i < length && !*(state->should_stop); i++) {
        switch (state->part) {
        case HEADER:
            push_byte(&state->last_four_bytes, buffer[i]);
            if (is_crlfcrlf(state->last_four_bytes)) {
                state->part = CONTENT;
            }
            else if (is_crlf(state->last_four_bytes)) {
                search_pattern_reset(&state->boundary);
            }
            else if (search_pattern_compare(&state->boundary, buffer[i])) {
                if (search_pattern_matches(&state->boundary)) {
                    DBG("Boundary found, extracting delimiter\n");
                    delim_buffer = malloc(MAX_DELIM_SIZE+2);
                    state->part = DELIMITER;
                }
            }
            break; 

        case CONTENT:
            state->buffer[state->length++] = buffer[i];
            search_pattern_compare(&state->boundary, buffer[i]);
            if (search_pattern_matches(&state->boundary)) {
                // remove CRLF and '--' before boundary
                state->length -= (strlen(state->boundary.string)+4);
                DBG("Image of length %d received\n", (int)state->length);
                if (state->on_image_received) // callback
                  state->on_image_received(state->buffer, state->length);
                init_extractor_state(state, FALSE); // reset fsm, retain boundary delimiterinit_e
            }
            break;
        
        case DELIMITER:
            push_byte(&state->last_four_bytes, buffer[i]);
            if (is_crlfcrlf(state->last_four_bytes)) {
                DBG("Delimiter found\n");
                // terminate delimiter, removing 0x0d0a from last two loops
                delim_buffer[j-1] = 0;
                state->boundary.string = strdup(delim_buffer);
                j = 0;
                search_pattern_reset(&state->boundary);
                state->part = HEADER;

                DBG("Boundary = %s, len = %d", state->boundary.string, strlen(state->boundary.string));
                free(delim_buffer);
                delim_buffer=NULL;
            }
            else {
                // delimiter must follow rfc 2046, max 70 characters plus 
                // optional surrounding double-quotes and CRLF - add these
                if (j > MAX_DELIM_SIZE+2) {
                    DBG("Multipart MIME delimiter longer than RFC 2046 maximum.\n");
                    break;
                }
                memcpy(delim_buffer+j, &buffer[i], 1);
                j++;
            }
            break;
        }

    }

}

void send_request_and_process_response(struct extractor_state * state) {
    int recv_length;
    char netbuffer[NETBUFFER_SIZE];
    char request[PATHBUFFER_SIZE] = { 0 }; 

    init_extractor_state(state, TRUE);
    
    // build request
    char prefix [] = "GET ";
    char suffix [] = " HTTP/1.0\r\n\r\n";
    snprintf(request, PATHBUFFER_SIZE, "%s %s %s", prefix, state->path, suffix);

    // send request
    send(state->sockfd, request, sizeof(request), 0);

    // and listen for answer until sockerror or THEY stop us 
    // TODO: we must handle EINTR here, it really might occur
    while ( (recv_length = recv(state->sockfd, netbuffer, sizeof(netbuffer), 0)) > 0 && !*(state->should_stop))
        extract_data(state, netbuffer, recv_length) ;

}

// TODO:this must be reworked to decouple from mjpeg-streamer
void show_help(char * program_name) {

fprintf(stderr, " ---------------------------------------------------------------\n" \
                " Help for input plugin..: %s\n" \
                " ---------------------------------------------------------------\n" \
                " The following parameters can be passed to this plugin:\n\n" \
                " [-v | --version ]........: current SVN Revision\n" \
                " [-h | --help]............: show this message\n"
                " [-H | --host]............: select host to data from, localhost is default\n"
                " [-p | --port]............: port, defaults to 8080\n"
                " [-u | --path]............: path, defaults to %s\n"
                " ---------------------------------------------------------------\n", program_name, DEFAULT_PATH);
}
// TODO: this must be reworked, too. I don't know how
void show_version() {
    printf("Version - %s\n", VERSION);
}

int parse_cmd_line(struct extractor_state * state, int argc, char * argv []) {
    while (TRUE) {
        static struct option long_options [] = {
            {"help", no_argument, 0, 'h'},
            {"version", no_argument, 0, 'v'},
            {"host", required_argument, 0, 'H'},
            {"port", required_argument, 0, 'p'},
            {"path", required_argument, 0, 'u'},
            {0,0,0,0}
        };

        int index = 0, c = 0;
        c = getopt_long_only(argc,argv, "hvH:p:u:", long_options, &index);

        if (c==-1) break;

        if (c=='?') {
            show_help(argv[0]);
            return 1;
            }
        else
            switch (c) {
            case 'h' :
                show_help(argv[0]);
                return 1;
                break;
            case 'v' :
                show_version();
                return 1;
                break;
            case 'H' :
                free(state->hostname);
                state->hostname = strdup(optarg);
                break;
            case 'p' :
                free(state->port);
                state->port = strdup(optarg);
                break;
            case 'u' :
                free(state->path);
                state->path = strdup(optarg);
                break;
            }
    }

  return 0;
}

// TODO: consider using hints for http

// TODO: consider moving delays to plugin command line arguments
void connect_and_stream(struct extractor_state * state) {
    struct addrinfo * info, * rp;
    int errorcode;
    while (TRUE) {
        errorcode = getaddrinfo(state->hostname, state->port, NULL, &info);
        if (errorcode) {
            perror(gai_strerror(errorcode));
        };
        for (rp = info ; rp != NULL; rp = rp->ai_next) {
            state->sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (state->sockfd <0) {
                perror("Can't allocate socket, will continue probing\n");
                continue;
            }

            DBG("socket value is %d\n", sockfd);
            if (connect(state->sockfd, (struct sockaddr *) rp->ai_addr, rp->ai_addrlen)>=0 ) {
                DBG("connected to host\n");
                break;
            }

            close(state->sockfd);

        }

        freeaddrinfo(info);

        if (rp==NULL) {
            perror("Can't connect to server, will retry in 5 sec");
            sleep(5);
        }
        else
        {
            send_request_and_process_response(state);
            
            DBG ("Closing socket\n");
            close (state->sockfd);
            if (*state->should_stop)
                break;
            sleep(1);
        };
    }

}

void close_mjpg_proxy(struct extractor_state * state) {
    free(state->hostname);
    free(state->port);
    free(state->path);
}

