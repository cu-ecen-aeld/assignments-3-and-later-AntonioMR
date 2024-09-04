#include "systemcalls.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#define __OPEN_LOG(__str__)         openlog((__str__), 0, LOG_USER)
#define __CLOSE_LOG()               closelog()

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{

/*
 * TODO  add your code here
 *  Call the system() function with the command set in the cmd
 *   and return a boolean true if the system() call completed with success
 *   or false() if it returned a failure
*/
    bool result = false;

    if (cmd == NULL)
        return result;
    
    __OPEN_LOG(NULL);

    int ret = system(cmd);

    if (ret == 0) {
        syslog(LOG_DEBUG, "Command \"%s\" executed succesfully", cmd);
        result = true;

    } else if (ret == -1) {
        syslog(LOG_DEBUG, "Error: Child process could not be created. Errno %d ( %s )", errno, strerror(errno));
    
    } else if (WIFEXITED(ret) && WEXITSTATUS(ret)) {
        syslog(LOG_DEBUG, "Error: Error executing command \"%s\". Errno %d ( %s )", cmd, errno, strerror(errno));
    
    } else if (WIFSIGNALED(ret)) {
        syslog(LOG_DEBUG, "Error: Error command \"%s\" exit for a signal ( %d )", cmd, WTERMSIG (ret));
    
    }
    
    __CLOSE_LOG();

    return result;
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    bool result = false;
    int status;
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

    va_end(args);
/*
 * TODO:
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
*/
    __OPEN_LOG(NULL);

    pid_t pid = fork();

    if (pid == -1) {
        syslog(LOG_DEBUG, "Error: Fork failed");
    
    } else if (pid == 0) {
        // Child process
        execv(command[0], command);

        // this code is only executed if execv fails
        syslog(LOG_DEBUG, "Error: Child execv command failed");
        __CLOSE_LOG();
        exit (EXIT_FAILURE);

    } else {
        // Parent process

        // wait for the child process to complete
        if (waitpid(pid, &status, 0) == -1) {
            // waitpid failed
            syslog(LOG_DEBUG, "Error: Waitpid in Parent process failed");

        } else if (WIFEXITED(status)) {
            // Result = true if process doesn't exit by a signal
            result = (WEXITSTATUS (status) == 0);

            syslog(LOG_DEBUG, "Child process exit status %d", WIFEXITED(status));
        }
    }

    __CLOSE_LOG();

    return result;
}



/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    bool result = false;
    int status;
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

    va_end(args);

/*
 * TODO
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/
    __OPEN_LOG(NULL);

    int fd = open(outputfile, O_RDWR|O_TRUNC|O_CREAT, 0666);
    if (fd < 0) {
        syslog(LOG_DEBUG, "Error: Creating file %s", outputfile);
        
    } else {

        pid_t pid = fork();

        if (pid == -1) {
            syslog(LOG_DEBUG, "Error: Fork failed");
        
        } else if (pid == 0) {
            // Child process

            if (dup2(fd, 1) < 0) {
                perror("dup2"); abort();
            
            } else {
                close(fd);
                execv(command[0], command);
                
                // this code is only executed if execv fails
                syslog(LOG_DEBUG, "Error: Child execv command failed");
                __CLOSE_LOG();
                exit (EXIT_FAILURE);
            }

        } else if (pid > 0) {
            // Parent process

            // wait for the child process to complete
            if (waitpid(pid, &status, 0) == -1) {
                syslog(LOG_DEBUG, "Error: Waitpid in Parent process failed");

            } else if (WIFEXITED(status)) {
                // Result = true if process doesn't exit by a signal
                result = (WEXITSTATUS (status) == 0);

                syslog(LOG_DEBUG, "Child process exit status %d", WIFEXITED(status));
            }

            close(fd);
        }
    }

    return result;
}
