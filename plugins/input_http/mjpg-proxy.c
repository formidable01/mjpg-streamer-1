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
#include <errno.h>
#include <stdlib.h>
#include <getopt.h>


#include "version.h"

#include "mjpg-proxy.h"


#define DELIMITER 2
#define HEADER 1
#define CONTENT 0
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
    state->index = 0;
    state->part = HEADER;
    state->last_four_bytes = 0;
    if (reset_boundary) {
        if (state->boundary.string) 
            free(state->boundary.string);
        state->boundary.string = malloc(MAX_DELIM_SIZE);
        if (state->boundary.string == NULL) {
            fprintf(stderr, "Failed to allocate memory: %s\n", strerror(errno));
            return;  // need better way to signal error condition
        }
        strncpy(state->boundary.string, BOUNDARY, 10); 
        state->delimiter_found = FALSE;
        if (state->buffer) 
            free(state->buffer);
        state->buffer = malloc(BUFFER_SIZE);
        state->buflen = BUFFER_SIZE;
        if (state->buffer == NULL) {
            fprintf(stderr, "Failed to allocate memory: %s\n", strerror(errno));
            return;  // need better way to signal error condition
        }
    }  
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
    int i, j, is_quoted;
    char delim_buffer[MAX_DELIM_SIZE+2];
 
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
            else if (!state->delimiter_found) {
                (search_pattern_compare(&state->boundary, buffer[i]));
                if (search_pattern_matches(&state->boundary)) {
                    DBG("Boundary found, extracting delimiter\n");
                    state->part = DELIMITER;
                }
            }
            break; 

        case CONTENT:
            if (state->index >= state->buflen) {
                DBG("Image exceeds current buffer size of %d.  Increasing by 100KB.\n", state->buflen);
                state->buffer = realloc(state->buffer, state->index + 102400);
                if (state->buffer == NULL) {
                    fprintf(stderr, "Failed to allocate memory: %s\n", strerror(errno));
                    return;
                }
                else
                    state->buflen = state->buflen + 102400;
            }
            state->buffer[state->index++] = buffer[i];
            search_pattern_compare(&state->boundary, buffer[i]);
            if (search_pattern_matches(&state->boundary)) {
                // remove '--' at line start and boundary from image buffer
                state->index -= (strlen(state->boundary.string)+2);
                DBG("Image of length %d received\n", (int)state->index);
                if (state->on_image_received) // callback
                  state->on_image_received(state->buffer, state->index);
                init_extractor_state(state, FALSE); // reset fsm, retain boundary and current buflen
            }
            break;
        
        case DELIMITER:
            push_byte(&state->last_four_bytes, buffer[i]);
            if (is_crlfcrlf(state->last_four_bytes)) {
                DBG("Delimiter found\n");
                // terminate delimiter, removing CRLF from the last two loops
                delim_buffer[j-3] = 0;
                free(state->boundary.string);
                state->boundary.string = &delim_buffer[0];
                // strncpy(state->boundary.string, delim_buffer, j-3);
                j = 0;
                search_pattern_reset(&state->boundary);
                state->part = HEADER;
                state->delimiter_found = TRUE;
                DBG("Boundary = %s, len = %lu\n", state->boundary.string, strlen(state->boundary.string));
            }
            else {
                // Delimiter must follow RFC 2046 Section 5.1.1
                //      boundary := 0*69<bchars> bcharsnospace
                //      bchars := bcharsnospace / " "
                //      bcharsnospace := DIGIT / ALPHA / "'" / "(" / ")" /
                //           "+" / "_" / "," / "-" / "." /
                //           "/" / ":" / "=" / "?"
                //
                // and optional surrounding double-quotes
                if (j == 0) {
                    if (buffer[i] == '\"' ) {
                        is_quoted = TRUE;
                        j++;
                        continue;
                    }
                    else if (buffer[i] == '\r') {
                        fprintf(stderr, "Invalid empty boundary value.\n");
                        break;
                    }
                    else
                        is_quoted = FALSE;
                }
                if (j > 0 && buffer[i] == '\"') {
                    if (is_quoted) {
                        j++;
                        continue;
                    }
                    else {
                        fprintf(stderr, "Invalid character in multipart MIME delimiter.  Unterminated double quote.\n");
                        break; 
                    }
                }
                if (!is_quoted && buffer[i] == ' ') {
                    fprintf(stderr, "Invalid character in multipart MIME delimiter.  Spaces are only allowed in quoted strings.\n");
                    break;
                }
                if (buffer[i] == '\r') {
                    if (is_quoted && buffer[i-1] == ' ') {
                        fprintf(stderr, "Invalid character in multipart MIME delimiter.  Quoted strings may not end with space.\n");
                        break;
                    }
                }
                if (!valid_boundary_token(buffer[i])) {
                    fprintf(stderr, "Invalid character in multipart MIME delimiter.  Character = %d\n", buffer[i]);
                    break;
                }
                if ((j >= MAX_DELIM_SIZE && !is_quoted) || (j >= MAX_DELIM_SIZE+2 && is_quoted)) {
                    fprintf(stderr, "Multipart MIME delimiter longer than RFC 2046 maximum 70 characters.\n");
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

            DBG("socket value is %d\n", state->sockfd);
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

