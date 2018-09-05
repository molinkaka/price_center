/**
 * @Author chengbo 20180824 in shanghai
 */
#ifndef PRICECENTER_H
#define PRICECENTER_H

/*
 * profile struct
 */
struct pc_cfg {
    char url[1024];
    int tps, numOfConnections, platform;
    char journalWriterName[50];
    char bindIPListFileName[512];
    char sendIPListFileName[512];
};

/*PLATFORM API START*/
#define TOP_N 5
#define TS_LEN 50
#define ASKS_LEN 1024
#define BIDS_LEN 1024
struct pc_price {
    char ts[TS_LEN +1];
    char asks[ASKS_LEN +1];
    char bids[BIDS_LEN +1];
};
int parseBody(int platform, char *httpBody, int bodyLen, struct pc_price *pprice);
int priceVary(struct pc_price *pNew, struct pc_price *pLast);
/*PLATFORM API END*/

/*HTTP API START*/
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
struct st_tcpHandle
{
    int https; //1 = https 0 = http
    int socket_fd;
    SSL *sslHandle;
    SSL_CTX *sslContext;
};
int http_parse_url(const char *url,char *host,char *file,int *port, int *https);
int tcpConnect(const char *sendip, const char *bindip, int port, struct st_tcpHandle *pH);
void tcpClose(struct st_tcpHandle *pH);
int tcpRecv(struct st_tcpHandle *pH, char *buf, int size);
int tcpSend(struct st_tcpHandle *pH, char *buf, int size);
int tcpSendN(struct st_tcpHandle *pH, char *buf, int size);
/*HTTP API END*/


/*THREAD API START*/
int PCStart(struct pc_cfg *p, void (*pWriter)(struct pc_price *));
//must implemented in external call code
void apiWrite(struct pc_price *pprice);
/*THREAD API END*/

#endif  //PRICECENTER_H


