#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#define QLEN 6 /* size of request queue */
#define PARMAX 255 /* max number of participants */
int visits = 0; /* counts client connections */

//sends to all observers
void sendToObs(char* str, int obsArr[PARMAX]){
    uint16_t msglen = (uint16_t)strlen(str);
    for(int i=0;i<PARMAX;i++){
        if(obsArr[i]!=-1){
            send(obsArr[i],&msglen,sizeof(uint16_t),MSG_DONTWAIT&MSG_NOSIGNAL);
            send(obsArr[i],str,strlen(str),MSG_DONTWAIT&MSG_NOSIGNAL);
        }
    }
}

int main(int argc, char **argv) {
	struct protoent *ptrp; /* pointer to a protocol table entry */
	struct sockaddr_in sad1, sad2; /* structure to hold server's address */
	struct sockaddr_in cad; /* structure to hold participant's address */
    struct timeval parTimeouts[PARMAX]; /*Array of parTimeouts*/
    struct timeval obsTimeouts[PARMAX];
    struct timeval start,end,timediff;
	int numpar=0; //number of participants
	int numobs=0; //number of observers
	int sd, sd2, x, y; /* socket descriptors */
	int port_participant; /* protocol port number for participants*/
	int port_observer; /* protocol port number for observers */
	int alen; /* length of address */
	int n; //recv return val
	int highfd=0; //highest socket descriptor value
	uint8_t usernameSize; //size of username
	int optval = 1; /* boolean value when we set socket option */
	char buf[1000]; /* buffer for string the server sends */
	int parArr[PARMAX]; /* keep track of the participants socket id's */
    int obsArr[PARMAX]; /* keep track of the observers socket id's */
    int parArrUN[PARMAX];//array of participants w/o username
    int obsArrUN[PARMAX]; //array of observers w/o username
	char usernames[PARMAX][11]; /*Array of usernames */
    fd_set readfds; //read and write socket sets
    uint16_t msglen; //uint8_t specifying message length
    bool brk=false; //for breaking from loops with loops inside of them



    //checks correct number of args
	if( argc != 3 ) {
		fprintf(stderr,"Error: Wrong number of arguments\n");
		fprintf(stderr,"usage:\n");
		fprintf(stderr,"./server server_port_Participant server_port_Observer\n");
		exit(EXIT_FAILURE);
	}

	memset((char *)&sad1,0,sizeof(sad1)); /* clear sockaddr structure */
	memset((char *)&sad2,0,sizeof(sad2)); /* clear sockaddr structure */
	sad1.sin_family = AF_INET; /* set family to Internet */
	sad1.sin_addr.s_addr = INADDR_ANY; /* set the local IP address */
	sad2.sin_family = AF_INET; /* set family to Internet */
	sad2.sin_addr.s_addr = INADDR_ANY; /* set the local IP address */

	port_participant = atoi(argv[1]); /* convert argument to binary */
	if (port_participant > 0) { /* test for illegal value */
		sad1.sin_port = htons((u_short)port_participant);
	}
    else { /* print error message and exit */
        fprintf(stderr,"Error: Bad port number %s\n",argv[1]);
		exit(EXIT_FAILURE);
	}

	port_observer = atoi(argv[2]); /* convert argument to binary */
	if (port_observer > 0) { /* test for illegal value */
		sad2.sin_port = htons((u_short)port_observer);
	}
    else { /* print error message and exit */
		fprintf(stderr,"Error: Bad port number %s\n",argv[2]);
	    exit(EXIT_FAILURE);
	}

	/* Map TCP transport protocol name to protocol number */
	if ( ((long int)(ptrp = getprotobyname("tcp"))) == 0) {
		fprintf(stderr, "Error: Cannot map \"tcp\" to protocol number");
		exit(EXIT_FAILURE);
	}

	/* Create sockets */
	sd = socket(PF_INET, SOCK_STREAM, ptrp->p_proto);
	sd2 = socket(PF_INET, SOCK_STREAM, ptrp->p_proto);
	if (sd < 0) {
		fprintf(stderr, "Error: Socket creation failed\n");
		exit(EXIT_FAILURE);
	}
	if (sd2 < 0) {
		fprintf(stderr, "Error: Socket creation failed\n");
		exit(EXIT_FAILURE);
	}

	/* Allow reuse of port - avoid "Bind failed" issues */
	if( setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0 ) {
		fprintf(stderr, "Error Setting socket option failed\n");
		exit(EXIT_FAILURE);
	}
	if( setsockopt(sd2, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0 ) {
		fprintf(stderr, "Error Setting socket option failed\n");
		exit(EXIT_FAILURE);
	}

	/* Bind a local address to the socket */
	if (bind(sd, (struct sockaddr *)&sad1, sizeof(sad1)) < 0) {
		fprintf(stderr,"Error: Bind failed\n");
		exit(EXIT_FAILURE);
	}

	if (bind(sd2, (struct sockaddr *)&sad2, sizeof(sad2)) < 0) {
		fprintf(stderr,"Error: Bind failed\n");
		exit(EXIT_FAILURE);
	}

	/* Specify size of request queue */
	if (listen(sd, QLEN) < 0) {
		fprintf(stderr,"Error: Listen failed\n");
		exit(EXIT_FAILURE);
	}
	if (listen(sd2, QLEN) < 0) {
		fprintf(stderr,"Error: Listen failed\n");
		exit(EXIT_FAILURE);
	}
    //zero out all arrays
	for (int i=0; i < PARMAX; i++){
	    parArr[i] = -1;
        obsArr[i] = -1;
        *usernames[i] = 0;
        parArrUN[i] = -1;
        obsArrUN[i] = -1;
        parTimeouts[i].tv_sec = -1;
        obsTimeouts[i].tv_sec = -1;
	}



	alen = sizeof(cad);
	highfd = sd2;
	x = 0;
	y = 0;
	/* Main server loop - accept and handle requests */
	while (1) {
        brk=false;
	    memset(buf, 0, sizeof(buf));

	    //add all sockets to readfds
	    FD_ZERO(&readfds);
		FD_SET(sd,&readfds);
	    FD_SET(sd2,&readfds);

        //add all active participants and anyone w/o associated username to readfds
        printf("readfds: \n");
	    for(int i=0;i<PARMAX;i++){
            if(parArr[i]!=-1){
                FD_SET(parArr[i],&readfds);
                printf("parArr[%d]=%d\n",i,parArr[i]);
                printf("usernames[%d]=%s\n",i,usernames[i]);
            }
            if(obsArr[i] != -1){
            	FD_SET(obsArr[i], &readfds);
            }
            if(parArrUN[i]!=-1){
                FD_SET(parArrUN[i],&readfds);
                printf("parArrUN[%d]=%d\n",i,parArrUN[i]);
            }
            if(obsArrUN[i] != -1){
                FD_SET(obsArrUN[i],&readfds);
                printf("obsArrUN[%d]=%d\n",i,obsArrUN[i]);
            }
        }

        //find smallest value in timeval arrays and use that for select
        struct timeval tv;
        int expireInd;
        bool expireIsPar;
        tv.tv_sec = -1;
        for(int i=1;i<PARMAX;i++){
            if(parTimeouts[i].tv_sec != -1){
                if(parTimeouts[i].tv_sec < tv.tv_sec){
                    tv = parTimeouts[i];
                    expireInd = i;
                    expireIsPar = true;
                }
                else if(parTimeouts[i].tv_sec == tv.tv_sec){
                    if(parTimeouts[i].tv_usec < tv.tv_usec){
                        tv=parTimeouts[i];
                        expireInd = i;
                        expireIsPar = true;
                    }
                }
            }
            if(obsTimeouts[i].tv_sec != -1){
                if(obsTimeouts[i].tv_sec < tv.tv_sec){
                    tv = obsTimeouts[i];
                    expireInd = i;
                    expireIsPar = false;
                }
                else if(obsTimeouts[i].tv_sec == tv.tv_sec){
                    if(obsTimeouts[i].tv_usec < tv.tv_usec){
                        tv=obsTimeouts[i];
                        expireInd = i;
                        expireIsPar = false;
                    }
                }
            }
        }
        gettimeofday(&start,NULL);
        //select (wait for input)
        int selret;
        if(tv.tv_sec==-1){
            selret = select(highfd+1, &readfds, NULL, NULL, NULL);
        }
        else{
            selret = select(highfd+1, &readfds, NULL, NULL, &tv);
        }
        gettimeofday(&end,NULL);
        timersub(&end,&start,&timediff);
        //subtract time spent in select from all timers
        for(int i=0;i<PARMAX;i++){
            if(parTimeouts[i].tv_sec != -1){
                timersub(&parTimeouts[i],&timediff,&parTimeouts[i]);
            }
            if(obsTimeouts[i].tv_sec != -1){
                timersub(&obsTimeouts[i],&timediff,&obsTimeouts[i]);
            }
        }
        if (selret == -1){
            fprintf(stderr, "Select Error: %d\n", errno);
        }
        //timeout: close connection waiting on username too long
        else if(selret == 0){
            if(expireIsPar){
                close(parArrUN[expireInd]);
            }
            else{
                close(obsArrUN[expireInd]);
            }
        }
        //connection or message recieved
        else{
            //participant connection
            if(FD_ISSET(sd,&readfds)){
                if ((x=accept(sd, (struct sockaddr *)&cad, &alen)) < 0) {
		            fprintf(stderr, "Accept failed\n");
		            close(x);
	            }
                else{
                   for (int i = 0; i < PARMAX; i++){
                       if (parArrUN[i] == -1){
                           parArrUN[i] = x;
                           parTimeouts[i].tv_sec = 60;
                           parTimeouts[i].tv_usec = 0;
                           sprintf(buf,"Y\n");
                           send(x,buf,strlen(buf),0);
                           if(highfd<x)
                                highfd=x;
                           break;
                       }
                       if (i == PARMAX - 1){
                            sprintf(buf,"N");
                            send(x,buf,strlen(buf),0);
                            close(x);
                        }
                    }
                }
            }
            //observer connection
            if (FD_ISSET(sd2,&readfds)){
                if ((y=accept(sd2, (struct sockaddr *)&cad, &alen)) < 0) {
		            fprintf(stderr, "Accept failed\n");
		            close(y);
	            }
	            else{
	                for (int i = 0; i < PARMAX; i++){
	                    if (obsArrUN[i] == -1){
	                        obsArrUN[i] = y;
                            obsTimeouts[i].tv_sec = 60;
                            obsTimeouts[i].tv_usec = 0;
	                        sprintf(buf, "Y");
	                        send(obsArrUN[i], buf, strlen(buf), 0);
                            if(highfd<y)
                                 highfd=y;
	                        break;
	                    }
	                    if (i == PARMAX - 1){
                            sprintf(buf,"N");
                            send(y,buf,strlen(buf),0);
	                        close(y);
	                    }
	                }
	            }
            }
            //message input
            for(int i=0;i<PARMAX;i++){
            	
	            if(FD_ISSET(parArr[i],&readfds)){

	                //message
                    n = recv(parArr[i], buf, sizeof(buf), 0);
                    buf[n]=0;
                    //particpant left
                    if (n == 0){
                        char* left = (char *)malloc(sizeof(char*));
                        sprintf(left, "%s has left\n", usernames[i]);
                        msglen = strlen(left);
                        sendToObs(left, obsArr);

                        close(parArr[i]);
                        parArr[i] =-1;
                        *usernames[i]=0;

                        if(obsArr[i] != -1){
                            close(obsArr[i]);
                            obsArr[i]=-1;
                        }
                    }
                    else{
                        sendToObs(buf,obsArr);
                    }
                }

                //participant username
                if(FD_ISSET(parArrUN[i],&readfds)){
                    n = recv(parArrUN[i], &usernameSize, 1, 0);
                    n = recv(parArrUN[i], buf, usernameSize, 0);
                    buf[usernameSize]=0;


                    //username with invalid characters (does not reset timeout)
                    for (int j = 0; j < usernameSize; j++){
                        if ((buf[j] > 122) || (buf[j] < 97 && buf[j] > 90) || (buf[j] < 65 && buf[j] > 57) || (buf[j] < 48)){
                            sprintf(buf, "I");
                            send(parArrUN[i],buf,strlen(buf),0);
                            brk=true;
                            break;
                        }
                    }
                    if(brk==true)
                        break;

                    //username already in use (resets timeout)
                    for(int j=0;j<PARMAX;j++){
                        if((parArr[j] != -1) && (usernames[j] != 0)){
                            if(strcmp(usernames[j],buf)==0){
                                sprintf(buf, "T");
                                send(parArrUN[i],buf,strlen(buf),0);
                                //reset timeout
                                parTimeouts[i].tv_sec=60;
                                parTimeouts[i].tv_usec=0;
                                brk=true;
                                break;
                            }
                        }
                    }
                    if(brk==true)
                        break;

                    //if name is valid
                    for(int j=0;j<PARMAX;j++){
                        if(parArr[j] == -1){
                            strcpy(usernames[j],buf);
                            parArr[j] = parArrUN[i];
                            parArrUN[i] = -1;
                            parTimeouts[i].tv_sec = -1;
                            sprintf(buf, "Y");
                            send(parArr[j],buf,strlen(buf),0);
                            sprintf(buf, "User %s has joined", usernames[j]);
                            sendToObs(buf, obsArr);
                            brk=true;
                            break;
                        }
                    }
                    if(brk==true)
                        break;
                }

                //observer username
                if(FD_ISSET(obsArrUN[i],&readfds)){
                    n = recv(obsArrUN[i], &usernameSize, 1, 0);
                    n = recv(obsArrUN[i], buf, usernameSize, 0);
                    buf[usernameSize]=0;

                    //look for matches in username array
                    bool foundName=false;
                    for(int j=0;j<PARMAX;j++){
                        //if username in use
                        if(usernames[j] != 0 && strcmp(usernames[j],buf)==0){
                            //if username already associated with observer
                            if(obsArr[j] != -1){
                                foundName = true;
                                sprintf(buf, "T");
                                send(obsArrUN[i],buf,strlen(buf),0);
                                obsTimeouts[i].tv_sec = 60;
                                obsTimeouts[i].tv_usec = 0;
                                break;
                            }
                            //if participant not yet associated with observer, make association
                            else{
                                foundName = true;
                                obsArr[j] = obsArrUN[i];
                                obsArrUN[i] = -1;
                                obsTimeouts[i].tv_sec = -1;
                                sprintf(buf, "Y");
                                send(obsArr[j],buf,strlen(buf),0);
                                break;
                            }
                        }
                    }
                    //if user doesn't exist, send N and disconnect observer
                    if(!foundName){
                        sprintf(buf, "N");
                        send(obsArrUN[i],buf,strlen(buf),0);
                        close(obsArrUN[i]);
                        obsTimeouts[i].tv_sec = -1;
                    }
                }
            }
        }
	}
	close(sd2);
	close(sd);
}
