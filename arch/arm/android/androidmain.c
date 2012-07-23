/* Android main() for Gforth on Android

  Copyright (C) 2012 Free Software Foundation, Inc.

  This file is part of Gforth.

  Gforth is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation, either version 3
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, see http://www.gnu.org/licenses/.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdarg.h>

#include "forth.h"

int sockfd;

void gforth_waitfor_client(int sig)
{
  int newsockfd;
  socklen_t clilen;
  struct sockaddr_in cli_addr;
  
  listen(sockfd, 1);
  clilen = sizeof(cli_addr);
  newsockfd = accept(sockfd, 
		     (struct sockaddr *) &cli_addr, 
		     &clilen);
  if (newsockfd < 0) 
    fprintf(stderr, "ERROR on accept\n");

  dup2(newsockfd, 0); // set socket to stdin
  dup2(newsockfd, 1); // set socket to stdout
  dup2(newsockfd, 2); // set socket to stderr
}

void gforth_server(int portno)
{
  struct sockaddr_in serv_addr;
  
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) 
    fprintf(stderr, "ERROR opening socket\n");
  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(portno);
  if (bind(sockfd, (struct sockaddr *) &serv_addr,
	   sizeof(serv_addr)) < 0) 
    fprintf(stderr, "ERROR on binding\n");
  
  gforth_waitfor_client(0);
}

#ifdef __ANDROID__
#include <android/log.h>
#include "android_native_app_glue.h"

void android_main(struct android_app* state)
{
  char statepointer[30];
  char *argv[] = { "gforth", NULL };
  char *env[] = { "HOME=/sdcard/gforth/home",
		  "SHELL=/system/bin/sh",
                  statepointer,
		  NULL };

  snprintf(statepointer, sizeof(statepointer), "APP_STATE=%p", state);
  setenv("HOME", "/sdcard/gforth/home", 1);
  setenv("SHELL", "/system/bin/sh", 1);
  setenv("APP_STATE", statepointer+10, 1);
  
  app_dummy();
  gforth_server(4444);
  bsd_signal(SIGPIPE, gforth_waitfor_client); 

  gforth_main(1, argv, env);
}
#else
void main(int argc, char ** argv, char ** env)
{
  gforth_server(4444);

  gforth_main(argc, argv, env);
}
#endif
