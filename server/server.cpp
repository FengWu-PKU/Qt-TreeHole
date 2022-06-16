#include <stdio.h>
#include <sys/types.h>         
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <malloc.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <bits/stdc++.h>

#include "server.h"

using namespace std;
using namespace THS;

// ip addr & port
#define HOST "127.0.0.1"
#define PORT 9999


// Statement
bool verify(ll uid, ll token);
void detach(int connfd);
void send_msg(State st, int connfd);
void signup(const ll uid, const char* password, int connfd);
void login(const ll uid, const char* password, int connfd);
void logout(const ll uid);

void additemtext(const char* textbuf, int uid);
void additemmulti(const char* textbuf, const char* multibuf, int uid);
void getlist(ll uid, int seqlen, int connfd);
void sendlist(vector<const Item*> il, int connfd);

void *client_thread(void *arg);


// Global data structrue
ItemList IL;
UserTable UT;


inline Datetime get_time(){

    time_t t;
    Datetime* tmp; 

	time(&t);
    tmp = localtime(&t);
	
    return *tmp;

}


//多线程 并发服务器
int main(int argc, char const *argv[])
{
    int ret;
    int sockfd;  //套接字描述符
    int connfd;
    char buf[1024];
    int fp;

    sockfd = socket(AF_INET, SOCK_STREAM, 0); //创建通信套接字ipv4协议 tcp通信
    if(sockfd == -1){
        cout<< "socket failed" <<endl;
        exit(-1);
    }
    
    //定义addr存入本机地址信息
    struct sockaddr_in addr;
    addr.sin_family =  AF_INET   ;  //协议
    addr.sin_port =  htons(PORT) ;  //端口
    addr.sin_addr.s_addr = inet_addr("0") ; //ip  0代表本机

    //绑定地址信息(sockfd + addr)
    ret = bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)); 
    if(ret == -1){
        cout<< "bind failed" <<endl;
        exit(-1);
    }
    
    listen(sockfd, 255);  //建立监听队列,并监听状态

    while(1){

        cout<< "Main thread listening on port "<<PORT<<"..."<<endl;
        connfd =  accept(sockfd, NULL, NULL); //建立连接

        // 创建子线程
        pthread_t pid;
        pthread_create(&pid, NULL, client_thread, (void *)&connfd);
        pthread_detach(pid);

    }

    close(sockfd);
    return 0;
}


// 多线程处理函数
void *client_thread(void *arg)
{

    int connfd = *(int *)arg; 
	cout<< "Establish Connection with client "<<connfd<<endl;

	ll uid = 0;
	ll token = 0;

	ssize_t ret;
	char hbuf[sizeof(RequestHeader)+4]; 

    while(1) {

		memset(hbuf, 0 ,sizeof(hbuf));
    	if((ret = read(connfd, hbuf, sizeof(hbuf))) > 0){// 返回值为实际接收的字节数，ret <= 0 表示客户端断开连接
			
			if(ret != sizeof(RequestHeader)){
				cerr<< "header size error" <<endl;
				continue;
			}

			RequestHeader* h = reinterpret_cast<RequestHeader*>(hbuf); 

			switch(h->htype){

				case SIGNUP:{

					uid = h->userID;
					char* password = h->password;
					signup(uid, password, connfd);
					break;

				}

				case LOGIN:{

					uid = h->userID;
					char* password = h->password;
					login(uid, password, connfd);
					break;

				}

				case GET:{

					uid = h->userID;
					token = h->token;
					if(!verify(uid, token)){
						cerr<< "Unknown user" <<endl;
						detach(connfd);
					}

					vector<const Item*> il = IL.getitems(h->starti, h->endi);
					sendlist(il, connfd);
					break;

				}

				case UPLOAD:{

					uid = h->userID;
					token = h->token;
					if(!verify(uid, token)){
						cerr<< "Unknown user" <<endl;
						detach(connfd);
					}

					int seqlen = h->seqlen;
					getlist(uid, seqlen, connfd);
					break;

				}


				default:{

					cout<<"Illegal header type, ignored"<<endl;

				}

			}

		}  
		else{
			logout(uid);
			detach(connfd);
		}
    }

	return NULL;
}





bool verify(ll uid, ll token){

	if(!UT.existtoken(uid))return false;

	ll real = UT.gettoken(uid);
	
	return token == real;

}


void detach(int connfd){

	close(connfd);
	cout<< "Connection "<<connfd<<" detached"<<endl;
	exit(0);

}


void send_msg(State st, int connfd){

	char msg[MAXMLEN];

	switch(st){
		case SUCCESS:
			strcpy(msg, "Success");
			break;
		case USER_EXIST:
			strcpy(msg, "User already exists");
			break;
		case USER_NOT_EXIST:
			strcpy(msg, "User not found");
			break;
		case PASSWORD_FAIL:
			strcpy(msg, "Wrong password");
			break;
		default:
			strcpy(msg, "Unknown error");
	}

	RespondHeader* h = new RespondHeader(REPLY, msg);

	ssize_t ret;
	char hbuf[sizeof(RespondHeader)+4]; 

	memset(hbuf, 0, sizeof(hbuf));
	memcpy(hbuf, h, sizeof(RequestHeader));

	if((ret = write(connfd, hbuf, sizeof(hbuf))) <= 0){
		cerr<<"send msg failed"<<endl;
	}

}


void signup(const ll uid, const char* password, int connfd){

	State res = UT.signup(uid, password);
	send_msg(res, connfd);
	
}

void login(const ll uid, const char* password, int connfd){

	State res = UT.login(uid, password);
	send_msg(res, connfd);
	
}


void logout(const ll uid){

	if(uid == 0)return;
	UT.erasetoken(uid);

}


void additemtext(const char* textbuf, int uid){

	Datetime timestamp = get_time();
	TextItem* tit = new TextItem(0, uid, timestamp, textbuf);

	Item* it = reinterpret_cast<Item*>(tit);
	IL.append(it);

}



void additemmulti(const char* textbuf, const char* multibuf, int uid){

	Datetime timestamp = get_time();
	MultiItem* mit = new MultiItem(0, uid, timestamp, textbuf, multibuf);

	Item* it = reinterpret_cast<Item*>(mit);
	IL.append(it);

}




void getlist(ll uid, int seqlen, int connfd){

	ssize_t dret;
	char dbuf[sizeof(DataHeader)+4]; 

	for(int curr = 0; curr < seqlen; curr++){

		if((dret = read(connfd, dbuf, sizeof(dbuf))) > 0){

			if(dret != sizeof(DataHeader)){
				cerr<< "header size error" <<endl;
				continue;
			}

			DataHeader* dh = reinterpret_cast<DataHeader*>(dbuf); 
			Itype itype = dh->itype;

			if(itype == TEXTITEM){

				int tlen = dh->textlen;
				char textbuf[tlen+4];

				ssize_t pret;
				memset(textbuf, 0, sizeof(textbuf));
				
				if((pret = read(connfd, textbuf, tlen)) != tlen){
					cerr<< "textitem length error" <<endl;
				}
				else{
					additemtext(textbuf, uid);
				}

			}
			else if(itype == MULTIITEM){

				int tlen = dh->textlen;
				int msize = dh->multisize;

				char textbuf[tlen+4];
				char multibuf[msize+4];

				ssize_t pret;
				memset(textbuf, 0, sizeof(textbuf));
				memset(multibuf, 0, sizeof(multibuf));

				if((pret = read(connfd, textbuf, tlen)) != tlen){
					cerr<< "textitem length error" <<endl;
				}
				else{
					if((pret = read(connfd, multibuf, msize)) == msize){
						cerr<< "multiitem length error" <<endl;
					}
					else{
						additemmulti(textbuf, multibuf, uid);
					}
				}

			}
			else{
				cerr<<"Illegal item type, ignored"<<endl;
			}

		}
		else{
			logout(uid);
			detach(connfd);
		}
		
	}
}


void sendlist(vector<const Item*> il, int connfd){

	int seql = il.size();

	RespondHeader* h = new RespondHeader(SEND, seql);

	ssize_t ret;
	char hbuf[sizeof(RespondHeader)+4]; 

	memset(hbuf, 0, sizeof(hbuf));
	memcpy(hbuf, h, sizeof(RequestHeader));

	if((ret = write(connfd, hbuf, sizeof(hbuf))) <= 0){
		cerr<<"send header failed"<<endl;
		return;
	}


	ssize_t dret;
	char dbuf[sizeof(DataHeader)+4]; 

	for(auto it: il){

		Itype itype = it->getitype();

		if(itype == TEXTITEM){
			
			const TextItem* tit = reinterpret_cast<const TextItem*>(it);
			size_t tlen = tit->gettextlen();
			DataHeader* dh = new DataHeader(TEXTITEM, tlen);

			memset(dbuf, 0, sizeof(dbuf));
			memcpy(dbuf, dh, sizeof(DataHeader));

			if((dret = write(connfd, dbuf, sizeof(dbuf))) <= 0){
				cerr<<"send dataheader failed"<<endl;
				continue;
			}

			ssize_t pret;
			const char* textbuf = tit->gettext();

			if((pret = write(connfd, textbuf, tlen)) != tlen){
				cerr<<"textitem length error"<<endl;
			}


		}
		else if(itype == MULTIITEM){

			const MultiItem* mit = reinterpret_cast<const MultiItem*>(it);
			size_t tlen = mit->gettextlen();
			size_t msize = mit->getmultisize();
			DataHeader* dh = new DataHeader(MULTIITEM, tlen, msize);

			memset(dbuf, 0, sizeof(dbuf));
			memcpy(dbuf, dh, sizeof(DataHeader));

			if((dret = write(connfd, dbuf, sizeof(dbuf))) <= 0){
				cerr<<"send dataheader failed"<<endl;
				continue;
			}

			ssize_t pret;
			const char* textbuf = mit->gettext();
			const char* multibuf = mit->getmulti();

			if((pret = write(connfd, textbuf, tlen)) != tlen){
				cerr<<"textitem length error"<<endl;
			}
			else{
				if((pret = write(connfd, multibuf, msize)) != msize){
					cerr<<"multiitem length error"<<endl;
				}
			}

		}
		else{
			cerr<<"Illegal item type, ignored"<<endl;
		}
	}

}
