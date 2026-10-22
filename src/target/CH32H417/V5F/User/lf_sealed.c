/* Compiles LiteForth's src/sealed.c into the V5F project. Its quoted
   includes find the core headers next to it in src/. */
#define SEALED_POOL 1                   /* its 8 KB of buffers from the pool, only while mounted */
#include "../../../../sealed.c"
