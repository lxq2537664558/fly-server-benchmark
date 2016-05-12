//-----------------------------------------------------------------------------
//(c) 2007-2016 pavel.pimenov@gmail.com
//-----------------------------------------------------------------------------

#include <stdio.h>
#include "CDBManager.h"

using sqlite3x::database_error;
using sqlite3x::sqlite3_transaction;
using sqlite3x::sqlite3_reader;

#ifdef _WIN32
#define snprintf _snprintf
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SOCKET int
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)
#define closesocket(x) close(x)

#endif
// DECLARE_PERFORMANCE_FILE_STREAM(C:\\!test\\log-sqlite.log, g_flylinkdc_sqlite);

bool g_DisableSQLiteWAL    = false;
//==========================================================================
bool g_setup_syslog_disable        = false;
//==========================================================================
CFlyLogThreadInfoArray* CFlyServerContext::g_log_array = NULL;
//==========================================================================
sqlite_int64 get_tick_count()
{
#ifdef _WIN32 // Only in windows
	LARGE_INTEGER l_counter;
	QueryPerformanceCounter(&l_counter);
	return l_counter.QuadPart;
	//return GetTickCount64();
#else // Linux
	struct timeval tim;
	gettimeofday(&tim, NULL);
	unsigned int t = ((tim.tv_sec * 1000) + (tim.tv_usec / 1000)) & 0xffffffff;
	return t;
#endif // _WIN32
}
//==========================================================================
void CFlyServerContext::run_db_query(const char* p_content, size_t p_len, CDBManager& p_DB)
{
	zlib_uncompress((uint8_t*)p_content, p_len, m_decompress);
	m_tick_count_start_db = get_tick_count();
	init_in_query(p_content, p_len);
    if (is_get_or_set_query())
	{
		m_res_stat = p_DB.store_media_info(*this);
		log_error();
	}
	m_tick_count_stop_db = get_tick_count();
	comress_result();
}
//==========================================================================
void CFlyServerContext::send_syslog() const
{
	extern unsigned long long g_sum_out_size;
	extern unsigned long long g_sum_in_size;
	extern unsigned long long g_z_sum_out_size;
	extern unsigned long long g_z_sum_in_size;
	extern unsigned long long g_count_query;
	if (g_setup_syslog_disable == false)
	{
		char l_log_buf[512];
		l_log_buf[0]   = 0;// Пока заполняется 152-160
		char l_buf_cache[32];
		l_buf_cache[0] = 0;
		char l_buf_counter[64];
		l_buf_counter[0] = 0;
		if (m_count_cache)
		{
			snprintf(l_buf_cache, sizeof(l_buf_cache), "[cache=%u]", (unsigned) m_count_cache);
		}
		if (m_count_get_only_counter == 0 && m_count_get_base_media_counter == 1 && m_count_get_ext_media_counter == 1)
		{
			snprintf(l_buf_counter, sizeof(l_buf_counter), "[get full Inform!]");
		}
		else if (m_count_get_base_media_counter != 0 || m_count_get_ext_media_counter != 0 || m_count_insert != 0)
		{
			snprintf(l_buf_counter, sizeof(l_buf_counter), "[cnt=%u,base=%u,ext=%u,new=%u]",
			         (unsigned)m_count_get_only_counter,
			         (unsigned)m_count_get_base_media_counter,
			         (unsigned)m_count_get_ext_media_counter,
			         (unsigned)m_count_insert);
		}
			snprintf(l_log_buf, sizeof(l_log_buf), "[%s][%c][%u]%s[%s][%s][%u/%u->%u/%u][time db=%llu][%s]%s%s",
			         m_fly_response.c_str(),
			         get_compress_flag(),
			         (unsigned)m_count_file_in_json,
			         "<-",
			         m_uri.c_str(),
			         m_remote_ip.c_str(),
			         (unsigned)get_real_query_size(),
			         (unsigned)m_content_len,
			         (unsigned)m_res_stat.size(),
			         (unsigned)get_http_len(),
			         get_delta_db(),
			         m_user_agent.c_str(),
			         l_buf_cache,
			         l_buf_counter
			        );
		std::cout << ".";
		static int g_cnt = 0;
		if ((++g_cnt % 30) == 0)
			std::cout << std::endl;
#ifndef _WIN32 // Only in linux
		syslog(LOG_NOTICE, "%s", l_log_buf);
#else
		std::cout << l_log_buf << std::endl;
#endif
	}
}
//========================================================================================================
static void* proc_sql_add_new_file(void* p_param)
{
	CFlyThreadUpdaterInfo* l_p = (CFlyThreadUpdaterInfo*)p_param;
	size_t l_count_insert;
	Lock l(l_p->m_db->m_cs_sqlite);
	if (l_p->m_file_full)
		l_p->m_db->process_sql_add_new_fileL(*l_p->m_file_full, l_count_insert);
	if (l_p->m_file_only_counter)
		l_p->m_db->process_sql_add_new_fileL(*l_p->m_file_only_counter, l_count_insert);
	if (l_p->m_id_array)
	{
		for (CFlyIDArray::const_iterator i = l_p->m_id_array->begin(); i != l_p->m_id_array->end(); ++i)
		{
			l_p->m_db->inc_counter_fly_file_bulkL(*i);
		}
	}
	delete l_p;
	return NULL;
}
//========================================================================================================
bool zlib_compress(const char* p_source, size_t p_len, std::vector<unsigned char>& p_compress, int& p_zlib_result, int p_level /*= 9*/)
{
	unsigned long l_dest_length = 0;
	l_dest_length =  compressBound(p_len) + 2;
	p_compress.resize(l_dest_length);
	p_zlib_result = compress2(p_compress.data(), &l_dest_length, (uint8_t*)p_source, p_len, p_level);
	if (p_zlib_result == Z_OK) // TODO - Check memory
	{
#ifdef _DEBUG
		if (l_dest_length)
		{
			std::cout << std::endl << "Compress  zlib size " << p_len << "/" << l_dest_length << std::endl;
		}
#endif
		p_compress.resize(l_dest_length);
	}
	else
	{
		p_compress.clear();
	}
	return !p_compress.empty();
}
//========================================================================================================
bool zlib_uncompress(const uint8_t* p_zlib_source, size_t p_zlib_len, std::vector<unsigned char>& p_decompress)
{
	unsigned long l_decompress_size = p_zlib_len * 3;
	if (p_zlib_len >= 2 && p_zlib_source[0] == 0x78) // zlib контент?
		// && (unsigned char)p_zlib_source[1] == 0x9C  этот код может быть другим
	{
		// Уровень компрессии - код второго байта (методом подбора)
		// 1     - 0x01
		// [2-5] - 0x5e
		// 6 - 0x9c
		// [7-9] - 0xda
//			#ifdef _DEBUG
//					char l_dump_zlib_debug[10] = {0};
//					sprintf(l_dump_zlib_debug, "%#x", (unsigned char)l_post_data[1] & 0xFF);
//				    std::cout << "DEBUD zlib decompress header l_post_data[1] = " << l_dump_zlib_debug << std::endl;
//			#endif

		p_decompress.resize(l_decompress_size);
		while (1)
		{
			const int l_un_compress_result = uncompress(p_decompress.data(), &l_decompress_size, p_zlib_source, p_zlib_len);
			if (l_un_compress_result == Z_BUF_ERROR)
			{
				l_decompress_size *= 2;
				p_decompress.resize(l_decompress_size);
				continue;
			}
			if (l_un_compress_result == Z_OK)
			{
				p_decompress.resize(l_decompress_size);
			}
			else
			{
				p_decompress.clear(); // Если ошибка - зачистим данные. размер мссива является флажком.
				// TODO оптимизнуть и подменить входной вектор.
				
				std::cout << "Error zlib_uncompress: code = " << l_un_compress_result << std::endl;
#ifndef _WIN32
				syslog(LOG_NOTICE, "Error zlib_uncompress: code = %d", l_un_compress_result);
#endif
			}
			break;
		};
	}
	return !p_decompress.empty();
}
//========================================================================================================
void CDBManager::pragma_executor(const char* p_pragma)
{
	static const char* l_db_name[] =
	{
		"main"
	};
	for (int i = 0; i < sizeof(l_db_name) / sizeof(l_db_name[0]); ++i)
	{
		string l_sql = "pragma ";
		l_sql += l_db_name[i];
		l_sql += '.';
		l_sql += p_pragma;
		l_sql += ';';
		m_flySQLiteDB.executenonquery(l_sql);
	}
}
//========================================================================================================
bool CDBManager::safeAlter(const char* p_sql)
{
	try
	{
		m_flySQLiteDB.executenonquery(p_sql);
		return true;
	}
	catch (const database_error& e)
	{
		if (e.getError().find("duplicate column name") == string::npos)
		{
			printf("safeAlter: %s", e.getError().c_str());
#ifndef _WIN32
			syslog(LOG_NOTICE, "CDBManager::safeAlter = %s", e.getError().c_str());
#endif
		}
	}
	return false;
}
//========================================================================================================
void CDBManager::errorDB(const string& p_txt, bool p_is_exit_process/* = false */)
{
#ifndef _WIN32
	syslog(LOG_NOTICE, "CDBManager::errorDB = %s", p_txt.c_str());
#endif
	printf("[sql] errorDB - %s\r\n", p_txt.c_str());
	dcdebug("[sql] errorDB - %s\r\n", p_txt.c_str()); // Всегда логируем в файл (т.к. база может быть битой)
	if (p_is_exit_process)
	{
		exit(-1);
	}
}
//========================================================================================================
static string toHexEscape(char val)
{
	char buf[sizeof(int) * 2 + 1 + 1];
	snprintf(buf, sizeof(buf), "%%%X", val & 0x0FF);
	return buf;
}
static char fromHexEscape(const string &aString)
{
	unsigned int res = 0;
	if (sscanf(aString.c_str(), "%X", &res) == EOF)
	{
		// TODO log error!
	}
	return static_cast<char>(res);
}
//========================================================================================================
static void trace_callback(void* p_udp, const char* p_sql)
{
//#ifndef _WIN32
//	syslog(LOG_NOTICE, "[sqltrace] %s", p_sql);
//#else
	char l_time_buf[64];
	l_time_buf[0] = 0;
	time_t now;
	time(&now);
	strftime(l_time_buf, sizeof(l_time_buf), "%d-%m-%Y %H:%M:%S", gmtime(&now));
	
	printf(" [%s] [sqltrace] sql = %s\r\n", l_time_buf, p_sql);
//#endif
}
//========================================================================================================
bool CDBManager::is_table_exists(const string& p_table_name)
{
	return m_flySQLiteDB.executeint(
	           "select count(*) from sqlite_master where type = 'table' and lower(tbl_name) = '" + p_table_name + "'") != 0;
}
//========================================================================================================
void CDBManager::init()
{
#ifndef _WIN32
	openlog("fly-server-benchmark", 0, LOG_USER); // LOG_PID
	syslog(LOG_NOTICE, "CDBManager init");
#endif
	try
	{
#ifdef FLY_SERVER_USE_LEVELDB
//    m_flyLevelDB = auto_ptr<CFlyLevelDB>(new CFlyLevelDB(leveldb::kNoCompression));
//		m_flyLevelDB->open_level_db("media-db.leveldb");

		m_flyLevelDBCompress = auto_ptr<CFlyLevelDB>(new CFlyLevelDB(leveldb::kSnappyCompression));
		m_flyLevelDBCompress->open_level_db("media-db-compress.leveldb");
		
#endif
		
		const string l_db_name = "fly-server-db.sqlite";
		std::ifstream l_old_db_file("FlylinkDCServer.sqlite");
		if (l_old_db_file.good())
		{
			l_old_db_file.close();
			if (rename("FlylinkDCServer.sqlite", l_db_name.c_str()))
			{
				std::cout << "Error rename " << l_db_name << " error code = " << errno << std::endl;
				exit(-1);
			}
		}
		m_flySQLiteDB.open(l_db_name.c_str());
		
		m_flySQLiteDB.setbusytimeout(5000); //  5 Сек
		
#ifdef FLY_SERVER_SQL_TRACE
#ifdef _DEBUG
#ifdef _WIN32
		sqlite3_trace(m_flySQLiteDB.get_db(), trace_callback, NULL);
#endif
#endif
#endif
		pragma_executor("page_size=4096");
		{
			pragma_executor("journal_mode=WAL");
		}
		pragma_executor("temp_store=MEMORY");
		pragma_executor("count_changes=OFF");
		
		extern int  g_sqlite_cache_db;
		const string l_pragma_cache =  toString(g_sqlite_cache_db * 1024 * 1024 / 4096);
		
		m_flySQLiteDB.executenonquery("PRAGMA cache_size=" + l_pragma_cache);
		m_flySQLiteDB.executenonquery("PRAGMA auto_vacuum=FULL");
		
	
		m_flySQLiteDB.executenonquery("CREATE TABLE IF NOT EXISTS fly_file ("
		                              "id             integer primary key AUTOINCREMENT not null,"
		                              "tth            char(39) not null,"
		                              "file_size      NUMBER not null,"
		                              "count_plus     NUMBER default 0 not null,"
		                              "count_minus    NUMBER default 0 not null,"
		                              "count_fake     NUMBER default 0 not null,"
		                              "count_download NUMBER default 0 not null,"
		                              "count_upload   NUMBER default 0 not null,"
		                              "count_query    NUMBER default 1 not null,"
		                              "first_date     int64 not null,"
		                              "last_date      int64,"
		                              "fly_audio      text,"
		                              "fly_video      text,"
		                              "fly_audio_br   text,"
		                              "fly_xy         text,"
		                              "count_media    NUMBER,"
									  "count_antivirus NUMBER default 0 not null"
		                              ");");
		safeAlter("ALTER TABLE fly_file add column count_media NUMBER");
		safeAlter("ALTER TABLE fly_file add column count_antivirus NUMBER default 0 not null");
		
		m_flySQLiteDB.executenonquery("CREATE UNIQUE INDEX IF NOT EXISTS iu_fly_file_tth ON fly_file(TTH,FILE_SIZE);");
		m_flySQLiteDB.executenonquery(
		    "CREATE TABLE IF NOT EXISTS fly_registry(segment integer not null, key text not null,val_str text, val_number int64,tick_count int not null);");
		m_flySQLiteDB.executenonquery("CREATE UNIQUE INDEX IF NOT EXISTS "
		                              "iu_fly_registry_key ON fly_registry(segment,key);");
		                              
	}
	catch (const database_error& e)
	{
		errorDB("SQLite: CDBManager::CDBManager" + e.getError());
	}
}
//========================================================================================================
void CDBManager::process_get_query(const size_t p_index_result,
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
                                   size_t& p_only_ext_info_counter)
{
	bool l_is_only_ext_info = p_is_all_only_ext_info;
	if (p_is_different_ext_info == true &&  l_is_only_ext_info == false)
		l_is_only_ext_info = p_cur_item_in["only_ext_info"].asInt() == 1; // Расширенная инфа нужна для этого элемента?
	if (l_is_only_ext_info)
		++p_only_ext_info_counter;
	//  CFlyMediaAttrArray l_attr_array;
	Json::Value& l_cur_item_out = p_result_arrays[int(p_index_result)];
	Json::Value l_base_mediainfo;
	
	if (p_is_only_counter == false) // Если запросили только счетчики для группы или всех, то не обращаемся к базе атрибутов
	{
		//dcassert(l_is_only_ext_info == false && p_file_record->m_audio.empty());
		if (p_file_record && l_is_only_ext_info == false)
		{
			// Есть уже ранее загруженная запись
			
			if (!p_file_record->m_media.m_audio.empty())
				l_base_mediainfo["fly_audio"] = p_file_record->m_media.m_audio;
			if (!p_file_record->m_media.m_audio_br.empty())
				l_base_mediainfo["fly_audio_br"] = p_file_record->m_media.m_audio_br;
			if (!p_file_record->m_media.m_video.empty())
				l_base_mediainfo["fly_video"] = p_file_record->m_media.m_video;
			if (!p_file_record->m_media.m_xy.empty())
				l_base_mediainfo["fly_xy"] = p_file_record->m_media.m_xy;
		}
		else
		{
			if (l_is_only_ext_info)
			{
				// Забираем расширенные атрибуты из LevelDB
				//get_ext_media_attr_array(p_id_file, l_attr_array);
				string l_json_ext_info;
				if (m_flyLevelDBCompress->get_value(p_tth, l_json_ext_info))
				{
				
					Json::Value l_root;
					Json::Reader reader(Json::Features::strictMode());
					const bool parsingSuccessful = reader.parse(l_json_ext_info, l_root);
					if (!parsingSuccessful)
					{
						std::cout  << "Failed to parse json configuration: l_json -> fly-server-error.log" << std::endl;
					}
					else
					{
						l_cur_item_out = l_root; // TODO - лишний
					}
				}
				
#ifndef _WIN32 // TODO debug
				syslog(LOG_NOTICE, "get_ext_media_attr_array (Inform) = %u", (unsigned)p_id_file);
#else
				std::cout << "get_ext_media_attr_array (Inform) p_id_file = " << p_id_file << std::endl;
#endif
			}
		}
	}
	// get_ext_mediainfo_as_json(l_attr_array, l_cur_item_out, l_is_only_ext_info);
	
	l_cur_item_out["info"]  = p_result_counter;
	l_cur_item_out["tth"]   = p_tth;
	l_cur_item_out["size"]  = p_size_str;
	if (!l_base_mediainfo.empty())
	{
		l_cur_item_out["media"] = l_base_mediainfo;
	}
}
//========================================================================================================
std::string CDBManager::store_media_info(CFlyServerContext& p_flyserver_cntx)
{
	string l_res_stat;
	try
	{
		const bool l_is_get = p_flyserver_cntx.m_query_type == FLY_POST_QUERY_GET;
		if (!l_is_get)
		{
			std::cout << "Error find metod fly-* or fly-get / fly-set" << std::endl;
		}
		Json::Value l_root;
		Json::Reader reader(Json::Features::strictMode());
		const std::string l_json = p_flyserver_cntx.m_in_query;
		const bool parsingSuccessful = reader.parse(l_json, l_root);
		if (!parsingSuccessful)
		{
			std::cout  << "Failed to parse json configuration: l_json -> fly-server-error.log" << std::endl;
			std::fstream l_out("fly-server-error.log", std::ios_base::out);
			l_out << l_json << std::endl;
			return "Failed to parse json configuration";
		}
		else
		{
#ifdef _DEBUG
			std::cout << "[OK - in] ";
			//std::fstream l_out("fly-server-OK-in-debug.log", std::ios_base::out);
			//l_out << l_root;
#endif
		}
		// Входной JSON-запрос
		// Распарсим общие значения
		bool l_is_all_only_ext_info  =  false;
		bool l_is_different_ext_info =  false;
		bool l_is_different_counter  =  false;
		p_flyserver_cntx.m_count_cache = 0;
		const bool l_is_all_only_counter   =  l_root["only_counter"].asInt() == 1; // Для всего блока нужны только счетчики?
		if (l_is_all_only_counter == false)
		{
			l_is_different_ext_info =  l_root["different_ext_info"].asInt() == 1; // В запросе есть желание получить и полную и не полную инфу
			// - при формировании выборки провести анализ признака only_ext_info для каждого файла.
			if (l_is_different_ext_info == false)
			{
				l_is_all_only_ext_info  =  l_root["only_ext_info"].asInt() == 1; // Расширенная инфа не нужна для всего блока?
				if (l_is_all_only_ext_info == false)
				{
					l_is_different_counter =  l_root["different_counter"].asInt() == 1; // Счетчики нужны только некоторым записям?
					if (l_is_different_counter)
						p_flyserver_cntx.m_count_cache = l_root["cache"].asInt();
				}
			}
		}
		// Обходим массив
		const Json::Value& l_array = l_root["array"];
		std::auto_ptr<CFlyFileRecordMap> l_sql_result_array(new CFlyFileRecordMap);
		std::auto_ptr<CFlyFileRecordMap> l_sql_result_array_only_counter(new CFlyFileRecordMap);
		prepare_and_find_all_tth(
		    l_root,
		    l_array,
		    *l_sql_result_array, // Результат запросов со счетчиком медиаинфы
		    *l_sql_result_array_only_counter, // Результат запросов только со счетчиком рейтингов
		    l_is_all_only_counter,
		    l_is_all_only_ext_info,
		    l_is_different_ext_info,
		    l_is_different_counter,
		    p_flyserver_cntx.m_count_insert
		);
		p_flyserver_cntx.m_count_get_only_counter = l_sql_result_array_only_counter->size();
		p_flyserver_cntx.m_count_get_base_media_counter = l_sql_result_array->size();
		p_flyserver_cntx.m_count_file_in_json = l_array.size();
		Json::Value  l_result_root; // Выходной JSON-пакет
		Json::Value& l_result_arrays = l_result_root["array"];
		std::auto_ptr<CFlyIDArray> l_id_file_array_ptr(new CFlyIDArray);
		size_t l_index_result = 0;
		for (int j = 0; j < p_flyserver_cntx.m_count_file_in_json; ++j)
		{
			const Json::Value& l_cur_item_in = l_array[j];
			bool l_is_only_counter = l_is_all_only_counter;
			if (l_is_get)
			{
				if (l_is_different_counter == true && l_is_only_counter == false)
					l_is_only_counter = l_cur_item_in["only_counter"].asInt() == 1; // Только счетчики нужны для этого элемента?
			}
			const string l_size_str = l_cur_item_in["size"].asString();
			int64_t l_size = 0;
			if (!l_size_str.empty())
				l_size = _atoi64(l_size_str.c_str());
			else
			{
#ifndef _WIN32
				syslog(LOG_NOTICE, "l_cur_item_in[size] is null!");
#endif
			}
			const string l_tth = l_cur_item_in["tth"].asString();
			
			// Попытка найти данные в массиве - уменьшаем кол-во SQL выборок для запроса по чтению
			const CFlyFileKey l_tth_key(l_tth, l_size);
			const CFlyFileRecord* l_cur_record = NULL;
			if (l_is_get)
			{
				CFlyFileRecordMap::const_iterator l_find_mediainfo = l_sql_result_array->find(l_tth_key);
				if (l_find_mediainfo != l_sql_result_array->end()) // Найдем инфу сначала в массиве с медиаинфой
				{
					l_cur_record = &l_find_mediainfo->second;
				}
				else
				{
					CFlyFileRecordMap::const_iterator l_find_counter = l_sql_result_array_only_counter->find(l_tth_key); // Если не нашли, поищем по каунтерам
					if (l_find_counter != l_sql_result_array_only_counter->end())
					{
						l_cur_record = &l_find_counter->second;
					}
				}
			}
			Json::Value  l_result_counter;
			int64_t l_count_query = 0;
			int64_t l_count_download = 0;
			int64_t l_count_antivirus = 0;
			int64_t l_id_file     = 0;
			if (l_cur_record && l_cur_record->m_fly_file_id) // Нашли информацию массовым запросом - без дополнительного SQL формируем ответ?
			{
				l_id_file     = l_cur_record->m_fly_file_id;
				l_count_query = l_cur_record->m_count_query;
				l_count_download = l_cur_record->m_count_download;
				l_count_antivirus = l_cur_record->m_count_antivirus;
				dcassert(l_id_file);
				if (l_cur_record->m_count_download)
					l_result_counter["count_download"] = toString(l_cur_record->m_count_download);
				if (l_cur_record->m_count_antivirus)
					l_result_counter["count_antivirus"] = toString(l_cur_record->m_count_antivirus);
				if (l_cur_record->m_count_query)
					l_result_counter["count_query"] = toString(l_cur_record->m_count_query);
				if (l_cur_record->m_count_mediainfo)
					l_result_counter["count_media"] = toString(l_cur_record->m_count_mediainfo);
				if (l_cur_record->m_is_new_file == true) // Если файл новый скинем ему счетчик (чтобы не шел update +1)
					l_count_query = 0;
			}
			else
			{
				l_cur_record = NULL;
				// Если не нашли в кэше - обращаемся старым вариантом к базе данных
				l_id_file = find_tth(l_tth,
				                     l_size,
				                     true,
				                     l_result_counter,
				                     l_is_get,
				                     (l_is_only_counter == true) ? true : false,
				                     l_count_query,
				                     l_count_download,
									 l_count_antivirus,
				                     p_flyserver_cntx.m_count_insert
				                    );
			}
			// Если запроc типа get + заказывают "только счетчики", то мы находимся в файл-листе
			// Дополнительно расчитаем сколько данных у нас лежит в медиаинфе и вернем назад на клиент для анализа
			// если будет возможность клиент нам докинет медиаинформацию для пополнения базы
			if (l_is_get && l_id_file && l_count_query) // Если файл уже был сохраним его ID для массового апдейта в одной траназакции
			{
				const int lc_batch_limit = 50;
				if (l_id_file_array_ptr->empty() || (l_id_file_array_ptr->back().size() % lc_batch_limit) == 0)
				{
					l_id_file_array_ptr->push_back(std::vector<int64_t>());
					l_id_file_array_ptr->back().reserve(lc_batch_limit);
				}
				l_id_file_array_ptr->back().push_back(l_id_file);
			}
            if (l_is_get)
			{
				process_get_query(l_index_result++, // TODO оптимизировать при случаях если l_cur_record найден.
				                  l_tth,
				                  l_size_str,
				                  l_id_file,
				                  l_cur_item_in,
				                  l_result_arrays,
				                  l_result_counter,
				                  l_is_all_only_ext_info,
				                  l_is_different_ext_info,
				                  l_is_all_only_counter,
				                  l_is_different_counter,
				                  l_is_only_counter,
				                  l_cur_record,
				                  p_flyserver_cntx.m_count_get_ext_media_counter);
			}
		}
		if (l_is_get)
		{
			if (l_index_result) // Генерим если есть ответ из базы
			{
				l_res_stat = l_result_root.toStyledString();
			}
			else
			{
#ifdef _DEBUG
				std::cout << "[l_result_root.empty()]" << std::endl;
#endif
			}
		}
		// Финализируеся
		// 1. Инкрементируем счетчики доступа
		// 2. Элементы не найденные в мапе вставляем в базу данных с каунтером = 1
		// Признаком нового файла является m_fly_file_id = 0;
		const bool l_is_new_file_only_counter = l_sql_result_array_only_counter->is_new_file();
		const bool l_is_new_file = l_sql_result_array->is_new_file();
		if (!l_id_file_array_ptr->empty() || l_is_new_file_only_counter || l_is_new_file)
		{
			CFlyThreadUpdaterInfo* l_thread_param = new CFlyThreadUpdaterInfo;
			l_thread_param->m_db = this;
			if (l_is_new_file)
				l_thread_param->m_file_full = l_sql_result_array.release();
			if (l_is_new_file_only_counter)
				l_thread_param->m_file_only_counter = l_sql_result_array_only_counter.release();
			if (!l_id_file_array_ptr->empty())
				l_thread_param->m_id_array = l_id_file_array_ptr.release();
			if (!mg_start_thread(proc_sql_add_new_file, l_thread_param)) // TODO - поток обединить с proc_inc_counter_thread
			{
				delete l_thread_param;
			}
		}
	}
	catch (const database_error& e)
	{
		p_flyserver_cntx.m_error = e.getError();
		errorDB("[sqlite] store_media_info error: " + e.getError(), false);
	}
	return l_res_stat;
}
//========================================================================================================
void CDBManager::inc_counter_fly_file_bulkL(const std::vector<int64_t>& p_id_array)
{
	dcassert(p_id_array.size());
	if (p_id_array.size())
	{
#ifdef FLY_SERVER_USE_ARRAY_UPDATE
		sqlite3_command* l_sql_command = NULL;
		CFlyCacheSQLCommandInt& l_pool_sql = m_sql_cache[SQL_CACHE_UPDATE_COUNT];
		CFlyCacheSQLCommandInt::const_iterator l_sql_it = l_pool_sql.find(p_id_array.size());
		if (l_sql_it == l_pool_sql.end())
		{
			string l_sql_text =  "update fly_file set count_query=count_query+1"
#ifdef FLY_SERVER_USE_LAST_DATE_FIELD
				", last_date=strftime('%s','now','localtime')"
#endif
				" where id in(?";
			for (unsigned i = 1; i < p_id_array.size(); ++i)
			{
				l_sql_text += ",?";
			}
			l_sql_text += ")";
			l_sql_command = new sqlite3_command(m_flySQLiteDB, l_sql_text);
			l_pool_sql.insert(std::make_pair(p_id_array.size(), l_sql_command));
		}
		else
			l_sql_command = l_sql_it->second;
		for (unsigned i = 0; i < p_id_array.size(); ++i)
		{
			l_sql_command->bind(i + 1, (long long int)p_id_array[i]);
		}
		l_sql_command->executenonquery();
#else
		
			if (!m_update_inc_count_query.get())
				m_update_inc_count_query = auto_ptr<sqlite3_command>(new sqlite3_command(m_flySQLiteDB,
					"update fly_file set count_query=count_query+1"
#ifdef FLY_SERVER_USE_LAST_DATE_FIELD
					",last_date=strftime('%s','now','localtime')"
#endif
					" where id=?"));
		sqlite3_command* l_sql = m_update_inc_count_query.get();
		for (unsigned i = 0; i < p_id_array.size(); ++i)
		{
			l_sql->bind(1,p_id_array[i]);
			l_sql->executenonquery();
		}
#endif

	}
}
//========================================================================================================
sqlite3_command* CDBManager::bind_sql_counter(const CFlyFileRecordMap& p_sql_array,
                                              bool p_is_get_base_mediainfo)
{
	sqlite3_command* l_sql_command = NULL;
	const size_t l_size_array = p_sql_array.size();
	// Шаг 0. Генерируем SQL запрос для выборки группы значений по ключевой паре TTH + Size.
	CFlyCacheSQLCommandInt& l_pool_sql = p_is_get_base_mediainfo ? m_sql_cache[SQL_CACHE_SELECT_COUNT_MEDIA] : m_sql_cache[SQL_CACHE_SELECT_COUNT];
	CFlyCacheSQLCommandInt::const_iterator l_sql_it = l_pool_sql.find(l_size_array);
	if (l_sql_it == l_pool_sql.end())
	{
		// TODO -
		string l_sql_text =  "select tth,file_size,id,count_query,count_download,count_media,count_antivirus";
		if (p_is_get_base_mediainfo) // Добавим загрузку медиаинфы?
		{
			l_sql_text += ",fly_audio,fly_video,fly_audio_br,fly_xy";
		}
		l_sql_text += " from fly_file where\r\n   (tth=? and file_size=?)";
		for (size_t i = 1; i < l_size_array; ++i)
		{
			l_sql_text += "\r\nor (tth=? and file_size=?)";
		}
		l_sql_command = new sqlite3_command(m_flySQLiteDB, l_sql_text);
		l_pool_sql.insert(std::make_pair(l_size_array, l_sql_command));
	}
	else
		l_sql_command = l_sql_it->second;
	// Шаг 1. биндим пары переменных TTH+size
	size_t l_bind_index = 1;
	for (CFlyFileRecordMap::const_iterator i = p_sql_array.begin(); i != p_sql_array.end(); ++i)
	{
		l_sql_command->bind(l_bind_index++, i->first.m_tth, SQLITE_STATIC);
		l_sql_command->bind(l_bind_index++, i->first.m_file_size);
	}
	return l_sql_command;
}
//========================================================================================================
void CDBManager::prepare_insert_fly_file()
{
	if (!m_insert_fly_file.get())
	{
		m_insert_fly_file = auto_ptr<sqlite3_command>(new sqlite3_command(m_flySQLiteDB,
			"insert into fly_file (tth,file_size,first_date) values(?,?,strftime('%s','now','localtime'))"));
	}
	if (!m_insert_or_replace_fly_file.get())
	{		
		m_insert_or_replace_fly_file = auto_ptr<sqlite3_command>(new sqlite3_command(m_flySQLiteDB,
			"insert or replace into fly_file (tth,file_size,first_date) values(?,?,strftime('%s','now','localtime'))")); 
	}
}
//========================================================================================================
long long CDBManager::internal_insert_fly_file(const string& p_tth,sqlite_int64 p_file_size)
{
	try
	{
		sqlite3_command* l_ins_sql = m_insert_fly_file.get();
		l_ins_sql->bind(1, p_tth.c_str(), 39, SQLITE_STATIC);
		l_ins_sql->bind(2, p_file_size);
		l_ins_sql->executenonquery();
	}
	catch (const database_error& e)
	{
		sqlite3_command* l_ins_sql = m_insert_or_replace_fly_file.get();
		l_ins_sql->bind(1, p_tth.c_str(), 39, SQLITE_STATIC);
		l_ins_sql->bind(2, p_file_size);
		l_ins_sql->executenonquery();
	}
	return m_flySQLiteDB.insertid();
}
//========================================================================================================
void CDBManager::internal_process_sql_add_new_fileL(size_t& p_count_insert)
{
		prepare_insert_fly_file();
		Lock l(m_cs_new_file);
		for (std::set<CFlyFileKey>::const_iterator j = m_new_file_array.cbegin(); j != m_new_file_array.cend(); ++j)
		{
			internal_insert_fly_file(j->m_tth, j->m_file_size);
			++p_count_insert;
		}
		m_new_file_array.clear();
}
//========================================================================================================
void CDBManager::process_sql_add_new_fileL(CFlyFileRecordMap& p_sql_array, size_t& p_count_insert)
{
	prepare_insert_fly_file();
	for (CFlyFileRecordMap::iterator i = p_sql_array.begin(); i != p_sql_array.end(); ++i)
	{
		if (i->second.m_fly_file_id == 0)
		{
			i->second.m_fly_file_id = internal_insert_fly_file(i->first.m_tth, i->first.m_file_size);
			{
				Lock l(m_cs_new_file);
				m_new_file_array.erase(CFlyFileKey(i->first.m_tth, i->first.m_file_size));
			}
			i->second.m_count_query = 1;
			i->second.m_count_download = 0;
			i->second.m_count_antivirus = 0;
			i->second.m_is_new_file = true;
			++p_count_insert;
		}
	}
	internal_process_sql_add_new_fileL(p_count_insert);
}
//========================================================================================================
void CDBManager::process_sql_counter(CFlyFileRecordMap& p_sql_array,
                                     bool p_is_get_base_mediainfo,
                                     size_t& p_count_insert)
{

	const size_t l_size_array = p_sql_array.size();
	if (l_size_array > 0)
	{
		Lock l(m_cs_sqlite);
		sqlite3_command* l_sql_command = bind_sql_counter(p_sql_array, p_is_get_base_mediainfo);
		// Шаг 2. Возвращаем результат в виде курсора и сохраняем обратно в мапе
		sqlite3_reader l_q = l_sql_command->executereader();
		while (l_q.read())
		{
			const CFlyFileKey l_tth_key(l_q.getstring(0), l_q.getint64(1));
			CFlyFileRecord& l_rec = p_sql_array[l_tth_key];
			l_rec.m_fly_file_id = l_q.getint64(2);
			l_rec.m_count_query = l_q.getint64(3) + 1; // +1 Т.к. следующей командой пойдет апдейт на инкремент этого параметра
			l_rec.m_count_download = l_q.getint64(4);
			// Кол-во медиаинфы считаем всегда (не сильно накладно) TODO - после анализа версии клиента прикрутить оптимизацию и этой части
			l_rec.m_count_mediainfo = l_q.getint(5);
			l_rec.m_count_antivirus = l_q.getint(6);
			if (p_is_get_base_mediainfo)
			{
				l_rec.m_media.m_audio = l_q.getstring(7);
				if (!l_rec.m_media.m_audio.empty())
					l_rec.m_media.m_audio_br = l_q.getstring(9);
				l_rec.m_media.m_video = l_q.getstring(8);
				if (!l_rec.m_media.m_video.empty())
					l_rec.m_media.m_xy = l_q.getstring(10);
			}
		}
	}
}
//========================================================================================================
void CDBManager::prepare_and_find_all_tth(
    const Json::Value& p_root,
    const Json::Value& p_array,
    CFlyFileRecordMap& p_sql_result_array, // Результат запросов со счетчиком медиаинфы
    CFlyFileRecordMap& p_sql_result_array_only_counter, // Результат запросов только со счетчиком рейтингов
    bool p_is_all_only_counter,
    bool p_is_all_only_ext_info,
    bool p_is_different_ext_info,
    bool p_is_different_counter,
    size_t& p_count_insert
)
{
	size_t l_count_file_in_json = p_array.size();
	// Обходим входной массив TTH и подготавливаем массив биндинга для последующего исполнения в sql
	for (int j = 0; j < l_count_file_in_json; ++j)
	{
		const Json::Value& l_cur_item_in = p_array[j];
		bool l_is_only_ext_info = p_is_all_only_ext_info;
		if (p_is_different_ext_info == true &&  l_is_only_ext_info == false)
			l_is_only_ext_info = l_cur_item_in["only_ext_info"].asInt() == 1; // Расширенная инфа не нужна для этого элемента?
		bool l_is_only_counter = p_is_all_only_counter;
		if (p_is_different_counter == true && l_is_only_counter == false)
			l_is_only_counter = l_cur_item_in["only_counter"].asInt() == 1; // Только счетчики нужны для этого элемента?
		const CFlyFileKey l_tth_key(l_cur_item_in["tth"].asString(), _atoi64(l_cur_item_in["size"].asString().c_str()));
		if (l_is_only_counter)
		{
			CFlyFileRecord& l_rec = p_sql_result_array_only_counter[l_tth_key];
			l_rec.m_is_only_counter = l_is_only_counter;
		}
		else
		{
			CFlyFileRecord& l_rec = p_sql_result_array[l_tth_key];
			l_rec.m_is_only_counter = l_is_only_counter;
		}
	}
	process_sql_counter(p_sql_result_array_only_counter, false, p_count_insert);
	process_sql_counter(p_sql_result_array, true, p_count_insert);
}
//========================================================================================================
int64_t CDBManager::find_tth(const string& p_tth,
                             int64_t p_size,
                             bool p_create,
                             Json::Value& p_result_stat,
                             bool p_fill_counter,
                             bool p_calc_count_mediainfo,
                             int64_t& p_count_query,
                             int64_t& p_count_download,
							 int64_t& p_count_antivirus,
                             size_t& p_count_insert)
{
	string l_marker_crash;
	try
	{
		Lock l(m_cs_sqlite);
		sqlite3_command* l_sql = 0;
		if (p_calc_count_mediainfo == false)
		{
			if (!m_find_tth.get())
				m_find_tth = auto_ptr<sqlite3_command>(new sqlite3_command(m_flySQLiteDB,
				                                                           "select id,count_query,count_download,count_antivirus "
#ifdef FLY_SERVER_USE_ALL_COUNTER
				                                                           ",count_plus,count_minus,count_fake,count_upload,first_date"
#endif
				                                                           " from fly_file where file_size=? and tth=?"));
			l_sql = m_find_tth.get();
		}
		else
		{
			if (!m_find_tth_and_count_media.get())
				m_find_tth_and_count_media = auto_ptr<sqlite3_command>(new sqlite3_command(m_flySQLiteDB,
				                                                                           "select id,count_query,count_download,count_antivirus,count_media"
#ifdef FLY_SERVER_USE_ALL_COUNTER
				                                                                           ",count_plus,count_minus,count_fake,count_upload,first_date"
#endif
				                                                                           " from fly_file where file_size=? and tth=?"));
			l_sql = m_find_tth_and_count_media.get();
		}
		l_sql->bind(1, (long long int)p_size);
		l_sql->bind(2, p_tth.c_str(), 39, SQLITE_STATIC);
		int64_t l_id = 0;
#ifdef FLY_SERVER_USE_ALL_COUNTER
		int64_t l_count_plus = 0;
		int64_t l_count_minus = 0;
		int64_t l_count_fake = 0;
		int64_t l_count_upload = 0;
		int64_t l_first_date = 0;
#endif  // FLY_SERVER_USE_ALL_COUNTER
		p_count_query = 0;
		p_count_download = 0;
		p_count_antivirus = 0;
		int l_count_mediainfo = 0;
		// string l_marker_crash = "[+] select from fly_file"; // TODO - Убрать в _DEBUG
		{
			sqlite3_reader l_q = l_sql->executereader();
			if (l_q.read())
			{
				l_id = l_q.getint64(0);
				p_count_query    = l_q.getint64(1);
				p_count_download = l_q.getint64(2);
				p_count_antivirus = l_q.getint64(3);
				if (p_calc_count_mediainfo)
					l_count_mediainfo = l_q.getint(4);
#ifdef FLY_SERVER_USE_ALL_COUNTER
				if (p_calc_count_mediainfo)
				{
					l_count_plus = l_q.getint64(5);
					l_count_minus = l_q.getint64(6);
					l_count_fake = l_q.getint64(7);
					l_count_upload = l_q.getint64(8);
					l_first_date = l_q.getint64(9);
				}
				else
				{
					l_count_plus = l_q.getint64(3);
					l_count_minus = l_q.getint64(4);
					l_count_fake = l_q.getint64(5);
					l_count_upload = l_q.getint64(6);
					l_first_date = l_q.getint64(7);
				}
#endif  // FLY_SERVER_USE_ALL_COUNTER
			}
		}
		const bool l_is_first_tth = !l_id && p_create;
		if (l_is_first_tth)
		{
			Lock l(m_cs_new_file);
			//prepare_insert_fly_file();
			m_new_file_array.insert(CFlyFileKey(p_tth, p_size));
			//l_id = internal_insert_fly_file(p_tth, p_size);
			++p_count_insert;
		}
		++p_count_query;
		if (p_fill_counter)
		{
#ifdef FLY_SERVER_USE_ALL_COUNTER
			if (l_count_plus)
				p_result_stat["count_plus"] = toString(l_count_plus);
			if (l_count_minus)
				p_result_stat["count_minus"] = toString(l_count_minus);
			if (l_count_fake)
				p_result_stat["count_fake"] = toString(l_count_fake);
			if (l_count_upload)
				p_result_stat["count_upload"] = toString(l_count_upload);
#endif // FLY_SERVER_USE_ALL_COUNTER
			if (p_count_download)
				p_result_stat["count_download"] = toString(p_count_download);
			if (p_count_antivirus)
				p_result_stat["count_antivirus"] = toString(p_count_antivirus);
			if (p_count_query)
				p_result_stat["count_query"] = toString(p_count_query);
			// TODO - пока дата регистрации на клиенте не визуализируeтся
			// if(l_first_date)
			// p_result_stat["first_date"] = toString(l_first_date);
		}
		if (l_is_first_tth)
		{
			p_count_query = 0; // Первый раз скидываем счетчик в 0. TODO - отрефакторить
		}
		if (p_calc_count_mediainfo)
		{
			p_result_stat["count_media"] = toString(l_count_mediainfo);
		}
		return l_id;
	}
	catch (const database_error& e)
	{
		errorDB("SQLite - find_tth: l_marker_crash = " + l_marker_crash + " error = " + e.getError());
	}
	return 0;
}
//========================================================================================================
void CDBManager::load_registry(TStringList& p_values, int p_Segment)
{
	p_values.clear();
	CFlyRegistryMap l_values;
	Lock l(m_cs_sqlite);
	load_registry(l_values, p_Segment);
	p_values.reserve(l_values.size());
	for (CFlyRegistryMap::const_iterator k = l_values.begin(); k != l_values.end(); ++k)
		p_values.push_back(k->first);
}
//========================================================================================================
void CDBManager::save_registry(const TStringList& p_values, int p_Segment)
{
	CFlyRegistryMap l_values;
	Lock l(m_cs_sqlite);
	for (TStringList::const_iterator i = p_values.begin(); i != p_values.end(); ++i)
		l_values.insert(CFlyRegistryMap::value_type(
		                    *i,
		                    CFlyRegistryValue()));
	save_registry(l_values, p_Segment);
}
//========================================================================================================
void CDBManager::load_registry(CFlyRegistryMap& p_values, int p_Segment)
{
	Lock l(m_cs_sqlite);
	try
	{
		if (!m_get_registry.get())
			m_get_registry = auto_ptr<sqlite3_command>(new sqlite3_command(m_flySQLiteDB,
			                                                               "select key,val_str,val_number from fly_registry where segment=?"));
		m_get_registry->bind(1, p_Segment);
		sqlite3_reader l_q = m_get_registry.get()->executereader();
		while (l_q.read())
			p_values.insert(CFlyRegistryMap::value_type(
			                    l_q.getstring(0),
			                    CFlyRegistryValue(l_q.getstring(1), l_q.getint64(2))));
	}
	catch (const database_error& e)
	{
		errorDB("SQLite - load_registry: " + e.getError());
	}
}
//========================================================================================================
void CDBManager::save_registry(const CFlyRegistryMap& p_values, int p_Segment)
{
	const sqlite_int64 l_tick = get_tick_count();
	Lock l(m_cs_sqlite);
	try
	{
		sqlite3_transaction l_trans(m_flySQLiteDB);
		if (!m_insert_registry.get())
			m_insert_registry = auto_ptr<sqlite3_command>(new sqlite3_command(m_flySQLiteDB,
			                                                                  "insert or replace into fly_registry (segment,key,val_str,val_number,tick_count) values(?,?,?,?,?)"));
		sqlite3_command* l_sql = m_insert_registry.get();
		for (CFlyRegistryMap::const_iterator k = p_values.begin(); k != p_values.end(); ++k)
		{
			l_sql->bind(1, p_Segment);
			l_sql->bind(2, k->first, SQLITE_TRANSIENT);
			l_sql->bind(3, k->second.m_val_str, SQLITE_TRANSIENT);
			l_sql->bind(4, k->second.m_val_int64);
			l_sql->bind(5, l_tick);
			l_sql->executenonquery();
		}
		if (!m_delete_registry.get())
			m_delete_registry = auto_ptr<sqlite3_command>(new sqlite3_command(m_flySQLiteDB,
			                                                                  "delete from fly_registry where segment=? and tick_count<>?"));
		m_delete_registry->bind(1, p_Segment);
		m_delete_registry->bind(2, l_tick);
		m_delete_registry.get()->executenonquery();
		l_trans.commit();
	}
	catch (const database_error& e)
	{
		errorDB("SQLite - save_registry: " + e.getError());
	}
}
//========================================================================================================
CDBManager::~CDBManager()
{
	for (int j = 0; j < SQL_CACHE_LAST; ++j)
	{
		for (CFlyCacheSQLCommandInt::iterator i = m_sql_cache[j].begin(); i != m_sql_cache[j].end(); ++i)
		{
			delete i->second;
		}
	}
	
#ifndef _WIN32
	syslog(LOG_NOTICE, "CDBManager destroy!");
	closelog();
#endif
}
#ifdef FLY_SERVER_USE_LEVELDB
//========================================================================================================
CDBManager::CFlyLevelDB::CFlyLevelDB(leveldb::CompressionType p_compression): m_db(NULL)
{
	m_readoptions.verify_checksums = true;
	m_readoptions.fill_cache = true;
	
	m_iteroptions.verify_checksums = true;
	m_iteroptions.fill_cache = false;
	
	m_writeoptions.sync      = true;
	
	m_options.compression = p_compression;
	m_options.max_open_files = 10;
	m_options.block_size = 4096;
	m_options.write_buffer_size = 1 << 20;
	m_options.block_cache = leveldb::NewLRUCache(1 * 1024); // 1M
	m_options.paranoid_checks = true;
	m_options.filter_policy = leveldb::NewBloomFilterPolicy(10);
	m_options.create_if_missing = true;
}
//========================================================================================================
CDBManager::CFlyLevelDB::~CFlyLevelDB()
{
	delete m_db;
	delete m_options.filter_policy;
	delete m_options.block_cache;
	
}
//========================================================================================================
void CDBManager::shutdown()
{
	m_flyLevelDBCompress.reset(NULL);
}
//========================================================================================================
bool CDBManager::CFlyLevelDB::open_level_db(const string& p_db_name)
{
	leveldb::Status l_status = leveldb::DB::Open(m_options, p_db_name, &m_db);
	if (!l_status.ok())
	{
		const string l_result_error = l_status.ToString();
		if (l_status.IsIOError())
		{
			errorDB("[CFlyLevelDB::open_level_db] l_status.IsIOError() = " + l_result_error);
			dcassert(0);
			// most likely there's another instance running or the permissions are wrong
//			messageF(STRING_F(DB_OPEN_FAILED_IO, getNameLower() % Text::toUtf8(ret.ToString()) % APPNAME % dbPath % APPNAME), false, true);
//			exit(0);
		}
		else
		{
			errorDB("[CFlyLevelDB::open_level_db] !l_status.IsIOError() the database is corrupted? = " + l_result_error);
			dcassert(0);
			// the database is corrupted?
			// messageF(STRING_F(DB_OPEN_FAILED_REPAIR, getNameLower() % Text::toUtf8(ret.ToString()) % APPNAME), false, false);
			// repair(stepF, messageF);
			// try it again
			//ret = leveldb::DB::Open(options, l_pdb_name, &db);
		}
	}
	return l_status.ok();
}
//========================================================================================================
bool CDBManager::CFlyLevelDB::get_value(const void* p_key, size_t p_key_len, string& p_result)
{
	dcassert(m_db);
	if (m_db)
	{
		const leveldb::Slice l_key((const char*)p_key, p_key_len);
		const leveldb::Status l_status = m_db->Get(m_readoptions, l_key, &p_result);
		if (!(l_status.ok() || l_status.IsNotFound()))
		{
			const string l_message = l_status.ToString();
			errorDB("[CFlyLevelDB::get_value] " + l_message);
		}
		dcassert(l_status.ok() || l_status.IsNotFound());
		return l_status.ok() || l_status.IsNotFound();
	}
	else
	{
		return false;
	}
}
//========================================================================================================
bool CDBManager::CFlyLevelDB::set_value(const void* p_key, size_t p_key_len, const void* p_val, size_t p_val_len)
{
	dcassert(m_db);
	if (m_db)
	{
		// Lock l (m_leveldb_cs);
		const leveldb::Slice l_key((const char*)p_key, p_key_len);
		const leveldb::Slice l_val((const char*)p_val, p_val_len);
		const leveldb::Status l_status = m_db->Put(m_writeoptions, l_key, l_val);
		if (!l_status.ok())
		{
			const string l_message = l_status.ToString();
			errorDB("[CFlyLevelDB::set_value] " + l_message);
		}
		return l_status.ok();
	}
	else
	{
		return false;
	}
}
//========================================================================================================
#endif // FLY_SERVER_USE_LEVELDB
