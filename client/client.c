#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <getopt.h>

#include <proto.h>   // 已经写入到Makefile中，所以这里可以写<>
//#include "../include/proto.h"
#include "client.h"

/*
 *	-M	--mgroup	指定多播组
 *	-P	--port		指定接收端口
 *	-p  --player	指定播放器
 *	-H	--help		显示帮助
 */


// 客户端配置
struct client_conf_st client_conf = {\
	.rcvport = DEFAULT_RECVPORT,\
	.mgroup = DEFAULT_MGROUP,\
	.player_cmd = DEFAULT_PLAYERCMD};

static void printhelp(void)
{
	printf("-P --port	指定接收端口\n");
	printf("-M --mgroup 指定多播组\n");
	printf("-p --player 指定播放器命令行\n");
	printf("-H --help	显示帮助\n");
}


// 从buf的位置开始写入len个字节到fd文件中
static ssize_t writen(int fd, uint8_t *buf, size_t len)
{
	int pos = 0;
	int ret = 0;

	while(len > 0)
	{
		ret = write(fd, buf + pos, len);
		if(ret < 0)
		{
			if(errno == EINTR)
				continue;

			perror("write()");
			return -1;
		}
		len -= ret;
		pos += ret;
	}

	return pos;
}


int main(int argc, char **argv)
{
	int index = 0;
	int val;
	int c;
	int ret;
	int len;
	int sd;
	int pd[2];
	int chosenid;
	pid_t pid;
	struct ip_mreqn mreq;
	struct sockaddr_in laddr, serveraddr, raddr;
	socklen_t serveraddr_len, raddr_len;

	struct option argarr[] = {{"port", 1, NULL, 'P'}, {"mgroup", 1, NULL, 'M'},\
	   	{"player", 1, NULL, 'p'}, {"help", 0, NULL, 'H'}, \
		{NULL, 0, NULL, 0}};
	/*
	 *	初始化
	 *	级别：默认值，配置文件，环境变量，命令行参数（由低到高）
	 *
	 */
	while(1)
	{
		c = getopt_long(argc, argv, "P:M:p:H", argarr, &index);    // 有传参的选项后面加冒号
		if(c < 0)
			break;
		switch(c)
		{
			case 'P':
				client_conf.rcvport = optarg;
				break;
			case 'M':
				client_conf.mgroup = optarg;
				break;
			case 'p':
				client_conf.player_cmd = optarg;
				break;
			case 'H':
				printhelp();
				exit(0);
				break;
			default:
				abort();
				break;
		}
	}
	
	sd = socket(AF_INET, SOCK_DGRAM, 0); 
	if(sd < 0)
	{
		perror("socket()");
		exit(1);
	}

	// 加入多播组
	inet_pton(AF_INET, client_conf.mgroup, &mreq.imr_multiaddr);   // 多播地址
	/*if error*/
	inet_pton(AF_INET, "0.0.0.0", &mreq.imr_address);    		// IP地址
	mreq.imr_ifindex = if_nametoindex("eth0");   // 当前用到的网络设备索引号

	if(setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
	{
		perror("setsockopt()");
		exit(1);
	}
 
	val = 1;
	if(setsockopt(sd, IPPROTO_IP, IP_MULTICAST_LOOP, &val, sizeof(val)) < 0)
	{
		perror("setsockopt()");
		exit(1);
	}

	// 给socket绑定端口
	laddr.sin_family = AF_INET;
	laddr.sin_port = htons(atoi(client_conf.rcvport));
	//inet_pton(AF_INET, "0.0.0.0", &laddr.sin_addr.s_addr);
	inet_pton(AF_INET, "0.0.0.0", &laddr.sin_addr);
	
	if(bind(sd, (void*)&laddr, sizeof(laddr)) < 0)
	{
		perror("bind()");
		exit(1);
	}

	if(pipe(pd) < 0)
	{
		perror("pipe()");
		exit(1);
	}

	pid = fork();
	if(pid < 0)
	{
		perror("fork()");
		exit(1);
	}

	if(pid == 0)    // child
	{
		// 子进程：调用解码器
		close(sd);
		close(pd[1]);
		dup2(pd[0], 0);    // 解码器只能接收标准输入的内容，所以将管道读端读到的数据直接重定向到标准输入
		if(pd[0] > 0){
			close(pd[0]);
		}			
		
		execl("/bin/sh", "sh", "-c", client_conf.player_cmd, NULL);
		perror("execl()");
		exit(1);
	}

	// parent 父进程：从网络上收包，通过管道发送给子进程

	// 收节目单，先为节目单分配空间
	struct msg_list_st *msg_list;
	msg_list = malloc(MSG_LIST_MAX);
	if(msg_list == NULL)
	{
		perror("malloc()");
		exit(1);
	}

	// 循环读取，直到读到节目单为止
	serveraddr_len = sizeof(serveraddr);
	while(1)
	{
		len = recvfrom(sd, msg_list, MSG_LIST_MAX, 0, (void*)&serveraddr, &serveraddr_len);
		if(len < sizeof(struct msg_list_st))
		{
			fprintf(stderr, "message is too small.\n");
			continue;
		}
		if(msg_list->chnid != LISTCHNID)
		{
			fprintf(stderr, "chnid is not match.\n");
			continue;
		}
		break;
	}

	// 打印节目单并选择频道
	struct msg_listentry_st *pos;
	for(pos = msg_list->entry; (char*)pos < (((char*)msg_list) + len); pos = (void*)((char*)pos + ntohs(pos->len)))
	{
		printf("channel %d:%s\n", pos->chnid, pos->desc);
	}

	free(msg_list);

	puts("Please chose a channel: ");
	ret = 0;
	fflush(0);
	while(ret < 1)
	{
		ret = scanf("%d", &chosenid);
		if(ret != 1)
			exit(1);
	}
	
	// 收频道包，发送给子进程
	fprintf(stdout, "chosenid = %d\n", chosenid);

	struct msg_channel_st *msg_channel;
	msg_channel = malloc(MSG_CHANNEL_MAX);
	if(msg_channel == NULL)
	{
		perror("malloc()");
		exit(1);
	}

	raddr_len = sizeof(raddr);
	while(1)
	{		
		len = recvfrom(sd, msg_channel, MSG_CHANNEL_MAX, 0, (void*)&raddr, &raddr_len);

	    // 验证频道包发送方和节目单发送方是否一样，防止存在第三方监听我的行为伪造一些包
		if(raddr.sin_addr.s_addr != serveraddr.sin_addr.s_addr || raddr.sin_port != serveraddr.sin_port)
		{
			fprintf(stderr, "Ignore: address not match.\n");
			continue;
		}

		// 数据包大小过小，认为里面没有有效数据
		if(len < sizeof(struct msg_channel_st))
		{
			fprintf(stderr, "Ignore: message too small.\n");
			continue;
		}

		// 如果收到的数据是我选择的频道数据，则将数据发送给子进程，这里可以考虑添加缓冲区，防止子进程数据不足播放的时候断续
		if(msg_channel->chnid == chosenid)
		{
			fprintf(stdout, "accepted msg:%d recieved.\n", msg_channel->chnid);

			// 将数据传给子进程
			if(writen(pd[1], msg_channel->data, len - sizeof(chnid_t)) < 0)
				exit(1);
		}
	}
	
	free(msg_channel);
	close(sd);

	exit(0);
}
