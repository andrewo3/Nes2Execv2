#include "mapper.h"
#include "cpu.h"
#include "ppu.h"


void MMC3::map_write(void** ptrs, int8_t* address, int8_t *value) {
    int8_t val = *value;
    CPU* cpu = (CPU*)ptrs[0];
    ROM* rom = cpu->rom;
    PPU* ppu = (PPU*)ptrs[1];
    long long location = address-(cpu->memory);
    if (location == 0x8000) {
        
        //if new $8000.D6 is different from last value, swap $8000 and $C000
        if ((val&0x40)!=(xbase&0x40)) {
            int8_t temp[0x2000];
            memcpy(temp,&cpu->memory[0x8000]+((xbase&0x40)<<8),0x2000); //copy R6 to temp
            memcpy(&cpu->memory[0x8000]+((xbase&0x40)<<8),&cpu->memory[0x8000]+((!(xbase&0x40))<<8),0x2000); //copy (-2) to R6
            memcpy(&cpu->memory[0x8000]+((!(xbase&0x40))<<8),temp,0x2000); //copy temp to (old) (-2) 
        }
        xbase = val;
        
    }
    if (0x8000<=location && location<=0x9fff && !(location&0x1)) { //bank select
        reg = val;
    } else if (0x8000<=location && location<=0x9fff && (location&0x1)) { //bank data
        uint8_t r = reg&0x7;
        int chrsize = (rom->get_chrsize())/0x2000;
        int prgsize = (rom->get_prgsize())/0x4000;
        if (r<6) {
            uint16_t start_loc = ((r>1)<<12)+(0x800>>(r>1))*(r-2*(r>1));
            int bank_size = (0x800>>(r>1));
            memcpy(ppu->memory+(start_loc^((xbase&0x80)<<5)),rom->get_chr_bank((val%chrsize)<<(r<2)),bank_size);
        } else {
            uint16_t start_loc = 0x8000+0x2000*(r==7)+0x4000*(r!=7 && (xbase&0x40));
            memcpy(&cpu->memory[0x8000]+start_loc,rom->get_prg_bank((val%prgsize)<<1),0x2000);
        }
    } else if (0xA000<=location && location<=0xBFFF && !(location&0x1) && rom->mirrormode!=FOURSCREEN) { //mirroring
        rom->mirrormode = (NT_MIRROR)!(val&0x1); //0 is vertical, 1 is horizontal - opposite of the enum defined in rom.h
    } else if (0xA000<=location && location<=0xBFFF && (location&0x1)) { //prg ram protect
        wp = val&0x40;
        prgram = val&0x80; //honestly dont know what to do with this flag
    } else if (0xC000<=location && location <=0xDFFF && !(location&0x1)) { // IRQ latch
        irq_reload = (uint8_t)val;
    } else if (0xC000<=location && location <=0xDFFF && (location&0x1)) { // IRQ reload
        irq_counter = -1; // on next clock this will immediately trigger reload (without triggering irq)
    } else if (0xE000<=location && location <=0xFFFF && !(location&0x1)) { // IRQ disable
        irq_enabled = false;
    } else if (0xE000<=location && location <=0xFFFF && (location&0x1)) { // IRQ enable
        irq_enabled = true;
    } else if ((0x2006==location) && (ppu->v&0x1000) && ppu_a12 == 0) { // PPU A12 rising edge via PPUADDR
        scanline_clock(cpu);
        printf("PPUADDR (v=%04x) Scanline Counter: %i on scanline %i - reload value: %i\n",ppu->v,irq_counter,ppu->scanline,irq_reload);
        scanline_counted = true;
    }

    //write protect
    if (wp && 0x6000<=location && location<=0x7FFF) {
        *value = *address; //set the value to the number already at the address, so when its written - nothing changes
    }

}

void MMC3::scanline_clock(CPU* cpu) {
    printf("SCANLINE CLOCK\n");
    irq_counter--;
    //printf("PPU ADDRESS ON CLOCK: %04x\n",ppu->v);
    if (irq_counter == 0 && irq_enabled) { 
        cpu->recv_irq = true;
    }
    if (irq_counter<=0) {
        irq_counter = irq_reload;
    }
}

void MMC3::clock(void** system) {
    CPU* cpu = (CPU*)system[0];
    ROM* rom = cpu->rom;
    PPU* ppu = (PPU*)system[1];
    bool rendering = ((*(ppu->PPUMASK))&0x18);
    if (ppu->scycle==260 && rendering && ppu->scanline <= 240) { //rising edge of a12
        scanline_counted == true;
        //scanline_clock(cpu);
        //printf("Scanline Counter: %i on scanline %i - reload value: %i\n",irq_counter,ppu->scanline,irq_reload);
    }
    if (ppu->scycle == 0) {
        scanline_counted = false;
    }
    ppu_a12 = (bool)(ppu->v&0x1000);
}

void CNROM::map_write(void** ptrs, int8_t* address, int8_t *value) {
    int8_t val = *value;
    CPU* cpu = (CPU*)ptrs[0];
    PPU* ppu = (PPU*)ptrs[1];
    long long location = address-(cpu->memory);
    if (0x8000<=location && location<=0xffff) {
        int chrsize = (ppu->rom->get_chrsize())/0x2000;
        ppu->chr_bank_num = ((uint8_t)val%chrsize)<<3;
        //printf("CHANGE! %i\n",ppu->chr_bank_num);
        ppu->loadRom(ppu->rom);
    }   
}

void UxROM::map_write(void** ptrs, int8_t* address, int8_t* value) {
    int8_t val = *value;
    CPU* cpu = (CPU*)ptrs[0];
    long long location = address-(cpu->memory);
    if (0x8000<=location && location<=0xffff) {
        cpu->prg_bank_num = (val&0xf)<<4;
        //printf("CHANGE! %i\n",ppu->chr_bank_num);
        cpu->loadRom(cpu->rom);
    }   
}