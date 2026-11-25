#include "mugensound.h"

#include <algorithm>
#include <string>
#include <vector>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <prism/sound.h>
#include <prism/file.h>
#include <prism/stlutil.h>

#include "osuhandler.h"
#include "gamelogic.h"
#include "config.h"
#include "pausecontrollers.h"

using namespace std;

static struct {
	int mIsPlayingMusic;
	int mIsPausedFlag;      // paused by AssertSpecial NoMusic
	int mNoMusicTime;       // the game tick it was last asserted on
} gDolmexicaMugenSoundData;

static void loadDolmexicaSoundHandler(void*) {
	setProfilingSectionMarkerCurrentFunction();

	gDolmexicaMugenSoundData.mIsPlayingMusic = 0;
	gDolmexicaMugenSoundData.mIsPausedFlag = 0;
	gDolmexicaMugenSoundData.mNoMusicTime = 0;
}

// NoMusic holds while asserted on the game's last tick, through pauses and
// super pauses (the paused players assert nothing then). It was let go
// on the first screen frame not asserted (the game's ticks and its screen
// frames not one to one: Abyss and Onslaught, both asserting it, at half
// speed on a 3DS, the stage's music cut in and out).
static void updatePauseFlag() {
	if (!gDolmexicaMugenSoundData.mIsPausedFlag) return;
	if (isDreamAnyPauseActive()) return;
	const int since = getDreamGameTime() - gDolmexicaMugenSoundData.mNoMusicTime;
	if (since >= 0 && since < 2) return;
	resumeMusic();
	gDolmexicaMugenSoundData.mIsPausedFlag = 0;
}

static void updateDolmexicaSoundHandler(void*) {
	setProfilingSectionMarkerCurrentFunction();
	if (!gDolmexicaMugenSoundData.mIsPlayingMusic) return;
	updatePauseFlag();
}

ActorBlueprint getDolmexicaSoundHandler()
{
	return makeActorBlueprint(loadDolmexicaSoundHandler, NULL, updateDolmexicaSoundHandler);
}

DreamMugenSound makeDreamMugenSound(int tGroup, int tItem)
{
	DreamMugenSound ret;
	ret.mGroup = tGroup;
	ret.mItem = tItem;
	return ret;
}

void setNoMusicFlag()
{
	if (getGameMode() == GAME_MODE_OSU) return;

	gDolmexicaMugenSoundData.mNoMusicTime = getDreamGameTime();
	if (gDolmexicaMugenSoundData.mIsPausedFlag) return;
	pauseMusic();
	gDolmexicaMugenSoundData.mIsPausedFlag = 1;
}

int isMugenBGMMusicPath(const char * tPath)
{
	return isMugenBGMMusicPath(tPath, "");
}

// Where a stage's or a screen's bgmusic is: in music/, from the game's
// folder (MUGEN's root: "sound/x.mp3"), next to the .def that names it, or
// (copied there instead of to sound/) just its file name next to that .def.
static int findMugenBGMMusicFile(const char* tPath, const char* tRootFilePath, std::string& oPath) {
	std::string path = tPath;
	std::replace(path.begin(), path.end(), '\\', '/');
	std::vector<std::string> candidates;
	candidates.push_back(getDolmexicaAssetFolder() + "music/" + path);
	candidates.push_back(getDolmexicaAssetFolder() + path);
	if (*tRootFilePath) {
		const char* folderEnd = strrchr(tRootFilePath, '/');
		const std::string folder = folderEnd ? std::string(tRootFilePath, size_t(folderEnd - tRootFilePath + 1)) : std::string();
		candidates.push_back(folder + path);
		const auto nameStart = path.find_last_of('/');
		if (nameStart != std::string::npos) candidates.push_back(folder + path.substr(nameStart + 1));
	}
	candidates.push_back(path);
	for (const auto& candidate : candidates) {
		if (isFile(candidate)) {
			oPath = candidate;
			return 1;
		}
	}
	return 0;
}

int isMugenBGMMusicPath(const char* tPath, const char* tRootFilePath) {
	if (!strchr(tPath, '.')) return 0;
	const char* fileExtension = getFileExtension(tPath);
	if (!strcmp("da", fileExtension)) return 1;
	if (getGameMode() == GAME_MODE_OSU && !strcmp("osu", fileExtension)) return 1;

	std::string found;
	return findMugenBGMMusicFile(tPath, tRootFilePath, found);
}

void playMugenBGMMusicPath(const char * tPath, int tIsLooping)
{
	playMugenBGMMusicPath(tPath, "", tIsLooping);
}

static void playMugenBGMTrack(const char* tPath, int tIsLooping) {
	char modPath[1024];
	strcpy(modPath, tPath);
	*strrchr(modPath, '.') = '\0';
	int trackNumber = atoi(modPath);
	if (tIsLooping) {
		playTrack(trackNumber);
	}
	else {
		playTrackOnce(trackNumber);
	}
	gDolmexicaMugenSoundData.mIsPlayingMusic = 1;
}

static void playMugenBGMMusicCompletePath(const char* tPath, int tIsLooping) {
	if (tIsLooping) {
		streamMusicFile(tPath);
	}
	else {
		streamMusicFileOnce(tPath);
	}
	gDolmexicaMugenSoundData.mIsPlayingMusic = 1;
}

void playMugenBGMMusicPath(const char* tPath, const char* tRootFilePath, int tIsLooping) {
	const char* fileExtension = getFileExtension(tPath);
	if (!strcmp("osu", fileExtension)) {
		if (*tRootFilePath)
		{
			std::string folder;
			getPathToFile(folder, tRootFilePath);
			const auto totalPath = folder + tPath;
			if (isFile(totalPath)) {
				setOsuFile(totalPath.c_str());
				return;
			}
		}
		setOsuFile(tPath);
		return;
	}

	if (!strcmp("da", fileExtension)) {
		playMugenBGMTrack(tPath, tIsLooping);
		return;
	}

	std::string found;
	if (findMugenBGMMusicFile(tPath, tRootFilePath, found)) {
		playMugenBGMMusicCompletePath(found.c_str(), tIsLooping);
		return;
	}
	playMugenBGMMusicCompletePath(tPath, tIsLooping);
}