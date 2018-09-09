#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#include "PriceCenter.h"

#define BUFFER_SIZE 2048
#define TCP_RECV_BUF_LEN 1600
#define TCP_HEAD_LINE_LEN_MAX 256
#define TCP_HEAD_ROW_NUM_MAX 50
#define TCP_BODY_LEN_MAX 20000
#define TCP_SPLIT_LEN_LEN_MAX 5

#define MAX_CONNECTIONS 100
#define SESSION_SEND_TIMES 99   //num of requests in one session

#define MY_HTTP_DEFAULT_PORT 80
//http request format
#define HTTP_POST "POST /%s HTTP/1.1\r\nHOST: %s:%d\r\nAccept: */*\r\n Content-Type:application/x-www-form-urlencoded\r\nContent-Length: %d\r\n\r\n%s"
#define HTTP_GET_END "GET /%s HTTP/1.1\r\nHOST: %s:%d\r\nConnection: close\r\nAccept: */*\r\n\r\n"
#define HTTP_GET_KEEP "GET /%s HTTP/1.1\r\nHOST: %s:%d\r\nConnection: keep-alive\r\nAccept: */*\r\n\r\n"
#define HTTP_GET "GET /%s HTTP/1.1\r\nHOST: %s:%d\r\nAccept: */*\r\n\r\n"

struct pc_price    globalPrice;   //globalBidsTopN, globalAsksTopN, globalTs, current save latest market price for all sessions
char    cur[TS_LEN +1];

//sync update
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static int gPlatform; //help parse tcp response body
void (*pWriter)(struct pc_price *);  //output api
void (*pWriterError)(int, char*);

struct st_connectionThreadHead
{
    int     index;
    int     sendModifier;  //time point(ms) of the first request of a session 
    int     sendInterval;  //time interval(ms) between  two requests in a session
    //int     socket_fd;  //socket with platform
    struct st_tcpHandle tcpHandle;
    int     connected;  //1 if connection is created
    unsigned char    errorCode;
    char    errorDesc[256];
    unsigned char    sendExit;
    unsigned char    recvExit;
    pthread_mutex_t updateLock;  //lock when update bidsTopN asksTopN ts
    char    lpbuf[BUFFER_SIZE*4];  //request tcp content
    char    lpbufend[BUFFER_SIZE*4];  //final request tcp content
    char    sendip[16];  //des ip
    char    bindip[16];  //local bind ip
    struct pc_price price; //bidsTopN, asksTopN, ts, current latest market price for this session
};

/*http response*/
struct  st_response
{
    char    head[TCP_HEAD_ROW_NUM_MAX][TCP_HEAD_LINE_LEN_MAX + 1];
    int     rowIndex;
    int     lineIndex;
    char    body[TCP_BODY_LEN_MAX + 1];
    int     bodyIndex;
    int     chunked;
    int     contentLen;
    struct pc_price price;
    char    cur[TS_LEN +1];
};

/*http message split*/
struct  st_split
{
    char    strHexLen[TCP_SPLIT_LEN_LEN_MAX + 1];
    int     lenIndex;
    long    splitLen;
    long    splitIndex;
};

/*tcp recv buf*/
struct st_recv
{
    char    buf[TCP_RECV_BUF_LEN + 1];
    int     index;
    int     len;
};
/*for ssl support multi thread*/
/* we have this global to let the callback get easy access to it */ 
static pthread_mutex_t *lockarray;
static void lock_callback(int mode, int type, const char *file, int line)
{
    (void)file;
    (void)line;
    if (mode & CRYPTO_LOCK) {
        pthread_mutex_lock(&(lockarray[type]));
    }
    else {
        pthread_mutex_unlock(&(lockarray[type]));
    }
}
static unsigned long thread_id(void)
{
    unsigned long ret;

    ret=(unsigned long)pthread_self();
    return(ret);
}
static void init_locks(void)
{
    int i;

    lockarray=(pthread_mutex_t *)OPENSSL_malloc(CRYPTO_num_locks() *
            sizeof(pthread_mutex_t));
    for (i=0; i<CRYPTO_num_locks(); i++) {
        pthread_mutex_init(&(lockarray[i]),NULL);
    }

    CRYPTO_set_id_callback((unsigned long (*)())thread_id);
    CRYPTO_set_locking_callback((void (*)(int, int, const char*, int))lock_callback);
}
static void kill_locks(void)
{
    int i;

    CRYPTO_set_locking_callback(NULL);
    for (i=0; i<CRYPTO_num_locks(); i++)
        pthread_mutex_destroy(&(lockarray[i]));

    OPENSSL_free(lockarray);
}
/*
 *adapt send time point
 */
int shiftTS(int sendModifier, int sendInterval)
{
    int delay;
    struct timeval current;

    memset(&current, 0x00, sizeof(current));
    gettimeofday(&current, NULL);
    delay = (sendInterval*1000 - (current.tv_usec - sendModifier*1000) % (sendInterval*1000)) % (sendInterval*1000);
    if((current.tv_usec + delay) >= 1000000) delay = 1000000 - current.tv_usec + sendModifier*1000;
    usleep(delay);

    return 0;
}

void * sendThread(void *args)
{
    int p;
    struct st_connectionThreadHead *ptHead;

    unsigned char  errorCode = 0;
    char errorDesc[256] = "";

    ptHead = (struct st_connectionThreadHead *)args;
    //pthread_mutex_lock(&ptHead->connectedUpdateLock);
    //connected = ptHead->connected;
    //pthread_mutex_unlock(&ptHead->connectedUpdateLock);
    //if(!connected) return NULL;
    for(p=0;p<SESSION_SEND_TIMES;p++)
    {
        if(ptHead->errorCode) 
            goto normalend;
        shiftTS(ptHead->sendModifier, ptHead->sendInterval);
        if(tcpSendN(&(ptHead->tcpHandle), ptHead->lpbuf, strlen(ptHead->lpbuf)) < 0){
            errorCode = 1; snprintf(errorDesc, sizeof(errorDesc) -1, "tcpSendN failed..[%s]\n", ptHead->lpbuf);
            goto exceptionend;
        }
    }
    if(tcpSendN(&(ptHead->tcpHandle), ptHead->lpbufend, strlen(ptHead->lpbufend)) < 0){
        errorCode = 1; snprintf(errorDesc, sizeof(errorDesc) -1, "tcpSendN failed..[%s]\n", ptHead->lpbufend);
        goto exceptionend;
    }
    goto normalend;

exceptionend:
    pthread_mutex_lock(&ptHead->updateLock);
    ptHead->errorCode = errorCode;
    strcpy(ptHead->errorDesc, errorDesc);
    pthread_mutex_unlock(&ptHead->updateLock);
    perror(errorDesc);

normalend:
    ptHead->sendExit = 1;
    return NULL;
}


void printResponse(struct st_response *pres)
{
    return ;
}

void process(struct st_connectionThreadHead *ptHead, struct pc_price *pprice)
{

    if(priceVary(pprice, &(ptHead->price)))
    {
        pthread_mutex_lock(&lock);
        if(priceVary(pprice, &globalPrice))
        {
            memcpy(&globalPrice, pprice, sizeof(struct pc_price));
            //strcpy(cur, pres->cur);
            pthread_cond_signal(&cond);
        }
        memcpy(&(ptHead->price), pprice, sizeof(struct pc_price));
        pthread_mutex_unlock(&lock);
    }
}

void *dumpThread(void *args)
{
    struct pc_price price;
    //struct timeval current;

    while(1)
    {
        pthread_mutex_lock(&lock);
        pthread_cond_wait(&cond, &lock);
        memcpy(&price, &globalPrice, sizeof(struct pc_price));
        pthread_mutex_unlock(&lock);

/*
        memset(&current, 0x00, sizeof(current));
        gettimeofday(&current, NULL);
        printf("%ld%03ld:%s\n", current.tv_sec, current.tv_usec / 1000, cur);
*/
        pWriter(&price);
    }
}


int parseHead(struct st_response *pres)
{
    int i;
    if(strstr(pres->head[0], "200") == NULL) return -1;
    for(i = 0;i < pres->rowIndex; i++)
    {
        if(strstr(pres->head[i], "Transfer-Encoding: chunked"))
        {
            pres->chunked = 1;
            break;
        }
        if(strstr(pres->head[i], "Content-Length:"))
        {
            pres->contentLen = atoi(pres->head[i] + strlen("Content-Length:"));
            break;
        }
    }
    if(i == pres->rowIndex) return -1;
    else return 0;
}

void * recvThread(void *args)
{
    struct  st_recv     rcv;
    struct  st_response res;
    struct  st_split    sp;
    struct  pc_price    price;
    char    status; //'H':reading head, 'C':reading body

    struct timeval current;
    struct st_connectionThreadHead *ptHead;

    unsigned char errorCode = 0;
    char errorDesc[256] = "";

    ptHead = (struct st_connectionThreadHead *)args;
    memset(&res, 0x00, sizeof(res));
    memset(&rcv, 0x00, sizeof(rcv));
    memset(&sp, 0x00, sizeof(sp));
    memset(&price, 0x00, sizeof(price));
    status = 'H';
    while((rcv.len = tcpRecv(&(ptHead->tcpHandle), rcv.buf, TCP_RECV_BUF_LEN)) > 0)
    {
        if(ptHead->errorCode)
            goto normalend;
        for(rcv.index = 0; rcv.index < rcv.len; rcv.index ++)
        {
            if(status == 'H')
            {
                res.head[res.rowIndex][res.lineIndex] = rcv.buf[rcv.index];
                if(res.rowIndex == 0 && res.lineIndex == 0 && res.head[0][0] == 'H')
                {
                    memset(&current, 0x00, sizeof(current));
                    gettimeofday(&current, NULL);
                    sprintf(res.cur, "%ld%03ld", current.tv_sec, current.tv_usec / 1000);
                }
                if(res.lineIndex >= 1 && res.head[res.rowIndex][res.lineIndex - 1] == '\r' && res.head[res.rowIndex][res.lineIndex] == '\n') //line end
                {
                    if(res.lineIndex == 1) //length == 2, empty line, end of head
                    {
                        if(parseHead(&res)) 
                        {
                            errorCode = 1; snprintf(errorDesc, sizeof(errorDesc) -1, "parseHead fail.[%s]",res.head[0]);
                            goto exceptionend; //parse fail, exit thread
                        }
                        else status = 'B'; //begin read body
                    }
                    else //begin new line
                    {
                        res.rowIndex ++;
                        if(res.rowIndex == TCP_HEAD_ROW_NUM_MAX) 
                        {
                            errorCode = 1; snprintf(errorDesc, sizeof(errorDesc) -1, "head include too many rows.");
                            goto exceptionend;  //head include too many rows
                        }
                        res.lineIndex = 0;
                    }
                }
                else
                {
                    res.lineIndex ++;
                    if(res.lineIndex > TCP_HEAD_LINE_LEN_MAX) 
                    {
                        errorCode = 1; snprintf(errorDesc, sizeof(errorDesc) -1,"line too long.");
                        goto exceptionend;;  //line too long
                    }
                }
            }
            else if(status == 'B')
            {
                if(res.chunked) //read by split
                {
                    if(sp.lenIndex >= 3 && sp.strHexLen[sp.lenIndex - 2] == '\r' && sp.strHexLen[sp.lenIndex - 1] == '\n') //read split
                    {
                        if(sp.splitIndex == (sp.splitLen + 1) && rcv.buf[rcv.index] == '\n')  //split end
                        {
                            if(sp.splitLen == 0) //body end
                            {
                                if(parseBody(gPlatform, res.body, res.bodyIndex, &price))
                                {
                                    errorCode = 1; snprintf(errorDesc, sizeof(errorDesc) -1, "parseBody fail.[%s]", res.body);
                                    goto exceptionend; //parse fail, exit thread
                                }
                                //printResponse(&res);
                                process(ptHead, &price);
                                memset(&res, 0x00, sizeof(res));
                                memset(&price, 0x00, sizeof(price));
                                status = 'H';
                            }
                            memset(&sp, 0x00, sizeof(sp));
                        }
                        else if(sp.splitIndex == sp.splitLen && rcv.buf[rcv.index] == '\r') //do nothing
                        {
                            sp.splitIndex ++;
                        }
                        else if(sp.splitIndex < sp.splitLen)
                        {
                            res.body[res.bodyIndex] = rcv.buf[rcv.index];
                            res.bodyIndex ++;
                            sp.splitIndex ++;
                        }
                        else 
                        {
                            errorCode = 1; snprintf(errorDesc, sizeof(errorDesc) -1, "read split error\n");
                            goto exceptionend; //error
                        }
                    }
                    else //read split len
                    {
                        if(sp.lenIndex >= sizeof(sp.strHexLen)) 
                        {
                            errorCode = 1; snprintf(errorDesc, sizeof(errorDesc) -1, "split len len too long\n");
                            goto exceptionend; //error
                        }
                        sp.strHexLen[sp.lenIndex] = rcv.buf[rcv.index];
                        sp.lenIndex ++;
                        if(sp.lenIndex >= 3 && sp.strHexLen[sp.lenIndex - 2] == '\r' && sp.strHexLen[sp.lenIndex - 1] == '\n')
                        {
                            sp.splitLen = strtol(sp.strHexLen, NULL, 16);
                        }
                    }
                }
                else  //read by content length
                {
                    res.body[res.bodyIndex] = rcv.buf[rcv.index];
                    res.bodyIndex ++;
                    if(res.bodyIndex == res.contentLen)
                    {
                        if(parseBody(gPlatform, res.body, res.bodyIndex, &price))
                        {
                            errorCode = 1; snprintf(errorDesc, sizeof(errorDesc) -1, "parseBody fail.[%s]\n", res.body);
                            goto exceptionend; //error
                        }
                        //printResponse(&res);
                        process(ptHead, &price);
                        memset(&res, 0x00, sizeof(res));
                        memset(&price, 0x00, sizeof(price));
                        status = 'H';
                    }
                }
            }
            else ;
        }
        memset(&rcv, 0x00, sizeof(rcv));
    }

    //tcpClose(&(ptHead->tcpHandle)); 
    //pthread_mutex_lock(&ptHead->connectedUpdateLock);
    //ptHead->connected = 0;
    //pthread_mutex_unlock(&ptHead->connectedUpdateLock);
    goto normalend;

exceptionend:
    pthread_mutex_lock(&ptHead->updateLock);
    ptHead->errorCode = errorCode;
    strcpy(ptHead->errorDesc, errorDesc);
    pthread_mutex_unlock(&ptHead->updateLock);
    perror(errorDesc);

normalend:
    ptHead->recvExit = 1;
    return NULL;
}

int PCStart(struct pc_cfg *pcfg, void (*pW)(struct pc_price *), void (*pWE)(int, char*))
{
    pthread_t id;
    pthread_attr_t attr;
    int ret;
    int i, j, k;
    struct st_connectionThreadHead  connectionThreadHeads[MAX_CONNECTIONS];
    char host_addr[BUFFER_SIZE] = {'\0'};
    int port = 0;
    char sendiplist[100][16] = {{'\0'}};
    char bindiplist[100][16] = {{'\0'}}; 
    struct hostent *he;
    char file[BUFFER_SIZE] = {'\0'};
    FILE *fp;
    int connected;
    int https;
    int errorCode;
    char errorDesc[100]; //limit by kungfu

    int platform;
    int numOfConnections;
    int tps;
    char *url, *bindIPListFileName, *sendIPListFileName;

    url = pcfg->url;
    tps = pcfg->tps;
    numOfConnections = pcfg->numOfConnections;
    platform = pcfg->platform;
    bindIPListFileName = pcfg->bindIPListFileName;
    sendIPListFileName = pcfg->sendIPListFileName;
    pWriter = pW;
    pWriterError = pWE;

    gPlatform = platform;
    //set host sendbuf port
    if(http_parse_url(url,host_addr,file,&port, &https)){
        fprintf(stderr, "http_parse_url failed!\n");
        return 1;
    }
    //set send ip list
    fp = fopen(sendIPListFileName, "r");
    if(fp != NULL)
    {
        i = 0;
        while(fgets(sendiplist[i], sizeof(sendiplist[i]), fp) != NULL)
        {
            sendiplist[i][strlen(sendiplist[i])-1] = '\0';
            i ++;
            if(i == 100) break;
        }
        fclose(fp);
    }
    else
    {
        he = gethostbyname(host_addr);
        inet_ntop(he->h_addrtype, he->h_addr, sendiplist[0], sizeof(sendiplist[0]));
    }
    //set bind ip list
    fp = fopen(bindIPListFileName, "r");
    if(fp != NULL)
    {
        i = 0;
        while(fgets(bindiplist[i], sizeof(bindiplist[i]), fp) != NULL)
        {
            bindiplist[i][strlen(bindiplist[i])-1] = '\0';
            i ++;
            if(i == 100) break;
        }
        fclose(fp);
    }
    else
    {
        strcpy(bindiplist[0], "0.0.0.0");
    }
    //set thread attr
    ret = pthread_attr_init(&attr);
    if(ret != 0)
    {
        fprintf(stderr, "attr init error. errno = %d\n", errno);
        return 1;
    }
    ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if(ret != 0)
    {
        fprintf(stderr, "set attr error. errno = %d\n", errno);
        return 1;
    }
    //set ssl multi thread lock
    //init_locks();
    //signal(SIGPIPE, SIG_IGN);
    //connectionThreadHeads init
    j = 0;k = 0;
    for(i=0;i<numOfConnections;i++)
    {
        memset(connectionThreadHeads + i, 0x00, sizeof(struct st_connectionThreadHead));
        connectionThreadHeads[i].index = i;
        connectionThreadHeads[i].sendModifier = i * (1000 / tps);
        connectionThreadHeads[i].sendInterval = (1000 * numOfConnections) / tps;
        sprintf(connectionThreadHeads[i].lpbuf,HTTP_GET_KEEP,file,host_addr,port);
        sprintf(connectionThreadHeads[i].lpbufend,HTTP_GET_END,file,host_addr,port);
        connectionThreadHeads[i].sendExit = 1;
        connectionThreadHeads[i].recvExit = 1;
        //connectionThreadHeads[i].tcpHandle.socket_fd = -1;
        pthread_mutex_init(&connectionThreadHeads[i].updateLock, NULL);
        strcpy(connectionThreadHeads[i].sendip, sendiplist[j]);
        strcpy(connectionThreadHeads[i].bindip, bindiplist[k]);
        if(strlen(sendiplist[++j]) == 0) j = 0;
        if(strlen(bindiplist[++k]) == 0) k = 0;
    }
    ret = pthread_create(&id, &attr, dumpThread, NULL);
    if(ret != 0){
        fprintf(stderr, "create dump pthead error! errno = %d\n", errno);
        exit(-1);
    }
    //keep connection pool
    while(1)
    {
        for(i=0;i<numOfConnections;i++)
        {
            if(connectionThreadHeads[i].sendExit && connectionThreadHeads[i].recvExit) //recreate session
            {
                if(connectionThreadHeads[i].tcpHandle.socket_fd) //error session, need dump error
                {
                    printf("session disconnect! index[%d], socket_fd[%d]\n", connectionThreadHeads[i].index, connectionThreadHeads[i].tcpHandle.socket_fd);
                    tcpClose(&(connectionThreadHeads[i].tcpHandle));
                    pthread_mutex_lock(&connectionThreadHeads[i].updateLock);
                    errorCode = connectionThreadHeads[i].errorCode;
                    strncpy(errorDesc, connectionThreadHeads[i].errorDesc, sizeof(errorDesc));
                    pthread_mutex_unlock(&connectionThreadHeads[i].updateLock);
                    pWE(errorCode, errorDesc);
                }
                connectionThreadHeads[i].tcpHandle.https = https;
                tcpConnect(connectionThreadHeads[i].sendip, connectionThreadHeads[i].bindip, port, &(connectionThreadHeads[i].tcpHandle));
                if(connectionThreadHeads[i].tcpHandle.socket_fd > 0) {
                    printf("session create success! index[%d], socket_fd[%d]\n", connectionThreadHeads[i].index, connectionThreadHeads[i].tcpHandle.socket_fd);
                    connectionThreadHeads[i].errorCode = 0;
                    connectionThreadHeads[i].sendExit = 0;
                    connectionThreadHeads[i].recvExit = 0;
                    ret = pthread_create(&id, &attr, sendThread, connectionThreadHeads + i);
                    if(ret)
                    {
                        pthread_mutex_lock(&connectionThreadHeads[i].updateLock);
                        connectionThreadHeads[i].errorCode = 1;
                        strcpy(connectionThreadHeads[i].errorDesc, "create sendThread error.");
                        pthread_mutex_unlock(&connectionThreadHeads[i].updateLock);
                        connectionThreadHeads[i].sendExit = 1;

                    }
                    ret = pthread_create(&id, &attr, recvThread, connectionThreadHeads + i);
                    if(ret)
                    {
                        pthread_mutex_lock(&connectionThreadHeads[i].updateLock);
                        connectionThreadHeads[i].errorCode = 1;
                        strcpy(connectionThreadHeads[i].errorDesc, "create sendThread error.");
                        pthread_mutex_unlock(&connectionThreadHeads[i].updateLock);
                        connectionThreadHeads[i].recvExit = 1;

                    }
                }
            }
        }
        usleep(100000);
    }
    //free ssl multi thred lock
    //kill_locks();
    return 0;
}
