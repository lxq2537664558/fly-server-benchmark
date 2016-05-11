// fly-server-benchmark-client.cpp : Defines the entry point for the console application.
//

/*
* Copyright (c) 2014 Cesanta Software Limited
* All rights reserved
*
* This program fetches HTTP URLs.
*/

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS /* Disable deprecation warning in VS2005+ */
#endif

#include <iostream>
#include <sstream>
#include <string>
#include <boost/filesystem.hpp>
#include <boost/iostreams/device/mapped_file.hpp>

/*
#include "Poco/Net/HTTPClientSession.h"
#include "Poco/Net/HTTPRequest.h"
#include "Poco/Net/HTTPResponse.h"
#include "Poco/StreamCopier.h"
#include "Poco/NullStream.h"

using Poco::Net::HTTPClientSession;
using Poco::Net::HTTPRequest;
using Poco::Net::HTTPResponse;
using Poco::Net::HTTPMessage;
using Poco::StreamCopier;
using Poco::NullOutputStream;

#include "mongoose.h"
*/
#include <windows.h>
#include <tchar.h>
#include <wininet.h>


using std::string;

#pragma comment(lib, "IPHLPAPI.lib")
#pragma comment(lib,"wininet.lib")

/*
static int s_exit_flag = 0;
static int s_show_headers = 1;

static void ev_handler(struct mg_connection *nc, int ev, void *ev_data) {
	struct http_message *hm = (struct http_message *) ev_data;

	switch (ev) {
    case MG_EV_CONNECT:
		if (*(int *)ev_data != 0) {
			fprintf(stderr, "connect() failed: %s\n", strerror(*(int *)ev_data));
			s_exit_flag = 1;
		}
		break;
    case MG_EV_HTTP_REPLY:
      nc->flags |= MG_F_CLOSE_IMMEDIATELY;
		if (s_show_headers) {
			fwrite(hm->message.p, 1, hm->message.len, stdout);
      } else {
			fwrite(hm->body.p, 1, hm->body.len, stdout);
		}
		putchar('\n');
		s_exit_flag = 1;
		break;
	default:
		break;
	}
}
*/
//======================================================================================================
class CInternetHandle
{
public:
	explicit CInternetHandle(HINTERNET p_hInternet) : m_hInternet(p_hInternet)
	{
	}
	~CInternetHandle()
	{
		if (m_hInternet)
		{
			::InternetCloseHandle(m_hInternet);
		}
	}
	operator const HINTERNET() const
	{
		return m_hInternet;
	}
protected:
	const HINTERNET m_hInternet;
};
class CFlyLog
{
public:
	const string m_message;
	void log(const string& p_msg)
	{
		std::cout<< p_msg << std::endl;
	}
public:
	CFlyLog(const string& p_message
	) :m_message(p_message)
	{
	}
	~CFlyLog()
	{
	}
	uint64_t calcSumTime() const;
	void step(const string& p_message_step, const bool p_reset_count = true)
	{
		log("[Step] " + m_message + ' ' + p_message_step);
	}
	void loadStep(const string& p_message_step, const bool p_reset_count = true)
	{
		log("[loadStep] " + m_message + ' ' + p_message_step);
	}
};

//===========================================================================================
static string toString(int val)
{
	char buf[16];
	_snprintf(buf, _countof(buf), "%d", val);
	return buf;
}
//===========================================================================================
static std::string translateError()
{
	return "GetLastError() = " + toString(GetLastError());
}
//===========================================================================================
static DWORD g_http_tick_count = 0;

static bool postQuery(
	const char* p_body,
	int p_len_body,
	bool& p_is_send,
	bool& p_is_error)
{
	p_is_send = false;
	p_is_error = false;
	CFlyLog l_fly_server_log("log");
	//string l_reason;
	//string l_result_query;
	//string l_log_string;
	// Передача
	const auto l_http_tick_start = GetTickCount();
	CInternetHandle hSession(InternetOpen(_T("fly-server-benchmark"), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0));
	//InternetSetOption(hSession, INTERNET_OPTION_RECEIVE_TIMEOUT, &CFlyServerConfig::g_winet_receive_timeout, sizeof(CFlyServerConfig::g_winet_receive_timeout));
	//InternetSetOption(hSession, INTERNET_OPTION_SEND_TIMEOUT, &CFlyServerConfig::g_winet_send_timeout, sizeof(CFlyServerConfig::g_winet_send_timeout));
	if (hSession)
	{
		DWORD dwFlags = 0; //INTERNET_FLAG_NO_COOKIES|INTERNET_FLAG_RELOAD|INTERNET_FLAG_NO_CACHE_WRITE|INTERNET_FLAG_PRAGMA_NOCACHE;
		CInternetHandle hConnect(InternetConnectA(hSession, "192.168.1.76", 37015, NULL, NULL, INTERNET_SERVICE_HTTP, dwFlags, NULL));
		if (hConnect)
		{
			CInternetHandle hRequest(HttpOpenRequestA(hConnect, "POST", "fly-zget", NULL, NULL, NULL /*g_accept*/, dwFlags, NULL));
			if (hRequest)
			{
				string l_fly_header;
				if (HttpSendRequestA(hRequest,
					l_fly_header.length() ? l_fly_header.c_str() : nullptr,
					l_fly_header.length(),
					LPVOID(p_body),
					p_len_body))
				{
					DWORD l_dwBytesAvailable = 0;
					// TODO убрать std::vector<char> l_zlib_blob;
					std::vector<unsigned char> l_MessageBody;
					while (InternetQueryDataAvailable(hRequest, &l_dwBytesAvailable, 0, 0))
					{
						if (l_dwBytesAvailable == 0)
							break;
#ifdef _DEBUG
						//l_fly_server_log.step("InternetQueryDataAvailable dwBytesAvailable = " + Util::toString(l_dwBytesAvailable));
#endif
						l_MessageBody.resize(l_dwBytesAvailable + 1);
						DWORD dwBytesRead = 0;
						const BOOL bResult = InternetReadFile(hRequest, l_MessageBody.data(), l_dwBytesAvailable, &dwBytesRead);
						if (!bResult)
						{
							l_fly_server_log.step("InternetReadFile error " + translateError());
							break;
						}
						if (dwBytesRead == 0)
							break;
						l_MessageBody[dwBytesRead] = 0;
						//const auto l_cur_size = l_zlib_blob.size();
						//l_zlib_blob.resize(l_cur_size + dwBytesRead);
						//memcpy(l_zlib_blob.data() + l_cur_size, l_MessageBody.data(), dwBytesRead);
					}
					p_is_send = true;
#ifdef _DEBUG
				//	l_fly_server_log.step("InternetReadFile Ok! size = " + toString(l_result_query.size()));
#endif
				}
				else
				{
					l_fly_server_log.step("HttpSendRequest error " + translateError());
					p_is_error = true;
				}
			}
			else
			{
				l_fly_server_log.step("HttpOpenRequest error " + translateError());
				p_is_error = true;
			}
		}
		else
		{
			l_fly_server_log.step("InternetConnect error " + translateError());
			p_is_error = true;
		}
	}
	else
	{
		l_fly_server_log.step("InternetOpen error " + translateError());
		p_is_error = true;
	}
	g_http_tick_count += GetTickCount() - l_http_tick_start;
	return p_is_send;
}


int main(int argc, char *argv[]) {
	/*
	 struct mg_mgr mgr;
     mg_mgr_init(&mgr, NULL);
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--hexdump") == 0 && i + 1 < argc) {
			mgr.hexdump_file = argv[++i];
		}
		else {
			break;
		}
	}
	*/

	//printf("Usage: %s http://192.168.1.76:37015/fly-zget\n\n", argv[0]);

	boost::filesystem::directory_iterator end_itr;
	const auto l_tick_start = GetTickCount();
	unsigned l_count_error = 0;
	unsigned l_count_send = 0;
	unsigned j = 1;

	for (
		boost::filesystem::directory_iterator itr(".");
		itr != end_itr;
		++itr
		)
	{
		if (!boost::filesystem::is_directory(*itr))
		{
			const auto l_name = itr->path().generic_string();
			const auto l_len = l_name.size();
			if (l_len > 5 && l_name[l_len-1] == 'b'
				&& l_name[l_len - 2] == 'i'
				&& l_name[l_len - 3] == 'l'
				&& l_name[l_len - 4] == 'z'
				&& l_name[l_len - 5] == '.')
			{
				boost::iostreams::mapped_file_source l_map_file;
				try
				{
					l_map_file.open(l_name);
				}
				catch (std::exception& e)
				{
					std::cout << std::endl << "Error mapped_file_source: " << e.what() << std::endl;
					continue;
				}
				if (l_map_file.is_open())
				{
					const auto l_size = l_map_file.size();
					const char* l_Mem = l_map_file.data();
					bool l_is_send;
					bool l_is_error;
					postQuery(
						l_Mem,
						l_size,
						l_is_send,
						l_is_error);
					if (l_is_error)
					{
						std::cout << "-";
						l_count_error++;
					}
					if (l_is_send)
					{
						std::cout << "+";
						l_count_send++;
					}
				}
				else
				{
					std::cout << std::endl  << "Error boost::iostreams::mapped_file_source l_map_file: " << *itr << " GetLastError() = " << ::GetLastError() << std::endl;
				}
				if ((++j % 80) == 0)
				{
					std::cout << std::endl << "[" << (j-1) << "]";
				}
			}
		}
	}
	std::cout << std::endl << "Full tick count = " << GetTickCount() - l_tick_start << std::endl
		<< "HHTP tick count = " << g_http_tick_count << std::endl
		<< "l_count_send = " << l_count_send << std::endl
		<< "l_count_error = " << l_count_error << std::endl;

/*
		//mg_mgr_init(&mgr, NULL);
/*
			HTTPClientSession session("192.168.1.76", 37015);
			HTTPRequest req(HTTPRequest::HTTP_POST, "/fly-zget", HTTPMessage::HTTP_1_1);
			std::ifstream file("fly-server-zget.json.zlib", std::ios::binary);
			if (file.is_open())
			{
				std::ostringstream ostrm;
				ostrm << file.rdbuf();
				req.write(ostrm);
				//std::cout << ostrm;
				//std::ostringstream ostrm;
				//ostrm << file.rdbuf();
				HTTPResponse res;
				//stringstream ss;
				session.sendRequest(req);
				std::istream& rs = session.receiveResponse(res);
				NullOutputStream nos;
				StreamCopier::copyStream(rs, nos);
			}
*/
/*
char* buf_base64 = (char*)malloc(n*3);
			memset(buf_base64, 0, n * 3);
			mg_base64_encode(buf, n, buf_base64); // Content-Type:application/octet-stream\r\n
			mg_connect_http(&mgr, ev_handler, "http://192.168.1.76:37015/fly-zget", "Content-Transfer-Encoding: base64\r\n", (const char*)buf_base64);
			free(buf_base64);
*/

/*
while (s_exit_flag == 0) {
			mg_mgr_poll(&mgr, 1000);
		}
		mg_mgr_free(&mgr);
		*/
	return 0;
}

static void Process_Files(const boost::filesystem::path &Path, bool recurse)
{
	//	std::cout << "Folder: " << Path << " [scan=" << g_sum_byte/1024/1024/1024 << " gb]\n";
	boost::filesystem::directory_iterator end_itr;

	for (
		boost::filesystem::directory_iterator itr(Path);
		itr != end_itr;
		++itr
		)
	{
		if (recurse && boost::filesystem::is_directory(*itr))
		{
			boost::filesystem::path Deeper(*itr);
			Process_Files(Deeper, recurse);
			continue;
		}
		for (int i = 1; i < 4; ++i)
		{
			switch (i)
			{
			case 1: // boost - map
			{
				boost::iostreams::mapped_file_source l_map_file;
				l_map_file.open(itr->path()); // filename.c_str() // , filesize
				if (l_map_file.is_open())
				{
					const auto l_size = l_map_file.size();
					const char* l_Mem = l_map_file.data();
				}
				else
				{
					std::cout << "Error boost::iostreams::mapped_file_source l_map_file: " << *itr << " GetLastError() = " << ::GetLastError() << std::endl;
				}
			}
			break;
			}
		}
	}
	return;
}
