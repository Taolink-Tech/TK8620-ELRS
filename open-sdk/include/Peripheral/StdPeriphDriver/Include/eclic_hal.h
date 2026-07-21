

#ifndef __ECLIC_HAL_H__
#define __ECLIC_HAL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>



/* constants definitions */
#define ECLIC_PRIGROUP_LEVEL0_PRIO3    (0) 
#define ECLIC_PRIGROUP_LEVEL1_PRIO2    (1) 
#define ECLIC_PRIGROUP_LEVEL2_PRIO1    (2) 
#define ECLIC_PRIGROUP_LEVEL3_PRIO0    (3) 


#define eclic_global_interrupt_enable  __enable_irq

#define eclic_global_interrupt_disable __disable_irq

/* function declarations */


void eclic_priority_group_set(uint32_t prigroup);


void eclic_irq_enable(uint32_t source, uint8_t level, uint8_t priority);


void eclic_irq_disable(uint32_t source);

#ifdef __cplusplus
}
#endif

#endif
