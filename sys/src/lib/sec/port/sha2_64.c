/*
 * sha2 64-bit
 */
#include <u.h>
#include <lib9.h>
#include <libsec.h>

static void encode32(uint8_t*, uint32_t*, uint32_t);
static DigestState* sha2_64(uint8_t *, uint32_t, uint8_t *, SHA2_256state *, int);

extern void _sha2block64(uint8_t*, uint32_t, uint32_t*);

/*
 *  for sha2_224 and sha2_256, len must be multiple of 64 for all but
 *  the last call.  There must be room in the input buffer to pad.
 *
 *  Note: sha2_224 calls sha2_256block as sha2_224, just uses different
 *  initial seed and produces a 224b hash result.  otherwise it's
 *  the same as sha2_256.
 */

SHA2_224state*
sha2_224(uint8_t *p, uint32_t len, uint8_t *digest, SHA2_224state *s)
{
	if(s == nil) {
		s = jehanne_mallocz(sizeof(*s), 1);
		if(s == nil)
			return nil;
		s->malloced = 1;
	}
	if(s->seeded == 0){
		/*
		 * seed the state with the first 32 bits of the fractional
		 * parts of the square roots of the first 8 primes 2..19).
		 */
		s->state[0] = 0xc1059ed8;
		s->state[1] = 0x367cd507;
		s->state[2] = 0x3070dd17;
		s->state[3] = 0xf70e5939;
		s->state[4] = 0xffc00b31;
		s->state[5] = 0x68581511;
		s->state[6] = 0x64f98fa7;
		s->state[7] = 0xbefa4fa4;
		s->seeded = 1;
	}
	return sha2_64(p, len, digest, s, SHA2_224dlen);
}

SHA2_256state*
sha2_256(uint8_t *p, uint32_t len, uint8_t *digest, SHA2_256state *s)
{
	if(s == nil) {
		s = jehanne_mallocz(sizeof(*s), 1);
		if(s == nil)
			return nil;
		s->malloced = 1;
	}
	if(s->seeded == 0){
		/*
		 * seed the state with the first 32 bits of the fractional
		 * parts of the square roots of the first 8 primes 2..19).
		 */
		s->state[0] = 0x6a09e667;
		s->state[1] = 0xbb67ae85;
		s->state[2] = 0x3c6ef372;
		s->state[3] = 0xa54ff53a;
		s->state[4] = 0x510e527f;
		s->state[5] = 0x9b05688c;
		s->state[6] = 0x1f83d9ab;
		s->state[7] = 0x5be0cd19;
		s->seeded = 1;
	}
	return sha2_64(p, len, digest, s, SHA2_256dlen);
}

/* common 64 byte block padding and count code for SHA2_224 and SHA2_256 */
static DigestState*
sha2_64(uint8_t *p, uint32_t len, uint8_t *digest, SHA2_256state *s, int dlen)
{
	int i;
	uint32_t x[16];
	uint8_t buf[128];
	uint8_t *e;

	/* fill out the partial 64 byte block from previous calls */
	if(s->blen){
		i = 64 - s->blen;
		if(len < i)
			i = len;
		jehanne_memmove(s->buf + s->blen, p, i);
		len -= i;
		s->blen += i;
		p += i;
		if(s->blen == 64){
			_sha2block64(s->buf, s->blen, s->state);
			s->len += s->blen;
			s->blen = 0;
		}
	}

	/* do 64 byte blocks */
	i = len & ~(64-1);
	if(i){
		_sha2block64(p, i, s->state);
		s->len += i;
		len -= i;
		p += i;
	}

	/* save the left overs if not last call */
	if(digest == 0){
		if(len){
			jehanne_memmove(s->buf, p, len);
			s->blen += len;
		}
		return s;
	}

	/*
	 *  this is the last time through, pad what's left with 0x80,
	 *  0's, and the input count to create a multiple of 64 bytes.
	 */
	if(s->blen){
		p = s->buf;
		len = s->blen;
	} else {
		jehanne_memmove(buf, p, len);
		p = buf;
	}
	s->len += len;
	e = p + len;
	if(len < 56)
		i = 56 - len;
	else
		i = 120 - len;
	jehanne_memset(e, 0, i);
	*e = 0x80;
	len += i;

	/* append the count */
	x[0] = s->len>>29;
	x[1] = s->len<<3;
	encode32(p+len, x, 8);

	/* digest the last part */
	_sha2block64(p, len+8, s->state);
	s->len += len+8;

	/* return result and free state */
	encode32(digest, s->state, dlen);
	if(s->malloced == 1)
		jehanne_free(s);
	return nil;
}

/*
 * Encodes input (uint32_t) into output (uint8_t).
 * Assumes len is a multiple of 4.
 */
static void
encode32(uint8_t *output, uint32_t *input, uint32_t len)
{
	uint32_t x;
	uint8_t *e;

	for(e = output + len; output < e;) {
		x = *input++;
		*output++ = x >> 24;
		*output++ = x >> 16;
		*output++ = x >> 8;
		*output++ = x;
	}
}

DigestState*
hmac_sha2_224(uint8_t *p, uint32_t len, uint8_t *key, uint32_t klen, uint8_t *digest,
	DigestState *s)
{
	return hmac_x(p, len, key, klen, digest, s, sha2_224, SHA2_224dlen);
}

DigestState*
hmac_sha2_256(uint8_t *p, uint32_t len, uint8_t *key, uint32_t klen, uint8_t *digest,
	DigestState *s)
{
	return hmac_x(p, len, key, klen, digest, s, sha2_256, SHA2_256dlen);
}
