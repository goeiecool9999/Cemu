#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/OS/libs/gx2/GX2_Event.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VsyncDriver/VsyncDriver.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "util/highresolutiontimer/HighResolutionTimer.h"
#include "config/CemuConfig.h"
#include "Cafe/CafeSystem.h"

sint32 s_customVsyncFrequency = -1;

void LatteTiming_NotifyHostVSync();

// calculate time between vsync events in timer units
// standard rate on Wii U is 59.94, however to prevent tearing and microstutter on ~60Hz displays it is better if we slightly overshoot 60 Hz
// can be modified by graphic packs
HRTick LatteTime_CalculateTimeBetweenVSync()
{
	// 59.94 -> 60 * 0.999

	HRTick tick = HighResolutionTimer::getFrequency();
	if (s_customVsyncFrequency > 0)
	{
		tick /= (uint64)s_customVsyncFrequency;
	}
	else
	{
		tick *= 1000ull;
		tick /= 1002ull;
		tick /= 60ull;
	}
	return tick;
}

void LatteTiming_setCustomVsyncFrequency(sint32 frequency)
{
	s_customVsyncFrequency = frequency;
}

void LatteTiming_disableCustomVsyncFrequency()
{
	s_customVsyncFrequency = -1;
}

bool LatteTiming_getCustomVsyncFrequency(sint32& customFrequency)
{
	sint32 t = s_customVsyncFrequency;
	if (t <= 0)
		return false;
	customFrequency = t;
	return true;
}

bool s_usingHostDrivenVSync = false;

void LatteTiming_EnableHostDrivenVSync()
{
	if (s_usingHostDrivenVSync)
		return;
	VsyncDriver_startThread(LatteTiming_NotifyHostVSync);
	s_usingHostDrivenVSync = true;
}
void LatteTiming_DisableHostDrivenVSync()
{
	s_usingHostDrivenVSync = false;
	LatteGPUState.timer_nextVSync = HighResolutionTimer::now().getTick();
}

bool LatteTiming_IsUsingHostDrivenVSync()
{
	return s_usingHostDrivenVSync;
}

void LatteTiming_Init()
{
	LatteGPUState.timer_frequency = HighResolutionTimer::getFrequency();
	LatteGPUState.timer_bootUp = HighResolutionTimer::now().getTick();
	LatteGPUState.timer_nextVSync = LatteGPUState.timer_bootUp + LatteTime_CalculateTimeBetweenVSync();
}

void LatteTiming_signalVsync()
{
	static uint32 s_vsyncIntervalCounter = 0;

	if (!LatteGPUState.gx2InitCalled)
		return;
	s_vsyncIntervalCounter++;
	uint32 swapInterval = 1;
	if (LatteGPUState.sharedArea)
		swapInterval = LatteGPUState.sharedArea->swapInterval;

	// flip
	if (s_vsyncIntervalCounter >= swapInterval)
	{
		if (LatteGPUState.sharedArea)
		{
			if (LatteGPUState.flipRequestCount > 0)
			{
				LatteGPUState.flipRequestCount.fetch_sub(1);
				LatteGPUState.sharedArea->flipExecuteCountBE = _swapEndianU32(_swapEndianU32(LatteGPUState.sharedArea->flipExecuteCountBE) + 1);
			}
		}
		GX2::__GX2NotifyEvent(GX2::GX2CallbackEventType::FLIP);
		s_vsyncIntervalCounter = 0;
	}
	// vsync
	GX2::__GX2NotifyEvent(GX2::GX2CallbackEventType::VSYNC);
}

HRTick s_lastHostVsync = 0;

// notify when host vsync event is triggered (on renderer canvas)
void LatteTiming_NotifyHostVSync()
{
	if (!LatteTiming_IsUsingHostDrivenVSync())
		return;
	auto nowTimePoint = HighResolutionTimer::now().getTick();
	auto dif = nowTimePoint - s_lastHostVsync;
	auto vsyncPeriod = LatteTime_CalculateTimeBetweenVSync();

	s_lastHostVsync = nowTimePoint;
	auto wholeperiods = dif/vsyncPeriod;
	if (dif % vsyncPeriod >= vsyncPeriod/2)
		wholeperiods++;
	for(int i = 0; i < wholeperiods; i++)
		LatteTiming_signalVsync();
	LatteGPUState.timer_nextVSync = nowTimePoint;
}

// handle timed vsync event
void LatteTiming_HandleTimedVsync()
{
	// simulate VSync
	uint64 currentTimer = HighResolutionTimer::now().getTick();
	if( currentTimer >= LatteGPUState.timer_nextVSync )
	{
		if(!LatteTiming_IsUsingHostDrivenVSync())
			LatteTiming_signalVsync();
		// even if vsync is delegated to the host device, we still use this virtual vsync timer to check finished states
		LatteQuery_UpdateFinishedQueries();
		LatteTextureReadback_UpdateFinishedTransfers(false);
		if(LatteTiming_IsUsingHostDrivenVSync())
		{
			LatteGPUState.timer_nextVSync = UINT64_MAX;
		}
		else
		{
			// update vsync timer
			uint64 vsyncTime = LatteTime_CalculateTimeBetweenVSync();
			uint64 missedVsyncCount = (currentTimer - LatteGPUState.timer_nextVSync) / vsyncTime;
			if (missedVsyncCount >= 2)
			{
				LatteGPUState.timer_nextVSync += vsyncTime * (missedVsyncCount + 1ULL);
			}
			else
				LatteGPUState.timer_nextVSync += vsyncTime;
		}
		g_renderer->PresentFrontBuffers();
	}
}