// LayerExDraw interface by Go Watanabe; see LICENSE-THIRD-PARTY.md.

using GeometryPoint=BevyDrawGeometry::Point;
using GeometryRect=BevyDrawGeometry::Rect;

static double coordinate(const tTJSVariant &value) {
    double n=value.AsReal();
    if(!std::isfinite(n) || std::abs(n)>1000000000)
        TVPThrowExceptionMessage(TJS_W("Invalid drawing coordinate"));
    return n;
}

static double component(const tTJSVariant &value,int index,const tjs_char *name) {
    if(value.Type()!=tvtObject || !value.AsObjectNoAddRef())
        TVPThrowExceptionMessage(TJS_W("Expected a drawing object or array"));
    auto obj=value.AsObjectNoAddRef();
    tTJSVariant part;
    if(TJS_FAILED(obj->PropGetByNum(0,index,&part,obj))) {
        if(TJS_FAILED(obj->PropGet(0,name,nullptr,&part,obj)))
            TVPThrowExceptionMessage(TJS_W("Missing drawing component"));
    }
    return coordinate(part);
}

static GeometryPoint pointValue(const tTJSVariant &v) {
    return {component(v,0,TJS_W("x")),component(v,1,TJS_W("y"))};
}

static GeometryRect rectValue(const tTJSVariant &v) {
    return {component(v,0,TJS_W("x")),component(v,1,TJS_W("y")),
            component(v,2,TJS_W("width")),component(v,3,TJS_W("height"))};
}

static int arrayCount(const tTJSVariant &value) {
    auto count=member(value,TJS_W("count"));
    if(count.Type()==tvtVoid) count=member(value,TJS_W("length"));
    if(count.Type()==tvtVoid || count.AsInteger()<0 || count.AsInteger()>1000000)
        TVPThrowExceptionMessage(TJS_W("Invalid drawing array length"));
    return static_cast<int>(count.AsInteger());
}

static tTJSVariant arrayValue(const tTJSVariant &value,int index) {
    tTJSVariant result;
    auto obj=value.AsObjectNoAddRef();
    if(TJS_FAILED(obj->PropGetByNum(0,index,&result,obj)))
        TVPThrowExceptionMessage(TJS_W("Missing drawing array item"));
    return result;
}

static std::vector<GeometryPoint> pointValues(const tTJSVariant &value) {
    int count=arrayCount(value);
    std::vector<GeometryPoint> result;
    result.reserve(count);
    for(int i=0;i<count;++i) result.push_back(pointValue(arrayValue(value,i)));
    return result;
}

template<class T> static tTJSVariant boxed(T *value) {
    auto object=ncbInstanceAdaptor<T>::CreateAdaptor(value);
    if(!object) { delete value; TVPThrowExceptionMessage(TJS_W("Could not create drawing object")); }
    tTJSVariant result(object,object); object->Release(); return result;
}

struct Point : GeometryPoint {
    Point(double x,double y) { this->x=coordinate(tTJSVariant(x)); this->y=coordinate(tTJSVariant(y)); }
    double getX() const { return x; } void setX(double v) { x=coordinate(tTJSVariant(v)); }
    double getY() const { return y; } void setY(double v) { y=coordinate(tTJSVariant(v)); }
    bool Equals(tTJSVariant other) const { return *this==pointValue(other); }
};

struct Rect : GeometryRect {
    Rect(double x,double y,double width,double height) {
        this->x=coordinate(tTJSVariant(x)); this->y=coordinate(tTJSVariant(y));
        this->width=coordinate(tTJSVariant(width)); this->height=coordinate(tTJSVariant(height));
    }
    explicit Rect(GeometryRect value):GeometryRect(value) {}
    double getX() const { return x; } void setX(double v) { x=coordinate(tTJSVariant(v)); }
    double getY() const { return y; } void setY(double v) { y=coordinate(tTJSVariant(v)); }
    double getWidth() const { return width; } void setWidth(double v) { width=coordinate(tTJSVariant(v)); }
    double getHeight() const { return height; } void setHeight(double v) { height=coordinate(tTJSVariant(v)); }
    double getRight() const { return right(); } double getBottom() const { return bottom(); }
    tTJSVariant getLocation() const { return boxed(new Point(x,y)); }
    tTJSVariant getBounds() const { return boxed(new Rect(*this)); }
    tTJSVariant Clone() const { return getBounds(); }
    bool Equals(tTJSVariant value) const { return *this==rectValue(value); }
    void Inflate(double dx,double dy) { x-=dx; y-=dy; width+=2*dx; height+=2*dy; }
    void InflatePoint(tTJSVariant value) { auto p=pointValue(value); Inflate(p.x,p.y); }
    void Offset(double dx,double dy) { x+=dx; y+=dy; }
    bool IntersectsWith(tTJSVariant value) const {
        auto r=rectValue(value);
        return x<r.right() && r.x<right() && y<r.bottom() && r.y<bottom();
    }
    bool IsEmptyArea() const { return empty(); }
    static bool Union(tTJSVariant output,tTJSVariant a,tTJSVariant b) {
        auto dst=ncbInstanceAdaptor<Rect>::GetNativeInstance(output.AsObjectNoAddRef());
        if(!dst) TVPThrowExceptionMessage(TJS_W("Union output must be a RectF"));
        auto r=rectValue(a),s=rectValue(b);
        double left=std::min(r.x,s.x),top=std::min(r.y,s.y);
        *static_cast<GeometryRect*>(dst)={left,top,std::max(r.right(),s.right())-left,std::max(r.bottom(),s.bottom())-top};
        return !dst->empty();
    }
};

struct Matrix : BevyDrawGeometry::Matrix {
    static Matrix read(const tTJSVariant &value) {
        if(value.Type()==tvtObject) {
            auto native=ncbInstanceAdaptor<Matrix>::GetNativeInstance(value.AsObjectNoAddRef());
            if(native) return *native;
        }
        Matrix result;
        const tjs_char *keys[]={TJS_W("m11"),TJS_W("m12"),TJS_W("m21"),TJS_W("m22"),TJS_W("dx"),TJS_W("dy")};
        for(int i=0;i<6;++i) result.elements[i]=component(value,i,keys[i]);
        return result;
    }
    static tjs_error TJS_INTF_METHOD create(Matrix **result,tjs_int count,tTJSVariant **p,iTJSDispatch2 *) {
        Matrix value;
        if(count==2) {
            auto r=rectValue(*p[0]);
            auto points=pointValues(*p[1]);
            if(points.size()!=3 || !r.width || !r.height) return TJS_E_INVALIDPARAM;
            auto &e=value.elements;
            e[0]=(points[1].x-points[0].x)/r.width;
            e[1]=(points[1].y-points[0].y)/r.width;
            e[2]=(points[2].x-points[0].x)/r.height;
            e[3]=(points[2].y-points[0].y)/r.height;
            e[4]=points[0].x-r.x*e[0]-r.y*e[2];
            e[5]=points[0].y-r.x*e[1]-r.y*e[3];
            if(!finite(e)) return TJS_E_INVALIDPARAM;
        } else {
            if(count!=0 && count!=6) return TJS_E_BADPARAMCOUNT;
            for(int i=0;i<count;++i) value.elements[i]=coordinate(*p[i]);
        }
        *result=new Matrix(value); return TJS_S_OK;
    }
    double OffsetX() const { return elements[4]; } double OffsetY() const { return elements[5]; }
    bool Equals(tTJSVariant other) const { return elements==read(other).elements; }
    int SetElements(double a,double b,double c,double d,double x,double y) { return set({a,b,c,d,x,y}); }
    int GetLastStatus() const { return getLastStatus(); }
    int Invert() { return invert(); } bool IsIdentity() const { return identity(); }
    bool IsInvertible() const { return invertible(); } int Reset() { return reset(); }
    template<int Kind> static tjs_error TJS_INTF_METHOD change(tTJSVariant *result,tjs_int count,tTJSVariant **p,iTJSDispatch2 *obj) {
        auto matrix=ncbInstanceAdaptor<Matrix>::GetNativeInstance(obj);
        if(!matrix) return TJS_E_INVALIDPARAM;
        const int required=(Kind==0 || Kind==1) ? 1 : 2;
        if(count<required) return TJS_E_BADPARAMCOUNT;
        int order=count>required ? static_cast<int>(p[required]->AsInteger()) : 0,status;
        if constexpr(Kind==0) status=matrix->multiply(read(*p[0]),order);
        else if constexpr(Kind==1) status=matrix->rotate(coordinate(*p[0]),order);
        else if constexpr(Kind==2) status=matrix->rotateAt(coordinate(*p[0]),pointValue(*p[1]),order);
        else if constexpr(Kind==3) status=matrix->scale(coordinate(*p[0]),coordinate(*p[1]),order);
        else if constexpr(Kind==4) status=matrix->shear(coordinate(*p[0]),coordinate(*p[1]),order);
        else status=matrix->translate(coordinate(*p[0]),coordinate(*p[1]),order);
        if(result) *result=status;
        return TJS_S_OK;
    }
};

struct Path : BevyDrawGeometry::Path {
    static void require(bool value) { if(!value) TVPThrowExceptionMessage(TJS_W("Invalid path parameters")); }
    void drawLine(double x1,double y1,double x2,double y2) { addLine({x1,y1},{x2,y2}); }
    void drawBezier(double x1,double y1,double x2,double y2,double x3,double y3,double x4,double y4) {
        addBezier({x1,y1},{x2,y2},{x3,y3},{x4,y4});
    }
    void drawBeziers(tTJSVariant points) { require(addBeziers(pointValues(points))); }
    void drawLines(tTJSVariant points) { require(addLines(pointValues(points))); }
    void drawPolygon(tTJSVariant points) { require(addLines(pointValues(points),true)); }
    void drawRectangle(double x,double y,double width,double height) { addRectangle({x,y,width,height}); }
    void drawRectangles(tTJSVariant rects) {
        std::vector<GeometryRect> values;
        for(int i=0,count=arrayCount(rects);i<count;++i) values.push_back(rectValue(arrayValue(rects,i)));
        for(auto value:values) addRectangle(value);
    }
    void drawArc(double x,double y,double width,double height,double startAngle,double sweepAngle) {
        require(addArc({x,y,width,height},startAngle,sweepAngle));
    }
    void drawPie(double x,double y,double width,double height,double startAngle,double sweepAngle) {
        require(addArc({x,y,width,height},startAngle,sweepAngle,true));
    }
    void drawEllipse(double x,double y,double width,double height) { require(addEllipse({x,y,width,height})); }
    void curve(tTJSVariant value,double tension,bool closed) {
        auto points=pointValues(value);
        require(addCurve(points,0,static_cast<int>(points.size())-(closed ? 0 : 1),tension,closed));
    }
    void drawCurve(tTJSVariant points) { curve(points,0.5,false); }
    void drawCurve2(tTJSVariant points,double tension) { curve(points,tension,false); }
    void drawCurve3(tTJSVariant points,int offset,int segments,double tension) { require(addCurve(pointValues(points),offset,segments,tension)); }
    void drawClosedCurve(tTJSVariant points) { curve(points,0.5,true); }
    void drawClosedCurve2(tTJSVariant points,double tension) { curve(points,tension,true); }
};
