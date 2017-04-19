/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <lib9.h>
#include <draw.h>
#include <thread.h>
#include <cursor.h>
#include <mouse.h>
#include <keyboard.h>
#include <frame.h>
#include <9P2000.h>
#include <plumb.h>
#include <complete.h>
#include "dat.h"
#include "fns.h"

Image	*tagcols[NCOL];
Image	*textcols[NCOL];

enum{
	TABDIR = 3	/* width of tabs in directory windows */
};

void
textinit(Text *t, File *f, Rectangle r, Reffont *rf, Image *cols[NCOL])
{
	t->file = f;
	t->all = r;
	t->scrollr = r;
	t->scrollr.max.x = r.min.x+Scrollwid;
	t->lastsr = nullrect;
	r.min.x += Scrollwid+Scrollgap;
	t->eq0 = ~0;
	t->ncache = 0;
	t->reffont = rf;
	t->tabstop = maxtab;
	memmove(t->Frame.cols, cols, sizeof t->Frame.cols);
	textredraw(t, r, rf->f, screen, -1);
}

void
textredraw(Text *t, Rectangle r, Font *f, Image *b, int odx)
{
	int maxt;
	Rectangle rr;

	frinit(t, r, f, b, t->Frame.cols);
	rr = t->r;
	rr.min.x -= Scrollwid+Scrollgap;	/* back fill to scroll bar */
	draw(t->b, rr, t->cols[BACK], nil, ZP);
	/* use no wider than 3-space tabs in a directory */
	maxt = maxtab;
	if(t->what == Body){
		if(t->w->isdir)
			maxt = min(TABDIR, maxtab);
		else
			maxt = t->tabstop;
	}
	t->maxtab = maxt*stringwidth(f, "0");
	if(t->what==Body && t->w->isdir && odx!=Dx(t->all)){
		if(t->maxlines > 0){
			textreset(t);
			textcolumnate(t, t->w->dlp,  t->w->ndl);
			textshow(t, 0, 0, 1);
		}
	}else{
		textfill(t);
		textsetselect(t, t->q0, t->q1);
	}
}

int
textresize(Text *t, Rectangle r)
{
	int odx;

	if(Dy(r) > 0)
		r.max.y -= Dy(r)%t->font->height;
	else
		r.max.y = r.min.y;
	odx = Dx(t->all);
	t->all = r;
	t->scrollr = r;
	t->scrollr.max.x = r.min.x+Scrollwid;
	t->lastsr = nullrect;
	r.min.x += Scrollwid+Scrollgap;
	frclear(t, 0);
	textredraw(t, r, t->font, t->b, odx);
	return r.max.y;
}

void
textclose(Text *t)
{
	free(t->cache);
	frclear(t, 1);
	filedeltext(t->file, t);
	t->file = nil;
	rfclose(t->reffont);
	if(argtext == t)
		argtext = nil;
	if(typetext == t)
		typetext = nil;
	if(seltext == t)
		seltext = nil;
	if(mousetext == t)
		mousetext = nil;
	if(barttext == t)
		barttext = nil;
}

int
dircmp(const void *a, const void *b)
{
	Dirlist *da, *db;
	int i, n;

	da = *(Dirlist**)a;
	db = *(Dirlist**)b;
	n = min(da->nr, db->nr);
	i = memcmp(da->r, db->r, n*sizeof(Rune));
	if(i)
		return i;
	return da->nr - db->nr;
}

void
textcolumnate(Text *t, Dirlist **dlp, int ndl)
{
	int i, j, w, colw, mint, maxt, ncol, nrow;
	Dirlist *dl;
	uint32_t q1;

	if(t->file->ntext > 1)
		return;
	mint = stringwidth(t->font, "0");
	/* go for narrower tabs if set more than 3 wide */
	t->maxtab = min(maxtab, TABDIR)*mint;
	maxt = t->maxtab;
	colw = 0;
	for(i=0; i<ndl; i++){
		dl = dlp[i];
		w = dl->wid;
		if(maxt-w%maxt < mint || w%maxt==0)
			w += mint;
		if(w % maxt)
			w += maxt-(w%maxt);
		if(w > colw)
			colw = w;
	}
	if(colw == 0)
		ncol = 1;
	else
		ncol = max(1, Dx(t->r)/colw);
	nrow = (ndl+ncol-1)/ncol;

	q1 = 0;
	for(i=0; i<nrow; i++){
		for(j=i; j<ndl; j+=nrow){
			dl = dlp[j];
			fileinsert(t->file, q1, dl->r, dl->nr);
			q1 += dl->nr;
			if(j+nrow >= ndl)
				break;
			w = dl->wid;
			if(maxt-w%maxt < mint){
				fileinsert(t->file, q1, (Rune*)L"\t", 1);
				q1++;
				w += mint;
			}
			do{
				fileinsert(t->file, q1, (Rune*)L"\t", 1);
				q1++;
				w += maxt-(w%maxt);
			}while(w < colw);
		}
		fileinsert(t->file, q1, (Rune*)L"\n", 1);
		q1++;
	}
}

uint32_t
textload(Text *t, uint32_t q0, char *file, int setqid)
{
	Rune *rp;
	Dirlist *dl, **dlp;
	int fd, i, j, n, ndl, nulls;
	uint32_t q, q1;
	Dir *d, *dbuf;
	char *tmp;
	Text *u;

	if(t->ncache!=0 || t->file->nc || t->w==nil || t!=&t->w->body)
		error("text.load");
	if(t->w->isdir && t->file->nname==0){
		warning(nil, "empty directory name\n");
		return 0;
	}
	fd = open(file, OREAD);
	if(fd < 0){
		warning(nil, "can't open %s: %r\n", file);
		return 0;
	}
	d = dirfstat(fd);
	if(d == nil){
		warning(nil, "can't fstat %s: %r\n", file);
		goto Rescue;
	}
	nulls = FALSE;
	if(d->qid.type & QTDIR){
		/* this is checked in get() but it's possible the file changed underfoot */
		if(t->file->ntext > 1){
			warning(nil, "%s is a directory; can't read with multiple windows on it\n", file);
			goto Rescue;
		}
		t->w->isdir = TRUE;
		t->w->filemenu = FALSE;
		if(t->file->nname > 0 && t->file->name[t->file->nname-1] != '/'){
			rp = runemalloc(t->file->nname+1);
			runemove(rp, t->file->name, t->file->nname);
			rp[t->file->nname] = '/';
			winsetname(t->w, rp, t->file->nname+1);
			free(rp);
		}
		dlp = nil;
		ndl = 0;
		dbuf = nil;
		while((n=dirread(fd, &dbuf)) > 0){
			for(i=0; i<n; i++){
				dl = emalloc(sizeof(Dirlist));
				j = strlen(dbuf[i].name);
				tmp = emalloc(j+1+1);
				memmove(tmp, dbuf[i].name, j);
				if(dbuf[i].qid.type & QTDIR)
					tmp[j++] = '/';
				tmp[j] = '\0';
				dl->r = bytetorune(tmp, &dl->nr);
				dl->wid = stringwidth(t->font, tmp);
				free(tmp);
				ndl++;
				dlp = realloc(dlp, ndl*sizeof(Dirlist*));
				dlp[ndl-1] = dl;
			}
			free(dbuf);
		}
		qsort(dlp, ndl, sizeof(Dirlist*), dircmp);
		t->w->dlp = dlp;
		t->w->ndl = ndl;
		textcolumnate(t, dlp, ndl);
		q1 = t->file->nc;
	}else{
		t->w->isdir = FALSE;
		t->w->filemenu = TRUE;
		q1 = q0 + fileload(t->file, q0, fd, &nulls);
	}
	if(setqid){
		t->file->dev = d->dev;
		t->file->mtime = d->mtime;
		t->file->qidpath = d->qid.path;
	}
	close(fd);
	rp = fbufalloc();
	for(q=q0; q<q1; q+=n){
		n = q1-q;
		if(n > RBUFSIZE)
			n = RBUFSIZE;
		bufread(t->file, q, rp, n);
		if(q < t->org)
			t->org += n;
		else if(q <= t->org+t->nchars)
			frinsert(t, rp, rp+n, q-t->org);
		if(t->lastlinefull)
			break;
	}
	fbuffree(rp);
	for(i=0; i<t->file->ntext; i++){
		u = t->file->text[i];
		if(u != t){
			if(u->org > u->file->nc)	/* will be 0 because of reset(), but safety first */
				u->org = 0;
			textresize(u, u->all);
			textbacknl(u, u->org, 0);	/* go to beginning of line */
		}
		textsetselect(u, q0, q0);
	}
	if(nulls)
		warning(nil, "%s: NUL bytes elided\n", file);
	free(d);
	return q1-q0;

    Rescue:
	close(fd);
	return 0;
}

uint32_t
textbsinsert(Text *t, uint32_t q0, Rune *r, uint32_t n, int tofile, int *nrp)
{
	Rune *bp, *tp, *up;
	int i, initial;

	if(t->what == Tag){	/* can't happen but safety first: mustn't backspace over file name */
    Err:
		textinsert(t, q0, r, n, tofile);
		*nrp = n;
		return q0;
	}
	bp = r;
	for(i=0; i<n; i++)
		if(*bp++ == '\b'){
			--bp;
			initial = 0;
			tp = runemalloc(n);
			runemove(tp, r, i);
			up = tp+i;
			for(; i<n; i++){
				*up = *bp++;
				if(*up == '\b')
					if(up == tp)
						initial++;
					else
						--up;
				else
					up++;
			}
			if(initial){
				if(initial > q0)
					initial = q0;
				q0 -= initial;
				textdelete(t, q0, q0+initial, tofile);
			}
			n = up-tp;
			textinsert(t, q0, tp, n, tofile);
			free(tp);
			*nrp = n;
			return q0;
		}
	goto Err;
}

void
textinsert(Text *t, uint32_t q0, Rune *r, uint32_t n, int tofile)
{
	int c, i;
	Text *u;

	if(tofile && t->ncache != 0)
		error("text.insert");
	if(n == 0)
		return;
	if(tofile){
		fileinsert(t->file, q0, r, n);
		if(t->what == Body){
			t->w->dirty = TRUE;
			t->w->utflastqid = -1;
		}
		if(t->file->ntext > 1)
			for(i=0; i<t->file->ntext; i++){
				u = t->file->text[i];
				if(u != t){
					u->w->dirty = TRUE;	/* always a body */
					textinsert(u, q0, r, n, FALSE);
					textsetselect(u, u->q0, u->q1);
					textscrdraw(u);
				}
			}
					
	}
	if(q0 < t->q1)
		t->q1 += n;
	if(q0 < t->q0)
		t->q0 += n;
	if(q0 < t->org)
		t->org += n;
	else if(q0 <= t->org+t->nchars)
		frinsert(t, r, r+n, q0-t->org);
	if(t->w){
		c = 'i';
		if(t->what == Body)
			c = 'I';
		if(n <= EVENTSIZE)
			winevent(t->w, "%c%d %d 0 %d %.*S\n", c, q0, q0+n, n, n, r);
		else
			winevent(t->w, "%c%d %d 0 0 \n", c, q0, q0+n, n);
	}
}

void
typecommit(Text *t)
{
	if(t->w != nil)
		wincommit(t->w, t);
	else
		textcommit(t, TRUE);
}

void
textfill(Text *t)
{
	Rune *rp;
	int i, n, m, nl;

	if(t->lastlinefull || t->nofill)
		return;
	if(t->ncache > 0)
		typecommit(t);
	rp = fbufalloc();
	do{
		n = t->file->nc-(t->org+t->nchars);
		if(n == 0)
			break;
		if(n > 2000)	/* educated guess at reasonable amount */
			n = 2000;
		bufread(t->file, t->org+t->nchars, rp, n);
		/*
		 * it's expensive to frinsert more than we need, so
		 * count newlines.
		 */
		nl = t->maxlines-t->nlines;
		m = 0;
		for(i=0; i<n; ){
			if(rp[i++] == '\n'){
				m++;
				if(m >= nl)
					break;
			}
		}
		frinsert(t, rp, rp+i, t->nchars);
	}while(t->lastlinefull == FALSE);
	fbuffree(rp);
}

void
textdelete(Text *t, uint32_t q0, uint32_t q1, int tofile)
{
	uint32_t n, p0, p1;
	int i, c;
	Text *u;

	if(tofile && t->ncache != 0)
		error("text.delete");
	n = q1-q0;
	if(n == 0)
		return;
	if(tofile){
		filedelete(t->file, q0, q1);
		if(t->what == Body){
			t->w->dirty = TRUE;
			t->w->utflastqid = -1;
		}
		if(t->file->ntext > 1)
			for(i=0; i<t->file->ntext; i++){
				u = t->file->text[i];
				if(u != t){
					u->w->dirty = TRUE;	/* always a body */
					textdelete(u, q0, q1, FALSE);
					textsetselect(u, u->q0, u->q1);
					textscrdraw(u);
				}
			}
	}
	if(q0 < t->q0)
		t->q0 -= min(n, t->q0-q0);
	if(q0 < t->q1)
		t->q1 -= min(n, t->q1-q0);
	if(q1 <= t->org)
		t->org -= n;
	else if(q0 < t->org+t->nchars){
		p1 = q1 - t->org;
		if(p1 > t->nchars)
			p1 = t->nchars;
		if(q0 < t->org){
			t->org = q0;
			p0 = 0;
		}else
			p0 = q0 - t->org;
		frdelete(t, p0, p1);
		textfill(t);
	}
	if(t->w){
		c = 'd';
		if(t->what == Body)
			c = 'D';
		winevent(t->w, "%c%d %d 0 0 \n", c, q0, q1);
	}
}

void
textconstrain(Text *t, uint32_t q0, uint32_t q1, uint32_t *p0, uint32_t *p1)
{
	*p0 = min(q0, t->file->nc);
	*p1 = min(q1, t->file->nc);
}

Rune
textreadc(Text *t, uint32_t q)
{
	Rune r;

	if(t->cq0<=q && q<t->cq0+t->ncache)
		r = t->cache[q-t->cq0];
	else
		bufread(t->file, q, &r, 1);
	return r;
}

int
textbswidth(Text *t, Rune c)
{
	uint32_t q, eq;
	Rune r;
	int skipping;

	/* there is known to be at least one character to erase */
	if(c == 0x08)	/* ^H: erase character */
		return 1;
	q = t->q0;
	skipping = TRUE;
	while(q > 0){
		r = textreadc(t, q-1);
		if(r == '\n'){		/* eat at most one more character */
			if(q == t->q0)	/* eat the newline */
				--q;
			break; 
		}
		if(c == 0x17){
			eq = isalnum(r);
			if(eq && skipping)	/* found one; stop skipping */
				skipping = FALSE;
			else if(!eq && !skipping)
				break;
		}
		--q;
	}
	return t->q0-q;
}

int
textfilewidth(Text *t, uint32_t q0, int oneelement)
{
	uint32_t q;
	Rune r;

	q = q0;
	while(q > 0){
		r = textreadc(t, q-1);
		if(r <= ' ')
			break;
		if(oneelement && r=='/')
			break;
		--q;
	}
	return q0-q;
}

Rune*
textcomplete(Text *t)
{
	int i, nstr, npath;
	uint32_t q;
	Rune tmp[200];
	Rune *str, *path;
	Rune *rp;
	Completion *c;
	char *s, *dirs;
	Runestr dir;

	/* control-f: filename completion; works back to white space or / */
	if(t->q0<t->file->nc && textreadc(t, t->q0)>' ')	/* must be at end of word */
		return nil;
	nstr = textfilewidth(t, t->q0, TRUE);
	str = runemalloc(nstr);
	npath = textfilewidth(t, t->q0-nstr, FALSE);
	path = runemalloc(npath);

	c = nil;
	rp = nil;
	dirs = nil;

	q = t->q0-nstr;
	for(i=0; i<nstr; i++)
		str[i] = textreadc(t, q++);
	q = t->q0-nstr-npath;
	for(i=0; i<npath; i++)
		path[i] = textreadc(t, q++);
	/* is path rooted? if not, we need to make it relative to window path */
	if(npath>0 && path[0]=='/')
		dir = (Runestr){path, npath};
	else{
		dir = dirname(t, nil, 0);
		if(dir.nr + 1 + npath > nelem(tmp)){
			free(dir.r);
			goto Return;
		}
		if(dir.nr == 0){
			dir.nr = 1;
			dir.r = runestrdup((Rune*)L".");
		}
		runemove(tmp, dir.r, dir.nr);
		tmp[dir.nr] = '/';
		runemove(tmp+dir.nr+1, path, npath);
		free(dir.r);
		dir.r = tmp;
		dir.nr += 1+npath;
		dir = cleanrname(dir);
	}

	s = smprint("%.*S", nstr, str);
	dirs = smprint("%.*S", dir.nr, dir.r);
	c = complete(dirs, s);
	free(s);
	if(c == nil){
		warning(nil, "error attempting completion: %r\n");
		goto Return;
	}

	if(!c->advance){
		warning(nil, "%.*S%s%.*S*%s\n",
			dir.nr, dir.r,
			dir.nr>0 && dir.r[dir.nr-1]!='/' ? "/" : "",
			nstr, str,
			c->nmatch? "" : ": no matches in:");
		for(i=0; i<c->nfile; i++)
			warning(nil, " %s\n", c->filename[i]);
	}

	if(c->advance)
		rp = runesmprint("%s", c->string);
	else
		rp = nil;
  Return:
	freecompletion(c);
	free(dirs);
	free(str);
	free(path);
	return rp;
}

void
texttype(Text *t, Rune r)
{
	uint32_t q0, q1;
	int nnb, nb, n, i;
	int nr;
	Rune *rp;
	Text *u;

	nr = 1;
	rp = &r;
	switch(r){
	case Kleft:
		typecommit(t);
		if(t->q0 > 0)
			textshow(t, t->q0-1, t->q0-1, TRUE);
		return;
	case Kright:
		typecommit(t);
		if(t->q1 < t->file->nc)
			textshow(t, t->q1+1, t->q1+1, TRUE);
		return;
	case Kdown:
		n = t->maxlines/3;
		goto case_Down;
	case Kscrollonedown:
		n = mousescrollsize(t->maxlines);
		if(n <= 0)
			n = 1;
		goto case_Down;
	case Kpgdown:
		n = 2*t->maxlines/3;
	case_Down:
		q0 = t->org+frcharofpt(t, Pt(t->r.min.x, t->r.min.y+n*t->font->height));
		if(t->what == Body)
			textsetorigin(t, q0, TRUE);
		return;
	case Kup:
		n = t->maxlines/3;
		goto case_Up;
	case Kscrolloneup:
		n = mousescrollsize(t->maxlines);
		goto case_Up;
	case Kpgup:
		n = 2*t->maxlines/3;
	case_Up:
		q0 = textbacknl(t, t->org, n);
		if(t->what == Body)
			textsetorigin(t, q0, TRUE);
		return;
	case Khome:
		typecommit(t);
		textshow(t, 0, 0, FALSE);
		return;
	case Kend:
		typecommit(t);
		textshow(t, t->file->nc, t->file->nc, FALSE);
		return;
	case 0x01:	/* ^A: beginning of line */
		typecommit(t);
		/* go to where ^U would erase, if not already at BOL */
		nnb = 0;
		if(t->q0>0 && textreadc(t, t->q0-1)!='\n')
			nnb = textbswidth(t, 0x15);
		textshow(t, t->q0-nnb, t->q0-nnb, TRUE);
		return;
	case 0x05:	/* ^E: end of line */
		typecommit(t);
		q0 = t->q0;
		while(q0<t->file->nc && textreadc(t, q0)!='\n')
			q0++;
		textshow(t, q0, q0, TRUE);
		return;
	}
	if(t->what == Body){
		seq++;
		filemark(t->file);
	}
	if(t->q1 > t->q0){
		if(t->ncache != 0)
			error("text.type");
		cut(t, t, nil, TRUE, TRUE, nil, 0);
		t->eq0 = ~0;
	}
	textshow(t, t->q0, t->q0, 1);
	switch(r){
	case 0x06:
	case Kins:
		rp = textcomplete(t);
		if(rp == nil)
			return;
		nr = runestrlen(rp);
		break;	/* fall through to normal insertion case */
	case 0x1B:
		if(t->eq0 != ~0)
			textsetselect(t, t->eq0, t->q0);
		if(t->ncache > 0)
			typecommit(t);
		return;
	case 0x08:	/* ^H: erase character */
	case 0x15:	/* ^U: erase line */
	case 0x17:	/* ^W: erase word */
		if(t->q0 == 0)	/* nothing to erase */
			return;
		nnb = textbswidth(t, r);
		q1 = t->q0;
		q0 = q1-nnb;
		/* if selection is at beginning of window, avoid deleting invisible text */
		if(q0 < t->org){
			q0 = t->org;
			nnb = q1-q0;
		}
		if(nnb <= 0)
			return;
		for(i=0; i<t->file->ntext; i++){
			u = t->file->text[i];
			u->nofill = TRUE;
			nb = nnb;
			n = u->ncache;
			if(n > 0){
				if(q1 != u->cq0+n)
					error("text.type backspace");
				if(n > nb)
					n = nb;
				u->ncache -= n;
				textdelete(u, q1-n, q1, FALSE);
				nb -= n;
			}
			if(u->eq0==q1 || u->eq0==~0)
				u->eq0 = q0;
			if(nb && u==t)
				textdelete(u, q0, q0+nb, TRUE);
			if(u != t)
				textsetselect(u, u->q0, u->q1);
			else
				textsetselect(t, q0, q0);
			u->nofill = FALSE;
		}
		for(i=0; i<t->file->ntext; i++)
			textfill(t->file->text[i]);
		return;
	case '\n':
		if(t->what == Body && t->w->autoindent){
			/* find beginning of previous line using backspace code */
			nnb = textbswidth(t, 0x15); /* ^U case */
			rp = runemalloc(nnb + 1);
			nr = 0;
			rp[nr++] = r;
			for(i=0; i<nnb; i++){
				r = textreadc(t, t->q0-nnb+i);
				if(r != ' ' && r != '\t')
					break;
				rp[nr++] = r;
			}
		}
		break; /* fall through to normal code */
	}
	/* otherwise ordinary character; just insert, typically in caches of all texts */
	for(i=0; i<t->file->ntext; i++){
		u = t->file->text[i];
		if(u->eq0 == ~0)
			u->eq0 = t->q0;
		if(u->ncache == 0)
			u->cq0 = t->q0;
		else if(t->q0 != u->cq0+u->ncache)
			error("text.type cq1");
		textinsert(u, t->q0, rp, nr, FALSE);
		if(u != t)
			textsetselect(u, u->q0, u->q1);
		if(u->ncache+nr > u->ncachealloc){
			u->ncachealloc += 10 + nr;
			u->cache = runerealloc(u->cache, u->ncachealloc);
		}
		runemove(u->cache+u->ncache, rp, nr);
		u->ncache += nr;
	}
	if(rp != &r)
		free(rp);
	textsetselect(t, t->q0+nr, t->q0+nr);
	if(r=='\n' && t->w!=nil)
		wincommit(t->w, t);
}

void
textcommit(Text *t, int tofile)
{
	if(t->ncache == 0)
		return;
	if(tofile)
		fileinsert(t->file, t->cq0, t->cache, t->ncache);
	if(t->what == Body){
		t->w->dirty = TRUE;
		t->w->utflastqid = -1;
	}
	t->ncache = 0;
}

static	Text	*clicktext;
static	uint32_t	clickmsec;
static	Text	*selecttext;
static	uint32_t	selectq;

/*
 * called from frame library
 */
void
framescroll(Frame *f, int dl)
{
	if(f != &selecttext->Frame)
		error("frameselect not right frame");
	textframescroll(selecttext, dl);
}

void
textframescroll(Text *t, int dl)
{
	uint32_t q0;

	if(dl == 0){
		scrsleep(100);
		return;
	}
	if(dl < 0){
		q0 = textbacknl(t, t->org, -dl);
		if(selectq > t->org+t->p0)
			textsetselect(t, t->org+t->p0, selectq);
		else
			textsetselect(t, selectq, t->org+t->p0);
	}else{
		if(t->org+t->nchars == t->file->nc)
			return;
		q0 = t->org+frcharofpt(t, Pt(t->r.min.x, t->r.min.y+dl*t->font->height));
		if(selectq > t->org+t->p1)
			textsetselect(t, t->org+t->p1, selectq);
		else
			textsetselect(t, selectq, t->org+t->p1);
	}
	textsetorigin(t, q0, TRUE);
}


void
textselect(Text *t)
{
	uint32_t q0, q1;
	int b, x, y;
	int state;

	selecttext = t;
	/*
	 * To have double-clicking and chording, we double-click
	 * immediately if it might make sense.
	 */
	b = mouse->buttons;
	q0 = t->q0;
	q1 = t->q1;
	selectq = t->org+frcharofpt(t, mouse->xy);
	if(clicktext==t && mouse->msec-clickmsec<500)
	if(q0==q1 && selectq==q0){
		textdoubleclick(t, &q0, &q1);
		textsetselect(t, q0, q1);
		flushimage(display, 1);
		x = mouse->xy.x;
		y = mouse->xy.y;
		/* stay here until something interesting happens */
		do
			readmouse(mousectl);
		while(mouse->buttons==b && abs(mouse->xy.x-x)<3 && abs(mouse->xy.y-y)<3);
		mouse->xy.x = x;	/* in case we're calling frselect */
		mouse->xy.y = y;
		q0 = t->q0;	/* may have changed */
		q1 = t->q1;
		selectq = q0;
	}
	if(mouse->buttons == b){
		t->Frame.scroll = framescroll;
		frselect(t, mousectl);
		/* horrible botch: while asleep, may have lost selection altogether */
		if(selectq > t->file->nc)
			selectq = t->org + t->p0;
		t->Frame.scroll = nil;
		if(selectq < t->org)
			q0 = selectq;
		else
			q0 = t->org + t->p0;
		if(selectq > t->org+t->nchars)
			q1 = selectq;
		else
			q1 = t->org+t->p1;
	}
	if(q0 == q1){
		if(q0==t->q0 && clicktext==t && mouse->msec-clickmsec<500){
			textdoubleclick(t, &q0, &q1);
			clicktext = nil;
		}else{
			clicktext = t;
			clickmsec = mouse->msec;
		}
	}else
		clicktext = nil;
	textsetselect(t, q0, q1);
	flushimage(display, 1);
	state = 0;	/* undo when possible; +1 for cut, -1 for paste */
	while(mouse->buttons){
		mouse->msec = 0;
		b = mouse->buttons;
		if((b&1) && (b&6)){
			if(state==0 && t->what==Body){
				seq++;
				filemark(t->w->body.file);
			}
			if(b & 2){
				if(state==-1 && t->what==Body){
					winundo(t->w, TRUE);
					textsetselect(t, q0, t->q0);
					state = 0;
				}else if(state != 1){
					cut(t, t, nil, TRUE, TRUE, nil, 0);
					state = 1;
				}
			}else{
				if(state==1 && t->what==Body){
					winundo(t->w, TRUE);
					textsetselect(t, q0, t->q1);
					state = 0;
				}else if(state != -1){
					paste(t, t, nil, TRUE, FALSE, nil, 0);
					state = -1;
				}
			}
			textscrdraw(t);
			clearmouse();
		}
		flushimage(display, 1);
		while(mouse->buttons == b)
			readmouse(mousectl);
		clicktext = nil;
	}
}

void
textshow(Text *t, uint32_t q0, uint32_t q1, int doselect)
{
	int qe;
	int nl;
	uint32_t q;

	if(t->what != Body){
		if(doselect)
			textsetselect(t, q0, q1);
		return;
	}
	if(t->w!=nil && t->maxlines==0)
		colgrow(t->col, t->w, 1);
	if(doselect)
		textsetselect(t, q0, q1);
	qe = t->org+t->nchars;
	if(t->org<=q0 && (q0<qe || (q0==qe && qe==t->file->nc+t->ncache)))
		textscrdraw(t);
	else{
		if(t->w->nopen[QWevent] > 0)
			nl = 3*t->maxlines/4;
		else
			nl = t->maxlines/4;
		q = textbacknl(t, q0, nl);
		/* avoid going backwards if trying to go forwards - long lines! */
		if(!(q0>t->org && q<t->org))
			textsetorigin(t, q, TRUE);
		while(q0 > t->org+t->nchars)
			textsetorigin(t, t->org+1, FALSE);
	}
}

static
int
region(int a, int b)
{
	if(a < b)
		return -1;
	if(a == b)
		return 0;
	return 1;
}

void
selrestore(Frame *f, Point pt0, uint32_t p0, uint32_t p1)
{
	if(p1<=f->p0 || p0>=f->p1){
		/* no overlap */
		frdrawsel0(f, pt0, p0, p1, f->cols[BACK], f->cols[TEXT]);
		return;
	}
	if(p0>=f->p0 && p1<=f->p1){
		/* entirely inside */
		frdrawsel0(f, pt0, p0, p1, f->cols[HIGH], f->cols[HTEXT]);
		return;
	}

	/* they now are known to overlap */

	/* before selection */
	if(p0 < f->p0){
		frdrawsel0(f, pt0, p0, f->p0, f->cols[BACK], f->cols[TEXT]);
		p0 = f->p0;
		pt0 = frptofchar(f, p0);
	}
	/* after selection */
	if(p1 > f->p1){
		frdrawsel0(f, frptofchar(f, f->p1), f->p1, p1, f->cols[BACK], f->cols[TEXT]);
		p1 = f->p1;
	}
	/* inside selection */
	frdrawsel0(f, pt0, p0, p1, f->cols[HIGH], f->cols[HTEXT]);
}

void
textsetselect(Text *t, uint32_t q0, uint32_t q1)
{
	int p0, p1;

	/* t->p0 and t->p1 are always right; t->q0 and t->q1 may be off */
	t->q0 = q0;
	t->q1 = q1;
	/* compute desired p0,p1 from q0,q1 */
	p0 = q0-t->org;
	p1 = q1-t->org;
	if(p0 < 0)
		p0 = 0;
	if(p1 < 0)
		p1 = 0;
	if(p0 > t->nchars)
		p0 = t->nchars;
	if(p1 > t->nchars)
		p1 = t->nchars;
	if(p0==t->p0 && p1==t->p1)
		return;
	/* screen disagrees with desired selection */
	if(t->p1<=p0 || p1<=t->p0 || p0==p1 || t->p1==t->p0){
		/* no overlap or too easy to bother trying */
		frdrawsel(t, frptofchar(t, t->p0), t->p0, t->p1, 0);
		frdrawsel(t, frptofchar(t, p0), p0, p1, 1);
		goto Return;
	}
	/* overlap; avoid unnecessary painting */
	if(p0 < t->p0){
		/* extend selection backwards */
		frdrawsel(t, frptofchar(t, p0), p0, t->p0, 1);
	}else if(p0 > t->p0){
		/* trim first part of selection */
		frdrawsel(t, frptofchar(t, t->p0), t->p0, p0, 0);
	}
	if(p1 > t->p1){
		/* extend selection forwards */
		frdrawsel(t, frptofchar(t, t->p1), t->p1, p1, 1);
	}else if(p1 < t->p1){
		/* trim last part of selection */
		frdrawsel(t, frptofchar(t, p1), p1, t->p1, 0);
	}

    Return:
	t->p0 = p0;
	t->p1 = p1;
}

/*
 * Release the button in less than DELAY ms and it's considered a null selection
 * if the mouse hardly moved, regardless of whether it crossed a char boundary.
 */
enum {
	DELAY = 2,
	MINMOVE = 4,
};

uint32_t
xselect(Frame *f, Mousectl *mc, Image *col, uint32_t *p1p)	/* when called, button is down */
{
	uint32_t p0, p1, q, tmp;
	uint32_t msec;
	Point mp, pt0, pt1, qt;
	int reg, b;

	mp = mc->xy;
	b = mc->buttons;
	msec = mc->msec;

	/* remove tick */
	if(f->p0 == f->p1)
		frtick(f, frptofchar(f, f->p0), 0);
	p0 = p1 = frcharofpt(f, mp);
	pt0 = frptofchar(f, p0);
	pt1 = frptofchar(f, p1);
	reg = 0;
	frtick(f, pt0, 1);
	do{
		q = frcharofpt(f, mc->xy);
		if(p1 != q){
			if(p0 == p1)
				frtick(f, pt0, 0);
			if(reg != region(q, p0)){	/* crossed starting point; reset */
				if(reg > 0)
					selrestore(f, pt0, p0, p1);
				else if(reg < 0)
					selrestore(f, pt1, p1, p0);
				p1 = p0;
				pt1 = pt0;
				reg = region(q, p0);
				if(reg == 0)
					frdrawsel0(f, pt0, p0, p1, col, display->white);
			}
			qt = frptofchar(f, q);
			if(reg > 0){
				if(q > p1)
					frdrawsel0(f, pt1, p1, q, col, display->white);

				else if(q < p1)
					selrestore(f, qt, q, p1);
			}else if(reg < 0){
				if(q > p1)
					selrestore(f, pt1, p1, q);
				else
					frdrawsel0(f, qt, q, p1, col, display->white);
			}
			p1 = q;
			pt1 = qt;
		}
		if(p0 == p1)
			frtick(f, pt0, 1);
		flushimage(f->display, 1);
		readmouse(mc);
	}while(mc->buttons == b);
	if(mc->msec-msec < DELAY && p0!=p1
	&& abs(mp.x-mc->xy.x)<MINMOVE
	&& abs(mp.y-mc->xy.y)<MINMOVE) {
		if(reg > 0)
			selrestore(f, pt0, p0, p1);
		else if(reg < 0)
			selrestore(f, pt1, p1, p0);
		p1 = p0;
	}
	if(p1 < p0){
		tmp = p0;
		p0 = p1;
		p1 = tmp;
	}
	pt0 = frptofchar(f, p0);
	if(p0 == p1)
		frtick(f, pt0, 0);
	selrestore(f, pt0, p0, p1);
	/* restore tick */
	if(f->p0 == f->p1)
		frtick(f, frptofchar(f, f->p0), 1);
	flushimage(f->display, 1);
	*p1p = p1;
	return p0;
}

int
textselect23(Text *t, uint32_t *q0, uint32_t *q1, Image *high, int mask)
{
	uint32_t p0, p1;
	int buts;
	
	p0 = xselect(t, mousectl, high, &p1);
	buts = mousectl->buttons;
	if((buts & mask) == 0){
		*q0 = p0+t->org;
		*q1 = p1+t->org;
	}

	while(mousectl->buttons)
		readmouse(mousectl);
	return buts;
}

int
textselect2(Text *t, uint32_t *q0, uint32_t *q1, Text **tp)
{
	int buts;

	*tp = nil;
	buts = textselect23(t, q0, q1, but2col, 4);
	if(buts & 4)
		return 0;
	if(buts & 1){	/* pick up argument */
		*tp = argtext;
		return 1;
	}
	return 1;
}

int
textselect3(Text *t, uint32_t *q0, uint32_t *q1)
{
	int h;

	h = (textselect23(t, q0, q1, but3col, 1|2) == 0);
	return h;
}

static Rune left1[] =  { L'{', L'[', L'(', L'<', L'«', 0 };
static Rune right1[] = { L'}', L']', L')', L'>', L'»', 0 };
static Rune left2[] =  { L'\n', 0 };
static Rune left3[] =  { L'\'', L'"', L'`', 0 };

static
Rune *left[] = {
	left1,
	left2,
	left3,
	nil
};
static
Rune *right[] = {
	right1,
	left2,
	left3,
	nil
};

void
textdoubleclick(Text *t, uint32_t *q0, uint32_t *q1)
{
	int c, i;
	Rune *r, *l, *p;
	uint32_t q;

	for(i=0; left[i]!=nil; i++){
		q = *q0;
		l = left[i];
		r = right[i];
		/* try matching character to left, looking right */
		if(q == 0)
			c = '\n';
		else
			c = textreadc(t, q-1);
		p = runestrchr(l, c);
		if(p != nil){
			if(textclickmatch(t, c, r[p-l], 1, &q))
				*q1 = q-(c!='\n');
			return;
		}
		/* try matching character to right, looking left */
		if(q == t->file->nc)
			c = '\n';
		else
			c = textreadc(t, q);
		p = runestrchr(r, c);
		if(p != nil){
			if(textclickmatch(t, c, l[p-r], -1, &q)){
				*q1 = *q0+(*q0<t->file->nc && c=='\n');
				*q0 = q;
				if(c!='\n' || q!=0 || textreadc(t, 0)=='\n')
					(*q0)++;
			}
			return;
		}
	}
	/* try filling out word to right */
	while(*q1<t->file->nc && isalnum(textreadc(t, *q1)))
		(*q1)++;
	/* try filling out word to left */
	while(*q0>0 && isalnum(textreadc(t, *q0-1)))
		(*q0)--;
}

int
textclickmatch(Text *t, int cl, int cr, int dir, uint32_t *q)
{
	Rune c;
	int nest;

	nest = 1;
	for(;;){
		if(dir > 0){
			if(*q == t->file->nc)
				break;
			c = textreadc(t, *q);
			(*q)++;
		}else{
			if(*q == 0)
				break;
			(*q)--;
			c = textreadc(t, *q);
		}
		if(c == cr){
			if(--nest==0)
				return 1;
		}else if(c == cl)
			nest++;
	}
	return cl=='\n' && nest==1;
}

uint32_t
textbacknl(Text *t, uint32_t p, uint32_t n)
{
	int i, j;

	/* look for start of this line if n==0 */
	if(n==0 && p>0 && textreadc(t, p-1)!='\n')
		n = 1;
	i = n;
	while(i-->0 && p>0){
		--p;	/* it's at a newline now; back over it */
		if(p == 0)
			break;
		/* at 128 chars, call it a line anyway */
		for(j=128; --j>0 && p>0; p--)
			if(textreadc(t, p-1)=='\n')
				break;
	}
	return p;
}

void
textsetorigin(Text *t, uint32_t org, int exact)
{
	int i, a, fixup;
	Rune *r;
	uint32_t n;

	if(org>0 && !exact){
		/* org is an estimate of the char posn; find a newline */
		/* don't try harder than 256 chars */
		for(i=0; i<256 && org<t->file->nc; i++){
			if(textreadc(t, org) == '\n'){
				org++;
				break;
			}
			org++;
		}
	}
	a = org-t->org;
	fixup = 0;
	if(a>=0 && a<t->nchars){
		frdelete(t, 0, a);
		fixup = 1;	/* frdelete can leave end of last line in wrong selection mode; it doesn't know what follows */
	}
	else if(a<0 && -a<t->nchars){
		n = t->org - org;
		r = runemalloc(n);
		bufread(t->file, org, r, n);
		frinsert(t, r, r+n, 0);
		free(r);
	}else
		frdelete(t, 0, t->nchars);
	t->org = org;
	textfill(t);
	textscrdraw(t);
	textsetselect(t, t->q0, t->q1);
	if(fixup && t->p1 > t->p0)
		frdrawsel(t, frptofchar(t, t->p1-1), t->p1-1, t->p1, 1);
}

void
textreset(Text *t)
{
	t->file->seq = 0;
	t->eq0 = ~0;
	/* do t->delete(0, t->nc, TRUE) without building backup stuff */
	textsetselect(t, t->org, t->org);
	frdelete(t, 0, t->nchars);
	t->org = 0;
	t->q0 = 0;
	t->q1 = 0;
	filereset(t->file);
	bufreset(t->file);
}
