#ifndef SERVER_CONF_H__
#define SERVER_CONF_H__

#define DEFAULT_MEDIADIR        "/var/media"   // 默认多媒体目录
#define DEFAUTL_IF              "eth0"         // 默认网卡

enum
{
    RUN_DAEMON = 1,
    RUN_FOREGROUND
};

struct server_conf_st
{
    char *rcvport;    // 接收端口
    char *mgroup;     // 多播组
    char *media_dir;   // 媒体库所在文件夹
    char runmode;      // 运行模式，前台或守护进程
    char *ifname;      // 网卡接口

};

extern struct server_conf_st server_conf;     // 服务器配置
extern int serversd;                // S端socket
extern struct sockaddr_in sndaddr;  // 对端地址

#endif