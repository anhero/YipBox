/*
Copyright (c) 2012 Anhero Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef YB_YIP_BOX_H
#define YB_YIP_BOX_H

#include <set>
#include <list>

#if defined(YIP_BOX_PURE_ISO) || (!defined(WIN32) && !defined(__GNUG__) && !defined(YIP_BOX_USE_POSIX_THREADS))
#       define _YIP_BOX_SINGLE_THREADED
#elif defined(WIN32)
#       define _YIP_BOX_HAS_WIN32_THREADS
#       include <windows.h>
#elif defined(__GNUG__) || defined(YIP_BOX_USE_POSIX_THREADS)
#       define _YIP_BOX_HAS_POSIX_THREADS
#       include <pthread.h>
#else
#       define _YIP_BOX_SINGLE_THREADED
#endif

#ifndef YIP_BOX_DEFAULT_MT_POLICY
#       ifdef _YIP_BOX_SINGLE_THREADED
#               define YIP_BOX_DEFAULT_MT_POLICY SingleThreaded
#       else
#               define YIP_BOX_DEFAULT_MT_POLICY MultiThreadedLocal
#       endif
#endif

namespace YipBox {
	class SingleThreaded {
	public:
		SingleThreaded() {
		}

		virtual ~SingleThreaded() {
		}

		virtual void lock() {
		}

		virtual void unlock() {
		}
	};
#ifdef _YIP_BOX_HAS_WIN32_THREADS
	// The multi threading policies only get compiled in if they are enabled.
	class MultiThreadedGlobal {
	public:
		MultiThreadedGlobal() {
			static bool isinitialised = false;

			if (!isinitialised) {
				InitializeCriticalSection(get_critsec());
				isinitialised = true;
			}
		}

		MultiThreadedGlobal(const MultiThreadedGlobal &) {
		}

		virtual ~MultiThreadedGlobal() {
		}

		virtual void lock() {
			EnterCriticalSection(get_critsec());
		}

		virtual void unlock() {
			LeaveCriticalSection(get_critsec());
		}

	private:
		CRITICAL_SECTION *get_critsec() {
			static CRITICAL_SECTION g_critsec;
			return &g_critsec;
		}
	};

	class MultiThreadedLocal {
	public:
		MultiThreadedLocal() {
			InitializeCriticalSection(&m_critsec);
		}

		MultiThreadedLocal(const MultiThreadedLocal &) {
			InitializeCriticalSection(&m_critsec);
		}

		virtual ~MultiThreadedLocal() {
			DeleteCriticalSection(&m_critsec);
		}

		virtual void lock() {
			EnterCriticalSection(&m_critsec);
		}

		virtual void unlock() {
			LeaveCriticalSection(&m_critsec);
		}

	private:
		CRITICAL_SECTION m_critsec;
	};
#endif // _YIP_BOX_HAS_WIN32_THREADS

#ifdef _YIP_BOX_HAS_POSIX_THREADS
	// The multi threading policies only get compiled in if they are enabled.
	class MultiThreadedGlobal {
	public:
		MultiThreadedGlobal() {
			pthread_mutex_init(get_mutex(), NULL);
		}

		MultiThreadedGlobal(const MultiThreadedGlobal &) {
		}

		virtual ~MultiThreadedGlobal() {
		}

		virtual void lock() {
			pthread_mutex_lock(get_mutex());
		}

		virtual void unlock() {
			pthread_mutex_unlock(get_mutex());
		}

	private:
		pthread_mutex_t *get_mutex() {
			static pthread_mutex_t g_mutex;
			return &g_mutex;
		}
	};

	class MultiThreadedLocal {
	public:
		MultiThreadedLocal() {
			pthread_mutex_init(&m_mutex, NULL);
		}

		MultiThreadedLocal(const MultiThreadedLocal &) {
			pthread_mutex_init(&m_mutex, NULL);
		}

		virtual ~MultiThreadedLocal() {
			pthread_mutex_destroy(&m_mutex);
		}

		virtual void lock() {
			pthread_mutex_lock(&m_mutex);
		}

		virtual void unlock() {
			pthread_mutex_unlock(&m_mutex);
		}

	private:
		pthread_mutex_t m_mutex;
	};
#endif // _YIP_BOX_HAS_POSIX_THREADS

	template<typename MTPolicy>
	class LockBlock {
	public:
		MTPolicy *mutex;

		LockBlock(MTPolicy *newMutex)
			: mutex(newMutex) {
			mutex->lock();
		}

		~LockBlock() {
			mutex->unlock();
		}
	};

	template <typename MTPolicy>
	class HasSlots;

	template <typename MTPolicy, typename ... Types>
	class ConnectionBase {
	public:
		virtual ~ConnectionBase() {}
		virtual HasSlots<MTPolicy> *getDestination() const = 0;
		virtual void shoot(Types ... arguments) const = 0;
		virtual ConnectionBase<MTPolicy, Types...> *clone() const = 0;
		virtual ConnectionBase<MTPolicy, Types...> *duplicate(HasSlots<MTPolicy> *newDestination) const = 0;
	};

	template <typename MTPolicy>
	class SignalBase : public MTPolicy {
	public:
		virtual void disconnectSlot(HasSlots<MTPolicy> *slot) = 0;
		virtual void duplicateSlot(const HasSlots<MTPolicy> *oldSlot, HasSlots<MTPolicy> *newSlot) = 0;
	};

	template <typename MTPolicy = YIP_BOX_DEFAULT_MT_POLICY>
	class HasSlots : public MTPolicy {
	private:
		typedef std::set<SignalBase<MTPolicy> *> SenderSet;
	public:
		HasSlots() : MTPolicy(), senders(), active(true) {
		}

		HasSlots(const HasSlots<MTPolicy> &src) : MTPolicy(src), senders(),
			active(src.active) {
			LockBlock<MTPolicy> lock(this);

			for (typename SenderSet::const_iterator i = src.senders.begin();
			     i != src.senders.end(); ++i) {
				(*i)->duplicateSlot(&src, this);
				senders.insert(*i);
			}
		}

		virtual ~HasSlots() {
			disconnectAll();
		}

		void signalConnect(SignalBase<MTPolicy> *sender) {
			LockBlock<MTPolicy> lock(this);
			senders.insert(sender);
		}

		void signalDisconnect(SignalBase<MTPolicy> *sender) {
			LockBlock<MTPolicy> lock(this);
			senders.erase(sender);
		}

		void disconnectAll() {
			LockBlock<MTPolicy> lock(this);

			for (typename SenderSet::iterator i = senders.begin();
			     i != senders.end(); ++i) {
				(*i)->disconnectSlot(this);
			}

			senders.clear();
		}

		bool isActive() const {
			return active;
		}

		void setActive(bool newActive) {
			active = newActive;
		}

	private:
		SenderSet senders;
		bool active;
	};

	template <typename MTPolicy, typename ... Types>
	class SignalBaseTyped : public SignalBase<MTPolicy> {
	public:
		typedef std::list<ConnectionBase<MTPolicy, Types...> *> ConnectionList;

		SignalBaseTyped() : connectedSlots() {
		}

		SignalBaseTyped(const SignalBaseTyped<MTPolicy, Types...> &src) : SignalBase<MTPolicy>(src) {
			LockBlock<MTPolicy> lock(this);

			for (typename ConnectionList::const_iterator i = src.connectedSlots.begin();
			     i != src.connectedSlots.end(); ++i) {
				(*i)->getDestination()->signalConnect(this);
				connectedSlots.push_back((*i)->clone());
			}
		}

		virtual ~SignalBaseTyped() {
			LockBlock<MTPolicy> lock(this);

			for (typename ConnectionList::iterator i = connectedSlots.begin();
			     i != connectedSlots.end(); ++i) {
				(*i)->getDestination()->signalDisconnect(this);
				delete *i;
			}
		}

		void disconnectAll() {
			LockBlock<MTPolicy> lock(this);

			for (typename ConnectionList::iterator i = connectedSlots.begin();
			     i != connectedSlots.end(); ++i) {
				(*i)->getDestination()->signalDisconnect(this);
				delete *i;
			}

			connectedSlots.clear();
		}

		void disconnect(HasSlots<MTPolicy> *item) {
			LockBlock<MTPolicy> lock(this);
			typename ConnectionList::iterator i = connectedSlots.begin();

			while (i != connectedSlots.end()) {
				if ((*i)->getDestination() == item) {
					delete *i;
					i = connectedSlots.erase(i);
					item->signalDisconnect(this);

				} else {
					++i;
				}
			}
		}

		void disconnectSlot(HasSlots<MTPolicy> *slot) {
			LockBlock<MTPolicy> lock(this);
			typename ConnectionList::iterator i = connectedSlots.begin();

			while (i != connectedSlots.end()) {
				if ((*i)->getDestination() == slot) {
					delete *i;
					i = connectedSlots.erase(i);

				} else {
					++i;
				}
			}
		}

		void duplicateSlot(const HasSlots<MTPolicy> *oldTarget,
		                   HasSlots<MTPolicy> *newTarget) {
			LockBlock<MTPolicy> lock(this);

			for (typename ConnectionList::iterator i = connectedSlots.begin();
			     i != connectedSlots.end(); ++i) {
				if ((*i)->getDestination() == oldTarget) {
					connectedSlots.push_back((*i)->duplicate(newTarget));
				}
			}
		}

	protected:
		ConnectionList connectedSlots;
	};

	template <typename MTPolicy, typename Destination, typename ... Types>
	class Connection : public ConnectionBase<MTPolicy, Types...> {
	public:
		typedef void (Destination:: *FunctionPointer)(Types ...);

		Connection() : object(nullptr), function(nullptr) {
		}

		Connection(Destination *newObject, FunctionPointer newFunction) :
			object(newObject), function(newFunction) {
		}

		Connection(const Connection<MTPolicy, Destination, Types...> &src) :
			ConnectionBase<MTPolicy, Types...>(src), object(src.object),
			function(src.function) {
		}

		virtual HasSlots<MTPolicy> *getDestination() const {
			return object;
		}

		virtual void shoot(Types ... arguments) const {
			(object->*function)(arguments...);
		}

		virtual Connection<MTPolicy, Destination, Types...> *clone() const {
			return new Connection<MTPolicy, Destination, Types...>(*this);
		}

		virtual Connection<MTPolicy, Destination, Types...> *duplicate(HasSlots<MTPolicy> *newDestination) const {
			return new Connection<MTPolicy, Destination, Types...>(reinterpret_cast<Destination *>(newDestination), function);
		}

	private:
		Destination *object;
		FunctionPointer function;
	};

	template <typename MTPolicy, typename ... Types>
	class SignalSpecific : public SignalBaseTyped<MTPolicy, Types...> {
	public:
		SignalSpecific() : SignalBaseTyped<MTPolicy, Types...>() {
		}

		SignalSpecific(const SignalSpecific<MTPolicy, Types...> &src) :
			SignalBaseTyped<MTPolicy, Types...>(src) {
		}

		template <typename Destination>
		void connect(Destination *object,
		             typename Connection<MTPolicy, Destination, Types...>::FunctionPointer function) {
			LockBlock<MTPolicy> lock(this);

			this->connectedSlots.push_back(new Connection<MTPolicy, Destination, Types...>(object, function));
			object->signalConnect(this);
		}

		void shoot(Types... arguments) {
			LockBlock<MTPolicy> lock(this);

			for (typename SignalBaseTyped<MTPolicy, Types...>::ConnectionList::const_iterator i = this->connectedSlots.begin();
			     i != this->connectedSlots.end(); ++i) {
				if ((*i)->getDestination()->isActive()) {
					(*i)->shoot(arguments...);
				}
			}
		}

		void operator()(Types... arguments) {
			shoot(arguments...);
		}
	};

	template <typename ... Types>
	using Signal = SignalSpecific<YIP_BOX_DEFAULT_MT_POLICY, Types...>;
}

#endif // YB_YIP_BOX_H