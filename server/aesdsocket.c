/******************************************************************************
* Title                 :   aesdsocket
* @file                 :   aesdsocket.c
* @author               :   AMR
* @date                 :   02/09/2024
* @details              :   Linux System Programming and Introduction to Buildroot Assignment 5
*                            
******************************************************************************/

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
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>


/**** Preprocessor Constants *************************************************/
#define LISTENING_PORT                  "9000"  // Port the aplication is listening for
#define MAX_INCOMING_QUEUE_CONN         5       // number of connections allowed on the incoming queue
#define ASSIGNMENT_OUTPUT_FILE          "/var/tmp/aesdsocketdata"
#define ASSIGNMENT_LOG_DESCRIPTION      "AESD_SOCKET"
#define ASSIGNMENT_BUFFER_SIZE          1024


/**** Preprocessor Macros ****************************************************/
#define INIT_LOG(_ident_)               openlog((_ident_), 0, LOG_USER)
#define DEINIT_LOG()                    closelog()
#define DEBUG_LOG(_msg_,...)            syslog(LOG_DEBUG,"DEBUG: " _msg_ "\n" , ##__VA_ARGS__)
#define INFO_LOG(_msg_,...)             syslog(LOG_INFO,"INFO: " _msg_ "\n" , ##__VA_ARGS__)
#define ERROR_LOG(_msg_,...)            syslog(LOG_ERR,"ERROR: " _msg_ "\n" , ##__VA_ARGS__)


/**** Types Definitions *******************************************************/
typedef void  (*signal_handler_cb) (int signal_number);


/**** Constants Definitions **************************************************/


/**** Variable Definitions ***************************************************/
static struct addrinfo *servinfo = NULL;
static FILE * file_h = NULL;
static char rw_buffer[ASSIGNMENT_BUFFER_SIZE] = {0};


/**** Function Prototypes ****************************************************/
static void signal_handler(int signal_number);
static void secure_exit(int code);

static int set_signals_handler(signal_handler_cb handler);
static int make_daemon(void);
static int cnfigure_socket_server(int *server_fd);


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

    socklen_t client_len;
    struct  sockaddr_in client_address;
    int     new_socket;
    char    client_ip[INET_ADDRSTRLEN];
    
    // Obtain parameters
    while ((opt = getopt(argc, argv, "d")) != -1) {
        switch (opt) {
        case 'd':
            run_as_daemon = true;
            printf("Server will run as Deamon!\n");
            break;
        default:
        }
    }

    INIT_LOG(ASSIGNMENT_LOG_DESCRIPTION);

    if ((status = cnfigure_socket_server(&server_fd)) != 0) {
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

    // Open the output file for read/write. Create if not exists
    file_h = fopen(ASSIGNMENT_OUTPUT_FILE, "w+");
    if (file_h == NULL) {
        ERROR_LOG("Error opening file \"%s\"", ASSIGNMENT_OUTPUT_FILE);
        secure_exit(-1);
    } else {
        INFO_LOG("File %s open successfully", ASSIGNMENT_OUTPUT_FILE);
    }


    // Wait incoming connections
    while (1) {

        int rw_bytes = 0;

        // Accept incoming connections
        client_len = sizeof(client_address);
        new_socket = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
        
        if (new_socket == -1) {
            ERROR_LOG("Accepting new incoming connection");
            secure_exit(-1);
        } 

        // Obtain client IP address
        inet_ntop(AF_INET, &client_address.sin_addr, client_ip, INET_ADDRSTRLEN);
        INFO_LOG("Accepted connection from %s", client_ip);

        // Receive data from client until new line character or exit signal is received
        while (1) {

            // Read data from client
            rw_bytes = recv(new_socket, rw_buffer, ASSIGNMENT_BUFFER_SIZE-1, 0);

            if (rw_bytes == -1) {
                ERROR_LOG("Receiving data from client: %s", strerror(errno));
                break;
            
            } else if(rw_bytes == 0) {
                break;
            
            } else if (fwrite(rw_buffer, sizeof(char), rw_bytes, file_h) != rw_bytes) {
                ERROR_LOG("Writting data to file: %s", strerror(errno));
                secure_exit(-1);

            }

            // Check new line character
            if(memchr(rw_buffer, '\n', rw_bytes) != NULL) {
                break;
            }
        }

        // Move the access pointer to the begining of the file
        fseek(file_h, 0, SEEK_SET);

        // Read the file content and send to the client
        while (1) {

            rw_bytes = fread(rw_buffer, sizeof(char), ASSIGNMENT_BUFFER_SIZE-1, file_h);
            if (rw_bytes == 0) {
                break;  // End of the file reached

            } else if (send(new_socket, rw_buffer, rw_bytes, 0) != rw_bytes) {
                ERROR_LOG("Sending data to client: %s", strerror(errno));
                secure_exit(-1);

            }
        }
    }

    fclose(file_h);
    INFO_LOG("File \"%s\" closed", ASSIGNMENT_OUTPUT_FILE);
    freeaddrinfo(servinfo);
    INFO_LOG("Free servinfo. Total %ld bytes", sizeof(struct addrinfo));
    DEINIT_LOG();
    remove(ASSIGNMENT_OUTPUT_FILE);

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
static int cnfigure_socket_server(int *server_fd)
{
    struct  addrinfo    hints;
    int     status;

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

    // Create the socket
    *server_fd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (*server_fd < 0) {
        ERROR_LOG("Failed to create socket\n");
        return EXIT_FAILURE;
    }

    // Bind it to the port we passed in to getaddrinfo:
    status = bind(*server_fd, servinfo->ai_addr, servinfo->ai_addrlen);
    if (status != 0) {
        ERROR_LOG("bind error: %s\n", gai_strerror(status));
        return EXIT_FAILURE;
    }

    // Set the socket in listen mode
    status = listen(*server_fd, MAX_INCOMING_QUEUE_CONN); 
    if (status != 0) {
        ERROR_LOG("listen error: %s\n", gai_strerror(status));
        return EXIT_FAILURE;
    }
    
    INFO_LOG("Server listening in the port %s", LISTENING_PORT);

    return EXIT_SUCCESS;
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
        secure_exit(EXIT_SUCCESS);
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
    // close outuput file
    if (file_h != NULL) {
        fclose(file_h);
        INFO_LOG("File \"%s\" closed", ASSIGNMENT_OUTPUT_FILE);
    }

    // free servinfo memory
    if (servinfo != NULL) {
        freeaddrinfo(servinfo);
        INFO_LOG("Free servinfo. Total %ld bytes", sizeof(struct addrinfo));
    }

    // close log file
    DEINIT_LOG();

    remove(ASSIGNMENT_OUTPUT_FILE);

    exit(code);
}


/**** End of File ************************************************************/