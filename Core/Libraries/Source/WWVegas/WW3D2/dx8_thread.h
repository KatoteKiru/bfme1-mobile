#pragma once

// BFME: E1-32. The check TextureLoader::Is_DX8_Thread() uses.
//
// recordedMainTid is DX8Wrapper::_MainThreadID. It stays 0 until
// DX8Wrapper::Init stores the caller (dx8wrapper.cpp:718). Headless never
// calls WW3D::Init (W3DDisplay.cpp:1163), so the id stays 0 for the whole run.
// 0 means not recorded: every caller is the DX8 thread. That is what _UNIX
// answered when every tid was 0. After Init, compare tids. A false answer
// queues the load, and ThreadClass::Execute does not start the loader on
// _UNIX (thread.cpp:109).

inline bool BfmeIsRecordedDx8Thread(unsigned currentTid, unsigned recordedMainTid)
{
	if (recordedMainTid == 0) {
		return true;
	}
	return currentTid == recordedMainTid;
}
