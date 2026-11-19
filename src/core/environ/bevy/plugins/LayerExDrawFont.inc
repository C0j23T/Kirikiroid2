// LayerExDraw interface by Go Watanabe; see LICENSE-THIRD-PARTY.md.

using FontVisitor=bool (*)(void *,const unsigned char *,size_t,uint32_t);
struct PositionedGlyph { uint32_t glyph; int32_t xOffset,yOffset,advance; uint32_t cluster; };
extern "C" bool krkr_host_font_list(void *,FontVisitor);
extern "C" bool krkr_host_font_face(const tjs_char *,size_t,uint32_t,void *,FontVisitor);
struct DrawingFace { const unsigned char *bytes; size_t length; uint32_t index; };
using GlyphVisitor=bool (*)(void *,const unsigned char *,size_t,uint32_t,const PositionedGlyph *,size_t);
extern "C" bool krkr_host_shape_line(const DrawingFace *,size_t,const tjs_char *,size_t,uint32_t,void *,GlyphVisitor);

struct PrivateFontData {
    ttstr source;
    std::shared_ptr<std::vector<unsigned char>> bytes;
    long index;
    int style=0;
    std::vector<ttstr> names;
};

static std::vector<std::shared_ptr<PrivateFontData>> &privateFonts() {
    static std::vector<std::shared_ptr<PrivateFontData>> fonts;
    return fonts;
}

static std::shared_ptr<PrivateFontData> privateFont(const ttstr &name,int style) {
    std::shared_ptr<PrivateFontData> fallback;
    for(auto font:privateFonts()) if(std::find(font->names.begin(),font->names.end(),name)!=font->names.end()) {
        if(font->style==(style&3)) return font;
        if(!fallback) fallback=font;
    }
    return fallback;
}

static void addPrivateFont(const ttstr &name) {
    for(const auto &face : privateFonts()) if(face->source==name) return;
    std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(name,TJS_BS_READ));
    if(!stream) TVPThrowExceptionMessage(TJS_W("Could not open private font"));
    auto size=stream->GetSize();
    if(size==0 || size>64*1024*1024) TVPThrowExceptionMessage(TJS_W("Invalid private font size"));
    auto bytes=std::make_shared<std::vector<unsigned char>>(static_cast<size_t>(size));
    stream->ReadBuffer(bytes->data(),bytes->size());
    std::vector<std::shared_ptr<PrivateFontData>> added;
    long faceCount=1;
    for(long index=0;index<faceCount;++index) {
        FT_Face face=nullptr;
        if(FT_New_Memory_Face(TVPGetFontLibrary(),bytes->data(),bytes->size(),index,&face))
            TVPThrowExceptionMessage(TJS_W("Could not open private font"));
        std::unique_ptr<std::remove_pointer_t<FT_Face>,decltype(&FT_Done_Face)> guard(face,FT_Done_Face);
        faceCount=face->num_faces;
        if(faceCount>128) TVPThrowExceptionMessage(TJS_W("Too many private font faces"));
        if(!FT_IS_SCALABLE(face)) continue;
        auto font=std::make_shared<PrivateFontData>(); font->source=name; font->bytes=bytes; font->index=index;
        font->style=((face->style_flags&FT_STYLE_FLAG_BOLD)?1:0)|((face->style_flags&FT_STYLE_FLAG_ITALIC)?2:0);
        if(face->family_name) font->names.push_back(ttstr(face->family_name));
        for(unsigned i=0,count=FT_Get_Sfnt_Name_Count(face);i<count;++i) {
            FT_SfntName item{};
            if(FT_Get_Sfnt_Name(face,i,&item) || item.name_id!=TT_NAME_ID_FONT_FAMILY || item.platform_id!=TT_PLATFORM_MICROSOFT || item.string_len%2) continue;
            std::vector<tjs_char> decoded(item.string_len/2+1);
            for(unsigned j=0;j<item.string_len/2;++j) decoded[j]=(item.string[j*2]<<8)|item.string[j*2+1];
            ttstr family(decoded.data());
            if(std::find(font->names.begin(),font->names.end(),family)==font->names.end()) font->names.push_back(family);
        }
        if(!font->names.empty()) added.push_back(font);
    }
    if(added.empty()) TVPThrowExceptionMessage(TJS_W("Private font has no scalable faces"));
    auto &fonts=privateFonts();
    if(fonts.size()+added.size()>512) TVPThrowExceptionMessage(TJS_W("Too many private fonts"));
    fonts.insert(fonts.end(),added.begin(),added.end());
}

struct FontFace {
    std::shared_ptr<PrivateFontData> data;
    FT_Face face=nullptr;
    int style;
    static bool copySystemFace(void *context,const unsigned char *bytes,size_t length,uint32_t index) noexcept {
        try {
            auto &data=*static_cast<PrivateFontData*>(context);
            data.bytes=std::make_shared<std::vector<unsigned char>>(bytes,bytes+length);
            data.index=index;
            return true;
        } catch(...) { return false; }
    }
    FontFace(const ttstr &family,double size,int style,std::shared_ptr<PrivateFontData> source={})
        :data(source ? source : privateFont(family,style)),style(style) {
        if(!data) {
            data=std::make_shared<PrivateFontData>();
            auto registered=TVPFindFont(family);
            if(registered || !krkr_host_font_face(family.c_str(),family.GetLen(),style,data.get(),copySystemFace)) {
                ttstr name=registered ? family : TVPFontSystem->GetBeingFont(family);
                auto info=TVPFindFont(name);
                std::unique_ptr<tTJSBinaryStream> stream(TVPCreateFontStream(name));
                if(!stream || stream->GetSize()>INT_MAX) TVPThrowExceptionMessage(TJS_W("Could not open font"));
                data->bytes=std::make_shared<std::vector<unsigned char>>(stream->GetSize());
                stream->ReadBuffer(data->bytes->data(),data->bytes->size());
                data->index=info ? info->Index : 0;
            }
        }
        open(data->bytes->data(),data->bytes->size(),data->index,size);
    }
    FontFace(const unsigned char *bytes,size_t length,unsigned index,double size,int style):style(style) {
        open(bytes,length,index,size);
    }
    void open(const unsigned char *bytes,size_t length,unsigned index,double size) {
        if(FT_New_Memory_Face(TVPGetFontLibrary(),bytes,length,index,&face))
            TVPThrowExceptionMessage(TJS_W("Could not create font face"));
        if(!FT_IS_SCALABLE(face) || !face->units_per_EM ||
           FT_Set_Char_Size(face,0,static_cast<FT_F26Dot6>(std::max(1.0,std::round(size*64))),72,72)) {
            FT_Done_Face(face);
            face=nullptr; TVPThrowExceptionMessage(TJS_W("Could not set font size"));
        }
    }
    ~FontFace() { if(face) FT_Done_Face(face); }
    FontFace(const FontFace&)=delete;
    FontFace &operator=(const FontFace&)=delete;
    double scale(double units) const { return FT_MulFix(FT_Long(units),face->size->metrics.y_scale)/64.0; }
    double ascent() const { return scale(face->ascender); }
    double descent() const { return scale(-face->descender); }
    double lineSpacing() const { return ascent()+descent(); }
    double ascentLeading() const {
        auto os2=static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face,ft_sfnt_os2));
        if(os2 && os2->sTypoAscender>0) return ascent()-scale(os2->sTypoAscender);
        return (lineSpacing()-scale(face->units_per_EM))*ascent()/lineSpacing();
    }
    void underline(double &position,double &thickness) const {
        position=ascent()-scale(face->underline_position);
        thickness=std::max(1.0,scale(face->underline_thickness));
    }
    void strikeout(double &position,double &thickness) const {
        auto os2=static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face,ft_sfnt_os2));
        position=ascent()-(os2 ? scale(os2->yStrikeoutPosition) : ascent()/3);
        thickness=std::max(1.0,scale(os2 ? os2->yStrikeoutSize : face->underline_thickness));
    }
    void load(unsigned glyph,int flags) const {
        if(FT_Load_Glyph(face,glyph,flags)) TVPThrowExceptionMessage(TJS_W("Could not load glyph outline"));
        if((style&1) && !(face->style_flags&FT_STYLE_FLAG_BOLD)) FT_GlyphSlot_Embolden(face->glyph);
        if((style&2) && !(face->style_flags&FT_STYLE_FLAG_ITALIC)) FT_GlyphSlot_Oblique(face->glyph);
    }
};

struct Font {
    ttstr family;
    double size;
    int style;
    bool forceSelfPathDraw=false;
    mutable std::shared_ptr<FontFace> cached;

    Font(ttstr name,double emSize,int flags):family(name),size(emSize),style(flags) { setSize(emSize); }
    const FontFace &face() const {
        if(!cached) cached=std::make_shared<FontFace>(family,size,style);
        return *cached;
    }
    ttstr getFamily() const { return family; }
    void setFamily(ttstr value) { family=value; cached.reset(); }
    double getSize() const { return size; }
    void setSize(double value) {
        if(!std::isfinite(value) || value<=0 || value>4096) TVPThrowExceptionMessage(TJS_W("Invalid font size"));
        size=value; cached.reset();
    }
    int getStyle() const { return style; }
    void setStyle(int value) { style=value; cached.reset(); }
    bool getForceSelfPathDraw() const { return forceSelfPathDraw; }
    void setForceSelfPathDraw(bool value) { forceSelfPathDraw=value; }
    double ascent() const { return face().ascent(); }
    double descent() const { return face().descent(); }
    double spacing() const { return face().lineSpacing(); }
    double ascentLeading() const { return face().ascentLeading(); }
    double descentLeading() const { return spacing()-size-ascentLeading(); }
};
