#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <zmq.h>
#include <libconfig.h>

#define TIME_BITLEN 39
#define MACHINE_BITLEN 15
#define SEQ_BITLEN 10

#define MACHINE_BITSHIFT SEQ_BITLEN
#define TIME_BITSHIFT SEQ_BITLEN + MACHINE_BITLEN

#define MACHINE_MAX (1ULL << MACHINE_BITLEN) - 1
#define SEQ_MAX (1ULL << SEQ_BITLEN) - 1

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

// Help
static void
print_help (void)
{
        printf ("Example: `znowflaked -p 5555 -m 1234` ");
        printf ("starts daemon for machine 1234 listening on port 5555\n\n");
}

// Building IDs
static inline uint64_t
get_ts (void)
{
        struct timeval t;
        gettimeofday (&t, NULL);
        return (((t.tv_sec - EPOCH) * 1000) + (t.tv_usec / 1000));
}

static inline uint64_t
build_id (uint64_t ts, uint64_t machine, uint64_t seq)
{
        return (ts << TIME_BITSHIFT) | (machine << MACHINE_BITSHIFT) | seq;
}

// Main
int
main (int argc, char **argv)
{
        //  Do some initial sanity checking
        assert (TIME_BITLEN + MACHINE_BITLEN + SEQ_BITLEN == 64);

        //  Parse command-line arguments
        int opt;
        int has_port_opt = 0;
        int has_machine_opt = 0;
        int has_config_file_opt = 0;
        int machine_specified = 0;

        const char *config_file_path;
        int port = DEFAULT_PORT;
        uint64_t machine;
        
        while ((opt = getopt (argc, argv, "hp:m:f:")) != -1) {
                switch (opt) {
                case 'h':
                        print_help ();
                        exit (EXIT_SUCCESS);
                case 'p':
                        has_port_opt = 1;
                        port = atoi (optarg);
                        break;
                case 'm':
                        has_machine_opt = 1;
                        machine = atoll (optarg);
                        machine_specified = 1;
                        break;
                case 'f':
                        has_config_file_opt = 1;
                        config_file_path = optarg;
                        break;
                }
        }

        // Read the config file
        if (has_config_file_opt) {
                config_t cfg;

                config_init (&cfg);

                if (!config_read_file (&cfg, config_file_path)) {
                        config_destroy (&cfg);
                        fprintf (stderr, "Invalid config file\n");
                        exit (EXIT_FAILURE);
                }

                long unsigned int machine_from_file;
                if (config_lookup_int (&cfg, "machine", &machine_from_file) && !has_machine_opt) {
                        machine_specified = 1;
                        machine = (uint64_t) machine_from_file;
                }

                long unsigned int port_from_file;
                if (config_lookup_int (&cfg, "port", &port_from_file) && !has_port_opt)
                        port = (int) port_from_file;
        }

        //  Build the ZMQ endpoint
        char *zmq_endpoint = NULL;
        asprintf (&zmq_endpoint, "tcp://*:%d", port);

        //  Sanity check the machine number
        if (!machine_specified) {
                fprintf (stderr, "No machine number specified.\n");
                exit (EXIT_FAILURE);
        }
        else if (machine > MACHINE_MAX) {
                fprintf (stderr, "Machine number too large. Cannot be greater than %llu\n", MACHINE_MAX);
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
        ms.tv_sec = 0;
        ms.tv_nsec = 1000000;
        nanosleep (&ms, NULL);

        //  Main loop
        void *context = zmq_init (1);
        void *socket = zmq_socket (context, ZMQ_REP);
        zmq_bind (socket, zmq_endpoint);

        uint64_t ts = 0;
        uint64_t last_ts = 0;
        uint64_t seq = 0;

        s_catch_signals ();        
        while (1) {
                //  Wait for the next request
                zmq_msg_t request;
                zmq_msg_init (&request);
                zmq_recv (socket, &request, 0);
                zmq_msg_close (&request);

                //  Grab a time click
                last_ts = ts;
                ts = get_ts ();

                //  Make sure the system clock wasn't reversed on us
                if (ts < last_ts) {
                        //  Wait until it catches up
                        while (ts <= last_ts) {
                                nanosleep (&ms, NULL);
                                ts = get_ts ();
                        }
                }

                //  Increment the sequence number
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

                // Exit program
                if (s_interrupted) {
                        printf ("interrupt received, killing server…\n");
                        break;
                }
        }
        zmq_close (socket);
        zmq_term (context);
        exit (EXIT_SUCCESS);
}
