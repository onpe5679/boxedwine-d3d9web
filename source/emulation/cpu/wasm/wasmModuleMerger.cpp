/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "boxedwine.h"
#ifdef BOXEDWINE_WASM_JIT
#include "wasmModuleMerger.h"

namespace {
struct FunctionShape {
    std::vector<U8> params;
    std::vector<U8> results;

    bool operator==(const FunctionShape& other) const {
        return params == other.params && results == other.results;
    }
};

struct ParsedModule {
    std::vector<U8> typePayload;
    std::vector<FunctionShape> types;
    std::vector<U8> importPayload;
    U32 importedFunctions = 0;
    std::vector<U32> functionTypes;
    // Each body includes its ULEB byte-size prefix.
    std::vector<std::vector<U8>> codeBodies;
};

bool readUleb(const std::vector<U8>& bytes, size_t& pos, size_t end, U32& value) {
    value = 0;
    for (U32 shift = 0; shift < 35; shift += 7) {
        if (pos >= end) {
            return false;
        }
        U8 byte = bytes[pos++];
        if (shift == 28 && (byte & 0xf0)) {
            return false;
        }
        value |= (U32)(byte & 0x7f) << shift;
        if (!(byte & 0x80)) {
            return true;
        }
    }
    return false;
}

void appendUleb(std::vector<U8>& out, U64 value) {
    do {
        U8 byte = (U8)(value & 0x7f);
        value >>= 7;
        out.push_back(value ? (U8)(byte | 0x80) : byte);
    } while (value);
}

void appendSection(std::vector<U8>& out, U8 id, const std::vector<U8>& payload) {
    out.push_back(id);
    appendUleb(out, payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
}

bool skipName(const std::vector<U8>& bytes, size_t& pos, size_t end) {
    U32 length = 0;
    return readUleb(bytes, pos, end, length) && length <= end - pos ? (pos += length, true) : false;
}

bool skipLimits(const std::vector<U8>& bytes, size_t& pos, size_t end) {
    if (pos >= end) {
        return false;
    }
    U8 flags = bytes[pos++];
    U32 ignored = 0;
    if (!readUleb(bytes, pos, end, ignored)) {
        return false;
    }
    return !(flags & 1) || readUleb(bytes, pos, end, ignored);
}

bool parseTypeSection(const std::vector<U8>& bytes, size_t pos, size_t end,
                      std::vector<FunctionShape>& types) {
    U32 count = 0;
    if (!readUleb(bytes, pos, end, count)) {
        return false;
    }
    types.clear();
    types.reserve(count);
    for (U32 i = 0; i < count; ++i) {
        if (pos >= end || bytes[pos++] != 0x60) {
            return false;
        }
        FunctionShape shape;
        U32 paramCount = 0;
        if (!readUleb(bytes, pos, end, paramCount) || paramCount > end - pos) {
            return false;
        }
        shape.params.assign(bytes.begin() + pos, bytes.begin() + pos + paramCount);
        pos += paramCount;
        U32 resultCount = 0;
        if (!readUleb(bytes, pos, end, resultCount) || resultCount > end - pos) {
            return false;
        }
        shape.results.assign(bytes.begin() + pos, bytes.begin() + pos + resultCount);
        pos += resultCount;
        types.push_back(std::move(shape));
    }
    return pos == end;
}

bool isI32Void(const FunctionShape& shape, U32 paramCount) {
    return shape.results.empty() &&
        shape.params.size() == paramCount &&
        std::all_of(shape.params.begin(), shape.params.end(),
                    [](U8 type) { return type == 0x7f; });
}

bool isI32Tuple(const FunctionShape& shape, U32 paramCount, U32 resultCount) {
    return shape.params.size() == paramCount &&
        shape.results.size() == resultCount &&
        std::all_of(shape.params.begin(), shape.params.end(),
                    [](U8 type) { return type == 0x7f; }) &&
        std::all_of(shape.results.begin(), shape.results.end(),
                    [](U8 type) { return type == 0x7f; });
}

bool rewriteSingleCallIndex(const std::vector<U8>& body, U32 oldIndex,
                            U32 newIndex, std::vector<U8>& rewritten) {
    size_t payloadStart = 0;
    U32 bodySize = 0;
    if (!readUleb(body, payloadStart, body.size(), bodySize) ||
            bodySize != body.size() - payloadStart) {
        return false;
    }
    size_t matchStart = 0;
    size_t matchEnd = 0;
    U32 matches = 0;
    for (size_t pos = payloadStart; pos < body.size(); ++pos) {
        if (body[pos] != 0x10) {
            continue;
        }
        size_t q = pos + 1;
        U32 index = 0;
        if (readUleb(body, q, body.size(), index) && index == oldIndex) {
            matchStart = pos;
            matchEnd = q;
            ++matches;
        }
    }
    if (matches != 1) {
        return false;
    }
    std::vector<U8> payload;
    payload.insert(payload.end(), body.begin() + payloadStart,
                   body.begin() + matchStart);
    payload.push_back(0x10);
    appendUleb(payload, newIndex);
    payload.insert(payload.end(), body.begin() + matchEnd, body.end());
    rewritten.clear();
    appendUleb(rewritten, payload.size());
    rewritten.insert(rewritten.end(), payload.begin(), payload.end());
    return true;
}

bool parseModule(const std::vector<U8>& bytes, ParsedModule& parsed, BString& error) {
    auto fail = [&error](const char* message) {
        error = B(message);
        return false;
    };

    static const U8 header[] = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    if (bytes.size() < sizeof(header) || !std::equal(header, header + sizeof(header), bytes.begin())) {
        return fail("invalid wasm module header");
    }

    bool hasType = false;
    bool hasImport = false;
    bool hasFunction = false;
    bool hasCode = false;
    size_t pos = sizeof(header);
    while (pos < bytes.size()) {
        U8 id = bytes[pos++];
        U32 sectionSize = 0;
        if (!readUleb(bytes, pos, bytes.size(), sectionSize)) {
            return fail("bad wasm section size");
        }
        if (sectionSize > bytes.size() - pos) {
            return fail("wasm section exceeds module bounds");
        }
        size_t sectionEnd = pos + sectionSize;

        if (id == 1) {
            if (hasType) {
                return fail("duplicate wasm type section");
            }
            parsed.typePayload.assign(bytes.begin() + pos, bytes.begin() + sectionEnd);
            if (!parseTypeSection(bytes, pos, sectionEnd, parsed.types)) {
                return fail("bad wasm type section");
            }
            hasType = true;
        } else if (id == 2) {
            if (hasImport) {
                return fail("duplicate wasm import section");
            }
            parsed.importPayload.assign(bytes.begin() + pos, bytes.begin() + sectionEnd);
            size_t q = pos;
            U32 importCount = 0;
            if (!readUleb(bytes, q, sectionEnd, importCount)) {
                return fail("bad import count");
            }
            for (U32 i = 0; i < importCount; ++i) {
                if (!skipName(bytes, q, sectionEnd) || !skipName(bytes, q, sectionEnd) || q >= sectionEnd) {
                    return fail("bad import name");
                }
                U8 kind = bytes[q++];
                U32 ignored = 0;
                switch (kind) {
                case 0: // function
                    if (!readUleb(bytes, q, sectionEnd, ignored)) {
                        return fail("bad function import");
                    }
                    parsed.importedFunctions++;
                    break;
                case 1: // table
                    if (q >= sectionEnd) {
                        return fail("bad table import");
                    }
                    q++; // reference type
                    if (!skipLimits(bytes, q, sectionEnd)) {
                        return fail("bad table limits");
                    }
                    break;
                case 2: // memory
                    if (!skipLimits(bytes, q, sectionEnd)) {
                        return fail("bad memory limits");
                    }
                    break;
                case 3: // global
                    if (sectionEnd - q < 2) {
                        return fail("bad global import");
                    }
                    q += 2;
                    break;
                default:
                    return fail("unsupported import kind");
                }
            }
            if (q != sectionEnd) {
                return fail("trailing bytes in import section");
            }
            hasImport = true;
        } else if (id == 3) {
            if (hasFunction) {
                return fail("duplicate wasm function section");
            }
            size_t q = pos;
            U32 functionCount = 0;
            if (!readUleb(bytes, q, sectionEnd, functionCount) ||
                    (functionCount != 1 && functionCount != 2)) {
                return fail("wasm module must contain one function or one dual-entry pair");
            }
            parsed.functionTypes.resize(functionCount);
            for (U32 i = 0; i < functionCount; ++i) {
                if (!readUleb(bytes, q, sectionEnd, parsed.functionTypes[i])) {
                    return fail("bad wasm function type");
                }
            }
            if (q != sectionEnd) {
                return fail("trailing bytes in wasm function section");
            }
            hasFunction = true;
        } else if (id == 10) {
            if (hasCode) {
                return fail("duplicate wasm code section");
            }
            size_t q = pos;
            U32 bodyCount = 0;
            if (!readUleb(bytes, q, sectionEnd, bodyCount) ||
                    (bodyCount != 1 && bodyCount != 2)) {
                return fail("wasm module must contain one code body or one dual-entry pair");
            }
            parsed.codeBodies.clear();
            parsed.codeBodies.reserve(bodyCount);
            for (U32 i = 0; i < bodyCount; ++i) {
                size_t bodyStart = q;
                U32 bodySize = 0;
                if (!readUleb(bytes, q, sectionEnd, bodySize)) {
                    return fail("bad wasm code body size");
                }
                if (bodySize > sectionEnd - q) {
                    return fail("wasm code body exceeds section bounds");
                }
                q += bodySize;
                parsed.codeBodies.emplace_back(
                    bytes.begin() + bodyStart, bytes.begin() + q);
            }
            if (q != sectionEnd) {
                return fail("trailing bytes in wasm code section");
            }
            hasCode = true;
        }
        pos = sectionEnd;
    }

    if (!hasType) {
        return fail("missing wasm type section");
    }
    if (!hasImport) {
        return fail("missing wasm import section");
    }
    if (!hasFunction) {
        return fail("missing wasm function section");
    }
    if (!hasCode) {
        return fail("missing wasm code section");
    }
    if (parsed.functionTypes.size() != parsed.codeBodies.size()) {
        return fail("wasm function/code count mismatch");
    }
    for (U32 type : parsed.functionTypes) {
        if (type >= parsed.types.size()) {
            return fail("wasm local function type index out of bounds");
        }
    }
    if (!isI32Void(parsed.types[parsed.functionTypes[0]], 2)) {
        return fail("wasm execute wrapper must have (i32, i32) -> void ABI");
    }
    if (parsed.functionTypes.size() == 2 &&
        !isI32Tuple(parsed.types[parsed.functionTypes[1]], 10, 8)) {
        return fail("wasm threaded body must have ten i32 parameters and eight i32 results");
    }
    return true;
}
}

bool wasmJitMergeModules(const std::vector<WasmJitMergeInput>& inputs, std::vector<U8>& output, BString& error) {
    output.clear();
    error = B("");
    if (inputs.empty()) {
        error = B("no wasm modules to merge");
        return false;
    }

    std::vector<ParsedModule> parsed(inputs.size());
    for (size_t i = 0; i < inputs.size(); ++i) {
        if (!inputs[i].bytes) {
            error = B("null wasm module input");
            return false;
        }
        if (!parseModule(*inputs[i].bytes, parsed[i], error)) {
            return false;
        }
        if (i && parsed[i].typePayload != parsed[0].typePayload) {
            error = B("wasm module type section mismatch");
            return false;
        }
        if (i && parsed[i].importPayload != parsed[0].importPayload) {
            error = B("wasm module import section mismatch");
            return false;
        }
        if (i && parsed[i].functionTypes != parsed[0].functionTypes) {
            error = B("wasm module local function shape mismatch");
            return false;
        }
    }

    const ParsedModule& first = parsed[0];
    const U32 functionsPerInput = (U32)first.functionTypes.size();
    std::vector<U8> functionPayload;
    appendUleb(functionPayload, parsed.size() * functionsPerInput);
    for (const ParsedModule& module : parsed) {
        for (U32 type : module.functionTypes) {
            appendUleb(functionPayload, type);
        }
    }

    std::vector<U8> exportPayload;
    appendUleb(exportPayload, parsed.size() * functionsPerInput);
    for (size_t i = 0; i < parsed.size(); ++i) {
        std::string name = "b" + std::to_string(i);
        appendUleb(exportPayload, name.size());
        exportPayload.insert(exportPayload.end(), name.begin(), name.end());
        exportPayload.push_back(0);
        appendUleb(exportPayload,
                   (U64)first.importedFunctions + i * functionsPerInput);
        if (functionsPerInput == 2) {
            name = "t" + std::to_string(i);
            appendUleb(exportPayload, name.size());
            exportPayload.insert(exportPayload.end(), name.begin(), name.end());
            exportPayload.push_back(0);
            appendUleb(exportPayload,
                       (U64)first.importedFunctions + i * functionsPerInput + 1);
        }
    }

    std::vector<U8> codePayload;
    appendUleb(codePayload, parsed.size() * functionsPerInput);
    for (size_t i = 0; i < parsed.size(); ++i) {
        const ParsedModule& module = parsed[i];
        for (U32 bodyIndex = 0; bodyIndex < functionsPerInput; ++bodyIndex) {
            const std::vector<U8>* body = &module.codeBodies[bodyIndex];
            std::vector<U8> rewrittenWrapper;
            if (functionsPerInput == 2 && bodyIndex == 0) {
                const U32 originalInternal = first.importedFunctions + 1;
                const U32 mergedInternal =
                    first.importedFunctions + (U32)i * functionsPerInput + 1;
                if (!rewriteSingleCallIndex(*body, originalInternal,
                                            mergedInternal, rewrittenWrapper)) {
                    error = B("wasm dual-entry wrapper internal call relocation failed");
                    output.clear();
                    return false;
                }
                body = &rewrittenWrapper;
            }
            codePayload.insert(codePayload.end(), body->begin(), body->end());
        }
    }

    output = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    appendSection(output, 1, first.typePayload);
    appendSection(output, 2, first.importPayload);
    appendSection(output, 3, functionPayload);
    appendSection(output, 7, exportPayload);
    appendSection(output, 10, codePayload);
    return true;
}
#endif
