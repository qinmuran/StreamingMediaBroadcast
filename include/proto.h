#ifndef PROTO_H__
#define PROTO_H__

#include <site_type.h>

#define DEFAULT_MGROUP		"224.2.2.2"   // 默认的组播地址
#define DEFAULT_RECVPORT	"1989"        // 默认的组播端口 

#define CHNNR				100           // channel数量
#define LISTCHNID			0			  // 节目单频道号
#define MINCHNID			1		      // 最小的频道ID
#define MAXCHNID			(MINCHNID + CHNNR - 1)   // 最大的频道ID

#define MSG_CHANNEL_MAX		(65536-20-8)    //通道消息最大长度（推荐包长度-IP包包头-UDP包包头）
#define MAX_DATA			MSG_CHANNEL_MAX-sizeof(chnid_t)

#define MSG_LIST_MAX		(65536-20-8)   // 节目单消息的大小
#define MAX_ENTRY			(MSG_LIST_MAX-sizeof(chnid_t))   // 节目单表项的大小
 
// channel包的数据结构
struct msg_channel_st
{
	chnid_t chnid;    // channel id, must between [MINCHNLID, MAXCHNID]
	uint8_t data[1];   // 数据，变长
}__attribute__((packed));   // UDP套接字的结构体不能考虑对齐，因为每个平台上实现不一样


// 节目单的每条记录
struct msg_listentry_st
{
	chnid_t chnid;    // channel id
	uint16_t len;     // 该结构体的大小
	uint8_t	desc[1];  // 当前节目的描述
}__attribute__((packed));


/* 节目单结构体
 * 	1 music:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
 *	2 sport:xxxxxxxxxxxxxxxxxxxxxxxx
 *  3 xxxx:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
 *  4 xx:xxxxxxxxxxxxxxxxx
 */
struct msg_list_st
{
	chnid_t chnid;    // must be LISTCHNID
	struct msg_listentry_st entry[1];   // 节目单，变长（节目单本身是变长的，其中的每个元素也是变长的）
}__attribute__((packed));

#endif
