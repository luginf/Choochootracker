#include "screen_instrument.h"
#include "selection_popup.h"
#include "corelib_gfx.h"
#include "corelib_file.h"
#include "chipnomad_lib.h"
#include "opl_patch.h"
#include "four_op_patch.h"
#include "dx7_patch.h"
#include "opll_presets.h"
#include "sid_patch.h"
#include "fm_catalog.h"
#include "utils.h"
#include "project_utils.h"
#include "waveform_display.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <set>

namespace {
using Entry=FMPresetEntry;
std::vector<Entry> catalog;
std::vector<InstrumentDX7> importedDX7,libraryDX7;
bool catalogRead=false;
std::vector<SelectionItem> bankItems,categoryItems;
std::vector<std::vector<SelectionItem>> sounds;
std::vector<std::string> categoryNames;
std::string folder;
int bankFilter=0,buttonDown=0;
int contextInstrument=-1,bankBeforeImport=0;
Bitmap* sidWaveformBitmap=nullptr;
InstrumentType contextType=InstrumentType::none;
bool importing=false;
Instrument* current(){return &chipnomadState->project.instruments[cInstrument];}
bool sid(){return current()->type==InstrumentType::SID;}
bool dx7(){return current()->type==InstrumentType::DX7;}
bool fourOp(){return isFourOp(current()->type);}
int bankId(){return sid()?current()->chip.sid.bankId:isOPLL(current()->type)?current()->chip.opll.bankId:fourOp()?current()->chip.fourOp.bankId:dx7()?current()->chip.dx7.bankId:current()->chip.opl.bankId;}
const char* presetName(){return sid()?current()->chip.sid.presetName:isOPLL(current()->type)?current()->chip.opll.presetName:fourOp()?current()->chip.fourOp.presetName:dx7()?current()->chip.dx7.presetName:current()->chip.opl.presetName;}
int8_t& fineTune(){return isOPLL(current()->type)?current()->chip.opll.fineTune:fourOp()?current()->chip.fourOp.fineTune:dx7()?current()->chip.dx7.fineTune:current()->chip.opl.fineTune;}
bool compatible(const Entry& e){if(importing)return e.bank==bankFilter;return e.type==int(current()->type)||(current()->type==InstrumentType::OPL3&&e.type==int(InstrumentType::OPL2));}
void refreshLibrary(){
  // Rebuild only while no popup owns pointers into catalog strings.
  std::string priorBank;
  for(const auto& e:catalog)if(e.library&&e.bank==bankFilter){priorBank=e.bankName;break;}
  catalog.erase(std::remove_if(catalog.begin(),catalog.end(),[](const auto& e){return e.library;}),catalog.end());
  auto library=scanDX7Library(folder+"../banks/dx7/");
  if(catalog.size()+library.entries.size()>65536){library.entries.resize(65536-catalog.size());library.limited=true;}
  libraryDX7=std::move(library.patches);
  catalog.insert(catalog.end(),library.entries.begin(),library.entries.end());
  if(!priorBank.empty()){
    bankFilter=0;for(const auto& e:catalog)if(e.library&&e.bankName==priorBank){bankFilter=e.bank;break;}
  }
  if(library.skipped||library.limited)screenMessage(MESSAGE_TIME_ERROR,"DX7: %d skipped%s",library.skipped,library.limited?", limit reached":"");
}
bool readCatalog(){
  if(catalogRead)return true;
  folder="instruments/chips/";
  bool external=fileIsRunningFromAppImage();
#ifdef ANDROID_BUILD
  external=true;
#endif
  if(external){char root[1024];if(fileGetDefaultDirectory(root,sizeof(root)))return false;folder=std::string(root)+"/instruments/chips/";}
  loadFMCatalog((folder+"catalog.tsv").c_str(),catalog);
  catalogRead=true;refreshLibrary();return true;
}
bool candidate(int index,Instrument& result){
  if(index<0||index>=int(catalog.size())||!compatible(catalog[index]))return false;
  if(catalog[index].imported>=0) {
    getInstrumentFunctions(InstrumentType::DX7).init(&result);result.chip.dx7=(catalog[index].library?libraryDX7:importedDX7)[catalog[index].imported];
    if(auto* amp=instrumentFMAmpSettings(current()))result.chip.dx7.amp=*amp;
    if(auto* tone=instrumentFMToneSettings(current()))result.chip.dx7.tone=*tone;
    strncpy(result.name,result.chip.dx7.presetName,PROJECT_INSTRUMENT_NAME_LENGTH);return true;
  }
  auto p=std::make_unique<Project>();projectInit(p.get());
  if(instrumentLoad(p.get(),(folder+catalog[index].path).c_str(),0)){projectFree(p.get());return false;}
  bool ok=p->instruments[0].type==InstrumentType::SID?validSID(p->instruments[0].chip.sid):isOPLL(p->instruments[0].type)?p->instruments[0].chip.opll.schema==1:isFourOp(p->instruments[0].type)?validFourOp(p->instruments[0].type,p->instruments[0].chip.fourOp):p->instruments[0].type==InstrumentType::DX7?validDX7(p->instruments[0].chip.dx7):isOPL(p->instruments[0].type)&&validOPL(p->instruments[0].type,p->instruments[0].chip.opl);
  ok=ok&&int(p->instruments[0].type)==catalog[index].type;
  if(ok){result=p->instruments[0];result.type=current()->type;
    if(auto* amp=instrumentFMAmpSettings(current()))*instrumentFMAmpSettings(&result)=*amp;
    if(auto* tone=instrumentFMToneSettings(current()))*instrumentFMToneSettings(&result)=*tone;
  }
  projectFree(p.get());return ok;
}
void stopPreview(){if(sid()){chipnomadQueueSIDPreview(chipnomadState,*pSongTrack,nullptr);return;}if(isOPLL(current()->type)&&!importing){chipnomadQueueOPLLPreview(chipnomadState,*pSongTrack,nullptr);return;}if(fourOp()&&!importing){chipnomadQueueFourOpPreview(chipnomadState,*pSongTrack,current()->type,nullptr);return;}if(dx7()||importing){chipnomadQueueDX7Preview(chipnomadState,*pSongTrack,nullptr);return;}chipnomadQueueOPLPreview(chipnomadState,*pSongTrack,current()->type,nullptr);}
void preview(int index,bool held){
  Instrument patch{};
  if(held&&candidate(index,patch)){if(patch.type==InstrumentType::SID)chipnomadQueueSIDPreview(chipnomadState,*pSongTrack,&patch.chip.sid);else if(isOPLL(patch.type))chipnomadQueueOPLLPreview(chipnomadState,*pSongTrack,&patch.chip.opll);else if(isFourOp(patch.type))chipnomadQueueFourOpPreview(chipnomadState,*pSongTrack,patch.type,&patch.chip.fourOp);else if(patch.type==InstrumentType::DX7)chipnomadQueueDX7Preview(chipnomadState,*pSongTrack,&patch.chip.dx7);else chipnomadQueueOPLPreview(chipnomadState,*pSongTrack,patch.type,&patch.chip.opl);}
  else stopPreview();
}
void cancel(){stopPreview();if(importing)bankFilter=bankBeforeImport;importing=false;screenSetup(&screenInstrument,cInstrument);}
void select(int index){
  stopPreview();Instrument patch{};
  if(candidate(index,patch)){instrumentClear(current());*current()=patch;projectModified=1;}
  else screenMessage(MESSAGE_TIME_ERROR,"Preset could not load");
  importing=false;screenSetup(&screenInstrument,cInstrument);
}
int selected(){for(size_t i=0;i<catalog.size();++i){const auto& e=catalog[i];if(e.bank==bankId()&&e.name==presetName()&&compatible(e))return i;}return -1;}
void selectBank(int value){bankFilter=value;screenSetup(&screenInstrument,cInstrument);}
void openBanks(){
  bool alreadyRead=catalogRead;
  if(!readCatalog()){screenMessage(MESSAGE_TIME_ERROR,"Factory catalog missing");return;}
  if(dx7()&&alreadyRead)refreshLibrary();
  bankItems.clear();bankItems.push_back({"All banks",0,nullptr,0});
  std::set<int> listed;
  for(const auto& e:catalog)if(compatible(e)&&listed.insert(e.bank).second)bankItems.push_back({e.bankName.c_str(),e.bank,nullptr,0});
  char title[32];snprintf(title,sizeof(title),"%s BANKS",instrumentTypeName(current()->type));
  selectionPopupSetup(title,bankItems.data(),bankItems.size(),bankFilter,selectBank,cancel,true);screenSetup(&screenSelectionPopup,0);
}
void openSounds(){
  if(!readCatalog()){screenMessage(MESSAGE_TIME_ERROR,"Factory catalog missing");return;}
  if(!importing&&bankFilter&&std::none_of(catalog.begin(),catalog.end(),[](const auto& e){return compatible(e)&&e.bank==bankFilter;}))bankFilter=0;
  categoryNames={"All"};
  for(const auto& e:catalog)if(compatible(e)&&(!bankFilter||e.bank==bankFilter)&&std::find(categoryNames.begin(),categoryNames.end(),e.category)==categoryNames.end())categoryNames.push_back(e.category);
  sounds.clear();sounds.resize(categoryNames.size());categoryItems.clear();
  for(size_t i=0;i<catalog.size();++i){const auto& e=catalog[i];if(!compatible(e)||(bankFilter&&e.bank!=bankFilter))continue;
    for(size_t c=0;c<categoryNames.size();++c)if(c==0||categoryNames[c]==e.category)sounds[c].push_back({e.name.c_str(),int(i),nullptr,0,e.name.c_str()});}
  for(size_t c=0;c<categoryNames.size();++c)categoryItems.push_back({categoryNames[c].c_str(),-1,sounds[c].data(),int(sounds[c].size())});
  char title[32];snprintf(title,sizeof(title),"%s PRESETS",instrumentTypeName(importing?InstrumentType::DX7:current()->type));
  selectionPopupSetup(title,categoryItems.data(),categoryItems.size(),selected(),select,cancel,false,preview);screenSetup(&screenSelectionPopup,0);
}
int sidField(int col,int row){
 if(row==5)return col?sidPulse:sidWave;
 if(row==6)return col==0?sidFilterMode:col==1?sidCutoff:sidResonance;
 return sidAttack+col;
}
int sidY(int row){return row==5?9:row==6?11:13;}
int sidX(int col,int row){return 9+col*(row==7?6:10);}
int columns(int row){if(sid()&&row>=5)return row==5?2:row==6?3:4;return row<3?instrumentCommonColumnCount(row):row==6?2:row==8?5:1;}
void drawStatic(){instrumentCommonDrawStatic();gfxSetFgColor(appSettings.colorScheme.textDefault);gfxPrint(0,6,"Bank");gfxPrint(0,7,"Preset");if(sid()){
 gfxPrint(0,9,"Wave/PW");gfxPrint(0,11,"Filter");gfxPrint(0,13,"ADSR");
 gfxSetFgColor(appSettings.colorScheme.textInfo);gfxPrint(0,10,"Tri Saw TS Pulse TP SP TSP Noise");
 gfxPrint(0,12,"Mode / Cutoff / Reso; max 4 notes");if(!sidWaveformBitmap)sidWaveformBitmap=gfxBitmapCreate(32,3);gfxClearRect(0,16,32,3);renderFMPreview(sidWaveformBitmap,current());gfxDrawBitmap(sidWaveformBitmap,0,16);return;
 }gfxPrint(0,9,"Fine ct");gfxPrint(0,11,"Mode");const char* mode=isOPLL(current()->type)?"2 operator":fourOp()?"4 operator":dx7()?"6 operator":current()->chip.opl.topology==OPLTopology::fourOperator?"4 operator":current()->chip.opl.topology==OPLTopology::dualVoice?"Dual voice":"2 operator";gfxPrint(9,11,mode);instrumentFMAmpDrawStatic();}
void drawCursor(int col,int row){if(sid()&&row>=5){gfxCursor(sidX(col,row),sidY(row),row==5&&!col?5:4);return;}if(row==6){instrumentFMToneDrawCursor(col);return;}if(row>=7){instrumentFMAmpDrawCursor(col,row-7);return;}if(row<3)instrumentCommonDrawCursor(col,row);else gfxCursor(9,row==3?6:row==4?7:9,row==5?4:28);}
void drawField(int col,int row,CellState state){
 if(sid()&&row>=5){
  int x=sidX(col,row),y=sidY(row);auto v=current()->chip.sid.value[sidField(col,row)];
  gfxSetFgColor(state==CellState::focus?appSettings.colorScheme.textValue:appSettings.colorScheme.textDefault);gfxClearRect(x,y,row==7?4:8,1);
  if(row==5&&!col){const char* waves[]={"Off","Tri","Saw","TS","Pulse","TP","SP","TSP","Noise"};gfxPrint(x,y,waves[v]);}
  else gfxPrintf(x,y,"%0*X",row==5||(row==6&&col==1)?3:2,v);
  if(sidWaveformBitmap){gfxClearRect(0,16,32,3);renderFMPreview(sidWaveformBitmap,current());gfxSetFgColor(appSettings.colorScheme.textInfo);gfxDrawBitmap(sidWaveformBitmap,0,16);}
  return;
 }

  if(row==6){instrumentFMToneDrawField(col,state);return;}if(row>=7){instrumentFMAmpDrawField(col,row-7,state);return;}
  if(row<3){instrumentCommonDrawField(col,row,state);return;}
  gfxSetFgColor(state==CellState::focus?appSettings.colorScheme.textValue:appSettings.colorScheme.textDefault);int y=row==3?6:row==4?7:9;gfxClearRect(9,y,30,1);
  if(row==3){const char* name="All banks";for(const auto& e:catalog)if(e.bank==bankFilter){name=e.bankName.c_str();break;}gfxPrintf(9,y,"%.30s",name);}
  else if(row==4)gfxPrintf(9,y,"%.30s",presetName());else gfxPrintf(9,y,"%+04d",fineTune());
  instrumentFMRefreshStaticWaveform();
}
int onEdit(int col,int row,CellEditAction action){
 if(sid()&&row>=5){
  int field=sidField(col,row),max=field==sidWave?8:field==sidPulse?4095:field==sidCutoff?2047:field==sidFilterMode?7:15;
  int min=field==sidWave?1:0,step=max>255?128:max>15?16:1;
  action=convertMultiAction(action);int v=current()->chip.sid.value[field];
  if(action==CellEditAction::clear)v=min;else if(action==CellEditAction::increase)++v;else if(action==CellEditAction::decrease)--v;
  else if(action==CellEditAction::increaseBig)v+=step;else if(action==CellEditAction::decreaseBig)v-=step;else return 0;
  current()->chip.sid.value[field]=std::clamp(v,min,max);projectModified=1;return 1;
 }

  if(row==6)return instrumentFMToneEdit(col,action);
  if(row>=7)return instrumentFMAmpEdit(col,row-7,action);
  if(row<3)return instrumentCommonOnEdit(col,row,action);
  if(row==3){openBanks();return 1;}if(row==4){openSounds();return 1;}
  action=convertMultiAction(action);int v=fineTune();
  if(action==CellEditAction::clear)v=0;else if(action==CellEditAction::increase)++v;else if(action==CellEditAction::decrease)--v;else if(action==CellEditAction::increaseBig)v+=10;else if(action==CellEditAction::decreaseBig)v-=10;else return 0;
  fineTune()=std::clamp(v,-100,100);projectModified=1;return 1;
}
int onInput(int down,int keys,int){
  int row=screenInstrumentOPL.cursorRow;if(row!=3&&row!=4){buttonDown=0;return 0;}
  PopupEditInput input=popupEditInput(down,keys,&buttonDown);
  if(input==PopupEditInput::cycle){
    if(row==4&&readCatalog()){
      int index=selected(),direction=keys==(keyEdit|keyRight)?1:-1;
      for(size_t tries=0;tries<catalog.size();++tries){index=(index+direction+catalog.size())%catalog.size();if(compatible(catalog[index])&&(!bankFilter||catalog[index].bank==bankFilter)){select(index);break;}}
    }return 1;
  }
  if(input==PopupEditInput::hold)return 1;
  if(input==PopupEditInput::open){if(row==3)openBanks();else openSounds();return 1;}return 0;
}
}
ScreenData screenInstrumentOPL={
 .rows=9,.cursorRow=0,.cursorCol=0,.topRow=0,.selectMode=-1,.selectStartRow=0,.selectStartCol=0,.selectAnchorRow=0,.selectAnchorCol=0,
 .playbackLevel=ScreenPlaybackLevel::none,.getColumnCount=columns,.drawStatic=drawStatic,.drawCursor=drawCursor,.drawSelection=nullptr,.drawRowHeader=nullptr,.drawColHeader=nullptr,
 .drawField=drawField,.onEdit=onEdit,.onInput=onInput,.onRawInput=nullptr,.isCellValid=nullptr,.getLoopRange=nullptr,
};

void instrumentFMSetContext(int instrument, InstrumentType type) {
  screenInstrumentOPL.rows=type==InstrumentType::SID?8:9;
  if(instrument!=contextInstrument||type!=contextType) {
    bankFilter=0;
    buttonDown=0;
    contextInstrument=instrument;
    contextType=type;
  }
}

void instrumentFMImportSysEx(const char* path) {
  FILE* f=fopen(path,"rb");
  if(!f){screenMessage(MESSAGE_TIME_ERROR,"Could not open SysEx");screenSetup(&screenInstrument,cInstrument);return;}
  std::vector<uint8_t> bytes(1024*1024+1);size_t size=fread(bytes.data(),1,bytes.size(),f);bool ioError=ferror(f);fclose(f);
  std::vector<InstrumentDX7> patches;std::string error;
  if(ioError||!importDX7SysEx(bytes.data(),size,patches,error)) {
    screenMessage(MESSAGE_TIME_ERROR,"%s",ioError?"SysEx read failed":error.c_str());screenSetup(&screenInstrument,cInstrument);return;
  }
  readCatalog();
  // Metadata browsing and patch materialization happen on the UI thread only.
  if(catalog.size()+patches.size()>65536){screenMessage(MESSAGE_TIME_ERROR,"FM catalog limit reached");screenSetup(&screenInstrument,cInstrument);return;}
  int bank=32768;for(const auto& e:catalog)if(!e.library)bank=std::max(bank,e.bank+1);
  if(bank>=40000){screenMessage(MESSAGE_TIME_ERROR,"FM bank limit reached");return;}
  const char* name=strrchr(path,PATH_SEPARATOR);name=name?name+1:path;
  for(auto& p:patches){p.bankId=bank;int index=importedDX7.size();importedDX7.push_back(p);catalog.push_back({int(InstrumentType::DX7),bank,name,"Unsorted",p.presetName,"",index});}
  // Opening a bank is transactional even when the current instrument isn't DX7.
  // Compatible imported entries are offered; selection performs the type change.
  bankBeforeImport=bankFilter;bankFilter=bank;importing=true;openSounds();
}
