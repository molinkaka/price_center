/*
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <errno.h>
*/
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "PriceCenter.h"

#define MY_HTTP_DEFAULT_PORT 80
#define MY_HTTPS_DEFAULT_PORT 443

//HTTP API START
int http_parse_url(const char *url,char *host,char *file,int *port, int *https)
{
    char *ptr1,*ptr2;
    int len = 0;

    if(!url || !host || !file || !port){
        return -1;
    }
    ptr1 = (char *)url;
    if(!strncmp(ptr1,"http://",strlen("http://"))){
        ptr1 += strlen("http://");
        *https = 0;
    }else if(!strncmp(ptr1,"https://",strlen("https://"))){
        ptr1 += strlen("https://");
        *https = 1;
    }else{
        return -1;
    }
    ptr2 = strchr(ptr1,'/');
    if(ptr2){
        len = strlen(ptr1) - strlen(ptr2);
        memcpy(host,ptr1,len);
        host[len] = '\0';
        if(*(ptr2 + 1)){
            memcpy(file,ptr2 + 1,strlen(ptr2) - 1 );
            file[strlen(ptr2) - 1] = '\0';
        }
    }else{
        memcpy(host,ptr1,strlen(ptr1));
        host[strlen(ptr1)] = '\0';
    }
    //get host and ip
    ptr1 = strchr(host,':');
    if(ptr1){
        *ptr1++ = '\0';
        *port = atoi(ptr1);
    }else if(!*https){
        *port = MY_HTTP_DEFAULT_PORT;
    }else if(*https){
        *port = MY_HTTPS_DEFAULT_PORT;
    }else 
        return -1;

    return 0;
}

int http_tcpclient_create(const char *sendip, const char *bindip, int port)
{
    struct sockaddr_in server_addr, client_addr; 
    int socket_fd;

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(sendip);

    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = inet_addr(bindip);
    client_addr.sin_port = htons(0);

    if((socket_fd = socket(AF_INET,SOCK_STREAM,0))==-1){
        return -1;
    }
    int ret;
    if((ret = bind(socket_fd, (struct sockaddr*)&client_addr, sizeof(struct sockaddr))) < 0)
    {
        fprintf(stderr, "bind error, ret %d errno %d error %s\n", ret, errno, strerror(errno));
        return -1;
    }

    if(connect(socket_fd, (struct sockaddr *)&server_addr,sizeof(struct sockaddr)) == -1){
        fprintf(stderr, "connect error\n");
        return -1;
    }

    return socket_fd;
}


void http_tcpclient_close(int socket){
    close(socket);
}

//HTTP API END

//HTTP & HTTPS START
int tcpConnect(const char *sendip, const char *bindip, int port, struct st_tcpHandle *pH)
{
    if((pH->socket_fd = http_tcpclient_create(sendip, bindip, port)) < 0) return -1;
    if(pH->https)
    {
        // Establish a connection using an SSL layer
        pH->sslContext = NULL;
        pH->sslHandle = NULL;
        // Register the error strings for libcrypto & libssl
        SSL_load_error_strings ();
        // Register the available ciphers and digests
        SSL_library_init ();
        OpenSSL_add_all_algorithms();
        // New context saying we are a client, and using SSL 2 or 3
        pH->sslContext = SSL_CTX_new(SSLv23_client_method());
        if (pH->sslContext == NULL)
        {
            ERR_print_errors_fp(stderr);
            return -1;
        }
        // Create an SSL struct for the connection
        pH->sslHandle = SSL_new(pH->sslContext);
        if (pH->sslHandle == NULL)
        {
            ERR_print_errors_fp (stderr);
            return -1;
        }
        // Connect the SSL struct to our connection
        if (!SSL_set_fd(pH->sslHandle, pH->socket_fd))
        {
            ERR_print_errors_fp(stderr);
            return -1;
        }
        // Initiate SSL handshake
        if (SSL_connect(pH->sslHandle) != 1)
        {
            ERR_print_errors_fp(stderr);
            return -1;
        }
    }
    return 0;
}

void tcpClose(struct st_tcpHandle *pH)
{
    close(pH->socket_fd);
    if(pH->https)
    {
        SSL_shutdown(pH->sslHandle);
        SSL_free(pH->sslHandle);
        SSL_CTX_free(pH->sslContext);
    }
}

int tcpRecv(struct st_tcpHandle *pH, char *buf, int size)
{
    if(pH->https)
    {
        return SSL_read(pH->sslHandle, buf, size);
    }
    else
    {
        return recv(pH->socket_fd, buf, size, 0);
    }
}

int tcpSend(struct st_tcpHandle *pH, char *buf, int size)
{
    if(pH->https)
    {
        return SSL_write(pH->sslHandle, buf, size);
    }
    else
    {
        return send(pH->socket_fd, buf, size, 0);
    }
}

//send n bytes as possible
int tcpSendN(struct st_tcpHandle *pH, char *buf,int size)
{
    int sent=0,tmpres=0;

    while(sent < size){
        tmpres = tcpSend(pH, buf + sent, size - sent);
        if(tmpres == -1){
            return -1;
        }
        sent += tmpres;
    }
    return sent;
}
//HTTP & HTTPS END
