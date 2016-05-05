/*
 * Copyright (C) 2001-2011 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef DCPLUSPLUS_DCPP_THREAD_H
#define DCPLUSPLUS_DCPP_THREAD_H


#include "Exception.h"

#ifndef _WIN32
#include <unistd.h> // + PPA ubuntu 13.04
#include <sys/resource.h>
#include <syslog.h>
typedef int LONG;
#endif

class CFlySafeGuard
{
		volatile LONG& m_counter;
	public:
		CFlySafeGuard(volatile LONG& p_flag) : m_counter(p_flag)
		{
#ifdef _WIN32
			InterlockedIncrement(&m_counter);
#else
			__sync_add_and_fetch(&m_counter, 1);
#endif
		}
		~CFlySafeGuard()
		{
#ifdef _WIN32
			InterlockedDecrement(&m_counter);
#else
			__sync_sub_and_fetch(&m_counter, 1);
#endif
		}
		
		operator LONG() const
		{
#ifdef _WIN32
			return InterlockedExchangeAdd(&m_counter, 0);
#else
			return __sync_fetch_and_add(&m_counter, 0);
#endif
		}
};

#ifdef _WIN32
typedef DWORD pthread_t;
static pthread_t pthread_self(void)
{
	return ::GetCurrentThreadId();
}
#endif

typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
#if defined (_WIN32) || defined (__linux__)  // __FreeBSD__
//typedef signed long long int64_t;
#endif
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
#if defined (_WIN32) || defined (__linux__) // __FreeBSD__
// typedef unsigned long long uint64_t;
#endif

typedef std::vector<std::string> TStringList;


#ifdef FLYLINKDC_USE_BOOST_LOCK
typedef boost::recursive_mutex  CriticalSection;
typedef boost::detail::spinlock FastCriticalSection;
typedef boost::lock_guard<boost::recursive_mutex> Lock;
#ifdef FLYLINKDC_HE
typedef boost::lock_guard<boost::detail::spinlock> FastLock;
#else
typedef Lock FastLock;
#endif // FLYLINKDC_HE
#else
class CriticalSection
{
#ifdef _WIN32
	public:
		void lock()
		{
			EnterCriticalSection(&cs);
			dcdrun(counter++);
		}
		void unlock()
		{
			dcassert(--counter >= 0);
			LeaveCriticalSection(&cs);
		}
		explicit CriticalSection()
		{
			dcdrun(counter = 0;);
			InitializeCriticalSectionAndSpinCount(&cs, 100); // [!] IRainman: InitializeCriticalSectionAndSpinCount
		}
		~CriticalSection()
		{
			dcassert(counter == 0);
			DeleteCriticalSection(&cs);
		}
	private:
		dcdrun(volatile long counter;)
		CRITICAL_SECTION cs;
#else
	public:
		explicit CriticalSection()
		{
			pthread_mutexattr_init(&ma);
			pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_RECURSIVE);
			pthread_mutex_init(&mtx, &ma);
		}
		~CriticalSection()
		{
			pthread_mutex_destroy(&mtx);
			pthread_mutexattr_destroy(&ma);
		}
		void lock()
		{
			pthread_mutex_lock(&mtx);
		}
		void unlock()
		{
			pthread_mutex_unlock(&mtx);
		}
		pthread_mutex_t& getMutex()
		{
			return mtx;
		}
	private:
		pthread_mutex_t mtx;
		pthread_mutexattr_t ma;
#endif
};

template<class T>
class LockBase
{
	public:
		explicit LockBase(T& aCs) : cs(aCs)
		{
			cs.lock();
		}
		~LockBase()
		{
			cs.unlock();
		}
	private:
		T& cs;
};
typedef LockBase<CriticalSection> Lock;

#endif
#endif // !defined(THREAD_H)

/**
 * @file
 * $Id: Thread.h 568 2011-07-24 18:28:43Z bigmuscle $
 */
