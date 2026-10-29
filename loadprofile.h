#pragma once

// [3DS] Load-time steps (the load phase of the port): a character's files,
// split into their parts. Written to load-log.txt on the 3DS (Prism's
// 3ds/perf_3ds.cpp); on a PC, to stderr when built with DOLMEXICA_LOAD_PROFILE.

#include <stdint.h>

#if defined(__3DS__)

namespace prism {
	uint64_t getN3DSPerfTicks();
	double getN3DSPerfMilliseconds(uint64_t tTicks);
	void addN3DSPerfLine(const char* tFormat, ...);
}
#define LOAD_PROFILE_ACTIVE 1
inline uint64_t getLoadProfileTicks() { return prism::getN3DSPerfTicks(); }
inline double getLoadProfileMilliseconds(uint64_t tTicks) { return prism::getN3DSPerfMilliseconds(tTicks); }
#define addLoadProfileLine(...) prism::addN3DSPerfLine(__VA_ARGS__)

#elif defined(DOLMEXICA_LOAD_PROFILE)

#include <stdio.h>
#include <chrono>
#define LOAD_PROFILE_ACTIVE 1
inline uint64_t getLoadProfileTicks() { return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
inline double getLoadProfileMilliseconds(uint64_t tTicks) { return tTicks / 1000.0; }
#define addLoadProfileLine(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)

#else

#define LOAD_PROFILE_ACTIVE 0
inline uint64_t getLoadProfileTicks() { return 0; }
inline double getLoadProfileMilliseconds(uint64_t) { return 0; }
#define addLoadProfileLine(...) do {} while (0)

#endif

// Times one step: "    <name> <ms> ms <detail>".
class LoadProfileStep {
public:
	LoadProfileStep(const char* tName, const char* tDetail = "") : mName(tName), mDetail(tDetail), mStart(getLoadProfileTicks()) {}
	~LoadProfileStep() { end(); }
	void end() {
		if (!mName) return;
		addLoadProfileLine("    %s %.1f ms %s", mName, getLoadProfileMilliseconds(getLoadProfileTicks() - mStart), mDetail);
		mName = nullptr;
	}
private:
	const char* mName;
	const char* mDetail;
	uint64_t mStart;
};
