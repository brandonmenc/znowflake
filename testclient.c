#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <time.h>
#include <zmq.h>

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
        zmq_msg_t message;
        zmq_msg_init (&message);
        if (zmq_recv (socket, &message, 0)) {
                printf ("Fatal: error receiving zmq message.\n");
                exit (EXIT_FAILURE);
        }
        int size = zmq_msg_size (&message);
        if (size != 8) {
                printf ("Fatal: ID payload was not 64 bits.\n");
                exit (EXIT_FAILURE);
        }
        uint64_t id;
        memcpy (&id, zmq_msg_data (&message), 8);
        zmq_msg_close (&message);
        //  Convert from network byte order
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
        printf ("%s", timestr);
        printf ("%llu, %llu, %llu\n", msec, machine, seq);
}

int
main (int argc, char **argv)
{
        //  Do some initial sanity checking
        if (TIME_BITLEN + MACHINE_BITLEN + SEQ_BITLEN != 64) {
                printf ("Major Error: the specified ID length does not equal 64 bits. Recompile!\n");
                exit (EXIT_FAILURE);
        }

        //  Parse command-line arguments
        int opt;
        int has_port_opt = 0;
        char *port;
        
        while ((opt = getopt (argc, argv, "p:")) != -1) {
                switch (opt) {
                case 'p':
                        has_port_opt = 1;
                        port = optarg;
                        break;
                }
        }

        //  Build the ZMQ endpoint
        char *zmq_endpoint = NULL;

        if (!has_port_opt)
                asprintf (&port, "%d", DEFAULT_PORT);
        asprintf (&zmq_endpoint, "tcp://*:%s", port);

        //  Main loop
        void *context = zmq_init (1);
        void *socket = zmq_socket (context, ZMQ_REQ);
        zmq_connect (socket, zmq_endpoint);

        int request_nbr;
        for (request_nbr = 0; request_nbr != 100; request_nbr++) {
                //  Send an arbitrary one-byte message to the server
                zmq_msg_t request;
                zmq_msg_init_size (&request, 1);
                memcpy (zmq_msg_data (&request), "x", 1);
                zmq_send (socket, &request, 0);
                zmq_msg_close (&request);

                uint64_t id = id_recv (socket);
                print_id (id);
        }
        zmq_close (socket);
        zmq_term (context);
        exit (EXIT_SUCCESS);
}
