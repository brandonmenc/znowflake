#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <inttypes.h>
#include <unistd.h>
#include <time.h>

#include "czmq.h"

#define TIME_BITLEN 39
#define MACHINE_BITLEN 15
#define SEQ_BITLEN 10

#define MACHINE_BITSHIFT SEQ_BITLEN
#define TIME_BITSHIFT SEQ_BITLEN + MACHINE_BITLEN

#define MACHINE_MASK (1ULL << MACHINE_BITLEN) - 1
#define SEQ_MASK (1ULL << SEQ_BITLEN) - 1

#define DEFAULT_PORT 23138
#define EPOCH 1337000000ULL

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

int
main (int argc, char **argv)
{
        //  Do some initial sanity checking
        assert (TIME_BITLEN + MACHINE_BITLEN + SEQ_BITLEN == 64);

        //  Parse command-line arguments
        int opt;
        int port = DEFAULT_PORT;
        
        while ((opt = getopt (argc, argv, "p:")) != -1) {
                switch (opt) {
                case 'p':
                        port = atoi (optarg);
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
        int request_nbr;
        for (request_nbr = 0; request_nbr != 100; request_nbr++) {
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
        }
        zmq_close (socket);
        zmq_term (context);
        exit (EXIT_SUCCESS);
}
