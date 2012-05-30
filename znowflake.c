#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <zmq.h>

#define TIME_BITLEN 39
#define MACHINE_BITLEN 15
#define SEQ_BITLEN 10

#define MACHINE_BITSHIFT SEQ_BITLEN
#define TIME_BITSHIFT SEQ_BITLEN + MACHINE_BITLEN

#define MACHINE_MAX (1ULL << MACHINE_BITLEN) - 1
#define SEQ_MAX (1ULL << SEQ_BITLEN) - 1

#define DEFAULT_PORT 23138
#define BASETIME 1337000000ULL

static inline uint64_t
get_ts (void)
{
        struct timeval t;
        gettimeofday (&t, NULL);
        return (((t.tv_sec - BASETIME) * 1000) + (t.tv_usec / 1000));
}

static inline uint64_t
build_id (uint64_t ts, uint64_t machine, uint64_t seq)
{
        return (ts << TIME_BITSHIFT) | (machine << MACHINE_BITSHIFT) | seq;
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
        int has_machine_opt = 0;
        char *port;
        uint64_t machine;
        
        while ((opt = getopt (argc, argv, "p:m:")) != -1) {
                switch (opt) {
                case 'p':
                        has_port_opt = 1;
                        port = optarg;
                        break;
                case 'm':
                        has_machine_opt = 1;
                        machine = atoll (optarg);
                        break;
                }
        }

        //  Build the ZMQ endpoint
        char *zmq_endpoint = NULL;

        if (!has_port_opt)
                asprintf (&port, "%d", DEFAULT_PORT);
        asprintf (&zmq_endpoint, "tcp://*:%s", port);

        //  Sanity check the machine number
        if (!has_machine_opt) {
                printf ("Error: no machine number specified. Use the -m command-line option.\n");
                exit (EXIT_FAILURE);
        }
        else if (machine > MACHINE_MAX) {
                printf ("Error: machine number too large. Cannot be greater than %llu\n", MACHINE_MAX);
                exit (EXIT_FAILURE);
        }

        //  Daemonize
        pid_t pid, sid;

        pid = fork ();
        if (pid < 0) {
                exit (EXIT_FAILURE);
        }	
        if (pid > 0) {
                exit (EXIT_SUCCESS);
        }

        umask (0);
                
        sid = setsid ();
        if (sid < 0) {
                exit (EXIT_FAILURE);
        }
        
        if ((chdir ("/")) < 0) {
                exit (EXIT_FAILURE);
        }
        
        close (STDIN_FILENO);
        close (STDOUT_FILENO);
        close (STDERR_FILENO);

        //  Sleep for 1ms to prevent collisions
        struct timespec ms;
        ms.tv_sec = 1;
        ms.tv_nsec = 1000000;
        nanosleep (&ms, NULL);

        //  Main loop
        void *context = zmq_init (1);
        void *socket = zmq_socket (context, ZMQ_REP);
        zmq_bind (socket, zmq_endpoint);

        uint64_t ts = 0;
        uint64_t last_ts = 0;
        uint64_t seq = 0;
        
        while (1) {
                //  Wait for the next request
                zmq_msg_t request;
                zmq_msg_init (&request);
                zmq_recv (socket, &request, 0);
                zmq_msg_close (&request);

                //  Grab a time click
                last_ts = ts;
                ts = get_ts ();

                //  Increment sequence
                if (ts != last_ts) {
                        //  We're in a new time click, so reset the sequence
                        seq = 0;
                }
                else if (seq == SEQ_MAX) {
                        //  Wrapped sequence, so wait for the next time tick
                        seq = 0;
                        nanosleep (&ms, NULL);
                }
                else {
                        //  Still in the same time click
                        seq++;
                }

                //  Build the ID and put it in network byte order
                uint64_t id = build_id (ts, machine, seq);
                uint64_t id_be64 = htobe64 (id);

                //  Reply
                zmq_msg_t reply;
                zmq_msg_init_size (&reply, 8);
                memcpy (zmq_msg_data (&reply), &id_be64, 8);
                zmq_send (socket, &reply, 0);
                zmq_msg_close (&reply);
        }
        exit (EXIT_SUCCESS);
}
