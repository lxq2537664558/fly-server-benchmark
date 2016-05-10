//-----------------------------------------------------------------------------
//(c) 2007-2016 pavel.pimenov@gmail.com
//-----------------------------------------------------------------------------
#ifndef CDBManager_H
#define CDBManager_H

#include <vector>
#include <map>
#include <memory>
#include <set>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <ctime>

#ifdef _WIN32
#include <process.h>
#include "zlib/zlib.h"
#else
#include <sys/time.h>
#include <zlib.h>
#include <errno.h>
#endif

#include "Thread.h"

#include "sqlite/sqlite3x.hpp"
#include "jsoncpp/include/json/value.h"
#include "jsoncpp/include/json/reader.h"
#include "jsoncpp/include/json/writer.h"

#include "mongoose.h"
//============================================================================================
extern bool g_setup_syslog_disable;
//============================================================================================
bool zlib_uncompress(const uint8_t* p_zlib_source, size_t p_zlib_len, std::vector<unsigned char>& p_decompress);
bool zlib_compress(const char* p_source, size_t p_len, std::vector<unsigned char>& p_compress, int& p_zlib_result, int p_level = 9);
//============================================================================================

//#ifdef _WIN32
#define FLY_SERVER_USE_LEVELDB
//#endif
//============================================================================================

#ifdef FLY_SERVER_USE_LEVELDB

#include "leveldb/status.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "leveldb/options.h"
#include "leveldb/cache.h"
#include "leveldb/filter_policy.h"

#endif

sqlite_int64 get_tick_count();

enum eTypeQuery
{
	FLY_POST_QUERY_UNKNOWN = 0,
	FLY_POST_QUERY_GET = 1
};


#ifdef _WIN32
#define snprintf _snprintf
#else
#define _atoi64 atoll
#endif

using std::auto_ptr;
using sqlite3x::sqlite3_command;
using sqlite3x::sqlite3_connection;
//==========================================================================
inline std::string toString(long long p_val)
{
	char l_buf[24];
	l_buf[0] = 0;
	snprintf(l_buf, sizeof(l_buf), "%lld", p_val);
	return l_buf;
}
//================================================================================
enum eTypeRegistrySegment
{
	e_Statistic = 1
};
//==========================================================================
struct CFlyRegistryValue
{
	std::string m_val_str;
	sqlite_int64  m_val_int64;
	CFlyRegistryValue(unsigned long long p_val_int64 = 0) :
		m_val_int64(p_val_int64)
	{
	}
	CFlyRegistryValue(const std::string &p_str, sqlite_int64 p_val_int64 = 0) :
		m_val_int64(p_val_int64),
		m_val_str(p_str)
	{
	}
	operator sqlite_int64() const
	{
		return m_val_int64;
	}
};
//================================================================================
typedef std::map<string, CFlyRegistryValue> CFlyRegistryMap;
//==========================================================================
struct CFlyPortTestThreadInfo
{
	std::string m_ip;
	std::string m_CID;
	std::string m_PID;
	std::vector<std::pair<std::string, bool> > m_ports; // second == true - is_tcp
  const char* get_type_port(int p_index) const
  {
      return m_ports[p_index].second ? "TCP" : "UDP";
  }
};
//==========================================================================
class CFlyLogThreadInfo
{
	public:
		eTypeQuery m_query_type;
		std::string m_remote_ip;
		std::string m_in_query;
		time_t m_now;
		CFlyLogThreadInfo()
		{
		}
		CFlyLogThreadInfo(eTypeQuery p_query_type, const std::string& p_remote_ip, const std::string& p_in_query):
			m_query_type(p_query_type),
			m_remote_ip(p_remote_ip),
			m_in_query(p_in_query)
		{
			time(&m_now);
		}
};
typedef std::vector<CFlyLogThreadInfo> CFlyLogThreadInfoArray;
//==========================================================================
class CDBManager;
class CFlyServerContext
{
	public:
		eTypeQuery m_query_type;
		bool m_is_zlib;
		bool m_is_zlib_result;
		std::string m_in_query;
		std::string m_res_stat;
		std::string m_fly_response;
		std::string m_user_agent;
		std::string m_uri;
		std::string m_remote_ip;
		std::string m_error;
		std::vector<unsigned char> m_dest_data;
		std::vector<unsigned char> m_decompress;
		int64_t m_tick_count_start_db;
		int64_t m_tick_count_stop_db;
		size_t m_count_file_in_json;
		size_t m_count_cache;
		size_t m_count_get_only_counter;
		size_t m_count_get_base_media_counter;
		size_t m_count_get_ext_media_counter;
		size_t m_count_insert;
		size_t m_content_len;
		static CFlyLogThreadInfoArray* g_log_array;
		CFlyServerContext():
			m_tick_count_start_db(0),
			m_tick_count_stop_db(0),
			m_count_file_in_json(0),
			m_count_cache(0),
			m_count_get_only_counter(0),
			m_count_get_base_media_counter(0),
			m_count_get_ext_media_counter(0),
			m_count_insert(0),
			m_query_type(FLY_POST_QUERY_UNKNOWN),
			m_content_len(0),
			m_is_zlib(false),
			m_is_zlib_result(true)
		{
		}
		void send_syslog() const;
		void run_db_query(const char* p_content, size_t p_len, CDBManager& p_DB);
		
		bool is_valid_query() const
		{
			return m_query_type == FLY_POST_QUERY_GET;
		}
		bool is_get_or_set_query() const
		{
			return m_query_type == FLY_POST_QUERY_GET;
		}
		unsigned long long get_delta_db() const
		{
			return m_tick_count_stop_db - m_tick_count_start_db;
		}
		size_t get_http_len() const
		{
			if (m_is_zlib)
				return m_dest_data.size();
			else
				return m_res_stat.length();
		}
		const char* get_result_content()
		{
			if (m_is_zlib)
				return reinterpret_cast<const char*>(m_dest_data.data());
			else
				return m_res_stat.c_str();
		}
		char get_compress_flag() const
		{
			return m_decompress.empty() ? ' ' : 'Z';
		}
		void init_uri(const char* p_uri, const char* p_user_agent, const char* p_fly_response)
		{
			if (p_uri)
			{
				m_uri = p_uri;
				calc_query_type(p_uri);
				if (p_user_agent)
				{
					m_user_agent = p_user_agent;
				}
				if (p_fly_response)
				{
					m_fly_response = p_fly_response;
				}
			}
		}
		void init_in_query(const char* p_content, size_t p_content_len)
		{
			if (m_decompress.empty()) // TODO проверять по коду возврата декомпрессии - чтобы 0x78 не слать по ошибке?
				m_in_query = string(p_content, p_content_len);
			else
				m_in_query = string(reinterpret_cast<char*>(m_decompress.data()), m_decompress.size());
		}
		size_t get_real_query_size() const
		{
			if (m_decompress.empty())
				return m_content_len;
			else
				return m_decompress.size();
		}
		void comress_result()
		{
			m_is_zlib = false;
			if (!m_res_stat.empty() && m_is_zlib_result) // Клиент просит сжать ответ zlib?
			{
				int l_zlib_result;
				m_is_zlib = zlib_compress(m_res_stat.c_str(), m_res_stat.size(), m_dest_data, l_zlib_result, 6);
				if (!m_is_zlib)
				{
					std::cout << "compression failed l_zlib_result=" <<   l_zlib_result <<
					          " l_dest_data.size() = " <<  m_dest_data.size() <<
					          " l_flyserver_cntx.m_res_stat.length() = " << m_res_stat.length() << std::endl;
#ifndef _WIN32
					syslog(LOG_NOTICE, "compression failed l_zlib_result = %d l_dest_length = %u m_res_stat.length() = %u",
					       l_zlib_result, unsigned(m_dest_data.size()), unsigned(m_res_stat.length()));
#endif
				}
			}
		}
		
		static std::string get_json_file_name(const char* p_name_dir, const char* p_ip, time_t p_now)
		{
			char l_time_buf[32];
			l_time_buf[0] = 0;
			strftime(l_time_buf, sizeof(l_time_buf), "%Y-%m-%d-%H-%M-%S", gmtime(&p_now));
			static int l_count_uid = 0;
			char l_result_buf[256];
			l_result_buf[0] = 0;
			snprintf(l_result_buf, sizeof(l_result_buf), "%s/%s-%s-%d-pid-%d-%lu.json",
			         p_name_dir, p_ip, l_time_buf, ++l_count_uid, getpid(), pthread_self());
			return l_result_buf;
		}
		
		void log_error()
		{
			if (!m_error.empty())
			{
				time_t l_now;
				time(&l_now);
				const string l_file_name = get_json_file_name("log-internal-sqlite-error", m_remote_ip.c_str(), l_now);
				std::fstream l_log_json(l_file_name.c_str(), std::ios_base::out | std::ios_base::trunc);
				if (!l_log_json.is_open())
				{
					std::cout << "Error open file: " << l_file_name;
#ifndef _WIN32
					syslog(LOG_NOTICE, "Error open file: = %s", l_file_name.c_str());
#endif
				}
				else
				{
					l_log_json.write(m_in_query.c_str(), m_in_query.length());
					l_log_json << std::endl << "Error:" << std::endl << m_error;
				}
			}
		}
	private:
		void calc_query_type(const char* p_uri)
		{
			m_query_type = FLY_POST_QUERY_UNKNOWN;
			m_is_zlib_result = true;
			if (p_uri)
			{
				if (!strncmp(p_uri, "/fly-zget", 9))
				{
					// - fly-zget
					// - fly-zget-full
					m_query_type = FLY_POST_QUERY_GET;
				}
			}
		}
};
//================================================================================
struct CFlyFileKey
{
	std::string   m_tth;
	sqlite_int64  m_file_size;
	CFlyFileKey(const std::string& p_tth, sqlite_int64 p_file_size): m_file_size(p_file_size), m_tth(p_tth)
	{
	}
	CFlyFileKey(): m_file_size(0)
	{
	}
	bool operator < (const CFlyFileKey& p_val) const
	{
		return m_file_size < p_val.m_file_size || (m_file_size == p_val.m_file_size && m_tth < p_val.m_tth);
	}
};
//================================================================================
struct CFlyBaseMediainfo
{
	std::string m_video;
	std::string m_audio;
	std::string m_audio_br;
	std::string m_xy;
};
//================================================================================
struct CFlyFileRecord
{
	sqlite_int64  m_fly_file_id;
	sqlite_int64  m_count_query;
	sqlite_int64  m_count_download;
	sqlite_int64  m_count_antivirus;
	uint16_t      m_count_mediainfo; 
	bool          m_is_only_counter; 
	bool          m_is_new_file;
//	bool          m_is_calc_count_mediainfo;

	CFlyBaseMediainfo m_media;
	CFlyFileRecord() :
		m_fly_file_id(0),
		m_count_query(0),
		m_count_download(0),
		m_count_antivirus(0),
		m_count_mediainfo(0),
		m_is_only_counter(false),
		m_is_new_file(false)
//		m_is_calc_count_mediainfo(false),
	{
	}
};
//================================================================================
typedef std::vector< std::vector<int64_t> > CFlyIDArray;
class CFlyFileRecordMap : public std::map<CFlyFileKey , CFlyFileRecord>
{
	public:
		bool is_new_file() const
		{
			for (const_iterator i = begin(); i != end(); ++i)
				if (i->second.m_fly_file_id == 0)
					return true;
			return false;
		}
};
//================================================================================
class CDBManager;
struct CFlyThreadUpdaterInfo
{
	CFlyFileRecordMap* m_file_full;
	CFlyFileRecordMap* m_file_only_counter;
	CDBManager*  m_db;
	CFlyIDArray* m_id_array;
	CFlyThreadUpdaterInfo():
		m_file_full(NULL),
		m_file_only_counter(NULL),
		m_db(NULL),
		m_id_array(NULL)
	{
	}
	~CFlyThreadUpdaterInfo()
	{
		delete m_file_full;
		delete m_file_only_counter;
		delete m_id_array;
	}
};
//================================================================================
class CDBManager
{
	public:
		CDBManager() {};
		~CDBManager();
		
		void shutdown();
		
		std::string store_media_info(CFlyServerContext& p_flyserver_cntx);
		void load_registry(CFlyRegistryMap& p_values, int p_Segment);
		void save_registry(const CFlyRegistryMap& p_values, int p_Segment);
		void load_registry(TStringList& p_values, int p_Segment);
		void save_registry(const TStringList& p_values, int p_Segment);
		
		void init();
	private:
		bool is_table_exists(const string& p_table_name);
		void pragma_executor(const char* p_pragma);
		
		sqlite3_connection m_flySQLiteDB;
		auto_ptr<sqlite3_command> m_find_tth;
		auto_ptr<sqlite3_command> m_find_tth_and_count_media;
		auto_ptr<sqlite3_command> m_insert_fly_file;
		auto_ptr<sqlite3_command> m_insert_or_replace_fly_file;
		auto_ptr<sqlite3_command> m_select_fly_file;
		auto_ptr<sqlite3_command> m_get_registry;
		auto_ptr<sqlite3_command> m_insert_registry;
		auto_ptr<sqlite3_command> m_delete_registry;

		void prepare_insert_fly_file();
		long long internal_insert_fly_file(const string& p_tth, sqlite_int64 p_file_size);
		
		
		typedef std::map<int, sqlite3_command* > CFlyCacheSQLCommandInt;
		enum
		{
			SQL_CACHE_SELECT_COUNT          = 0,
			SQL_CACHE_SELECT_COUNT_MEDIA    = 1,
			SQL_CACHE_UPDATE_COUNT          = 2,
			SQL_CACHE_LAST
		};
		
		CFlyCacheSQLCommandInt m_sql_cache[SQL_CACHE_LAST]; // Кэш для паетного запроса получения
		// 0. счетчика count_query,
		// 1. счетчика count_query + count(*) из медиаинфы
		// 2. Кэш для массового апдейта счетчика count_query
		
	public:
		void inc_counter_fly_file_bulkL(const std::vector<int64_t>& p_id_array);
	private:
	
	
		int64_t find_tth(const string& p_tth,
		                 int64_t p_size,
		                 bool p_create,
		                 Json::Value& p_result_stat,
		                 bool p_fill_counter,
		                 bool p_calc_count_mediainfo,
		                 int64_t& p_count_query, // Возвращаем счетчик. если >1 то выполняем массовый апдейт +1
		                 int64_t& p_count_download,
						 int64_t& p_count_antivirus,
		                 size_t&  p_count_insert
		                );
		void process_get_query(const size_t p_index_result,
		                       const string& p_tth,
		                       const string& p_size_str,
		                       const int64_t p_id_file,
		                       const Json::Value& p_cur_item_in,
		                       Json::Value& p_result_arrays,
		                       const Json::Value& p_result_counter,
		                       bool p_is_all_only_ext_info,
		                       bool p_is_different_ext_info,
		                       bool p_is_all_only_counter,
		                       bool p_is_different_counter,
		                       bool p_is_only_counter,
		                       const CFlyFileRecord* p_file_record,
		                       size_t& p_only_ext_info_counter
		                      );
		void process_sql_counter(CFlyFileRecordMap& p_sql_array,
		                         bool p_is_get_base_mediainfo,
		                         size_t&   p_count_insert);
	public:
		void process_sql_add_new_fileL(CFlyFileRecordMap& p_sql_array,
		                               size_t& p_count_insert);
	private:
		sqlite3_command* bind_sql_counter(const CFlyFileRecordMap& p_sql_array,
		                                  bool p_is_get_base_mediainfo);
		void prepare_and_find_all_tth(const Json::Value& p_root,
		                              const Json::Value& p_array,
		                              CFlyFileRecordMap& p_sql_result_array,
		                              CFlyFileRecordMap& p_sql_result_array_only_counter,
		                              bool p_is_all_only_counter,
		                              bool p_is_all_only_ext_info,
		                              bool p_is_different_ext_info,
		                              bool p_is_different_counter,
		                              size_t& p_count_insert);
		// CriticalSection gm_cs_Writer;
		// CriticalSection gm_cs_Reader;
	public: 
		CriticalSection m_cs_sqlite;
	private:
		
		static void errorDB(const string& p_txt, bool p_is_exit_process = true);
		bool safeAlter(const char* p_sql);
		
#ifdef FLY_SERVER_USE_LEVELDB
	private:
		typedef sqlite_int64 JSONMediainfoID;
		class CFlyLevelDB
		{
				leveldb::DB* m_db;
				leveldb::Options      m_options;
				leveldb::ReadOptions  m_readoptions;
				leveldb::ReadOptions  m_iteroptions;
				leveldb::WriteOptions m_writeoptions;
				
			public:
				CFlyLevelDB(leveldb::CompressionType p_compression);
				~CFlyLevelDB();
				
				bool open_level_db(const string& p_db_name);
				bool get_value(const void* p_key, size_t p_key_len, string& p_result);
				bool set_value(const void* p_key, size_t p_key_len, const void* p_val, size_t p_val_len);
				bool get_value(const string& p_tth, string& p_result)
				{
					return get_value(p_tth.c_str(), p_tth.length(), p_result);
				}
				bool set_value(const string& p_tth, const string& p_status)
				{
					dcassert(!p_status.empty());
					if (!p_status.empty())
						return set_value(p_tth.c_str(), p_tth.length(), p_status.c_str(), p_status.length());
					else
						return false;
				}
		};
		auto_ptr<CFlyLevelDB> m_flyLevelDBCompress;
		//
#endif // FLY_SERVER_USE_LEVELDB
		
};

#endif
