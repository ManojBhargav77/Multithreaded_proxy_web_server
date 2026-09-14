/* Windows-compatible Multi-Threaded Proxy with Cache
 * This version keeps pthreads/semaphores (for MinGW/MSYS2).
 * Sockets are adapted for Winsock2.
 */
#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  #define close closesocket
  #define bzero(b,len) memset((b), 0, (len))
  #define bcopy(src,dst,len) memcpy((dst), (src), (len))
  #ifndef SHUT_RDWR
    #define SHUT_RDWR SD_BOTH
  #endif
  typedef SOCKET sock_t;
  typedef int socklen_t;
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netdb.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <sys/wait.h>
  typedef int sock_t;
#endif

#define MAX_BYTES 4096
#define MAX_CLIENTS 400
#define MAX_SIZE (200*(1<<20))
#define MAX_ELEMENT_SIZE (10*(1<<20))

typedef struct cache_element cache_element;

struct cache_element{
    char* data;
    int len;
    char* url;
    time_t lru_time_track;
    cache_element* next;
};

cache_element* find(char* url);
int add_cache_element(char* data,int size,char* url);
void remove_cache_element();

int port_number = 8080;				
sock_t proxy_socketId;					
pthread_t tid[MAX_CLIENTS];         
sem_t seamaphore;	                
pthread_mutex_t lock;               

cache_element* head = NULL;                
int cache_size = 0;             

int sendErrorMessage(sock_t socketId, int status_code)
{
	char str[1024];
	char currentTime[64];
	time_t now = time(0);

	struct tm data;
#ifdef _WIN32
	gmtime_s(&data, &now);
#else
	data = *gmtime(&now);
#endif
	strftime(currentTime,sizeof(currentTime),"%a, %d %b %Y %H:%M:%S %Z", &data);

	switch(status_code)
	{
		case 400: snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: Proxy/1.0\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Request</H1>\n</BODY></HTML>", currentTime);
				  send(socketId, str, (int)strlen(str), 0);
				  break;

		case 403: snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: Proxy/1.0\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
				  send(socketId, str, (int)strlen(str), 0);
				  break;

		case 404: snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: Proxy/1.0\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
				  send(socketId, str, (int)strlen(str), 0);
				  break;

		case 500: snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: Proxy/1.0\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
				  send(socketId, str, (int)strlen(str), 0);
				  break;

		case 501: snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: Proxy/1.0\r\n\r\n<HTML><HEAD><TITLE>501 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
				  send(socketId, str, (int)strlen(str), 0);
				  break;

		case 505: snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: Proxy/1.0\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
				  send(socketId, str, (int)strlen(str), 0);
				  break;

		default:  return -1;

	}
	return 1;
}

static sock_t connectRemoteServer(const char* host_addr, int port_num)
{
	sock_t remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
	if( remoteSocket == (sock_t)(-1) )
	{
		fprintf(stderr, "Error in Creating Socket.\n");
		return (sock_t)(-1);
	}
	
	struct hostent *host = gethostbyname(host_addr);	
	if(host == NULL)
	{
		fprintf(stderr, "No such host exists.\n");	
#ifdef _WIN32
        closesocket(remoteSocket);
#else
        close(remoteSocket);
#endif
		return (sock_t)(-1);
	}

	struct sockaddr_in server_addr;
	bzero((char*)&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons((unsigned short)port_num);
	bcopy((char *)host->h_addr,(char *)&server_addr.sin_addr.s_addr,(size_t)host->h_length);

	if( connect(remoteSocket, (struct sockaddr*)&server_addr, (socklen_t)sizeof(server_addr)) < 0 )
	{
		fprintf(stderr, "Error in connecting !\n"); 
#ifdef _WIN32
        closesocket(remoteSocket);
#else
        close(remoteSocket);
#endif
		return (sock_t)(-1);
	}
	return remoteSocket;
}


static int handle_request(sock_t clientSocket, ParsedRequest *request, char *tempReq)
{
	char *buf = (char*)malloc(sizeof(char)*MAX_BYTES);
	strcpy(buf, "GET ");
	strcat(buf, request->path);
	strcat(buf, " ");
	strcat(buf, request->version);
	strcat(buf, "\r\n");

	size_t len = strlen(buf);

	if (ParsedHeader_set(request, "Connection", "close") < 0){
		printf("set header key not work\n");
	}

	if(ParsedHeader_get(request, "Host") == NULL)
	{
		if(ParsedHeader_set(request, "Host", request->host) < 0){
			printf("Set \"Host\" header key not working\n");
		}
	}

	if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0) {
		printf("unparse failed\n");
	}

	int server_port = 80;
	if(request->port != NULL)
		server_port = atoi(request->port);

	sock_t remoteSocketID = connectRemoteServer(request->host, server_port);
	if(remoteSocketID == (sock_t)(-1))
	{
		free(buf);
		return -1;
	}

	int bytes_recv = 0;
	int bytes_send = send(remoteSocketID, buf, (int)strlen(buf), 0);
	bzero(buf, MAX_BYTES);

	bytes_recv = recv(remoteSocketID, buf, MAX_BYTES-1, 0);
	char *temp_buffer = (char*)malloc(sizeof(char)*MAX_BYTES);
	int temp_buffer_size = MAX_BYTES;
	int temp_buffer_index = 0;

	while(bytes_recv > 0)
	{
		int w = send(clientSocket, buf, bytes_recv, 0);
		if (w < 0) {
			perror("Error in sending data to client socket.\n");
			break;
		}
		for(int i=0;i<bytes_recv;i++){
			temp_buffer[temp_buffer_index++] = buf[i];
			if (temp_buffer_index >= temp_buffer_size) {
				temp_buffer_size += MAX_BYTES;
				temp_buffer=(char*)realloc(temp_buffer,temp_buffer_size);
			}
		}

		bzero(buf, MAX_BYTES);
		bytes_recv = recv(remoteSocketID, buf, MAX_BYTES-1, 0);
	} 
	temp_buffer[temp_buffer_index]='\0';
	free(buf);
	add_cache_element(temp_buffer, (int)strlen(temp_buffer), tempReq);
	free(temp_buffer);
	
 	close(remoteSocketID);
	return 0;
}

static int checkHTTPversion(const char *msg)
{
	if(strncmp(msg, "HTTP/1.1", 8) == 0) return 1;
	if(strncmp(msg, "HTTP/1.0", 8) == 0) return 1;
	return -1;
}


static void* thread_fn(void* socketNew)
{
	sem_wait(&seamaphore); 

    int* t= (int*)(socketNew);
	sock_t socketId = (sock_t)(*t);
	int bytes_recv_client = 0;
	size_t len = 0;

	char *buffer = (char*)calloc(MAX_BYTES,sizeof(char));
	bzero(buffer, MAX_BYTES);
	bytes_recv_client = recv(socketId, buffer, MAX_BYTES, 0);
	
	while(bytes_recv_client > 0)
	{
		len = strlen(buffer);
		if(strstr(buffer, "\r\n\r\n") == NULL)
		{	
			bytes_recv_client = recv(socketId, buffer + len, MAX_BYTES - (int)len, 0);
		}
		else{
			break;
		}
	}
	
	char *tempReq = (char*)malloc(strlen(buffer)+1);
	memcpy(tempReq, buffer, strlen(buffer));
	tempReq[strlen(buffer)] = '\0';
	
	struct cache_element* temp = find(tempReq);

	if( temp != NULL){
		int size=temp->len;
		int pos=0;
		char response[MAX_BYTES];
		while(pos<size){
			int chunk = (size - pos) > MAX_BYTES ? MAX_BYTES : (size - pos);
			bzero(response,MAX_BYTES);
			memcpy(response, temp->data + pos, (size_t)chunk);
			send(socketId,response,chunk,0);
			pos += chunk;
		}
		printf("Data retrieved from the Cache\n");
	}
	else if(bytes_recv_client > 0)
	{
		len = strlen(buffer); 
		ParsedRequest* request = ParsedRequest_create();
		if (ParsedRequest_parse(request, buffer, (int)len) < 0) 
		{
		   	printf("Parsing failed\n");
		}
		else
		{	
			bzero(buffer, MAX_BYTES);
			if(!strcmp(request->method,"GET"))							
			{
				if( request->host && request->path && (checkHTTPversion(request->version) == 1) )
				{
					int rc = handle_request(socketId, request, tempReq);		
					if(rc == -1) sendErrorMessage(socketId, 500);
				}
				else sendErrorMessage(socketId, 500);
			}
            else
            {
                printf("This code doesn't support any method other than GET\n");
            }
		}
		ParsedRequest_destroy(request);
	}
	else if( bytes_recv_client < 0)
	{
		perror("Error in receiving from client.\n");
	}
	else if(bytes_recv_client == 0)
	{
		printf("Client disconnected!\n");
	}

	shutdown(socketId, SHUT_RDWR);
	close(socketId);
	free(buffer);
	sem_post(&seamaphore);	
	free(tempReq);
	return NULL;
}


int main(int argc, char * argv[]) {

#ifdef _WIN32
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
		fprintf(stderr, "WSAStartup failed.\n");
		return 1;
	}
#endif

	sock_t client_socketId;
	socklen_t client_len; 
	struct sockaddr_in server_addr, client_addr; 

    sem_init(&seamaphore,0,MAX_CLIENTS);
    pthread_mutex_init(&lock,NULL);
    
	if(argc == 2)
	{
		port_number = atoi(argv[1]);
	}
	else
	{
		printf("Usage: %s <port>\n", argv[0]);
#ifdef _WIN32
		WSACleanup();
#endif
		return 1;
	}

	printf("Setting Proxy Server Port : %d\n",port_number);

	proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
	if( proxy_socketId == (sock_t)(-1))
	{
		perror("Failed to create socket.\n");
#ifdef _WIN32
		WSACleanup();
#endif
		return 1;
	}

	int reuse = 1;
	if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0) 
        perror("setsockopt(SO_REUSEADDR) failed\n");

	bzero((char*)&server_addr, sizeof(server_addr));  
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons((unsigned short)port_number); 
	server_addr.sin_addr.s_addr = INADDR_ANY; 

	if( bind(proxy_socketId, (struct sockaddr*)&server_addr, (socklen_t)sizeof(server_addr)) < 0 )
	{
		perror("Port is not free\n");
#ifdef _WIN32
		WSACleanup();
#endif
		return 1;
	}
	printf("Binding on port: %d\n",port_number);

	int listen_status = listen(proxy_socketId, MAX_CLIENTS);
	if(listen_status < 0 )
	{
		perror("Error while Listening !\n");
#ifdef _WIN32
		WSACleanup();
#endif
		return 1;
	}

	int i = 0;
	int Connected_socketId[MAX_CLIENTS];

	while(1)
	{
		bzero((char*)&client_addr, sizeof(client_addr));
		client_len = (socklen_t)sizeof(client_addr); 

		client_socketId = accept(proxy_socketId, (struct sockaddr*)&client_addr, &client_len);
		if(client_socketId == (sock_t)(-1))
		{
			fprintf(stderr, "Error in Accepting connection !\n");
			continue;
		}
		else{
			Connected_socketId[i] = (int)client_socketId;
		}

		struct sockaddr_in* client_pt = (struct sockaddr_in*)&client_addr;
		struct in_addr ip_addr = client_pt->sin_addr;
		char str[INET_ADDRSTRLEN];					
		inet_ntop( AF_INET, &ip_addr, str, INET_ADDRSTRLEN );
		printf("Client connected: port %d, ip %s \n",ntohs(client_addr.sin_port), str);

		pthread_create(&tid[i],NULL,thread_fn, (void*)&Connected_socketId[i]);
		i = (i + 1) % MAX_CLIENTS; 
	}
	close(proxy_socketId);
#ifdef _WIN32
	WSACleanup();
#endif
 	return 0;
}

cache_element* find(char* url){
    cache_element* site=NULL;
    pthread_mutex_lock(&lock);
    if(head!=NULL){
        site = head;
        while (site!=NULL)
        {
            if(!strcmp(site->url,url)){
                site->lru_time_track = time(NULL);
                break;
            }
            site=site->next;
        }       
    }
    pthread_mutex_unlock(&lock);
    return site;
}

void remove_cache_element(){
    cache_element * p ;
	cache_element * q ;
	cache_element * temp;
    pthread_mutex_lock(&lock);
	if( head != NULL) { 
		for (q = head, p = head, temp =head ; q -> next != NULL; 
			q = q -> next) {
			if(( (q -> next) -> lru_time_track) < (temp -> lru_time_track)) {
				temp = q -> next;
				p = q;
			}
		}
		if(temp == head) { 
			head = head -> next;
		} else {
			p->next = temp->next;	
		}
		cache_size = cache_size - (temp -> len) - (int)sizeof(cache_element) - 
		(int)strlen(temp -> url) - 1;
		free(temp->data);     		
		free(temp->url);
		free(temp);
	} 
    pthread_mutex_unlock(&lock);
}

int add_cache_element(char* data,int size,char* url){
    pthread_mutex_lock(&lock);
    int element_size=size+1+(int)strlen(url)+(int)sizeof(cache_element);
    if(element_size>MAX_ELEMENT_SIZE){
        pthread_mutex_unlock(&lock);
        return 0;
    }
    else
    {   while(cache_size+element_size>MAX_SIZE){
            remove_cache_element();
        }
        cache_element* element = (cache_element*) malloc(sizeof(cache_element));
        element->data= (char*)malloc(size+1);
		strcpy(element->data,data); 
        element -> url = (char*)malloc(1+( strlen( url )));
		strcpy( element -> url, url );
		element->lru_time_track=time(NULL);
        element->next=head; 
        element->len=size;
        head=element;
        cache_size+=element_size;
        pthread_mutex_unlock(&lock);
        return 1;
    }
    return 0;
}