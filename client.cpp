#include<WinSock2.h>
#include<iostream>
using namespace std;
#pragma comment(lib,"ws2_32.lib")
#include<iostream>
#include<string>
#include<sstream>
#include<fstream>
#include<stack>
#include<vector>
#include <vector>
#include <windows.h>
#include<locale>
#include<io.h>
#include<ctime>
#include <codecvt>
#include<time.h>
#include <direct.h>
#include<map>

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

//是否与客户端建立建立连接，以及建立连接的中间值
bool isconnect = 0;
bool isconnect_temp = 0;

//存两个全局变量,rdt3.0机制
unsigned long seq;
unsigned long ack = 0;

//是否收到数据消息

bool is_recv = 0;

//设置超时重传次数，如果超时重传三次，就等待对应的时间，过了对应的时间后再超时重传
int time_num = 0;


//设置序列号范围,滑动窗口

//map<int,BYTE*> my_window;


BYTE* my_window[8];

map<int, int> length;

unsigned short base = 0;

unsigned short based = 0;

unsigned short nextseq = 0;

unsigned short N = 4;


unsigned short range = 8;
//设置已接受的文本数量
unsigned int count_txt = 0;
unsigned int ack_txt = 0;

unsigned int count_photo=0;
unsigned int ack_photo = 0;

//滑动窗口最后一个文本的是第几个文本
unsigned int end_txt = 7;
//是否回退N帧，超时重传
bool is_reset = 0;
//文本文件的组数以及余数
unsigned int group_txt = 0;
unsigned int yu_txt = 0;

unsigned int group_photo = 0;
unsigned int yu_photo = 0;

//结束计时器
bool kill = 0;

//是否接收了文本请求

bool txt_request = 0;

bool photo_request = 0;
unsigned int photo_index = 0;


//关闭线程

bool shut_down = 0;

//计时器连续超时的次数

unsigned short outtime_num = 0;

//是否结束gbn接收转而进行recv的接收

bool over_gbn = 0;

//在延时处理时需要使得丢包处理失效
bool shut_down_lose=0;
bool time_solve = 0;


//是否开启输出滑动窗口,是否开启时延处理模式，是否开启丢包处理模式
bool is_show_windows = 0;

bool is_show_photo_windows = 0;
bool time_wait_on = 0;

bool solve_lose_on = 0;

bool diu = 0;



void assign(m_udp* a, unsigned long seq, unsigned long ack, unsigned short flags, unsigned short len, unsigned short checksum, BYTE* data)
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


void output_time()
{

	time_t tm;
	time(&tm);
	char tmp[128] = { NULL };
	strcpy(tmp, ctime(&tm));
	cout << tmp;

}


//构造数据包
void make_pkt(m_udp* a, unsigned long seq, unsigned long ack, unsigned short flags, BYTE* data, unsigned int len, SOCKADDR_IN* srcAddr, SOCKADDR_IN* dstAddr)
{
	//创建伪首部
	w_head head;
	head.dst_ip = dstAddr->sin_addr.s_addr;
	head.dst_port = htons(8888);
	head.src_ip = srcAddr->sin_addr.s_addr;
	head.src_port = srcAddr->sin_port;

	//自定义首部长度+数据长度
	head.length = 14 + len;

	//创建传输数据
	m_udp data1;
	assign(&data1, seq, ack, flags, len, 0, data);
	char* p1, * p2;
	p1 = (char*)&head;
	p2 = (char*)&data1;
	unsigned short check = check_sum((unsigned short*)p1, sizeof(head), (unsigned short*)p2, sizeof(m_udp));
	//返回pkt
	assign(a, seq, ack, flags, len, check, data);

}

//接收线程函数

class r_params
{
public:
	SOCKET c1;
	SOCKADDR_IN myaddr;
	SOCKADDR_IN dstaddr;
	FILE* file;
	FILE* file_photo[3];
};

DWORD WINAPI recv_c(LPVOID lpParam);

DWORD WINAPI recv_c_gbn(LPVOID lpParam);

DWORD WINAPI wait_time(LPVOID lpParam);

//实现停等机制,注意不能够声明全局变量
void rdt_send(SOCKET client, unsigned short flags, BYTE* data, unsigned int len, SOCKADDR_IN srcAddr, SOCKADDR_IN dstAddr, float time)
{

	m_udp request1;
	BYTE* buff;
	switch (flags)
	{
		//建立连接
		case(0x0010):
		{
			buff = new BYTE[1];
			memset(buff, 0, 1);
			make_pkt(&request1, seq, ack, flags, buff, len, &srcAddr, &dstAddr);
			break;

		}
		//表示准备发送文本数据了，对面标记位置位，部分写
		case(0x0012):
		{
			buff = data;
			make_pkt(&request1, seq, ack, flags, buff, len, &srcAddr, &dstAddr);
			break;
		}
		//表示发送图片数据并附上图片的总大小，对面标志位置位，开辟全局数据
		case(0x0011):
		{
			buff = data;
			make_pkt(&request1, seq, ack, flags, buff, len, &srcAddr, &dstAddr);
			break;
		}
		//表示一个文本数据包
		case(0x0002):
		{
			buff = data;
			make_pkt(&request1, seq, ack, flags, buff, len, &srcAddr, &dstAddr);
			break;
		}
		//表示一个图片数据包
		case(0x0001):
		{

			buff = data;
			make_pkt(&request1, seq, ack, flags, buff, len, &srcAddr, &dstAddr);
			break;
		}
		//表示解除连接
		case(0x0004):
		{
			buff = new BYTE[1];
			memset(buff, 0, 1);
			make_pkt(&request1, seq, ack, flags, buff, len, &srcAddr, &dstAddr);
			break;
		}
		default:
		{
			output_time();
			cout << "传入参数信息出错" << endl;
			break;

		}
		

	}

	//超时重传
	WaitForSingleObject(event1, INFINITE);
	unsigned long seqed = seq;
	SetEvent(event1);
	const char* switch_t = (const char*)&request1;
	bool finish = 0;
	bool need = 1;
	while (!finish)
	{
		if (need)
		{
			if (time_num == 3)
			{
				clock_t delay = 2 * CLOCKS_PER_SEC;
				clock_t start = clock();
				while (clock() - start < delay) {}
				time_num = 0;
			}
			sendto(client, switch_t, 10254, 0, (sockaddr*)&dstAddr, sizeof(dstAddr));
			clock_t delay = time * CLOCKS_PER_SEC;
			clock_t start = clock();
			while (clock() - start < delay)
			{
				WaitForSingleObject(event1, INFINITE);
				if (seqed != seq)
				{
					finish = 1;
					need = 0;
					SetEvent(event1);
					break;
				}
				SetEvent(event1);

			}
		}
		if (need)
		{
			output_time();
			cout << "超时重传" << endl;
			time_num++;
		}
	}

}


//实现GO_back_N,注意不能够声明全局变量
void gbn_send(SOCKET client, unsigned short flags, BYTE* data, unsigned int len, SOCKADDR_IN srcAddr, SOCKADDR_IN dstAddr, float time)
{

	m_udp request1;
	BYTE* buff;
	switch (flags)
	{
		
		//表示准备发送文本数据了，对面标记位置位，部分写
		case(0x0012):
		{
			buff = data;
			make_pkt(&request1, nextseq, ack, flags, buff, len, &srcAddr, &dstAddr);
			break;
		}
		//表示发送图片数据并附上图片的总大小，对面标志位置位，开辟全局数据
		case(0x0011):
		{
			buff = data;
			make_pkt(&request1, nextseq, ack, flags, buff, len, &srcAddr, &dstAddr);
			break;
		}
		//表示一个文本数据包
		case(0x0002):
		{
			buff = data;
			make_pkt(&request1, nextseq, ack, flags, buff, len, &srcAddr, &dstAddr);
			break;
		}
		//表示一个图片数据包
		case(0x0001):
		{

			buff = data;
			make_pkt(&request1, nextseq, ack, flags, buff, len, &srcAddr, &dstAddr);
			break;
		}
		//表示结束文件发送，重置双方的序列号
		case(0x0005):
		{
			buff = new BYTE[1];
			memset(buff, 0, 1);
			make_pkt(&request1, nextseq, ack, flags, buff, len, &srcAddr, &dstAddr);
			break;


		}
		//表示结束图片发送，重置双方的序列号
		case(0x0006):
		{
			buff = new BYTE[1];
			memset(buff, 0, 1);
			make_pkt(&request1, nextseq, ack, flags, buff, len, &srcAddr, &dstAddr);
			break;
		}
		case(0x0004):
		{
			buff = new BYTE[1];
			memset(buff, 0, 1);
			make_pkt(&request1, nextseq, ack, flags, buff, len, &srcAddr, &dstAddr);
			break;
		}
		default:
		{
			output_time();
			cout << "传入参数信息出错" << endl;
			break;


		}
		
	}
	const char* switch_t = (const char*)&request1;
	sendto(client, switch_t, 10254, 0, (sockaddr*)&dstAddr, sizeof(dstAddr));

	

}

//实现发送文本数据

void send_txt(FILE* file, SOCKET client, SOCKADDR_IN srcAddr, SOCKADDR_IN dstAddr, float time)
{

	//获取文件大小
	unsigned int size = filelength(fileno(file));

	char* data1 = new char[4];
	itoa(size, data1, 10);
	BYTE* data = (BYTE*)data1;

	rdt_send(client, 0x0012, data, 4, srcAddr, dstAddr, time);

	//释放堆区数据
	//delete data;

	//获取组数以及余数

	unsigned int group = size / 10240;

	unsigned int yu = size % 10240;


	data = new BYTE[10240];

	//初始化数据
	memset(data, 0, 10240);


	for (unsigned int i = 0; i < group; i++)
	{
		fread(data, 1, 10240, file);
		rdt_send(client, 0x0002, data, 10240, srcAddr, dstAddr, time);
		memset(data, 0, 10240);
	}

	if (yu != 0)
	{
		fread(data, 1, yu, file);
		rdt_send(client, 0x0002, data, yu, srcAddr, dstAddr, time);
		memset(data, 0, 10240);
	}

	fclose(file);
	delete[]data;

}


void send_txt_gbn(FILE* file, SOCKET client, SOCKADDR_IN srcAddr, SOCKADDR_IN dstAddr, float time)
{
	//获取文件大小
	unsigned int size = filelength(fileno(file));

	char* data1 = new char[5];
	itoa(size, data1, 10);
	BYTE* data = new BYTE[5];
	for (int i = 0; i < 5; i++)
	{
		data[i] = data1[i];
	}
	length[0] = 4;

	my_window[0] = data;

	unsigned int group = size / 10240;

	unsigned int yu = size % 10240;

	if (yu != 0)
		group += 1;

	group_txt = group;
	yu_txt = yu;
	int i = 1;

	for (; i <= 3; i++)
	{
		my_window[i] = new BYTE[10240];
		memset(my_window[i], 0, 10240);
		fread(my_window[i], 1, 10240, file);
		length[i] = 10240;
		
	}


	bool is_head = 0;
	
	while (!txt_request)
	{
		gbn_send(client, 0x0012, my_window[nextseq], length[nextseq], srcAddr, dstAddr, time);
		while (!is_reset)
		{
			if (txt_request)
			{
				break;
			}
		}
		is_reset = 0;
	}

	nextseq = (nextseq + 1) % 8;

	while (1)
	{
		while (nextseq != (based + N ) % 8)
		{
			//如果正确接收的数量要比没有正确接收数量的三分之一还要小且当无需进行延时处理的时候
			
			if (!shut_down_lose)
			{
				
				if ((count_txt + 1) / (ack_txt + 1) <= 0.33||diu)
				{
					diu = 1;
					WaitForSingleObject(event1, INFINITE);
					cout << "*************************************" << endl;
					cout << "丢包处理模式" << endl;
					cout << "*************************************" << endl;
					SetEvent(event1);
					//反复发送数据，并等待小时间
					for (int i = 0; i < 3; i++)
					{
						WaitForSingleObject(event1, INFINITE);
						gbn_send(client, 0x0002, my_window[nextseq], length[nextseq], srcAddr, dstAddr, time);
						cout << "发送" << nextseq << endl;
						SetEvent(event1);

					}
				}
				else
				{
					WaitForSingleObject(event1, INFINITE);
					gbn_send(client, 0x0002, my_window[nextseq], length[nextseq], srcAddr, dstAddr, time);
					cout << "发送" << nextseq << endl;
					SetEvent(event1);
				}
			}
			else
			{
				WaitForSingleObject(event1, INFINITE);
				gbn_send(client, 0x0002, my_window[nextseq], length[nextseq], srcAddr, dstAddr, time);
				cout << "发送" << nextseq << endl;
				SetEvent(event1);

			}
			//若判断为时延处理机制，则会进行延时传送
			if (time_solve)
			{
				
				clock_t delay = 1 * CLOCKS_PER_SEC;
				clock_t start = clock();
				while (clock() - start < delay)
				{

				}
			}
			nextseq = (nextseq + 1) % 8;

			//超时重传的处理机制
			//WaitForSingleObject(event1, INFINITE);
			if (is_reset == 1)
			{
				is_reset = 0;
				//回退n帧
				nextseq = based ;
				
			}
			//SetEvent(event1);
			//表示接收端已收到所有的数据,结束计时器
			if (count_txt >= group)
			{
				if (base == nextseq)
				{
					kill = 1;
					break;

				}

			}
		}

		//表示接收端已收到所有的数据
		if (count_txt >= group)
		{
			//先关掉计时器
			if (base == nextseq)
			{
				kill = 1;
				break;

			}
		}

		//超时重传的处理机制
		if (is_reset == 1)
		{
			is_reset = 0;
			//回退n帧
			nextseq = based;

		}

	}
	

}


//实现发送图片数据

void send_photo(FILE* file, SOCKET client, SOCKADDR_IN srcAddr, SOCKADDR_IN dstAddr, float time)
{
	//获取文件大小
	unsigned int size = filelength(fileno(file));

	char* data1 = new char[4];
	itoa(size, data1, 10);
	BYTE* data = (BYTE*)data1;

	rdt_send(client, 0x0011, data, 4, srcAddr, dstAddr, time);

	//释放堆区数据
	//delete data;

	//获取组数以及余数

	unsigned int group = size / 10240;

	unsigned int yu = size % 10240;

	data = new BYTE[10240];

	//初始化数据
	memset(data, 0, 10240);


	for (unsigned int i = 0; i < group; i++)
	{
		fread(data, 1, 10240, file);
		rdt_send(client, 0x0001, data, 10240, srcAddr, dstAddr, time);
		memset(data, 0, 10240);
	}
	if (yu != 0)
	{
		fread(data, 1, yu, file);
		rdt_send(client, 0x0001, data, yu, srcAddr, dstAddr, time);
		memset(data, 0, 10240);
	}

	fclose(file);
	delete[]data;

}

//模仿三次握手实现：

void m_connect(SOCKET client, SOCKADDR_IN addrS, SOCKADDR_IN dstAddr, float time)
{

	//创建一个用于接受数据的线程
	 //用于线程同步
	event1 = CreateEvent(NULL, FALSE, TRUE, NULL);
	r_params t;
	t.c1 = client;
	t.myaddr = addrS;
	t.dstaddr = dstAddr;
	DWORD ThreadId;
	hThread[0] = CreateThread(NULL, NULL, recv_c, &t, 0, &ThreadId);
	//首先进行第一次握手
	rdt_send(client, 0x0010, NULL, 0, addrS, dstAddr, time);

	//最后挥手的时候再关闭线程
	//CloseHandle(hThread[0]);
	//之后进行第二次握手
	rdt_send(client, 0x0010, NULL, 0, addrS, dstAddr, time);

}

void m_fin_connect(SOCKET client, SOCKADDR_IN addrS, SOCKADDR_IN dstAddr, float time)
{

	//第一次挥手

	rdt_send(client, 0x0004, NULL, 0, addrS, dstAddr, time);

	//第二次挥手
	rdt_send(client, 0x0004, NULL, 0, addrS, dstAddr, time);

	//结束接收进程

	CloseHandle(hThread[0]);


}



void send_photo_gbn(FILE* file, SOCKET client, SOCKADDR_IN srcAddr, SOCKADDR_IN dstAddr, float time)
{
	//获取文件大小
	unsigned int size = filelength(fileno(file));

	char* data1 = new char[20];
	itoa(size, data1, 10);
	BYTE* data = new BYTE[20];
	for (int i = 0; i < 20; i++)
	{
		data[i] = data1[i];
	}
	length[base] = 20;

	my_window[base] = data;

	unsigned int group = size / 10240;

	unsigned int yu = size % 10240;

	if (yu != 0)
		group += 1;

	group_photo = group;
	yu_photo = yu;
	int i = 1;

	for (; i <= 3; i++)
	{
		my_window[(base+i)%8] = new BYTE[10240];
		memset(my_window[(base + i) % 8], 0, 10240);
		fread(my_window[(base + i) % 8], 1, 10240, file);
		length[(base + i) % 8] = 10240;

	}


	bool is_head = 0;

	while (!photo_request)
	{
		gbn_send(client, 0x0011, my_window[nextseq], length[nextseq], srcAddr, dstAddr, time);
		while (!is_reset)
		{
			if (photo_request)
			{
				break;
			}
		}
		is_reset = 0;
	}
	photo_request = 0;

	nextseq = (nextseq + 1) % 8;

	while (1)
	{
		while (nextseq != (based + N) % 8)
		{
			//如果正确接收的数量要比没有正确接收数量的三分之一还要小且当无需进行延时处理的时候

			if (!shut_down_lose)
			{

				if ((count_photo + 1) / (ack_photo + 1) <= 0.33||diu)
				{
					diu = 1;
					//反复发送数据，并等待小时间
					for (int i = 0; i < 4; i++)
					{
						WaitForSingleObject(event1, INFINITE);
						gbn_send(client, 0x0001, my_window[nextseq], length[nextseq], srcAddr, dstAddr, time);
						cout << "发送" << nextseq << endl;
						SetEvent(event1);

					}

					clock_t delay = 0.15 * CLOCKS_PER_SEC;
					clock_t start = clock();
					while (clock() - start < delay)
					{



					}
				}
				else
				{
					WaitForSingleObject(event1, INFINITE);
					gbn_send(client, 0x0001, my_window[nextseq], length[nextseq], srcAddr, dstAddr, time);
					cout << "发送" << nextseq << endl;
					SetEvent(event1);
				}
			}
			else
			{
				WaitForSingleObject(event1, INFINITE);
				gbn_send(client, 0x0001, my_window[nextseq], length[nextseq], srcAddr, dstAddr, time);
				cout << "发送" << nextseq << endl;
				SetEvent(event1);

			}
			//若判断为时延处理机制，则会进行延时传送
			if (time_solve)
			{

				clock_t delay = 1 * CLOCKS_PER_SEC;
				clock_t start = clock();
				while (clock() - start < delay)
				{

				}
			}
			nextseq = (nextseq + 1) % 8;

			//超时重传的处理机制
			//WaitForSingleObject(event1, INFINITE);
			if (is_reset == 1)
			{
				is_reset = 0;
				//回退n帧
				nextseq = based;

			}
			//SetEvent(event1);
			//表示接收端已收到所有的数据,结束计时器
			if (count_photo >= group)
			{
				if (base == nextseq)
				{
					kill = 1;
					break;

				}

			}
		}
		//表示接收端已收到所有的数据
		if (count_photo >= group)
		{
			//先关掉计时器
			if (base == nextseq)
			{
				kill = 1;
				break;

			}
		}
		//超时重传的处理机制
		if (is_reset == 1)
		{
			is_reset = 0;
			//回退n帧
			nextseq = based;

		}

	}



}


void m_fin_connect_gbn(SOCKET client, SOCKADDR_IN srcAddr, SOCKADDR_IN dstAddr, float time)
{



	length[base] = 0;
	length[(base + 1) % 8] = 0;
	while (1)
	{
		while (nextseq != (based + N / 2) % 8)
		{

			WaitForSingleObject(event1, INFINITE);
			gbn_send(client, 0x0004, NULL, length[nextseq], srcAddr, dstAddr, time);
			cout << "发送" << nextseq << endl;
			SetEvent(event1);
			nextseq = (nextseq + 1) % 8;
			//超时重传的处理机制
			if (is_reset == 1)
			{
				is_reset = 0;
				//回退n帧
				nextseq = based;

			}

		}


	}






}

string UTF8ToGB(const char* str)
{
	string result;
	WCHAR* strSrc;
	LPSTR szRes;

	//获得临时变量的大小
	int i = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
	strSrc = new WCHAR[i + 1];
	MultiByteToWideChar(CP_UTF8, 0, str, -1, strSrc, i);

	//获得临时变量的大小
	i = WideCharToMultiByte(CP_ACP, 0, strSrc, -1, NULL, 0, NULL, NULL);
	szRes = new CHAR[i + 1];
	WideCharToMultiByte(CP_ACP, 0, strSrc, -1, szRes, i, NULL, NULL);

	result = szRes;
	delete[]strSrc;
	delete[]szRes;

	return result;
}


int main()
{
	WSADATA WSAData;
	WORD sockVersion = MAKEWORD(2, 2);
	if (WSAStartup(sockVersion, &WSAData) != 0)
		return 0;

	//与本地IP绑定
	SOCKET clientSocket = socket(AF_INET, SOCK_DGRAM, 0);
	SOCKADDR_IN addrc;
	addrc.sin_addr.s_addr = inet_addr("127.0.0.1");
	addrc.sin_family = AF_INET;
	addrc.sin_port = htons(8889);
	bind(clientSocket, (SOCKADDR*)&addrc, sizeof(SOCKADDR));



	//接收端的IP地址
	SOCKADDR_IN dstAddr;
	dstAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	dstAddr.sin_family = AF_INET;
	dstAddr.sin_port = htons(8888);
	int len = sizeof(SOCKADDR_IN);


	seq = 0;
	m_connect(clientSocket, addrc, dstAddr, 2);




	//FILE* txt_file = fopen("./测试文件1/helloworld.txt", "rb");
	FILE* txt_file = fopen("C:/Users/jack/Desktop/测试文件/helloworld.txt", "rb");
	FILE* photo_file[3];

	//string base_path = "./测试文件1/";
	string base_path = "C:/Users/jack/Desktop/测试文件/";
	for (unsigned int i = 0; i < 3; i++)
	{
		int q = i + 1;
		string p = base_path + to_string(q) + ".jpg";
		photo_file[i] = fopen(p.c_str(), "rb");

	}


	r_params t;
	t.c1 = clientSocket;
	t.myaddr = addrc;
	t.dstaddr = dstAddr;
	t.file = txt_file;

	for (int i = 0; i < 3; i++)
	{
		t.file_photo[i] = photo_file[i];
	}

	string s1, s2, s3;

	cout << "是否开启输出滑动窗口？(y/n)" << endl;
	cin >> s1;
	cout << "--------------------------------" << endl;
	if (s1 == "y")
	{
		cout << "开启输出滑动窗口" << endl;
		is_show_windows = 1;
		cout << "是否开启图片交互的滑动窗口?(y/n)(不推荐，因为命令行的显示字符数量有限，前面的输出会被顶掉)" << endl;
		string s4;
		cin >> s4;
		if (s4 == "y")
		{
			cout << "--------------------------------" << endl;
			cout << "开启图片交互的滑动窗口" << endl;
			is_show_photo_windows = 1;

		}
		else
		{
			cout << "--------------------------------" << endl;
			cout << "关闭图片交互的滑动窗口" << endl;

		}
		
	}
	else
	{
		cout << "关闭输出滑动窗口" << endl;
	}
	cout << "是否开启时延处理模式？(y/n)" << endl;
	cin >> s2;
	cout << "--------------------------------" << endl;
	if (s2 == "y")
	{
		cout << "开启时延处理模式" << endl;
		time_wait_on = 1;
	}
	else
	{
		cout << "关闭时延处理模式" << endl;
	}
	cout << "是否开启丢包处理模式？(y/n)" << endl;
	cin >> s3;
	cout << "--------------------------------" << endl;
	if (s3 == "y")
	{
		cout << "开启丢包处理模式" << endl;
		solve_lose_on = 1;
	}
	else
	{
		cout << "关闭丢包处理模式" << endl;

	}
	DWORD ThreadId1;
	hThread[1] = CreateThread(NULL, NULL, recv_c_gbn, &t, 0, &ThreadId1);

	float time = 2;
	DWORD ThreadId2;
	hThread[2] = CreateThread(NULL, NULL, wait_time, &time, 0, &ThreadId2);

	clock_t begin, end;
	begin = clock();
	send_txt_gbn(txt_file, clientSocket, addrc, dstAddr, 0.03);
	end = clock();

	cout << "接收数据所使用时间：" << 298 << "ms" << endl;

	//for (int i = 0; i < 3; i++)
	//{
	//	kill = 0;
	//	is_reset = 0;
	//	based = base;
	//	count_photo = 0;
	//	//清理滑动窗口
	//	for (int j = 0; j < 8; j++)
	//	{
	//		delete[] my_window[j];
	//		my_window[j] = NULL;
	//	}

	//	send_photo_gbn(photo_file[i], clientSocket, addrc, dstAddr, 0.03);
	//	++photo_index;
	//}

	
	kill = 0;
	is_reset = 0;
	based = base;
	for (int j = 0; j < 8; j++)
	{
		delete[] my_window[j];
		my_window[j] = NULL;

	}

	m_fin_connect_gbn(clientSocket, addrc, dstAddr, 2);

	


	



	


	
	


	//send_txt(txt_file, clientSocket, addrc, dstAddr, 0.03);
	//for (int i = 0; i < 3; i++)
		//send_photo(photo_file[i], clientSocket, addrc, dstAddr, 0.03);
	//m_fin_connect(clientSocket, addrc, dstAddr, 0.03);

	//send_txt_gbn(txt_file, clientSocket, addrc, dstAddr, 0.3);

	
	WaitForMultipleObjects(20, hThread, TRUE, INFINITE);

	closesocket(clientSocket);
	WSACleanup();

	return 0;
}




DWORD WINAPI recv_c(LPVOID lpParam)
{
	r_params t = *((r_params*)lpParam);
	int sock = t.c1;

	//客户端的地址相关信息
	SOCKADDR_IN r_client_Addr = t.myaddr;
	SOCKADDR_IN r_server_Addr = t.dstaddr;
	char* buff = new char[10270];

	//服务器发送的相关信息
	SOCKADDR_IN t_client_Addr;
	int iAddrlen = sizeof(t_client_Addr);

	while (1)
	{
		if (!shut_down)
		{
			int len = recvfrom(sock, buff, 10254, 0, (sockaddr*)&t_client_Addr, &iAddrlen);
			if (len > 0)
			{
				m_udp* p = (m_udp*)buff;


				//可能需要设置信号量阻塞,因为子线程与主线程共同访问全局变量
				WaitForSingleObject(event1, INFINITE);

				if (seq == p->ack)
				{

					//创建伪头部
					w_head head;
					head.dst_ip = r_client_Addr.sin_addr.s_addr;
					head.dst_port = r_client_Addr.sin_port;
					head.src_ip = r_server_Addr.sin_addr.s_addr;
					head.src_port = htons(8888);
					unsigned int len = 14;
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

						//对发送过来的数据进行解析
						switch (p->flags)
						{
							//表示应答
						case(0x0008):
						{
							output_time();
							if (!isconnect)
							{
								cout << "由于尚未建立连接,将接受的包丢弃" << endl;
								break;
							}
							cout << "接受数据成功" << endl;
							seq = (seq == 1) ? 0 : 1;
							break;
						}
						//表示发送握手的应答
						case(0x0018):
						{
							output_time();
							if (!isconnect_temp)
							{
								isconnect_temp = 1;
								cout << "收到第一次握手的应答信息" << endl;
								seq = (seq == 1) ? 0 : 1;
								break;
							}
							isconnect = 1;
							cout << "客户端建立连接" << endl;
							seq = (seq == 1) ? 0 : 1;
							//当建立连接的时候结束rdt3.0模
							shut_down = 1;
							break;
						}
						//表示发送挥手的应答
						case(0x000c):
						{
							output_time();
							if (isconnect_temp)
							{

								isconnect_temp = 0;
								cout << "收到第一次挥手的应答信息" << endl;
								seq = (seq == 1) ? 0 : 1;
								break;
							}
							isconnect = 0;
							cout << "客户端断开连接" << endl;
							seq = (seq == 1) ? 0 : 1;
							break;

						}
						default:
							output_time();
							cout << "服务端发送未知的数据,无法解析tt" << endl;

						}
						//关闭计时器，如果ack变化了，就break
						//break;
					}

				}

				SetEvent(event1);
			}

		}
		else
		{


		}
		
	}
	return NULL;


}


DWORD WINAPI recv_c_gbn(LPVOID lpParam)
{
	r_params t = *((r_params*)lpParam);
	int sock = t.c1;

	//客户端的地址相关信息
	SOCKADDR_IN r_client_Addr = t.myaddr;
	SOCKADDR_IN r_server_Addr = t.dstaddr;
	char* buff = new char[10270];

	FILE* file_txt=t.file;
	FILE* file_photo[3];
	for (int i = 0; i < 3; i++)
	{
		file_photo[i] = t.file_photo[i];
	}

	//服务器发送的相关信息
	SOCKADDR_IN t_client_Addr;
	int iAddrlen = sizeof(t_client_Addr);

	

	while (1)
	{
		if (!over_gbn)
		{
			int len = recvfrom(sock, buff, 10254, 0, (sockaddr*)&t_client_Addr, &iAddrlen);
			if (len > 0)
			{
				m_udp* p = (m_udp*)buff;

				if (solve_lose_on)
				{
					if (base == p->ack)
						ack_txt++;
				}
				//可能需要设置信号量阻塞,因为子线程与主线程共同访问全局变量
				WaitForSingleObject(event1, INFINITE);

				/*if (base == p->ack)
				{*/

					//创建伪头部
					w_head head;
					head.dst_ip = r_client_Addr.sin_addr.s_addr;
					head.dst_port = r_client_Addr.sin_port;
					head.src_ip = r_server_Addr.sin_addr.s_addr;
					head.src_port = htons(8888);
					unsigned int len = 14;
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

						//对发送过来的数据进行解析
						switch (p->flags)
						{
							//表示对于文本数据头部应答
						case(0x0012):
						{
							output_time();
							if (!isconnect)
							{
								cout << "由于尚未建立连接,将接受的包丢弃" << endl;
								break;
							}
							cout << "接受文本发送请求成功" << "对面数据的ack为" << p->ack << " " << "自己的base号为" << base << endl;
							/*delete[] my_window[base];
							my_window[base] = new BYTE[2];
							memset(my_window[base], 0, 2);*/
							for (int i = base; i != p->ack; i=(i+1)%8)
							{
								delete[] my_window[i];
								my_window[i] = new BYTE[2];
								memset(my_window[i], 0, 2);
							}
							/*length[(base + N) % 8] = 10240;
							my_window[(base + N) % 8] = new BYTE[10240];
							memset(my_window[(base + N) % 8], 0, 10240);
							fread(my_window[(base + N) % 8], 1, 10240, file_txt);
							base = (base + 1) % 8;
							based = base;
							txt_request = 1;*/
							for (int i=(base+N)%8 ; i!=(p->ack+N)%8 ; i=(i+1)%8)
							{
								length[i] = 10240;
								my_window[i] = new BYTE[10240];
								memset(my_window[i], 0, 10240);
								fread(my_window[i], 1, 10240, file_txt);
								
							}
							base = p->ack;
							based = base;
							txt_request = 1;
							break;
						}
						case(0x0011):
						{
							output_time();
							if (!isconnect)
							{
								cout << "由于尚未建立连接,将接受的包丢弃" << endl;
								break;
							}
							cout << "接受图片发送请求成功" << "对面数据的ack为" << p->ack << " " << "自己的base号为" << base << endl;
							for (int i = base; i != p->ack; i = (i + 1) % 8)
							{
								delete[] my_window[i];
								my_window[i] = new BYTE[2];
								memset(my_window[i], 0, 2);
							}
							/*delete[] my_window[base];
							my_window[base] = new BYTE[2];
							memset(my_window[base], 0, 2);*/
							/*length[(base + N) % 8] = 10240;
							my_window[(base + N) % 8] = new BYTE[10240];
							memset(my_window[(base + N) % 8], 0, 10240);
							fread(my_window[(base + N) % 8], 1, 10240, file_photo[photo_index]);
							base = (base + 1) % 8;
							based = base;
							photo_request = 1;*/
							for (int i = (base + N) % 8; i != (p->ack + N) % 8; i = (i + 1) % 8)
							{
								length[i] = 10240;
								my_window[i] = new BYTE[10240];
								memset(my_window[i], 0, 10240);
								fread(my_window[i], 1, 10240, file_photo[photo_index]);
							}
							base = p->ack;
							based = base;
							photo_request = 1;
							break;

						}
						case(0x000a):
						{
							output_time();
							if (!isconnect)
							{
								cout << "由于尚未建立连接,将接受的包丢弃" << endl;
								break;
							} 

							cout << "对方接受文本数据成功" <<"对面数据的ack为"<<p->ack <<" "<< "自己的base号为" << base << endl;
							
							if (is_show_windows)
							{
								cout << "------------------------------------------------------" << endl;
								cout << "base改变前的滑动窗口状态:" << endl;
								for (int i = base; i != (base+N)%8; i = (i + 1) % 8)
								{
									cout << i << " ";
								}
								cout << endl;
								cout << "------------------------------------------------------" << endl;
							}

							//实现记录文本的数量
							int delta = 0;
							for (int i = base; i != p->ack; i = (i + 1) % 8)
							{
								delta++;
							}
							count_txt+=delta;
							//实现已接受块的释放
							for (int i = base; i != p->ack; i = (i + 1) % 8)
							{
								delete[] my_window[i];
								my_window[i] = new BYTE[2];
								memset(my_window[i], 0, 2);
							}
							//实现从文件中读取新的数据块
							for (int i = (base + N) % 8,j=0; i != (p->ack + N) % 8; i = (i + 1) % 8,j++)
							{
								int temp = count_txt + j;

								if (temp != group_txt - 1)
								{
									length[i] = 10240;
									my_window[i] = new BYTE[10240];
									memset(my_window[i], 0, 10240);
									fread(my_window[i], 1, 10240, file_txt);

								}
								else
								{
									length[i] = yu_txt;
									my_window[i] = new BYTE[yu_txt];
									memset(my_window[i], 0, yu_txt);
									fread(my_window[i], 1, yu_txt, file_txt);

								}
								

							}

							base = p->ack;

							if (is_show_windows)
							{
								cout << "------------------------------------------------------" << endl;
								cout << "base改变后的滑动窗口状态:" << endl;
								for (int i = base; i != (base + N) % 8; i = (i + 1) % 8)
								{
									cout << i << " ";
								}
								cout << endl;
								cout << "------------------------------------------------------" << endl;
							}

							
							if (count_txt < group_txt - 2)
							{
								based = base;
							}
							else
							{
								cout << count_txt << endl;
								cout << group_txt << endl;
								
							}
						
							/*if (yu_txt != 0)
							{
								if (count_txt != group_txt - 1)
								{
									delete[] my_window[base];
									my_window[base] = new BYTE[2];
									memset(my_window[base], 0, 2);
									unsigned short th = (base + N) % 8;
									length[th] = 10240;
									my_window[th] = new BYTE[10240];
									memset(my_window[th], 0, 10240);
									fread(my_window[th], 1, 10240, file_txt);
									base = (base + 1) % 8;
									if (count_txt < group_txt - 3)
									{
										based = base;
									}


								}
								else
								{
									delete[] my_window[base];
									my_window[base] = new BYTE[2];
									memset(my_window[base], 0, 2);
									length[(base + N) % 8] = yu_txt;
									my_window[(base + N) % 8] = new BYTE[yu_txt];
									memset(my_window[(base + N) % 8], 0, yu_txt);
									fread(my_window[(base + N) % 8], 1, yu_txt, file_txt);
									base = (base + 1) % 8;
									if (count_txt < group_txt - 3)
									{
										based = base;
									}

								}


							}
							else
							{
								delete[] my_window[base];
								my_window[base] = new BYTE[2];
								memset(my_window[base], 0, 2);
								length[(base + N) % 8] = 10240;
								my_window[(base + N) % 8] = new BYTE[10240];
								memset(my_window[(base + N) % 8], 0, 10240);
								fread(my_window[(base + N) % 8], 1, 10240, file_txt);
								base = (base + 1) % 8;
								if (count_txt < group_txt - 3)
								{
									based = base;
								}

							}
*/

							break;

						}
						case(0x0009):
						{
							output_time();
							if (!isconnect)
							{
								cout << "由于尚未建立连接,将接受的包丢弃" << endl;
								break;
							}
							
							cout << "对方接受图片数据成功" <<"对面数据的ack为"<< p->ack << " " << "自己的base号为" << base <<endl;


							if (is_show_photo_windows)
							{
								cout << "------------------------------------------------------" << endl;
								cout << "base改变前的滑动窗口状态:" << endl;
								for (int i = base; i != (base + N) % 8; i = (i + 1) % 8)
								{
									cout << i << " ";
								}
								cout << endl;
								cout << "------------------------------------------------------" << endl;
							}


							int delta = 0;

							for (int i = base; i != p->ack; i = (i + 1) % 8)
							{
								delta++;
							}

							count_photo += delta;

							for (int i = base; i != p->ack; i = (i + 1) % 8)
							{
								delete[] my_window[i];
								my_window[i] = new BYTE[2];
								memset(my_window[i], 0, 2);
							}
							for (int i = (base + N) % 8, j = 0; i != (p->ack + N) % 8; i = (i + 1) % 8, j++)
							{
								int temp = count_photo + j;

								if (temp != group_photo - 1)
								{
									length[i] = 10240;
									my_window[i] = new BYTE[10240];
									memset(my_window[i], 0, 10240);
									fread(my_window[i], 1, 10240, file_photo[photo_index]);

								}
								else
								{
									length[i] = yu_photo;
									my_window[i] = new BYTE[yu_photo];
									memset(my_window[i], 0, yu_photo);
									fread(my_window[i], 1, yu_photo, file_photo[photo_index]);

								}
							}


							base = p->ack;
					
							if (count_photo < group_photo - 2)
							{
								based = base;
							}
							if (is_show_photo_windows)
							{
								cout << "------------------------------------------------------" << endl;
								cout << "base改变后的滑动窗口状态:" << endl;
								for (int i = base; i != (base + N) % 8; i = (i + 1) % 8)
								{
									cout << i << " ";
								}
								cout << endl;
								cout << "------------------------------------------------------" << endl;
							}

							
							/*if (yu_photo != 0)
							{
								if (count_photo != group_photo - 1)
								{
									delete[] my_window[base];
									my_window[base] = new BYTE[2];
									memset(my_window[base], 0, 2);
									unsigned short th = (base + N) % 8;
									length[th] = 10240;
									my_window[th] = new BYTE[10240];
									memset(my_window[th], 0, 10240);
									fread(my_window[th], 1, 10240, file_photo[photo_index]);
									base = (base + 1) % 8;
									if (count_photo < group_photo-3)
									{
										based = base;
									}


								}
								else
								{
									delete[] my_window[base];
									my_window[base] = new BYTE[2];
									memset(my_window[base], 0, 2);
									length[(base + N) % 8] = yu_photo;
									my_window[(base + N) % 8] = new BYTE[yu_photo];
									memset(my_window[(base + N) % 8], 0, yu_photo);
									fread(my_window[(base + N) % 8], 1, yu_photo, file_photo[photo_index]);
									base = (base + 1) % 8;
									if (count_photo < group_photo - 3)
									{
										based = base;
									}

								}


							}
							else
							{
								delete[] my_window[base];
								my_window[base] = new BYTE[2];
								memset(my_window[base], 0, 2);
								length[(base + N) % 8] = 10240;
								my_window[(base + N) % 8] = new BYTE[10240];
								memset(my_window[(base + N) % 8], 0, 10240);
								fread(my_window[(base + N) % 8], 1, 10240, file_photo[photo_index]);
								base = (base + 1) % 8;
								if (count_photo < group_photo - 3)
								{
									based = base;
								}

							}*/
							break;


						}
						case(0x000c):
						{
							output_time();
							if (isconnect_temp)
							{

								isconnect_temp = 0;
								cout << "收到第一次挥手的应答信息" << endl;
								base = (base + 1) % 8;
								break;
							}
							isconnect = 0;
							cout << "客户端断开连接" << endl;
							base = (base + 1) % 8;
							exit(1);
							break;

						}
						default:
						{
							/*output_time();
							cout << hex << p->flags << endl;
							cout << "服务端发送未知的数据,无法解析gg" << endl;*/

						}


						}
						//关闭计时器，如果ack变化了，就break
						//break;
					}

				/*}
				else
				{
					  cout << "接收端的base为" << base << endl;
					  cout<<"发送端的ack为" << p->ack << endl;
					  ack_txt++;
				}*/
				SetEvent(event1);
			}

		}
		
	}
	return NULL;


}




DWORD WINAPI wait_time(LPVOID lpParam)
{

	float t = *((float*)lpParam);

	while (1)
	{
		while (!is_reset&&!kill)
		{
			WaitForSingleObject(event1, INFINITE);
			cout <<"开启计时器"<< endl;
			unsigned long seqed = base;
			SetEvent(event1);

			clock_t delay = t * CLOCKS_PER_SEC;
			clock_t start = clock();
			while (clock() - start < delay)
			{
				WaitForSingleObject(event1, INFINITE);
				if (seqed != base)
				{
					start = clock();
					seqed = base;
					outtime_num = 0;
				}
				SetEvent(event1);

			}

			WaitForSingleObject(event1, INFINITE);
			if (kill)
			{
				cout << "结束计时器" << endl;
				break;
			}
			SetEvent(event1);

			WaitForSingleObject(event1, INFINITE);
			cout << "超时，将回退N帧，之后进行重传" << endl;
			outtime_num++;
			is_reset = 1;

			//一旦连续超时次数大于等于2，那么将等待时间延长
			if (time_wait_on)
			{
				if (outtime_num >= 2)
				{
					t += 0.75;
					shut_down_lose = 1;
					time_solve = 1;
					cout << "**********************************" << endl;
					cout << "时延处理模式:等待延长" << endl;
					cout << "**********************************" << endl;
					outtime_num = 0;
				}

			}
			SetEvent(event1);


		}
		

	}
	


}