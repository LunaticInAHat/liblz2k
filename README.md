# The LZ2K Compression Scheme #

LZ2K is a data compression scheme, commonly used by Traveler's Tales in various LEGO-branded videogames. It is unclear whether LZ2K was developed in-house at TT Games, or externally.

This document attempts to capture a description of the LZ2K compression scheme. It is primarily focused on the decompression side of the scheme, however presumably readers could infer how to create a compressor, from the descriptions provided here.

The high-level architecture of LZ2K is comprised of three distinct functional units, in a layered relationship with one another. In order from the bottom layer to the top, they are:
 - A bitstream parser
 - A Huffman-style prefix coder
 - An LZSS-style dictionary compressor

Each of these units will be detailed in the following sections, along with a general description of how the scheme is employed. Superficially, the LZ2K scheme is rather similar to DEFLATE; the biggest point of distinction between LZ2K and DEFLATE is likely the relatively small (8KiB) LZ window which LZ2K employs. This may have been a choice driven by a need to minimize resource consumption, or may have instead been a reflection of the characteristics of the data it was expected to compress, where repeats from the distance past are not expected (or could not be encoded profitably, compared to shorter repeats in more-recent history).


## Bitstream Layer ##

The bitstream layer serves to ingest compressed data, and provide it to the prefix-coding layer. The bitstream layer supports only a single operation (`BSGet(N)`), which retrieves the next N-bits of input data. Each byte of input is consumed, starting from its most-significant bit. If all input data has already been consumed, then the bitstream layer produces zeroes.

When bits retrieved from the bitstream are interpreted as integers, they are interpreted in big-endian order.


## Symbol-Coding Layer ##

The Symbol-Coding layer is responsible for taking in bits from the bitstream layer, and providing decoded symbols to the LZ layer. Fundamentally, three separate symbol decoders are used by the algorithm, depending upon context:
 - Literal & LZ Length
 - LZ Repeat Offset
 - Literal & Length Code-Length

Each decoder is a fairly standard, non-adaptive Huffman-style decoder. The "Literal & Length Code-Length" decoder (henceforth abbreviated as "Code-Length decoder") is only used during the initialization process of each block of data (refer to the LZSS section for details on the blocking scheme), to encode the lengths of each Literal & LZ Length symbol's code. Code lengths for the Code-Length dictionary and the LZ Repeat Offset dictionary are not prefix-coded, and are instead encoding use a hybrid binary-unary scheme, which will be described in detail below.

The major operations of a symbol coder are:
 - InitializeHybrid
 - InitializeCoded
 - GenerateCodes
 - Lookup
 - Decode

The `InitializeHybrid` operation is used for reading in code lengths that have been encoded in the style used for the Code-Length and LZ Repeat Offset dictionaries, and generating a prefix code from them. The `InitializeCoded` operation is used for reading in code lengths that have been encoded with a prefix code (the Code-Length code), and generating a prefix code.

The `GenerateCodes` operation is used by both `InitializeHybrid` and `InitializeCoded` to generate a prefix code, once code lengths have been read in.


### InitializeHybrid ###

The `InitializeHybrid` operation prepares the coder for use with either the Code-Length dictionary or the LZ Repeat Offset dictionary.

```
def SCInitializeHybrid(p_AS, p_L2AS, p_S1C):
    AS = p_AS
    resize(CL, AS)
    resize(CD, AS)
    fill(CL, 0)
    US = BSGet(p_L2AS)
    if US == 0:                         # Single-Symbol Mode
        US = 1
        SSYM = BSGet(p_L2AS)
    else:                               # Multi-Symbol Mode
        SSYM = -1
        sid = 0
        while sid < US:
            cl = BSGet(3)
            if cl == 7:                 # Long Code Length
                while BSGet(1):
                    cl += 1
            CL[sid] = cl
            sid += 1
            if sid == p_S1C:            # Segment 1 -> Segment 2
                skip = BSGet(2)
                sid += skip
        SCGenerateCodes()
```

The `p_AS` parameter is the absolute size of the alphabet being decoded. This is the number of unique symbols that could be decoded, without regard for how many are expected to occur. This is stored in the `AS` member variable for later use, when decoding symbols. For the Code Length dictionary, the alphabet size is 19. For the LZ Repeat Offset dictionary, the alphabet size is 14.

The `p_L2AS` parameter is the base-2 logarithm of the size of the alphabet. This determines how many bits need to be read from the bitstream, to resolve the size of the code length table.

The `p_S1C` parameter is the number of symbols which are present in "Segment 1" of the code length table. This parameter is only relevant when reading in the Code-Length dictionary. The first three symbols of that dictionary will used for encoding runs of consecutive 0-length codes, in the Literal & LZ Length dictionary, with the remaining symbols being used for encoding codes with length 1 - 16 bits. However, it is uncommon for the input symbol distribution to be so lopsided that any symbol is given a 1-bit or 2-bit code (or, frequently, even 3- or 4-bits). Thus, as a measure to save space in the Code Length dictionary's length table, once the first 3 code lengths have been decoded, a 2-bit number is then decoded ("Segment 1 -> Segment 2"). This number represents a number of 0-bit code lengths which follow. This allows entries 3-6 of the code length table (which would correspond to 1- to 4-bit code lengths when used in `SCInitializeCoded`) to be skipped using only 2 bits, rather than the 3 to 12 which would be required to store those lengths directly.

The `US` member variable is one greater than the highest-valued symbol that is expected to occur, in the course of decompression. This determines the number of entries which will be present in the bitstream's code length table.

The `SSYM` member variable is the index of the only symbol which occurs in the compressed data, or -1 when multiple symbols occur. The precise motivation for providing a mode dedicated to decompressing data which was all a single value is unclear; perhaps this caused degenerate behavior in the compressor's Huffman generation algorithm, or perhaps large blocks of single values were expected to be common enough that it was deemed worthwhile to be able to decode them in 0 bits-per-symbol, rather than 1. The function of this variable will become more clear, in the description of `SCDecode`.

The `CL` member variable is an array of code lengths for each symbol. Values can range from 0 to 16. Symbols with a code length of 0 do not occur, and are not assigned prefix codes. Code lengths are stored in a hybrid binary-unary scheme; a 3-bit binary number is initially read as the code length. If that value is 7, then the further length of the symbol's code is encoded in unary (a sequence of 1-bits, which each increment the length, terminated by a 0-bit).

The `CD` member variable is an array of code values which have been assigned to each symbol. Values can be up to 16 bits long.


### InitializeCoded ###

The `InitializeCoded` operation prepares the coder for use with the Literal & LZ Length dictionary. It relies upon a Code-Length decoder already having been created and initialized.

```
def SCInitializeCoded(p_AS, p_L2AS, p_DEC):
    AS = p_AS
    resize(CL, AS)
    resize(CD, AS)
    fill(CL, 0)
    US = BSGet(p_L2AS)
    if US == 0:                         # Single-Symbol Mode
        US = 1
        SSYM = BSGet(p_L2AS)
    else:                               # Multi-Symbol Mode
        SSYM = -1
        sid = 0
        while sid < US:
            sym = p_DEC.Decode()
            if sym == 0:
                CL[sid] = 0
                sid += 1
            elif sym == 1:
                rl = 3 + BSGet(4)
                sid += rl
            elif sym == 2:
                rl = 20 + BSGet(9)
                sid += rl
            else:
                CL[sid] = sym - 2
                sid += 1
        SCGenerateCodes()
```

The `p_AS` parameter is as described in `InitializeHybrid`. The alphabet size is always 510.

The `p_L2AS` parameter is as described in `InitializeHybrid`. This value is always 9.

The `p_DEC` parameter is an already-initialized symbol coder, for the Code Length dictionary.

The `US` member variable is as described in `InitializeHybrid`.

The `SSYM` member variable is as described in `InitializeHybrid`.

The `CL` member variable is an array of code lengths for each symbol. Symbols with a code length of 0 do not occur, and are not assigned prefix codes. Code lengths are stored, encoded using the Code Length prefix code. Symbol 0 of the Code Length dictionary represents a single 0-length code. Symbol 1 of the Code Length dictionary represents a short run of 0-length codes, with the run being at least 3 codes long, with a 4-bit value present in the bitstream to describe the additional length of the run. Symbol 2 of the Code Length dictionary represents a long run of 0-length codes, with the run being at least 20 codes long, with a 9-bit value to describe the additional length of the run. The remaining symbols correspond to the values 1-16.


### GenerateCodes ###

The `GenerateCodes` operation generates a prefix code, corresponding to the code lengths which have already been read in by one of the initialization operations.

```
def SCGenerateCodes():
    resize(lc, 17)
    fill(lc, 0)
    for cl in CL:                       # Count number of symbols at each code length
        lc[cl] += 1
    next = 0
    for cl in 1..16:                    # Assign codes at each length
        span = (1 << (16 - cl))
        for sid in 0..(AS-1):           # Assign codes to each symbol of length
            if CL[sid] == cl:
                CD[sid] = (next >> (16 - cl))
                next += span
```

The `lc` local variable is an array which contains the number of symbols that occur at each code length.

The `next` local variable is an accumulator which steps through the coding space.

The `span` local variable is the amount of coding space "captured" by a single code of a given length.


### Lookup ###

The `Lookup` operation resolves a bitstring into a symbol, using the prefix code. If the code does not yet resolve to any individual symbol, -1 is returned.

```
def SCLookup(code, len):
    if SSYM != -1:
        return SSYM
    for sid in 0..(AS-1):
        if (len == CL[sid]) && (code == CD[sid]):
            return sid
    return -1
```


### Decode ###

At last, we can describe the `Decode` operation. This operation reads bits from the bitstream, until a valid symbol has been decoded.

```
def SCDecode():
    if SSYM != -1:
        return SSYM
    len = 0
    code = 0
    while len < 16:
        code = (code << 1) | BSGet(1)
        len += 1
        sid = SCLookup(code, len)
        if sid != -1:
            return sid
    return -1
```

Here, we can see the functionality of the `SSYM` member variable, and "single-symbol mode": In this mode, the only possible symbol is decoded, without reading any bits from the bitstream.

In (the much more typical) multi-symbol mode, if the compressor generated its prefix code using the Huffman algorithm, or some similar algorithm, every portion of the coding space will be assigned to some symbol (there will be no unused codes), and it will not be possible for this operation to return -1. If the compressor used a less advanced algorithm for generating its code lengths, then some codes may not be assigned to any symbol, and a corrupted bitstream may result in failing to decode a symbol. This operation will return -1, however we do not give any further treatment to such erroneous cases.


## LZ Layer ##

The LZ layer is a very classic implementation of LZSS. The decompressor reads symbols from the symbol-coding layer, and constructs the symbols that it reads into either:
 - A "repeat" code, which specifies a length & offset of already-decompressed data to replay into the output stream, or
 - A "literal" code, which provides a raw byte value to be emitted into the output stream

The major operations of the LZ layer are:
 - Initialize
 - Decode

The major interfaces of the LZ layer are:
 - Decode symbols, via the Symbol-Coder layer
 - Emit decompressed data bytes

As an LZ-style dictionary decompressor, the LZ layer has the concept of a "window", which bounds the amount of previously-decompressed data that can be referenced by "repeat" codes. The size of the LZ2K window is fixed at 8KiB.


### Initialize ###

The `Initialize` operation consumes the LZ2K block header, and initializes the dictionaries which will be used for decoding. Note that the `Initialize` operation is not invoked by external code; instead, it will be invoked by the `Decode` operation, whenever applicable.

```
def LZInitialize():
    BIB = BSGet(16)
    CL.Initialize(19, 5, 3)
    LIT.Initialize(510, 9, CL)
    OFF.Initialize(14, 4, -1)
```

The `BIB` member variable is the number of bytes of data in the block.

The `CL` member variable is a symbol-coder for the Code Length dictionary. It is not used again, once the Literal & LZ Length dictionary has been initialized.

The `LIT` member variable is a symbol-coder for the Literal & LZ Length dictionary.

The `OFF` member variable is a symbol-coder for the LZ Repeat Offset dictionary.



### Decode ###

The `Decode` operation decodes a single symbol from the bitstream, and uses it to generate one or more bytes of decompressed data.

```
def LZDecode():
    if BIB == 0:
        LZInitialize()
    BIB -= 1
    sym = LIT.Decode()
    if sym <= 255:
        LZEmitLiteral(sym)
    else:
        rl = sym - 253
        osym = OFF.Decode()
        if osym == 0:
            o = 1
        else:
            o = 1 + (1 << (osym - 1)) + BSGet(osym - 1)
            LZEmitRepeat(rl, o)
```
The `sym` local variable is the next symbol decoded from the bitstream, using the Literal & LZ Length symbol coder.

The `rl` local variable is the length of the repeat. Note that the highest-valued symbol in the Literal & LZ Length alphabet is 509, and the first 256 symbols correspond to literal byte values. Additionally, repeats shorter than length 3 are not considered and cannot be represented. Thus, the longest repeat that can be represented is 509 - 253 = 256 bytes.

The `osym` local variable is a symbol decoded from the bitstream, using the LZ Repeat Offset symbol coder. Symbol 0 represents a repeat from offset -1 (i.e., the previous byte of decompressed data). Higher-valued symbols are accompanied by an `osym-1`-bit integer encoded directly into the bitstream, which represents an offset from the power-of-2 implied by the symbol number. The highest-valued symbol in the LZ Repeat Offset alphabet is 13, which has an implicit base offset of 4096 bytes, with a 12-bit offset, which allows encoding offsets up to 8191.

The `o` local variable is the offset from which to repeat previously-decompressed data.

The `LZEmitLiteral` pseudo-operation appends a single byte to the stream of decompressed data.

The `LZEmitRepeat` pseudo-operation replays a string of bytes from the already-decompressed data, into the output stream.



## The LZ2K Header ##

The LZ2K header is 12 bytes of metadata that are frequently provided alongside the data to be decompressed (although it is not required that it be prepended directly onto it, it often is); this metadata conveys the length of the compressed and decompressed data.

```
     3                   2                   1
   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  |      'L'      |      'Z'      |      '2'      |      'K'      |
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  |                      Uncompressed Size                        |
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  |                       Compressed Size                         |
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

Use of this header is conventional in cases where we have seen LZ2K used, but is not mandatory. The surrounding code must know when to stop attempting to invoke `LZDecode`, as there is no End-of-Stream symbol, and this is most conveniently achieved by knowing the amount of data to expect to decompress, however there are other ways this information could be conveyed.


## Decompressing Data ##

Interfacing with the LZ layer to decompress LZ2K data is trivial: Invoke the LZ layer's `Decode` operation, until the expected amount of data has been decompressed. Note that it is an expectation of the scheme, that the total uncompressed size of the data will be communicated to the decompressor, out-of-band (see above about the LZ2K header that is often used); there is no End-of-Stream symbol.


## Reference Implementation ##

Included alongside this document is a basic implementation of an LZ2K decoder. It forgoes any attempt at optimization, in the interest of being compact and readable.
