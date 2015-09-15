
## POINTER is the local read pointer
## SIZE64 is the parameters containing the 64 bits size of the payload

## Load number of 64 bits elements to send.
dma_load dcnt0 ${SIZE64}
dma_write_bundle

## Test if we must send 64 bits elements or not.
dma_decr dcnt0
dma_bez dcnt0 skip_label${POINTER}
dma_write_bundle

## Send 64 bits elements
dma_label send_64_bytes_loop_label${POINTER}
dma_decr dcnt0
dma_read_w64 0 ${POINTER}
dma_decr ${POINTER}
dma_bnz dcnt0 send_64_bytes_loop_label${POINTER}
dma_write_bundle

dma_send_eot 0
dma_write_bundle

dma_label skip_label${POINTER}
