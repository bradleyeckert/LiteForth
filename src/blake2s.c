/* 4/27/25 BNE
   BLAKE2 reference source code package - reference C implementations

   Copyright 2012, Samuel Neves <sneves@dei.uc.pt>.  You may use this under the
   terms of the CC0, the OpenSSL Licence, or the Apache Public License 2.0, at
   your option.  The terms of these licenses can be found at:

   - CC0 1.0 Universal : http://creativecommons.org/publicdomain/zero/1.0
   - OpenSSL license   : https://www.openssl.org/source/license.html
   - Apache 2.0        : http://www.apache.org/licenses/LICENSE-2.0

   More information about the BLAKE2 hash function can be found at
   https://blake2.net.
*/

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "blake2s.h"

static const uint32_t blake2s_IV[8] =
{
  0x6A09E667UL, 0xBB67AE85UL, 0x3C6EF372UL, 0xA54FF53AUL,
  0x510E527FUL, 0x9B05688CUL, 0x1F83D9ABUL, 0x5BE0CD19UL
};

#ifndef BLAKE2S_UNROLL                  /* the unrolled rounds have it as constants */
static const uint8_t blake2s_sigma[10][16] =
{
  {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 } ,
  { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 } ,
  { 11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4 } ,
  {  7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8 } ,
  {  9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13 } ,
  {  2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9 } ,
  { 12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11 } ,
  { 13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10 } ,
  {  6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5 } ,
  { 10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13 , 0 } ,
};
#endif

static void blake2s_set_lastnode( blake2s_state *S )
{
  S->f[1] = (uint32_t)-1;
}

/* Some helper functions, not necessarily useful */
static int blake2s_is_lastblock( const blake2s_state *S )
{
  return S->f[0] != 0;
}

static void blake2s_set_lastblock( blake2s_state *S )
{
  if( S->last_node ) blake2s_set_lastnode( S );

  S->f[0] = (uint32_t)-1;
}

static void blake2s_increment_counter( blake2s_state *S, const uint32_t inc )
{
  S->t[0] += inc;
  S->t[1] += ( S->t[0] < inc );
}

static void blake2s_init0( blake2s_state *S )
{
  int i;
  memset( S, 0, sizeof( blake2s_state ) );

  for( i = 0; i < 8; ++i ) S->h[i] = blake2s_IV[i];
}

/* init2 xors IV with input parameter block */
static int blake2s_init_param( blake2s_state *S, const blake2s_param *P )
{
  const unsigned char *p = ( const unsigned char * )( P );
  int i;

  blake2s_init0( S );

  /* IV XOR ParamBlock */
  for( i = 0; i < 8; ++i )
    S->h[i] ^= load32( &p[i * 4] );

  S->outlen = P->digest_length;
  return 0;
}


/* The compression function, with the state in locals so a compiler can
   keep it in registers: the reference code's G, unrolled for each round,
   reading the message words through that round's sigma row. */
#define G(a, b, c, d, x, y)                     \
  do {                                          \
    a += b + (x);  d = rotr32(d ^ a, 16);       \
    c += d;        b = rotr32(b ^ c, 12);       \
    a += b + (y);  d = rotr32(d ^ a, 8);        \
    c += d;        b = rotr32(b ^ c, 7);        \
  } while (0)

static void blake2s_compress( blake2s_state *S, const uint8_t in[BLAKE2S_BLOCKBYTES] )
{
  uint32_t m[16];
  for( int i = 0; i < 16; ++i ) {
    m[i] = load32( in + i * sizeof( m[i] ) );
  }

  uint32_t v0 = S->h[0], v1 = S->h[1], v2 = S->h[2], v3 = S->h[3];
  uint32_t v4 = S->h[4], v5 = S->h[5], v6 = S->h[6], v7 = S->h[7];
  uint32_t v8 = blake2s_IV[0], v9 = blake2s_IV[1], v10 = blake2s_IV[2], v11 = blake2s_IV[3];
  uint32_t v12 = S->t[0] ^ blake2s_IV[4], v13 = S->t[1] ^ blake2s_IV[5];
  uint32_t v14 = S->f[0] ^ blake2s_IV[6], v15 = S->f[1] ^ blake2s_IV[7];

#ifndef BLAKE2S_UNROLL
  /* The rounds as a loop, the message words through sigma: 2.2 KB of RV32
     code at -Os, 2.6 times faster than the reference code */
  for( int r = 0; r < 10; ++r ) {
    const uint8_t *s = blake2s_sigma[r];
    G(v0, v4, v8,  v12, m[s[ 0]], m[s[ 1]]);
    G(v1, v5, v9,  v13, m[s[ 2]], m[s[ 3]]);
    G(v2, v6, v10, v14, m[s[ 4]], m[s[ 5]]);
    G(v3, v7, v11, v15, m[s[ 6]], m[s[ 7]]);
    G(v0, v5, v10, v15, m[s[ 8]], m[s[ 9]]);
    G(v1, v6, v11, v12, m[s[10]], m[s[11]]);
    G(v2, v7, v8,  v13, m[s[12]], m[s[13]]);
    G(v3, v4, v9,  v14, m[s[14]], m[s[15]]);
  }
#else
  /* BLAKE2S_UNROLL: every round written out, about 25% faster again but
     three times the code (6.7 KB of RV32 at -Os) */
  /* ROUND(r) with the round's sigma row as constants, so each message
     word is a fixed load (blake2s_sigma lists the same rows) */
#define ROUND(s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, s12, s13, s14, s15) \
  do {                                                \
    G(v0, v4, v8,  v12, m[s0],  m[s1]);               \
    G(v1, v5, v9,  v13, m[s2],  m[s3]);               \
    G(v2, v6, v10, v14, m[s4],  m[s5]);               \
    G(v3, v7, v11, v15, m[s6],  m[s7]);               \
    G(v0, v5, v10, v15, m[s8],  m[s9]);               \
    G(v1, v6, v11, v12, m[s10], m[s11]);              \
    G(v2, v7, v8,  v13, m[s12], m[s13]);              \
    G(v3, v4, v9,  v14, m[s14], m[s15]);              \
  } while (0)
  ROUND( 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15);
  ROUND(14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3);
  ROUND(11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4);
  ROUND( 7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8);
  ROUND( 9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13);
  ROUND( 2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9);
  ROUND(12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11);
  ROUND(13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10);
  ROUND( 6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5);
  ROUND(10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13,  0);
#undef ROUND
#endif

  S->h[0] ^= v0 ^ v8;   S->h[1] ^= v1 ^ v9;
  S->h[2] ^= v2 ^ v10;  S->h[3] ^= v3 ^ v11;
  S->h[4] ^= v4 ^ v12;  S->h[5] ^= v5 ^ v13;
  S->h[6] ^= v6 ^ v14;  S->h[7] ^= v7 ^ v15;
}
#undef G

void b2s_hmac_putc(blake2s_state *S, uint8_t c) {
  if (S->buflen == BLAKE2S_BLOCKBYTES) {
    blake2s_increment_counter(S, BLAKE2S_BLOCKBYTES);
    blake2s_compress( S, S->buf );
    S->buflen = 0;
  }
  S->buf[S->buflen++] = c;
}
void b2s_hmac_putc_g(size_t *S, uint8_t c) {
  b2s_hmac_putc((void *)S, c);
}

int b2s_hmac_final( blake2s_state *S, uint8_t *out )
{
  uint8_t buffer[BLAKE2S_OUTBYTES] = {0};

  if( blake2s_is_lastblock( S ) ) return -1;

  blake2s_increment_counter( S, ( uint32_t )S->buflen );
  blake2s_set_lastblock( S );
  memset( S->buf + S->buflen, 0, BLAKE2S_BLOCKBYTES - S->buflen ); /* Padding */
  blake2s_compress( S, S->buf );

  for( int i = 0; i < 8; ++i ) /* Output full hash to temp buffer */
    store32( buffer + sizeof( S->h[i] ) * i, S->h[i] );

  memcpy( out, buffer, S->outlen );
  secure_zero_memory(buffer, sizeof(buffer));
  return 0;
}
int b2s_hmac_final_g(size_t *S, uint8_t *out) {
  return b2s_hmac_final((void *)S, out);
}


int b2s_hmac_puts( blake2s_state *S, const uint8_t *in, int inlen )
{
  /* As b2s_hmac_putc byte by byte, but whole blocks are compressed straight
     from in. A full buffer is compressed only when more data comes, since
     the last block must wait for b2s_hmac_final (it gets the final flag). */
  if( inlen <= 0 ) return 0;
  size_t left = BLAKE2S_BLOCKBYTES - (size_t)S->buflen;
  if( (size_t)inlen > left ) {
    memcpy( S->buf + S->buflen, in, left );         /* fill the buffer */
    in += left;  inlen -= (int)left;
    blake2s_increment_counter( S, BLAKE2S_BLOCKBYTES );
    blake2s_compress( S, S->buf );
    S->buflen = 0;
    while( inlen > BLAKE2S_BLOCKBYTES ) {           /* more follows each one */
      blake2s_increment_counter( S, BLAKE2S_BLOCKBYTES );
      blake2s_compress( S, in );
      in += BLAKE2S_BLOCKBYTES;  inlen -= BLAKE2S_BLOCKBYTES;
    }
  }
  memcpy( S->buf + S->buflen, in, (size_t)inlen );  /* 1 to 64 bytes stay */
  S->buflen += inlen;
  return 0;
}

int b2s_init(blake2s_state *S, int hsize, const uint8_t *key, int keylen,
             const uint8_t *salt, const uint8_t *personal)
{
  blake2s_param P[1];

  if ( ( hsize < 1 ) || ( hsize > BLAKE2S_OUTBYTES ) ) return 0;
  if ( ( keylen < 0 ) || ( keylen > BLAKE2S_KEYBYTES ) ) return 0;
  if ( ( keylen > 0 ) && ( key == NULL ) ) return 0;

  P->digest_length = (uint8_t)hsize;
  P->key_length    = (uint8_t)keylen;
  P->fanout        = 1;
  P->depth         = 1;
  store32( &P->leaf_length, 0 );
  store32( &P->node_offset, 0 );
  store16( &P->xof_length, 0 );
  P->node_depth    = 0;
  P->inner_length  = 0;
  if ( salt ) memcpy( P->salt, salt, sizeof( P->salt ) );
  else        memset( P->salt, 0, sizeof( P->salt ) );
  if ( personal ) memcpy( P->personal, personal, sizeof( P->personal ) );
  else            memset( P->personal, 0, sizeof( P->personal ) );

  if( blake2s_init_param( S, P ) < 0 ) return 0;

  if ( keylen > 0 ) {         /* a keyed hash starts with the key, padded to a block */
    uint8_t block[BLAKE2S_BLOCKBYTES];
    memset( block, 0, BLAKE2S_BLOCKBYTES );
    memcpy( block, key, keylen );
    b2s_hmac_puts( S, block, BLAKE2S_BLOCKBYTES );
    secure_zero_memory( block, BLAKE2S_BLOCKBYTES ); /* Burn the key from stack */
  }
  return S->outlen;
}
int b2s_init_g(size_t *S, int hsize, const uint8_t *key, int keylen,
               const uint8_t *salt, const uint8_t *personal) {
  return b2s_init((void *)S, hsize, key, keylen, salt, personal);
}

int b2s_hmac_init(blake2s_state *S, const uint8_t *key, int hsize, uint64_t ctr)
{
  if ( !b2s_init( S, hsize, key, BLAKE2S_KEYBYTES, NULL, NULL ) ) return 0;
  /* The key block is only buffered so far (it is compressed when more data
     or b2s_hmac_final arrives), so t[] is still 0 here: the same as XORing
     ctr in before the key, as earlier versions did. */
  S->t[0] ^= (uint32_t) ctr;
  S->t[1] ^= ctr >> 32;
  return S->outlen;
}
int b2s_hmac_init_g(size_t *S, const uint8_t *key, int hsize, uint64_t ctr) {
  return b2s_hmac_init((void *)S, key, hsize, ctr);
}
