#include "Timer.h"

std::atomic<int64_t> Timer::s_sequenceCreator{ 0 };

Timer::Timer(TimerCallback cb, Timestamp when, double interval)
	: callback_(std::move(cb)),
	  expiration_(when),
	  interval_(interval),
	  repeat_(interval > 0.0),
	  sequence_(s_sequenceCreator.fetch_add(1)) {}

void Timer::restart(Timestamp now)
{
	if (repeat_)
	{
		expiration_ = addTime(now, interval_);
	}
	else expiration_ = Timestamp::invalid();
}