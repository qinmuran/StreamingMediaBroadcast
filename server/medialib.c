#include <stdio.h>
#include <stdlib.h>
#include <glob.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <proto.h>

#include "medialib.h"
#include "mytbf.h"
#include "server_conf.h"

#define PATHSIZE       1024
#define LINEBUFSIZE    1024
#define MP3_BITRATE    65536

struct channel_context_st 
{
    chnid_t chnid;     // 频道ID
    char *desc;        // 当前音频描述
    glob_t mp3glob;    // mp3文件
    int pos;           // 在mp3glob.gl_pathv中的下标
    int fd;            // 指向文件的文件描述符
    off_t offset;      // 当前文件偏移量
    mytbf_t *tbf;      // 令牌桶变量，用于流控
};

static struct channel_context_st channel[MAXCHNID+1];

/*
 * @brief 将路径转换为节目记录，并创建该节目的令牌桶进行流控
 * @param path 每个通道文件的路径，path下应该有两个文件： path/desc.text 和 path/*.mp3
 * @return 按path转换成的频道记录指针
 */
static struct channel_context_st *path2entry(const char *path)
{
    // path/desc.text   path/*.mp3
    char pathstr[PATHSIZE];
    char linebuf[LINEBUFSIZE];
    FILE *fp;
    struct channel_context_st *me;
    static chnid_t curr_id = MINCHNID;

    strncpy(pathstr, path, PATHSIZE);
    strncat(pathstr, "/desc.text", PATHSIZE);

    fp = fopen(pathstr, "r");
    if(fp == NULL)
    {
        syslog(LOG_INFO, "%s is not a channel dir(Can't find desc.text)", path);
        return NULL;
    }
    if(fgets(linebuf, LINEBUFSIZE, fp) == NULL)
    {
        syslog(LOG_INFO, "%s is not a channel dir(Can't get the desc.text)", path);
        fclose(fp);
        return NULL;
    }

    fclose(fp);

    me = malloc(sizeof(*me));
    if(me == NULL)
    {
        syslog(LOG_ERR, "malloc():%s\n", strerror(errno));
        return NULL;
    }

    me->tbf = mytbf_init(MP3_BITRATE/8*5, MP3_BITRATE/8*10);
    if(me->tbf == NULL)
    {
        syslog(LOG_ERR, "mytbf_init():%s\n", strerror(errno));
        free(me);
        return NULL;
    }

    me->desc = strdup(linebuf);
    strncpy(pathstr, path, PATHSIZE);
    strncat(pathstr, "/*.mp3", PATHSIZE);
    if(glob(pathstr, 0, NULL, &me->mp3glob) != 0)
    {
        curr_id++;
        syslog(LOG_ERR, "%s is not a channel dir(Can't find mp3 files)", path);
        free(me);
        return NULL;
    }

    me->pos = 0;
    me->offset = 0;
    me->fd = open(me->mp3glob.gl_pathv[me->pos], O_RDONLY);  // fd先指向第一个.mp3文件
    if(me->fd < 0)
    {
        syslog(LOG_WARNING, "%s open failed.", me->mp3glob.gl_pathv[me->pos]);
        free(me);
        return NULL;
    }

    me->chnid = curr_id;
    curr_id++;

    return me;
}

/*
 * @brief 获取节目单信息
 * @param result 节目单（数组），传出参数，每条节目包含一个chnid（频道号）和desc（节目描述）
 * @param resnum 节目个数，传出参数
 * @return 成功返回0，失败返回非0
 */
int mlib_getchnlist(struct mlib_listentry_st **result, int *resnum)
{
    int i;
    int num = 0;   // 记录目录结构的数量
    char path[PATHSIZE];
    glob_t globres;
    struct mlib_listentry_st *ptr;   // 要回填给开发者的结构体（chnid, desc）
    struct channel_context_st *res;  // 实际每个channel的结构体

    for(i = 0; i < MAXCHNID+1; ++i)
    {
        channel[i].chnid = -1;
    }

    snprintf(path, PATHSIZE, "%s/*", server_conf.media_dir);
    if(glob(path, 0, NULL, &globres))
    {
        return -1;
    }   

    ptr = malloc(sizeof(struct mlib_listentry_st) * globres.gl_pathc);
    if(ptr == NULL)
    {
        syslog(LOG_ERR, "malloc() error.");
        exit(1);
    }

    // 每个路径对应一个频道，应该有一个desc.text文件和若干个.mp3文件
    for(i = 0; i < globres.gl_pathc; ++i)
    {
        // globres.gl_pathv[i] -> "/var/media/ch1"
        // 将路径转换为节目记录，并创建该节目的令牌桶进行流控        
        res = path2entry(globres.gl_pathv[i]);     
        if(res != NULL)
        {
            //syslog(LOG_DEBUG, "path2entry() returned :%d %s.",res->chnid, res->desc);
            syslog(LOG_DEBUG, "channel %d:", res->chnid);
            syslog(LOG_DEBUG, "dir:%s   %d mp3 file found.", globres.gl_pathv[i], res->mp3glob.gl_pathc);
            for(int j = 0; j < res->mp3glob.gl_pathc; ++j)
            {
                syslog(LOG_DEBUG, "------%d: %s.", j+1, res->mp3glob.gl_pathv[j]);
            }
            memcpy(channel+res->chnid, res, sizeof(*res));
            ptr[num].chnid = res->chnid;
            ptr[num].desc = res->desc;            
            num++;
        }        
    }

    *result = realloc(ptr, sizeof(struct mlib_listentry_st) * num);
    if(*result == NULL)
    {
        syslog(LOG_ERR, "realloc() error.");
        exit(1);
    }

    *resnum = num;

    return 0;
}

int mlib_freechnlist(struct mlib_listentry_st *ptr)
{
    free(ptr);
}

// 关闭上一个文件，打开下一个文件
static int open_next(chnid_t chnid)
{
    for(int i = 0; i < channel[chnid].mp3glob.gl_pathc; ++i)
    {
        channel[chnid].pos++;        

        // 如果播放完了最后一个，就重头开始播放
        if(channel[chnid].pos == channel[chnid].mp3glob.gl_pathc)
        {
            channel[chnid].pos = 0;
            //break;
        }    
        
         // 先关掉上一个文件
        close(channel[chnid].fd);  
        // 再打开下一个文件
        channel[chnid].fd = open(channel[chnid].mp3glob.gl_pathv[channel[chnid].pos], O_RDONLY);
        if(channel[chnid].fd < 0)
        {
            syslog(LOG_WARNING, "open(%s):%s.", channel[chnid].mp3glob.gl_pathv[channel[chnid].pos], strerror(errno));
        }
        else   // successed
        {
            syslog(LOG_DEBUG, "(channel %d) open next file: %s", chnid, channel[chnid].mp3glob.gl_pathv[channel[chnid].pos]);
            channel[chnid].offset = 0;
            return 0;
        }
    }

    syslog(LOG_ERR, "None of mp3s in channel %d is available.", chnid);
}

/*
 *  从频道chnid中读取size个字节的数据到buf中，返回实际读到的数据大小
 *  每个频道可能有多个.mp3文件，如果读取一个文件失败，就读取下一个文件
 */
ssize_t mlib_readchn(chnid_t chnid, void *buf, size_t size)
{
    int len;
    int tbfsize;

    tbfsize = mytbf_fetchtoken(channel[chnid].tbf, size);

    while(1)
    {    
        len = pread(channel[chnid].fd, buf, tbfsize, channel[chnid].offset);
        if(len < 0)    // 这首歌读取失败，没必要退出，关闭当前这首歌，继续打开下一首歌
        {
            syslog(LOG_WARNING, "media file %s pread():%s.", channel[chnid].mp3glob.gl_pathv[channel[chnid].pos], strerror(errno));
            open_next(chnid);
        }
        else if(len == 0)   // 这首歌读取结束，关闭当前这首歌，继续打开下一首歌
        {
            syslog(LOG_DEBUG, "media file %s is over.", channel[chnid].mp3glob.gl_pathv[channel[chnid].pos]);
            open_next(chnid);
        }
        else   // len > 0
        {
            syslog(LOG_DEBUG, "(channel %d) %s: %d bytes.", chnid, channel[chnid].mp3glob.gl_pathv[channel[chnid].pos], len);
            channel[chnid].offset += len;
            break;
        }
    }

    if(tbfsize - len > 0)
    {
        mytbf_returntoken(channel[chnid].tbf, tbfsize - len);
    }

    return len;    
}