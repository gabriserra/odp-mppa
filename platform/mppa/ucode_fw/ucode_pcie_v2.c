/** \brief ucode_pcie_v2
 * PCIe microcode
 * arg0: number of 64 bits elements for packet 1
 * argx: number of 64 bits elements for packet x
 * arg7: number of 64 bits elements for packet 8
 * p0: address of packet 1
 * px: address of packet x
 * p7: address of packet 8
 */
unsigned long long ucode_pcie_v2[] __attribute__((aligned(128) )) = {
0x0000001000600000ULL,  /* C_0: dcnt0=R[0];*/
0x00000000004000ceULL,  /* C_1: if(dcnt0==0) goto C_51; dcnt0--;*/
0x0000000803c0000bULL,  /* C_2: READ8(ptr_0,chan_0); ptr_0+=8; if(dcnt0!=0) goto C_2; dcnt0--;*/
0x0000003000600000ULL,  /* C_3: dcnt0=R[1];*/
0x00000000004000caULL,  /* C_4: if(dcnt0==0) goto C_50; dcnt0--;*/
0x0000000843c00017ULL,  /* C_5: READ8(ptr_1,chan_0); ptr_1+=8; if(dcnt0!=0) goto C_5; dcnt0--;*/
0x0000005000600041ULL,  /* C_6: goto C_16; dcnt0=R[2];*/
0x0000000000000041ULL,  /* C_7: goto C_16;*/
0x0000000000001000ULL,  /* C_8: STOP(); ALIGN DROP ADDRESS */
0x0000000000001000ULL,  /* C_9: STOP(); ALIGN DROP ADDRESS */
0x0000000000001000ULL,  /* C_10: STOP(); ALIGN DROP ADDRESS */
0x0000000000001000ULL,  /* C_11: STOP(); ALIGN DROP ADDRESS */
0x0000000000001000ULL,  /* C_12: STOP(); ALIGN DROP ADDRESS */
0x0000000000001000ULL,  /* C_13: STOP(); ALIGN DROP ADDRESS */
0x0000000000001000ULL,  /* C_14: STOP(); ALIGN DROP ADDRESS */
0x0000000000001000ULL,  /* C_15: STOP(); ALIGN DROP ADDRESS */
0x00000000004000caULL,  /* C_16: if(dcnt0==0) goto C_50; dcnt0--;*/
0x0000000883c00047ULL,  /* C_17: READ8(ptr_2,chan_0); ptr_2+=8; if(dcnt0!=0) goto C_17; dcnt0--;*/
0x0000007000600000ULL,  /* C_18: dcnt0=R[3];*/
0x00000000004000caULL,  /* C_19: if(dcnt0==0) goto C_50; dcnt0--;*/
0x00000008c3c00053ULL,  /* C_20: READ8(ptr_3,chan_0); ptr_3+=8; if(dcnt0!=0) goto C_20; dcnt0--;*/
0x0000009000600000ULL,  /* C_21: dcnt0=R[4];*/
0x00000000004000caULL,  /* C_22: if(dcnt0==0) goto C_50; dcnt0--;*/
0x0000000000000081ULL,  /* C_23: goto C_32;*/
0x0000000000001000ULL,  /* C_24: STOP(); ALIGN DROP ADDRESS */
0x0000000000001000ULL,  /* C_25: STOP(); ALIGN DROP ADDRESS */
0x0000000000001000ULL,  /* C_26: STOP(); ALIGN DROP ADDRESS */
0x0000000000001000ULL,  /* C_27: STOP(); ALIGN DROP ADDRESS */
0x0000000000001000ULL,  /* C_28: STOP(); ALIGN DROP ADDRESS */
0x0000000000001000ULL,  /* C_29: STOP(); ALIGN DROP ADDRESS */
0x0000000000001000ULL,  /* C_30: STOP(); ALIGN DROP ADDRESS */
0x0000000000001000ULL,  /* C_31: STOP(); ALIGN DROP ADDRESS */
0x0000000903c00083ULL,  /* C_32: READ8(ptr_4,chan_0); ptr_4+=8; if(dcnt0!=0) goto C_32; dcnt0--;*/
0x000000b000600000ULL,  /* C_33: dcnt0=R[5];*/
0x00000000004000caULL,  /* C_34: if(dcnt0==0) goto C_50; dcnt0--;*/
0x0000000943c0008fULL,  /* C_35: READ8(ptr_5,chan_0); ptr_5+=8; if(dcnt0!=0) goto C_35; dcnt0--;*/
0x000000d000600000ULL,  /* C_36: dcnt0=R[6];*/
0x00000000004000caULL,  /* C_37: if(dcnt0==0) goto C_50; dcnt0--;*/
0x0000000983c0009bULL,  /* C_38: READ8(ptr_6,chan_0); ptr_6+=8; if(dcnt0!=0) goto C_38; dcnt0--;*/
0x000000f0006000c1ULL,  /* C_39: goto C_48; dcnt0=R[7];*/
0x0000000000001000ULL,  /* C_40: STOP(); ALIGN DROP ADDRESS */
0x0000000000001000ULL,  /* C_41: STOP(); ALIGN DROP ADDRESS */
0x0000000000001000ULL,  /* C_42: STOP(); ALIGN DROP ADDRESS */
0x0000000000001000ULL,  /* C_43: STOP(); ALIGN DROP ADDRESS */
0x0000000000001000ULL,  /* C_44: STOP(); ALIGN DROP ADDRESS */
0x0000000000001000ULL,  /* C_45: STOP(); ALIGN DROP ADDRESS */
0x0000000000001000ULL,  /* C_46: STOP(); ALIGN DROP ADDRESS */
0x0000000000001000ULL,  /* C_47: STOP(); ALIGN DROP ADDRESS */
0x00000000004000caULL,  /* C_48: if(dcnt0==0) goto C_50; dcnt0--;*/
0x00000009c3c000c7ULL,  /* C_49: READ8(ptr_7,chan_0); ptr_7+=8; if(dcnt0!=0) goto C_49; dcnt0--;*/
0x0000000000026001ULL,  /* C_50: SEND_IT(); SEND_EOT(chan_0); WAIT_TOKEN(); goto C_0;*/
0x0000000000022001ULL}; /* C_51: SEND_IT(); WAIT_TOKEN(); goto C_0;*/

