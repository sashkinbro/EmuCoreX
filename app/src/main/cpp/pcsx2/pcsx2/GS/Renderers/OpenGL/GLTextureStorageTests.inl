// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Native CPU metadata proof; no unsupported GL allocation or fake capabilities.
#include "GLTextureStorage.h"
namespace GLFetchStorageOracle
{
    struct Texture final : GSTexture
    {
        explicit Texture(u8 bytes)
        {
            m_size=GSVector2i(5,3);m_mipmap_levels=3;m_format=Format::ColorClip;
            m_storage_bytes_per_pixel=bytes;
        }
        void* GetNativeHandle() const override{return nullptr;}
        bool DoUpdate(const GSVector4i&,const void*,int,int) override{return false;}
        bool DoMap(GSMap&,const GSVector4i*,int) override{return false;}
        void Unmap() override{}
        void GenerateMipmap() override{}
#ifdef PCSX2_DEVBUILD
        void SetDebugName(std::string_view) override{}
#endif
    };
    struct Download final : GSDownloadTexture
    {
        std::array<u8,512> bytes{};
        explicit Download(u8 size):GSDownloadTexture(5,4,GSTexture::Format::ColorClip)
        {
            m_storage_bytes_per_pixel=size;m_current_pitch=128;m_map_pointer=bytes.data();
            for(u32 i=0;i<bytes.size();++i)bytes[i]=static_cast<u8>((i*29u+17u)^((i>>8)*53));
        }
        void DoCopyFromTexture(const GSVector4i&,GSTexture*,const GSVector4i&,u32,bool) override{}
        bool Map(const GSVector4i&) override{return true;}
        void Unmap() override{}
        void Flush() override{}
#ifdef PCSX2_DEVBUILD
        void SetDebugName(std::string_view) override{}
#endif
    };
    void Run(void (*check)(bool,const char*))
    {
        const auto normalized=ResolveOGLColorClipStorage(true);
        const auto floating=ResolveOGLColorClipStorage(false);
        check(normalized.format==GL_RGBA16&&normalized.upload_type==GL_UNSIGNED_SHORT&&normalized.pixel_shift==3&&
            floating.format==GL_RGBA32F&&floating.upload_type==GL_FLOAT&&floating.pixel_shift==4&&
            OGLStorageSupportsARMColorFetch(normalized.format)&&!OGLStorageSupportsARMColorFetch(floating.format),
            "GL ColorClip actual storage resolver retains UNORM16 and selects legal FP32 sampler fallback when unavailable");
        Texture legacy(0),physical(16);
        check(legacy.CalcUploadPitch(5)==40&&legacy.CalcUploadRowLengthFromPitch(128)==16&&legacy.GetMemUsage()==144&&
            GSTexture::CalcUploadPitch(GSTexture::Format::ColorClip,5)==40&&
            GSTexture::CalcUploadRowLengthFromPitch(GSTexture::Format::ColorClip,128)==16,
            "GL physical metadata leaves default texture semantics and static format helpers unchanged");
        check(physical.GetCompressedBytesPerBlock()==16&&physical.CalcUploadPitch(5)==80&&
            physical.CalcUploadRowLengthFromPitch(128)==8&&physical.CalcUploadRowLengthFromPitch(0xffffffffu)==0x10000000u&&physical.GetMemUsage()==288,
            "GL FP32 physical texture metadata covers odd-width upload rows and complete mip memory accounting");
        Download download(16);u32 offset=0,size=0,rows=0;
        const GSVector4i rect(1,1,4,3);
        download.GetTransferSize(rect,&offset,&size,&rows);
        std::array<u8,256> actual{},expected{};actual.fill(0xa5);expected=actual;
        for(u32 row=0;row<2;++row)std::memcpy(expected.data()+16+row*80,download.bytes.data()+144+row*128,48);
        const bool read=download.ReadTexels(rect,actual.data()+16,80);
        check(download.GetTransferPitch(5,64)==128&&offset==144&&size==48&&rows==2&&read&&actual==expected,
            "GL FP32 download transfer offsets row pitch tails and native ReadTexels preserve surrounding canaries");
        actual.fill(0xa5);expected=actual;
        const bool rejected=!download.ReadTexels(rect,actual.data()+16,24);
        check(rejected&&actual==expected,
            "GL FP32 ReadTexels rejects insufficient caller stride before writing any output bytes");
    }
}
