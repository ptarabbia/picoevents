# picoevents
A simple mechanism to notify events between various UI elements
 Not thread-safe (but can be easily made so with mutexes protecting
 the add / replace / remove / notify methods

 A typical use case is, you have a button with a on/off state that
 can be changed in different parts of the application, and you also
 need other objects to react when the state of the button changes

 so first you define an event:
 ```
 picoevents::Event<const int> buttonStateChangedEvent;
```
 you'll have somewhere, something that hold the current state with
 get/set methods
	
```	
 bool getCurrentButtonState() const
 {
	return _buttonState;
 }
 void setCurrentButtonState(bool value) const
 {
	buttonStateChangedEvent.notify(value);
 }
```
	
 Note how setCurrentButtonState doesn't change the value, but instead
 notifies the event. So you need to handle the event:
 
 ```
buttonStateChangedEvent.add([&](bool v) { this->_buttonState=v;});
```

Now any object in the UI can change the state of the button by calling:
```
buttonStateChangedEvent.notify(value);
```
or the shortened version:
```
buttonStateChangedEvent(value);
```

and any object in the UI can be notified of button change by adding:
```
buttonStateChangedEvent.add([&](bool v) { dosomething.... });
```

All of this, without these objects having any knowledge of each other !


You can also prepare a notification, then trigger it at a later time:

```
auto notifier = buttonStateChangedEvent.makeNotifier(false);
notifier.trigger();  // call buttonStateChangedEvent.notify(false);
```
That way you can prepare a notification on a thread, and trigger it on
a different one.

Adding a callback means you also need to remove it at some point.
Event::add() returns a CallbackID that can be passed to Event::remove()

Since it's sometimes tedious to keep track of all the added callbacks, especially 
if you have a listener class that listens to a lot of things, you can inherit from
the ScopedCallbacksHolder class instead, like this:
```
class A: public ScopedCallbacksHolder
{
    A(Event<int>& e)
    {
        addCallback(e, [](const int & data)
        {
            // do something
        };
    }
    ~A()
    {
        // callback will be automatically removed
    }
};
```