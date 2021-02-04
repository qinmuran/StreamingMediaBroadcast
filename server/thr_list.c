#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <syslog.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>

#include <proto.h>

#include "thr_list.h"
#include "server_conf.h"

static pthread_t tid_list;
static int nr_list_ent;
static struct mlib_listentry_st *list_ent;

/*
 * @brief 节目单线程函数，每秒广播一次节目单内容
 * @param p 无用
 */

static void *thr_list(void *p)
{
    int i;
    int totalsize;
    int size;
    int ret;
    struct msg_list_st *entlistp;
    struct msg_listentry_st *entryp;

    totalsize = sizeof(chnid_t);

    for(i = 0; i < nr_list_ent; ++i)
    {
        totalsize += sizeof(struct msg_listentry_st) + strlen(list_ent[i].desc);        
    }

    entlistp = malloc(totalsize);
    if(entlistp == NULL)
    {
        syslog(LOG_ERR, "malloc():%s.", strerror(errno));
        exit(1);
    }

    entlistp->chnid = LISTCHNID;
    entryp = entlistp->entry;

    for(i = 0; i < nr_list_ent; ++i)
    {
        size = sizeof(struct msg_listentry_st) + strlen(list_ent[i].desc);

        entryp->chnid = list_ent[i].chnid;
        entryp->len = htons(size);
        strcpy(entryp->desc, list_ent[i].desc);
        entryp = (void*)(((char*)entryp) + size);
    }

    while(1)  // 每秒发送一次节目单
    {
        ret = sendto(serversd, entlistp, totalsize, 0, (void*)&sndaddr, sizeof(sndaddr));
        if(ret < 0)
        {
            syslog(LOG_WARNING, "sendto(serversd, entlistp...):%s", strerror(errno));
        }
        else
        {
            syslog(LOG_DEBUG, "sendto(serversd, entlistp...):successed.");        
        }
        sleep(1);        
    }
}

/*
 * @brief 创建节目单线程，广播节目单
 * @param listp 节目单列表（数组，每个成员都是指针）
 * @param nr_ent 节目记录的条数
 * @return 成功创建返回0，失败返回非0
 */
int thr_list_create(struct mlib_listentry_st *listp, int nr_ent)
{
    int err;

    list_ent = listp;
    nr_list_ent = nr_ent;

    err = pthread_create(&tid_list, NULL, thr_list, NULL);
    if(err)
    {
        syslog(LOG_ERR, "pthread_create():%s.", strerror(err));
        return -1;        
    }
    return 0;
}

int thr_list_destroy(void)
{
    pthread_cancel(tid_list);
    pthread_join(tid_list, NULL);
    return 0;
}