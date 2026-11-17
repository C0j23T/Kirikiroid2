// LayerExDraw interface by Go Watanabe; see LICENSE-THIRD-PARTY.md.

extern FT_Library &TVPGetFontLibrary();

struct PrivateFontData {
    std::shared_ptr<std::vector<unsigned char>> bytes;
    long index;
    int style;
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
    std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(name,TJS_BS_READ));
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
        auto font=std::make_shared<PrivateFontData>(); font->bytes=bytes; font->index=index;
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
    std::unique_ptr<tFreeTypeFace> native;
    FT_Face face=nullptr;
    int style;
    FontFace(const ttstr &family,double size,int style,std::shared_ptr<PrivateFontData> data={}) :data(std::move(data)),style(style) {
        if(!this->data) this->data=privateFont(family,style);
        if(this->data) {
            if(FT_New_Memory_Face(TVPGetFontLibrary(),this->data->bytes->data(),this->data->bytes->size(),this->data->index,&face))
                TVPThrowExceptionMessage(TJS_W("Could not create private font face"));
        } else {
            auto resolved=TVPFindFont(family) ? family : TVPFontSystem->GetBeingFont(family);
            native=std::make_unique<tFreeTypeFace>(resolved,0); native->SetHeight(static_cast<int>(size));
            face=native->GetBaseFace()->GetFTFace();
        }
        if(FT_Set_Char_Size(face,0,static_cast<FT_F26Dot6>(std::round(size*64)),72,72)) {
            if(this->data) FT_Done_Face(face);
            face=nullptr; TVPThrowExceptionMessage(TJS_W("Could not set font size"));
        }
    }
    ~FontFace() { if(data && face) FT_Done_Face(face); }
    int ascent() const { return static_cast<int>(face->ascender*face->size->metrics.y_ppem/face->units_per_EM); }
    void underline(int &position,int &thickness) const {
        auto ppem=face->size->metrics.y_ppem; auto units=face->units_per_EM;
        position=(face->ascender-face->underline_position)*ppem/units;
        thickness=std::max(1,int(face->underline_thickness*ppem/units));
    }
    void strikeout(int &position,int &thickness) const {
        position=face->ascender*7*face->size->metrics.y_ppem/(10*face->units_per_EM);
        thickness=std::max(1,int(face->underline_thickness*face->size->metrics.y_ppem/face->units_per_EM));
    }
    unsigned defaultChar() const { return native ? native->GetDefaultChar() : '?'; }
    void load(unsigned glyph,int flags) const {
        if(FT_Load_Glyph(face,glyph,flags)) TVPThrowExceptionMessage(TJS_W("Could not load glyph outline"));
        if((style&1) && !(face->style_flags&FT_STYLE_FLAG_BOLD)) FT_GlyphSlot_Embolden(face->glyph);
        if((style&2) && !(face->style_flags&FT_STYLE_FLAG_ITALIC)) FT_GlyphSlot_Oblique(face->glyph);
    }
};
