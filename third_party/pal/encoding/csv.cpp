#include "pch.h"
#include "csv.h"
#include "../alloc.h"

namespace imqs {
namespace csv {

Decoder::Decoder(io::Reader* r) {
	SetReader(r);
}

Decoder::~Decoder() {
	free(Buf);
}

void Decoder::SetReader(io::Reader* r) {
	Reader = r;
	ResetBuffer();
}

void Decoder::ResetBuffer() {
	BufPos = 0;
	BufLen = 0;
}

void Decoder::SetBufferSize(size_t cap) {
	BufCap = cap;
	if (Buf)
		Buf = (char*) imqs_realloc_or_die(Buf, BufCap);
}

Error Decoder::ReadLine(std::string& decoded, std::vector<size_t>& starts) {
	enum States {
		CellStart,    // start of cell
		Raw,          // inside cell, content is not quoted
		Quoted,       // inside cell, content is quoted
		DoubleQuoted, // inside cell, content is quoted, and we've now seen a second quote. Could end of cell, or escaped quote
	} state = CellStart;

	if (!Buf)
		Buf = (char*) imqs_malloc_or_die(BufCap);

	// Keep local copies of constants, to avoid member class member variable lookup on each character.
	// Also, maintaining pointers instead of offsets makes it easier to deal with the utf8 decoding.
	const char* s   = Buf + BufPos;
	char*       end = Buf + BufLen;

	// make local copies to avoid class member lookups in the inner loop
	char _sep   = Separator;
	char _quote = Quote;

	bool eof = false;
	while (true) {
		// +---------------+------------+---------+
		// |   consumed    | remaining  | unused  |
		// +---------------+------------+---------+
		// Buf             s            end       Buf+BufCap
		if (s == end) {
			// refill buffer
			s          = Buf;
			end        = Buf;
			size_t n   = BufCap;
			auto   err = Reader->Read(end, n);
			if (!err.OK()) {
				n = 0;
				if (err == ErrEOF)
					eof = true;
				else
					return err;
			}
			end += n;
			if (end - s == 0)
				break;
		}
		char cp = *s;
		s++;
		if (state == CellStart) {
			if (cp == _quote) {
				state = Quoted;
				starts.push_back(decoded.size());
			} else if (cp == '\n') {
				// ignore. This is the \n after a \r\n pair, from our previous line
			} else {
				state = Raw;
				starts.push_back(decoded.size());
				decoded.push_back(cp);
			}
		} else if (state == Raw) {
			if (cp == _sep)
				state = CellStart;
			else if (cp == '\r' || cp == '\n')
				break;
			else
				decoded.push_back(cp);
		} else if (state == Quoted) {
			if (cp == _quote)
				state = DoubleQuoted;
			else
				decoded.push_back(cp);
		} else if (state == DoubleQuoted) {
			if (cp == _quote) {
				// escaped quote
				state = Quoted;
				decoded.push_back('"');
			} else if (cp == _sep) {
				// end of quoted cell
				state = Raw;
			} else {
				// don't know what this ought to mean. Ignore the quote character.
				decoded.push_back(cp);
				state = Quoted;
			}
		}
	}

	BufPos = s - Buf;
	BufLen = end - Buf;

	// if (state == Quoted): unterminated quote. Just ignore.

	// add a terminal
	if (starts.size() != 0)
		starts.push_back(decoded.size());

	if (eof && starts.size() == 0)
		return ErrEOF;

	return Error();
}

Error Decoder::ClearAndReadLine(std::string& decoded, std::vector<size_t>& starts) {
	decoded.clear();
	starts.clear();
	return ReadLine(decoded, starts);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Error Encoder::Write(io::Writer* w, const char* str, size_t len) {
	bool needEscape = false;
	if (len == -1)
		len = strlen(str);
	for (size_t i = 0; i < len; i++) {
		if (str[i] == ',' || str[i] == '"' || str[i] == 10 || str[i] == 13) {
			needEscape = true;
			break;
		}
	}
	if (!needEscape)
		return w->Write(str, len);

	CellBuf.resize(0);
	CellBuf.push_back('"');
	for (size_t i = 0; i < len; i++) {
		if (str[i] == '"') {
			CellBuf.push_back('"');
			CellBuf.push_back('"');
		} else {
			CellBuf.push_back(str[i]);
		}
	}
	CellBuf.push_back('"');
	return w->Write(&CellBuf[0], CellBuf.size());
}

Error Encoder::WriteWithComma(io::Writer* w, const char* str, size_t len) {
	auto err = Write(w, str, len);
	if (err.OK())
		err = w->Write(",", 1);
	return err;
}

} // namespace csv
} // namespace imqs
