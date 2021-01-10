/***************************************************************************
    Copyright (c) 2015 Philip Fortier

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
***************************************************************************/
#include "stdafx.h"
#include "ExtractAll.h"
#include "AppState.h"
#include "ResourceEntity.h"
#include "Components.h"
#include "PicOperations.h"
#include "View.h"
#include "RasterOperations.h"
#include "PaletteOperations.h"
#include "CompiledScript.h"
#include "Disassembler.h"
#include "Vocab000.h"
#include "ResourceContainer.h"
#include "Message.h"
#include "Text.h"
#include "ResourceBlob.h"
#include "Audio.h"
#include "SoundUtil.h"
#include "format.h"
#include "AudioCacheResourceSource.h"
#include <Src/Util/ImageUtil.h>

void ExportViewResourceAsCelImages(const ResourceEntity& resource, PaletteComponent* optionalPalette, CString destinationFolder)
{
    CelIndex celIndex = CelIndex(-1, -1);
   
        if (&resource)
        {
            const RasterComponent& raster = resource.GetComponent<RasterComponent>();
            int startLoop = (celIndex.loop == 0xffff) ? 0 : celIndex.loop;
            int endLoop = (celIndex.loop == 0xffff) ? raster.LoopCount() : (celIndex.loop + 1);


            for (int l = endLoop - 1; l >= startLoop; l--)
            {
                const Loop& loop = raster.Loops[l];
                celIndex.loop = l;
                celIndex.cel = -1;
                int startCelBase = -1;
                int endCelBase = -1;
                if (celIndex.loop != 0xffff)
                {
                    startCelBase = (celIndex.cel == 0xffff) ? 0 : celIndex.cel;
                    endCelBase = (celIndex.cel == 0xffff) ? (int)raster.Loops[celIndex.loop].Cels.size() : (celIndex.cel + 1);
                }
                int startCel = (startCelBase == -1) ? 0 : startCelBase;
                int endCel = (endCelBase == -1) ? (int)loop.Cels.size() : endCelBase;

                for (int c = endCel - 1; c >= startCel; c--)
                {
                    celIndex.loop = l;
                    celIndex.cel = c;
                    const Cel& cel = loop.Cels[c];
                    PaletteComponent* palette = optionalPalette;
                    if (!palette)
                    {
                        palette = &g_egaDummyPalette;
                    }
                    CBitmap bitmap;
                    SCIBitmapInfo bmi;
                    BYTE* pBitsDest;
                    bitmap.Attach(CreateBitmapFromResource(resource, celIndex, palette, &bmi, &pBitsDest));
                    if ((HBITMAP)bitmap)
                    {
                        // Construct a cel based on the bitmap. Throw away the HBITMAP.
                        Cel celEntire(size16((uint16_t)bmi.bmiHeader.biWidth, (uint16_t)bmi.bmiHeader.biHeight), point16(), 0);
                        celEntire.TransparentColor = cel.TransparentColor;
                        celEntire.Data.allocate(celEntire.GetDataSize());
                        celEntire.Data.assign(pBitsDest, pBitsDest + celEntire.GetDataSize());
                        // Default extension should be the first one in the list for g_szGdiplus8BitSaveFilter

                        CString strFileName = destinationFolder + '/' + "view." + std::to_string(resource.ResourceNumber).c_str() + '.' + std::to_string(celIndex.loop).c_str() + '.' + std::to_string(celIndex.cel).c_str() + ".png";
                        Save8BitBmpGdiP(strFileName, celEntire, *palette, false);

                    }
                }
            }
        }
    
}
void ExtractAllResources(SCIVersion version, const std::string &destinationFolderIn, bool extractResources, bool extractPicImages, bool extractViewImages, bool disassembleScripts, bool extractMessages, bool generateWavs, IExtractProgress *progress)
{
    std::string destinationFolder = destinationFolderIn;
    if (destinationFolder.back() != '\\')
    {
        destinationFolder += "\\";
    }

    ObjectFileScriptLookups objectFileLookups(appState->GetResourceMap().Helper(), appState->GetResourceMap().GetCompiledScriptLookups()->GetSelectorTable());
    GlobalCompiledScriptLookups scriptLookups;
    if (disassembleScripts)
    {
        if (!scriptLookups.Load(appState->GetResourceMap().Helper()))
        {
            disassembleScripts = false;
        }
    }

    int totalCount = 0;
    auto resourceContainer = appState->GetResourceMap().Resources(ResourceTypeFlags::All, ResourceEnumFlags::MostRecentOnly | ResourceEnumFlags::ExcludePatchFiles);
    for (auto &blob : *resourceContainer)
    {
        if (extractResources)
        {
            totalCount++;
        }
        if (extractViewImages && (blob->GetType() == ResourceType::View))
        {
            totalCount++;
        }
        if (extractPicImages && (blob->GetType() == ResourceType::Pic))
        {
            totalCount++;
        }
        if (disassembleScripts && (blob->GetType() == ResourceType::Pic))
        {
            totalCount++;
        }
        if (extractMessages && (blob->GetType() == ResourceType::Message))
        {
            totalCount++;
        }
        if (generateWavs && (blob->GetType() == ResourceType::Audio))
        {
            totalCount++;
        }
    }

    // sync36/audio36
    if (generateWavs || extractResources)
    {
        resourceContainer = appState->GetResourceMap().Resources(ResourceTypeFlags::AudioMap, ResourceEnumFlags::MostRecentOnly | ResourceEnumFlags::ExcludePatchFiles);
        for (auto &blob : *resourceContainer)
        {
            if (blob->GetNumber() != version.AudioMapResourceNumber)
            {
                totalCount++;
                if (generateWavs)
                {
                    totalCount++;
                }
            }
        }
    }

    int count = 0;
    // Get it again, because we don't supprot reset.
    resourceContainer = appState->GetResourceMap().Resources(ResourceTypeFlags::All, ResourceEnumFlags::MostRecentOnly | ResourceEnumFlags::ExcludePatchFiles);
    bool keepGoing = true;
    for (auto &blob : *resourceContainer)
    {
        std::string filename = GetFileNameFor(*blob);
        std::string fullPath = destinationFolder + filename;
        keepGoing = true;
        try
        {
            if (progress)
            {
                keepGoing = progress->SetProgress(fullPath, count, totalCount);
            }
            // Just the resource
            if (extractResources)
            {
                count++;
                blob->SaveToFile(fullPath);
            }

            if (keepGoing)
            {
                // Then possible pictures
                CBitmap bitmap;
                SCIBitmapInfo bmi;
                BYTE *pBitsDest = nullptr;
                std::string possibleImagePath = fullPath + ".bmp";
                if (extractPicImages && (blob->GetType() == ResourceType::Pic))
                {
                    count++;
                    if (progress)
                    {
                        keepGoing = progress->SetProgress(possibleImagePath, count, totalCount);
                    }

                    std::unique_ptr<ResourceEntity> resource = CreateResourceFromResourceData(*blob);
                    PicComponent &pic = resource->GetComponent<PicComponent>();
                    PaletteComponent *palette = resource->TryGetComponent<PaletteComponent>();
                    bitmap.Attach(GetPicBitmap(PicScreen::Visual, pic, palette, pic.Size.cx, pic.Size.cy, &bmi, &pBitsDest));
                }

                if (extractViewImages && (blob->GetType() == ResourceType::View))
                {
                    count++;
                    if (progress)
                    {
                        keepGoing = progress->SetProgress(possibleImagePath, count, totalCount);
                    }

                    std::unique_ptr<ResourceEntity> view = CreateResourceFromResourceData(*blob);
                    std::unique_ptr<PaletteComponent> optionalPalette;
                    if (view->GetComponent<RasterComponent>().Traits.PaletteType == PaletteType::VGA_256)
                    {
                        optionalPalette = appState->GetResourceMap().GetMergedPalette(*view, 999);
                    }
                    bitmap.Attach(CreateBitmapFromResource(*view, optionalPalette.get(), &bmi, &pBitsDest));
                    ExportViewResourceAsCelImages(*view, optionalPalette.get(), destinationFolder.c_str());
                }

                if ((HBITMAP)bitmap)
                {
                    Save8BitBmp(possibleImagePath, bmi, pBitsDest, 0);
                }

                if (disassembleScripts && (blob->GetType() == ResourceType::Script))
                {
                    count++;
                    std::string scriptPath = fullPath + ".txt";
                    if (progress)
                    {
                        keepGoing = progress->SetProgress(scriptPath, count, totalCount);
                    }

                    // Supply the heap stream here, since we want it match patch vs vs not.
                    std::unique_ptr<sci::istream> heapStream;
                    std::unique_ptr<ResourceBlob> heapBlob = appState->GetResourceMap().Helper().MostRecentResource(ResourceType::Heap, blob->GetNumber(), ResourceEnumFlags::ExcludePatchFiles);
                    if (heapBlob)
                    {
                        heapStream = std::make_unique<sci::istream>(heapBlob->GetReadStream());
                    }

                    CompiledScript compiledScript(blob->GetNumber());
                    compiledScript.Load(appState->GetResourceMap().Helper(), appState->GetVersion(), blob->GetNumber(), blob->GetReadStream(), heapStream.get());
                    std::stringstream out;
                    DisassembleScript(compiledScript, out, &scriptLookups, &objectFileLookups, appState->GetResourceMap().GetVocab000());
                    std::string actualPath = MakeTextFile(out.str().c_str(), scriptPath.c_str());
                }

                if (extractMessages && (blob->GetType() == ResourceType::Message))
                {
                    count++;
                    std::string msgPath = fullPath + "-msg.txt";
                    std::unique_ptr<ResourceEntity> resource = CreateResourceFromResourceData(*blob);
                    ExportMessageToFile(resource->GetComponent<TextComponent>(), msgPath);
                }

                if (generateWavs && (blob->GetType() == ResourceType::Audio))
                {
                    count++;
                    std::string wavPath = fullPath + ".wav";
                    std::unique_ptr<ResourceEntity> resource = CreateResourceFromResourceData(*blob);
                    WriteWaveFile(wavPath, resource->GetComponent<AudioComponent>());
                }
            }
        }
        catch (std::exception)
        {

        }
    }

    // Finally, the sync36 and audio36 resources and the audio maps
    if (keepGoing)
    {
        auto audioMapContainer = appState->GetResourceMap().Resources(ResourceTypeFlags::AudioMap, ResourceEnumFlags::MostRecentOnly | ResourceEnumFlags::ExcludePatchFiles);
        for (auto &blob : *audioMapContainer)
        {
            if (extractResources)
            {
                count++;
                std::string filename = GetFileNameFor(*blob);
                std::string fullPath = destinationFolder + filename;
                blob->SaveToFile(fullPath);
            }

            if ((blob->GetNumber() != version.AudioMapResourceNumber) && (extractResources || generateWavs))
            {
                count++;
                auto subResourceContainer = appState->GetResourceMap().Resources(ResourceTypeFlags::Audio, ResourceEnumFlags::MostRecentOnly, blob->GetNumber());
                if (progress)
                {
                    keepGoing = progress->SetProgress(fmt::format("Files for audio map {0}", blob->GetNumber()).c_str(), count, totalCount);
                }
                if (keepGoing)
                {
                    for (auto &blobSubs : *subResourceContainer)
                    {
                        if (extractResources)
                        {
                            SaveAudioBlobToFiles(*blobSubs, destinationFolder);
                        }

                        if (generateWavs)
                        {
                            std::string filename = GetFileNameFor(*blobSubs);
                            std::string wavPath = destinationFolder + filename + ".wav";
                            std::unique_ptr<ResourceEntity> resource = CreateResourceFromResourceData(*blobSubs);
                            WriteWaveFile(wavPath, resource->GetComponent<AudioComponent>());
                        }
                    }
                }
            }
        }
    }
}