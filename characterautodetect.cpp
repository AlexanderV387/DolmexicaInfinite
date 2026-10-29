#include "characterautodetect.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <prism/file.h>
#include <prism/datastructures.h>

#include "config.h"
#include "loadprofile.h"

static struct {
	int mHasSearched = 0;
	std::vector<std::string> mFound;   // as select.def would list them
} gCharacterAutoDetectData;

static std::string lowercase(std::string tText) {
	turnStringLowercase(tText);
	return tText;
}

static int isFolderEntry(const std::string& tFolder, struct dirent* tEntry) {
#ifdef DT_DIR
	if (tEntry->d_type == DT_DIR) return 1;
	if (tEntry->d_type != DT_UNKNOWN) return 0;
#endif
	struct stat sb;
	const auto path = tFolder + tEntry->d_name;
	return stat(path.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode);
}

static std::vector<std::string> getEntries(const std::string& tFullPath, int tAreFolders) {
	std::vector<std::string> ret;
	DIR* dir = opendir(tFullPath.c_str());
	if (!dir) return ret;
	while (struct dirent* entry = readdir(dir)) {
		if (entry->d_name[0] == '.') continue;
		if (isFolderEntry(tFullPath, entry) != tAreFolders) continue;
		ret.push_back(entry->d_name);
	}
	closedir(dir);
	std::sort(ret.begin(), ret.end(), [](const std::string& a, const std::string& b) { return lowercase(a) < lowercase(b); });
	return ret;
}

// A character's .def has a [Files] group with its sprites or states (intro
// and ending storyboards, also .def files, do not).
static int isCharacterDefinition(const std::string& tFullPath) {
	FILE* file = fopen(tFullPath.c_str(), "rb");
	if (!file) return 0;
	char buffer[8192];
	const size_t length = fread(buffer, 1, sizeof(buffer) - 1, file);
	fclose(file);
	buffer[length] = '\0';
	const auto text = lowercase(buffer);
	return text.find("[files]") != std::string::npos && (text.find("sprite") != std::string::npos || text.find("cns") != std::string::npos);
}

// The folder's character .def: the one named as the folder, else the first
// that is a character's. Empty if none.
static std::string findCharacterDefinition(const std::string& tFolderFullPath, const std::string& tFolder) {
	std::vector<std::string> definitions;
	for (const auto& name : getEntries(tFolderFullPath, 0)) {
		if (name.size() > 4 && lowercase(name.substr(name.size() - 4)) == ".def") definitions.push_back(name);
	}
	for (const auto& name : definitions) {
		if (lowercase(name) == lowercase(tFolder) + ".def" && isCharacterDefinition(tFolderFullPath + name)) return name;
	}
	for (const auto& name : definitions) {
		if (isCharacterDefinition(tFolderFullPath + name)) return name;
	}
	return "";
}

static void searchCharacters() {
	gCharacterAutoDetectData.mHasSearched = 1;
	const uint64_t start = getLoadProfileTicks();

	char charsFullPath[1024];
	getFullPath(charsFullPath, (getDolmexicaAssetFolder() + "chars/").c_str());
	std::string charsFolder = charsFullPath;
	if (!charsFolder.empty() && charsFolder.back() != '/') charsFolder += '/';

	beginCompatSection("characters found in chars/ (not in select.def: added after it)");
	const auto folders = getEntries(charsFolder, 1);
	for (const auto& folder : folders) {
		const auto definition = findCharacterDefinition(charsFolder + folder + "/", folder);
		if (definition.empty()) {
			addCompatLine("%s: no character .def in it, left out", folder.c_str());
			continue;
		}
		// As select.def lists it: the folder alone if its .def is named so.
		const auto entry = (definition == folder + ".def") ? folder : folder + "/" + definition;
		gCharacterAutoDetectData.mFound.push_back(entry);
		addCompatLine("%s: %s", folder.c_str(), definition.c_str());
	}
	addLoadProfileLine("  characters in chars/: %d folders, %d characters, %.0f ms", (int)folders.size(), (int)gCharacterAutoDetectData.mFound.size(), getLoadProfileMilliseconds(getLoadProfileTicks() - start));
	endCompatSection();
}

// The chars/ folder a select.def line is about ("kfm", "kfm/kfm.def",
// "kfm, stages/kfm.def"), lowercase; empty for a .def straight in chars/.
static std::string getListedFolder(const char* tName) {
	std::string name = tName;
	std::replace(name.begin(), name.end(), '\\', '/');
	while (!name.empty() && name.front() == ' ') name.erase(name.begin());
	while (!name.empty() && name.back() == ' ') name.pop_back();
	const auto slash = name.find('/');
	if (slash != std::string::npos) return lowercase(name.substr(0, slash));
	if (name.find('.') != std::string::npos) return "";
	return lowercase(name);
}

static void addListedFolder(void* tCaller, void* tData) {
	auto listed = (std::set<std::string>*)tCaller;
	auto element = (MugenDefScriptGroupElement*)tData;
	if (element->mType == MUGEN_DEF_SCRIPT_GROUP_VECTOR_ELEMENT) {
		auto vector = (MugenDefScriptVectorElement*)element->mData;
		if (vector->mVector.mSize) listed->insert(getListedFolder(vector->mVector.mElement[0]));
	}
	else if (element->mType == MUGEN_DEF_SCRIPT_GROUP_STRING_ELEMENT) {
		auto stringElement = (MugenDefScriptStringElement*)element->mData;
		listed->insert(getListedFolder(stringElement->mString));
	}
}

void addAutoDetectedCharactersToSelectScript(MugenDefScript* tSelectScript)
{
	if (!isAutoDetectingCharactersConfig()) return;
	if (!stl_string_map_contains_array(tSelectScript->mGroups, "characters")) return;
	if (!gCharacterAutoDetectData.mHasSearched) {
		searchCharacters();
	}

	std::set<std::string> listed;
	list_map(&tSelectScript->mGroups["characters"].mOrderedElementList, addListedFolder, &listed);
	for (const auto& entry : gCharacterAutoDetectData.mFound) {
		if (listed.count(getListedFolder(entry.c_str()))) continue;
		addMugenDefScriptGroupStringStatement(tSelectScript, "characters", entry.c_str());
	}
}
