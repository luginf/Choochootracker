#include "opl_patch.h"
#include "opll_presets.h"
#include "four_op_patch.h"
#include "simple_chip_presets.h"
#include "user_preset_browser.h"
#include "screen_instrument.h"
#include "selection_popup.h"
#include "user_presets.h"
#include "corelib_file.h"
#include "chipnomad_lib.h"
#include "project_utils.h"
#include <map>
#include <memory>
#include <cstring>

namespace {
std::map<InstrumentType, UserPresets> libraries;
UserPresets* library = nullptr;
InstrumentType type = InstrumentType::none;
std::vector<SelectionItem> choices;
std::vector<std::string> labels;
std::unique_ptr<Project> audition;
struct Cursor {bool valid=false;UserPresets::Reference selected;};
std::map<std::pair<int,InstrumentType>,Cursor> cursors;
std::map<InstrumentType,std::vector<UserPresets::Reference>> sequences;
bool samePreset(const UserPresets::Reference& a,const UserPresets::Reference& b) {
  return a.path==b.path&&a.archive==b.archive&&a.voice==b.voice&&a.legacy==b.legacy;
}
Cursor& cursor(){return cursors[{cInstrument,chipnomadState->project.instruments[cInstrument].type}];}

constexpr int backValue = -2, emptyValue = -3;
void show();
void stop() {
  if (type == InstrumentType::DX7) chipnomadQueueDX7Preview(chipnomadState, *pSongTrack, nullptr);
  else if (type == InstrumentType::SID) chipnomadQueueSIDPreview(chipnomadState, *pSongTrack, nullptr);
  else if (isOPLL(type)) chipnomadQueueOPLLPreview(chipnomadState, *pSongTrack, nullptr);
  else if (isOPL(type)) chipnomadQueueOPLPreview(chipnomadState, *pSongTrack, type, nullptr);
  else if (isFourOp(type)) chipnomadQueueFourOpPreview(chipnomadState, *pSongTrack, type, nullptr);
  else chipnomadQueueSimpleChipPreview(chipnomadState, *pSongTrack, type, nullptr);
  if (audition) { projectFree(audition.get()); audition.reset(); }
}
void preview(int value, bool held) {
  stop();
  if (!held || value < 0 || size_t(value) >= library->items().size() ||
      library->items()[value].kind != UserPresets::Kind::preset) return;
  audition = std::make_unique<Project>(); projectInit(audition.get()); std::string error;
  if (!library->load(value, audition.get(), 0, error)) { screenMessage(MESSAGE_TIME_ERROR, "%s", error.c_str()); return; }
  const auto& i = audition->instruments[0];
  if (type == InstrumentType::DX7) chipnomadQueueDX7Preview(chipnomadState, *pSongTrack, &i.chip.dx7);
  else if (type == InstrumentType::SID) chipnomadQueueSIDPreview(chipnomadState, *pSongTrack, &i.chip.sid);
  else if (isOPLL(type)) chipnomadQueueOPLLPreview(chipnomadState, *pSongTrack, &i.chip.opll);
  else if (isOPL(type)) chipnomadQueueOPLPreview(chipnomadState, *pSongTrack, type, &i.chip.opl);
  else if (isFourOp(type)) chipnomadQueueFourOpPreview(chipnomadState, *pSongTrack, type, &i.chip.fourOp);
  else chipnomadQueueSimpleChipPreview(chipnomadState, *pSongTrack, type, &i.chip.simpleChip);
}
void back() {
  stop();
  if (library->atRoot()) { screenSetup(&screenInstrument, cInstrument); return; }
  std::string error; library->back(error);
  if (!error.empty()) screenMessage(MESSAGE_TIME_ERROR, "%s", error.c_str());
  show();
}
void select(int value) {
  stop();
  if (value == backValue) { back(); return; }
  if (value < 0 || size_t(value) >= library->items().size()) return;
  std::string error;
  if (library->items()[value].kind != UserPresets::Kind::preset) {
    library->enter(value, error); show();
  } else if (library->load(value, &chipnomadState->project, cInstrument, error)) {
    rememberUserPreset(library->reference(value));
    projectModified = 1; screenSetup(&screenInstrument, cInstrument);
  }
  if (!error.empty()) screenMessage(MESSAGE_TIME_ERROR, "%s", error.c_str());
}
void show() {
  labels.clear(); choices.clear();
  labels.push_back(library->atRoot() ? "< Back to instrument" : "../");
  for (const auto& i : library->items())
    labels.push_back(i.name + (i.kind == UserPresets::Kind::zip || i.kind == UserPresets::Kind::bank ? " >" : ""));
  if (library->items().empty()) labels.push_back("(No compatible presets)");
  choices.push_back({labels[0].c_str(), backValue, nullptr, 0, nullptr});
  for (size_t i = 0; i < library->items().size(); ++i)
    choices.push_back({labels[i + 1].c_str(), int(i), nullptr, 0, labels[i + 1].c_str()});
  if (library->items().empty()) choices.push_back({labels.back().c_str(), emptyValue, nullptr, 0, nullptr});
  char title[32]; snprintf(title, sizeof(title), "%s USER", instrumentTypeName(type));
  int selected=library->items().empty()?backValue:0;
  const auto& position=cursor();
  if(position.valid)for(size_t i=0;i<library->items().size();++i)
    if(samePreset(library->reference(i),position.selected))selected=int(i);
  selectionPopupSetup(title, choices.data(), int(choices.size()), selected,
                      select, back, true, preview);
  screenSetup(&screenSelectionPopup, 0);
}
}

bool setupUserPresetLibrary(UserPresets& target, InstrumentType type) {
  const auto* subfolder = userPresetFolder(type);
  if (!subfolder) return false;
  std::string base = "instruments/";
  bool external = fileIsRunningFromAppImage();
#ifdef ANDROID_BUILD
  external = true;
#endif
  if (external) {
    char directory[1024];
    if (fileGetDefaultDirectory(directory, sizeof(directory))) return false;
    base = std::string(directory) + "/instruments/";
  }
  target.setup(base + "USER/" + subfolder, type, base + "banks/" + subfolder);
  return true;
}

void openUserPresetBrowser() {
  type = chipnomadState->project.instruments[cInstrument].type;
  library = &libraries[type];
  if(!setupUserPresetLibrary(*library,type))return;
  sequences[type].clear();
  std::string error;
  if(cursor().valid)library->focus(cursor().selected,error);
  if (!library->refresh(error)) {
    while (!library->atRoot()) library->back(error);
    library->refresh(error);
  }
  show();
  if (!error.empty()) screenMessage(MESSAGE_TIME_ERROR, "%s", error.c_str());
}

void rememberUserPreset(const UserPresets::Reference& preset) {
  auto& position=cursor();position.valid=true;position.selected=preset;sequences[chipnomadState->project.instruments[cInstrument].type].clear();
}
void cycleUserPreset(int direction) {
  type=chipnomadState->project.instruments[cInstrument].type;
  library=&libraries[type];if(!setupUserPresetLibrary(*library,type))return;
  auto& position=cursor();auto& sequence=sequences[type];std::string error;
  if(sequence.empty())sequence=library->scan(error);
  if(error.rfind("ALL: ",0)==0)error.erase(0,5);
  if(!error.empty())screenMessage(MESSAGE_TIME_ERROR,"%s",error.c_str());
  const int count=int(sequence.size());
  if(!count){screenMessage(MESSAGE_TIME_ERROR,"No compatible USER presets");return;}
  int index=-1;
  if(position.valid)for(int i=0;i<count;++i)if(samePreset(position.selected,sequence[i])){index=i;break;}
  if(index<0&&!position.valid) {
    const auto& current=chipnomadState->project.instruments[cInstrument];
    for(int i=0;i<count;++i)if(sequence[i].name==current.name&&
      (sequence[i].voice<0||(type==InstrumentType::DX7&&sequence[i].voice==current.chip.dx7.sourceProgram))){index=i;break;}
  }
  if(index<0)index=direction>0?-1:0;
  for(int tries=0;tries<count;++tries) {
    index=(index+(direction>0?1:-1)+count)%count;const auto& preset=sequence[index];
    if(!library->load(preset,&chipnomadState->project,cInstrument,error))continue;
    position.selected=preset;position.valid=true;
    projectModified=1;screenSetup(&screenInstrument,cInstrument);return;
  }
  sequence.clear();screenMessage(MESSAGE_TIME_ERROR,"USER presets could not load");
}
