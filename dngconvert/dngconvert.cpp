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
   
   This file uses code from dngwriter.cpp -- KDE Kipi-plugins dngconverter utility 
   (https://projects.kde.org/projects/extragear/graphics/kipi-plugins) utility,
   dngwriter.cpp is Copyright 2008-2010 by Gilles Caulier <caulier dot gilles at gmail dot com> 
   and Jens Mueller <tschenser at gmx dot de>
*/

#include <string>
#include <assert.h>

#include "dng_bad_pixels.h"
#include "dng_camera_profile.h"
#include "dng_color_space.h"
#include "dng_exceptions.h"
#include "dng_file_stream.h"
#include "dng_globals.h"
#include "dng_host.h"
#include "dng_ifd.h"
#include "dng_image_writer.h"
#include "dng_info.h"
#include "dng_linearization_info.h"
#include "dng_memory_stream.h"
#include "dng_mosaic_info.h"
#include "dng_negative.h"
#include "dng_preview.h"
#include "dng_read_image.h"
#include "dng_render.h"
#include "dng_simple_image.h"
#include "dng_tag_codes.h"
#include "dng_tag_types.h"
#include "dng_tag_values.h"
#include "dng_xmp.h"
#include "dng_xmp_sdk.h"

#include "zlib.h"
#define CHUNK 65536

#include "rawhelper.h"
#include "exiv2meta.h"
#include "librawimage.h"

#include "dnghost.h"
#include "dngimagewriter.h"

#ifdef WIN32
#define snprintf _snprintf
#include <windows.h>
#endif

int main(int argc, const char* argv [])
{
    if(argc == 1)
    {
        fprintf(stderr,
                "\n"
                "dngconvert - DNG convertion tool\n"
                "Usage: %s [options] <dngfile>\n"
                "Valid options:\n"
                "  -d <filename>     include dead pixel list\n"
                "  -e                embed original\n"
                "  -p <filename>     use adobe camera profile\n"
                "  -x <filename>|-   read EXIF from this file, - to disable\n",
                argv[0]);

        return -1;
    }

    //parse options
    int index;
    const char* deadpixelfilename = NULL;
    const char* outfilename = NULL;
    const char* profilefilename = NULL;
    const char* exiffilename = NULL;
    bool embedOriginal = false;

    for (index = 1; index < argc && argv [index][0] == '-'; index++)
    {
        std::string option = &argv[index][1];

        if (0 == strcmp(option.c_str(), "o"))
        {
            outfilename = argv[++index];
        }

        if (0 == strcmp(option.c_str(), "d"))
        {
            deadpixelfilename = argv[++index];
        }

        if (0 == strcmp(option.c_str(), "p"))
        {
            profilefilename = argv[++index];
        }

        if (0 == strcmp(option.c_str(), "e"))
        {
            embedOriginal = true;
        }
        
        if (0 == strcmp(option.c_str(), "x"))
        {
            exiffilename = argv[++index];
        }
    }

    if (index == argc)
    {
        fprintf (stderr, "no file specified\n");
        return 1;
    }

    const char* filename = argv[index++];


    RawHelper rawProcessor;
    libraw_data_t imgdata;
    int ret = rawProcessor.identifyRawData(filename, &imgdata);
    if(ret != 0)
    {
        printf("can not extract raw data");
        return 1;
    }

    dng_memory_allocator memalloc(gDefaultDNGMemoryAllocator);

    DngHost host(&memalloc);

    host.SetSaveDNGVersion(dngVersion_SaveDefault);
    host.SetSaveLinearDNG(false);
    host.SetKeepOriginalFile(true);

    AutoPtr<dng_image> image(new LibRawImage(filename, memalloc));
    LibRawImage* rawImage = static_cast<LibRawImage*>(image.Get());

    // -----------------------------------------------------------------------------------------

    AutoPtr<dng_negative> negative(host.Make_dng_negative());

    negative->SetDefaultScale(dng_urational(rawImage->FinalSize().W(), rawImage->ActiveArea().W()),
                              dng_urational(rawImage->FinalSize().H(), rawImage->ActiveArea().H()));
    if (imgdata.idata.filters != 0)
    {
        negative->SetDefaultCropOrigin(8, 8);
        negative->SetDefaultCropSize(rawImage->ActiveArea().W() - 16, rawImage->ActiveArea().H() - 16);
    }
    else
    {
        negative->SetDefaultCropOrigin(0, 0);
        negative->SetDefaultCropSize(rawImage->ActiveArea().W(), rawImage->ActiveArea().H());
    }
    negative->SetActiveArea(rawImage->ActiveArea());

    std::string file(filename);
    size_t found = std::min(file.rfind("\\"), file.rfind("/"));
    if (found != std::string::npos)
        file = file.substr(found + 1, file.length() - found - 1);
    negative->SetOriginalRawFileName(file.c_str());

    negative->SetColorChannels(rawImage->Channels());

    ColorKeyCode colorCodes[4] = {colorKeyMaxEnum, colorKeyMaxEnum, colorKeyMaxEnum, colorKeyMaxEnum};
    for(uint32 i = 0; i < 4; i++)
    {
        switch(imgdata.idata.cdesc[i])
        {
        case 'R':
            colorCodes[i] = colorKeyRed;
            break;
        case 'G':
            colorCodes[i] = colorKeyGreen;
            break;
        case 'B':
            colorCodes[i] = colorKeyBlue;
            break;
        case 'C':
            colorCodes[i] = colorKeyCyan;
            break;
        case 'M':
            colorCodes[i] = colorKeyMagenta;
            break;
        case 'Y':
            colorCodes[i] = colorKeyYellow;
            break;
        }
    }

    negative->SetColorKeys(colorCodes[0], colorCodes[1], colorCodes[2], colorCodes[3]);

    uint32 bayerPhase = 0xFFFFFFFF;
    if (rawImage->Channels() == 4)
    {
        negative->SetQuadMosaic(imgdata.idata.filters);
    }
    else if (0 == memcmp("FUJIFILM", rawImage->MakeName().Get(), std::min((uint32)8, sizeof(rawImage->MakeName().Get()))))
    {
        negative->SetFujiMosaic(0);
    }
    else
    {
        switch(imgdata.idata.filters)
        {
        case 0xe1e1e1e1:
            bayerPhase = 0;
            break;
        case 0xb4b4b4b4:
            bayerPhase = 1;
            break;
        case 0x1e1e1e1e:
            bayerPhase = 2;
            break;
        case 0x4b4b4b4b:
            bayerPhase = 3;
            break;
        }
        if (bayerPhase != 0xFFFFFFFF)
            negative->SetBayerMosaic(bayerPhase);
    }

    negative->SetWhiteLevel(rawImage->WhiteLevel(0), 0);
    negative->SetWhiteLevel(rawImage->WhiteLevel(1), 1);
    negative->SetWhiteLevel(rawImage->WhiteLevel(2), 2);
    negative->SetWhiteLevel(rawImage->WhiteLevel(3), 3);

    const dng_mosaic_info* mosaicinfo = negative->GetMosaicInfo();
    if ((mosaicinfo != NULL) && (mosaicinfo->fCFAPatternSize == dng_point(2, 2)))
    {
        negative->SetQuadBlacks(rawImage->BlackLevel(0),
                                rawImage->BlackLevel(1),
                                rawImage->BlackLevel(2),
                                rawImage->BlackLevel(3));
    }
    else
    {
        negative->SetBlackLevel(rawImage->BlackLevel(0), 0);
    }

    negative->SetBaselineExposure(0.0);
    negative->SetBaselineNoise(1.0);
    negative->SetBaselineSharpness(1.0);

    dng_orientation orientation;
    switch (imgdata.sizes.flip)
    {
    case 3:
        orientation = dng_orientation::Rotate180();
        break;

    case 5:
        orientation = dng_orientation::Rotate90CCW();
        break;

    case 6:
        orientation = dng_orientation::Rotate90CW();
        break;

    default:
        orientation = dng_orientation::Normal();
        break;
    }
    negative->SetBaseOrientation(orientation);

    negative->SetAntiAliasStrength(dng_urational(100, 100));
    negative->SetLinearResponseLimit(1.0);
    negative->SetShadowScale(dng_urational(1, 1));

    negative->SetAnalogBalance(dng_vector_3(1.0, 1.0, 1.0));

    // -------------------------------------------------------------------------------

    AutoPtr<dng_camera_profile> prof(new dng_camera_profile);
    if (profilefilename != NULL)
    {
        dng_file_stream profStream(profilefilename);
        prof->ParseExtended(profStream);
    }
    else
    {
        char* lpszProfName = new char[255];
        strcpy(lpszProfName, rawImage->MakeName().Get());
        strcat(lpszProfName, " ");
        strcat(lpszProfName, rawImage->ModelName().Get());

        prof->SetName(lpszProfName);
        delete lpszProfName;

        prof->SetColorMatrix1((dng_matrix) rawImage->ColorMatrix());
        prof->SetCalibrationIlluminant1(lsD65);
    }

    negative->AddProfile(prof);

    negative->SetCameraNeutral(rawImage->CameraNeutral());

    // -----------------------------------------------------------------------------------------

    if (deadpixelfilename != NULL)
    {
        if (bayerPhase != 0xFFFFFFFF)
        {
            AutoPtr<dng_bad_pixel_list> badPixelList(new dng_bad_pixel_list());

            char*cp, line[128];
            int time, row, col;
            FILE *fp = fopen(deadpixelfilename, "r");
            if (fp)
            {
                while (fgets (line, 128, fp))
                {
                    cp = strchr(line, '#');
                    if (cp)
                        *cp = 0;
                    if (sscanf (line, "%d %d %d", &col, &row, &time) < 2)
                        continue;
                    if ((unsigned) col >= image->Width() || (unsigned) row >= image->Height())
                        continue;
                    //currently ignore timestamp
                    //if (time > timestamp)
                    //    continue;
                    badPixelList->AddPoint(dng_point(row, col));
                }
                fclose(fp);
            }
            else
            {
                fprintf (stderr, "could not read dead pixel file\n");
                return 1;
            }

            AutoPtr<dng_opcode> badPixelOpcode(new dng_opcode_FixBadPixelsList(badPixelList, bayerPhase));
            negative->OpcodeList1().Append(badPixelOpcode);
        }
        else
        {
            fprintf (stderr, "dead pixel lists are only applyable to bayer images\n");
            return 1;
        }
    }

    // -----------------------------------------------------------------------------------------

    if (exiffilename == NULL)
        // read exif from raw file
        exiffilename = filename;
    // '-x -' disables exif reading
    if (strcmp(exiffilename, "-") != 0)
    {
        dng_file_stream stream(exiffilename);
        Exiv2Meta exiv2Meta;
        exiv2Meta.Parse(host, stream);
        exiv2Meta.PostParse(host);

        // Exif Data
        dng_xmp xmpSync(memalloc);
        dng_exif* exifData = exiv2Meta.GetExif();
        if (exifData != NULL)
        {
            xmpSync.SyncExif(*exifData);
            AutoPtr<dng_memory_block> xmpBlock(xmpSync.Serialize());
            negative->SetXMP(host, xmpBlock->Buffer(), xmpBlock->LogicalSize());
            negative->SynchronizeMetadata();
        }

        // XMP Data
        dng_xmp* xmpData = exiv2Meta.GetXMP();
        if (xmpData != NULL)
        {
            AutoPtr<dng_memory_block> xmpBlock(xmpData->Serialize());
            negative->SetXMP(host, xmpBlock->Buffer(), xmpBlock->LogicalSize());
            negative->SynchronizeMetadata();
        }

        // Makernote backup.
        if ((exiv2Meta.MakerNoteLength() > 0) && (exiv2Meta.MakerNoteByteOrder().Length() == 2))
        {
            dng_memory_stream streamPriv(memalloc);
            streamPriv.SetBigEndian();

            streamPriv.Put("Adobe", 5);
            streamPriv.Put_uint8(0x00);
            streamPriv.Put("MakN", 4);
            streamPriv.Put_uint32(exiv2Meta.MakerNoteLength() + exiv2Meta.MakerNoteByteOrder().Length() + 4);
            streamPriv.Put(exiv2Meta.MakerNoteByteOrder().Get(), exiv2Meta.MakerNoteByteOrder().Length());
            streamPriv.Put_uint32(exiv2Meta.MakerNoteOffset());
            streamPriv.Put(exiv2Meta.MakerNoteData(), exiv2Meta.MakerNoteLength());
            AutoPtr<dng_memory_block> blockPriv(host.Allocate(streamPriv.Length()));
            streamPriv.SetReadPosition(0);
            streamPriv.Get(blockPriv->Buffer(), streamPriv.Length());
            negative->SetPrivateData(blockPriv);
        }
    }

    // -----------------------------------------------------------------------------------------

    negative->SetModelName(negative->GetExif()->fModel.Get());

    // -----------------------------------------------------------------------------------------

    if (true == embedOriginal)
    {
        dng_file_stream originalDataStream(filename);
        originalDataStream.SetReadPosition(0);

        uint32 forkLength = (uint32)originalDataStream.Length();
        uint32 forkBlocks = (uint32)floor((forkLength + 65535.0) / 65536.0);

        int level = Z_DEFAULT_COMPRESSION;
        int ret;
        z_stream zstrm;
        unsigned char inBuffer[CHUNK];
        unsigned char outBuffer[CHUNK * 2];

        dng_memory_stream embedDataStream(memalloc);
        embedDataStream.SetBigEndian(true);
        embedDataStream.Put_uint32(forkLength);

        uint32 offset = (2 + forkBlocks) * sizeof(uint32);
        embedDataStream.Put_uint32(offset);

        for (uint32 block = 0; block < forkBlocks; block++)
        {
            embedDataStream.Put_uint32(0);
        }

        for (uint32 block = 0; block < forkBlocks; block++)
        {
            uint32 originalBlockLength = (uint32)std::min((uint64)CHUNK,
                                                          originalDataStream.Length() - originalDataStream.Position());
            originalDataStream.Get(inBuffer, originalBlockLength);

            /* allocate deflate state */
            zstrm.zalloc = Z_NULL;
            zstrm.zfree = Z_NULL;
            zstrm.opaque = Z_NULL;
            ret = deflateInit(&zstrm, level);
            if (ret != Z_OK)
                return ret;

            /* compress */
            zstrm.avail_in = originalBlockLength;
            if (zstrm.avail_in == 0)
                break;
            zstrm.next_in = inBuffer;

            zstrm.avail_out = CHUNK * 2;
            zstrm.next_out = outBuffer;
            ret = deflate(&zstrm, Z_FINISH);
            assert(ret == Z_STREAM_END);

            uint32 compressedBlockLength = zstrm.total_out;

            /* clean up and return */
            (void)deflateEnd(&zstrm);

            embedDataStream.SetWritePosition(offset);
            embedDataStream.Put(outBuffer, compressedBlockLength);

            offset += compressedBlockLength;
            embedDataStream.SetWritePosition((2 + block) * sizeof(int32));
            embedDataStream.Put_uint32(offset);
        }

        embedDataStream.SetWritePosition(offset);
        embedDataStream.Put_uint32(0);
        embedDataStream.Put_uint32(0);
        embedDataStream.Put_uint32(0);
        embedDataStream.Put_uint32(0);
        embedDataStream.Put_uint32(0);
        embedDataStream.Put_uint32(0);
        embedDataStream.Put_uint32(0);

        AutoPtr<dng_memory_block> block(host.Allocate(embedDataStream.Length()));
        embedDataStream.SetReadPosition(0);
        embedDataStream.Get(block->Buffer(), embedDataStream.Length());

        dng_md5_printer md5;
        md5.Process(block->Buffer(), block->LogicalSize());
        negative->SetOriginalRawFileData(block);
        negative->SetOriginalRawFileDigest(md5.Result());
        negative->ValidateOriginalRawFileDigest();
    }

    // -----------------------------------------------------------------------------------------

    // Assign Raw image data.
    negative->SetStage1Image(image);

    // Compute linearized and range mapped image
    negative->BuildStage2Image(host);

    // Compute demosaiced image (used by preview and thumbnail)
    negative->BuildStage3Image(host);

    negative->SynchronizeMetadata();
    negative->RebuildIPTC(true, false);

    // -----------------------------------------------------------------------------------------

    dng_preview_list previewList;

    AutoPtr<dng_image> jpegImage;
    dng_render jpeg_render(host, *negative);
    jpeg_render.SetFinalSpace(dng_space_sRGB::Get());
    jpeg_render.SetFinalPixelType(ttByte);
    jpeg_render.SetMaximumSize(1024);
    jpegImage.Reset(jpeg_render.Render());

    DngImageWriter jpeg_writer;
    AutoPtr<dng_memory_stream> dms(new dng_memory_stream(gDefaultDNGMemoryAllocator));
    jpeg_writer.WriteJPEG(host, *dms, *jpegImage.Get(), 75, 1);
    dms->SetReadPosition(0);

    AutoPtr<dng_jpeg_preview> jpeg_preview;
    jpeg_preview.Reset(new dng_jpeg_preview);
    jpeg_preview->fPhotometricInterpretation = piYCbCr;
    jpeg_preview->fPreviewSize               = jpegImage->Size();
    jpeg_preview->fYCbCrSubSampling          = dng_point(2, 2);
    jpeg_preview->fCompressedData.Reset(host.Allocate(dms->Length()));
    dms->Get(jpeg_preview->fCompressedData->Buffer_char(), dms->Length());
    jpeg_preview->fInfo.fApplicationName.Set_ASCII("DNG SDK");
    jpeg_preview->fInfo.fApplicationVersion.Set_ASCII("1.3");
    //jpeg_preview->fInfo.fDateTime = ;
    jpeg_preview->fInfo.fColorSpace = previewColorSpace_sRGB;

    AutoPtr<dng_preview> pp(dynamic_cast<dng_preview*>(jpeg_preview.Release()));
    previewList.Append(pp);
    dms.Reset();

    // -----------------------------------------------------------------------------------------

    dng_image_preview thumbnail;
    dng_render thumbnail_render(host, *negative);
    thumbnail_render.SetFinalSpace(dng_space_sRGB::Get());
    thumbnail_render.SetFinalPixelType(ttByte);
    thumbnail_render.SetMaximumSize(256);
    thumbnail.fImage.Reset(thumbnail_render.Render());

    // -----------------------------------------------------------------------------------------

    dng_image_writer writer;

    // output filename: replace raw file extension with .dng
    std::string lpszOutFileName(filename);
    if (outfilename != NULL)
    {
        lpszOutFileName.assign(outfilename);
    }
    else
    {
        found = lpszOutFileName.find_last_of(".");
        if(found != std::string::npos)
            lpszOutFileName.resize(found);
        lpszOutFileName.append(".dng");
    }

    dng_file_stream filestream(lpszOutFileName.c_str(), true);

    writer.WriteDNG(host, filestream, *negative.Get(), thumbnail, ccJPEG, &previewList);

    return 0;
}

