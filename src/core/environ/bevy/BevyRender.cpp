#include "BevyRender.h"
#include "LayerBitmapIntf.h"
#include "tjsCommHead.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
    void Dispatch(TVPBevyRenderCommand &command)
    {
        if (krkr_render_dispatch(&command))
            throw std::runtime_error("Bevy texture operation failed");
    }

    class Texture final : public iTVPTexture2D
    {
        TVPTextureFormat::e Format;
        std::vector<uint8_t> Mapping;
        bool Writable = false;
        std::vector<uint32_t> PluginMapping;
        bool PluginWritable = false;

    public:
        uint64_t ID;
        Texture(unsigned w, unsigned h, TVPTextureFormat::e format)
            : iTVPTexture2D(w, h), Format(format)
        {
            static std::atomic<uint64_t> next{1};
            ID = next++;
            TVPBevyRenderCommand c{};
            c.operation = 0;
            c.target = ID;
            c.format = format;
            c.rect[2] = w;
            c.rect[3] = h;
            Dispatch(c);
        }
        ~Texture() override
        {
            TVPBevyRenderCommand c{};
            c.operation = 1;
            c.target = ID;
            krkr_render_dispatch(&c);
        }
        void Flush()
        {
            if(PluginWritable) {
                Mapping.resize(PluginMapping.size()*4);
                auto out=reinterpret_cast<uint32_t *>(Mapping.data());
                for(size_t i=0;i<PluginMapping.size();++i) {
                    auto p=PluginMapping[i];
                    out[i]=(p&0xff00ff00)|((p&0xff)<<16)|((p>>16)&0xff);
                }
                Writable=true;
            }
            if (!Writable)
                return;
            TVPBevyRenderCommand c{};
            c.operation = 2;
            c.target = ID;
            c.format = Format;
            c.rect[2] = Width;
            c.rect[3] = Height;
            c.data = Mapping.data();
            c.pitch = GetPitch();
            Dispatch(c);
        }
        void Invalidate()
        {
            PluginMapping.clear(); PluginWritable=false;
            Mapping.clear();
            Writable = false;
        }
        TVPTextureFormat::e GetFormat() const override { return Format; }
        tjs_int GetPitch() const override { return Width * (int)Format; }
        bool IsStatic() override { return false; }
        bool IsOpaque() override { return false; }
        cocos2d::Texture2D *GetAdapterTexture(cocos2d::Texture2D *) override
        {
            return nullptr;
        }
        const void *GetScanLineForRead(tjs_uint line) override
        {
            if(PluginWritable) Flush();
            if (line >= (unsigned)Height)
                throw std::out_of_range("texture row");
            if (Mapping.empty())
            {
                Mapping.resize((size_t)GetPitch() * Height);
                TVPBevyRenderCommand c{};
                c.operation = 3;
                c.target = ID;
                c.format = Format;
                c.rect[2] = Width;
                c.rect[3] = Height;
                c.data = Mapping.data();
                c.pitch = GetPitch();
                Dispatch(c);
            }
            return Mapping.data() + (size_t)line * GetPitch();
        }
        void *GetScanLineForWrite(tjs_uint line) override
        {
            auto data = const_cast<void *>(GetScanLineForRead(line));
            PluginMapping.clear(); PluginWritable=false;
            Writable = true;
            return data;
        }
        void *PluginBuffer(bool write) {
            Flush();
            if(PluginMapping.empty()) {
                const auto *source=static_cast<const uint32_t *>(GetScanLineForRead(0));
                PluginMapping.resize(size_t(Width)*Height);
                for(size_t i=0;i<PluginMapping.size();++i) {
                    auto p=source[i];
                    PluginMapping[i]=(p&0xff00ff00)|((p&0xff)<<16)|((p>>16)&0xff);
                }
            }
            PluginWritable|=write;
            return PluginMapping.data();
        }
        void Update(const void *pixels, TVPTextureFormat::e format, int pitch,
                    const tTVPRect &r) override
        {
            Flush();
            TVPBevyRenderCommand c{};
            c.operation = 2;
            c.target = ID;
            c.format = format;
            c.pitch = pitch;
            c.rect[0] = r.left;
            c.rect[1] = r.top;
            c.rect[2] = r.right;
            c.rect[3] = r.bottom;
            c.data = const_cast<void *>(pixels);
            Dispatch(c);
            Invalidate();
        }
        uint32_t GetPoint(int x, int y) override
        {
            if (x < 0 || x >= Width)
                throw std::out_of_range("texture column");
            auto row = static_cast<const uint8_t *>(GetScanLineForRead(y));
            uint32_t value = 0;
            std::memcpy(&value, row + x * (int)Format, (int)Format);
            return value;
        }
        void SetPoint(int x, int y, uint32_t color) override
        {
            Update(&color, Format, (int)Format, tTVPRect(x, y, x + 1, y + 1));
        }
        void SetSize(unsigned w, unsigned h) override
        {
            TVPBevyRenderCommand c{};
            c.operation = 0;
            c.target = ID;
            c.format = Format;
            c.rect[2] = w;
            c.rect[3] = h;
            Dispatch(c);
            Width = w;
            Height = h;
            Invalidate();
        }
    };

    class Method final : public iTVPRenderMethod
    {
    public:
        float Parameters[16] = {255};
        explicit Method(const char *name) { SetName(name); }
        int EnumParameterID(const char *name) override
        {
            const char *names[] = {"opacity", "color", "phase",
                                   "vague", "area_left", "area_top",
                                   "area_right", "area_bottom", "gammaAdjustData"};
            for (int i = 0; i < 9; ++i)
                if (!strcmp(name, names[i]))
                    return i;
            if (!strcmp(name, "data"))
                return 8;
            return -1;
        }
        void SetParameterInt(int id, int value) override
        {
            if (id >= 0 && id < 16)
                Parameters[id] = value;
        }
        void SetParameterUInt(int id, unsigned value) override
        {
            SetParameterFloat(id, value);
        }
        void SetParameterFloat(int id, float value) override
        {
            if (id >= 0 && id < 16)
                Parameters[id] = value;
        }
        void SetParameterOpa(int id, int value) override
        {
            SetParameterInt(id, value);
        }
        void SetParameterColor4B(int, unsigned value) override
        {
            for (int i = 0; i < 4; ++i)
                Parameters[12 + i] = ((value >> (i * 8)) & 255) / 255.f;
        }
        void SetParameterPtr(int id, const void *value) override
        {
            if (id != 8 || !value)
                throw std::runtime_error("Unknown Bevy render parameter");
            auto &g = *static_cast<const tTVPGLGammaAdjustData *>(value);
            Parameters[8] = g.RGamma;
            Parameters[9] = g.GGamma;
            Parameters[10] = g.BGamma;
            Parameters[4] = g.RFloor / 255.f;
            Parameters[5] = g.GFloor / 255.f;
            Parameters[6] = g.BFloor / 255.f;
            Parameters[12] = g.RCeil / 255.f;
            Parameters[13] = g.GCeil / 255.f;
            Parameters[14] = g.BCeil / 255.f;
        }
    };

    class Manager final : public iTVPRenderManager
    {
        std::unordered_map<std::string, std::unique_ptr<Method>> Methods;
        int Sampling = 0;
        TVPBevyRenderCommand Command(iTVPRenderMethod *method, iTVPTexture2D *target,
                                     iTVPTexture2D *reference, const tTVPRect &r)
        {
            auto &m = *static_cast<Method *>(method);
            TVPBevyRenderCommand c{};
            c.operation = 4;
            c.target = TVPBevyTextureID(target);
            c.format = target->GetFormat();
            c.sources[1] = TVPBevyTextureID(reference ? reference : target);
            c.method = m.GetName().c_str();
            c.rect[0] = r.left;
            c.rect[1] = r.top;
            c.rect[2] = r.right;
            c.rect[3] = r.bottom;
            for (int i = 0; i < 4; ++i)
                c.source_rects[1][i] = c.rect[i];
            std::copy(m.Parameters, m.Parameters + 16, c.parameters);
            c.parameters[11] = Sampling;
            Sampling = 0;
            return c;
        }

    public:
        const char *GetName() override { return "bevy"; }
        bool GetRenderStat(unsigned &count, uint64_t &memory) override
        {
            count = 0;
            memory = 0;
            return true;
        }
        int EnumParameterID(const char *name) override
        {
            return !strcmp(name, "StretchType") ? 0 : -1;
        }
        void SetParameterInt(int id, int value) override
        {
            if (id == 0)
                Sampling = value & 0xffff;
        }
        iTVPRenderMethod *GetRenderMethod(const char *name,
                                          uint32_t * = nullptr) override
        {
            auto &m = Methods[name];
            if (!m)
                m = std::make_unique<Method>(name);
            return m.get();
        }
        iTVPTexture2D *CreateTexture2D(const void *pixels, int pitch, unsigned w,
                                       unsigned h, TVPTextureFormat::e format,
                                       int = 0) override
        {
            auto result = std::make_unique<Texture>(w, h, format);
            if (pixels)
                result->Update(pixels, format, pitch, tTVPRect(0, 0, w, h));
            return result.release();
        }
        iTVPTexture2D *CreateTexture2D(tTVPBitmap *bitmap) override
        {
            return CreateTexture2D(bitmap->GetBits(), bitmap->GetPitch(),
                                   bitmap->GetWidth(), bitmap->GetHeight(),
                                   bitmap->GetBPP() == 8 ? TVPTextureFormat::Gray
                                                         : TVPTextureFormat::RGBA);
        }
        iTVPTexture2D *CreateTexture2D(TJS::tTJSBinaryStream *) override
        {
            return nullptr;
        }
        iTVPTexture2D *CreateTexture2D(unsigned w, unsigned h,
                                       iTVPTexture2D *source) override
        {
            auto result = std::make_unique<Texture>(w, h, source->GetFormat());
            tTVPRect r(0, 0, std::min(w, source->GetWidth()),
                       std::min(h, source->GetHeight()));
            tRenderTexRectArray::Element inputs[] = {{source, r}};
            OperateRect(GetRenderMethod("Copy"), result.get(), nullptr, r, inputs);
            return result.release();
        }
        void OperateRect(iTVPRenderMethod *method, iTVPTexture2D *target,
                         iTVPTexture2D *reference, const tTVPRect &r,
                         const tRenderTexRectArray &inputs) override
        {
            auto c = Command(method, target, reference, r);
            if (inputs.size() > 3)
                throw std::runtime_error("Too many texture inputs");
            for (size_t i = 0; i < inputs.size(); ++i)
            {
                c.sources[i] = TVPBevyTextureID(inputs[i].first);
                auto &s = inputs[i].second;
                c.source_rects[i][0] = s.left;
                c.source_rects[i][1] = s.top;
                c.source_rects[i][2] = s.right;
                c.source_rects[i][3] = s.bottom;
            }
            Dispatch(c);
            static_cast<Texture *>(target)->Invalidate();
        }
        void Geometry(iTVPRenderMethod *method, int count, int vertices,
                      iTVPTexture2D *target, iTVPTexture2D *reference,
                      const tTVPRect &clip, const tTVPPointD *points,
                      const tRenderTexQuadArray &inputs)
        {
            if (inputs.size() > 3)
                throw std::runtime_error("Too many texture inputs");
            const int sampling = Sampling;
            for (int n = 0; n < count; ++n)
            {
                Sampling = sampling;
                auto c = Command(method, target, reference, clip);
                c.geometry = vertices;
                for (int v = 0; v < vertices; ++v)
                {
                    c.points[0][v * 2] = points[n * vertices + v].x + 0.5;
                    c.points[0][v * 2 + 1] = points[n * vertices + v].y + 0.5;
                }
                for (size_t i = 0; i < inputs.size(); ++i)
                {
                    c.sources[i] = TVPBevyTextureID(inputs[i].first);
                    for (int v = 0; v < vertices; ++v)
                    {
                        c.points[i + 1][v * 2] = inputs[i].second[n * vertices + v].x;
                        c.points[i + 1][v * 2 + 1] = inputs[i].second[n * vertices + v].y;
                    }
                }
                Dispatch(c);
                static_cast<Texture *>(target)->Invalidate();
            }
        }
        void OperateTriangles(iTVPRenderMethod *m, int n, iTVPTexture2D *t,
                              iTVPTexture2D *r, const tTVPRect &c,
                              const tTVPPointD *p,
                              const tRenderTexQuadArray &s) override
        {
            Geometry(m, n, 3, t, r, c, p, s);
        }
        void OperatePerspective(iTVPRenderMethod *m, int n, iTVPTexture2D *t,
                                iTVPTexture2D *r, const tTVPRect &c,
                                const tTVPPointD *p,
                                const tRenderTexQuadArray &s) override
        {
            Geometry(m, n, 4, t, r, c, p, s);
        }
    };
} // namespace

uint64_t TVPBevyTextureID(iTVPTexture2D *texture)
{
    if (!texture)
        return 0;
    auto &t = *static_cast<Texture *>(texture);
    t.Flush();
    return t.ID;
}
void TVPBevyInvalidateTexture(iTVPTexture2D *texture)
{
    if (texture)
        static_cast<Texture *>(texture)->Invalidate();
}
void iTVPTexture2D::Release()
{
    if (!--RefCount)
        delete this;
}
void iTVPTexture2D::RecycleProcess() {}
void iTVPRenderManager::Initialize() {}
void iTVPRenderManager::RegisterRenderMethod(const char *, iTVPRenderMethod *)
{
}
iTVPRenderMethod *iTVPRenderManager::GetRenderMethod(const char *, uint32_t *)
{
    throw std::runtime_error("No Bevy render method");
}
iTVPRenderMethod *iTVPRenderManager::CompileRenderMethod(const char *,
                                                         const char *, int,
                                                         unsigned)
{
    throw std::runtime_error("GLSL render methods are not supported by Bevy");
}
iTVPRenderMethod *iTVPRenderManager::GetOrCompileRenderMethod(const char *name,
                                                              uint32_t *hint,
                                                              const char *, int,
                                                              unsigned)
{
    return GetRenderMethod(name, hint);
}
iTVPRenderMethod *iTVPRenderManager::GetRenderMethod(tjs_int opacity,
                                                     bool hold_alpha,
                                                     int method)
{
    static const char *names[] = {"ConstAlphaBlend",
                                  "ConstAlphaBlend_d",
                                  "AlphaBlend",
                                  "AlphaBlend_d",
                                  "AddBlend",
                                  "SubBlend",
                                  "MulBlend",
                                  "ColorDodgeBlend",
                                  "DarkenBlend",
                                  "LightenBlend",
                                  "ScreenBlend",
                                  "AdditiveAlphaBlend",
                                  "AdditiveAlphaBlend_a",
                                  "AdditiveAlphaBlend_d",
                                  "AlphaBlend_a",
                                  "ConstAlphaBlend_a",
                                  "PsAlphaBlend",
                                  "PsAddBlend",
                                  "PsSubBlend",
                                  "PsMulBlend",
                                  "PsScreenBlend",
                                  "PsOverlayBlend",
                                  "PsHardLightBlend",
                                  "PsSoftLightBlend",
                                  "PsColorDodgeBlend",
                                  "PsColorDodge5Blend",
                                  "PsColorBurnBlend",
                                  "PsLightenBlend",
                                  "PsDarkenBlend",
                                  "PsDiffBlend",
                                  "PsDiff5Blend",
                                  "PsExclusionBlend"};
    if (method < 0 || method > bmPsExclusion)
        throw std::runtime_error("Invalid blend mode");
    const char *name = names[method];
    if (opacity == 255 && method == bmCopy)
        name = hold_alpha ? "CopyColor" : "Copy";
    if (opacity == 255 && (method == bmCopyOnAlpha || method == bmCopyOnAddAlpha))
        name = hold_alpha ? "CopyColor" : "CopyOpaqueImage";
    auto result = GetRenderMethod(name);
    result->SetParameterOpa(result->EnumParameterID("opacity"), opacity);
    return result;
}
void TVPRegisterRenderManager(const char *, iTVPRenderManager *(*)()) {}
iTVPRenderManager *TVPGetRenderManager()
{
    static Manager manager;
    return &manager;
}
iTVPRenderManager *TVPGetRenderManager(const TJS::tTJSString &)
{
    return TVPGetRenderManager();
}
bool TVPIsSoftwareRenderManager() { return false; }

void *TVPBevyPluginImageBuffer(iTVPTexture2D *texture, bool write) {
    return texture ? static_cast<Texture *>(texture)->PluginBuffer(write) : nullptr;
}
