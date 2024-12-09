// This is free and unencumbered software released into the public domain.
//
// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.
//
// In jurisdictions that recognize copyright laws, the author or authors
// of this software dedicate any and all copyright interest in the
// software to the public domain. We make this dedication for the benefit
// of the public at large and to the detriment of our heirs and
// successors. We intend this dedication to be an overt act of
// relinquishment in perpetuity of all present and future rights to this
// software under copyright law.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//
// For more information, please refer to <https://unlicense.org>
#ifndef LIBLZ2K_LZ2K_H
#define LIBLZ2K_LZ2K_H

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>
//#include "Format.h"

namespace LZ2K {

class LZ2KBitstream {
public:
	explicit LZ2KBitstream(const uint8_t *input, unsigned int inputSize) : input(input), inputSize(inputSize) {
		Ingest(32);
	}

	void Ingest(unsigned int bits) {
		//std::cout << Formatted("Ingesting %u bits into shift register of %08x, with %u bits unused of %02x", bits, sr, 8 - usedBits, *input) << std::endl;
		while (bits > 0) {
			const auto bitsDesired = std::min(bits, 8u);
			const auto bitsToRead = std::min(bitsDesired, 8u - usedBits);
			const auto readBits = (inputSize) ? ((*input << usedBits) >> (8 - bitsToRead)) : 0u;
			sr = (sr << bitsToRead) | readBits;
			usedBits += bitsToRead;
			if (usedBits >= 8) {
				usedBits = 0;
				input++;
				inputSize--;
			}
			bits -= bitsToRead;
		}
		//std::cout << Formatted("Shift register is now %08x, with %u bits unused of %02x", sr, 8 - usedBits, *input) << std::endl;
	}

	uint32_t Get(unsigned int bits) {
		if (!bits) {
			return 0;
		}
		const auto value = sr >> (32u - bits);
		Ingest(bits);
		return value;
	}

private:
	const uint8_t *input = nullptr;
	unsigned int inputSize = 0u;
	unsigned int usedBits = 0u;
	uint32_t sr = 0u;
};

class LZ2KDecoder {
public:
	LZ2KDecoder() = default;

	void InitializeHybrid(LZ2KBitstream& bs, unsigned int alphabetSize, unsigned int log2AlphabetSize, unsigned int seg1Count);
	void InitializeCoded(LZ2KBitstream& bs, unsigned int alphabetSize, unsigned int log2AlphabetSize, const LZ2KDecoder& decoder);
	void GenerateCodes();

	unsigned int Decode(LZ2KBitstream& bs) const {
		if (singleSymbol != -1) {
			return singleSymbol;
		}
		auto length = 0u;
		auto code = 0u;
		do {
			code = (code << 1) | bs.Get(1);
			length++;
			const auto symbol = Lookup(code, length);
			if (symbol != -1) {
				return symbol;
			}
		} while (length < 16);
		throw std::runtime_error("Unable to decode symbol");
	}

	int Lookup(unsigned int code, unsigned int length) const {
		if (singleSymbol != -1) {
			return singleSymbol;
		}
		for (auto idx = 0u; idx < symbolCodeLengths.size(); idx++) {
			if ((length == symbolCodeLengths[idx]) && (code == symbolCodes[idx])) {
				return idx;
			}
		}
		return -1;
	}

private:
	int singleSymbol = -1;					// SSYM
	uint32_t usedSymbolCount = 0u;			// US
	std::vector<uint32_t> symbolCodes;		// CD
	std::vector<uint8_t> symbolCodeLengths;	// CL
};

class LZ2KDecompressor {
public:
	explicit LZ2KDecompressor(LZ2KBitstream& bs) : bs(bs) { }

	unsigned int Decompress(uint8_t *output);

private:
	static const auto WindowSize = 8192u;
	static const auto CLAlphabetSize = 19u;
	static const auto LitAlphabetSize = 510u;
	static const auto OffsetAlphabetSize = 14u;

	LZ2KBitstream& bs;
	LZ2KDecoder clDecoder;
	LZ2KDecoder litDecoder;
	LZ2KDecoder offDecoder;

	unsigned int bytesInBlock = 0u;
	unsigned int bytesDecoded = 0u;

	void Initialize();
};

}

#endif
