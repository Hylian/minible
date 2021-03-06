/**
 * \file    clock_manager.c
 * \author  MBorregoTrujillo
 * \date    13-March-2018
 * \brief   Selects and enable the clock source of the peripherals
 */
#include "clock_manager.h"

typedef struct clock_cfg {
	uint32_t clockid  : 6;    /* Peripheral CLOCK ID */
	uint32_t clockgen : 4;    /* CLOCK GENERATOR source */
} T_clock_cfg;

/* SERCOM1 clock configuration */
const T_clock_cfg clock_sercom1 = {
	.clockid = SERCOM1_GCLK_ID_CORE,
	.clockgen = GCLK_GENERATOR_0
};

/**
 * \fn      clock_manager_config
 * \brief   Configures the clock of the peripheral defined
 *          in clock configuration parameter
 * \param   clock   Pointer to clock configuration
 */
static void clock_manager_config(const T_clock_cfg* clock){
	cpu_irq_enter_critical();

	/* Do an 8-bit write of the i value to GENCTRL.ID */
	*((uint8_t*)&GCLK->CLKCTRL.reg) = clock->clockid;
	/* Select clock generator source */
	GCLK->CLKCTRL.bit.GEN = clock->clockgen;
	/* Enable clock for the selected peripheral */
	GCLK->CLKCTRL.bit.CLKEN = true;

	cpu_irq_leave_critical();
}


void clock_manager_init(void){
	clock_manager_config(&clock_sercom1);
}

