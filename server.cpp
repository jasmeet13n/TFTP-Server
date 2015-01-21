#include<sys/types.h>
#include<sys/socket.h>
#include<netdb.h>
#include<unistd.h>
#include<fcntl.h>

#include<arpa/inet.h>
#include<netinet/in.h>

#include<stdio.h>
#include<string.h>
#include<stdlib.h>

#include<sys/wait.h>
#include<signal.h>

#include<iostream>
#include<fstream>
#include<string>
#include<cstdlib>

#define RRQ 1
#define DATA 3
#define ACK 4
#define ERROR 5
#define OCTET "octet"
#define DATALENGTH 512

// Thanks BEEJ.US for tutorial on how to use SELECT and pack and unpack functions
// Some parts of the code have been taken from BEEJ.US

using namespace std;

//function to reep dead processes (Taken from BEEJ.US)
void sigchld_handler(int s)
{
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

void packi16 (char *buf, unsigned short int i){   //change the host order to network byte order (16bit)
	i = htons(i);
	memcpy(buf,&i,2);
}
unsigned short int unpacki16(char *buf){	//change  network byte order to the host order (16bit)
	unsigned short int i;
	memcpy(&i,buf,2);
	i = ntohs(i);
	return i;
}

// get sockaddr, IPv4 or IPv6 (Taken from BEEJ.US)
void *get_in_addr(struct sockaddr *sa){
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// Structure for receiving TFTP packets
struct tftp{
    uint16_t opcode;                            // 2 Bytes Opcode
    char * filename;                            // Filename
    char * mode;                                // Mode (Octet)
    uint16_t block;                             // Block Number
};

// Decode received packet and store it in TFTP structure
struct tftp *decode(char *packet){
    struct tftp *output;
    output = (struct tftp *)malloc(sizeof(struct tftp));
    output->opcode = unpacki16(packet);         // Get the received Opcode
    if(output->opcode == RRQ){                  // If it is a Read Request, then decode filename and mode
        char tmp[512];
        int i=2, j=0;
        while(packet[i]!='\0')
            tmp[j++] = packet[i++];
        tmp[j]='\0';
        output->filename = (char *)malloc(strlen(tmp)*sizeof(char));
        strcpy(output->filename,tmp);

        while(packet[i]=='\0')
            i++;

        j=0;
        while(packet[i]!='\0')
            tmp[j++] = packet[i++];
        tmp[j]='\0';
        output->mode = (char *)malloc(strlen(tmp)*sizeof(char));
        strcpy(output->mode,tmp);
    }
    else if(output->opcode == ACK){                 // If received packet is Acknowledgment then decode the block number
        output->block = unpacki16(packet+2);
    }
    return output;                                  // Return the decoded TFTP structure
}

// Returns packet (character array) for given opcode, block/error_code, message/error_message, and message/error_message length
char *encode(uint16_t opcode, uint16_t block, char *msg, int len){
    char *packet;
    if(opcode == DATA){                             // If Opcode received is DATA, then pack the given msg into the packet with given block number
        //cout << "Length of data = " << len << endl;
        packet = (char *)malloc((len+4)*sizeof(char));
        packi16(packet,opcode);
        packi16(packet+2,block);
        memcpy(packet+4,msg,len);
        return packet;
    }
    else if(opcode == ERROR){                       // If Opcode received is ERROR, then pack the given error message into the packet with given error_number (passed through block)
        packet = (char *)malloc((len+5)*sizeof(char));
        packi16(packet,opcode);
        packi16(packet+2,block);
        memcpy(packet+4,msg,len);
        memset(packet+4+len,'\0',1);
        return packet;
    }
    return NULL;                                    // Return NULL if any other opcode thatn DATA, ERROR is sent to the function
}

unsigned short int getRandomPort(){
    srand(time(NULL));
    unsigned short int range = rand()%2 + 2;
    srand(time(NULL));
    unsigned short int port = (rand()%1000) + (range*10000);
    return port;
}

int main(int argc, char *argv[]){
	int error;

	if(argc!=3){						            // if user arguments are not equal to 3 then give an error
		fprintf(stderr,"usage: server ip port\n");
		return 1;
	}

    struct sigaction sa;

	struct addrinfo hints;
	struct addrinfo *servinfo, *p;
	struct sockaddr_storage client_addr, tmp_addr;
	socklen_t addr_len, tmp_len;
	char ipstr[INET_ADDRSTRLEN];		//INET6_ADDRSTRLEN for IPv6

	//Getting my address
	memset(&hints,0,sizeof(hints));		// initializing the hints structure to be given to getaddrinfo
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	//hints.ai_flags = AI_PASSIVE;

	if((error = getaddrinfo(argv[1], argv[2], &hints, &servinfo)) != 0){	// get the address info for given IP and PORT number
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(error));
		exit(1);
	}

	int sockfd;
	for(p=servinfo; p!=NULL; p=p->ai_next){
		if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol))== -1){	// create the server socket
			perror("server: socket");
			continue;
		}
		if((error = bind(sockfd, p->ai_addr, p->ai_addrlen))== -1){		// bind the server to the IP address and port
			perror("server: bind");
			continue;
		}
		break;
	}
	if(p==NULL){
		fprintf(stderr, "my sock fd: failed to bind\n");
		return 2;
	}

	// Initializing the required variables
	char buf[1024];
	int num_bytes;

	struct sockaddr new_a;
    new_a = *(p->ai_addr);
    struct sockaddr_in* new_addr;
    new_addr = (struct sockaddr_in*) &new_a;
    new_addr->sin_port = htons(0);

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

  	fd_set master;    // master file descriptor list
	fd_set read_fds;  // temp file descriptor list for select()
	int fdmax;        // maximum file descriptor number
	FD_ZERO(&master);    // clear the master and temp sets
	FD_ZERO(&read_fds);

    time_t rawtime;
    struct tm * utc;

    printf("Server Running ...\n");
	while(1){
		addr_len = sizeof client_addr;
		if ((num_bytes = recvfrom(sockfd, buf, 1023 , 0,(struct sockaddr *)&client_addr, &addr_len)) == -1) {
			perror("recvfrom");
			exit(1);
		}
		struct sockaddr_in* new_client_a = (struct sockaddr_in*)&client_addr;
        printf("Server: got packet from %s on port %hu\n",inet_ntop(client_addr.ss_family,get_in_addr((struct sockaddr *)&client_addr),ipstr, sizeof ipstr),ntohs(new_client_a->sin_port));
        //printf("Server: packet is %d bytes long\n", num_bytes);

        struct tftp *out;
        out = decode(buf);  //decode received message
        cout <<"Opcode = "<< out->opcode <<", Filename = "<< out->filename <<", Mode = "<< out->mode << endl;

        if(out->opcode != RRQ){
            cout << "Invalid Opcode Received" << endl;
            continue;
        }

        if(!fork()){
            close(sockfd);
            //getting new socket with random port number for communication
        	if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol))== -1){	// create the new server socket
		    	perror("server: socket");
    		}
                
	    	if((error = bind(sockfd, &new_a, sizeof(new_a)))== -1){		// bind the server to the IP address and port
	    		perror("server: bind");
    		}

    		socklen_t len = sizeof(new_a);
            if (getsockname(sockfd, &new_a, &len) == -1) {
                perror("getsockname");
            }
            //printf("New Client Port %hu\n",ntohs(new_addr->sin_port));

            /***********************************************************************************/
            // Opening requested file and send ERROR message if file could not be opened
            ifstream myfile;
            myfile.open(out->filename,ios::in | ios::binary);
            if(myfile.is_open()==false){
                cout << "Could not open requested file " << out->filename << endl;
                printf("------------------------------------------------------------\n");
                string message = "**Could not open requested file**";
                char *emsg= (char *)malloc(1024*sizeof(char));
                emsg = strcpy(emsg,message.c_str());
                int emsg_len = strlen(emsg);
                char *epacket = encode(ERROR,1,emsg,emsg_len);
                free(emsg);
                if( (num_bytes = sendto(sockfd,epacket,emsg_len+5,0,(struct sockaddr *)&client_addr,addr_len)) == -1 ){
                    perror("server: sendto");
                    exit(1);
                }
                exit(0);
            }
            // Calculate the size of the file
            streampos first,last;
            first = myfile.tellg();
            myfile.seekg(0,ios::end);
            last = myfile.tellg();
            myfile.seekg(0,ios::beg);
            int filesize = last - first;
            int num_packets = (filesize/512) + 1;           // Calculate the number of packets to be sent

            cout << "File Size = " << filesize << ", Packets = " << num_packets << ", New Port = " << ntohs(new_addr->sin_port) << endl;

            char file_buf[513];
            int block = 0, last_ack = 0, resend = 0, m_len;

            free(out->filename);
            free(out->mode);
            free(out);

        	FD_SET(sockfd,&master);
        	fdmax = sockfd;
        	struct timeval t;
        	t.tv_sec = 0;		// Set timeout to 100 ms
        	t.tv_usec = 100000;

            do{
                if(last_ack==block){
                    myfile.read(file_buf,512);                  // Read next 512 bytes
                    m_len = myfile.gcount();
                    block++;
                    resend = 0;
                    //cout << "Block : " << block << endl;
                }
                if(resend > 0){
                    if(resend == 51){
                        cout << "Time Out !! " << endl;
                        break;
                    }
                    cout << "Resending Attempt " << resend << " for Block " << block << endl;
                }
                char *packet = encode(DATA,block,file_buf,m_len);                       // Encode the data into the TFTP packet structure
                if( (num_bytes = sendto(sockfd,packet,m_len+4,0,(struct sockaddr *)&client_addr,addr_len)) == -1 ){ // Send Data Packet
                    perror("server: sendto");
                    exit(1);
                }
                free(packet);

                t.tv_usec = 100000;                                         // reset the timeout counter
                read_fds = master;
                if( select(fdmax+1, &read_fds, NULL, NULL, &t) == -1){		// Select between client socket to wait for ACK or timeout after 100 ms
                    perror("server: select");
                    exit(4);
                }
                if(FD_ISSET(sockfd,&read_fds)){
                    tmp_len = sizeof tmp_addr;
                    if ( (num_bytes = recvfrom(sockfd, buf, 4 , 0,(struct sockaddr *)&tmp_addr, &tmp_len)) == -1 ) {        // receive ACK
                        perror("recvfrom");
                        exit(1);
                    }
                    struct sockaddr_in* tmp_client_a = (struct sockaddr_in*)&client_addr;
                    if(tmp_client_a->sin_addr.s_addr == new_client_a->sin_addr.s_addr){
                        out = decode(buf);                                      // decode ACK message
                        last_ack = out->block;
                        //cout << "ACK : " << last_ack << endl;
                        free(out);
                    }
                }
                resend++;
            }while(last_ack<num_packets);
            /***********************************************************************************/
            myfile.close();
            close(sockfd);
            printf("Request Complete\n");
            printf("------------------------------------------------------------\n");
            return 0;
    	    exit(0);
		}
        free(out->filename);
        free(out->mode);
        free(out);
	}
	close(sockfd);
	freeaddrinfo(servinfo);
	return 0;
}
