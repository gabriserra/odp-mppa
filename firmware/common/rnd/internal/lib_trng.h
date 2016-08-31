#include <HAL/hal/cluster/trng.h>
#include <HAL/hal/core/types.h>

typedef struct mppa_trng_rnd_data {
	__k1_uint32_t data[4];
} mppa_trng_rnd_data_t;

/** check if some FROs have raised alarms or were shutdown  
 *  if no shutdown occures, clear alarm mask and goes on
 *  if shutdown occured, detune concerned FRO(s) and restart
 */ 
void mppa_trng_check_alarm(void);

/** Copy the trng configuration into the structure *pcfg */
void mppa_trng_copy_config(mppa_trng_config_t* pcfg);

/** Copy the content of the trng control register into the structure */
void mppa_trng_copy_control(mppa_trng_control_t* pcontrol);

/** load the configuration described by cfg into the TRNG IP */
void mppa_trng_load_config(mppa_trng_config_t* pcfg);

/** load the control described by cfg into the TRNG IP */
void mppa_trng_control(mppa_trng_control_t* pcontrol);

/** load a simple configuration into the TRNG */
void mppa_trng_config_simple(unsigned noise_blocks, unsigned sample_div, unsigned sample_cycles);

/** enable default level of internal interrupt in the TRNG */
void mppa_trng_enable_interrupt_simple(void);

/** Enable the TRNG (starting Random Number generation) */
void mppa_trng_enable(void);

/** Disable the TRNG (stopping Random Number generation) */
void mppa_trng_disable(void);

/** read the value of the status register from the TRNG */
__k1_uint32_t mppa_trng_read_status(void);

/** Test if a new chunk of random 128-bit is ready in the TRNG buffer */
int mppa_trng_data_ready(void);

/** Test the TRNG interrupt status, return 1 if no failure occured and data is ready, else 0 */
int mppa_trng_valid_data(void);

/** Copy the random data from the TRNG into data 
 *  @param data is a 4-word array where the random data are copied
 *  @return a valid status (1 means no internal TRNG failure, 0 means any failure)
 * */
int mppa_trng_read_data(mppa_trng_rnd_data_t *data);

/** Acknowledge the TRNG data */ 
void mppa_trng_ack_data(void);

/** test if a failure event occured */
int mppa_trng_failure_event(void); 

/** dummy repair of a failure event */
void mppa_trng_repare_failure(unsigned noise_blocks, unsigned sample_div, unsigned sample_cycles);
