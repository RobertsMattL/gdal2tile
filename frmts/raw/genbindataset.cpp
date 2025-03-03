/******************************************************************************
 *
 * Project:  GDAL
 * Purpose:  Generic Binary format driver (.hdr but not ESRI .hdr!)
 * Author:   Frank Warmerdam, warmerdam@pobox.com
 *
 ******************************************************************************
 * Copyright (c) 2007, Frank Warmerdam <warmerdam@pobox.com>
 * Copyright (c) 2008-2011, Even Rouault <even dot rouault at spatialys.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/

#include "cpl_string.h"
#include "gdal_frmts.h"
#include "ogr_spatialref.h"
#include "rawdataset.h"

#include <cstdlib>

/* ==================================================================== */
/*      Table relating USGS and ESRI state plane zones.                 */
/* ==================================================================== */
constexpr int anUsgsEsriZones[] = {
    101,  3101, 102,  3126, 201,  3151, 202,  3176, 203,  3201, 301,  3226,
    302,  3251, 401,  3276, 402,  3301, 403,  3326, 404,  3351, 405,  3376,
    406,  3401, 407,  3426, 501,  3451, 502,  3476, 503,  3501, 600,  3526,
    700,  3551, 901,  3601, 902,  3626, 903,  3576, 1001, 3651, 1002, 3676,
    1101, 3701, 1102, 3726, 1103, 3751, 1201, 3776, 1202, 3801, 1301, 3826,
    1302, 3851, 1401, 3876, 1402, 3901, 1501, 3926, 1502, 3951, 1601, 3976,
    1602, 4001, 1701, 4026, 1702, 4051, 1703, 6426, 1801, 4076, 1802, 4101,
    1900, 4126, 2001, 4151, 2002, 4176, 2101, 4201, 2102, 4226, 2103, 4251,
    2111, 6351, 2112, 6376, 2113, 6401, 2201, 4276, 2202, 4301, 2203, 4326,
    2301, 4351, 2302, 4376, 2401, 4401, 2402, 4426, 2403, 4451, 2500, 0,
    2501, 4476, 2502, 4501, 2503, 4526, 2600, 0,    2601, 4551, 2602, 4576,
    2701, 4601, 2702, 4626, 2703, 4651, 2800, 4676, 2900, 4701, 3001, 4726,
    3002, 4751, 3003, 4776, 3101, 4801, 3102, 4826, 3103, 4851, 3104, 4876,
    3200, 4901, 3301, 4926, 3302, 4951, 3401, 4976, 3402, 5001, 3501, 5026,
    3502, 5051, 3601, 5076, 3602, 5101, 3701, 5126, 3702, 5151, 3800, 5176,
    3900, 0,    3901, 5201, 3902, 5226, 4001, 5251, 4002, 5276, 4100, 5301,
    4201, 5326, 4202, 5351, 4203, 5376, 4204, 5401, 4205, 5426, 4301, 5451,
    4302, 5476, 4303, 5501, 4400, 5526, 4501, 5551, 4502, 5576, 4601, 5601,
    4602, 5626, 4701, 5651, 4702, 5676, 4801, 5701, 4802, 5726, 4803, 5751,
    4901, 5776, 4902, 5801, 4903, 5826, 4904, 5851, 5001, 6101, 5002, 6126,
    5003, 6151, 5004, 6176, 5005, 6201, 5006, 6226, 5007, 6251, 5008, 6276,
    5009, 6301, 5010, 6326, 5101, 5876, 5102, 5901, 5103, 5926, 5104, 5951,
    5105, 5976, 5201, 6001, 5200, 6026, 5200, 6076, 5201, 6051, 5202, 6051,
    5300, 0,    5400, 0};

/************************************************************************/
/* ==================================================================== */
/*                              GenBinDataset                           */
/* ==================================================================== */
/************************************************************************/

class GenBinDataset final : public RawDataset
{
    friend class GenBinBitRasterBand;

    VSILFILE *fpImage;  // image data file.

    bool bGotTransform;
    double adfGeoTransform[6];
    OGRSpatialReference m_oSRS{};

    char **papszHDR;

    void ParseCoordinateSystem(char **);

    CPL_DISALLOW_COPY_ASSIGN(GenBinDataset)

    CPLErr Close() override;

  public:
    GenBinDataset();
    ~GenBinDataset() override;

    CPLErr GetGeoTransform(double *padfTransform) override;

    const OGRSpatialReference *GetSpatialRef() const override
    {
        return m_oSRS.IsEmpty() ? RawDataset::GetSpatialRef() : &m_oSRS;
    }

    char **GetFileList() override;

    static GDALDataset *Open(GDALOpenInfo *);
};

/************************************************************************/
/* ==================================================================== */
/*                       GenBinBitRasterBand                            */
/* ==================================================================== */
/************************************************************************/

class GenBinBitRasterBand final : public GDALPamRasterBand
{
    int nBits;

    CPL_DISALLOW_COPY_ASSIGN(GenBinBitRasterBand)

  public:
    GenBinBitRasterBand(GenBinDataset *poDS, int nBits);
    ~GenBinBitRasterBand() override
    {
    }

    CPLErr IReadBlock(int, int, void *) override;
};

/************************************************************************/
/*                        GenBinBitRasterBand()                         */
/************************************************************************/

GenBinBitRasterBand::GenBinBitRasterBand(GenBinDataset *poDSIn, int nBitsIn)
    : nBits(nBitsIn)
{
    SetMetadataItem("NBITS", CPLString().Printf("%d", nBitsIn),
                    "IMAGE_STRUCTURE");

    poDS = poDSIn;
    nBand = 1;

    eDataType = GDT_Byte;

    nBlockXSize = poDSIn->nRasterXSize;
    nBlockYSize = 1;
}

/************************************************************************/
/*                             IReadBlock()                             */
/************************************************************************/

CPLErr GenBinBitRasterBand::IReadBlock(int /* nBlockXOff */, int nBlockYOff,
                                       void *pImage)

{
    GenBinDataset *poGDS = reinterpret_cast<GenBinDataset *>(poDS);

    /* -------------------------------------------------------------------- */
    /*      Establish desired position.                                     */
    /* -------------------------------------------------------------------- */
    const vsi_l_offset nLineStart =
        (static_cast<vsi_l_offset>(nBlockXSize) * nBlockYOff * nBits) / 8;
    int iBitOffset = static_cast<int>(
        (static_cast<vsi_l_offset>(nBlockXSize) * nBlockYOff * nBits) % 8);
    const unsigned int nLineBytes = static_cast<unsigned int>(
        (static_cast<vsi_l_offset>(nBlockXSize) * (nBlockYOff + 1) * nBits +
         7) /
            8 -
        nLineStart);

    /* -------------------------------------------------------------------- */
    /*      Read data into buffer.                                          */
    /* -------------------------------------------------------------------- */
    GByte *pabyBuffer = static_cast<GByte *>(CPLCalloc(nLineBytes, 1));

    if (VSIFSeekL(poGDS->fpImage, nLineStart, SEEK_SET) != 0 ||
        VSIFReadL(pabyBuffer, 1, nLineBytes, poGDS->fpImage) != nLineBytes)
    {
        CPLError(CE_Failure, CPLE_FileIO,
                 "Failed to read %u bytes at offset %lu.\n%s", nLineBytes,
                 static_cast<unsigned long>(nLineStart), VSIStrerror(errno));
        CPLFree(pabyBuffer);
        return CE_Failure;
    }

    /* -------------------------------------------------------------------- */
    /*      Copy data, promoting to 8bit.                                   */
    /* -------------------------------------------------------------------- */
    GByte *pafImage = reinterpret_cast<GByte *>(pImage);
    if (nBits == 1)
    {
        for (int iX = 0; iX < nBlockXSize; iX++, iBitOffset += nBits)
        {
            if (pabyBuffer[iBitOffset >> 3] & (0x80 >> (iBitOffset & 7)))
                pafImage[iX] = 1;
            else
                pafImage[iX] = 0;
        }
    }
    else if (nBits == 2)
    {
        for (int iX = 0; iX < nBlockXSize; iX++, iBitOffset += nBits)
        {
            pafImage[iX] =
                (pabyBuffer[iBitOffset >> 3]) >> (6 - (iBitOffset & 0x7)) & 0x3;
        }
    }
    else if (nBits == 4)
    {
        for (int iX = 0; iX < nBlockXSize; iX++, iBitOffset += nBits)
        {
            if (iBitOffset == 0)
                pafImage[iX] = (pabyBuffer[iBitOffset >> 3]) >> 4;
            else
                pafImage[iX] = (pabyBuffer[iBitOffset >> 3]) & 0xf;
        }
    }
    else
    {
        CPLAssert(false);
    }

    CPLFree(pabyBuffer);

    return CE_None;
}

/************************************************************************/
/* ==================================================================== */
/*                              GenBinDataset                           */
/* ==================================================================== */
/************************************************************************/

/************************************************************************/
/*                            GenBinDataset()                             */
/************************************************************************/

GenBinDataset::GenBinDataset()
    : fpImage(nullptr), bGotTransform(false), papszHDR(nullptr)
{
    m_oSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    adfGeoTransform[0] = 0.0;
    adfGeoTransform[1] = 1.0;
    adfGeoTransform[2] = 0.0;
    adfGeoTransform[3] = 0.0;
    adfGeoTransform[4] = 0.0;
    adfGeoTransform[5] = 1.0;
}

/************************************************************************/
/*                            ~GenBinDataset()                          */
/************************************************************************/

GenBinDataset::~GenBinDataset()

{
    GenBinDataset::Close();
}

/************************************************************************/
/*                              Close()                                 */
/************************************************************************/

CPLErr GenBinDataset::Close()
{
    CPLErr eErr = CE_None;
    if (nOpenFlags != OPEN_FLAGS_CLOSED)
    {
        if (GenBinDataset::FlushCache(true) != CE_None)
            eErr = CE_Failure;

        if (fpImage)
        {
            if (VSIFCloseL(fpImage) != 0)
            {
                CPLError(CE_Failure, CPLE_FileIO, "I/O error");
                eErr = CE_Failure;
            }
        }

        CSLDestroy(papszHDR);

        if (GDALPamDataset::Close() != CE_None)
            eErr = CE_Failure;
    }
    return eErr;
}

/************************************************************************/
/*                          GetGeoTransform()                           */
/************************************************************************/

CPLErr GenBinDataset::GetGeoTransform(double *padfTransform)

{
    if (bGotTransform)
    {
        memcpy(padfTransform, adfGeoTransform, sizeof(double) * 6);
        return CE_None;
    }

    return GDALPamDataset::GetGeoTransform(padfTransform);
}

/************************************************************************/
/*                            GetFileList()                             */
/************************************************************************/

char **GenBinDataset::GetFileList()

{
    const CPLString osPath = CPLGetPath(GetDescription());
    const CPLString osName = CPLGetBasename(GetDescription());

    // Main data file, etc.
    char **papszFileList = GDALPamDataset::GetFileList();

    // Header file.
    const CPLString osFilename = CPLFormCIFilename(osPath, osName, "hdr");
    papszFileList = CSLAddString(papszFileList, osFilename);

    return papszFileList;
}

/************************************************************************/
/*                       ParseCoordinateSystem()                        */
/************************************************************************/

void GenBinDataset::ParseCoordinateSystem(char **papszHdr)

{
    const char *pszProjName = CSLFetchNameValue(papszHdr, "PROJECTION_NAME");
    if (pszProjName == nullptr)
        return;

    /* -------------------------------------------------------------------- */
    /*      Translate zone and parameters into numeric form.                */
    /* -------------------------------------------------------------------- */
    int nZone = 0;
    if (CSLFetchNameValue(papszHdr, "PROJECTION_ZONE"))
        nZone = atoi(CSLFetchNameValue(papszHdr, "PROJECTION_ZONE"));

#if 0
    // TODO(schwehr): Why was this being done but not used?
    double adfProjParams[15] = { 0.0 };
    if( CSLFetchNameValue( papszHdr, "PROJECTION_PARAMETERS" ) )
    {
        char **papszTokens = CSLTokenizeString(
            CSLFetchNameValue( papszHdr, "PROJECTION_PARAMETERS" ) );

        for( int i = 0; i < 15 && papszTokens[i] != NULL; i++ )
            adfProjParams[i] = CPLAtofM( papszTokens[i] );

        CSLDestroy( papszTokens );
    }
#endif

    /* -------------------------------------------------------------------- */
    /*      Handle projections.                                             */
    /* -------------------------------------------------------------------- */
    const char *pszDatumName = CSLFetchNameValue(papszHdr, "DATUM_NAME");

    if (EQUAL(pszProjName, "UTM") && nZone != 0)
    {
        // Just getting that the negative zone for southern hemisphere is used.
        m_oSRS.SetUTM(std::abs(nZone), nZone > 0);
    }

    else if (EQUAL(pszProjName, "State Plane") && nZone != 0)
    {
        const int nPairs = sizeof(anUsgsEsriZones) / (2 * sizeof(int));

        for (int i = 0; i < nPairs; i++)
        {
            if (anUsgsEsriZones[i * 2 + 1] == nZone)
            {
                nZone = anUsgsEsriZones[i * 2];
                break;
            }
        }

        const char *pszUnits = CSLFetchNameValueDef(papszHdr, "MAP_UNITS", "");
        double dfUnits = 0.0;
        if (EQUAL(pszUnits, "feet"))
            dfUnits = CPLAtofM(SRS_UL_US_FOOT_CONV);
        else if (STARTS_WITH_CI(pszUnits, "MET"))
            dfUnits = 1.0;
        else
            pszUnits = nullptr;

        m_oSRS.SetStatePlane(std::abs(nZone),
                             pszDatumName == nullptr ||
                                 !EQUAL(pszDatumName, "NAD27"),
                             pszUnits, dfUnits);
    }

    /* -------------------------------------------------------------------- */
    /*      Setup the geographic coordinate system.                         */
    /* -------------------------------------------------------------------- */
    if (m_oSRS.GetAttrNode("GEOGCS") == nullptr)
    {
        const char *pszSpheroidName =
            CSLFetchNameValue(papszHdr, "SPHEROID_NAME");
        const char *pszSemiMajor =
            CSLFetchNameValue(papszHdr, "SEMI_MAJOR_AXIS");
        const char *pszSemiMinor =
            CSLFetchNameValue(papszHdr, "SEMI_MINOR_AXIS");
        if (pszDatumName != nullptr &&
            m_oSRS.SetWellKnownGeogCS(pszDatumName) == OGRERR_NONE)
        {
            // good
        }
        else if (pszSpheroidName && pszSemiMajor && pszSemiMinor)
        {
            const double dfSemiMajor = CPLAtofM(pszSemiMajor);
            const double dfSemiMinor = CPLAtofM(pszSemiMinor);

            m_oSRS.SetGeogCS(pszSpheroidName, pszSpheroidName, pszSpheroidName,
                             dfSemiMajor,
                             (dfSemiMajor == 0.0 || dfSemiMajor == dfSemiMinor)
                                 ? 0.0
                                 : 1.0 / (1.0 - dfSemiMinor / dfSemiMajor));
        }
        else  // fallback default.
            m_oSRS.SetWellKnownGeogCS("WGS84");
    }
}

/************************************************************************/
/*                                Open()                                */
/************************************************************************/

GDALDataset *GenBinDataset::Open(GDALOpenInfo *poOpenInfo)

{
    /* -------------------------------------------------------------------- */
    /*      We assume the user is pointing to the binary (i.e. .bil) file.  */
    /* -------------------------------------------------------------------- */
    if (poOpenInfo->nHeaderBytes < 2 || poOpenInfo->fpL == nullptr)
        return nullptr;

    /* -------------------------------------------------------------------- */
    /*      Now we need to tear apart the filename to form a .HDR           */
    /*      filename.                                                       */
    /* -------------------------------------------------------------------- */
    const CPLString osPath = CPLGetPath(poOpenInfo->pszFilename);
    const CPLString osName = CPLGetBasename(poOpenInfo->pszFilename);
    CPLString osHDRFilename;

    char **papszSiblingFiles = poOpenInfo->GetSiblingFiles();
    if (papszSiblingFiles)
    {
        const int iFile = CSLFindString(
            papszSiblingFiles, CPLFormFilename(nullptr, osName, "hdr"));
        if (iFile < 0)  // return if there is no corresponding .hdr file
            return nullptr;

        osHDRFilename =
            CPLFormFilename(osPath, papszSiblingFiles[iFile], nullptr);
    }
    else
    {
        osHDRFilename = CPLFormCIFilename(osPath, osName, "hdr");
    }

    const bool bSelectedHDR = EQUAL(osHDRFilename, poOpenInfo->pszFilename);

    /* -------------------------------------------------------------------- */
    /*      Do we have a .hdr file?                                         */
    /* -------------------------------------------------------------------- */
    VSILFILE *fp = VSIFOpenL(osHDRFilename, "r");
    if (fp == nullptr)
    {
        return nullptr;
    }

    /* -------------------------------------------------------------------- */
    /*      Read a chunk to skim for expected keywords.                     */
    /* -------------------------------------------------------------------- */
    char achHeader[1000] = {'\0'};

    const int nRead =
        static_cast<int>(VSIFReadL(achHeader, 1, sizeof(achHeader) - 1, fp));
    achHeader[nRead] = '\0';
    CPL_IGNORE_RET_VAL(VSIFSeekL(fp, 0, SEEK_SET));

    if (strstr(achHeader, "BANDS:") == nullptr ||
        strstr(achHeader, "ROWS:") == nullptr ||
        strstr(achHeader, "COLS:") == nullptr)
    {
        CPL_IGNORE_RET_VAL(VSIFCloseL(fp));
        return nullptr;
    }

    /* -------------------------------------------------------------------- */
    /*      Has the user selected the .hdr file to open?                    */
    /* -------------------------------------------------------------------- */
    if (bSelectedHDR)
    {
        CPLError(
            CE_Failure, CPLE_AppDefined,
            "The selected file is an Generic Binary header file, but to "
            "open Generic Binary datasets, the data file should be selected "
            "instead of the .hdr file.  Please try again selecting"
            "the raw data file corresponding to the header file: %s",
            poOpenInfo->pszFilename);
        CPL_IGNORE_RET_VAL(VSIFCloseL(fp));
        return nullptr;
    }

    /* -------------------------------------------------------------------- */
    /*      Read the .hdr file.                                             */
    /* -------------------------------------------------------------------- */
    char **papszHdr = nullptr;
    const char *pszLine = CPLReadLineL(fp);

    while (pszLine != nullptr)
    {
        if (EQUAL(pszLine, "PROJECTION_PARAMETERS:"))
        {
            CPLString osPP = pszLine;

            pszLine = CPLReadLineL(fp);
            while (pszLine != nullptr && (*pszLine == '\t' || *pszLine == ' '))
            {
                osPP += pszLine;
                pszLine = CPLReadLineL(fp);
            }
            papszHdr = CSLAddString(papszHdr, osPP);
        }
        else
        {
            char *pszName = nullptr;
            const char *pszKey = CPLParseNameValue(pszLine, &pszName);
            if (pszKey && pszName)
            {
                CPLString osValue = pszKey;
                osValue.Trim();

                papszHdr = CSLSetNameValue(papszHdr, pszName, osValue);
            }
            CPLFree(pszName);

            pszLine = CPLReadLineL(fp);
        }
    }

    CPL_IGNORE_RET_VAL(VSIFCloseL(fp));

    if (CSLFetchNameValue(papszHdr, "COLS") == nullptr ||
        CSLFetchNameValue(papszHdr, "ROWS") == nullptr ||
        CSLFetchNameValue(papszHdr, "BANDS") == nullptr)
    {
        CSLDestroy(papszHdr);
        return nullptr;
    }

    /* -------------------------------------------------------------------- */
    /*      Create a corresponding GDALDataset.                             */
    /* -------------------------------------------------------------------- */
    GenBinDataset *poDS = new GenBinDataset();

    /* -------------------------------------------------------------------- */
    /*      Capture some information from the file that is of interest.     */
    /* -------------------------------------------------------------------- */
    const int nBands = atoi(CSLFetchNameValue(papszHdr, "BANDS"));

    poDS->nRasterXSize = atoi(CSLFetchNameValue(papszHdr, "COLS"));
    poDS->nRasterYSize = atoi(CSLFetchNameValue(papszHdr, "ROWS"));
    poDS->papszHDR = papszHdr;

    if (!GDALCheckDatasetDimensions(poDS->nRasterXSize, poDS->nRasterYSize) ||
        !GDALCheckBandCount(nBands, FALSE))
    {
        delete poDS;
        return nullptr;
    }

    poDS->fpImage = poOpenInfo->fpL;
    poOpenInfo->fpL = nullptr;
    poDS->eAccess = poOpenInfo->eAccess;

    /* -------------------------------------------------------------------- */
    /*      Figure out the data type.                                       */
    /* -------------------------------------------------------------------- */
    const char *pszDataType = CSLFetchNameValue(papszHdr, "DATATYPE");
    GDALDataType eDataType = GDT_Byte;
    int nBits = -1;  // Only needed for partial byte types

    if (pszDataType == nullptr)
    {
        // nothing to do
    }
    else if (EQUAL(pszDataType, "U16"))
        eDataType = GDT_UInt16;
    else if (EQUAL(pszDataType, "S16"))
        eDataType = GDT_Int16;
    else if (EQUAL(pszDataType, "F32"))
        eDataType = GDT_Float32;
    else if (EQUAL(pszDataType, "F64"))
        eDataType = GDT_Float64;
    else if (EQUAL(pszDataType, "U8"))
    {
        // nothing to do
    }
    else if (EQUAL(pszDataType, "U1") || EQUAL(pszDataType, "U2") ||
             EQUAL(pszDataType, "U4"))
    {
        nBits = atoi(pszDataType + 1);
        if (nBands != 1)
        {
            CPLError(CE_Failure, CPLE_OpenFailed,
                     "Only one band is supported for U1/U2/U4 data type");
            delete poDS;
            return nullptr;
        }
    }
    else
    {
        CPLError(CE_Warning, CPLE_AppDefined,
                 "DATATYPE=%s not recognised, assuming Byte.", pszDataType);
    }

    /* -------------------------------------------------------------------- */
    /*      Do we need byte swapping?                                       */
    /* -------------------------------------------------------------------- */
    const char *pszBYTE_ORDER = CSLFetchNameValue(papszHdr, "BYTE_ORDER");
    bool bNative = true;

    if (pszBYTE_ORDER != nullptr)
    {
#ifdef CPL_LSB
        bNative = STARTS_WITH_CI(pszBYTE_ORDER, "LSB");
#else
        bNative = !STARTS_WITH_CI(pszBYTE_ORDER, "LSB");
#endif
    }

    /* -------------------------------------------------------------------- */
    /*      Work out interleaving info.                                     */
    /* -------------------------------------------------------------------- */
    const int nItemSize = GDALGetDataTypeSizeBytes(eDataType);
    int nPixelOffset = 0;
    int nLineOffset = 0;
    vsi_l_offset nBandOffset = 0;
    bool bIntOverflow = false;

    const char *pszInterleaving = CSLFetchNameValue(papszHdr, "INTERLEAVING");
    if (pszInterleaving == nullptr)
        pszInterleaving = "BIL";

    if (EQUAL(pszInterleaving, "BSQ") || EQUAL(pszInterleaving, "NA"))
    {
        nPixelOffset = nItemSize;
        if (nItemSize <= 0 || poDS->nRasterXSize > INT_MAX / nItemSize)
            bIntOverflow = true;
        else
        {
            nLineOffset = nItemSize * poDS->nRasterXSize;
            nBandOffset =
                nLineOffset * static_cast<vsi_l_offset>(poDS->nRasterYSize);
        }
    }
    else if (EQUAL(pszInterleaving, "BIP"))
    {
        nPixelOffset = nItemSize * nBands;
        if (nPixelOffset == 0 || poDS->nRasterXSize > INT_MAX / nPixelOffset)
            bIntOverflow = true;
        else
        {
            nLineOffset = nPixelOffset * poDS->nRasterXSize;
            nBandOffset = nItemSize;
        }
    }
    else
    {
        if (!EQUAL(pszInterleaving, "BIL"))
            CPLError(CE_Warning, CPLE_AppDefined,
                     "INTERLEAVING:%s not recognised, assume BIL.",
                     pszInterleaving);

        nPixelOffset = nItemSize;
        if (nPixelOffset == 0 || nBands == 0 ||
            poDS->nRasterXSize > INT_MAX / (nPixelOffset * nBands))
            bIntOverflow = true;
        else
        {
            nLineOffset = nPixelOffset * nBands * poDS->nRasterXSize;
            nBandOffset =
                nItemSize * static_cast<vsi_l_offset>(poDS->nRasterXSize);
        }
    }

    if (bIntOverflow)
    {
        delete poDS;
        CPLError(CE_Failure, CPLE_AppDefined, "Int overflow occurred.");
        return nullptr;
    }

    if (nBits < 0 &&
        !RAWDatasetCheckMemoryUsage(poDS->nRasterXSize, poDS->nRasterYSize,
                                    nBands, nItemSize, nPixelOffset,
                                    nLineOffset, 0, nBandOffset, poDS->fpImage))
    {
        delete poDS;
        return nullptr;
    }

    poDS->SetDescription(poOpenInfo->pszFilename);
    poDS->PamInitialize();

    /* -------------------------------------------------------------------- */
    /*      Create band information objects.                                */
    /* -------------------------------------------------------------------- */
    poDS->nBands = nBands;
    for (int i = 0; i < poDS->nBands; i++)
    {
        if (nBits != -1)
        {
            poDS->SetBand(i + 1, new GenBinBitRasterBand(poDS, nBits));
        }
        else
        {
            poDS->SetBand(i + 1,
                          new RawRasterBand(poDS, i + 1, poDS->fpImage,
                                            nBandOffset * i, nPixelOffset,
                                            nLineOffset, eDataType, bNative,
                                            RawRasterBand::OwnFP::NO));
        }
    }

    /* -------------------------------------------------------------------- */
    /*      Get geotransform.                                               */
    /* -------------------------------------------------------------------- */
    if (poDS->nRasterXSize > 1 && poDS->nRasterYSize > 1 &&
        CSLFetchNameValue(papszHdr, "UL_X_COORDINATE") != nullptr &&
        CSLFetchNameValue(papszHdr, "UL_Y_COORDINATE") != nullptr &&
        CSLFetchNameValue(papszHdr, "LR_X_COORDINATE") != nullptr &&
        CSLFetchNameValue(papszHdr, "LR_Y_COORDINATE") != nullptr)
    {
        const double dfULX =
            CPLAtofM(CSLFetchNameValue(papszHdr, "UL_X_COORDINATE"));
        const double dfULY =
            CPLAtofM(CSLFetchNameValue(papszHdr, "UL_Y_COORDINATE"));
        const double dfLRX =
            CPLAtofM(CSLFetchNameValue(papszHdr, "LR_X_COORDINATE"));
        const double dfLRY =
            CPLAtofM(CSLFetchNameValue(papszHdr, "LR_Y_COORDINATE"));

        poDS->adfGeoTransform[1] = (dfLRX - dfULX) / (poDS->nRasterXSize - 1);
        poDS->adfGeoTransform[2] = 0.0;
        poDS->adfGeoTransform[4] = 0.0;
        poDS->adfGeoTransform[5] = (dfLRY - dfULY) / (poDS->nRasterYSize - 1);

        poDS->adfGeoTransform[0] = dfULX - poDS->adfGeoTransform[1] * 0.5;
        poDS->adfGeoTransform[3] = dfULY - poDS->adfGeoTransform[5] * 0.5;

        poDS->bGotTransform = true;
    }

    /* -------------------------------------------------------------------- */
    /*      Try and parse the coordinate system.                            */
    /* -------------------------------------------------------------------- */
    poDS->ParseCoordinateSystem(papszHdr);

    /* -------------------------------------------------------------------- */
    /*      Initialize any PAM information.                                 */
    /* -------------------------------------------------------------------- */
    poDS->TryLoadXML();

    /* -------------------------------------------------------------------- */
    /*      Check for overviews.                                            */
    /* -------------------------------------------------------------------- */
    poDS->oOvManager.Initialize(poDS, poOpenInfo->pszFilename);

    return poDS;
}

/************************************************************************/
/*                         GDALRegister_GenBin()                        */
/************************************************************************/

void GDALRegister_GenBin()

{
    if (GDALGetDriverByName("GenBin") != nullptr)
        return;

    GDALDriver *poDriver = new GDALDriver();

    poDriver->SetDescription("GenBin");
    poDriver->SetMetadataItem(GDAL_DCAP_RASTER, "YES");
    poDriver->SetMetadataItem(GDAL_DMD_LONGNAME,
                              "Generic Binary (.hdr Labelled)");
    poDriver->SetMetadataItem(GDAL_DMD_HELPTOPIC, "drivers/raster/genbin.html");
    poDriver->SetMetadataItem(GDAL_DCAP_VIRTUALIO, "YES");

    poDriver->pfnOpen = GenBinDataset::Open;

    GetGDALDriverManager()->RegisterDriver(poDriver);
}
