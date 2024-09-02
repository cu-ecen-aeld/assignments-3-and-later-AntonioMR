/******************************************************************************
* Title                 :   Writer
* @file                 :   writer.c
* @author               :   AMR
* @date                 :   02/09/2024
* @details              :   Linux System Programming and Introduction to Buildroot Assignment 2 
******************************************************************************/

/******************************************************************************
* Includes
******************************************************************************/
#include <stdio.h>
#include <syslog.h>

/**     
 * @brief   Main Function
 * @param   argc
 * @param   argv
 * @return  0 if SUCCESS, 1 if ERROR 
*/
int main (int argc, char **argv)
{
    FILE * file_h;
    
    openlog(NULL, 0, LOG_USER);

    // Check parameters available
    if (argc < 3) {
        syslog(LOG_ERR, "2 parameters expected, %d provided", argc - 1 );
        return 1;
    }
    
    file_h = fopen(argv[1], "w");

    if (file_h == NULL)
    {
        syslog(LOG_ERR, "Error opening file \"%s\"", argv[1]);
        return 1;
    }

    fprintf(file_h, "%s\n", argv[2]);
    syslog(LOG_DEBUG, "Writing %s to %s", argv[2], argv[1]);

    fclose(file_h);
    closelog();

    return 0;
}