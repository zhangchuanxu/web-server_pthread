#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> //for getopt, fork
#include <string.h> //for strcat
//for struct evkeyvalq
#include <sys/queue.h>
#include <event.h>

#include <pthread.h>
#include <signal.h>
#include <ctype.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include "threadpool.h"
#include "make_log.h"
//for http
//#include <evhttp.h>
#include <event2/http.h>
#include <event2/http_struct.h>
#include <event2/http_compat.h>
#include <event2/util.h>


#define MYHTTPD_SIGNATURE "myhttpd v 0.0.1"
#define MYDIR "/home/ubuntu/http/mydir"
#define N 2048
#define MIN_PTHREAD 10
#define MAX_PTHREAD 100

//判断文件是否存在

pthread_mutex_t mutex;

int is_file_exist(const char *file_path)
{
	if(file_path==NULL)
	{
		return -1;
	}
	if(access(file_path,F_OK)==0)
	{
		return 0;
	}
	return -1;
}
//判断目录是否存在
int is_dir_exist(const char *dir_path)
{
	if(dir_path==NULL)
	{
		return -1;
	}
	if(opendir(dir_path)==NULL)
	{
		return -1;
	}
	return 0;
}
//发送http头
void send_headers(struct evhttp_request *req,char *type)
{
	if(req==NULL || type==NULL)
	{
		return;
	}
	evhttp_add_header(req->output_headers, "Server", MYHTTPD_SIGNATURE);
	evhttp_add_header(req->output_headers, "Content-Type", type);
	evhttp_add_header(req->output_headers, "Connection", "close");
}
//发送错误页面
void send_error(struct evhttp_request *req,int status,char *title,char *text)
{
	send_headers(req,"text/html");
		struct evbuffer *buf;
		buf = evbuffer_new();
		evbuffer_add_printf(buf, "<html><head><title>%d %s</title></head>\n", status,title);
		evbuffer_add_printf(buf, "<body bgcolor=\"#cc99cc\"><h4>%d %s</h4>\n",status,title);
		evbuffer_add_printf(buf, "%s\n<hr>\n</body>\n</html>\n",text);
		evhttp_send_reply(req, HTTP_OK, "OK", buf);
		evbuffer_free(buf);
}


//发送网页   网页里为文件目录
//发送网页 参数 1 标题  2 GET 后面获取的路径 例：/aa 3 文件名  4文件个数
void send_html(struct evhttp_request *req,char *title,char *path,char *text[],int count)
{
	struct evbuffer *buf;
		buf = evbuffer_new();
		evbuffer_add_printf(buf, "<html><head><title>%s</title></head>\n",title);
		evbuffer_add_printf(buf, "<body bgcolor=\"#cc99cc\"><h4>%s</h4>\n",title);
		int i=0;
		for(i=0;i<count;++i)
		{
			char send_path[N]={0};
				//此处为  /xxxx/xxx   path 为GET /xxx/xxx HTTP/1.1 获取的文件路径
				strcat(send_path,path);
				//此处为必须  因为当光输入例：192.1.1.1:2222  path会自动变为/ 其情况末尾没有/
				if(path[strlen(path)-1]!='/')
				{
					strcat(send_path,"/");
				}
			strcat(send_path,text[i]);
				evbuffer_add_printf(buf, "<a href=\"%s\">%s</a><br/>",send_path,text[i]);
		}
	//evbuffer_add_printf(buf,"<img width=\"%d\" height=\"%d\" src=\"%s\" />",1200,1400,"a.jpg");
		evbuffer_add_printf(buf,"<hr>\n</body>\n</html>\n");
		evhttp_send_reply(req, HTTP_OK, "OK", buf);
		evbuffer_free(buf);
}


//读取目录下的所有文件  并以网页的形式展示
//第一个参数 访问的目录的绝对路径  第二个参数 /xxx/xxx
void read_dir(struct evhttp_request *req,char *absolute_path,char *path)
{
		int i=0;
		char *readpath=absolute_path;
		struct dirent **namelist;
		//读取当前目录下所有文件和目录的名字，1 目录名  2 二维数组存放  3 屏蔽的问价或目录  4 排序 按字母或时间排序
		int ret=scandir(readpath,&namelist,NULL,alphasort);
		if(ret==-1)
		{
			LOG("server_log","server_log","%s,%d,read dir failed",__FILE__,__LINE__);
				send_error(req,406,"scandir dir not find","scandir dir not fount");
				return;
		}
		char *temp[ret];
		for(i=0;i<ret;i++)
		{
			temp[i]=namelist[i]->d_name;
		}
		send_headers(req,"text/html");
		send_html(req,"List",path,temp,ret);
}

void *pthread_handler(void *arg)
{
		struct evhttp_request *req=(struct evhttp_request *)arg;
		if(req==NULL)
		{
				LOG("server_log","server_log","%s,%d,req==NULL",__FILE__,__LINE__);
				send_error(req,399, "struct evhttp_request=NULL ERROR", "struct evhttp_request=NULL ERROR");
				//pthread_exit(NULL);
				return NULL;
		}
		char output[N] = "\0";
		char tmp[N];
		char * file;
		FILE *fp;
		char *type=NULL;
		struct stat sbuf;
		
		//获取客户端请求的URI(使用evhttp_request_uri或直接req->uri)
		const char *uri;
		uri = evhttp_request_uri(req);
		sprintf(tmp, "uri=%s\n", uri);
		strcat(output, tmp);
		
		//decoded uri
		char *decoded_uri;
		decoded_uri = evhttp_decode_uri(uri);
		sprintf(tmp, "decoded_uri=%s\n", decoded_uri);
		strcat(output, tmp);
		
		/*
		//解析URI的参数(即GET方法的参数)
		struct evkeyvalq params;
		//将URL数据封装成key-value格式,q=value1, s=value2
		evhttp_parse_query(decoded_uri, &params);
		//得到q所对应的value
		sprintf(tmp, "q=%s\n", evhttp_find_header(&params, "q"));
		strcat(output, tmp);
		//得到s所对应的value
		sprintf(tmp, "s=%s\n", evhttp_find_header(&params, "s"));
		strcat(output, tmp);
		
		//获取POST方法的数据
		char *post_data = (char *) EVBUFFER_DATA(req->input_buffer);
		sprintf(tmp, "post_data=%s\n", post_data);
		strcat(output, tmp);
		 */
		//移动到当前目录
		if(chdir(MYDIR)==-1)
		{
			LOG("server_log","server_log","%s,%d,chdir(MYDIR)==-1",__FILE__,__LINE__);
				send_error(req,402, "Internal Error", "Config error - couldn't chdir().");
				free(decoded_uri);
				//pthread_exit(NULL);
				return NULL;
		}
	if(decoded_uri[0]!='/')
	{
		LOG("server_log","server_log","%s,%d,decoded_uri[0]!='/'",__FILE__,__LINE__);
		send_error(req,400, "Bad Request", "Bad filename.");
		free(decoded_uri);
		//pthread_exit(NULL);
		return NULL;
	}
	//文件的绝对路径
	char absolute_path[N]={0};
		strcat(absolute_path,MYDIR);
		strcat(absolute_path,decoded_uri);
		//如果请求的路径不对
		if(is_file_exist(absolute_path)!=0 && is_dir_exist(absolute_path)!=0)
		{
			LOG("server_log","server_log","%s,%d,Bad Request Can not find dir or file",__FILE__,__LINE__);
				send_error(req,403, "Bad Request", "Bad filename.");
				free(decoded_uri);
				//pthread_exit(NULL);
				return NULL;
		}
	//去掉最前面/之后的文件
		file=decoded_uri+1;
		//如果请求的是根的话  变成./
		if(strcmp("",file)==0)
		{
			file="./";
		}
	//如果获取文件信息失败
	if(lstat(file,&sbuf)<0)
	{
			LOG("server_log","server_log","%s,%d,lstat error",__FILE__,__LINE__);
			send_error(req,404,"can not find file or dir",file);
			free(decoded_uri);
			//pthread_exit(NULL);
			return NULL;
	}
	//判断目录
	if(S_ISDIR(sbuf.st_mode))
	{
		//decoded_uri 前面 有/
		read_dir(req,absolute_path,decoded_uri);
			free(decoded_uri);
			//pthread_exit(NULL);
			return NULL;
	}
		pthread_mutex_lock(&mutex);
		fp=fopen(file,"r");
		if(fp==NULL)
		{
			LOG("server_log","server_log","%s,%d,fopen error",__FILE__,__LINE__);
			send_error(req,405, "Forbidden", "File is protected.");
			free(decoded_uri);
			//pthread_exit(NULL);
			return NULL;
		}
	//strrchr 获取后缀的位置
	  char *dot = strrchr(file, '.');
		if (dot == NULL)
		{type = "text/plain; charset=utf-8";}
		else if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
		{type = "text/html; charset=utf-8";}
		else if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
		{type = "image/jpeg";}
		else if (strcmp(dot, ".gif") == 0)
		{type = "image/gif";}
		else if (strcmp(dot, ".png") == 0)
		{type = "image/png";}
		else if (strcmp(dot, ".css") == 0)	
		{type = "text/css";}
		else if (strcmp(dot, ".au") == 0)	
		{type = "audio/basic";}
		else if (strcmp( dot, ".wav") == 0)
		{type = "audio/wav";}
		else if (strcmp(dot, ".avi") == 0)
		{type = "video/x-msvideo";}
		else if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
		{type = "video/quicktime";}
		else if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
		{type = "video/mpeg";}
		else if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)	
		{type = "model/vrml";}
		else if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)	
		{type = "audio/midi";}
		else if (strcmp(dot, ".mp3") == 0)
		{type = "audio/mpeg";}
		else if (strcmp(dot, ".ogg") == 0)	
		{type = "application/ogg";}
		else if (strcmp(dot, ".pac") == 0)	
		{type = "application/x-ns-proxy-autoconfig";}
		else 
		{type = "text/plain; charset=utf-8";}
	
		send_headers(req,type);
		struct evbuffer *buf;
		buf = evbuffer_new();
		char readBuf[N]={0};
		int num=-1;
		
		while((num=fread(readBuf,1,N,fp))>0)
		{
			evbuffer_add(buf,readBuf,num);
			memset(readBuf,0,N);
		} 
		pthread_mutex_unlock(&mutex);
	  evhttp_send_reply(req, HTTP_OK, "OK", buf);
		fclose(fp);
		free(decoded_uri);
		//pthread_exit(NULL);
		return NULL;
}

//事件处理函数
void httpd_handler(struct evhttp_request *req, void *arg) 
{
	threadpool_t *thp=(threadpool_t *)arg;
	int ret=threadpool_add(thp,pthread_handler,(void *)req);
	if(ret!=0)
	{
		LOG("server_log","server_log","%s,%d,threadpool_add error",__FILE__,__LINE__);
		send_error(req,405, "pthread_create error", "pthread_create error");
		return;
	}
	return ;
}




//显示帮助
void show_help() {
	char *help = "http://localhost:8080\n"\
		"-l <ip_addr> interface to listen on, default is 0.0.0.0\
		\n"\
		"-p <num> port number to listen on, default is 1984"\
		"\n"\
		"-d run as a deamon\n"\
		"-t <second> timeout for a http request, default is 120"\
		"seconds\n"\
		"-h print this help and exit\n"\
		"\n";
		fprintf(stderr,"%s",help);
}




//当向进程发出SIGTERM/SIGHUP/SIGINT/SIGQUIT的时候，终止event的事件侦听循环
void signal_handler(int sig) {
	switch (sig) {
		case SIGTERM:
		case SIGHUP:
		case SIGQUIT:
		case SIGINT:
			    event_loopbreak(); //终止侦听event_dispatch()的事件侦听循环，执行之后的代码
			    break;
	}
}

//-----------------------------------------main----------------------------------------------
int main(int argc, char *argv[]) 
{
		pthread_mutex_init(&mutex,NULL);
		//自定义信号处理函数
		signal(SIGHUP, signal_handler);
		signal(SIGTERM, signal_handler);
		signal(SIGINT, signal_handler);
		signal(SIGQUIT, signal_handler);
		//默认参数
		char *httpd_option_listen = "0.0.0.0";
		int httpd_option_port = 8080;
		int httpd_option_daemon = 0;
		int httpd_option_timeout = 120; //in seconds
	//获取参数   ：表示必须有参数   ：：表示可选   
	int c;
		while ((c = getopt(argc, argv, "l:p:dt:h")) != -1) {
			switch (c) {
				case 'l' :
					  httpd_option_listen = optarg;
						  break;
				case 'p' :
					  httpd_option_port = atoi(optarg);
						  break;
				case 'd' :
					  httpd_option_daemon = 1;
						  break;
				case 't' :
					  httpd_option_timeout = atoi(optarg);
						  break;
				case 'h' :
				default :
					 show_help();
						 exit(EXIT_SUCCESS);
			}
		}
	//判断是否设置了-d，以daemon运行
	if (httpd_option_daemon) {
		pid_t pid;
			pid = fork();
			if (pid < 0) {
				perror("fork failed");
					exit(EXIT_FAILURE);
			}
		if (pid > 0) {
			//生成子进程成功，退出父进程
			exit(EXIT_SUCCESS);
		}
	}
	/* 使用libevent创建HTTP Server */
		//初始化event API
		struct event_base *base = event_init();
		//创建一个http server
		struct evhttp *httpd;
		httpd = evhttp_start(httpd_option_listen, httpd_option_port);
		
		threadpool_t *thp = threadpool_create(MIN_PTHREAD,MAX_PTHREAD,MAX_PTHREAD);
		if(thp==NULL)
		{
			LOG("server_log","server_log","%s,%d,create_pthreadpool error",__FILE__,__LINE__);
			event_base_dispatch(base); 
			evhttp_free(httpd);
			pthread_mutex_destroy(&mutex);
			return 0;
		}
		evhttp_set_timeout(httpd, httpd_option_timeout);
		//指定generic callback
		evhttp_set_gencb(httpd, httpd_handler, (void *)thp);
		//也可以为特定的URI指定callback
		//evhttp_set_cb(httpd, "/", specific_handler, NULL);
		//循环处理events
		event_base_dispatch(base); 
		evhttp_free(httpd);
		threadpool_destroy(thp);
		pthread_mutex_destroy(&mutex);
		return 0;
}
