#include "internal/lib_trng.h"

/** check if some FRO have raised alarms or were shutdown  **/ 
/*void mppa_check_alarm(void) {
  if (mppa_trng[0]->fro.alarm_mask.word != 0) {
    // alarm have been raised
    fro_alarm_warning_count++;
    unsigned int alarm_stop = mppa_trng[0]->fro.alarm_stop.word;
    if (alarm_stop != 0) {
      fro_alarm_stop_count++;
      unsigned int detune_mask = mppa_trng[0]->fro.detune.word;
      // FRO(s) have been stopped
      // trying to detune them to circumvent locking
      unsigned i;
      for (i = 0; i < K1B_FRO_NUM; ++i) {
        unsigned int fro_mask = 1 << i;
        if ((alarm_stop & fro_mask) == 0) {
          // FRO i has been stopped
          detune_mask |= fro_mask;
        }
      }

      mppa_trng[0]->fro.detune.word = detune_mask;
    } else {
      // clearing alarm mask
      mppa_trng[0]->fro.alarm_mask.word = 0;
    }
  }
}*/


void mppa_trng_copy_config(mppa_trng_config_t* pcfg) {
  *pcfg = mppa_trng[0]->config; 
};


void mppa_trng_copy_control(mppa_trng_control_t* pcontrol) {
  *pcontrol = mppa_trng[0]->control;
};


void mppa_trng_load_config(mppa_trng_config_t* pcfg) {
  mppa_trng[0]->config = *pcfg;
}


void mppa_trng_control(mppa_trng_control_t* pcontrol) {
  mppa_trng[0]->control = *pcontrol;
}


void mppa_trng_enable(void) {
  mppa_trng[0]->control._.enable_trng = 1;
}


void mppa_trng_disable(void) {
  mppa_trng[0]->control._.enable_trng = 0;
}


void mppa_trng_config_simple(unsigned noise_blocks, unsigned sample_div, unsigned sample_cycles) {
  mppa_trng_config_t trng_cfg;

  mppa_trng_copy_config(&trng_cfg);

  trng_cfg._.noise_blocks  = noise_blocks;
  trng_cfg._.sample_div    = sample_div;
  trng_cfg._.sample_cycles = sample_cycles;

  mppa_trng_load_config(&trng_cfg);
}


void mppa_trng_enable_interrupt_simple(void) {
  mppa_trng_control_t trng_ctrl;

  mppa_trng_copy_control(&trng_ctrl);

  trng_ctrl._.ready_mask         = 1;
  trng_ctrl._.shutdown_oflo_mask = 1;
  trng_ctrl._.stuck_out_mask     = 1;
  trng_ctrl._.noise_fail_mask    = 1;
  trng_ctrl._.run_fail_mask      = 1;
  trng_ctrl._.long_run_fail_mask = 1;
  trng_ctrl._.poker_fail_mask    = 1;
  trng_ctrl._.monobit_fail_mask  = 1;
  trng_ctrl._.stuck_nrbg_mask    = 1;
  trng_ctrl._.repcnt_fail_mask   = 1;
  trng_ctrl._.aprop_fail_mask    = 1;

  mppa_trng_control(&trng_ctrl);
}

__k1_uint32_t mppa_trng_read_status(void) {
  __k1_uint32_t trng_int_status;
  trng_int_status = mppa_trng[0]->sts_int.word;

  return trng_int_status;
}

// mask to test TRNG builtin-test failure in status register
#define MPPA_TRNG_STATUS_MASK (\
  MPPA_TRNG_STS_INT_READY__MASK|\
  MPPA_TRNG_STS_INT_SHUTDOWN_OFLO__MASK |\
  MPPA_TRNG_STS_INT_STUCK_OUT__MASK |\
  MPPA_TRNG_STS_INT_NOISE_FAIL__MASK |\
  MPPA_TRNG_STS_INT_RUN_FAIL__MASK |\
  MPPA_TRNG_STS_INT_LONG_RUN_FAIL__MASK |\
  MPPA_TRNG_STS_INT_POKER_FAIL__MASK |\
  MPPA_TRNG_STS_INT_MONOBIT_FAIL__MASK |\
  MPPA_TRNG_STS_INT_TEST_READY__MASK |\
  MPPA_TRNG_STS_INT_STUCK_NRBG__MASK |\
  MPPA_TRNG_STS_INT_REPCNT_FAIL__MASK |\
  MPPA_TRNG_STS_INT_APROP_FAIL__MASK |\
  MPPA_TRNG_STS_INT_TEST_STUCK_OUT__MASK \
)

int mppa_trng_valid_data(void) {
  __k1_uint32_t trng_int_status = mppa_trng_read_status();
  return trng_int_status & MPPA_TRNG_STS_INT_READY__MASK;
}

int mppa_trng_failure_event(void) {
  __k1_uint32_t trng_int_status = mppa_trng_read_status();
  trng_int_status &= MPPA_TRNG_STATUS_MASK; 
  return trng_int_status & (
    MPPA_TRNG_STS_INT_SHUTDOWN_OFLO__MASK |
    MPPA_TRNG_STS_INT_STUCK_OUT__MASK |
    MPPA_TRNG_STS_INT_NOISE_FAIL__MASK |
    MPPA_TRNG_STS_INT_RUN_FAIL__MASK |
    MPPA_TRNG_STS_INT_LONG_RUN_FAIL__MASK |
    MPPA_TRNG_STS_INT_POKER_FAIL__MASK |
    MPPA_TRNG_STS_INT_MONOBIT_FAIL__MASK |
    MPPA_TRNG_STS_INT_TEST_READY__MASK |
    MPPA_TRNG_STS_INT_STUCK_NRBG__MASK |
    MPPA_TRNG_STS_INT_REPCNT_FAIL__MASK |
    MPPA_TRNG_STS_INT_APROP_FAIL__MASK |
    MPPA_TRNG_STS_INT_TEST_STUCK_OUT__MASK 
  );
}

void mppa_trng_repare_failure(unsigned noise_blocks, unsigned sample_div, unsigned sample_cycles) {
  // dummy unplug ...
  mppa_trng_disable();

  mppa_trng_config_simple(noise_blocks, sample_div, sample_cycles);

  mppa_trng[0]->sts_int.word = 
    MPPA_TRNG_STS_ACK_READY_ACK__MASK |
    MPPA_TRNG_STS_ACK_SHUTDOWN_OFLO_ACK__MASK |
    MPPA_TRNG_STS_ACK_STUCK_OUT_ACK__MASK |
    MPPA_TRNG_STS_ACK_NOISE_FAIL_ACK__MASK |
    MPPA_TRNG_STS_ACK_RUN_FAIL_ACK__MASK |
    MPPA_TRNG_STS_ACK_LONG_RUN_FAIL_ACK__MASK |
    MPPA_TRNG_STS_ACK_POKER_FAIL_ACK__MASK |
    MPPA_TRNG_STS_ACK_MONOBIT_FAIL_ACK__MASK |
    MPPA_TRNG_STS_ACK_TEST_READY_ACK__MASK |
    MPPA_TRNG_STS_ACK_STUCK_NRBG_ACK__MASK |
    MPPA_TRNG_STS_ACK_REPCNT_FAIL_ACK__MASK |
    MPPA_TRNG_STS_ACK_APROP_FAIL_ACK__MASK |
    MPPA_TRNG_STS_ACK_TEST_STUCK_OUT__MASK;

  mppa_trng[0]->fro.enable.word = 0xff; // enabling the 8-FROS


  // and reboot
  mppa_trng_enable();
}

int mppa_trng_data_ready(void) {
  __k1_uint32_t trng_int_status = mppa_trng_read_status();
  return trng_int_status & MPPA_TRNG_STS_INT_READY__MASK;
}

int mppa_trng_read_data(mppa_trng_rnd_data_t *data) {
  unsigned i = 0;
  for (i = 0; i < 4; i++) {
    data->data[i] = mppa_trng[0]->data[i].word;
  } 

  return mppa_trng_valid_data();
}

void mppa_trng_ack_data(void) {
  mppa_trng[0]->sts_int.sts_ack.ready_ack = 1;
}
 

