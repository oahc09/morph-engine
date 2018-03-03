//-----------------------------------------------------------------------------------------------
// Time.cpp
//	

//-----------------------------------------------------------------------------------------------
#include "Engine/Core/Time/Time.hpp"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "Game/Gameplay/Encounter.hpp"

Time::Time() {
  memset(this, 0, sizeof(Time));
}

Time& Time::operator+=(const Time& rhs) {
  second += rhs.second;
  hpc += rhs.hpc;
  millisecond += rhs.millisecond;
  return *this;
}


struct TimeSystem {
  TimeSystem() {
    LARGE_INTEGER li;
    QueryPerformanceFrequency(&li);
    mFrequency = *(uint64_t*)&li;
    mSecondsPerCount = 1.0 / (double)mFrequency;
  }

public:
  uint64_t mFrequency;
  double mSecondsPerCount;
};


static TimeSystem g_timeSystem;
//-----------------------------------------------------------------------------------------------
double InitializeTime( LARGE_INTEGER& out_initialTime )
{
	LARGE_INTEGER countsPerSecond;
	QueryPerformanceFrequency( &countsPerSecond );
	QueryPerformanceCounter( &out_initialTime );
	return( 1.0 / static_cast< double >( countsPerSecond.QuadPart ) );
}


//-----------------------------------------------------------------------------------------------
double GetCurrentTimeSeconds()
{
	static LARGE_INTEGER initialTime;
	static double secondsPerCount = InitializeTime( initialTime );
	LARGE_INTEGER currentCount;
	QueryPerformanceCounter( &currentCount );
	LONGLONG elapsedCountsSinceInitialTime = currentCount.QuadPart - initialTime.QuadPart;

	double currentSeconds = static_cast< double >( elapsedCountsSinceInitialTime ) * secondsPerCount;
	return currentSeconds;
}

uint64_t __fastcall GetPerformanceCounter() {
  LARGE_INTEGER li;
  QueryPerformanceCounter(&li);

  return *(uint64_t*)&li;
}

double PerformanceCountToSecond(uint64_t count) {
  return (double)count * g_timeSystem.mSecondsPerCount;
}

ProfileLogScope::ProfileLogScope(const char* tag) {
  mTag = tag;
  mStartHPC = GetPerformanceCounter();
}
ProfileLogScope::~ProfileLogScope() {
  uint64_t elapsed = GetPerformanceCounter() - mStartHPC;
  DebuggerPrintf("[ %s ] took %f seconds.", mTag, PerformanceCountToSecond(elapsed));
}

