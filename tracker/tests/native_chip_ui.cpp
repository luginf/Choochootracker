#include <filesystem>
// Developer-only offscreen production UI integration; never shipped.
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <memory>
#include <algorithm>
#include "chipnomad_lib.h"
#include "app.h"
#include "corelib_font.h"
#include "corelib_gfx.h"
#include "screens.h"
#include "screen_instrument.h"
#include "selection_popup.h"
#include "waveform_display.h"
#include "monitor_display.h"
#include "dx7_patch.h"
#include "sid_patch.h"
#include "project_utils.h"
#include "copy_paste.h"
#include "insert_fx.h"
#include "help.h"
extern SDL_Renderer* renderer;
static const char* output;
static void require(bool condition,const char* why){if(!condition){fprintf(stderr,"FAIL: %s\n",why);exit(2);}}
static void capture(const char* name){appDraw();auto* s=SDL_CreateRGBSurfaceWithFormat(0,640,480,32,SDL_PIXELFORMAT_ARGB8888);require(s,"surface");require(SDL_RenderReadPixels(renderer,nullptr,s->format->format,s->pixels,s->pitch)==0,"pixels");char path[2048];snprintf(path,sizeof(path),"%s/%s.bmp",output,name);require(SDL_SaveBMP(s,path)==0,"capture");SDL_FreeSurface(s);}
static void key(int down,int keys){appDraw();currentScreen->onInput(down,keys,1);appDraw();}
static void tapEdit(){key(1,keyEdit);key(0,0);}
int main(int argc,char** argv){
  if(argc!=2&&argc!=3)return 1;output=argv[1];initDefaultAppSettings();appSettings.screenWidth=640;appSettings.screenHeight=480;fontSetCurrent(fontGetDefault());require(!gfxSetup(&appSettings.screenWidth,&appSettings.screenHeight),"SDL dummy setup");
  chipnomadState=chipnomadCreate();require(chipnomadState,"state");require(!projectLoad(&chipnomadState->project,"projects/gm-midi-demo.cct"),"fixture");
  chipnomadInitChips(chipnomadState,48000,nullptr);chipnomadReserveRenderBuffers(chipnomadState,1024);screensInitAll();waveformDisplayInit();monitorDisplayInit();
  if(argc==3 && !strcmp(argv[2],"--fx-only")) {
    for(auto type:{InstrumentType::OPLL,InstrumentType::VRC7,InstrumentType::OPL2,InstrumentType::OPL3,InstrumentType::GenesisFM,InstrumentType::ArcadeFM,InstrumentType::DX7}) {
      auto& inst=chipnomadState->project.instruments[0];getInstrumentFunctions(type).init(&inst);
      require(!instrumentFXAvailableForInstrument(&inst,fxFBK+1),"selector removed");
      for(int command=fxOAR;command<=fxLEN;++command) {
        uint8_t fx[]={EMPTY_VALUE_8,0};selectInstrumentFX(fx,command,0);
        require((fx[0]==command)==bool(instrumentFXAvailableForInstrument(&inst,command)),"selection follows compact list");
      }
      *pSongTrack=0;*pSongRow=0;*pChainRow=0;
      chipnomadState->project.song[0][0]=0;
      chipnomadState->project.chains[0].rows[0].phrase=0;
      auto& row=chipnomadState->project.phrases[0].rows[0];
      row.instrument=0;row.note=45;row.fx[0][0]=type==InstrumentType::DX7?fxFBK:fxOMU;
      for(int header:{0,1}) {
        appSettings.persistentWaveform=header;
        screenPhrase.init();screenSetup(&screenPhrase,0);appDraw();
        for(int column=0;column<3;++column)key(1,keyRight);
        key(1,keyEdit|keyUp);
        char name[64];snprintf(name,sizeof(name),"compact-fx-%d-header-%d",int(type),header);capture(name);
      }
    }
    printf("Focused FX selection and popup captures passed\n");
    chipnomadDestroy(chipnomadState);SDL_Quit();return 0;
  }
  // The accepted insert review shares popup controls with native-chip browsing.
  // Check the full-height page and category selection in both waveform modes.
  for (int header : {0, 1}) {
    appSettings.persistentWaveform = header;
    *pSongTrack = 0;
    insertSelect(&chipnomadState->project.trackInserts[0][0], insertDistortion);
    screenSetup(&screenInsertFX, -1); appDraw();
    require(gfxGetContentRowOffset() == 0, "Insert FX keeps accepted full-height layout");
    capture(header ? "insert-header-on" : "insert-header-off");
    key(1, keyEdit); key(0, 0);
    require(currentScreen == &screenSelectionPopup && selectionPopupIsFullWidth(),
            "insert chooser opens full width");
    capture(header ? "insert-chooser-header-on" : "insert-chooser-header-off");
    key(1, keyUp); key(1, keyRight); key(1, keyDown);
    capture("insert-dynamics-ott");
    tapEdit();
    require(currentScreen == &screenInsertFX, "insert choice returns to page");
    require(chipnomadState->project.trackInserts[0][0].module == insertOTT,
            "Dynamics category selects OTT");
    insertSelect(&chipnomadState->project.trackInserts[0][0], insertOff);
  }
  // This harness calls screen input directly; normal app button release clears tips.
  screenMessage(0, "");
  appSettings.persistentWaveform = 0;
  printf("Insert review UI passed: full-height page, grouped chooser, waveform off/on\n");
  // Exercise the real Type popup; direct instrument initialization cannot catch
  // a stale category count hiding otherwise functional instrument pages.
  int fmIndex = 0;
  for (auto type : {InstrumentType::OPLL, InstrumentType::VRC7, InstrumentType::OPL2,
                   InstrumentType::OPL3, InstrumentType::GenesisFM,
                   InstrumentType::ArcadeFM, InstrumentType::DX7}) {
    getInstrumentFunctions(InstrumentType::AY1).init(&chipnomadState->project.instruments[0]);
    screenSetup(&screenInstrument, 0);
    appDraw();
    key(1, keyEdit); key(0, 0);
    require(currentScreen == &screenSelectionPopup, "Type popup opens on EDIT release");
    key(1, keyUp); // CHIP wraps to the FM category.
    key(1, keyRight);
    for (int i = 0; i < fmIndex; ++i) key(1, keyDown);
    if (type == InstrumentType::DX7) capture("type-fm-dx7");
    tapEdit();
    require(currentScreen == &screenInstrument, "Type popup selection returns to instrument");
    require(chipnomadState->project.instruments[0].type == type, "all seven FM types selectable through popup");
    key(1, keyEdit); key(0, 0); key(1, keyRight); tapEdit();
    require(chipnomadState->project.instruments[0].type == type, "Type popup reopens on selected FM type");
    ++fmIndex;
  }
  printf("Type popup passed: all seven FM types selected and reopened\n");
  for(auto type:{InstrumentType::SID,InstrumentType::OPLL,InstrumentType::VRC7,InstrumentType::OPL2,InstrumentType::OPL3,InstrumentType::SegaPSG,InstrumentType::GBPulse,InstrumentType::GBNoise,InstrumentType::GenesisFM,InstrumentType::ArcadeFM,InstrumentType::DX7}){
    getInstrumentFunctions(type).init(&chipnomadState->project.instruments[0]);screenSetup(&screenInstrument,0);appDraw();if(type==InstrumentType::SegaPSG)require(screenInstrumentSimpleChip.isCellValid(0,6),"Sega bass extension selectable");char name[40];snprintf(name,sizeof(name),"instrument-%d",int(type));capture(name);
  }
  require(!strcmp(instrumentTypeName(InstrumentType::SID),"SID"),"SID instrument and browser titles identify the engine");
  // Real SDL pixel regression: incremental ADSR edits must match a fresh draw.
  auto pixels=[](){std::vector<uint32_t> p(640*480);require(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,p.data(),640*4)==0,"read graph pixels");return p;};
  // A bank stays selected within its engine, but switching engines must return
  // to All, even between OPL2 and OPL3 which share compatible bank IDs.
  appSettings.persistentWaveform=0;
  for(auto type:{InstrumentType::SID,InstrumentType::OPLL,InstrumentType::VRC7,InstrumentType::OPL2,InstrumentType::OPL3,InstrumentType::GenesisFM,InstrumentType::ArcadeFM,InstrumentType::DX7}) {
    getInstrumentFunctions(type).init(&chipnomadState->project.instruments[0]);screenSetup(&screenInstrument,0);appDraw();
    screenInstrumentOPL.onEdit(0,3,CellEditAction::tap);appDraw();auto allBanks=pixels();
    key(1,keyDown);tapEdit();screenInstrumentOPL.onEdit(0,3,CellEditAction::tap);appDraw();
    require(pixels()!=allBanks,"bank selection persists within engine");key(1,keyOpt);
    getInstrumentFunctions(InstrumentType::GBPulse).init(&chipnomadState->project.instruments[0]);screenSetup(&screenInstrument,0);appDraw();
    getInstrumentFunctions(type).init(&chipnomadState->project.instruments[0]);screenSetup(&screenInstrument,0);appDraw();
    screenInstrumentOPL.onEdit(0,3,CellEditAction::tap);appDraw();require(pixels()==allBanks,"engine round trip resets bank to All");
    char name[64];snprintf(name,sizeof(name),"banks-%d-all",int(type));capture(name);key(1,keyOpt);
    if(type==InstrumentType::OPL3) {
      getInstrumentFunctions(InstrumentType::OPL2).init(&chipnomadState->project.instruments[0]);screenSetup(&screenInstrument,0);appDraw();
      screenInstrumentOPL.onEdit(0,3,CellEditAction::tap);key(1,keyDown);tapEdit();
      getInstrumentFunctions(type).init(&chipnomadState->project.instruments[0]);screenSetup(&screenInstrument,0);appDraw();
      screenInstrumentOPL.onEdit(0,3,CellEditAction::tap);appDraw();require(pixels()==allBanks,"compatible OPL bank cannot carry across engines");key(1,keyOpt);
    }
  }
  for(int fx=fxFBR;fx<=fxFBK;++fx) {
    const char* description=helpFXDescription((FX)fx,0);
    require(description&&description[0]&&strchr(description,'\n'),"native FX has title and description");
    uint8_t value[]={uint8_t(fx),0};require(helpFXHint(value,0,0)[0],"native FX has value hint");
  }
  for(auto type:{InstrumentType::SegaPSG,InstrumentType::GBPulse,InstrumentType::GBNoise}) {
    getInstrumentFunctions(type).init(&chipnomadState->project.instruments[0]);
    require(strstr(helpFXDescription(fxCMD,0),type==InstrumentType::SegaPSG?"Tone / Noise":type==InstrumentType::GBPulse?"Pulse Duty":"Noise Width"),"chip mode description follows engine");
  }
  printf("Bank reset and native FX description regressions passed\n");
  for(auto type:{InstrumentType::OPLL,InstrumentType::VRC7,InstrumentType::OPL2,InstrumentType::OPL3,InstrumentType::GenesisFM,InstrumentType::ArcadeFM,InstrumentType::DX7,InstrumentType::SegaPSG,InstrumentType::GBPulse,InstrumentType::GBNoise,InstrumentType::SID}) {
    auto& inst=chipnomadState->project.instruments[0];getInstrumentFunctions(type).init(&inst);
    for(int command=fxFBR;command<fxTotalCount;++command) {
      NativeFXInfo info{};if(!instrumentFXAvailableForInstrument(&inst,command)||!instrumentNativeFXInfo(&inst,command,&info))continue;
      uint8_t fx[]={EMPTY_VALUE_8,255},last[]={uint8_t(command),255};
      selectInstrumentFX(fx,command,0);require(fx[1]==info.preset,"new native FX starts at instrument value");
      fx[1]=info.minimum;selectInstrumentFX(fx,command,0);require(fx[1]==info.minimum,"existing native FX value preserved");
      for(auto action:{CellEditAction::increase,CellEditAction::increaseBig,CellEditAction::multiIncreaseBig}) {
        fx[1]=info.maximum;editFXValue(action,fx,last,0,0);require(fx[1]==info.maximum,"native upper bound");
      }
      for(auto action:{CellEditAction::decrease,CellEditAction::decreaseBig}) {
        fx[1]=info.minimum;editFXValue(action,fx,last,1,0);require(fx[1]==info.minimum,"native lower bound in table");
      }
      fx[1]=255;editFXValue(CellEditAction::tap,fx,last,0,0);require(fx[1]<=info.maximum,"out of range cached FX repaired on edit");
    }
  }
  getInstrumentFunctions(InstrumentType::DX7).init(&chipnomadState->project.instruments[0]);
  chipnomadState->project.instruments[0].chip.dx7.voice[5*21+16]=42;
  auto& fxProject=chipnomadState->project;
  auto phraseBackup=fxProject.phrases[0];auto chainBackup=fxProject.chains[0];auto songBackup=fxProject.song[0][0];
  *pSongRow=*pChainRow=*pSongTrack=0;fxProject.song[0][0]=0;fxProject.chains[0].rows[0].phrase=0;
  phraseClear(&fxProject.phrases[0]);fxProject.phrases[0].rows[0].instrument=0;
  getInstrumentFunctions(InstrumentType::OPL2).init(&fxProject.instruments[1]);
  fxProject.instruments[1].chip.opl.operators[0].level=20;
  fxProject.phrases[0].rows[1].instrument=1;
  require(lookupInstrument(&fxProject,0,0,0,0)==0&&lookupInstrument(&fxProject,0,0,2,0)==1,"FX lookup follows explicit and inherited I column");
  uint8_t inherited[]={EMPTY_VALUE_8,255};selectInstrumentFX(inherited,fxOL1,lookupInstrument(&fxProject,0,0,2,0));
  require(inherited[1]==43,"inherited instrument supplies its own operator level");
  for(int header:{0,1}) {
    appSettings.persistentWaveform=header;screenSetup(&screenPhrase,-1);appDraw();
    fxProject.phrases[0].rows[0].fx[0][0]=EMPTY_VALUE_8;
    screenPhrase.init();
    for(int col=0;col<3;++col)key(1,keyRight);
    key(1,keyEdit|keyUp);
    fxEditFullDraw(fxOL1,0,0);capture(header?"fx-native-dx7-header-on":"fx-native-dx7-header-off");
    fxEditFullDraw(fxFBK,0,0);capture(header?"fx-native-last-header-on":"fx-native-last-header-off");
    fxEditFullDraw(fxOL1,0,0);
    screenPhrase.onInput(0,0,1);
    auto* fx=fxProject.phrases[0].rows[0].fx[0];require(fx[0]==fxOL1&&fx[1]==42,"popup commit reads instrument preset");
  }
  getInstrumentFunctions(InstrumentType::GBPulse).init(&chipnomadState->project.instruments[0]);
  screenSetup(&screenPhrase,-1);appDraw();screenPhrase.init();
  for(int col=0;col<3;++col)key(1,keyRight);
  key(1,keyEdit|keyUp);
  fxEditFullDraw(fxCMD,0,0);capture("fx-native-duty");
  screenPhrase.onInput(0,0,1);
  fxProject.phrases[0]=phraseBackup;fxProject.chains[0]=chainBackup;fxProject.song[0][0]=songBackup;
  printf("Native FX popup preset values, selection defaults and engine bounds passed\n");
  for(int header:{0,1}){
    appSettings.persistentWaveform=header;getInstrumentFunctions(InstrumentType::SID).init(&chipnomadState->project.instruments[0]);screenSetup(&screenInstrument,0);appDraw();
    for(int row=5;row<=7;++row)for(int col=0;col<screenInstrumentOPL.getColumnCount(row);++col){screenInstrumentOPL.onEdit(col,row,CellEditAction::increase);screenInstrumentOPL.drawField(col,row,CellState::normal);appDraw();}
    auto incremental=pixels();currentScreen->fullRedraw();appDraw();auto clean=pixels();int start=(8+gfxGetContentRowOffset())*gfxGetCharHeight()*640;
    require(std::equal(incremental.begin()+start,incremental.end(),clean.begin()+start),"SID controls incremental pixels match full redraw");capture(header?"sid-controls-header-on":"sid-controls-header-off");
    require(screenInstrumentOPL.rows==8,"SID has only implemented synthesis controls");
  }

  printf("SID controls passed: native waveform, pulse width, filter and ADSR; no alternate model options\n");
  for(int header:{0,1})for(auto type:{InstrumentType::SegaPSG,InstrumentType::GBPulse,InstrumentType::GBNoise,InstrumentType::OPLL,InstrumentType::VRC7,InstrumentType::OPL2,InstrumentType::OPL3,InstrumentType::GenesisFM,InstrumentType::ArcadeFM,InstrumentType::DX7}) {
    appSettings.persistentWaveform=header;getInstrumentFunctions(type).init(&chipnomadState->project.instruments[0]);screenSetup(&screenInstrument,0);appDraw();
    auto* amp=instrumentFMAmpSettings(&chipnomadState->project.instruments[0]);
    if(amp){instrumentFMAmpEdit(0,0,CellEditAction::increase);require(amp->enabled,"FM amp toggle");instrumentFMToneEdit(0,CellEditAction::increase);instrumentFMToneEdit(1,CellEditAction::increase);appDraw();}
    for(int edit=0;edit<12;++edit){
      int col=edit%4;
      if(amp){instrumentFMAmpEdit(col,1,CellEditAction::increaseBig);instrumentFMAmpDrawField(col,1,CellState::normal);}
      else{screenInstrumentSimpleChip.onEdit(col,7,CellEditAction::increaseBig);screenInstrumentSimpleChip.drawField(col,7,CellState::normal);}
      appDraw();
    }
    auto incremental=pixels();currentScreen->fullRedraw();appDraw();auto clean=pixels();
    // Below row 6 excludes the independently refreshed persistent waveform.
    int start=(6+gfxGetContentRowOffset())*gfxGetCharHeight()*640;
    require(std::equal(incremental.begin()+start,incremental.end(),clean.begin()+start),"ADSR incremental pixels match full redraw");
    char name[64];snprintf(name,sizeof(name),"adsr-%d-header-%d",int(type),header);capture(name);
  }
  printf("ADSR pixel regression passed: ten engines, header off/on, twelve edits each\n");
  appSettings.persistentWaveform=1;getInstrumentFunctions(InstrumentType::DX7).init(&chipnomadState->project.instruments[0]);screenSetup(&screenInstrument,0);appDraw();
  auto before=std::make_unique<Project>(chipnomadState->project);screenInstrumentOPL.onEdit(0,4,CellEditAction::tap);appDraw();require(currentScreen==&screenSelectionPopup,"shared FM browser");capture("dx7-categories");key(1,keyRight);capture("dx7-presets");
  auto popupPixels = pixels();
  screenSelectionPopup.fullRedraw();
  require(popupPixels == pixels(), "app overlays must not cover native preset lists or footer");
  key(1,keyEdit);require(currentScreen==&screenSelectionPopup,"EDIT waits to permit preview chord");key(1,keyEdit|keyPlay);
  std::vector<float> audio(2048);double energy=0;for(int n=0;n<12;++n){chipnomadRender(chipnomadState,audio.data(),1024);for(float x:audio)energy+=x*x;}require(energy>1e-5,"audition audio");require(!memcmp(before.get(),&chipnomadState->project,sizeof(Project)),"audition mutation");key(0,keyPlay);key(1,keyOpt);require(!memcmp(before.get(),&chipnomadState->project,sizeof(Project)),"cancel mutation");
  screenInstrumentOPL.onEdit(0,4,CellEditAction::tap);appDraw();key(1,keyRight);tapEdit();require(currentScreen==&screenInstrument,"confirm returns");require(!memcmp(before->tables,chipnomadState->project.tables,sizeof(before->tables)),"table changed");require(!memcmp(before->trackInserts,chipnomadState->project.trackInserts,sizeof(before->trackInserts)),"insert changed");capture("dx7-loaded");
  // Local SysEx opens the same transactional browser from another instrument.
  InstrumentDX7 patch{};initDX7Patch(&patch);char path[2048];snprintf(path,sizeof(path),"%s/original-test.syx",output);FILE* f=fopen(path,"wb");require(f,"syx fixture");uint8_t h[]={240,67,0,0,1,27};fwrite(h,1,6,f);fwrite(patch.voice,1,155,f);unsigned sum=0;for(auto b:patch.voice)sum+=b;fputc((-sum)&127,f);fputc(247,f);fclose(f);
  // A bank dropped in the persistent hierarchy appears without Load Instrument.
  const char* libraryFile="instruments/banks/dx7/UI-test.syx";
  require(!std::filesystem::exists(libraryFile),"isolated persistent library fixture");
  std::filesystem::copy_file(path,libraryFile);
  getInstrumentFunctions(InstrumentType::DX7).init(&chipnomadState->project.instruments[0]);screenSetup(&screenInstrument,0);appDraw();
  screenInstrumentOPL.onEdit(0,3,CellEditAction::tap);appDraw();capture("dx7-persistent-banks");
  // All, three shipped banks, then the newly discovered file.
  for(int n=0;n<4;++n)key(1,keyDown);tapEdit();
  screenInstrumentOPL.onEdit(0,4,CellEditAction::tap);key(1,keyRight);tapEdit();
  require(chipnomadState->project.instruments[0].chip.dx7.bankId>=40000,"persistent bank selected");
  require(!memcmp(patch.voice,chipnomadState->project.instruments[0].chip.dx7.voice,155),"persistent bank voice exact");
  auto savedLibraryPatch=chipnomadState->project.instruments[0];
  std::filesystem::remove(libraryFile);
  screenInstrumentOPL.onEdit(0,3,CellEditAction::tap);key(1,keyOpt);
  require(!memcmp(&savedLibraryPatch,&chipnomadState->project.instruments[0],sizeof(Instrument)),"rescan preserves selected patch after file removal");
  printf("Persistent DX7 folder discovery, selection and removal passed\n");
  getInstrumentFunctions(InstrumentType::MME).init(&chipnomadState->project.instruments[0]);*before=chipnomadState->project;instrumentFMImportSysEx(path);appDraw();require(currentScreen==&screenSelectionPopup,"local import browser");require(!memcmp(before.get(),&chipnomadState->project,sizeof(Project)),"import must not commit");capture("dx7-user-bank");key(1,keyRight);tapEdit();require(chipnomadState->project.instruments[0].type==InstrumentType::DX7,"import commit type");require(!memcmp(patch.voice,chipnomadState->project.instruments[0].chip.dx7.voice,155),"import patch");
  auto original=chipnomadState->project.instruments[0];copyInstrument(0);pasteInstrument(3);require(!memcmp(&original,&chipnomadState->project.instruments[3],sizeof(Instrument)),"DX7 paste");
  require(cloneInstrument(0,4),"DX7 clone");require(!memcmp(&original,&chipnomadState->project.instruments[4],sizeof(Instrument)),"DX7 clone payload");
  for(auto type:{InstrumentType::SID,InstrumentType::OPLL,InstrumentType::VRC7,InstrumentType::GenesisFM,InstrumentType::ArcadeFM}){getInstrumentFunctions(type).init(&chipnomadState->project.instruments[0]);screenSetup(&screenInstrument,0);appDraw();screenInstrumentOPL.onEdit(0,4,CellEditAction::tap);appDraw();require(currentScreen==&screenSelectionPopup,"four-op browser");key(1,keyRight);key(1,keyEdit);key(1,keyEdit|keyPlay);for(int n=0;n<8;++n)chipnomadRender(chipnomadState,audio.data(),1024);key(0,keyPlay);tapEdit();require(currentScreen==&screenInstrument,"four-op confirm");char name[64];snprintf(name,sizeof(name),"four-op-%d-loaded",int(type));capture(name);}
  // Actual sequencer playback with UI rendering and the existing song sends.
  for(auto& i:chipnomadState->project.instruments)if(i.type==InstrumentType::Midi)getInstrumentFunctions(InstrumentType::DX7).init(&i);
  require(chipnomadQueueProjectRefresh(chipnomadState),"snapshot");chipnomadQueuePlaybackStartSong(chipnomadState,0,0,1);energy=0;
  for(int n=0;n<24;++n){chipnomadRender(chipnomadState,audio.data(),1024);appDraw();SDL_RenderFlush(renderer);for(float x:audio)energy+=x*x;}
  require(energy>0.001,"sequencer playback");capture("dx7-song-playing");printf("UI smoke passed; sequencer playback and drawing verified\n");
  screenSetup(&screenInstrument,0);appDraw();screenInstrumentOPL.onEdit(0,4,CellEditAction::tap);appDraw();
  require(currentScreen==&screenSelectionPopup,"browser during playback");capture("fm-presets-during-playback");
  key(1,keyRight);key(1,keyEdit);key(1,keyEdit|keyPlay);chipnomadRender(chipnomadState,audio.data(),1024);
  require(chipnomadState->opllPreviewTrack<0,"playback cannot admit extra preview voice");key(0,keyPlay);key(1,keyOpt);
  // Drive the app event path, including a tick between press and release: the
  // press snapshot must not swallow the preset committed by the release.
  getInstrumentFunctions(InstrumentType::DX7).init(&chipnomadState->project.instruments[0]);screenSetup(&screenInstrument,0);appDraw();
  require(chipnomadQueueProjectRefresh(chipnomadState),"initial live patch snapshot");chipnomadRender(chipnomadState,audio.data(),1024);
  screenInstrumentOPL.onEdit(0,4,CellEditAction::tap);key(1,keyRight);key(1,keyDown);
  MainLoopEventData event{};event.type=MainLoopEvent::keyDown;event.data.input={InputDeviceType::logical,keyEdit};appOnEvent(event);appDraw();
  event.type=MainLoopEvent::tick;appOnEvent(event);chipnomadRender(chipnomadState,audio.data(),1024);
  auto oldVoice=chipnomadState->audioProject.instruments[0].chip.dx7;
  event.type=MainLoopEvent::keyUp;event.data.input={InputDeviceType::logical,keyEdit};appOnEvent(event);appDraw();
  require(currentScreen==&screenInstrument,"app release commits preset");
  require(memcmp(oldVoice.voice,chipnomadState->project.instruments[0].chip.dx7.voice,155)!=0,"live preset choice changes patch");
  event.type=MainLoopEvent::tick;appOnEvent(event);chipnomadRender(chipnomadState,audio.data(),1024);
  require(!memcmp(&chipnomadState->audioProject.instruments[0],&chipnomadState->project.instruments[0],sizeof(Instrument)),"release commit reaches audio without another input or phrase restart");
  require(chipnomadGetPlaybackStatus(chipnomadState)->isPlaying,"preset change preserves playback");
  printf("Playing preset release refresh regression passed\n");
  chipnomadDestroy(chipnomadState);SDL_Quit();return 0;
}
