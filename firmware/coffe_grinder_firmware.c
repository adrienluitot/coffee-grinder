#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"

// DEFINES
// Pins
#define LGP_BTN_IPIN    11 // Launch Grind (Perco)
#define LGF_BTN_IPIN    12 // Launch Grind (Front)
#define MOT_DRV_1_OPIN  13 // Motor Driver Input 1
#define MOT_DRV_2_OPIN  14 // Motor Driver Input 2
#define FB_LED_OPIN     15 // FeedBack LED (front)
#define DBG_LED_OPIN    25 // Debug LED (on RPi Pico)
#define DURATION_APIN   28 // Potentiometer Analog Read

// Consts
#define FALLING_EDGE    0x4
#define ADC_CONV_FACTOR 3.3f / (1 << 12) // might be useless
#define MIN_MS_DURATION 25000 // min 25s
#define MAX_MS_DURATION 100000 // max 100s
#define ADC_MIN_VAL     0x000
#define ADC_MAX_VAL     0xFFF

// FUNCTION HEADERS
void startup(void);
enum FSM_STATES fsm_calc_next_state(enum FSM_STATES current_state);
void fsm_state_effects(enum FSM_STATES state);
void lgp_irq_callback();
void lgf_irq_callback();

// GLOBAL VARIABLES
// Flags
static bool LGP_IRQ_FLAG = false;
static bool LGF_IRQ_FLAG = false;
bool DURATION_READ = false;

// FSM
enum FSM_STATES { FSM_IDLE, FSM_READ_ADC, FSM_GRIND }; // FSM_STOP, or back to idle ?
enum FSM_STATES fsm_curr_state = FSM_IDLE;
enum FSM_STATES fsm_next_state = FSM_IDLE;

// Timings
uint64_t grind_duration_us = 0; // -> max ~65s
uint64_t start_grind_us = 0;
uint64_t stop_grind_us = 0;

// Gpio pwm
uint DRV2_pwm_slice, DRV2_pwm_channel;


// MAIN FUNCTION
int main() {
    startup();

    while (1) {
        fsm_next_state = fsm_calc_next_state(fsm_curr_state);

        if(fsm_next_state != fsm_curr_state) {
            sleep_ms(1); // might be removed? FTM it doesn't work without the sleep
            fsm_curr_state = fsm_next_state;
            fsm_state_effects(fsm_curr_state);
        }

        sleep_ms(1); // might be removed, when there is only the ifs, they don't work without the sleep
    }

    return 0;
}

// function to calculate next state
enum FSM_STATES fsm_calc_next_state(enum FSM_STATES current_state) {
    enum FSM_STATES next_state;

    switch (current_state) {
        default:
        case FSM_IDLE:
            if(LGP_IRQ_FLAG || LGF_IRQ_FLAG) {
                LGF_IRQ_FLAG = false; LGP_IRQ_FLAG = false;

                next_state = FSM_READ_ADC;
                printf("moving to ADC\n");
            }
        break;
        case FSM_READ_ADC:
            if(DURATION_READ) {
                next_state = FSM_GRIND;
                printf("moving to GRIND\n");
            }
        break;
        case FSM_GRIND:
            if(LGP_IRQ_FLAG || LGF_IRQ_FLAG) {
                LGP_IRQ_FLAG = false; LGF_IRQ_FLAG = false;
                next_state = FSM_IDLE;
                printf("Cancel to IDLE\n");
            } else if((time_us_64() >= stop_grind_us)) {// if push button, stop the grind
                next_state = FSM_IDLE;
                printf("Back to IDLE\n");
            }
        break;
    }

    return next_state;
}


void fsm_state_effects(enum FSM_STATES state) {
    // Changing input/output Switch case
    switch (fsm_curr_state) {
        default:
        case FSM_IDLE:
            gpio_put(MOT_DRV_1_OPIN, 0);
            gpio_put(MOT_DRV_2_OPIN, 0); // Putting both DRV pin to 1 will slow down/brake the motor
            gpio_put(FB_LED_OPIN, 0);
        break;
        case FSM_READ_ADC:
            gpio_put(MOT_DRV_1_OPIN, 0);
            gpio_put(MOT_DRV_2_OPIN, 0); // Putting both DRV pin to 1 will slow down/brake the motor
            gpio_put(FB_LED_OPIN, 0);
            uint16_t adc_value = adc_read();
            // /!\ the order is important to have a big number before dividing (to avoid dividing and avoid a 0 because it's using uint and not floats)
            grind_duration_us = 1000 * ((adc_value * (MAX_MS_DURATION - MIN_MS_DURATION)) / ADC_MAX_VAL + MIN_MS_DURATION); 
            start_grind_us = time_us_64();
            stop_grind_us = start_grind_us + grind_duration_us;
            DURATION_READ = true;
        break;
        case FSM_GRIND:
            DURATION_READ = false; // reset flag
            // Info: Exchange DRV1/2 values if not turning in the right direction (Or just invert the cables)
            gpio_put(MOT_DRV_1_OPIN, 1);
            gpio_put(MOT_DRV_2_OPIN, 0); 
            gpio_put(FB_LED_OPIN, 1);           
        break;
    }

    return;
}


void startup(void) {
    stdio_init_all();
    adc_init();

    // init adc_init
    adc_gpio_init(DURATION_APIN);
    adc_select_input(2); // only one ADC used (ADC2 on input 28)

    // init GPIOs
    gpio_init(LGP_BTN_IPIN);
    gpio_init(LGF_BTN_IPIN);
    gpio_init(MOT_DRV_1_OPIN);
    gpio_init(MOT_DRV_2_OPIN);
    gpio_init(FB_LED_OPIN);
    gpio_init(DBG_LED_OPIN);

    // Set GPIOs direction
    gpio_set_dir(LGP_BTN_IPIN, GPIO_IN);
    gpio_set_dir(LGF_BTN_IPIN, GPIO_IN);
    gpio_set_dir(MOT_DRV_1_OPIN, GPIO_OUT);
    gpio_set_dir(MOT_DRV_2_OPIN, GPIO_OUT);
    gpio_set_dir(FB_LED_OPIN, GPIO_OUT);
    gpio_set_dir(DBG_LED_OPIN, GPIO_OUT);
    
    // Set GPIOs Pull Ups
    gpio_pull_up(LGP_BTN_IPIN);
    gpio_pull_up(LGF_BTN_IPIN);

    // Set Outputs GPIOs to low level
    gpio_put(MOT_DRV_1_OPIN, 0); // When both DRV input 0 -> Both outputs are Hi-Z
    gpio_put(MOT_DRV_2_OPIN, 0);  // uses PWM instead
    gpio_put(FB_LED_OPIN, 0);
    gpio_put(DBG_LED_OPIN, 0);

    // IRQ setup
    gpio_set_irq_enabled(LGP_BTN_IPIN, FALLING_EDGE, true);
    gpio_set_irq_enabled(LGF_BTN_IPIN, FALLING_EDGE, true);
    gpio_add_raw_irq_handler(LGP_BTN_IPIN, &lgp_irq_callback);
    gpio_add_raw_irq_handler(LGF_BTN_IPIN, &lgf_irq_callback);
    irq_set_enabled(IO_IRQ_BANK0, true);

    sleep_ms(100); // TODO: For uart console, can delete ? reduce ?
    puts("==> Coffe Grinder <==\n");
    puts("Starting...\n");

    for(uint8_t i = 0; i < 3; i++){
        gpio_put(DBG_LED_OPIN, 1); sleep_ms(100);
        gpio_put(DBG_LED_OPIN, 0); sleep_ms(50);
    }
}



// IRQ functions
void lgp_irq_callback() {
    if (gpio_get_irq_event_mask(LGP_BTN_IPIN) & GPIO_IRQ_EDGE_FALL) {
        LGP_IRQ_FLAG = true;
        gpio_acknowledge_irq(LGP_BTN_IPIN, GPIO_IRQ_EDGE_FALL);
    }
}

void lgf_irq_callback() {
    if (gpio_get_irq_event_mask(LGF_BTN_IPIN) & GPIO_IRQ_EDGE_FALL) {
        LGF_IRQ_FLAG = true;
        gpio_acknowledge_irq(LGF_BTN_IPIN, GPIO_IRQ_EDGE_FALL);
    }
}