#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>

#include <proto.h>

#include "thr_channel.h"    
#include "server_conf.h"

struct thr_channel_ent_st
{
    chnid_t chnid;
    pthread_t tid;
};

struct thr_channel_ent_st thr_channel[CHNNR];
static int tid_nextpos = 0;


/*
 * @brief 频道线程函数，从每个频道读取数据并广播出去
 * @param ptr 当前频道信息
 */
static void *thr_channel_snder(void *ptr)
{
    int len;
    struct msg_channel_st *sbufp;
    struct mlib_listentry_st *ent = ptr;

    sbufp = malloc(MSG_CHANNEL_MAX);
    if(sbufp == NULL)
    {
        syslog(LOG_ERR, "malloc():%s.", strerror(errno));
        exit(1);
    }

    sbufp->chnid = ent->chnid;

    while(1)
    {
        // 从频道ent->chnid中读取MAX_DATA个字节的数据到sbufp->data中，返回实际读取到的数据长度len
        len = mlib_readchn(ent->chnid, sbufp->data, MAX_DATA);   

        if(sendto(serversd, sbufp, len+sizeof(chnid_t), 0, (void*)&sndaddr, sizeof(sndaddr)) < 0)
        {
            syslog(LOG_ERR, "thr_channel(%d):sendto():%s.", ent->chnid, strerror(errno));
        }
        else
        {
            syslog(LOG_DEBUG, "thr_channel(%d):sendto() successed.", ent->chnid);
        }
        
        sched_yield(); 
    }

    pthread_exit(NULL);
}

/*
 * @brief 为每个频道创建一个线程用于广播
 * @param ptr 频道记录
 * @return 创建成功返回0，失败返回非0
 */ 
int thr_channel_create(struct mlib_listentry_st *ptr)
{
    int err;

    err = pthread_create(&thr_channel[tid_nextpos].tid, NULL, thr_channel_snder, ptr);
    if(err)
    {
        syslog(LOG_WARNING, "pthread_create():%s.", strerror(err));
        return -err;
    }

    thr_channel[tid_nextpos].chnid = ptr->chnid;
    tid_nextpos++;

    return 0;
}

/*
 * @brief 停止特定频道的广播
 * @param ptr 要停止广播的频道的信息
 * @return 成功返回0，失败返回非0
 */
int thr_channel_destroy(struct mlib_listentry_st *ptr)
{    
    for(int i = 0; i < CHNNR; ++i)
    {
        if(thr_channel[i].chnid == ptr->chnid)
        {
            if(pthread_cancel(thr_channel[i].tid) < 0)
            {
                syslog(LOG_ERR, "pthread_cancel(): the thread of channel %d.", ptr->chnid);
                return -ESRCH;
            }
        }
        pthread_join(thr_channel[i].tid, NULL);
        thr_channel[i].chnid = -1;
        return 0;
    }
}


/*
 * @brief 停止所有频道的广播
 * @return 成功返回0，失败返回非0
 */
int thr_channel_destroyall(void)
{
    for(int i = 0; i < CHNNR; ++i)
    {
        if(thr_channel[i].chnid > 0)
        {
            if(pthread_cancel(thr_channel[i].tid) < 0)
            {
                syslog(LOG_ERR, "pthread_cancel(): the thread of channel %d.", thr_channel[i].chnid);
                return -ESRCH;
            }
            pthread_join(thr_channel[i].tid, NULL);
            thr_channel[i].chnid = -1;
        }
    }
    return 0;
}