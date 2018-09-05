/**
 * @Author chengbo 20180824 in shanghai
 */

#include "longfist/LFUtils.h"
#include "JournalWriter.h"
#include "PriceCenter.h"

#define PC_VALUE_LEN 30

USING_YJJ_NAMESPACE

kungfu::yijinjing::JournalWriterPtr apiWriter;
short apiSourceId;

/*
   INPUT
   ts  //time stamp
   bids // [[price,volumn][price,volumn]...]
   asks // [[price,volumn][price,volumn]...]
 */
void apiWrite(struct pc_price *pprice)
{
    LFMarketDataField data={};
    char *p, *q;
    char btmp1[PC_VALUE_LEN+1], btmp2[PC_VALUE_LEN+1], atmp1[PC_VALUE_LEN+1], atmp2[PC_VALUE_LEN+1];
    int j;

    printf("%s, %s, %s\n", pprice->ts, pprice->bids, pprice->asks);
    if(apiWriter == NULL)
    {
        return;
    }
/*
    strcpy(data.TradingDay, "20180101");
    strcpy(data.InstrumentID, "TESTINSTID");
    strcpy(data.ExchangeID, "TESTEXID");
    strcpy(data.ExchangeInstID, "TESTEXINSTID");
    data.LastPrice=0.001 ;
    data.PreSettlementPrice=0.002 ;
    data.PreClosePrice=0.003 ;
    data.PreOpenInterest=0.004 ;
    data.OpenPrice=0.005 ;
    data.HighestPrice=0.006 ;
    data.LowestPrice=0.007 ;
    data.Volume=1 ;
    data.Turnover=0.008 ;
    data.OpenInterest=0.009 ;
    data.ClosePrice=0.010 ;
    data.SettlementPrice=0.011 ;
    data.UpperLimitPrice=0.012 ;
    data.LowerLimitPrice=0.013 ;
    data.PreDelta=0.014 ;
    data.CurrDelta=0.015 ;
    strcpy(data.UpdateTime, "20180102");
*/
    data.UpdateMillisec=atol(pprice->ts);
    p = pprice->bids; q = pprice->asks;
    for(int i=1;i<=5;i++)
    {
        while(*p != '[') p++;
        p++;
        memset(btmp1, 0x00, sizeof(btmp1));
        j = 0;
        while(*p != ',') btmp1[j++] = *p++;
        p++;
        memset(btmp2, 0x00, sizeof(btmp2));
        j = 0;
        while(*p != ']') btmp2[j++] = *p++;

        while(*q != '[') q++;
        q++;
        memset(atmp1, 0x00, sizeof(atmp1));
        j = 0;
        while(*q != ',') atmp1[j++] = *q++;
        q++;
        memset(atmp2, 0x00, sizeof(atmp2));
        j = 0;
        while(*q != ']') atmp2[j++] = *q++;
        switch (i)
        {
            case 1:
                data.BidPrice1=strtod(btmp1, NULL);
                data.BidVolume1=strtod(btmp2, NULL);
                data.AskPrice1=strtod(atmp1, NULL);
                data.AskVolume1=strtod(atmp2, NULL);
                break;
            case 2:
                data.BidPrice2=strtod(btmp1, NULL);
                data.BidVolume2=strtod(btmp2, NULL);
                data.AskPrice2=strtod(atmp1, NULL);
                data.AskVolume2=strtod(atmp2, NULL);
                break;
            case 3:
                data.BidPrice3=strtod(btmp1, NULL);
                data.BidVolume3=strtod(btmp2, NULL);
                data.AskPrice3=strtod(atmp1, NULL);
                data.AskVolume3=strtod(atmp2, NULL);
                break;
            case 4:
                data.BidPrice4=strtod(btmp1, NULL);
                data.BidVolume4=strtod(btmp2, NULL);
                data.AskPrice4=strtod(atmp1, NULL);
                data.AskVolume4=strtod(atmp2, NULL);
                break;
            case 5:
                data.BidPrice5=strtod(btmp1, NULL);
                data.BidVolume5=strtod(btmp2, NULL);
                data.AskPrice5=strtod(atmp1, NULL);
                data.AskVolume5=strtod(atmp2, NULL);
                break;
            default:
                return;
         }
    }
    //apiWriter->write_frame(btmp1, strlen(btmp1), 1, MSG_TYPE_LF_MD, 1, -1);
 //       printf("%f\n", data.AskVolume5);
    apiWriter->write_frame(&data, sizeof(LFMarketDataField), apiSourceId, MSG_TYPE_LF_MD, 1/*islast*/, -1/*invalidRid*/);
    return;
}

/*
 * INPUT 
 * profile   // file path
 * OUTPUT
 * p  
 */
int readProfile(char *profile, struct pc_cfg *p)
{
    char *fn;
    FILE *fp;
    char s1[1024], s2[1024];
    char *saveptr1;
    char fieldN[50], fieldV[512];
    char *token;

    fn = profile;
    fp = fopen(fn, "r");
    if(fp == NULL){
        fprintf(stderr, "can't open file [%s]\n", fn);
        return 1;
    }
    memset(s1, 0x00, sizeof(s1));
    memset(s2, 0x00, sizeof(s2));
    memset(fieldN, 0x00, sizeof(fieldN));
    memset(fieldV, 0x00, sizeof(fieldV));
    while(1)
    {
        memset(s1, 0x00, sizeof(s1));
        memset(s2, 0x00, sizeof(s2));
        memset(fieldN, 0x00, sizeof(fieldN));
        memset(fieldV, 0x00, sizeof(fieldV));

        if(NULL == fgets(s1, sizeof(s1), fp)) break;
        if(s1[0] == '#' || s1[0] == ' ') continue;
        strcpy(s2, s1);
        token = strtok_r(s2, " ", &saveptr1);
        if(token == NULL) continue;
        strcpy(fieldN, token);
        token = strtok_r(NULL, " \r\n", &saveptr1);
        if(token == NULL) continue;
        strcpy(fieldV, token);
        if(!strcmp(fieldN, "numOfConnections")) {
            p->numOfConnections = atoi(fieldV);
        }
        else if(!strcmp(fieldN, "tps")) {
            p->tps = atoi(fieldV);
        }
        else if(!strcmp(fieldN, "url")) {
            strcpy(p->url, fieldV);
        }
        else if(!strcmp(fieldN, "platform")) {
            p->platform = atoi(fieldV);
        }
        else if(!strcmp(fieldN, "journalWriterName")){
            strcpy(p->journalWriterName, fieldV);
        }
        else if(!strcmp(fieldN, "bindIPListFileName")){
            strcpy(p->bindIPListFileName, fieldV);
        }
        else if(!strcmp(fieldN, "sendIPListFileName")){
            strcpy(p->sendIPListFileName, fieldV);
        }
        else
        {
            continue;
        }

    }
//    fprintf(stdout, "%d, %d, %d, %s, %s\n", p->numOfConnections, p->tps, p->platform, p->url, p->journalWriterName);
    return 0;
}

int main(int argc, char *argv[])
{
    struct pc_cfg cfg;

    if(argc <= 1){
        printf("Usage:%s profile\n", argv[0]);
        return 1;
    }
    memset(&cfg, 0x00, sizeof(cfg));
    if(readProfile(argv[1], &cfg)){
        printf("read profile error [%s]\n", argv[1]);
        return 1;
    }
    JournalPair l1MdPair = getMdJournalPair(cfg.platform);
    apiWriter = JournalWriter::create(l1MdPair.first, l1MdPair.second, cfg.journalWriterName);
    apiSourceId = cfg.platform;
    PCStart(&cfg, apiWrite);
}
