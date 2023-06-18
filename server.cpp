#include<WinSock2.h>
#include<iostream>
#include<string>
#include<map>
#include<queue>
#include <direct.h>
using namespace std;
#pragma comment(lib,"ws2_32.lib")

#pragma pack(1)
class w_head
{
public:

	unsigned long dst_ip;
	unsigned short dst_port;
	unsigned long src_ip;
	unsigned short src_port;
	unsigned int length;
};

class m_udp {
public:
	unsigned long seq;
	unsigned long ack;
	//syn,ack,fin,txt,photo
	unsigned short flags;

	//数据部分的长度
	unsigned short len;
	//校验和声明为32位
	unsigned short checksum;
	//每个数据包的最大数据长度为10kb
	BYTE data[10240];

};
#pragma pack()

//事件用于线程同步
HANDLE event1;

//线程数组
HANDLE hThread[20];

//存两个全局变量
unsigned long seq = 0;
unsigned long ack = 0;
unsigned long acked = 0;

//是否建立连接
bool connect_temp = 0;
bool isconnect = 0;
bool shut_down = 0;

//存储文本文件大小
unsigned int txt_len=0;
unsigned int photo_len = 0;
unsigned int photo_group = 0;
unsigned int photo_yu = 0;
//FILE全局变量
FILE* txt_file;
FILE* photo_file[3];

unsigned int index = -1;

//接收端缓冲区

queue<BYTE*> list;


//表示是否为文本数据，或是第几个图片数据

bool txt = 0;
unsigned short photo_index = 0;

//处理接收端的缓冲
DWORD WINAPI solve_queue(LPVOID lpParam);

//是否结束建立连接
bool shut_down_connect = 0;


int i[3] = {1,1,1};

//是否退出gbn
bool over_gbn = 0;

//累计确认计时器

DWORD WINAPI wait_time(LPVOID lpParam);

//bool 全局变量是否重启，全局变量是否kill,是否开启了计时器线程

bool is_reset = 0;
bool kill = 1;
bool is_begin = 0;

//全局变量累计数量

unsigned short N=0;

//是否发送ack

bool send_ack = 0;

//超时重传模式

unsigned int mode;

//时延处理全局变量

unsigned short tn = 0;


void assign(m_udp* a, unsigned long seq, unsigned long ack, unsigned short flags,unsigned short len, unsigned short checksum ,BYTE* data)
{
	a->seq = seq;
	a->ack = ack;
	a->flags = flags;
	a->len = len;
	a->checksum = checksum;
	memset(a->data, 0, 10240);
	for (unsigned int i = 0; i < len; i++)
	{
		a->data[i] = data[i];
	}

}

//差错校验的宽度为16bit
//len表示字节数
unsigned int check_sum_temp(unsigned short* a, int len)
{
	unsigned int sum = 0x00000000;

	//16位宽度累加
	while (len > 1)
	{
		sum += *a++;
		len -= 2;
	}
	//处理1字节
	if (len == 1)
	{
		sum += *(unsigned char*)a;
	}


	return sum;
}

unsigned short check_sum(unsigned short* a, int len1, unsigned short* b, int len2)
{
	unsigned int sum = 0x00000000;
	sum = check_sum_temp(a, len1);
	sum += check_sum_temp(b, len2);


	//循环判断溢出，并将溢出加至末尾

	while (sum >> 16)
	{
		sum = (sum >> 16) + sum & 0x0000ffff;
	}

	return (unsigned short)(~sum);

}

//构造数据包
void make_pkt(m_udp* a, unsigned long seq, unsigned long ack, unsigned short flags, BYTE* data, unsigned int len, SOCKADDR_IN* srcAddr, SOCKADDR_IN* dstAddr)
{
	//创建伪首部
	w_head head;
	head.dst_ip = dstAddr->sin_addr.s_addr;
	head.dst_port = htons(8889);
	head.src_ip = srcAddr->sin_addr.s_addr;
	head.src_port = srcAddr->sin_port;

	//自定义首部长度+数据长度
	head.length = 14 + len;


	//创建传输数据
	m_udp data1;
	assign(&data1, seq, ack, flags,len,0, data);
	

	char* p1, * p2;
	p1 = (char*)&head;
	p2 = (char*)&data1;
	unsigned short check = check_sum((unsigned short*)p1, sizeof(head), (unsigned short*)p2, sizeof(m_udp));
	//返回pkt
	assign(a, seq, ack, flags,len, check, data);

}


void output_time()
{

	time_t tm;
	time(&tm);
	char tmp[128] = { NULL };
	strcpy(tmp, ctime(&tm));
	cout << tmp;

}

//接收线程函数

class r_params
{
public:
	SOCKET c1;
	SOCKADDR_IN myaddr;
};

class t_params
{
public:

	float time;
	SOCKET serSocket;
	SOCKADDR_IN serAddr; 
	SOCKADDR_IN dstAddr;




};

//rdt3.0,停等机制,在接受数据之后发送对应的应答数据
void rdt_recv(SOCKET serSocket, SOCKADDR_IN serAddr, SOCKADDR_IN dstAddr, float time)
{
	char* buff = new char[10254];
	SOCKADDR_IN clientAddr;
	int iAddrlen = sizeof(clientAddr);
	int len = recvfrom(serSocket, buff, 10254, 0, (sockaddr*)&clientAddr, &iAddrlen);
	m_udp* p = (m_udp*)buff;
	if (len > 0)
	{
		if (ack == p->seq)
		{

			//创建伪头部
			w_head head;
			head.dst_ip = serAddr.sin_addr.s_addr;
			head.dst_port = serAddr.sin_port;
			head.src_ip = dstAddr.sin_addr.s_addr;
			head.src_port = htons(8889);
			unsigned int len = 14 + p->len;
			head.length = len;

			unsigned short temp = p->checksum;
			p->checksum = 0;
			//计算校验和
			char* p1, * p2;
			p1 = (char*)&head;
			p2 = (char*)p;

			unsigned short check = check_sum((unsigned short*)p1, sizeof(head), (unsigned short*)p2, sizeof(m_udp));
			//进行校验和的检查判断
			if (check == temp)
			{
				switch (p->flags)
				{
					//建立连接
				case(0x0010):
				{
					BYTE *buff1 = new BYTE[1];
					memset(buff1, 0, 1);
					m_udp ack1;
					if (connect_temp)
					{
						output_time();
						make_pkt(&ack1, seq, ack, 0x0018, buff1, 0, &serAddr, &dstAddr);
						isconnect = 1;
						cout << "服务端建立连接" << endl;
						shut_down_connect = 1;
					
					}
					else
					{
						output_time();
						connect_temp = 1;
						cout << "收到第一次握手的请求信息" << endl;
						make_pkt(&ack1, seq, ack, 0x0018, buff1, 0, &serAddr, &dstAddr);
					}
					const char *switch_t = (const char*)&ack1;
					sendto(serSocket, switch_t, 10254, 0, (sockaddr*)&clientAddr, iAddrlen);	
					ack = (ack == 1) ? 0 : 1;
					break;

				}
				//表示准备发送文本数据了，对面标记位置位，部分写
				case(0x0012):
				{	
					output_time();
					if (!isconnect)
					{
						
						cout << "由于尚未建立连接,将接受的包丢弃" << endl;
						break;
					}
					cout << "已接受发送文本数据的请求" << endl;
					txt_len = p->len;
					BYTE* buff1 = new BYTE[1];
					memset(buff1, 0, 1);
					m_udp ack1;
					make_pkt(&ack1, seq, ack, 0x0008, buff1, 0, &serAddr, &dstAddr);
					const char* switch_t = (const char*)&ack1;
					sendto(serSocket, switch_t, 10254, 0, (sockaddr*)&clientAddr, iAddrlen);
					ack = (ack == 1) ? 0 : 1;
					break;


				}
				//表示发送图片数据并附上图片的总大小，对面标志位置位，开辟全局数据
				case(0x0011):
				{
					output_time();
					if (!isconnect)
					{
						
						cout << "由于尚未建立连接,将接受的包丢弃" << endl;
						break;
					}
					cout << "已接受发送图片数据的请求" << endl;
					photo_len = atoi((char*)p->data);
					BYTE* buff1 = new BYTE[1];
					memset(buff1, 0, 1);
					m_udp ack1;
					make_pkt(&ack1, seq, ack, 0x0008, buff1, 0, &serAddr, &dstAddr);
					const char* switch_t = (const char*)&ack1;
					sendto(serSocket, switch_t, 10254, 0, (sockaddr*)&clientAddr, iAddrlen);
					ack = (ack == 1) ? 0 : 1;
					index++;
					break;
				}
				//表示一个文本数据包
				case(0x0002):
				{
					output_time();
					if (!isconnect)
					{
						
						cout << "由于尚未建立连接,将接受的包丢弃" << endl;
						break;
					}
					list.push(p->data);
					
					//fwrite(p->data, 1, p->len, txt_file);
					//cout << "写入文本中" << endl;
					BYTE* buff1 = new BYTE[1];
					memset(buff1, 0, 1);
					m_udp ack1;
					make_pkt(&ack1, seq, ack, 0x000a, buff1, 0, &serAddr, &dstAddr);
					const char* switch_t = (const char*)&ack1;
					sendto(serSocket, switch_t, 10254, 0, (sockaddr*)&clientAddr, iAddrlen);
					ack = (ack == 1) ? 0 : 1;
					break;
				}
				//表示一个图片数据包
				case(0x0001):
				{
					output_time();
					if (!isconnect)
					{
						
						cout << "由于尚未建立连接,将接受的包丢弃" << endl;
						break;
					}

					list.push(p->data);
					//fwrite(p->data, 1, p->len, photo_file[index]);
					//cout << "写入图片中" << endl;
					BYTE* buff1 = new BYTE[1];
					memset(buff1, 0, 1);
					m_udp ack1;
					make_pkt(&ack1, seq, ack, 0x0009, buff1, 0, &serAddr, &dstAddr);
					const char* switch_t = (const char*)&ack1;
					sendto(serSocket, switch_t, 10254, 0, (sockaddr*)&clientAddr, iAddrlen);
					ack = (ack == 1) ? 0 : 1;
					break;
				}
				//表示解除连接
				case(0x0004):
				{
					BYTE* buff1 = new BYTE[1];
					memset(buff1, 0, 1);
					m_udp ack1;
					if (!connect_temp)
					{
						make_pkt(&ack1, seq, ack, 0x000c, buff1, 0, &serAddr, &dstAddr);
						isconnect = 0;
						shut_down = 1;
						output_time();
						cout << "服务端关闭连接" << endl;
					}
					else
					{
						connect_temp = 0;
						output_time();
						cout << "收到第一次挥手的请求" << endl;
						make_pkt(&ack1, seq, ack, 0x000c, buff1, 0, &serAddr, &dstAddr);
					}
					const char* switch_t = (const char*)&ack1;
					sendto(serSocket, switch_t, 10254, 0, (sockaddr*)&clientAddr, iAddrlen);
					ack = (ack == 1) ? 0 : 1;
					if (shut_down)
					{
						clock_t delay = 2 * CLOCKS_PER_SEC;
						clock_t start = clock();
						while (clock() - start < delay)
						{
		                 //等待2s之后断开连接
						}
						exit(1);
					}
					break;
				}
				default:
					output_time();
					cout << "客户端传送的数据无法解析" << endl;
					break;
				}

			}
			
		}
	   else
		{
			BYTE* buff1 = new BYTE[1];
			memset(buff1, 0, 1);
			m_udp ack1;
			make_pkt(&ack1, seq, ack, 0x0018, buff1, 0, &serAddr, &dstAddr);

		}
	}








}


void gbn_recv(SOCKET serSocket, SOCKADDR_IN serAddr, SOCKADDR_IN dstAddr, float time)
{
	char* buff = new char[10254];
	SOCKADDR_IN clientAddr;
	int iAddrlen = sizeof(clientAddr);
	int len = recvfrom(serSocket, buff, 10254, 0, (sockaddr*)&clientAddr, &iAddrlen);
	m_udp* p = (m_udp*)buff;
	if (len > 0)
	{
		
		if (ack == p->seq)
		{
			//创建伪头部
			w_head head;
			head.dst_ip = serAddr.sin_addr.s_addr;
			head.dst_port = serAddr.sin_port;
			head.src_ip = dstAddr.sin_addr.s_addr;
			head.src_port = htons(8889);
			unsigned int len = 14 + p->len;
			head.length = len;

			unsigned short temp = p->checksum;
			p->checksum = 0;
			//计算校验和
			char* p1, * p2;
			p1 = (char*)&head;
			p2 = (char*)p;
			unsigned short check = check_sum((unsigned short*)p1, sizeof(head), (unsigned short*)p2, sizeof(m_udp));
			//进行校验和的检查判断
			if (check == temp)
			{
				switch (p->flags)
				{

					//表示准备发送文本数据了，对面标记位置位，部分写
					case(0x0012):
					{
						output_time();
						if (!isconnect)
						{
							cout << "由于尚未建立连接,将接受的包丢弃" << endl;
							break;
						}
						cout << "已接受发送文本数据的请求" << endl;

						N++;
						acked = ack;
						ack = (ack + 1) % 8;

						txt = 1;
						txt_len = p->len;
						BYTE* buff1 = new BYTE[1];
						memset(buff1, 0, 1);
						m_udp ack1;
						make_pkt(&ack1, seq, ack, 0x0012, buff1, 0, &serAddr, &dstAddr);
						const char* switch_t = (const char*)&ack1;
						sendto(serSocket, switch_t, 10254, 0, (sockaddr*)&clientAddr, iAddrlen);
						
						//to_do
						if (!is_begin)
						{
							t_params hh;
							hh.serSocket = serSocket;
							hh.serAddr = serAddr;
							hh.dstAddr = dstAddr;
							hh.time = time;
							DWORD ThreadId1;
							hThread[3] = CreateThread(NULL, NULL, wait_time, &hh, 0, &ThreadId1);
							is_begin = 1;
							kill = 0;
							mode = 1;
						}
						

						/*acked = ack;
						ack = (ack + 1) % 8;*/
						break;

					}
					//表示发送图片数据并附上图片的总大小，对面标志位置位，开辟全局数据
					case(0x0011):
					{

						output_time();
						if (!isconnect)
						{

							cout << "由于尚未建立连接,将接受的包丢弃" << endl;
							break;
						}
						cout << "已接受发送图片数据的请求" << endl;
						txt = 0;
						++photo_index;
						mode = 2;
						
						photo_len = atoi((char*)p->data);
						cout << photo_len << endl;
						photo_group = photo_len / 10240;
						if (photo_len % 10240 != 0)
						{
							photo_group++;
						}

						acked = ack;
						ack = (ack + 1) % 8;
						N=0;

						photo_yu = photo_len % 10240;
						BYTE* buff1 = new BYTE[1];
						memset(buff1, 0, 1);
						m_udp ack1;
						make_pkt(&ack1, seq, ack, 0x0011, buff1, 0, &serAddr, &dstAddr);
						const char* switch_t = (const char*)&ack1;
						sendto(serSocket, switch_t, 10254, 0, (sockaddr*)&clientAddr, iAddrlen);
						/*acked = ack;
						ack = (ack + 1) % 8;*/
						index++;


						break;
					}
					//表示一个文本数据包
					case(0x0002):
					{
						output_time();
						if (!isconnect)
						{

							cout << "由于尚未建立连接,将接受的包丢弃" << endl;
							break;
						}
						list.push(p->data);
						cout << "已接受文本数据, 它的序列号为"<<p->seq << endl;
						cout << "-------------------------------------------" << endl;
						//fwrite(p->data, 1, p->len, txt_file);
						//cout << "写入文本中" << endl;
						/*BYTE* buff1 = new BYTE[1];
						memset(buff1, 0, 1);
						m_udp ack1;
						make_pkt(&ack1, seq, ack, 0x000a, buff1, 0, &serAddr, &dstAddr);
						const char* switch_t = (const char*)&ack1;
						sendto(serSocket, switch_t, 10254, 0, (sockaddr*)&clientAddr, iAddrlen);*/
						N++;
						acked = ack;
						ack = (ack + 1) % 8;
						if (N == 2)
						{
							cout << "达到累计计数,发送ack" << endl;
							is_reset = 1;
							BYTE* buff1 = new BYTE[1];
							memset(buff1, 0, 1);
							m_udp ack1;
							make_pkt(&ack1, seq, ack, 0x000a, buff1, 0, &serAddr, &dstAddr);
							const char* switch_t = (const char*)&ack1;
							sendto(serSocket, switch_t, 10254, 0, (sockaddr*)&clientAddr, iAddrlen);
							N = 0;
							tn = 0;
						}
						/*acked = ack;
						ack = (ack + 1) % 8;*/
						break;
					}
					//表示一个图片数据包
					case(0x0001):
					{
						output_time();
						if (!isconnect)
						{

							cout << "由于尚未建立连接,将接受的包丢弃" << endl;
							break;
						}

						if(i[photo_index-1] < photo_group)
							list.push(p->data);

						//fwrite(p->data, 1, p->len, photo_file[index]);
						//cout << "写入图片中" << endl;
						cout << "已接受图片数据,它的序列号为" << p->seq << endl;
						cout << "-------------------------------------------" << endl;
						N++;
						acked = ack;
						ack = (ack + 1) % 8;
						if (N == 2)
						{
						
							cout << "达到累计计数，发送ack" << endl;
							cout << "-------------------------------------------" << endl;
							is_reset = 1;
							BYTE* buff1 = new BYTE[1];
							memset(buff1, 0, 1);
							m_udp ack1;
							make_pkt(&ack1, seq, ack, 0x0009, buff1, 0, &serAddr, &dstAddr);
							const char* switch_t = (const char*)&ack1;
							sendto(serSocket, switch_t, 10254, 0, (sockaddr*)&clientAddr, iAddrlen);
							N = 0;
						}
						/*acked = ack;
						ack = (ack + 1) % 8;*/
						break;
					}
					case(0x0004):
					{
						BYTE* buff1 = new BYTE[1];
						memset(buff1, 0, 1);
						m_udp ack1;
						if (!connect_temp)
						{
							make_pkt(&ack1, seq, ack, 0x000c, buff1, 0, &serAddr, &dstAddr);
							isconnect = 0;
							shut_down = 1;
							output_time();
							cout << "服务端关闭连接" << endl;
							
						}
						else
						{
							connect_temp = 0;
							output_time();
							cout << "收到第一次挥手的请求" << endl;
							make_pkt(&ack1, seq, ack, 0x000c, buff1, 0, &serAddr, &dstAddr);
						}
						const char* switch_t = (const char*)&ack1;
						sendto(serSocket, switch_t, 10254, 0, (sockaddr*)&clientAddr, iAddrlen);
						acked = ack;
						ack = (ack + 1) % 8;
						break;
					}
					

				}

			}
			else
			{

				BYTE* buff1 = new BYTE[1];
				memset(buff1, 0, 1);
				m_udp ack1;
				make_pkt(&ack1, seq, acked, 0x0018, buff1, 0, &serAddr, &dstAddr);
				const char* switch_t = (const char*)&ack1;
				sendto(serSocket, switch_t, 10254, 0, (sockaddr*)&clientAddr, iAddrlen);

			}
		}
		else
		{
			BYTE* buff1 = new BYTE[1];
			memset(buff1, 0, 1);
			m_udp ack1;
			make_pkt(&ack1, seq, acked, 0x0018, buff1, 0, &serAddr, &dstAddr);
			const char* switch_t = (const char*)&ack1;
			sendto(serSocket, switch_t, 10254, 0, (sockaddr*)&clientAddr, iAddrlen);
		}








	}

}


int main()
{
	//初始化socket资源 
	WSADATA WSAData;
	WORD sockVersion = MAKEWORD(2, 2);
	if (WSAStartup(sockVersion, &WSAData) != 0)
		return 0;

	event1 = CreateEvent(NULL, FALSE, TRUE, NULL);
	SOCKET serSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);	//创建服务器socket
	if (INVALID_SOCKET == serSocket)
	{
		cout << "socket error!";
		return 0;
	}
	SOCKADDR_IN serAddr;
	serAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	serAddr.sin_family = AF_INET;
	serAddr.sin_port = htons(8888);
	

	SOCKADDR_IN dstAddr;
	dstAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	dstAddr.sin_family = AF_INET;
	dstAddr.sin_port = htons(8889);

	if (bind(serSocket, (sockaddr*)&serAddr, sizeof(serAddr)) == SOCKET_ERROR)	 //将socket绑定地址 
	{
		cout << "bind error";
		closesocket(serSocket);
		return 0;
	}

	SOCKADDR_IN clientAddr;
	int iAddrlen = sizeof(clientAddr);

	//txt_file=fopen("./测试文件/helloworld.txt","wb");
	//string base="./测试文件/";
	txt_file = fopen("C:/Users/jack/Desktop/测试文件1/helloworld.txt", "wb");
	string base = "C:/Users/jack/Desktop/测试文件1/";
	for (unsigned int i = 0; i < 3; i++)
	{
		int q = i + 1;
		string p = base + to_string(q) + ".jpg";
		photo_file[i] = fopen(p.c_str(), "wb");

	}
	
	while (1)
	{
		rdt_recv(serSocket, serAddr, dstAddr, 10);
		if (shut_down_connect)
			break;
	}
	ack = 0;
	
	

	DWORD ThreadId1;
	hThread[0] = CreateThread(NULL, NULL, solve_queue, NULL, 0, &ThreadId1);
	
	bool k = 1;
	while (1)
	{
		gbn_recv(serSocket, serAddr, dstAddr, 4);
		//WaitForSingleObject(event1, INFINITE);
		//cout <<"当前的ack值为"<<ack<< endl;
		//SetEvent(event1);
		if (k)
		{
			clock_t delay = 0.01 * CLOCKS_PER_SEC;
			clock_t start = clock();
			while (clock() - start < delay || is_reset)
			{
				
			}
			k = 0;

		}
		if (shut_down)
			exit(1);
	}


	for(int i=0;i<3;i++)
	fclose(photo_file[i]);
	fclose(txt_file);
	closesocket(serSocket);		//关闭socket 
	WSACleanup();

	return 0;
}



DWORD WINAPI solve_queue(LPVOID lpParam)
{

	while (1)
	{
		while (list.size() != 0)
		{
			if (txt)
			{
				BYTE* temp = list.front();
				fwrite(temp, 1, 10240, txt_file);
				list.pop();
				
			}
			else
			{
				if (photo_index == 1)
				{
					
					if (i[0] <= photo_group)
					{
						WaitForSingleObject(event1, INFINITE);
						BYTE* temp = list.front();
						if(i[0]< photo_group)
							fwrite(temp, 1, 10240, photo_file[photo_index - 1]);
						else if(i[0] == photo_group && photo_yu != 0)
							fwrite(temp,1,photo_yu, photo_file[photo_index - 1]);
						list.pop();
						SetEvent(event1);
						i[0]++;
					}
					

				}
				else if(photo_index==2)
				{
					if (i[1] <= photo_group)
					{
						WaitForSingleObject(event1, INFINITE);
						BYTE* temp = list.front();
						if (i[1] < photo_group)
							fwrite(temp, 1, 10240, photo_file[photo_index - 1]);
						else if (i[1] == photo_group && photo_yu != 0)
							fwrite(temp, 1, photo_yu, photo_file[photo_index - 1]);
						list.pop();
						SetEvent(event1);
						i[1]++;
					}

					
				}
				else if (photo_index == 3)
				{

					if (i[2] <= photo_group)
					{
						WaitForSingleObject(event1, INFINITE);
						BYTE* temp = list.front();
						if (i[2] < photo_group)
							fwrite(temp, 1, 10240, photo_file[photo_index - 1]);
						else if (i[2] == photo_group && photo_yu != 0)
							fwrite(temp, 1, photo_yu, photo_file[photo_index - 1]);
						list.pop();
						SetEvent(event1);
						i[2]++;
					}

				}
				
			}

		}
	}
	
	
	
	return NULL;

}




DWORD WINAPI wait_time(LPVOID lpParam)
{
	t_params tt = *((t_params*)lpParam);
	float t = tt.time;
	SOCKADDR_IN clientAddr;
	int iAddrlen = sizeof(clientAddr);

	while (1)
	{
		WaitForSingleObject(event1, INFINITE);
		while (!kill)
		{
			SetEvent(event1);
			cout << "开启计时器" << endl;
			clock_t delay = t * CLOCKS_PER_SEC;
			clock_t start = clock();
			while (clock() - start < delay||is_reset)
			{
			   WaitForSingleObject(event1, INFINITE);
			   if (is_reset)
			   {
				   is_reset = 0;
				   start = clock();
			   }
			   SetEvent(event1);

			   WaitForSingleObject(event1, INFINITE);
			   if (kill)
			   {
				   cout << "结束计时器中" << endl;
				   break;
			   }
			   SetEvent(event1);
			}
			
			WaitForSingleObject(event1, INFINITE);
			cout << "超时了,发送超时后的ack" << endl;
			tn++;
			if (tn == -1)
			{
				cout << "**********************************" << endl;
				cout << "时延处理模式:等待延长" << endl;
				cout << "**********************************" << endl;
				t += 0.5;

			}
			if (!is_reset && !kill)
			{
				
				BYTE* buff1 = new BYTE[1];
				memset(buff1, 0, 1);
				m_udp ack1;
				unsigned short flags=0;
				if (mode == 1)
				{
					flags = 0x000a;
				}
				else if (mode == 2)
				{
					flags = 0x0009;
				}
				make_pkt(&ack1, seq, ack, flags, buff1, 0, &tt.serAddr, &tt.dstAddr);
				const char* switch_t = (const char*)&ack1;

				sendto(tt.serSocket, switch_t, 10254, 0, (sockaddr*)&tt.dstAddr, iAddrlen);
				N = 0;
				
			}
			SetEvent(event1);

			WaitForSingleObject(event1, INFINITE);
			if (kill)
			{
				cout << "结束计时器" << endl;
				break;
			}
			SetEvent(event1);
		}
	}
}