#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>
#include <inttypes.h>
#include <unistd.h>
#include <time.h>

#include "czmq.h"

#define DEFAULT_RATE 4

#define TIME_BITLEN 39
#define MACHINE_BITLEN 15
#define SEQ_BITLEN 10

#define MACHINE_BITSHIFT SEQ_BITLEN
#define TIME_BITSHIFT SEQ_BITLEN + MACHINE_BITLEN

#define MACHINE_MASK (1ULL << MACHINE_BITLEN) - 1
#define SEQ_MASK (1ULL << SEQ_BITLEN) - 1

#define DEFAULT_PORT 23138
#define EPOCH 1337000000ULL

// Signal handling
static int s_interrupted = 0;

static void
s_signal_handler (int signal_value)
{
        s_interrupted = 1;
}

static void
s_catch_signals (void)
{
        struct sigaction action;
        action.sa_handler = s_signal_handler;
        action.sa_flags = 0;
        sigemptyset (&action.sa_mask);
        sigaction (SIGINT, &action, NULL);
        sigaction (SIGTERM, &action, NULL);
}

// Millisecond sleep
void
minisleep (int ms)
{
        struct timespec duration;
        int secs = (int) (ms / 1000);
        duration.tv_sec = 1 * secs;
        duration.tv_nsec = 1000000 * (ms - secs * 1000);
        nanosleep (&duration, NULL);
}

// Receiving messages
static uint64_t
id_recv (void *socket)
{
        //  Read message
        zmsg_t *msg = zmsg_recv (socket);
        assert (msg);
        assert (zmsg_size (msg) == 1);
        assert (zmsg_content_size (msg) == 8);
        zframe_t *frame = zmsg_first (msg);
        assert (frame);
        assert (zframe_size (frame) == 8);

        uint64_t id;
        memcpy (&id, zframe_data (frame), 8);
        zmsg_destroy (&msg);
        zframe_destroy (&frame);

        return (be64toh (id));
}

// Printing IDs
void
print_id (uint64_t id)
{
        //  Break it down
        uint64_t ts = (EPOCH * 1000) + (id >> TIME_BITSHIFT);
        uint64_t sec = ts / 1000;
        uint64_t msec = ts - (sec * 1000);
        uint64_t machine = (id >> MACHINE_BITSHIFT) & MACHINE_MASK;
        uint64_t seq = id & SEQ_MASK;

        //  Print it out
        time_t time = (time_t) sec;
        char *timestr = ctime (&time);

        printf ("id:          %llu\n", id);
        printf ("machine:     %llu\n", machine);
        printf ("datetime:    %s", timestr);
        printf ("timestamp:   %llu\n", sec);
        printf ("(msec, seq): (%llu, %llu)\n\n", msec, seq);
}

// Main
int
main (int argc, char **argv)
{
        //  Do some initial sanity checking
        assert (TIME_BITLEN + MACHINE_BITLEN + SEQ_BITLEN == 64);

        //  Parse command-line arguments
        int opt;
        int port = DEFAULT_PORT;
        int rate = DEFAULT_RATE;
        
        while ((opt = getopt (argc, argv, "p:r:")) != -1) {
                switch (opt) {
                case 'p':
                        port = atoi (optarg);
                        break;
                case 'r':
                        rate = atoi (optarg);
                        break;
                }
        }

        //  Initialize ZeroMQ
        zctx_t *context = zctx_new ();
        assert (context);
        void *socket = zsocket_new (context, ZMQ_REQ);
        assert (socket);
        assert (streq (zsocket_type_str (socket), "REQ"));
        int rc = zsocket_connect (socket, "tcp://127.0.0.1:%d", port);
        if (rc != 0) {
                printf ("E: bind failed: %s\n", strerror (errno));
                exit (EXIT_FAILURE);
        }

        //  Main loop
        s_catch_signals ();
        while (1) {
                //  Send a zero-length message to the server
                zmsg_t *msg = zmsg_new ();
                assert (msg);
                rc = zmsg_addmem (msg, "", 0);
                assert (rc == 0);
                zmsg_send (&msg, socket);
                assert (msg == NULL);

                //  Get the response
                uint64_t id = id_recv (socket);
                print_id (id);

                // Sleep
                minisleep (1000 / rate);
                
                // Exit program
                if (s_interrupted) {
                        printf ("\ninterrupt received, killing client…\n");
                        break;
                }
        }
        zmq_close (socket);
        zmq_term (context);
        exit (EXIT_SUCCESS);
}
