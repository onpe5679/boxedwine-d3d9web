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

#include "fszipopennode.h"
#include "fszipnode.h"
#include "fszip.h"
#include "kwaitframeprofile.h"

#include <fcntl.h>
#include UNISTD

FsZipOpenNode::FsZipOpenNode(std::shared_ptr<FsNode> node, std::shared_ptr<FsZipNode>& zipNode, U32 flags, U64 offset, U64 dataOffset, U32 compressionMethod, BString zipPath) : FsOpenNode(node, flags), zipNode(zipNode), pos(0), offset(offset), dataOffset(dataOffset), compressionMethod(compressionMethod) {
#ifdef BOXEDWINE_AZROK_HOT_WIS_CACHE
    if (compressionMethod != 0) {
        azrokRepeatedZipCache = zipNode->getAzrokRepeatedZipCache(node->path);
    }
#endif
    if (compressionMethod == 0 && dataOffset && zipPath.length()) {
        directHandle = ::open(zipPath.c_str(), O_RDONLY | O_BINARY);
    }
}

FsZipOpenNode::~FsZipOpenNode() {
    close();
}

S64 FsZipOpenNode::length() {
    return this->zipNode->length();
}

bool FsZipOpenNode::setLength(S64 len) {
    // if this file was open for write, it would have been copied to the file system
    kpanic("FsZipOpenNode::setLength should not have been called"); 
    return false;
}

S64 FsZipOpenNode::getFilePointer() {
    return this->pos;
}

S64 FsZipOpenNode::seek(S64 pos) {
    if (pos>(S64)this->length())
        this->pos = this->length();
    else
        this->pos = pos;
    return this->pos;
}

void FsZipOpenNode::close() {
#ifdef BOXEDWINE_AZROK_HOT_WIS_CACHE
    if (!azrokReadActivityReported && azrokPositiveReadBytes) {
        zipNode->noteAzrokReadActiveOpen(azrokPositiveReadBytes);
        azrokReadActivityReported = true;
    }
#endif
    if (directHandle != 0xFFFFFFFF) {
        ::close(directHandle);
        directHandle = 0xFFFFFFFF;
    }
}

bool FsZipOpenNode::isOpen() {
    return true;
}

U32 FsZipOpenNode::ioctl(KThread* thread, U32 request) {
    return -K_ENODEV;
}

void FsZipOpenNode::setAsync(bool isAsync) {
    if (isAsync)
        kdebug("FsZipOpenNode::setAsync not implemented");
}

bool FsZipOpenNode::isAsync() {
    return false;
}

void FsZipOpenNode::waitForEvents(BOXEDWINE_CONDITION& parentCondition, U32 events) {
    kdebug("FsZipOpenNode::waitForEvents not implemented");
}

bool FsZipOpenNode::isWriteReady() {
    return (this->flags & K_O_ACCMODE)!=K_O_RDONLY;
}

bool FsZipOpenNode::isReadReady() {
    return (this->flags & K_O_ACCMODE)!=K_O_WRONLY;
}

U32 FsZipOpenNode::map(KThread* thread, U32 address, U32 len, S32 prot, S32 flags, U64 off) {
    return 0;
}

bool FsZipOpenNode::canMap() {
    return true;
}

U32 FsZipOpenNode::readNative(U8* buffer, U32 len) {
#ifdef BOXEDWINE_AZROK_HOT_WIS_CACHE
#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
    const bool azrokCausalActive = azrokCausalWisActive();
    U64 azrokCacheLookupStart = 0;
    if (azrokCausalActive) azrokCacheLookupStart = azrokCausalProfileNow();
    const bool azrokCacheHit = (bool)azrokRepeatedZipCache;
    if (azrokCausalActive) {
        const U64 azrokCacheLookupEnd = azrokCausalProfileNow();
        azrokCausalWisRecordStage(K_AZROK_CAUSAL_WIS_CACHE_LOOKUP,
            azrokCacheLookupStart, azrokCacheLookupEnd);
        azrokCausalWisRecordCacheLookup(azrokCacheHit);
    }
    if (azrokCacheHit) {
#else
    if (azrokRepeatedZipCache) {
#endif
#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
        if (azrokCausalActive) {
            azrokCausalWisRecordBackend(K_AZROK_CAUSAL_WIS_BACKEND_CACHE);
        }
#endif
        if (this->pos < 0) {
            return (U32)-K_EINVAL;
        }
        if (this->pos >= (S64)azrokRepeatedZipCache->size()) {
            return 0;
        }
        const U64 available = azrokRepeatedZipCache->size() - (U64)this->pos;
        if (len > available) {
            len = (U32)available;
        }
        if (!len) {
            return 0;
        }
#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
        U64 azrokMemcpyStart = 0;
        if (azrokCausalActive) azrokMemcpyStart = azrokCausalProfileNow();
#endif
        memcpy(buffer, azrokRepeatedZipCache->data() + this->pos, len);
#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
        if (azrokCausalActive) {
            const U64 azrokMemcpyEnd = azrokCausalProfileNow();
            azrokCausalWisRecordStage(K_AZROK_CAUSAL_WIS_CACHE_MEMCPY,
                azrokMemcpyStart, azrokMemcpyEnd);
        }
#endif
        this->pos += len;
        return len;
    }
#endif
    if (directHandle != 0xFFFFFFFF) {
#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
        const bool azrokCausalActive = azrokCausalWisActive();
        if (azrokCausalActive) {
            azrokCausalWisRecordBackend(K_AZROK_CAUSAL_WIS_BACKEND_DIRECT);
        }
#endif
        if (this->pos >= this->length()) {
            return 0;
        }
        U64 available = this->length() - (U64)this->pos;
        if (len > available) {
            len = (U32)available;
        }
        if (!len) {
            return 0;
        }
        if (lseek64(directHandle, dataOffset + this->pos, SEEK_SET) < 0) {
            return 0;
        }
#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
        U64 azrokHostReadStart = 0;
        if (azrokCausalActive) azrokHostReadStart = azrokCausalProfileNow();
#endif
        S32 read = (S32)::read(directHandle, buffer, len);
#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
        if (azrokCausalActive) {
            const U64 azrokHostReadEnd = azrokCausalProfileNow();
            azrokCausalWisRecordStage(K_AZROK_CAUSAL_WIS_HOST_READ,
                azrokHostReadStart, azrokHostReadEnd);
        }
#endif
        if (read <= 0) {
            return 0;
        }
        U32 result = (U32)read;
        this->pos += result;
        return result;
    }

    U32 result = 0;
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(*getReadMutex());
    std::shared_ptr<FsZip> fsZip = zipNode->fsZip;
    if (fsZip) {
#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
        const bool azrokCausalActive = azrokCausalWisActive();
        if (azrokCausalActive) {
            azrokCausalWisRecordBackend(K_AZROK_CAUSAL_WIS_BACKEND_ZIP);
        }
        U64 azrokZipSetupStart = 0;
        if (azrokCausalActive) azrokZipSetupStart = azrokCausalProfileNow();
#endif
        fsZip->setupZipRead(this->offset, this->pos);
#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
        if (azrokCausalActive) {
            const U64 azrokZipSetupEnd = azrokCausalProfileNow();
            azrokCausalWisRecordStage(K_AZROK_CAUSAL_WIS_ZIP_SETUP,
                azrokZipSetupStart, azrokZipSetupEnd);
        }
        U64 azrokZipInflateStart = 0;
        if (azrokCausalActive) azrokZipInflateStart = azrokCausalProfileNow();
#endif
        result = unzReadCurrentFile(fsZip->zipfile, buffer, len);
#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
        if (azrokCausalActive) {
            const U64 azrokZipInflateEnd = azrokCausalProfileNow();
            azrokCausalWisRecordStage(K_AZROK_CAUSAL_WIS_ZIP_INFLATE,
                azrokZipInflateStart, azrokZipInflateEnd);
        }
#endif
        this->pos += result;
        fsZip->lastZipFileOffset = this->pos;
#ifdef BOXEDWINE_AZROK_HOT_WIS_CACHE
        if (result && azrokPositiveReadBytes != 0xFFFFFFFFFFFFFFFFULL) {
            const U64 remaining = 0xFFFFFFFFFFFFFFFFULL - azrokPositiveReadBytes;
            azrokPositiveReadBytes += std::min<U64>((U64)result, remaining);
        }
#endif
    }
    return result;
}

U32 FsZipOpenNode::writeNative(U8* buffer, U32 len) {
    kpanic("FsZipOpenNode::writeNative not implemented");
    return 0;
}

void FsZipOpenNode::reopen() {
    this->pos = 0;
}

BOXEDWINE_MUTEX* FsZipOpenNode::getReadMutex() {
#if defined(BOXEDWINE_AZROK_HOT_WIS_CACHE) && defined(BOXEDWINE_AZROK_CACHED_ZIP_UNLOCKED_READ)
    // KFile::filePosMutex still serializes this open node's private position.
    // The promoted cache is an immutable shared vector published completely
    // before it is retained by this open node, so it has no FsZip cursor state
    // to protect.  Every uncached/STORE/direct path keeps the archive mutex.
    if (azrokRepeatedZipCache) {
        return nullptr;
    }
#endif
    std::shared_ptr<FsZip> fsZip = zipNode->fsZip;
    if (fsZip) {
        return &fsZip->readMutex;
    }
    return nullptr;
}

#endif
