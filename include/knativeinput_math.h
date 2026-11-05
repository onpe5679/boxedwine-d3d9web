/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef __KNATIVEINPUT_MATH_H__
#define __KNATIVEINPUT_MATH_H__

#include "platformBoxedwine.h"

namespace KNativeInputMath {
    inline int convertCoordinate(int value, U32 sourceExtent, U32 targetExtent) {
        if (!sourceExtent || !targetExtent) {
            return value;
        }
        return (int)((S64)value * targetExtent / sourceExtent);
    }
}

#endif
