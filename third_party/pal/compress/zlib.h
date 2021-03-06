#pragma once

#include "../io/io.h"
#include "../io/Buffer.h"

namespace imqs {
namespace compress {
namespace zlib {

enum Flags {
	FlagShrinkBuffer = 1, // For Compress() only, shrink the encoded heap-allocated buffer to it's minimal size.
	FlagDeflate      = 2,
	FlagGzip         = 4,
	FlagLevel0       = 1 << 4,
	FlagLevel1       = 2 << 4,
	FlagLevel2       = 3 << 4,
	FlagLevel3       = 4 << 4,
	FlagLevel4       = 5 << 4,
	FlagLevel5       = 6 << 4,
	FlagLevel6       = 7 << 4,
	FlagLevel7       = 8 << 4,
	FlagLevel8       = 9 << 4,
	FlagLevel9       = 10 << 4,
};

inline int LevelFromFlags(uint32_t flags) { return (int) (15 & (flags >> 4)) - 1; }

// Compress a buffer using zlib. The buffer returned in 'enc' must be freed with free().
// You must specify either FlagDeflate or FlagGzip
IMQS_PAL_API Error Compress(const void* raw, size_t rawLen, void*& enc, size_t& encLen, uint32_t flags);

// Wrapper around deflateInit2
IMQS_PAL_API Error DeflateInit(z_stream& stream, uint32_t flags);

// Stream compressor
// When finished, you must flush the compressor by writing zero bytes to it. For example, Write(nullptr, 0).
class IMQS_PAL_API Compressor : public io::Writer {
public:
	io::Writer* Target = nullptr; // Destination of compressed bytes

	Compressor();
	Compressor(io::Writer* target, uint32_t flags);
	~Compressor();

	uint32_t GetFlags() const { return Flags; }
	void     SetFlags(uint32_t flags);

	Error Write(const void* buf, size_t len) override;

private:
	z_stream   Stream;
	bool       IsInitialized   = false;
	uint32_t   Flags           = 0;
	uint64_t   TotalCompressed = 0;
	io::Buffer OutBuf;
};

} // namespace zlib
} // namespace compress
} // namespace imqs
