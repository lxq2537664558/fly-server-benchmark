//-----------------------------------------------------------------------------
//(c) 2007-2016 pavel.pimenov@gmail.com
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <iostream>

#include <map>

//============================================================================================
#ifdef _WIN32
//#define FLY_SERVER_USE_POCO
#endif

#ifdef FLY_SERVER_USE_POCO
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include "Poco/StreamCopier.h"
#include <Poco/Util/ServerApplication.h>
#include <iostream>
#include <string>
#include <vector>

using namespace Poco::Net;
using namespace Poco::Util;

#endif // FLY_SERVER_USE_POCO
//============================================================================================

#include "CDBManager.h"

#include <ctime>
#include <signal.h>

#ifndef _WIN32 // Only in linux
#include <sys/time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <wait.h>
#include <cerrno>

#else

#define snprintf _snprintf
#endif // _WIN32

//#include "frozen/frozen.h"
#ifdef _DEBUG
// #define FLY_SERVER_USE_FROZEN
#endif

#ifdef _WIN32 // Only in WIN32
#ifdef _DEBUG
// #define VLD_DEFAULT_MAX_DATA_DUMP 1
// #define VLD_FORCE_ENABLE // Uncoment this define to enable VLD in release
// #include "C:\Program Files (x86)\Visual Leak Detector\include\vld.h" // VLD качать тут http://vld.codeplex.com/
#endif
#endif

//==========================================================================
CDBManager g_DB;
//==========================================================================
struct mg_server *g_server = NULL;
//==========================================================================
unsigned long long g_count_query = 0;
unsigned long long g_count_download = 0;
unsigned long long g_count_antivirus = 0;
unsigned long long g_sum_out_size = 0;
unsigned long long g_sum_in_size = 0;
unsigned long long g_z_sum_out_size = 0;
unsigned long long g_z_sum_in_size = 0;
int  g_sqlite_cache_db     = 8;  // Размер кеша по умолчанию
//==========================================================================
static void save_statistic()
{
	CFlyRegistryMap l_values;
	l_values["g_count_query"]   = g_count_query;
	l_values["g_count_download"]   = g_count_download;
	l_values["g_count_antivirus"]   = g_count_download;
	l_values["g_sum_out_size"]  = g_sum_out_size;
	l_values["g_sum_in_size"]  = g_sum_in_size;
	l_values["g_z_sum_out_size"]  = g_z_sum_out_size;
	l_values["g_z_sum_in_size"]  = g_z_sum_in_size;
	g_DB.save_registry(l_values, e_Statistic);
}
//==========================================================================
static void load_statistic()
{
	CFlyRegistryMap l_values;
	g_DB.load_registry(l_values, e_Statistic);
	g_count_download = l_values["g_count_download"];
    g_count_antivirus = l_values["g_count_antivirus"];
	g_count_query = l_values["g_count_query"];
	g_sum_out_size = l_values["g_sum_out_size"];
	g_sum_in_size = l_values["g_sum_in_size"];
	g_z_sum_out_size = l_values["g_z_sum_out_size"];
	g_z_sum_in_size = l_values["g_z_sum_in_size"];
}
//==========================================================================
static const char *g_html_form =
    "<html><body>Error fly-server-benchmark - contact: pavel.pimenov@gmail.com query</body></html>";
//==========================================================================
static const char* g_badRequestReply = "HTTP/1.1 400 Bad Request\r\n";
//==========================================================================
static int begin_request_handler(struct mg_connection *conn, enum mg_event ev)
{

	if (ev == MG_AUTH)
	{
		//dcassert(0);
		return MG_TRUE;
	}
	if (ev == MG_REQUEST)
	{
		CFlyServerContext l_flyserver_cntx;
		l_flyserver_cntx.init_uri(conn->uri, mg_get_header(conn, "User-Agent"), mg_get_header(conn, "X-fly-response"));
		l_flyserver_cntx.m_remote_ip = conn->remote_ip;
		l_flyserver_cntx.m_content_len = conn->content_len;
		if (l_flyserver_cntx.m_query_type != FLY_POST_QUERY_UNKNOWN)
		{
			if (conn->content_len <= 0)
			{
				mg_printf(conn, "%s", g_badRequestReply);
				mg_printf(conn, "Error: no content\n");
				std::cout << "Error: no content." << std::endl;
#ifndef _WIN32
				syslog(LOG_NOTICE, "Error: no content");
#endif
				return MG_TRUE;
			}
// BEGIN - process - TODO - убрать копипаст
			l_flyserver_cntx.run_db_query(conn->content, conn->content_len, g_DB);
// END Process

			const int l_result_mg_printf = mg_printf(conn, "HTTP/1.1 200 OK\r\n"          
			                                         "Content-Length: %u\r\n\r\n",
			                                         l_flyserver_cntx.get_http_len());
			// Докидываем второй командой данные (TODO - Склеить все в одну)
			const int l_result_mg_write = mg_write(conn, l_flyserver_cntx.get_result_content(), l_flyserver_cntx.get_http_len());
			g_sum_out_size += l_flyserver_cntx.m_res_stat.size();
			g_sum_in_size  += l_flyserver_cntx.get_real_query_size();
			g_z_sum_out_size += l_flyserver_cntx.get_http_len();
			g_z_sum_in_size  += conn->content_len;
			l_flyserver_cntx.send_syslog();
			++g_count_query;
			
#ifdef _DEBUG
			std::cout << "[DEBUG] l_result_mg_write = " << l_result_mg_write << " l_http_len = " << l_flyserver_cntx.get_http_len() << " l_result_mg_printf = " << l_result_mg_printf << std::endl;
#endif
			return MG_TRUE;
		}
		else
		{
			// Show HTML form.
			mg_printf(conn, "HTTP/1.1 200 OK\r\n"
			          "Content-Length: %d\r\n"
			          "Content-Type: text/html\r\n\r\n%s",
			          (int) strlen(g_html_form), g_html_form);
			return MG_TRUE;
		}
		// Mark as processed
	}
	return MG_FALSE;
}

static int g_exit_flag = 0;

static void signal_handler(int sig_num)
{
	// Reinstantiate signal handler
	signal(sig_num, signal_handler);
	
#if !defined(_WIN32)
	// Do not do the trick with ignoring SIGCHLD, cause not all OSes (e.g. QNX)
	// reap zombies if SIGCHLD is ignored. On QNX, for example, waitpid()
	// fails if SIGCHLD is ignored, making system() non-functional.
	if (sig_num == SIGCHLD)
	{
		do {}
		while (waitpid(-1, &sig_num, WNOHANG) > 0);
	}
	else
#endif
	{
		g_exit_flag = sig_num;
	}
}
//======================================================================================
#ifdef FLY_SERVER_USE_POCO
class MyRequestHandler : public HTTPRequestHandler
{
	public:
		virtual void handleRequest(HTTPServerRequest &p_req, HTTPServerResponse &p_resp)
		{
			// https://github.com/sys-bio/telPlugins/blob/9e4b20fd0e0802f1ed3e7dea1e058fae93b4ad7d/third_party/poco/Net/testsuite/src/HTTPServerTest.cpp#L132
			/*
			    if (request.getChunkedTransferEncoding())
			                response.setChunkedTransferEncoding(true);
			            else if (request.getContentLength() != HTTPMessage::UNKNOWN_CONTENT_LENGTH)
			                response.setContentLength(request.getContentLength());
			
			            response.setContentType(request.getContentType());
			
			            std::istream& istr = request.stream();
			            std::ostream& ostr = response.send();
			            std::streamsize n = StreamCopier::copyStream(istr, ostr);
			*/
			p_resp.setStatus(HTTPResponse::HTTP_OK);
			//resp.setContentType("text/html");
			if (p_req.getMethod() == "POST")
			{
				std::string l_fly_resp; // TODO  = p_req.get("X-fly-response");
				const std::string l_URI = p_req.getURI();
				const std::string l_user_agent = p_req.get("User-Agent");
				if (!l_URI.empty())
				{
					std::string l_post_data; // http://pocoproject.org/forum/viewtopic.php?p=11920#p11920
					Poco::StreamCopier::copyToString(p_req.stream(), l_post_data, 64535 * 4);
					if (l_post_data.size() > 0)
					{
						CFlyServerContext l_flyserver_cntx;
						l_flyserver_cntx.init_uri(l_URI.c_str(), l_user_agent.c_str(), l_fly_resp.c_str());
						const std::string l_ip = p_req.clientAddress().toString();
						l_flyserver_cntx.m_remote_ip = l_ip.substr(0, l_ip.find(':')); // TODO - криво
						l_flyserver_cntx.m_content_len = l_post_data.size();
						
// BEGIN - process - TODO - убрать копипаст
						l_flyserver_cntx.run_db_query(l_post_data.data(), l_flyserver_cntx.m_content_len, g_DB);
// END Process

						p_resp.sendBuffer(l_flyserver_cntx.get_result_content(), l_flyserver_cntx.get_http_len());
						//std::cout << std::endl
						//    << " and URI=" << p_req.getURI() << std::endl
						//    << " and X-fly-response=" << l_fly_resp << std::endl;
					}
					else
					{
						std::cout << "Error: no content." << std::endl;
#ifndef _WIN32
						syslog(LOG_NOTICE, "Error: no content");
#endif
					}
				}
			}
		}
};
//======================================================================================
class MyRequestHandlerFactory : public HTTPRequestHandlerFactory
{
	public:
		virtual HTTPRequestHandler* createRequestHandler(const HTTPServerRequest &)
		{
			return new MyRequestHandler;
		}
};

class MyServerApp : public ServerApplication
{
	protected:
		int main(const std::vector<string> &)
		{
			HTTPServer s(new MyRequestHandlerFactory, ServerSocket(37015), new HTTPServerParams);
			
			s.start();
			std::cout << std::endl << "Server started" << std::endl;
			
			waitForTerminationRequest();  // wait for CTRL-C or kill
			
			std::cout << std::endl << "Shutting down..." << std::endl;
			s.stop();
			
			return Application::EXIT_OK;
		}
};
#endif
//======================================================================================
int main(int argc, char* argv[])
{
	for (int i = 1; i < argc; i++)
	{
       if (strcmp(argv[i], "-cache") == 0 && ++i < argc)
			{
				g_sqlite_cache_db = atoi(argv[i]);
				if (g_sqlite_cache_db == 0)
				{
					std::cout << std::endl << "Error cache size: usage -cache 8M " << std::endl;
					g_sqlite_cache_db = 16;
				}
				else
				{
					std::cout << std::endl << "[setup] cache_size = " << g_sqlite_cache_db << "Mb" << std::endl;
				}
			}
	}
	// Setup signal handler: quit on Ctrl-C
	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);
#ifndef _WIN32
	signal(SIGCHLD, signal_handler);
#endif

	std::cout << std::endl << "* FlylinkDC++ server r726 (c) 2012-2016 pavel.pimenov@gmail.com " << std::endl
#ifndef FLY_SERVER_USE_POCO
	          << "  - mongoose " << MONGOOSE_VERSION << " (c) https://github.com/cesanta" << std::endl
#else
	          << "  - POCO 1.3.x" << " (c) http://pocoproject.org" << std::endl  // TODO - версию
#endif
	          << "  - sqlite " << SQLITE_VERSION << " (c) http://sqlite.org" << std::endl
#ifdef FLY_SERVER_USE_LEVELDB
	          << "  - LevelDB " << LEVELDB_VER << " (c) https://github.com/google/leveldb" << std::endl
	          << "  - Snappy " << "1.1.3" << " (c) https://github.com/google/snappy" << std::endl
#endif
	          << std::endl << "Usage: fly-server-benchmark [-cache 8M]" << std::endl;
	g_DB.init();
	load_statistic();
	
#ifdef FLY_SERVER_USE_POCO
	MyServerApp app;
	const int l_result_app = app.run(1 /*argc*/, argv);
	save_statistic();
	g_DB.shutdown();
	std::cout << std::endl << "* fly-server-benchmark (POCO) shutdown!" << std::endl;
  return l_result_app;
#endif
	
	// g_DB.zlib_convert_attr_value();
	g_server = mg_create_server(NULL, begin_request_handler);
#ifdef _WIN32
	mg_set_option(g_server, "listening_port", "37015");
#else
	mg_set_option(g_server, "listening_port", "37015");
#endif
	mg_set_option(g_server, "enable_directory_listing", "no");
#ifdef _WIN32
 #ifdef _DEBUG
	// mg_set_option(g_server, "hexdump_file", "111-hex.log");
 #endif
#endif

#ifdef _WIN32
//		mg_set_option(server, "access_log_file", "fly-server-access.log");
//		mg_set_option(server, "error_log_file", "fly-server-error.log");
#else
//		"access_log_file", "/var/log/fly-server/fly-server-access.log",
//		mg_set_option(server, "error_log_file", "/var/log/fly-server/fly-server-error.log");
#endif


	while (g_exit_flag == 0)
	{
		mg_poll_server(g_server, 1000);
	}
	std::cout << std::endl << "* Exiting on signal " << g_exit_flag << std::endl << std::endl;
	mg_destroy_server(&g_server);
	save_statistic();
	g_DB.shutdown();
	std::cout << std::endl << "* fly-server-benchmark (mongoose) shutdown!" << std::endl;
	return 0;
}

