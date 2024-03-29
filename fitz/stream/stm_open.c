/*
 * Creation and destruction.
 */

#include "fitz-base.h"
#include "fitz-stream.h"

static fz_stream *
newstm(int kind, int mode)
{
	fz_stream *stm;

	stm = fz_malloc(sizeof(fz_stream));
	if (!stm)
		return nil;

	stm->refs = 1;
	stm->kind = kind;
	stm->mode = mode;
	stm->dead = 0;
	stm->error = fz_okay;
	stm->buffer = nil;

	stm->chain = nil;
	stm->filter = nil;
	stm->file = -1;

	return stm;
}

fz_stream *
fz_keepstream(fz_stream *stm)
{
	stm->refs ++;
	return stm;
}

void
fz_dropstream(fz_stream *stm)
{
	stm->refs --;
	if (stm->refs == 0)
	{
		if (stm->error)
		{
			fflush(stdout);
			fz_printerror(stm->error);
			fz_droperror(stm->error);
			fflush(stderr);
			fz_warn("dropped unhandled ioerror");
		}

		if (stm->mode == FZ_SWRITE)
		{
			stm->buffer->eof = 1;
			fz_flush(stm);
		}

		switch (stm->kind)
		{
		case FZ_SFILE:
			_close(stm->file);
			break;
		case FZ_SFILTER:
			fz_dropfilter(stm->filter);
			fz_dropstream(stm->chain);
			break;
		case FZ_SBUFFER:
			break;
		}

		fz_dropbuffer(stm->buffer);
		fz_free(stm);
	}
}

#ifdef WIN32_UNICODE_HACK
static fz_error *
openfilew(fz_stream **stmp, const wchar_t *path, int mode, int realmode)
{
	fz_error *error;
	fz_stream *stm;

	stm = newstm(FZ_SFILE, mode);
	if (!stm)
		return fz_throw("outofmem: stream struct");

	error = fz_newbuffer(&stm->buffer, FZ_BUFSIZE);
	if (error)
	{
		fz_free(stm);
		return fz_rethrow(error, "cannot create buffer");
	}

	stm->file = _wopen(path, realmode, 0666);
	if (stm->file < 0)
	{
		fz_dropbuffer(stm->buffer);
		fz_free(stm);
		return fz_throw("syserr: open %s", strerror(errno));
	}

	*stmp = stm;
	return fz_okay;
}
#endif

static fz_error *
openfile(fz_stream **stmp, char *path, int mode, int realmode)
{
	fz_error *error;
	fz_stream *stm;

	stm = newstm(FZ_SFILE, mode);
	if (!stm)
		return fz_throw("outofmem: stream struct");

	error = fz_newbuffer(&stm->buffer, FZ_BUFSIZE);
	if (error)
	{
		fz_free(stm);
		return fz_rethrow(error, "cannot create buffer");
	}

	stm->file = _open(path, realmode, 0666);
	if (stm->file < 0)
	{
		fz_dropbuffer(stm->buffer);
		fz_free(stm);
		return fz_throw("syserr: open '%s': %s", path, strerror(errno));
	}

	*stmp = stm;
	return fz_okay;
}

static fz_error *
openfilter(fz_stream **stmp, fz_filter *flt, fz_stream *src, int mode)
{
	fz_error *error;
	fz_stream *stm;

	stm = newstm(FZ_SFILTER, mode);
	if (!stm)
		return fz_throw("outofmem: stream struct");

	error = fz_newbuffer(&stm->buffer, FZ_BUFSIZE);
	if (error)
	{
		fz_free(stm);
		return fz_rethrow(error, "cannot create buffer");
	}

	stm->chain = fz_keepstream(src);
	stm->filter = fz_keepfilter(flt);

	*stmp = stm;
	return fz_okay;
}

static fz_error *
openbuffer(fz_stream **stmp, fz_buffer *buf, int mode)
{
	fz_stream *stm;

	stm = newstm(FZ_SBUFFER, mode);
	if (!stm)
		return fz_throw("outofmem: stream struct");

	stm->buffer = fz_keepbuffer(buf);

	if (mode == FZ_SREAD)
		stm->buffer->eof = 1;

	*stmp = stm;
	return fz_okay;
}

#ifdef WIN32_UNICODE_HACK
fz_error * fz_openrfilew(fz_stream **stmp, const wchar_t *path)
{
	fz_error *error;
	error = openfilew(stmp, path, FZ_SREAD, O_BINARY | O_RDONLY);
	if (error)
		return fz_rethrow(error, "cannot open file for reading: '%s'", path);
	return fz_okay;
}
#endif

fz_error * fz_openrfile(fz_stream **stmp, char *path)
{
	fz_error *error;
	error = openfile(stmp, path, FZ_SREAD, O_BINARY | O_RDONLY);
	if (error)
		return fz_rethrow(error, "cannot open file for reading: '%s'", path);
	return fz_okay;
}

fz_error * fz_openwfile(fz_stream **stmp, char *path)
{
	fz_error *error;
	error = openfile(stmp, path, FZ_SWRITE,
			O_BINARY | O_WRONLY | O_CREAT | O_TRUNC);
	if (error)
		return fz_rethrow(error, "cannot open file for writing: '%s'", path);
	return fz_okay;
}

fz_error * fz_openafile(fz_stream **stmp, char *path)
{
	fz_error *error;
	int t;

	error = openfile(stmp, path, FZ_SWRITE, O_BINARY | O_WRONLY);
	if (error)
		return fz_rethrow(error, "cannot open file for writing: '%s'", path);

	t = _lseek((*stmp)->file, 0, 2);
	if (t < 0)
	{
		(*stmp)->dead = 1;
		return fz_throw("syserr: lseek '%s': %s", path, strerror(errno));
	}

	return fz_okay;
}

fz_error * fz_openrfilter(fz_stream **stmp, fz_filter *flt, fz_stream *src)
{
	fz_error *error;
	error = openfilter(stmp, flt, src, FZ_SREAD);
	if (error)
		return fz_rethrow(error, "cannot create reading filter stream");
	return fz_okay;
}

fz_error * fz_openwfilter(fz_stream **stmp, fz_filter *flt, fz_stream *src)
{
	fz_error *error;
	error = openfilter(stmp, flt, src, FZ_SWRITE);
	if (error)
		return fz_rethrow(error, "cannot create writing filter stream");
	return fz_okay;
}

fz_error * fz_openrbuffer(fz_stream **stmp, fz_buffer *buf)
{
	fz_error *error;
	error = openbuffer(stmp, buf, FZ_SREAD);
	if (error)
		return fz_rethrow(error, "cannot create reading buffer stream");
	return fz_okay;
}

fz_error * fz_openwbuffer(fz_stream **stmp, fz_buffer *buf)
{
	fz_error *error;
	error = openbuffer(stmp, buf, FZ_SWRITE);
	if (error)
		return fz_rethrow(error, "cannot create writing buffer stream");
	return fz_okay;
}

fz_error * fz_openrmemory(fz_stream **stmp, unsigned char *mem, int len)
{
	fz_error *error;
	fz_buffer *buf;

	error = fz_newbufferwithmemory(&buf, mem, len);
	if (error)
		return fz_rethrow(error, "cannot create memory buffer");

	error = fz_openrbuffer(stmp, buf);
	if (error)
	{
		fz_dropbuffer(buf);
		return fz_rethrow(error, "cannot open memory buffer stream");
	}

	fz_dropbuffer(buf);

	return fz_okay;
}

