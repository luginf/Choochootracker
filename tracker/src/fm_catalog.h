#pragma once
#include <string>
#include <vector>
#include "project_instruments.h"
struct FMPresetEntry {
  int type,bank;
  std::string bankName,category,name,path;
  int imported=-1;
  bool library=false;
};
// UI/offline only. Transactional, bounded metadata read; no synthesis allocation.
bool loadFMCatalog(const char* path,std::vector<FMPresetEntry>& entries);
struct DX7Library {
  std::vector<FMPresetEntry> entries;
  std::vector<InstrumentDX7> patches;
  int skipped=0;
  bool limited=false;
};
// Scan the persistent bank folder on the UI thread. Missing folders are empty.
// Bad files are isolated; selected instruments own their patch independently.
DX7Library scanDX7Library(const std::string& folder);
