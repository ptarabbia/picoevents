/*
 * Copyright 2019 Patrice Tarabbia
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#pragma once
 
#include <list>
#include <functional>
#include <tuple>
#include <utility>

/*
 A simple mechanism to notify events between various UI elements
 Not thread-safe (but can be easily made so with mutexes protecting
 the add / replace / remove / notify methods

 A typical use case is, you have a button with a on/off state that
 can be changed in different parts of the application, and you also
 need other objects to react when the state of the button changes

 so first you define an event:
 picoevents::Event<const int> buttonStateChangedEvent;

 you'll have somewhere, something that hold the current state with
 get/set methods
 bool getCurrentButtonState() const
 {
	return _buttonState;
 }
 void setCurrentButtonState(bool value) const
 {
	buttonStateChangedEvent.notify(value);
 }
 Note how setCurrentButtonState doesn't change the value, but instead
 notifies the event. So you need to handle the event:
buttonStateChangedEvent.add([&](bool v) { this->_buttonState=v;});

Now any object in the UI can change the state of the button by calling:
buttonStateChangedEvent.notify(value);

and any object in the UI can be notifed of button change by adding:
buttonStateChangedEvent.add([&](bool v) { dosomething.... });

All of this, without these objects having any knowledge of each other !


You can also prepare a notification, then trigger it at a later time:
auto notifier = buttonStateChangedEvent.makeNotifier(false);
notifier.trigger();  // call buttonStateChangedEvent.notify(false);

That way you can prepare a notification on a thread, and trigger it on
a different one.

*/

namespace picoevents
{
	class ScopedCallbackIDBase
	{
	public:
		ScopedCallbackIDBase() = default;
		virtual ~ScopedCallbackIDBase() {}
	};


	template<typename ...T>
	class Event
	{
	public:
		using Callback = std::function<void(T...)>;
		using CallbackID = typename std::list<Callback>::iterator;

        void setEnabled(bool b)
        {
            _enabled = b;
        }
        bool isEnabled() const
        {
            return _enabled;
        }

		CallbackID empty_callback() 
		{
			return _callbacks.end();
		}

		CallbackID add(const Callback& c, bool first=false)
		{
			if (first)
			{
				_callbacks.push_front(c);
				return _callbacks.begin();
			}
			else
			{
				_callbacks.push_back(c);
				return std::prev(_callbacks.end());
			}
		}
		void remove(CallbackID &c)
		{
			if (c != _callbacks.end())
			{
				if (c == _nextCallback)
				{
					_nextCallback++;
				}
				_callbacks.erase(c);
				c = _callbacks.end();
			}
		}
		bool replace(CallbackID id, Callback& c)
		{
			*id = c;
			return true;
		}

		// overloading () so you can do event(a, b, c); instead of event.notify(a, b, c);
		void operator()(T... t)
		{
			notify(t...);
		}

		void notify(T... t) const
		{
            if (_enabled)
            {
                for (CallbackID c = _callbacks.begin(); c != _callbacks.end();)
                {
					// as erase only invalidates the current iterator,
					// we can perform erase() while calling notify by preloading
					// the next one
					_nextCallback = std::next(c);
                    (*c)(t...);
					c = _nextCallback;
                }
				_nextCallback = _callbacks.end();
			}
		}

        // RAII way to temporary disable an event
        class ScopedDisable
        {
            Event& _e;
            bool _prev;
        public:
            ScopedDisable(Event& e) : _e(e), _prev(_e.isEnabled())
            {
                _e.setEnabled(false);
            }
            ~ScopedDisable()
            {
                _e.setEnabled(_prev);
            }
        };

		class Notifier
		{
			// https://stackoverflow.com/questions/12742877/remove-reference-from-stdtuple-members
			template <typename... TT>
			using tuple_with_removed_refs = std::tuple<typename std::remove_reference<TT>::type...>;
			template <typename... TT>
			tuple_with_removed_refs<TT...> remove_ref_from_tuple_members(std::tuple<TT...> const& t) {
				return tuple_with_removed_refs<TT...>{ t };
			}
		public:
			Notifier(Event& e, T... t) :
				_t(std::tuple<T...>(t...)),
				_e(e) {}
			void trigger()
			{
				std::apply([&](auto &&... args) { _e.notify(args...); }, _t);
			}
		private:
			tuple_with_removed_refs<T...> _t;
			Event& _e;
		};

        class ScopedNotifier
        {
            Notifier _n;
        public:
            ScopedNotifier(const Notifier& n): 
                _n(n)
            {
            }
            ~ScopedNotifier()
            {
                _n.trigger();
            }
        };

		Notifier makeNotifier(T... t)
		{
			return Notifier(*this, t...);
		}

		class ScopedCallbackID: public ScopedCallbackIDBase
		{
			Event& _event;
			Event::CallbackID _cb;
		public:
			ScopedCallbackID(Event& event, const Callback& c, bool first=false) : _event(event)
			{
				_cb = _event.add(c, first);
			}
			ScopedCallbackID(ScopedCallbackID&& other) : _event(other._event), _cb(other._cb)
			{
				other._cb = _event.empty_callback();
			}
			virtual ~ScopedCallbackID()
			{
				_event.remove(_cb);
			}
			void replace(Event& event, const Callback& c, bool first = false)
			{
				_event.remove(_cb);
				_event = event;
				_cb = _event.add(c, first);
			}
			void invoke(T... t)
			{
				(*_cb)(t...);
			}
			Event& getEvent()
			{
				return _event;
			}
		};

	private:
		// could possibly use std::forward_list<> but then removing 
		// callbacks becomes more tricky
		mutable std::list<Callback> _callbacks;
        bool _enabled = true;
		mutable CallbackID _nextCallback = _callbacks.end();
	};


	//
	// if you are using a lot of callbacks for the entire lifetime of an object, it can be
	// tedious to store each callbackID in a ScopedCallbackID.
	// Instead you can make your object inherit from ScopedCallbacksHolder, and use 
	// addCallback() to define events callbacks (for possibly a lot of different event types) 
	// that will automatically be removed when the object is deleted
	//
    class ScopedCallbacksHolder
    {
        std::vector< std::unique_ptr<ScopedCallbackIDBase> > _callbacks;
    public:
        ScopedCallbacksHolder() = default;
        virtual ~ScopedCallbacksHolder()
        {
        }
        template<typename E>
        typename E::ScopedCallbackID* addCallback(E& e, const typename E::Callback& c, bool first = false)
        {
            typename E::ScopedCallbackID* res = new typename E::ScopedCallbackID(e, c, first);
            _callbacks.emplace_back(res);
            return res;
        }
		void removeAllCallbacks()
		{
			_callbacks.clear();
		}

		template<typename E>
		void removeCallback(typename E::ScopedCallbackID * r)
		{
			auto it = _callbacks.begin();
			while (it != _callbacks.end())
			{
				if (it->get() == r) break;
				it++;
			}
			// because _callbacks contains unique_ptr, this actually deletes the original r
			if (it != _callbacks.end()) _callbacks.erase(it);
		}
    };


	// added listeners are removed when the value is deleted
	// if a listener is deleted before the value, it needs to be removed with removeCallback() first !
	template<typename T, typename ET=T>
	class Value : public ScopedCallbacksHolder
	{
		T _t;
		Event<ET> _e;
	public:
		Value(const T& t) : _t(t) {}
		virtual ~Value()
		{
			removeAllCallbacks(); // remove callbacks before deleting _e
		}
		void set(const T& t)
		{
			_t = t;
			notify();
		}
		void set(T&& t)
		{
			_t = t;
			notify();
		}
		const T& get() const
		{
			return _t;
		}
		Event<ET>& getEvent()
		{
			return _e;
		}
		void notify()
		{
			_e.notify(_t);
		}

		typename Event<ET>::ScopedCallbackID* addListener(const typename Event<ET>::Callback& c, bool first = false)
		{
			return addCallback(_e, c, first);
		}
	};
}


// Example code below
#if 0

class Test
{
	picoevents::Event<const int>& _event;
	picoevents::Event<const int>::CallbackID _onEventTriggered;
public:
	Test(picoevents::Event<const int>& event) : _event(event)
	{
		picoevents::Event<const int>::Callback callback = [&](const int i)
		{
			printf("Event Triggered %d\n", i);
		};

		_onEventTriggered = _event.add(callback);
	}
	~Test()
	{
		_event.remove(_onEventTriggered);
	}
};

int main(int argc, char** argv)
{
	{
		// simple event with int argument
		picoevents::Event<const int> event;
		event.notify(1);
		{
			Test* t1 = new Test(event);
			Test t2(event);
			event.notify(2); // notify twice
			delete t1;
			event.notify(3); // notify once
		}
		event.notify(4); // dont notify
	}


	{
		// event with int&, handler modifies argument
		picoevents::Event<int&> event2;
		event2.add(
			[&](int& k)
			{
				k++;
			}
		);

		int z = 5;
		event2.notify(z);
		printf("z = %d\n", z);
	}

	{
		// event with multiple arguments
		picoevents::Event<int, float, const std::string&> event3;
		picoevents::Event<int, float, const std::string&>::Callback cb =
			[&](int k, float v, const std::string& s)
		{
			printf("%d %f %s\n", k, v, s.c_str());
		};

		event3.add(cb);
		event3.notify(3, 0.25f, "3 args event");
	}
	{
		// creating a notifier, then triggering it. With delayed notifiers, you can't use references in the parameters
		picoevents::Event<int, float, std::string> event4;
		event4.add(
			[&](int k, float v, const std::string s)
			{
				printf("%d %f %s\n", k, v, s.c_str());
			});

		picoevents::Event<int, float, std::string>::Notifier notifier(event4, 1, -0.5f, "Delayed notifier");
		notifier.trigger();
	}
}
#endif
