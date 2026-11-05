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

#include "boxedwine.h"
#ifdef BOXEDWINE_ZLIB
#include "fszipnode.h"
#include "fsfilenode.h"
#include UNISTD
#include <fcntl.h>
#include "fszipopennode.h"
#include UTIME

#ifdef BOXEDWINE_AZROK_HOT_WIS_CACHE
namespace {
constexpr U64 AZROK_REPEATED_ZIP_CACHE_BUDGET = 32ULL * 1024ULL * 1024ULL;
constexpr U64 AZROK_REPEATED_ZIP_CACHE_MAX_ENTRY = 1ULL * 1024ULL * 1024ULL;
BOXEDWINE_MUTEX azrokRepeatedZipCacheBudgetMutex;
U64 azrokRepeatedZipCacheBytes = 0;
}
#endif

FsZipNode::FsZipNode(const fsZipInfo& zipInfo, const std::shared_ptr<FsZip>& fsZip) : fsZip(fsZip) {
    this->zipInfo = zipInfo;
}

#ifdef BOXEDWINE_AZROK_HOT_WIS_CACHE
FsZipNode::~FsZipNode() {
    if (azrokCacheBudgetReserved) {
        // No FsZipOpenNode can still exist once its shared FsZipNode owner is
        // being destroyed, so release the retained bytes before returning the
        // accounting reservation.
        azrokZipCache.reset();
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(azrokRepeatedZipCacheBudgetMutex);
        if (azrokRepeatedZipCacheBytes >= zipInfo.length) {
            azrokRepeatedZipCacheBytes -= zipInfo.length;
        } else {
            azrokRepeatedZipCacheBytes = 0;
        }
    }
}
#endif

bool FsZipNode::moveToFileSystem(std::shared_ptr<FsNode> node) {
    if (node->isDirectory())
        return false;
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
    // Invalidate HLE readers before any native overlay becomes visible.
    if (fsZip) fsZip->noteAzrokFaceWisMutation();
#endif
    if (node->isLink()) {
        U32 to = ::open((node->nativePath+EXT_LINK).c_str(), O_WRONLY | O_CREAT | O_BINARY, 0666);
        ::write(to, node->link.c_str(), node->link.length());
        ::close(to);
        return true;
    }

    FsOpenNode* from = this->open(node, K_O_RDONLY);
    bool result = false;
    U32 to = ::open(node->nativePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
    if (to != 0xFFFFFFFF) {
        U8 buffer[4096];
        U32 read = from->readNative(buffer, 4096);
        while (read) {
            if (::write(to, buffer, read) != (int)read)
                return false;
            read = from->readNative(buffer, 4096);
        }
        ::close(to);

        struct utimbuf settime = { 0, 0 };

        settime.actime = this->zipInfo.lastModified / 1000;
        settime.modtime = this->zipInfo.lastModified / 1000;
        utime(node->nativePath.c_str(), &settime);

        result = true;
#if defined(BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE) && \
        !defined(BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1)
        FsZip::noteAzrokFaceWisGlobalMutation();
#endif
    }
    from->close();
    return result;
}

U64 FsZipNode::lastModified() {
    return this->zipInfo.lastModified;
}

U64 FsZipNode::length() {
    return this->zipInfo.length;
}

FsOpenNode* FsZipNode::open(std::shared_ptr<FsNode> node, U32 flags) {
    std::shared_ptr<FsZipNode> zipNode = shared_from_this();
    std::shared_ptr<FsZip> fsZip = this->fsZip;
    BString zipPath;
    if (fsZip) {
        zipPath = fsZip->zipPath;
    }
    return new FsZipOpenNode(node, zipNode, flags, (U64)this->zipInfo.offset, this->zipInfo.dataOffset, this->zipInfo.compressionMethod, zipPath);
}

#ifdef BOXEDWINE_AZROK_HOT_WIS_CACHE
void FsZipNode::noteAzrokReadActiveOpen(U64 positiveReadBytes) {
    if (!positiveReadBytes) {
        return;
    }
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(azrokZipCacheMutex);
    if (azrokZipCache) {
        return;
    }
    if (azrokReadActiveOpenCount != 0xFFFFFFFF) {
        ++azrokReadActiveOpenCount;
    }
    if (azrokReadActiveOpenCount > 1) {
        const U64 threshold = std::max<U64>(64ULL * 1024ULL, zipInfo.length);
        if (azrokRepeatReadBytes < threshold) {
            const U64 remaining = threshold - azrokRepeatReadBytes;
            azrokRepeatReadBytes += std::min<U64>(positiveReadBytes, remaining);
        }
    }
}

std::shared_ptr<std::vector<U8>> FsZipNode::getAzrokRepeatedZipCache(const BString& path) {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(azrokZipCacheMutex);
    if (azrokZipCache) {
        return azrokZipCache;
    }
    // Promote only after distinct read-active opens have produced at least one
    // entry's worth of repeat traffic (and at least 64 KiB for tiny probes).
    // Counting opens alone made readless probes consume the cache.
    const U64 repeatThreshold = std::max<U64>(64ULL * 1024ULL, zipInfo.length);
    if (azrokReadActiveOpenCount < 2 || azrokRepeatReadBytes < repeatThreshold ||
            !fsZip || zipInfo.compressionMethod == 0 ||
            !zipInfo.length || zipInfo.length > AZROK_REPEATED_ZIP_CACHE_MAX_ENTRY) {
        return nullptr;
    }

    // Check before allocating, then reserve after allocation.  The second
    // check handles races without leaving a leaked reservation if allocation
    // itself fails.
    {
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(azrokRepeatedZipCacheBudgetMutex);
        if (azrokRepeatedZipCacheBytes + zipInfo.length > AZROK_REPEATED_ZIP_CACHE_BUDGET) {
#ifdef BOXEDWINE_AZROK_WAIT_FRAME_PROFILE
            if (!azrokBudgetDenialLogged) {
                klog_fmt("AZROK_ZIP_CACHE_DENY path=%s bytes=%llu used=%llu",
                    path.c_str(), (unsigned long long)zipInfo.length,
                    (unsigned long long)azrokRepeatedZipCacheBytes);
                azrokBudgetDenialLogged = true;
            }
#endif
            return nullptr;
        }
    }

    std::shared_ptr<std::vector<U8>> data =
        std::make_shared<std::vector<U8>>((size_t)zipInfo.length);
    U64 reservedBytes = 0;
    {
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(azrokRepeatedZipCacheBudgetMutex);
        if (azrokRepeatedZipCacheBytes + zipInfo.length > AZROK_REPEATED_ZIP_CACHE_BUDGET) {
#ifdef BOXEDWINE_AZROK_WAIT_FRAME_PROFILE
            if (!azrokBudgetDenialLogged) {
                klog_fmt("AZROK_ZIP_CACHE_DENY path=%s bytes=%llu used=%llu",
                    path.c_str(), (unsigned long long)zipInfo.length,
                    (unsigned long long)azrokRepeatedZipCacheBytes);
                azrokBudgetDenialLogged = true;
            }
#endif
            return nullptr;
        }
        azrokRepeatedZipCacheBytes += zipInfo.length;
        reservedBytes = azrokRepeatedZipCacheBytes;
        azrokCacheBudgetReserved = true;
    }
    U32 total = 0;
    {
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(fsZip->readMutex);
        fsZip->setupZipRead((U64)zipInfo.offset, 0);
        while (total < (U32)zipInfo.length) {
            const S32 read = (S32)unzReadCurrentFile(
                fsZip->zipfile, data->data() + total, (U32)zipInfo.length - total);
            if (read <= 0) {
                break;
            }
            total += (U32)read;
        }
        fsZip->lastZipFileOffset = total;
    }
    if (total != (U32)zipInfo.length) {
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(azrokRepeatedZipCacheBudgetMutex);
        azrokRepeatedZipCacheBytes -= zipInfo.length;
        azrokCacheBudgetReserved = false;
        return nullptr;
    }
    azrokZipCache = data;
#ifdef BOXEDWINE_AZROK_WAIT_FRAME_PROFILE
    klog_fmt("AZROK_ZIP_CACHE_ADMIT path=%s bytes=%llu used=%llu read_opens=%u repeat_bytes=%llu",
        path.c_str(), (unsigned long long)zipInfo.length,
        (unsigned long long)reservedBytes, azrokReadActiveOpenCount,
        (unsigned long long)azrokRepeatReadBytes);
#endif
    return azrokZipCache;
}
#endif

#endif
