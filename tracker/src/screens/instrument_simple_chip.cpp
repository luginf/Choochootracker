#include "screen_instrument.h"
#include "selection_popup.h"
#include "simple_chip_presets.h"
#include "project_utils.h"
#include "chipnomad_lib.h"
#include "corelib_gfx.h"
#include "utils.h"
#include "waveform_display.h"
#include <cstring>
#include <vector>
namespace {
int buttonDown=0;std::vector<SelectionItem> presets;Bitmap* staticWaveformBitmap=nullptr;
Instrument* current(){return &chipnomadState->project.instruments[cInstrument];}
void refreshStaticWaveform(){int row=appSettings.persistentWaveform?15:16;if(!staticWaveformBitmap)staticWaveformBitmap=gfxBitmapCreate(32,3);gfxClearRect(0,row,32,3);renderSimpleChipPreview(staticWaveformBitmap,current()->type,&current()->chip.simpleChip);gfxSetFgColor(appSettings.colorScheme.textInfo);gfxDrawBitmap(staticWaveformBitmap,0,row);auto& p=current()->chip.simpleChip;instrumentCommonDrawEnvelopePreview(p.attack,p.decay,p.sustain,p.release,p.envelopeShape);}
void stop(){chipnomadQueueSimpleChipPreview(chipnomadState,*pSongTrack,current()->type,nullptr);}
void preview(int value,bool held){Instrument i=*current();if(held&&simpleChipApplyPreset(&i,value))chipnomadQueueSimpleChipPreview(chipnomadState,*pSongTrack,i.type,&i.chip.simpleChip);else stop();}
void select(int value){stop();if(simpleChipApplyPreset(current(),value))projectModified=1;screenSetup(&screenInstrument,cInstrument);}
void cancel(){stop();screenSetup(&screenInstrument,cInstrument);}
void open(){int n=simpleChipPresetCount(current()->type);presets.resize(n);for(int i=0;i<n;++i)presets[i]={simpleChipPresetName(current()->type,i),i,nullptr,0};char title[32];snprintf(title,sizeof(title),"%s PRESETS",instrumentTypeName(current()->type));selectionPopupSetup(title,presets.data(),n,current()->chip.simpleChip.preset,select,cancel,true,preview);screenSetup(&screenSelectionPopup,0);}
int columns(int row){if(row<3)return instrumentCommonColumnCount(row);if(row==7)return 4;if(row==5)return current()->type==InstrumentType::SegaPSG?1:3;if(row==6)return current()->type==InstrumentType::GBPulse?3:current()->type==InstrumentType::GBNoise?2:1;return 1;}
int valid(int,int){return 1;}
int y(int row){return row==3?6:row+4;}
uint8_t* field(int col,int row,int& max){auto& p=current()->chip.simpleChip;max=255;
 if(row==4){max=current()->type==InstrumentType::SegaPSG?2:current()->type==InstrumentType::GBPulse?3:1;return &p.mode;}
 if(row==5){if(current()->type==InstrumentType::SegaPSG){max=3;return &p.noiseRate;}max=col==0?15:col==1?7:1;return col==0?&p.envelopeInitial:col==1?&p.envelopePeriod:&p.envelopeIncrease;}
 if(row==6){if(current()->type==InstrumentType::SegaPSG){max=1;return &p.segaBassExtension;}if(current()->type==InstrumentType::GBPulse){max=col==2?1:7;return col==0?&p.sweepPeriod:col==1?&p.sweepShift:&p.sweepNegate;}if(current()->type==InstrumentType::GBNoise){max=col==0?7:13;return col==0?&p.noiseDivisor:&p.noiseShift;}return nullptr;}
 if(row==7)return col==0?&p.attack:col==1?&p.decay:col==2?&p.sustain:&p.release;
 return nullptr;
}
void drawStatic(){instrumentCommonDrawStatic();gfxSetFgColor(appSettings.colorScheme.textDefault);gfxPrint(0,6,"Preset");gfxPrint(0,8,current()->type==InstrumentType::SegaPSG?"Mode":current()->type==InstrumentType::GBPulse?"Duty":"Width");gfxPrint(0,9,current()->type==InstrumentType::SegaPSG?"Noise rate":"GB Env");gfxPrint(0,10,current()->type==InstrumentType::GBPulse?"Sweep":current()->type==InstrumentType::GBNoise?"Div/Shift":"Bass range");gfxPrint(0,11,"Amp ADSR");gfxSetFgColor(appSettings.colorScheme.textInfo);gfxPrint(0,13,current()->type==InstrumentType::SegaPSG?"Bass extends tone / linked noise":"Env: initial / step / rise");gfxPrint(0,14,current()->type==InstrumentType::GBNoise?"Fixed noise rate; keyboard unpitched":"Software ADSR shapes native source");refreshStaticWaveform();}
void drawCursor(int col,int row){if(row<3)instrumentCommonDrawCursor(col,row);else gfxCursor(10+col*6,y(row),row==3?28:row==4?12:2);}
void drawField(int col,int row,CellState state){if(row<3){instrumentCommonDrawField(col,row,state);return;}refreshStaticWaveform();gfxSetFgColor(state==CellState::focus?appSettings.colorScheme.textValue:appSettings.colorScheme.textDefault);gfxClearRect(10+col*6,y(row),row==3?28:row==4?16:row==6&&current()->type==InstrumentType::SegaPSG?16:5,1);auto& p=current()->chip.simpleChip;
 if(row==3)gfxPrintf(10,6,"%.28s",simpleChipPresetName(current()->type,p.preset));
 else if(row==4){const char* sega[]={"Tone","White noise","Periodic"};const char* duty[]={"12.5%","25%","50%","75%"};gfxPrint(10,8,current()->type==InstrumentType::SegaPSG?sega[p.mode]:current()->type==InstrumentType::GBPulse?duty[p.mode]:p.mode?"7 bit":"15 bit");}
 else if(row==6&&current()->type==InstrumentType::SegaPSG)gfxPrint(10,10,p.segaBassExtension?"Extended":"Chip");
 else{int max;auto* v=field(col,row,max);if(v)gfxPrintf(10+col*6,y(row),"%02X",*v);}
}
int edit(int col,int row,CellEditAction action){if(row<3)return instrumentCommonOnEdit(col,row,action);if(row==3){open();return 1;}int max;auto* value=field(col,row,max);if(!value)return 0;
 int ok=edit8noLast(action,value,max<16?1:16,0,max);if(ok)projectModified=1;return ok;}
int input(int down,int keys,int){if(screenInstrumentSimpleChip.cursorRow!=3){buttonDown=0;return 0;}auto action=popupEditInput(down,keys,&buttonDown);if(action==PopupEditInput::cycle){int n=simpleChipPresetCount(current()->type);select((current()->chip.simpleChip.preset+(keys==(keyEdit|keyRight)?1:n-1))%n);return 1;}if(action==PopupEditInput::hold)return 1;if(action==PopupEditInput::open){open();return 1;}return 0;}
}
ScreenData screenInstrumentSimpleChip={.rows=8,.cursorRow=0,.cursorCol=0,.topRow=0,.selectMode=-1,.selectStartRow=0,.selectStartCol=0,.selectAnchorRow=0,.selectAnchorCol=0,.playbackLevel=ScreenPlaybackLevel::none,.getColumnCount=columns,.drawStatic=drawStatic,.drawCursor=drawCursor,.drawSelection=nullptr,.drawRowHeader=nullptr,.drawColHeader=nullptr,.drawField=drawField,.onEdit=edit,.onInput=input,.onRawInput=nullptr,.isCellValid=valid,.getLoopRange=nullptr};
