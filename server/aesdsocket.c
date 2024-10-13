/******************************************************************************
* Title                 :   aesdsocket
* @file                 :   aesdsocket.c
* @author               :   AMR
* @date                 :   02/09/2024
* @details              :   Linux System Programming and Introduction to Buildroot Assignment 5
*                            
******************************************************************************/
#define __USE_GNU

/**** Includes ***************************************************************/
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#if USE_AESD_CHAR_DEVICE
#include "../aesd-char-driver/aesd_ioctl.h"
#endif

/**** Preprocessor Constants *************************************************/
#define LISTENING_PORT                  "9000"  // Port the aplication is listening for
#define MAX_INCOMING_QUEUE_CONN         10      // number of connections allowed on the incoming queue
#define ASSIGNMENT_LOG_DESCRIPTION      "AESD_SOCKET"
#define ASSIGNMENT_BUFFER_SIZE          1024
#define LOG_INTERVAL_SECONDS            10
#if USE_AESD_CHAR_DEVICE
#define     ASSIGNMENT_OUTPUT_FILE      "/dev/aesdchar"
#define     AESDCHAR_IOCSEEDTO_CMD      "AESDCHAR_IOCSEEKTO:"
#define     AESDCHAR_IOCSEEDTO_CMD_SIZE 19
#else
#define     ASSIGNMENT_OUTPUT_FILE      "/var/tmp/aesdsocketdata"
#endif

/**** Preprocessor Macros ****************************************************/
#define INIT_LOG(_ident_)               openlog((_ident_), 0, LOG_USER)
#define DEINIT_LOG()                    closelog()
#define DEBUG_LOG(_msg_,...)            syslog(LOG_DEBUG,"DEBUG: " _msg_ "\n" , ##__VA_ARGS__)
#define INFO_LOG(_msg_,...)             syslog(LOG_INFO,"INFO: " _msg_ "\n" , ##__VA_ARGS__)
#define ERROR_LOG(_msg_,...)            syslog(LOG_ERR,"ERROR: " _msg_ "\n" , ##__VA_ARGS__)


/**** Types Definitions *******************************************************/
typedef void  (*signal_handler_cb) (int signal_number);

#if !USE_AESD_CHAR_DEVICE
struct timer_thread_data_s {
    pthread_mutex_t            *mutex;
    FILE                       *file_h;
};
#endif

struct client_thread_data_s {
    int                         socket;
    struct sockaddr_in          address;
    socklen_t                   client_len;
    char                       *buffer;
    pthread_mutex_t            *mutex;
#if !USE_AESD_CHAR_DEVICE
    FILE                       *file_h;
#endif
   // bool                        complete;
};

typedef struct list_thread list_thread_t;
struct list_thread_s {
    pthread_t                   thread;
    struct client_thread_data_s *data;
    LIST_ENTRY(list_thread_s)   entries;
};


/**** Constants Definitions **************************************************/


/**** Variable Definitions ***************************************************/
#if USE_AESD_CHAR_DEVICE
static int fd;                  // Driver file descriptor
#else
static FILE * file_h = NULL;
#endif
static volatile bool keep_running = true;
static pthread_mutex_t file_mutex;
static bool file_mutex_init = false;


LIST_HEAD(listhead, list_thread_s)  client_list_head;

/**** Function Prototypes ****************************************************/
static void signal_handler(int signal_number);
static void secure_exit(int code);

static int set_signals_handler(signal_handler_cb handler);
#if !USE_AESD_CHAR_DEVICE
static int init_log_timer(timer_t *timer_id,FILE *file_h, pthread_mutex_t *mutex);
static void log_timer_thread (union sigval sigval);
#endif
static int make_daemon(void);
static int configure_socket_server(int *server_fd);
static void* client_thread_func(void *thread_param);

/**** Functions Definitions **************************************************/

/**     
 * @brief   Main Function
 * @param   argc
 * @param   argv
 * @return  0 if SUCCESS, 1 if ERROR 
*/
int main (int argc, char **argv)
{
    int     opt;
    int     status;
    bool    run_as_daemon = false;
    int     server_fd = 0;
    struct  list_thread_s *client_list_thread;
#if !USE_AESD_CHAR_DEVICE
    timer_t timer_id;
#endif

    // Obtain parameters
    while ((opt = getopt(argc, argv, "d")) != -1) {
        switch (opt) {
        case 'd':
            run_as_daemon = true;
            printf("Server will run as Deamon!\n");
            break;
        }
    }

    INIT_LOG(ASSIGNMENT_LOG_DESCRIPTION);

    if ((status = configure_socket_server(&server_fd)) != 0) {
        ERROR_LOG("Server Configuration: %d\n", status);
        return -1;
    }

    if (run_as_daemon) {
        if ((status = make_daemon()) != EXIT_SUCCESS) {
            ERROR_LOG("Deamon Configuration: %d", status);
            secure_exit(-1);
        }
        INFO_LOG("The server processs ID is %d", getpid());
    
    } else {
        printf("Server will run as shell program!\n");

    }

    // Configure signals handler
    if ((status = set_signals_handler(signal_handler)) != EXIT_SUCCESS) {
        ERROR_LOG("sigaction error: %d", status);
        secure_exit(-1);
    }


#if !USE_AESD_CHAR_DEVICE
    // Open the output file for read/write. Create if not exists
    file_h = fopen(ASSIGNMENT_OUTPUT_FILE, "w+");
    if (file_h == NULL) {
        ERROR_LOG("Error opening file \"%s\"", ASSIGNMENT_OUTPUT_FILE);
        secure_exit(-1);
    } else {
        INFO_LOG("File %s open successfully", ASSIGNMENT_OUTPUT_FILE);
    }
#endif

    // Initialize file access mutex
    if (pthread_mutex_init(&file_mutex, NULL) != 0) {
        ERROR_LOG("Init file access mutex initialize");
        secure_exit(-1);
    } else {
        file_mutex_init = true;
    }

#if !USE_AESD_CHAR_DEVICE
    // Initialize log timer
    if (init_log_timer(&timer_id, file_h, &file_mutex) != 0) {
        ERROR_LOG("Periodic log timer setup");
        secure_exit(-1);
    }
#endif

    LIST_INIT(&client_list_head);

    // Wait incoming connections
    while (keep_running) {

        client_list_thread = malloc(sizeof(struct list_thread_s));
        if (!client_list_thread) {
            ERROR_LOG("Thread Memory allocation failed");
            break;
        }

        client_list_thread->data = malloc(sizeof(struct client_thread_data_s));
        if (!client_list_thread->data) {
            ERROR_LOG("Thread Data Memory allocation failed");
            free(client_list_thread);
            break;
        }

        // Accept incoming connections
        client_list_thread->data->client_len = sizeof(struct sockaddr_in);
        client_list_thread->data->socket = accept(server_fd, (struct sockaddr*)&client_list_thread->data->address, &client_list_thread->data->client_len);
        
        if (client_list_thread->data->socket == -1) {
            ERROR_LOG("Accepting new incoming connection");
            free(client_list_thread->data);
            free(client_list_thread);
            break;
        }

        client_list_thread->data->mutex    = &file_mutex;
#if !USE_AESD_CHAR_DEVICE
        client_list_thread->data->file_h   = file_h;
#endif
        //client_list_thread->data->complete = false;

        int thread_ret = pthread_create (&client_list_thread->thread, NULL, client_thread_func, client_list_thread->data);
        if (thread_ret) {
            ERROR_LOG("Error %d creating client pthread", thread_ret);
            free(client_list_thread->data);
            free(client_list_thread);
            break;
        }

        LIST_INSERT_HEAD(&client_list_head, client_list_thread, entries);
        
    }

    // Free allocated memory
    client_list_thread = LIST_FIRST(&client_list_head);
    while (client_list_thread != NULL)
    {
        // Just use a new stack var
        struct list_thread_s *next = LIST_NEXT(client_list_thread, entries);
        pthread_join(client_list_thread->thread, NULL); // ingore errors
        // Cleanup the thread's data
        if (client_list_thread->data != NULL)
        {
            free(client_list_thread->data);
        }
        free(client_list_thread);
        client_list_thread = next;
    }

#if !USE_AESD_CHAR_DEVICE
    fclose(file_h);
    INFO_LOG("File \"%s\" closed", ASSIGNMENT_OUTPUT_FILE);
    timer_delete(timer_id);
    remove(ASSIGNMENT_OUTPUT_FILE);
#endif
    close(server_fd);
    DEINIT_LOG();

    return EXIT_SUCCESS;
}



/**** Private Functions Definitions ******************************************/
/**
 * @brief   Configure signals handler used
 * @param   handler: handler function to configure
 * @return  0 if SUCCESS, 1 if ERROR 
*/
static int set_signals_handler(signal_handler_cb handler)
{
    struct  sigaction   ext_action;
    int     status = 0;

    memset(&ext_action, 0, sizeof(struct sigaction));
    ext_action.sa_handler = handler;

    if (status == 0)
        status = sigaction(SIGTERM, &ext_action, NULL);
    
    if (status == 0)
        status = sigaction(SIGINT, &ext_action, NULL);

    return status;
}


#if !USE_AESD_CHAR_DEVICE
static int init_log_timer(timer_t *timer_id, FILE *file_h, pthread_mutex_t *mutex)
{
    // Declarar el temporizador y la estructura de tiempo
    int                 clock_id = CLOCK_REALTIME;
    struct sigevent     sev;
    struct itimerspec   timer_spec;
    struct timer_thread_data_s timer_data;

    memset(&sev, 0, sizeof(struct sigevent));
    memset(&timer_spec, 0, sizeof(struct itimerspec));
    memset(&timer_data, 0, sizeof(struct timer_thread_data_s));

    timer_data.mutex  = mutex;
    timer_data.file_h = file_h;

    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_value.sival_ptr = &timer_data;
    sev.sigev_notify_function = log_timer_thread;
    sev.sigev_notify_attributes = NULL;

    // Create timer
    if (timer_create(clock_id, &sev, timer_id) == -1) {
        ERROR_LOG("Timer creation in log timer init");
        return EXIT_FAILURE;
    }

    // Config Timer period
    timer_spec.it_value.tv_sec = LOG_INTERVAL_SECONDS;
    timer_spec.it_value.tv_nsec = 0;       
    timer_spec.it_interval.tv_sec = LOG_INTERVAL_SECONDS;
    timer_spec.it_interval.tv_nsec = 0;

    // Start the timer
    if (timer_settime(*timer_id, 0, &timer_spec, NULL) == -1) {
        ERROR_LOG("Settime in log timer init");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}



static void log_timer_thread (union sigval sigval)
{
    time_t                       now;
    struct  tm                  *now_tm;
    struct  timer_thread_data_s *timer_data = (struct timer_thread_data_s*)sigval.sival_ptr;
    char    buffer[50] = {0};
    size_t  timestamp_len = 0;

    now = time(NULL);
    now_tm = localtime(&now);
    if (now_tm == NULL) {
        ERROR_LOG("Getting local time");
        return;
    }

    timestamp_len = strftime(buffer, sizeof(buffer), "timestamp:%a, %d %b %Y %T %z\n", now_tm);
    if (timestamp_len == 0) {
        ERROR_LOG("Formatting timestamp");
        return;
    }

    if (pthread_mutex_lock (timer_data->mutex) != 0) {
        ERROR_LOG("Periodic Log Lock file access failed");
        return;
    }

    if (fwrite(buffer, sizeof(char), timestamp_len, timer_data->file_h) != timestamp_len) {
        ERROR_LOG("Writting timestamp to file");
    }


    if (pthread_mutex_unlock (timer_data->mutex) != 0){
        ERROR_LOG("Periodic Log Unlock file access failed");
        return;
    }
}
#endif


/**
 * @brief   Fork proccess to start in al child process as a daemon
 * @param   none
 * @return  0 if SUCCESS, 1 if ERROR 
*/
static int make_daemon(void)
{
    // Demonize if deamon option is set
    pid_t   deamon_pid;
    bool    result = true;

    /* create new process */
    deamon_pid = fork ();
    
    if (deamon_pid == -1)
        exit(-1);

    else if (deamon_pid != 0)
        exit (EXIT_SUCCESS);

    /* create new session and process group */
    if (result && (setsid () == -1))
        result = false; 

    /* set the working directory to the root directory */
    if (result && chdir ("/") == -1)
        result = false;

    /* redirect fd's 0,1,2 to /dev/null */
    if (result) {
        close(STDIN_FILENO); /* Reopen standard fd's to /dev/null */
        open("/dev/null", O_RDWR);
    
        if (dup2(STDIN_FILENO, STDOUT_FILENO) != STDOUT_FILENO)
            return -1;
        if (dup2(STDIN_FILENO, STDERR_FILENO) != STDERR_FILENO)
            return -1;
    }

    return result ? EXIT_SUCCESS : EXIT_FAILURE;
}


/**
 * @brief   Configure a socket listening in the app port
 * @param   server_fd: pointer where return the socket created
 * @return  0 if SUCCESS, 1 if ERROR 
*/
static int configure_socket_server(int *server_fd)
{
    struct addrinfo     hints;
    struct addrinfo    *p, *servinfo = NULL;
    int    status, optval = 1;

    if (server_fd == NULL)
        return EXIT_FAILURE;

    // Set up sockaddr:
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;     // fill in my IP for me

    status = getaddrinfo(NULL, LISTENING_PORT, &hints, &servinfo);
    if (status != 0) {
        ERROR_LOG("getaddrinfo error: %s\n", gai_strerror(status));
        return EXIT_FAILURE;
    }

    // Try to create the socket in the obtained address
    for (p = servinfo; p != NULL; p = p->ai_next) {

        *server_fd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
        if (*server_fd == -1) {
            ERROR_LOG("Failed to create socket: %s\n", strerror(errno));
            continue;
        }

        // Config SO_REUSEADDR option
        if (setsockopt(*server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
            ERROR_LOG("setsockopt error: %s\n", strerror(errno));
            close(*server_fd);
            freeaddrinfo(servinfo);
            return EXIT_FAILURE;
        }
        
        // Enlazar el socket al puerto
        status = bind(*server_fd, servinfo->ai_addr, servinfo->ai_addrlen);
        if (status == -1) {
            ERROR_LOG("bind error: %s\n", strerror(errno));
            close(*server_fd);
            continue;
        }

        // If this is hit, the socket is correctly created and binded
        break;
    }

    // No address could be binded
    if (p == NULL) {
        ERROR_LOG("Failed to bind socket\n");
        freeaddrinfo(servinfo);
        return EXIT_FAILURE;
    }

    freeaddrinfo(servinfo);

    // Set the socket in listen mode
    status = listen(*server_fd, MAX_INCOMING_QUEUE_CONN); 
    if (status != 0) {
        ERROR_LOG("listen error: %s\n", gai_strerror(status));
        return EXIT_FAILURE;
    }
    
    INFO_LOG("Server listening in the port %s", LISTENING_PORT);

    return EXIT_SUCCESS;
}


static void* client_thread_func(void *thread_param)
{
    struct  client_thread_data_s *client_data = (struct client_thread_data_s*)thread_param;
    int     rw_bytes = 0;
    char    client_ip[INET_ADDRSTRLEN];
#if USE_AESD_CHAR_DEVICE
    int     fd;
#endif

    if (thread_param == NULL) {
        ERROR_LOG("Invalid parameter reference received in thread function");
        return NULL;
    }
    
    if (client_data->mutex == NULL) {
        ERROR_LOG("Invalid mutex reference received in thread function");
        goto err_free_socket;
    }

#if !USE_AESD_CHAR_DEVICE
    if (client_data->file_h == NULL) {
        ERROR_LOG("Invalid file reference received in thread function");
        goto err_free_socket;
    }
#endif
       
    // Obtain client IP address
    inet_ntop(AF_INET, &client_data->address.sin_addr, client_ip, INET_ADDRSTRLEN);
    INFO_LOG("Accepted connection from %s", client_ip);

    client_data->buffer = malloc(ASSIGNMENT_BUFFER_SIZE);
    if (!client_data->buffer) {
        ERROR_LOG("Thread Read/Write Buffer allocation failed");
        goto err_free_socket;
    }

    memset(client_data->buffer, 0x00, ASSIGNMENT_BUFFER_SIZE);

#if !USE_AESD_CHAR_DEVICE
    if (pthread_mutex_lock (client_data->mutex) != 0) {
        ERROR_LOG("Client Lock file access failed");
        goto err_free_memory;
    }
#endif

#if USE_AESD_CHAR_DEVICE
    fd = open(ASSIGNMENT_OUTPUT_FILE, O_RDWR);
    if (fd == -1) {
        ERROR_LOG("Client Open file failed");
        goto err_free_resources;
    }
#endif

    // Receive data from client until new line character or exit signal is received
    while (keep_running) {

        // Read data from client
        rw_bytes = recv(client_data->socket, client_data->buffer, ASSIGNMENT_BUFFER_SIZE-1, 0);

        if (rw_bytes == -1) {
            ERROR_LOG("Receiving data from client: %s", strerror(errno));
            break;
        
        } else if(rw_bytes == 0) {
            break;
        
#if USE_AESD_CHAR_DEVICE
        } else if (strncmp(client_data->buffer, AESDCHAR_IOCSEEDTO_CMD, AESDCHAR_IOCSEEDTO_CMD_SIZE) == 0) {
            
            INFO_LOG("ioctl AESDCHAR_IOCSEEKTO request received\n");
            struct aesd_seekto seekto;
            if (sscanf(client_data->buffer, "AESDCHAR_IOCSEEKTO:%u,%u", &seekto.write_cmd, &seekto.write_cmd_offset) == 2) {
                if (ioctl(fd, AESDCHAR_IOCSEEKTO, &seekto) != 0) {
                    ERROR_LOG("Send ioctl AESDCHAR_IOCSEEKTO failed: %s", strerror(errno));
                    break;
                }
                INFO_LOG("                                          cmd: %d, offset: %d", seekto.write_cmd, seekto.write_cmd_offset);
                goto read;
                
            } else {
                ERROR_LOG("parse ioctl AESDCHAR_IOCSEEKTO failed");
                break;
            }

        } else if (write(fd, client_data->buffer, rw_bytes) != rw_bytes) {
#else
        } else if (fwrite(client_data->buffer, sizeof(char), rw_bytes, client_data->file_h) != rw_bytes) {
#endif
            ERROR_LOG("Writting data to file: %s", strerror(errno));
            break;

        }

        // Check new line character
        if(memchr(client_data->buffer, '\n', rw_bytes) != NULL) {
            break;
        }
    }

    if (keep_running) {

    // Move the access pointer to the begining of the file
#if USE_AESD_CHAR_DEVICE
        lseek(fd, 0, SEEK_SET);
#else
        fseek(client_data->file_h, 0, SEEK_SET);
#endif

read:
        // Read the file content and send to the client
        while (keep_running) {

#if USE_AESD_CHAR_DEVICE
            rw_bytes = read(fd, client_data->buffer, ASSIGNMENT_BUFFER_SIZE-1);
#else
            rw_bytes = fread(client_data->buffer, sizeof(char), ASSIGNMENT_BUFFER_SIZE-1, client_data->file_h);
#endif
            if (rw_bytes == 0) {
                break;  // End of the file reached

            } else if (send(client_data->socket, client_data->buffer, rw_bytes, 0) != rw_bytes) {
                ERROR_LOG("Sending data to client: %s", strerror(errno));
                break;

            }
        }
    }

#if USE_AESD_CHAR_DEVICE
    close(fd);
err_free_resources:
#endif
#if !USE_AESD_CHAR_DEVICE
    pthread_mutex_unlock (client_data->mutex);
err_free_memory:
#endif
    free(client_data->buffer);
err_free_socket:
    close(client_data->socket);

    return NULL;
}


/**     
 * @brief   External event handler function
 * @param   signal_number:  signal received by the app
 * @return  none
 */
static void signal_handler(int signal_number)
{
    if ((signal_number == SIGINT) || (signal_number == SIGTERM)) {
        INFO_LOG("Caught Signal, exiting");
        keep_running = false;
    }
}


/**     
 * @brief   Close any open file, free dynamic allocated memory and exit
 *          with the given code
 * @param   code:  exit status code
 * @return  none
 */
static void secure_exit(int code)
{

#if USE_AESD_CHAR_DEVICE
    // close outuput file
    close(fd);
#else
    // close outuput file
    if (file_h != NULL) {
        fclose(file_h);
        INFO_LOG("File \"%s\" closed", ASSIGNMENT_OUTPUT_FILE);
    }
#endif

    if (file_mutex_init)
        pthread_mutex_destroy(&file_mutex);

    // close log file
    DEINIT_LOG();

    remove(ASSIGNMENT_OUTPUT_FILE);

    exit(code);
}


/**** End of File ************************************************************/