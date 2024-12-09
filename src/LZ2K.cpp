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
#include <iostream>
#include <lz2k/LZ2K.h>
//#include "Format.h"

namespace LZ2K {

void LZ2KDecoder::InitializeHybrid(LZ2KBitstream& bs, unsigned int alphabetSize, unsigned int log2AlphabetSize, unsigned int seg1Count) {
	// Used for initializing codes that use hybrid binary/unary encoding of code lengths (namely, the code-length dictionary,
	// and the LZ offset dictionary)
	symbolCodes.resize(alphabetSize);
	symbolCodeLengths.resize(alphabetSize);
	usedSymbolCount = bs.Get(log2AlphabetSize);
	//std::cout << Formatted("%u symbols appear in hybrid dictionary", usedSymbolCount) << std::endl;
	if (!usedSymbolCount) {
		singleSymbol = bs.Get(log2AlphabetSize);
		usedSymbolCount = 1u;
		return;
	}
	auto symbolId = 0u;
	while (symbolId < usedSymbolCount) {
		auto codeLength = bs.Get(3);
		if (codeLength == 7) {
			while (bs.Get(1)) {
				codeLength++;
			}
		}
		symbolCodeLengths[symbolId] = codeLength;
		//std::cout << Formatted("Length for symbol %u is %u", symbolId, codeLength) << std::endl;
		symbolId++;
		if (symbolId == seg1Count) {
			const auto skipCount = bs.Get(2);
			for (auto idx = 0u; idx < skipCount; idx++) {
				symbolCodeLengths[symbolId] = 0;
				symbolId++;
			}
		}
	}
	GenerateCodes();
}

void LZ2KDecoder::InitializeCoded(LZ2KBitstream& bs, unsigned int alphabetSize, unsigned int log2AlphabetSize, const LZ2KDecoder& decoder) {
	// Used for initializing codes that use prefix-coding for code lengths (namely, the literal & length dictionary)
	symbolCodes.resize(alphabetSize);
	symbolCodeLengths.resize(alphabetSize);
	usedSymbolCount = bs.Get(log2AlphabetSize);
	//std::cout << Formatted("%u symbols appear in coded dictionary", usedSymbolCount) << std::endl;
	if (!usedSymbolCount) {
		singleSymbol = bs.Get(log2AlphabetSize);
		usedSymbolCount = 1u;
		return;
	}
	auto symbolId = 0u;
	while (symbolId < usedSymbolCount) {
		const auto lengthSymbol = decoder.Decode(bs);
		switch (lengthSymbol) {
			case 0:
				//std::cout << Formatted("Length for symbol %u is %u", symbolId, lengthSymbol) << std::endl;
				symbolCodeLengths[symbolId] = 0;
				symbolId++;
				break;
			case 1: {
				const auto runLength = 3u + bs.Get(4);
				//std::cout << Formatted("Decoding a run of zeroes of length %u", runLength) << std::endl;
				for (auto idx = 0u; idx < runLength; idx++) {
					symbolCodeLengths[symbolId] = 0u;
					symbolId++;
				}
				break;
			}
			case 2: {
				const auto runLength = 20u + bs.Get(9);
				//std::cout << Formatted("Decoding a run of zeroes of length %u", runLength) << std::endl;
				for (auto idx = 0u; idx < runLength; idx++) {
					symbolCodeLengths[symbolId] = 0u;
					symbolId++;
				}
				break;
			}
			default:
				//std::cout << Formatted("Length for symbol %u is %u", symbolId, lengthSymbol - 2u) << std::endl;
				symbolCodeLengths[symbolId] = lengthSymbol - 2u;
				symbolId++;
		}
	}
	GenerateCodes();
}

void LZ2KDecoder::GenerateCodes() {
	std::array<uint32_t,17> codesPerLength = { 0 };

	for (const auto length : symbolCodeLengths) {
		codesPerLength[length]++;
	}

	/*std::cout << "Done generating length count list:" << std::endl;
	for (auto length = 0u; length < codesPerLength.size(); length++) {
		std::cout << Formatted("%u: %u", length, codesPerLength[length]) << std::endl;
	}*/

	auto nextCode = 0u;
	for (auto length = 1u; length <= 16u; length++) {
		const auto spanPerCode = (1u << (16 - length));
		for (auto symbolId = 0u; symbolId < symbolCodeLengths.size(); symbolId++) {
			if (symbolCodeLengths[symbolId] == length) {
				symbolCodes[symbolId] = (nextCode >> (16 - length));
				nextCode += spanPerCode;
			}
		}
	}

	/*for (auto symbolId = 0u; symbolId < symbolCodeLengths.size(); symbolId++) {
		if (!symbolCodeLengths[symbolId]) {
			continue;
		}
		std::cout << Formatted("Symbol %u of length %u gets code %x", symbolId, symbolCodeLengths[symbolId], (symbolCodes[symbolId] << (16 - symbolCodeLengths[symbolId]))) << std::endl;
	}*/
}

void LZ2KDecompressor::Initialize() {
	bytesInBlock = bs.Get(16);
	//std::cout << Formatted("%u bytes in block", bytesInBlock) << std::endl;
	//std::cout << "Reading in code-length dictionary" << std::endl;
	clDecoder.InitializeHybrid(bs, CLAlphabetSize, 5u, 3u);
	//std::cout << "Reading in literal dictionary" << std::endl;
	litDecoder.InitializeCoded(bs, LitAlphabetSize, 9u, clDecoder);
	//std::cout << "Reading in offset dictionary" << std::endl;
	offDecoder.InitializeHybrid(bs, OffsetAlphabetSize, 4u, -1u);
}

static void Repeat(uint8_t *output, uint32_t offset, uint32_t length) {
	const uint8_t *input = output - offset;
	while (length) {
		*output = *input;
		output++;
		input++;
		length--;
	}
}

uint32_t LZ2KDecompressor::Decompress(uint8_t *output) {
	if (!bytesInBlock) {
		Initialize();
	}
	bytesInBlock--;
	const auto symbol = litDecoder.Decode(bs);
	if (symbol <= 255) {
		//std::cout << Formatted("Read literal: %02x", symbol) << std::endl;
		bytesDecoded++;
		*output = symbol;
		return 1u;
	} else {
		const auto repeatLength = symbol - 253;
		const auto offsetSymbol = offDecoder.Decode(bs);
		const auto offsetBase = (offsetSymbol > 0) ? (1u << (offsetSymbol - 1u)) : 0u;
		const auto offsetOffset = (offsetSymbol > 0) ? bs.Get(offsetSymbol - 1u) : 0u;
		const auto repeatOffset = offsetBase + offsetOffset + 1u;
		//std::cout << Formatted("Read LZ repeat of length %u, from offset %u (absolute %u, outputting to %u)", repeatLength, repeatOffset, bytesDecoded - repeatOffset, bytesDecoded) << std::endl;
		Repeat(output, repeatOffset, repeatLength);
		bytesDecoded += repeatLength;
		return repeatLength;
	}
}

}
