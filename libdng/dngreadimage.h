/* This file is part of the dngconvert project
   Copyright (C) 2011 Jens Mueller <tschensensinger at gmx dot de>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#pragma once

#include <dng_read_image.h>

class DngReadImage : public dng_read_image
{
public:
    DngReadImage(void);
    ~DngReadImage(void);

protected:
    virtual bool ReadBaselineJPEG (dng_host &host, const dng_ifd &ifd, dng_stream &stream,
                                   dng_image &image, const dng_rect &tileArea, uint32 plane, uint32 planes, uint32 tileByteCount);
};
