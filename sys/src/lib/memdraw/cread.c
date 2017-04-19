/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>

Memimage*
creadmemimage(int fd)
{
	char hdr[5*12+1];
	Rectangle r;
	int m, nb, miny, maxy, new, ldepth, ncblock;
	uint8_t *buf;
	Memimage *i;
	uint32_t chan;

	if(jehanne_readn(fd, hdr, 5*12) != 5*12){
		jehanne_werrstr("readmemimage: short header (2)");
		return nil;
	}

	/*
	 * distinguish new channel descriptor from old ldepth.
	 * channel descriptors have letters as well as numbers,
	 * while ldepths are a single digit formatted as %-11d.
	 */
	new = 0;
	for(m=0; m<10; m++){
		if(hdr[m] != ' '){
			new = 1;
			break;
		}
	}
	if(hdr[11] != ' '){
		jehanne_werrstr("creadimage: bad format");
		return nil;
	}
	if(new){
		hdr[11] = '\0';
		if((chan = strtochan(hdr)) == 0){
			jehanne_werrstr("creadimage: bad channel string %s", hdr);
			return nil;
		}
	}else{
		ldepth = ((int)hdr[10])-'0';
		if(ldepth<0 || ldepth>3){
			jehanne_werrstr("creadimage: bad ldepth %d", ldepth);
			return nil;
		}
		chan = drawld2chan[ldepth];
	}
	r.min.x=jehanne_atoi(hdr+1*12);
	r.min.y=jehanne_atoi(hdr+2*12);
	r.max.x=jehanne_atoi(hdr+3*12);
	r.max.y=jehanne_atoi(hdr+4*12);
	if(r.min.x>r.max.x || r.min.y>r.max.y){
		jehanne_werrstr("creadimage: bad rectangle");
		return nil;
	}

	i = allocmemimage(r, chan);
	if(i == nil)
		return nil;
	ncblock = _compblocksize(r, i->depth);
	buf = jehanne_malloc(ncblock);
	if(buf == nil)
		goto Errout;
	miny = r.min.y;
	while(miny != r.max.y){
		if(jehanne_readn(fd, hdr, 2*12) != 2*12){
		Shortread:
			jehanne_werrstr("readmemimage: short read");
		Errout:
			freememimage(i);
			jehanne_free(buf);
			return nil;
		}
		maxy = jehanne_atoi(hdr+0*12);
		nb = jehanne_atoi(hdr+1*12);
		if(maxy<=miny || r.max.y<maxy){
			jehanne_werrstr("readimage: bad maxy %d", maxy);
			goto Errout;
		}
		if(nb<=0 || ncblock<nb){
			jehanne_werrstr("readimage: bad count %d", nb);
			goto Errout;
		}
		if(jehanne_readn(fd, buf, nb)!=nb)
			goto Shortread;
		if(!new)	/* old image: flip the data bits */
			_twiddlecompressed(buf, nb);
		cloadmemimage(i, Rect(r.min.x, miny, r.max.x, maxy), buf, nb);
		miny = maxy;
	}
	jehanne_free(buf);
	return i;
}
