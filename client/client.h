#ifndef CLIENT_H__
#define CLIENT_H__

#define DEFAULT_PLAYERCMD	"/usr/bin/mpg123 -  > /dev/null"

struct client_conf_st
{
	char *rcvport;    // 接收端口
	char *mgroup;     // 多播地址
	char *player_cmd;  // 命令行传参
};

extern struct client_conf_st client_conf;   // 客户端配置

#endif
