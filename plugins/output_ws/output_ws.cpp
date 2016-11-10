/*******************************************************************************
#                                                                              #
#      MJPG-streamer allows to stream JPG frames from an input-plugin          #
#      to several output plugins                                               #
#                                                                              #
#      Copyright (C) 2007 Tom Stöveken                                         #
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include <pthread.h>
#include <fcntl.h>
#include <time.h>
#include <syslog.h>
#include <stdbool.h>
#include <dirent.h>

// Using Alex Hultman's uWebSockets library: https://github.com/uWebSockets/uWebSockets
#include <uWS/uWS.h>

#include <string>
#include <thread>
#include <iostream>
#include <chrono>
#include <atomic>

#include "output_ws.h"

namespace
{
    const char* OUTPUT_PLUGIN_NAME = "Websocket output plugin";

    // Standard mjpg-streamer plugin variables
    pthread_t worker;
    globals *pglobal;
    int fd;
    int delay;
    int max_frame_size;
    unsigned char *frame = NULL;
    int input_number = 0;

    // Websocket variables
    // ------------------------
    uint16_t port = 8200;       // -p,--port

    bool useSSL = false;        // -s,--secure
    std::string certPath{ "" }; // -c,--cert
    std::string keyPath{ "" };  // -k, --key

    uS::TLS::Context sslContext;


    std::atomic<bool> readyToSend( false );
    uv_async_t closeEvent;
    std::thread t;
    uWS::Group<uWS::SERVER> *tServerGroup = nullptr;
}

/******************************************************************************
Description.: print a help message
Input Value.: -
Return Value: -
******************************************************************************/
void help(void)
{
    std::cerr   << " ---------------------------------------------------------------"
    << "\n"     << " Help for output plugin: " << OUTPUT_PLUGIN_NAME
    << "\n"     << " ---------------------------------------------------------------"
    << "\n"     << " The following parameters can be passed to this plugin:"
    << "\n\n"   << " [Required]"
    << "\n"     << " [-p | --port ]..........: Port to listen for websocket connections"
    << "\n\n"   << " [Optional]"
    << "\n"     << " [-s | --secure ]........: Use SSL to establish a secure websocket"
    << "\n"     << " [-c | --cert ]..........: Path to an SSL cert file"
    << "\n"     << " [-k | --key ]...........: Path to an SSL key file"
    << "\n"     << " ---------------------------------------------------------------" 
    << std::endl;
}

/******************************************************************************
Description.: clean up allocated ressources
Input Value.: unused argument
Return Value: -
******************************************************************************/
void worker_cleanup( void *arg )
{
    static unsigned char first_run = 1;

    if( !first_run ) 
    {
        DBG("Already cleaned up ressources\n");
        return;
    }

    first_run = 0;
    OPRINT( "Cleaning up resources allocated by worker thread\n" );

    if( frame != NULL ) 
    {
        free( frame );
    }

    close( fd );

    // Stop broadcasting
    readyToSend = false;

    // Send close event
    uv_async_send( &closeEvent );

    // Wait for the thread to join
    t.join();
}

void close_async_cb( uv_async_t* async ) 
{
    uWS::Group<uWS::SERVER> *serverGroup = (uWS::Group<uWS::SERVER>*)(async->data);

    std::cout << "Closing Server..." << std::endl;

    serverGroup->close();
    uv_close((uv_handle_t*)async, NULL);

    std::cout << "Closed Server" << std::endl;
}

/******************************************************************************
Description.: this is the main worker thread
              it loops forever, grabs a fresh frame and stores it to file
Input Value.:
Return Value:
******************************************************************************/
void *worker_thread(void *args)
{
    int ok          = 1;
    int frame_size  = 0;
    unsigned char *tmp_framebuffer = nullptr;

    // Create thread which handles ws connections asynchronously 
    t = std::thread( []
    {
        uWS::Hub th;
        tServerGroup = &th.getDefaultGroup<uWS::SERVER>();
        th.getDefaultGroup<uWS::SERVER>().addAsync();

        // Add close callback event to server's uv loop
        uv_async_init( th.getLoop(), &closeEvent, close_async_cb );
        closeEvent.data = (void*)tServerGroup;

        std::cout << "Running Server" << std::endl;

        if( useSSL == true )
        {
            th.listen( port, sslContext );
        }
        else
        {
            th.listen( port );
        }

        readyToSend = true;

        th.run();

        std::cout << "Server thread exited." << std::endl;
    } );

    /* set cleanup handler to cleanup allocated ressources */
    pthread_cleanup_push(worker_cleanup, NULL);

    while(ok >= 0 && !pglobal->stop) 
    {
        //DBG("waiting for fresh frame\n");
        pthread_mutex_lock(&pglobal->in[input_number].db);
        pthread_cond_wait(&pglobal->in[input_number].db_update, &pglobal->in[input_number].db);

        /* read buffer */
        frame_size = pglobal->in[input_number].size;

        /* check if buffer for frame is large enough, increase it if necessary */
        if(frame_size > max_frame_size) 
        {
            DBG("increasing buffer size to %d\n", frame_size);

            max_frame_size = frame_size + (1 << 16);

            if((tmp_framebuffer = (unsigned char*)realloc(frame, max_frame_size)) == NULL) 
            {
                pthread_mutex_unlock(&pglobal->in[input_number].db);
                LOG("not enough memory\n");
                return NULL;
            }

            frame = tmp_framebuffer;
        }

        /* copy frame to our local buffer now */
        memcpy(frame, pglobal->in[input_number].buf, frame_size);

        /* allow others to access the global buffer again */
        pthread_mutex_unlock(&pglobal->in[input_number].db);

        DBG( "Framesize: %d\n", frame_size );

        // Send frame here
        if( readyToSend )
        {
            tServerGroup->broadcast( (const char*)frame, frame_size, uWS::OpCode::BINARY );
        }
    }

    /* cleanup now */
    pthread_cleanup_pop(1);
   
    return NULL;
}


/*** plugin interface functions ***/
/******************************************************************************
Description.: this function is called first, in order to initialize
              this plugin and pass a parameter string
Input Value.: parameters
Return Value: 0 if everything is OK, non-zero otherwise
******************************************************************************/
int output_init(output_parameter *param)
{
	int i;
    delay = 0;

    param->argv[0] = (char*)OUTPUT_PLUGIN_NAME;

    /* show all parameters for DBG purposes */
    for( i = 0; i < param->argc; i++ ) 
    {
        DBG( "argv[%d]=%s\n", i, param->argv[i] );
    }

    reset_getopt();

    // Parse all options
    while(1) 
    {
        int option_index = 0;
        int c = 0;

        static struct option long_options[] = 
        {
            { "h",      no_argument,        0, 0 },
            { "help",   no_argument,        0, 0 },
            { "p",      required_argument,  0, 0 },
            { "port",   required_argument,  0, 0 },
            { "s",      no_argument,        0, 0 },
            { "secure", no_argument,        0, 0 },
            { "c",      required_argument,  0, 0 },
            { "cert",   required_argument,  0, 0 },
            { "k",      required_argument,  0, 0 },
            { "key",    required_argument,  0, 0 },
            { 0, 0, 0, 0 }
        };

        // Get the first option passed in from the command line
        c = getopt_long_only( param->argc, param->argv, "", long_options, &option_index );

        // No more options to parse
        if( c == -1 )
        {
            break;
        }
        
        // Unrecognized option
        if( c == '?' ) 
        {
            help();
            return 1;
        }

        switch( option_index ) 
        {
            // h, help
            case 0:
            case 1:
            {
                DBG( "case 0,1\n" );
                help();
                return 1;
                break;
            }
            
            // p, port
            case 2:
            case 3:
            {
                DBG( "case 2,3\n" );
                port = atoi( optarg );
                break;
            }

            // s, secure
            case 4:
            case 5:
            {
                DBG( "case 4,5\n" );
                useSSL = true;
                break;
            }

            // c, cert
            case 6:
            case 7:
            {
                DBG("case 6,7\n");
                char *tempCertPath = realpath( optarg, NULL );

                if( tempCertPath != nullptr )
                {
                    certPath = std::string( (const char*)tempCertPath );
                }
                else
                {
                    OPRINT( "ERROR: Invalid certificate file path provided!\n" );
                    free( tempCertPath );
                    return 1;
                }
                
                free( tempCertPath );
                break;
            }
            
            // k, key
            case 8:
            case 9:
            {
                DBG("case 8,9\n");
                char *tempKeyPath = realpath( optarg, NULL );

                if( tempKeyPath != nullptr )
                {
                    keyPath = std::string( (const char*)tempKeyPath );
                }
                else
                {
                    OPRINT( "ERROR: Invalid key file path provided!\n" );
                    free( tempKeyPath );
                    return 1;
                }
                
                free( tempKeyPath );
                break;
            }

            default:
            {
                OPRINT( "ERROR: Should not have gotten here!\n" );
                return 1;
            }
        }
    }

    // Validate port number
    if( port == 0 )
    {
        OPRINT( "ERROR: Please specify a non-zero port!\n" );
        return 1;
    }

    // Validate SSL cert file information, if necessary, and create SSL context
    if( useSSL == true )
    {
        if( certPath.length() == 0 || keyPath.length() == 0 )
        {
            OPRINT( "ERROR: Specified SSL, but did not provide valid cert and key file paths!\n" );
            return 1;
        }
        else
        {
            sslContext = uS::TLS::createContext( certPath.c_str(), keyPath.c_str() );

            if( !sslContext )
            {
                OPRINT( "ERROR: Failed to create SSL context!\n" );
                return 1;
            }
        }
    }

    
    pglobal = param->global;

    // Validate input plugin count
    if( !( input_number < pglobal->incnt ) ) 
    {
        OPRINT( "ERROR: the %d input_plugin number is too much only %d plugins loaded\n", input_number, pglobal->incnt );
        return 1;
    }

    OPRINT( "input plugin.....: %d: %s\n", input_number, pglobal->in[input_number].plugin );

    return 0;
}

/******************************************************************************
Description.: calling this function stops the worker thread
Input Value.: -
Return Value: always 0
******************************************************************************/
int output_stop(int id)
{
    DBG("will cancel worker thread\n");
    pthread_cancel(worker);
    return 0;
}

/******************************************************************************
Description.: calling this function creates and starts the worker thread
Input Value.: -
Return Value: always 0
******************************************************************************/
int output_run(int id)
{
    DBG("launching worker thread\n");
    pthread_create(&worker, 0, worker_thread, NULL);
    pthread_detach(worker);
    return 0;
}

int output_cmd(int plugin, unsigned int control_id, unsigned int group, int value)
{
    DBG("command (%d, value: %d) for group %d triggered for plugin instance #%02d\n", control_id, value, group, plugin);
    return 0;
}
