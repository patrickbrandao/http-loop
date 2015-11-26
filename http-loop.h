/*


	Biblioteca


*/
//------------------------------------------------------------------------------------------------

#ifndef _HTTP_LOOP_H
#define _HTTP_LOOP_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/ioctl.h>

#include <sys/resource.h>
#include <errno.h>


#define HTTP_NORMAL 1	// HTTP
#define HTTP_SSL	2	// HTTPs


#define HEADER_CR_NL "\r\n"
#define HEADER_END "\r\n\r\n"



//------------------------------------------------------------------------------------------------


/* _HTTP_LOOP_H end */
#endif
