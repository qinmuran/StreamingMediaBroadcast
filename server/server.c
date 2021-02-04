#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#include <proto.h>

#include "server_conf.h"
#include "thr_channel.h"
#include "medialib.h"
#include "thr_list.h"

/*
 *  -M      指定多播组
 *  -P      指定接收端口
 *  -F      前台运行
 *  -D      指定媒体库位置
 *  -I      指定网络设备
 *  -H      显示帮助
 */

struct server_conf_st server_conf = {.rcvport = DEFAULT_RECVPORT,\
                                    .mgroup = DEFAULT_MGROUP,\
                                    .media_dir = DEFAULT_MEDIADIR,\
                                    .runmode = RUN_DAEMON,\
                                    .ifname = DEFAUTL_IF};

int serversd;   // S端socket，要在别的文件中用到，不能使用static局部化
struct sockaddr_in sndaddr;  // 对端地址

static struct mlib_listentry_st *list;

static void printfhelp(void)
{
    printf("-M      指定多播组\n");
    printf("-P      指定接收端口\n");
    printf("-F      前台运行\n");
    printf("-D      指定媒体库位置\n");
    printf("-I      指定网络设备\n");
    printf("-H      显示帮助\n");
}


static void daemon_exit(int s)
{
    thr_list_destroy();
    thr_channel_destroyall();
    mlib_freechnlist(list);

    syslog(LOG_WARNING, "signal-%d caught, exit now.", s);
    closelog();
    exit(0);
}

static int daemonize(void)
{
    int fd;
    pid_t pid;
    pid = fork();
    if(pid < 0)
    {
        //perror("fork()");
        syslog(LOG_ERR, "fork(): %s", strerror(errno));
        return -1;
    }
    if(pid > 0)   // parent
    {
        exit(0);
    }

    fd = open("/dev/null", O_RDWR);
    if(fd < 0)
    {
        //perror("open()");        
        syslog(LOG_WARNING, "open():%s", strerror(errno));
        return -2;
    }
    else
    {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        
        if(fd > 2)
        {
            close(fd);
        }
    }
    setsid();

    chdir("/");
    umask(0);

    return 0;
}

static int socket_init(void)
{
    struct ip_mreqn mreq;

    serversd = socket(AF_INET, SOCK_DGRAM, 0);
    if(serversd < 0)
    {
        syslog(LOG_ERR, "socket():%s", strerror(errno));
        exit(1);
    }

    // 将该设备设置为多播套接字端口
    inet_pton(AF_INET, server_conf.mgroup, &mreq.imr_multiaddr);
    inet_pton(AF_INET, "0.0.0.0", &mreq.imr_address);    // 0.0.0.0表示any address，能够匹配当前任意IP地址
    mreq.imr_ifindex = if_nametoindex(server_conf.ifname);

    if(setsockopt(serversd, IPPROTO_IP, IP_MULTICAST_IF, &mreq, sizeof(mreq)) < 0)
    {
        syslog(LOG_ERR, "setsockopt(IP_MULTICAST_IF):%s", strerror(errno));
        exit(1);
    }

    sndaddr.sin_family = AF_INET;
    sndaddr.sin_port = htons(atoi(server_conf.rcvport));
    inet_pton(AF_INET, server_conf.mgroup, &sndaddr.sin_addr.s_addr);

    //bind();
    return 0;
}



int main(int argc, char *argv[])
{
    int c;
    struct sigaction sa;

    sa.sa_handler = daemon_exit;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGINT);
    sigaddset(&sa.sa_mask, SIGQUIT);
    sigaddset(&sa.sa_mask, SIGTERM);

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);

    // 开启系统日志
    openlog("netradio", LOG_PID | LOG_PERROR, LOG_DAEMON);

    /*命令行分析*/
    while(1)
    {
        c = getopt(argc, argv, "M:P:FD:I:H");
        if(c < 0)
            break;

        switch(c)
        {
            case 'M':   // multicast group
                server_conf.mgroup = optarg;
                break;
            case 'P':   // port
                server_conf.rcvport = optarg;
                break;
            case 'F':   // foreground or daemon
                server_conf.runmode = RUN_FOREGROUND;
                break;
            case 'D':   // media_fir
                server_conf.media_dir = optarg;
                break;
            case 'I':   // internet interface
                server_conf.ifname = optarg;
                break;
            case 'H':   // help
                printfhelp();
                exit(0);
                break;
            default:
                abort();
                break;
        }
    }
    

    /*守护进程的实现*/
    if(server_conf.runmode == RUN_DAEMON)
    {
        if(daemonize() != 0)
        {
            exit(1);
        }
    }
    else if(server_conf.runmode == RUN_FOREGROUND)
    {
        /*do nothing*/
    }
    else
    {
        //fprintf(stderr, "EINVAL\n");
        syslog(LOG_ERR, "EINVAL server_conf.runmode.");
        exit(1);
    }
    

    /*socket初始化*/
    socket_init();    

    /*获取频道信息*/
    int list_size, err;
    err = mlib_getchnlist(&list, &list_size);
    if(err)
    {
        syslog(LOG_ERR, "mlib_getchnlist():%s.", strerror(err));
        exit(1);
    }

    /*创建节目单线程*/
    err = thr_list_create(list, list_size);
    if(err)
    {
        exit(1);
    }

    /*创建频道线程*/
    int i;
    fprintf(stdout, "list_size: %d\n", list_size);
    for(i = 0; i < list_size; ++i)
    {
        err = thr_channel_create(list + i);   // 每个频道创建一个线程进行广播
        if(err)
        {
            fprintf(stderr, "thr_channel_create():%s.\n", strerror(err));
            exit(1);
        }
    }

    syslog(LOG_DEBUG, "%d channel threads created.", i);

    while(1)
    {
        pause();
    }

    exit(0);    
}