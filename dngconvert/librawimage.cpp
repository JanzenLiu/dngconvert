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

#include "librawimage.h"

#include "dng_memory.h"

#include "libraw/libraw.h"

LibRawImage::LibRawImage(const char *filename, dng_memory_allocator &allocator)
    :	dng_image(dng_rect(0, 0), 0, ttShort),
      m_Memory(),
      m_Buffer(),
      m_Allocator(allocator)
{
    AutoPtr<LibRaw> fRawProcessor(new LibRaw());

    int ret = fRawProcessor->open_file(filename);
    if (ret != LIBRAW_SUCCESS)
    {
        printf("Cannot open %s: %s\n", filename, libraw_strerror(ret));
        fRawProcessor->recycle();
        return;
    }

    ret = fRawProcessor->adjust_sizes_info_only();
    if (ret != LIBRAW_SUCCESS)
    {
        printf("LibRaw: failed to run adjust_sizes_info_only: %s", libraw_strerror(ret));
        fRawProcessor->recycle();
        return;
    }

    if ((fRawProcessor->imgdata.sizes.flip == 5) || (fRawProcessor->imgdata.sizes.flip == 6))
    {
        m_FinalSize = dng_rect(fRawProcessor->imgdata.sizes.iwidth, fRawProcessor->imgdata.sizes.iheight);
    }
    else
    {
        m_FinalSize = dng_rect(fRawProcessor->imgdata.sizes.iheight, fRawProcessor->imgdata.sizes.iwidth);
    }

    fRawProcessor->recycle();

    ret = fRawProcessor->open_file(filename);
    if (ret != LIBRAW_SUCCESS)
    {
        printf("Cannot open %s: %s\n", filename, libraw_strerror(ret));
        fRawProcessor->recycle();
        return;
    }

    fRawProcessor->imgdata.params.output_bps = 16;
    fRawProcessor->imgdata.params.document_mode = 2;
    fRawProcessor->imgdata.params.shot_select = 0;

    ret = fRawProcessor->unpack();
    if (ret != LIBRAW_SUCCESS)
    {
        printf("LibRaw: failed to run unpack: %s", libraw_strerror(ret));
        fRawProcessor->recycle();
        return;
    }

    if (0 == strcmp(fRawProcessor->imgdata.idata.make, "Canon") && (fRawProcessor->imgdata.idata.filters != 0))
    {
        ret = fRawProcessor->add_masked_borders_to_bitmap();
        if (ret != LIBRAW_SUCCESS)
        {
            printf("LibRaw: failed to run add_masked_borders_to_bitmap: %s", libraw_strerror(ret));
            fRawProcessor->recycle();
            return;
        }

        m_ActiveArea = dng_rect(fRawProcessor->imgdata.sizes.top_margin,
                                fRawProcessor->imgdata.sizes.left_margin,
                                fRawProcessor->imgdata.sizes.iheight - m_Imgdata.sizes.bottom_margin,
                                fRawProcessor->imgdata.sizes.iwidth - m_Imgdata.sizes.right_margin);
    }
    else
    {
        m_ActiveArea = dng_rect(fRawProcessor->imgdata.sizes.iheight,
                                fRawProcessor->imgdata.sizes.iwidth);
    }

    m_Imgdata = fRawProcessor->imgdata;

    bool fujiRotate90 = false;
    if ((0 == memcmp("FUJIFILM", m_Imgdata.idata.make, std::min((size_t)8, sizeof(m_Imgdata.idata.make)))) &&
            (2 == fRawProcessor->COLOR(0, 1)) &&
            (1 == fRawProcessor->COLOR(1, 0)))
    {
        fujiRotate90 = true;
        m_Imgdata.sizes.iheight = fRawProcessor->imgdata.sizes.iwidth;
        m_Imgdata.sizes.iwidth = fRawProcessor->imgdata.sizes.iheight;
        m_Imgdata.sizes.flip = 6;
    }

    fBounds = dng_rect(m_Imgdata.sizes.iheight, m_Imgdata.sizes.iwidth);
    fPlanes = (m_Imgdata.idata.filters == 0) ? 3 : 1;
    uint32 pixelType = ttShort;
    uint32 pixelSize = TagTypeSize(pixelType);
    uint32 bytes = fBounds.H() * fBounds.W() * fPlanes * pixelSize;

    m_Memory.Reset(m_Allocator.Allocate(bytes));

    m_Buffer.fArea       = fBounds;
    m_Buffer.fPlane      = 0;
    m_Buffer.fPlanes     = fPlanes;
    m_Buffer.fRowStep    = m_Buffer.fPlanes * fBounds.W();
    m_Buffer.fColStep    = m_Buffer.fPlanes;
    m_Buffer.fPlaneStep  = 1;
    m_Buffer.fPixelType  = pixelType;
    m_Buffer.fPixelSize  = pixelSize;
    m_Buffer.fData       = m_Memory->Buffer();

    if (fRawProcessor->imgdata.idata.filters == 0)
    {
        unsigned short* output = (unsigned short*)m_Buffer.fData;

        for (unsigned int row = 0; row < fRawProcessor->imgdata.sizes.iheight; row++)
        {
            for (unsigned int col = 0; col < fRawProcessor->imgdata.sizes.iwidth; col++)
            {
                for (int color = 0; color < fRawProcessor->imgdata.idata.colors; color++)
                {
                    *output = fRawProcessor->imgdata.image[row * fRawProcessor->imgdata.sizes.iwidth + col][color];
                    *output++;
                }
            }
        }
    }
    else
    {
        if (!fRawProcessor->imgdata.idata.cdesc[3])
            fRawProcessor->imgdata.idata.cdesc[3] = 'G';

        unsigned short* output = (unsigned short*)m_Buffer.fData;

        if (fujiRotate90 == false)
        {
            for (unsigned int row = 0; row < fRawProcessor->imgdata.sizes.iheight; row++)
            {
                for (unsigned int col = 0; col < fRawProcessor->imgdata.sizes.iwidth; col++)
                {
                    *output = m_Imgdata.image[row * fRawProcessor->imgdata.sizes.iwidth + col][fRawProcessor->COLOR(row, col)];
                    *output++;
                }
            }
        }
        else
        {
            for (unsigned int col = 0; col < fRawProcessor->imgdata.sizes.iwidth; col++)
            {
                for (unsigned int row = 0; row < fRawProcessor->imgdata.sizes.iheight; row++)
                {
                    *output = fRawProcessor->imgdata.image[row * fRawProcessor->imgdata.sizes.iwidth + col][fRawProcessor->COLOR(row, col)];
                    *output++;
                }
            }
        }
    }

    m_CameraNeutral = dng_vector(m_Imgdata.idata.colors);
    for (int i = 0; i < m_Imgdata.idata.colors; i++)
    {
        m_CameraNeutral[i] = 1.0 / m_Imgdata.color.cam_mul[i];
    }

    m_MakeName.Set_ASCII(m_Imgdata.idata.make);
    m_ModelName.Set_ASCII(m_Imgdata.idata.model);

    m_Channels = m_Imgdata.idata.colors;

    switch (m_Imgdata.idata.colors)
    {
    case 3:
    {
        dng_matrix_3by3 camXYZ;
        camXYZ[0][0] = m_Imgdata.color.cam_xyz[0][0];
        camXYZ[0][1] = m_Imgdata.color.cam_xyz[0][1];
        camXYZ[0][2] = m_Imgdata.color.cam_xyz[0][2];
        camXYZ[1][0] = m_Imgdata.color.cam_xyz[1][0];
        camXYZ[1][1] = m_Imgdata.color.cam_xyz[1][1];
        camXYZ[1][2] = m_Imgdata.color.cam_xyz[1][2];
        camXYZ[2][0] = m_Imgdata.color.cam_xyz[2][0];
        camXYZ[2][1] = m_Imgdata.color.cam_xyz[2][1];
        camXYZ[2][2] = m_Imgdata.color.cam_xyz[2][2];
        if (camXYZ.MaxEntry() == 0.0)
        {
            printf("Warning, camera XYZ Matrix is null");
            camXYZ = dng_matrix_3by3(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
        }

        m_ColorMatrix = camXYZ;

        break;
    }
    case 4:
    {
        dng_matrix_4by3 camXYZ;
        camXYZ[0][0] = m_Imgdata.color.cam_xyz[0][0];
        camXYZ[0][1] = m_Imgdata.color.cam_xyz[0][1];
        camXYZ[0][2] = m_Imgdata.color.cam_xyz[0][2];
        camXYZ[1][0] = m_Imgdata.color.cam_xyz[1][0];
        camXYZ[1][1] = m_Imgdata.color.cam_xyz[1][1];
        camXYZ[1][2] = m_Imgdata.color.cam_xyz[1][2];
        camXYZ[2][0] = m_Imgdata.color.cam_xyz[2][0];
        camXYZ[2][1] = m_Imgdata.color.cam_xyz[2][1];
        camXYZ[2][2] = m_Imgdata.color.cam_xyz[2][2];
        camXYZ[3][0] = m_Imgdata.color.cam_xyz[3][0];
        camXYZ[3][1] = m_Imgdata.color.cam_xyz[3][1];
        camXYZ[3][2] = m_Imgdata.color.cam_xyz[3][2];
        if (camXYZ.MaxEntry() == 0.0)
        {
            printf("Warning, camera XYZ Matrix is null");
            camXYZ = dng_matrix_4by3(0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
        }

        m_ColorMatrix = camXYZ;

        break;
    }
    }

    fRawProcessor->recycle();
}

LibRawImage::LibRawImage(const dng_rect &bounds,
                         uint32 planes,
                         uint32 pixelType,
                         dng_memory_allocator &allocator)    
    : dng_image(bounds, planes, pixelType),
      m_Memory(),
      m_Buffer(),
      m_Allocator(allocator)
{
    uint32 pixelSize = TagTypeSize(pixelType);

    uint32 bytes = bounds.H() * bounds.W() * planes * pixelSize;

    m_Memory.Reset(allocator.Allocate(bytes));

    m_Buffer.fArea = bounds;

    m_Buffer.fPlane  = 0;
    m_Buffer.fPlanes = planes;

    m_Buffer.fRowStep   = planes * bounds.W();
    m_Buffer.fColStep   = planes;
    m_Buffer.fPlaneStep = 1;

    m_Buffer.fPixelType = pixelType;
    m_Buffer.fPixelSize = pixelSize;

    m_Buffer.fData = m_Memory->Buffer();
}

LibRawImage::~LibRawImage(void)
{
}

dng_image* LibRawImage::Clone() const
{
    AutoPtr<LibRawImage> result(new LibRawImage(Bounds(), Planes(), PixelType(), m_Allocator));

    result->m_Buffer.CopyArea(m_Buffer, Bounds(), 0, Planes());

    return result.Release ();
}

void LibRawImage::AcquireTileBuffer(dng_tile_buffer &buffer,
                                    const dng_rect &area,
                                    bool dirty) const
{
    buffer.fArea = area;

    buffer.fPlane      = m_Buffer.fPlane;
    buffer.fPlanes     = m_Buffer.fPlanes;
    buffer.fRowStep    = m_Buffer.fRowStep;
    buffer.fColStep    = m_Buffer.fColStep;
    buffer.fPlaneStep  = m_Buffer.fPlaneStep;
    buffer.fPixelType  = m_Buffer.fPixelType;
    buffer.fPixelSize  = m_Buffer.fPixelSize;

    buffer.fData = (void *) m_Buffer.ConstPixel(buffer.fArea.t, buffer.fArea.l, buffer.fPlane);

    buffer.fDirty = dirty;
}

const dng_vector& LibRawImage::CameraNeutral() const
{
    return m_CameraNeutral;
}

const dng_string& LibRawImage::ModelName() const
{
    return m_ModelName;
}

const dng_string& LibRawImage::MakeName() const
{
    return m_MakeName;
}

const dng_rect& LibRawImage::ActiveArea() const
{
    return m_ActiveArea;
}

const dng_rect& LibRawImage::FinalSize() const
{
    return m_FinalSize;
}

uint32 LibRawImage::Channels() const
{
    return m_Channels;
}

const dng_matrix& LibRawImage::ColorMatrix() const
{
    return m_ColorMatrix;
}
