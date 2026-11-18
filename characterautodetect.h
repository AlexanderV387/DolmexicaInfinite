#pragma once

// [3DS] Characters found by themselves: the folders in chars/ that select.def
// does not list are added to its [Characters] (in memory: select.def stays as
// it is), after the listed ones and in alphabetical order, each with the .def
// that is its character's. Off with "autocharacters = 0" in mugen.cfg's
// [Config] (a prepared game whose select.def leaves characters out on
// purpose).

#include <prism/mugendefreader.h>

using namespace prism;

#include <string>
#include <vector>

#ifdef DOLMEXICA_CHARACTER_AUTODETECT
void addAutoDetectedCharactersToSelectScript(MugenDefScript* tSelectScript);
void getAutoDetectedContent(std::vector<std::string>* oCharacterFolders, std::vector<std::string>* oStages);
#else
inline void addAutoDetectedCharactersToSelectScript(MugenDefScript*) {}
inline void getAutoDetectedContent(std::vector<std::string>* oCharacterFolders, std::vector<std::string>* oStages) { oCharacterFolders->clear(); oStages->clear(); }
#endif
