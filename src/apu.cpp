#include "util.h"
#include "apu.h"
#include <cstdint>
#include <cmath>


void APU::setCPU(CPU* c_ptr) {
    cpu = c_ptr;
    FRAME_COUNTER = &cpu->memory[0x4017];
}

int16_t mix(APU* a_ptr) { //TODO: REWRITE THIS WHOLE FUNCTION TO TAKE EXISTING OUTPUTS FROM CHANNELS
    //pulse1/60.0+pulse2/60.0+
    a_ptr->audio_frame++;

    uint8_t p_out = a_ptr->pulse_out[0]+a_ptr->pulse_out[1];
    float final_vol = 0.00752*(p_out);
    //TODO: add triangle noise and DMC
    int16_t output = final_vol*32767;
    //printf("out: %f\n", (float)output/32767);
    return output;
}

void APU::clock_envs() { //clock_envs
    for (int i=0; i<3; i++) {
        int env_addr = 0x4000|(((1<<i)-1)<<2);
        uint8_t* start_flag = &env[i][0];
        uint8_t* divider = &env[i][1];
        uint8_t* decay = &env[i][2]; // ends up determining final volume (or use constant val if flag is set)
        if (*start_flag) {
            if (*divider) {
                *divider--;
            } else {
                *divider = cpu->memory[env_addr]&0xf; //reset to V value from corresponding envelope register
                //clock decay counter
                if (*decay) {
                    *decay--;
                } else if (cpu->memory[env_addr]&0x20) { // if loop flag set for envelope
                    *decay = 15;
                }
            }
        }
    }
}

void APU::clock_linear() {
    uint8_t* linear_counter = &tri[2];
    uint8_t* linear_counter_reload = &tri[3];
    if (*linear_counter_reload) {
        *linear_counter = cpu->memory[0x4008]&0x7F; // counter reload value
    } else if (*linear_counter) {
        *linear_counter--;
    }
    if (!(cpu->memory[0x4008]&0x80)) {
        *linear_counter_reload = 0;
    }
}

void APU::clock_length() {
    for (int l=0; l<4; l++) {
        if (cpu->memory[0x4015]&(1<<l)) { // check if channel is enabled using $4015
            bool halt_flag = cpu->memory[0x4000+4*l]&(0x20<<(2*(l==2))); // get corresponding length counter halt flag
            
            if (!(length_counter[l]==0 || halt_flag)) { // if the length counter is not 0 and the halt flag is not set, decrement
                length_counter[l]--;
            } else if (length_counter[l]==0) {
                //silence corresponding channel
            }
        } else {
            length_counter[l]=0;
        }
    }
}

uint16_t APU::get_pulse_period(bool ind) {
    return (cpu->memory[0x4002|(ind<<2)]&0xff)|((cpu->memory[0x4003|(ind<<2)]&0x7)<<8);
}

void APU::set_pulse_period(uint16_t val, bool ind) { // sets the pulse channel's period to the new value, stored in the appropriate cpu registers (only presumably used by the sweep units)
    cpu->memory[0x4002|(ind<<2)] = val&0xff;
    cpu->memory[0x4003|(ind<<2)] &= ~0x7;
    cpu->memory[0x4003|(ind<<2)] |= (val&0x700)>>8;
}

void APU::clock_sweep() { //clock sweep units
    for (int i=0; i<2; i++) {
        uint8_t* divider = &sweep_units[i][0];
        uint8_t* reload = &sweep_units[i][1];
        uint8_t* muted = &sweep_units[i][2];

        uint8_t sweep_setup = (uint8_t)cpu->memory[0x4001|(i<<2)];
        bool enabled = sweep_setup&0x80;
        uint16_t pulse_period = get_pulse_period(i);
        
        //calculate target period
        int16_t change_amount = pulse_period>>(sweep_setup&0x7); // shift current period by shift count in register
        if (sweep_setup&0x8) { //if negative flag
            change_amount = -(change_amount)-i; // the -i is because pulse 2 uses twos complement whereas pulse 1 uses ones, a very minor difference but id like to include it
        }
        int16_t target_period = pulse_period+change_amount;
        if (target_period<0) {
            target_period = 0;
        }

        *muted = (pulse_period<8) || (target_period>0x7ff); // set muted
        
        if (!(*divider) && enabled && !(*muted)) { // if divider reached zero, sweep is enabled and its not muted
            set_pulse_period(target_period,i); // set pulse period to target period
        } 
        if (!(*divider) || (*reload)) { // check if divider is zero, or reload flag is set
            *divider = (cpu->memory[0x4001|(i<<2)]&0x70)>>4; //reload divider
            *reload = 0; // clear reload
        } else {
            *divider--;
        }
    }
}

void APU::func_frame_counter() { //APU frame counter which clocks other update things
    FRAME_COUNTER = &cpu->memory[0x4017];
    bool step5 = ((*FRAME_COUNTER)&0x80);
    bool inhibit = ((*FRAME_COUNTER)&0x40);
    int sequence_clocks = cycles%(14916+3724*step5); //14916 is (approximately) the number of apu clocks will occur in 1/60 of a second.
    if (sequence_clocks == 3729) { //step 1 (first quarter frame)
        clock_envs(); // clock envelopes
        clock_linear(); //clock triangle linear counter

    } else if (sequence_clocks == 7458) {//step 2 (second quarter frame)
        clock_envs(); // clock envelopes
        clock_linear(); //clock triangle linear counter

        clock_length(); //clock length counters
        clock_sweep(); //clock sweep units
        

    } else if (sequence_clocks == 11187) { //step 3 (third quarter frame)
        clock_envs(); // clock envelopes
        clock_linear(); //clock triangle linear counter

    } else if (sequence_clocks == 0) {//step 4 (fourth quarter frame)
        clock_envs(); // clock envelopes
        clock_linear(); //clock triangle linear counter

        clock_length(); //clock length counters
        clock_sweep(); //clock sweep units
        //TODO: set frame interrupt if interrupt inhibit is clear and 4-step sequence
        if (!step5 && !inhibit) {
            frame_interrupt = true;
        }
    }
}

void APU::cycle() { // apu clock (every other cpu cycle)
    func_frame_counter();
    //everything else
    pulse(0);
    pulse(1);
    cycles++;

    uint8_t p_out = pulse_out[0];
    float final_vol = 0.00752*(p_out);
    int16_t output = 32767*final_vol;
    buffer_size = SDL_GetQueuedAudioSize(device);
    int target_buffer_size = 2048;
    if (buffer_size==0 && sample_adj<100) {
        sample_adj++;
    } else if (buffer_size>=2048 && sample_adj>-100) {
        sample_adj--;
    }
    if (audio_frame<cycles*2*(sample_rate+sample_adj)/cpu->CLOCK_SPEED) {
        SDL_QueueAudio(device,&output,2);
        audio_frame++;
        //printf("Pulse out 0: %i\n",pulse_out[0]);
    }
}

void APU::pulse(bool ind) {
    /*uint16_t period = get_pulse_period(ind);
    if (sweep_units[ind][2] || period<8) { //muted channel
        pulse_out[ind] = 0;
    } else {
        uint8_t pulse_reg = cpu->memory[0x4000|(ind<<2)];
        uint8_t duty = (pulse_reg&0xC0)>>6;
        uint8_t volume = pulse_reg&0xf ? pulse_reg&0x10 : env[ind][3];
        pulse_out[ind] = volume*pulse_waveforms[duty][pulse_timer[ind] % 8];

    }
    pulse_timer[ind]++;
    pulse_timer[ind]%=period+1;*/
    pulse_out[ind] = 30*((cycles*4*880/cpu->CLOCK_SPEED)%2);
}

uint8_t APU::length_lookup(uint8_t in) {
    // from: https://www.nesdev.org/wiki/APU_Length_Counter#Table_structure
    uint8_t low4 = in&0x10;
    if (in&1) {
        if (in!=1) {
            return in-1;
        } else {
            return 254;
        }
    } else if ((in&0xF)<=0x8) {
        return (10|(low4>>3))<<((in&0xF)>>1);
    } else if ((in&0xF)>0x8) {
        switch (in&0xF) {
            case 0xA:
                return low4 ? 72 : 60;
            case 0xC:
                return low4 ? 16 : 14;
            case 0xE:
                return low4 ? 32 : 26;
        }
    }
}