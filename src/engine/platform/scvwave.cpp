/**
 * Furnace Tracker - multi-system chiptune tracker
 * Copyright (C) 2021-2025 tildearrow and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "scvwave.h"
#include "../engine.h"
#include "../../ta-log.h"
#include "furIcons.h"
#include <math.h>

//#define rWrite(a,v) pendingWrites[a]=v;
#define rWrite(a,v) if (!skipRegisterWrites) {packet[a]=v; writePacket=true; if (dumpWrites) {addWrite(a,v);} }

#define CHIP_DIVIDER 64

const char* regCheatSheetUPD1771c[]={
  NULL
};

const char** DivPlatformSCVWave::getRegisterSheet() {
  return regCheatSheetUPD1771c;
}

void DivPlatformSCVWave::acquire(short** buf, size_t len) {
  oscBuf[0]->begin(len);

  for (size_t h=0; h<len; h++) {
    while (!writes.empty()) {
      QueuedWrite w=writes.front();
      scv.write(w.val);
      regPool[w.addr&0xf]=w.val;
      writes.pop();
    }

    scv.sound_stream_update(&buf[0][h],1);
    if (isMuted[0]) buf[0][h]=0;
    oscBuf[0]->putSample(h,buf[0][h]);
  }

  oscBuf[0]->end(len);
}

void DivPlatformSCVWave::tick(bool sysTick) {
  for (int i=0; i<1; i++) {
    chan[i].std.next();
    if (chan[i].std.vol.had) {
      chan[i].outVol=VOL_SCALE_LINEAR(chan[i].vol&31,MIN(31,chan[i].std.vol.val),31);
    }
    if (NEW_ARP_STRAT) {
      chan[i].handleArp();
    } else if (chan[i].std.arp.had) {
      if (!chan[i].inPorta) {
        int f=parent->calcArp(chan[i].note,chan[i].std.arp.val);
        chan[i].baseFreq=NOTE_PERIODIC(f);
      }
      chan[i].freqChanged=true;
    }
    if (chan[i].std.wave.had) {
      chan[i].wave=chan[i].std.wave.val&7;
    }
    if (chan[i].std.duty.had) {
      chan[i].duty=chan[i].std.duty.val&1;
    }
    if (chan[i].std.ex1.had) {
      chan[i].pos=chan[i].std.ex1.val&31;
    }
    if (chan[i].std.pitch.had) {
      if (chan[i].std.pitch.mode) {
        chan[i].pitch2+=chan[i].std.pitch.val;
        CLAMP_VAR(chan[i].pitch2,-32768,32767);
      } else {
        chan[i].pitch2=chan[i].std.pitch.val;
      }
      chan[i].freqChanged=true;
    }
    if (chan[i].freqChanged || chan[i].keyOn || chan[i].keyOff) {
      //DivInstrument* ins=parent->getIns(chan[i].ins,DIV_INS_PCE);
      if (i==0) {
        chan[i].freq=parent->calcFreq(chan[i].baseFreq,chan[i].pitch,chan[i].fixedArp?chan[i].baseNoteOverride:chan[i].arpOff,chan[i].fixedArp,true,0,chan[i].pitch2,chipClock,CHIP_DIVIDER);
      }
      if (chan[i].keyOn) kon[i]=1;
      if (chan[i].keyOff) kon[i]=0;
      if (chan[i].keyOn) chan[i].keyOn=false;
      if (chan[i].keyOff) chan[i].keyOff=false;
      chan[i].freqChanged=false;
    }

    if (kon[i]) {
      if (i==0) {
        if (chan[i].duty == 0) {
          rWrite(0,2);
          rWrite(1,(chan[i].wave<<5)|chan[i].pos);
          // TODO: improve
          float p = ((float)chan[i].freq)/((float)(31-chan[i].pos))*31.0;
          rWrite(2,MIN(MAX((int)p,0),255));
          rWrite(3,chan[i].outVol);
        } else if (chan[i].duty == 1) {
          rWrite(0,1);
          rWrite(1,(chan[i].wave<<5));
          rWrite(2,MIN(MAX(chan[i].freq>>7,0),255));
          rWrite(3,chan[i].outVol);
        } else {
          rWrite(0,0);
        }
      }
    } else {
      if (i == 0) {
        rWrite(0,0);
      }
    }

  }

  // if need be, write packet
  if (writePacket) {
    writePacket=false;
    int len=1;
    if (packet[0]==2) {
      len=4;
    } else if (packet[0]==1) {
      len=10;
    }

    for (int i=0; i<len; i++) {
      writes.push(QueuedWrite(0,packet[i]));
    }
  }
}

int DivPlatformSCVWave::dispatch(DivCommand c) {
  switch (c.cmd) {
    case DIV_CMD_NOTE_ON: {
      DivInstrument* ins=parent->getIns(chan[c.chan].ins,DIV_INS_UPD1771C);
      if (c.value!=DIV_NOTE_NULL) {
        chan[c.chan].baseFreq=c.chan==3?c.value:NOTE_PERIODIC(c.value);
        chan[c.chan].freqChanged=true;
        chan[c.chan].note=c.value;
      }
      chan[c.chan].active=true;
      chan[c.chan].keyOn=true;
      //chwrite(c.chan,0x04,0x80|chan[c.chan].vol);
      chan[c.chan].macroInit(ins);
      if (!parent->song.brokenOutVol && !chan[c.chan].std.vol.will) {
        chan[c.chan].outVol=chan[c.chan].vol;
      }
      chan[c.chan].insChanged=false;
      break;
    }
    case DIV_CMD_NOTE_OFF:
      if (dumpWrites) addWrite(0xffff0002+(c.chan<<8),0);
      chan[c.chan].active=false;
      chan[c.chan].keyOff=true;
      chan[c.chan].macroInit(NULL);
      break;
    case DIV_CMD_NOTE_OFF_ENV:
    case DIV_CMD_ENV_RELEASE:
      chan[c.chan].std.release();
      break;
    case DIV_CMD_INSTRUMENT:
      if (chan[c.chan].ins!=c.value || c.value2==1) {
        chan[c.chan].ins=c.value;
        chan[c.chan].insChanged=true;
      }
      break;
    case DIV_CMD_VOLUME:
      if (chan[c.chan].vol!=c.value) {
        chan[c.chan].vol=c.value;
        if (!chan[c.chan].std.vol.has) {
          chan[c.chan].outVol=c.value;
          if (chan[c.chan].active) {
            //chwrite(c.chan,0x04,0x80|chan[c.chan].outVol);
          }
        }
      }
      break;
    case DIV_CMD_GET_VOLUME:
      if (chan[c.chan].std.vol.has) {
        return chan[c.chan].vol;
      }
      return chan[c.chan].outVol;
      break;
    case DIV_CMD_PITCH:
      chan[c.chan].pitch=c.value;
      chan[c.chan].freqChanged=true;
      break;
    case DIV_CMD_NOTE_PORTA: {
      int destFreq=NOTE_PERIODIC(c.value2);
      bool return2=false;
      if (destFreq>chan[c.chan].baseFreq) {
        chan[c.chan].baseFreq+=c.value;
        if (chan[c.chan].baseFreq>=destFreq) {
          chan[c.chan].baseFreq=destFreq;
          return2=true;
        }
      } else {
        chan[c.chan].baseFreq-=c.value;
        if (chan[c.chan].baseFreq<=destFreq) {
          chan[c.chan].baseFreq=destFreq;
          return2=true;
        }
      }
      chan[c.chan].freqChanged=true;
      if (return2) {
        chan[c.chan].inPorta=false;
        return 2;
      }
      break;
    }
    case DIV_CMD_STD_NOISE_MODE:
      chan[c.chan].wave=c.value&7;
      chan[c.chan].duty=c.value>>4&1;
      break;
    case DIV_CMD_N163_WAVE_POSITION:
      chan[c.chan].pos=c.value;
      break;
    case DIV_CMD_LEGATO:
      chan[c.chan].baseFreq=NOTE_PERIODIC(c.value+((HACKY_LEGATO_MESS)?(chan[c.chan].std.arp.val):(0)));
      chan[c.chan].freqChanged=true;
      chan[c.chan].note=c.value;
      break;
    case DIV_CMD_PRE_PORTA:
      if (chan[c.chan].active && c.value2) {
        if (parent->song.resetMacroOnPorta) chan[c.chan].macroInit(parent->getIns(chan[c.chan].ins,DIV_INS_UPD1771C));
      }
      if (!chan[c.chan].inPorta && c.value && !parent->song.brokenPortaArp && chan[c.chan].std.arp.will && !NEW_ARP_STRAT) chan[c.chan].baseFreq=NOTE_PERIODIC(chan[c.chan].note);
      chan[c.chan].inPorta=c.value;
      break;
    case DIV_CMD_GET_VOLMAX:
      return 31;
      break;
    case DIV_CMD_MACRO_OFF:
      chan[c.chan].std.mask(c.value,true);
      break;
    case DIV_CMD_MACRO_ON:
      chan[c.chan].std.mask(c.value,false);
      break;
    case DIV_CMD_MACRO_RESTART:
      chan[c.chan].std.restart(c.value);
      break;
    default:
      break;
  }
  return 1;
}

void DivPlatformSCVWave::muteChannel(int ch, bool mute) {
  isMuted[ch]=mute;
}

void DivPlatformSCVWave::forceIns() {
  for (int i=0; i<1; i++) {
    chan[i].insChanged=true;
    chan[i].freqChanged=true;
    //chwrite(i,0x05,isMuted[i]?0:chan[i].pan);
  }
}

void* DivPlatformSCVWave::getChanState(int ch) {
  return &chan[ch];
}

DivMacroInt* DivPlatformSCVWave::getChanMacroInt(int ch) {
  return &chan[ch].std;
}

DivDispatchOscBuffer* DivPlatformSCVWave::getOscBuffer(int ch) {
  return oscBuf[ch];
}

unsigned char* DivPlatformSCVWave::getRegisterPool() {
  return regPool;
}

int DivPlatformSCVWave::getRegisterPoolSize() {
  return 16;
}

void DivPlatformSCVWave::reset() {
  writes.clear();
  memset(regPool,0,16);
  for (int i=0; i<1; i++) {
    chan[i]=DivPlatformSCVWave::Channel();
    chan[i].std.setEngine(parent);
  }
  if (dumpWrites) {
    addWrite(0xffffffff,0);
  }
  scv.device_reset();
  memset(tempL,0,32*sizeof(int));
  memset(tempR,0,32*sizeof(int));
  memset(kon,0,1*sizeof(unsigned char));
  memset(initWrite,1,1*sizeof(unsigned char));
  memset(packet,0,16);
  writePacket=false;
}

int DivPlatformSCVWave::getOutputCount() {
  return 1;
}

bool DivPlatformSCVWave::keyOffAffectsArp(int ch) {
  return true;
}

void DivPlatformSCVWave::notifyInsDeletion(void* ins) {
  for (int i=0; i<1; i++) {
    chan[i].std.notifyInsDeletion((DivInstrument*)ins);
  }
}

void DivPlatformSCVWave::setFlags(const DivConfig& flags) {
  chipClock=6000000;
  CHECK_CUSTOM_CLOCK;
  rate=chipClock/4;
  for (int i=0; i<1; i++) {
    oscBuf[i]->setRate(rate);
  }
}

void DivPlatformSCVWave::poke(unsigned int addr, unsigned short val) {
  rWrite(addr,val);
}

void DivPlatformSCVWave::poke(std::vector<DivRegWrite>& wlist) {
  for (DivRegWrite& i: wlist) rWrite(i.addr,i.val);
}

int DivPlatformSCVWave::init(DivEngine* p, int channels, int sugRate, const DivConfig& flags) {
  parent=p;
  dumpWrites=false;
  skipRegisterWrites=false;
  for (int i=0; i<1; i++) {
    isMuted[i]=false;
    oscBuf[i]=new DivDispatchOscBuffer;
  }
  setFlags(flags);
  reset();
  return 1;
}

void DivPlatformSCVWave::quit() {
  for (int i=0; i<1; i++) {
    delete oscBuf[i];
  }
}

DivPlatformSCVWave::~DivPlatformSCVWave() {
}
