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
#include <cstring>
#include <fstream>
#include <iostream>
#include <lz2k/LZ2K.h>

static std::vector<uint8_t> DecompressFile(const std::vector<uint8_t>& input) {
	std::vector<uint8_t> output;
	unsigned int inputOffset = 0u;

	while (inputOffset < input.size()) {
		const auto* inputData = &input.data()[inputOffset];
		if ((input.size() - inputOffset) < 12) {
			throw std::runtime_error("File does not contain an LZ2K header");
		} else if (memcmp(inputData, "LZ2K", 4)) {
			throw std::runtime_error("File does not contain an LZ2K header");
		}
		const auto outputSize = *(uint32_t*)(inputData + 4);
		const auto inputSize = *(uint32_t*)(inputData + 8);
		const auto outputOffset = output.size();
		output.resize(outputOffset + outputSize);
		auto* outputData = &output.data()[outputOffset];
		LZ2K::LZ2KBitstream bs(inputData + 12, inputSize);
		//std::cout << Formatted("Compressed block is %u, should decompress to %u", inputSize, outputSize) << std::endl;
		LZ2K::LZ2KDecompressor decomp(bs);
		unsigned int bytesDecoded = 0u;
		while (bytesDecoded < outputSize) {
			bytesDecoded += decomp.Decompress(&outputData[bytesDecoded]);
		}
		//std::cout << Formatted("Decompressed %u bytes (expected %u)", bytesDecoded, outputSize) << std::endl;
		inputOffset += 12 + inputSize;
	}

	return output;
}

int main(int argc, char *argv[]) {
	if (argc < 3) {
		std::cerr << "Usage: lz2kdemo <input filename> <output filename>" << std::endl;
		return 0;
	}
	std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
	if (!file.is_open()) {
		std::cerr << "Can't read from input file" << std::endl;
		return -1;
	}
	const auto size = file.tellg();
	file.seekg(0);
	const auto bytes = std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
	file.close();

	const auto data = DecompressFile(bytes);

	std::ofstream outFile(argv[2], std::ios::binary);
	if (!outFile.is_open()) {
		std::cerr << "Can't write to output file" << std::endl;
		return -1;
	}
	outFile.write((const char*)data.data(), data.size());
	outFile.close();

	return 0;
}
