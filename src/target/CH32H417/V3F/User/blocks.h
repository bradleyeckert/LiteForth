#ifndef BLOCKS_H
#define BLOCKS_H

/* The little of the block layer that src/card.c needs on the V3F, which
   only finds files on the card (stage1.c). The V5F has the real one. */
#define BLK_SIZE_BYTES      4096
#define BLK_SIGNATURE       "LITEFORTH"
#define BLK_SIGNATURE_LEN   9

#endif /* BLOCKS_H */
