/*
 *  Copyright (C) 2012-2025  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef BOXEDWINE_ZLIB

#ifndef __FSZIPENODE_H__
#define __FSZIPNODE_H__

#include "fsnode.h"
#include "fszip.h"

#ifdef BOXEDWINE_AZROK_HOT_WIS_CACHE
#include <vector>
#endif

class FsFileNode;

class FsZipNode : public std::enable_shared_from_this<FsZipNode> {
public:
    FsZipNode(const fsZipInfo& zipInfo, const std::shared_ptr<FsZip>& fsZip);
#ifdef BOXEDWINE_AZROK_HOT_WIS_CACHE
    ~FsZipNode();
#endif
    U64 lastModified();
    U64 length();
    FsOpenNode* open(std::shared_ptr<FsNode> node, U32 flags);
    bool moveToFileSystem(std::shared_ptr<FsNode> node);

#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE
    U64 getAzrokFaceWisEntryOffset() const { return zipInfo.offset; }
    U64 getAzrokFaceWisCompressedLength() const { return zipInfo.compressedLength; }
    U32 getAzrokFaceWisCompressionMethod() const { return zipInfo.compressionMethod; }
    U32 getAzrokFaceWisCrc32() const { return zipInfo.crc32; }
#endif

#ifdef BOXEDWINE_AZROK_HOT_WIS_CACHE
    std::shared_ptr<std::vector<U8>> getAzrokRepeatedZipCache(const BString& path);
    void noteAzrokReadActiveOpen(U64 positiveReadBytes);
#endif

    std::shared_ptr<FsZip> fsZip;
private:
    fsZipInfo zipInfo;
#ifdef BOXEDWINE_AZROK_HOT_WIS_CACHE
    BOXEDWINE_MUTEX azrokZipCacheMutex;
    std::shared_ptr<std::vector<U8>> azrokZipCache;
    U32 azrokReadActiveOpenCount = 0;
    U64 azrokRepeatReadBytes = 0;
    bool azrokCacheBudgetReserved = false;
    bool azrokBudgetDenialLogged = false;
#endif
};
#endif
#endif
