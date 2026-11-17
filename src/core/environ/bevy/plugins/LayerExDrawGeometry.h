// LayerExDraw interface by Go Watanabe; see LICENSE-THIRD-PARTY.md.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace BevyDrawGeometry {

struct Point {
    double x=0, y=0;
    bool operator==(const Point &other) const { return x==other.x && y==other.y; }
};

struct Rect {
    double x=0, y=0, width=0, height=0;
    double right() const { return x+width; }
    double bottom() const { return y+height; }
    bool empty() const { return width<=0 || height<=0; }
    bool operator==(const Rect &other) const {
        return x==other.x && y==other.y && width==other.width && height==other.height;
    }
};

struct Matrix {
    std::array<double,6> elements{1,0,0,1,0,0};
    mutable int status=0;

    Matrix()=default;
    Matrix(double m11,double m12,double m21,double m22,double dx,double dy)
        :elements{m11,m12,m21,m22,dx,dy} {}

    static bool finite(const std::array<double,6> &e) {
        return std::all_of(e.begin(),e.end(),[](double n) { return std::isfinite(n); });
    }
    int setStatus(int value) const { if(value) status=value; return value; }
    int getLastStatus() const { int value=status; status=0; return value; }
    int set(const std::array<double,6> &value) {
        if(!finite(value)) return setStatus(2);
        elements=value; return 0;
    }
    int reset() { elements={1,0,0,1,0,0}; return 0; }
    bool identity() const { return elements==std::array<double,6>{1,0,0,1,0,0}; }
    double determinant() const { return elements[0]*elements[3]-elements[1]*elements[2]; }
    bool invertible() const { double d=determinant(); return std::isfinite(d) && d!=0; }
    Point apply(Point p) const {
        const auto &e=elements;
        return {p.x*e[0]+p.y*e[2]+e[4],p.x*e[1]+p.y*e[3]+e[5]};
    }
    static Matrix product(const Matrix &left,const Matrix &right) {
        const auto &a=left.elements; const auto &b=right.elements;
        return {a[0]*b[0]+a[1]*b[2],a[0]*b[1]+a[1]*b[3],
                a[2]*b[0]+a[3]*b[2],a[2]*b[1]+a[3]*b[3],
                a[4]*b[0]+a[5]*b[2]+b[4],a[4]*b[1]+a[5]*b[3]+b[5]};
    }
    int multiply(const Matrix &matrix,int order) {
        if(order!=0 && order!=1) return setStatus(2);
        return set((order ? product(*this,matrix) : product(matrix,*this)).elements);
    }
    int invert() {
        if(!invertible()) return setStatus(2);
        const auto &e=elements; double d=determinant();
        return set({e[3]/d,-e[1]/d,-e[2]/d,e[0]/d,
                    (e[2]*e[5]-e[3]*e[4])/d,(e[1]*e[4]-e[0]*e[5])/d});
    }
    int translate(double dx,double dy,int order) { return multiply({1,0,0,1,dx,dy},order); }
    int scale(double sx,double sy,int order) { return multiply({sx,0,0,sy,0,0},order); }
    int shear(double sx,double sy,int order) { return multiply({1,sy,sx,1,0,0},order); }
    int rotate(double degrees,int order) {
        double angle=std::remainder(degrees,360.0)*0.017453292519943295;
        double s=std::sin(angle),c=std::cos(angle);
        return multiply({c,s,-s,c,0,0},order);
    }
    int rotateAt(double degrees,Point center,int order) {
        Matrix rotation;
        if(rotation.rotate(degrees,1)) return setStatus(2);
        auto displaced=rotation.apply(center);
        rotation.elements[4]=center.x-displaced.x;
        rotation.elements[5]=center.y-displaced.y;
        return multiply(rotation,order);
    }
};

enum class Verb : unsigned int { Move, Line, Cubic, Close };

struct Command {
    Verb verb;
    std::array<double,6> points{};
};

struct Path {
    std::vector<Command> commands;
    bool newFigure=true;
    Point current{},start{};

    void startFigure() { newFigure=true; }
    void move(Point p) {
        commands.push_back({Verb::Move,{p.x,p.y}});
        current=start=p; newFigure=false;
    }
    void line(Point p) {
        commands.push_back({Verb::Line,{p.x,p.y}}); current=p;
    }
    void connect(Point p) {
        if(newFigure) move(p);
        else if(!(current==p)) line(p);
    }
    void cubic(Point a,Point b,Point end) {
        commands.push_back({Verb::Cubic,{a.x,a.y,b.x,b.y,end.x,end.y}}); current=end;
    }
    void closeFigure() {
        if(newFigure) return;
        commands.push_back({Verb::Close,{}}); current=start; newFigure=true;
    }
    void addLine(Point a,Point b) { connect(a); line(b); }
    void addBezier(Point a,Point b,Point c,Point d) { connect(a); cubic(b,c,d); }
    bool addBeziers(const std::vector<Point> &points) {
        if(points.size()<4 || (points.size()-1)%3) return false;
        connect(points[0]);
        for(std::size_t i=1;i<points.size();i+=3) cubic(points[i],points[i+1],points[i+2]);
        return true;
    }
    bool addLines(const std::vector<Point> &points,bool closed=false) {
        if(points.size()<(closed ? 3u : 2u)) return false;
        if(closed) startFigure();
        connect(points[0]);
        for(std::size_t i=1;i<points.size();++i) line(points[i]);
        if(closed) closeFigure();
        return true;
    }
    void addRectangle(Rect rect) {
        startFigure(); move({rect.x,rect.y});
        line({rect.right(),rect.y}); line({rect.right(),rect.bottom()}); line({rect.x,rect.bottom()});
        closeFigure();
    }
    bool addArc(Rect rect,double startAngle,double sweepAngle,bool pie=false) {
        if(rect.width<=0 || rect.height<=0 || !std::isfinite(startAngle) || !std::isfinite(sweepAngle)) return false;
        double sweep=std::clamp(sweepAngle,-360.0,360.0);
        if(sweep==0) return true;
        double angle=std::remainder(startAngle,360.0)*0.017453292519943295;
        int count=static_cast<int>(std::ceil(std::abs(sweep)/90.0));
        double step=sweep*0.017453292519943295/count;
        double rx=rect.width/2,ry=rect.height/2,cx=rect.x+rx,cy=rect.y+ry;
        auto parameter=[&](double a) { return std::atan2(rx*std::sin(a),ry*std::cos(a)); };
        auto point=[&](double a) -> Point { return {cx+rx*std::cos(a),cy+ry*std::sin(a)}; };
        if(pie) { startFigure(); move({cx,cy}); }
        connect(point(parameter(angle)));
        for(int i=0;i<count;++i) {
            double end=parameter(angle+step),begin=parameter(angle);
            double delta=std::remainder(end-begin,6.283185307179586);
            if(step>0 && delta<0) delta+=6.283185307179586;
            if(step<0 && delta>0) delta-=6.283185307179586;
            double k=4.0/3.0*std::tan(delta/4.0);
            Point a=point(begin),b=point(end);
            cubic({a.x-k*rx*std::sin(begin),a.y+k*ry*std::cos(begin)},
                  {b.x+k*rx*std::sin(end),b.y-k*ry*std::cos(end)},b);
            angle+=step;
        }
        if(pie) closeFigure();
        return true;
    }
    bool addEllipse(Rect rect) {
        if(rect.width<=0 || rect.height<=0) return false;
        startFigure(); addArc(rect,0,360); closeFigure(); return true;
    }
    bool addCurve(const std::vector<Point> &points,int offset,int segments,double tension,bool closed=false) {
        if(points.size()<2 || !std::isfinite(tension) || offset<0 || segments<1 ||
           static_cast<std::size_t>(offset)>=points.size() ||
           (!closed && static_cast<std::size_t>(segments)>=points.size()-offset)) return false;
        if(closed && (points.size()<3 || offset!=0 || static_cast<std::size_t>(segments)!=points.size())) return false;
        auto at=[&](int index) -> Point {
            int n=static_cast<int>(points.size());
            return points[closed ? (index+n)%n : std::clamp(index,0,n-1)];
        };
        if(closed) startFigure();
        connect(at(offset));
        double factor=tension/3.0;
        for(int i=offset;i<offset+segments;++i) {
            Point p0=at(i-1),p1=at(i),p2=at(i+1),p3=at(i+2);
            cubic({p1.x+(p2.x-p0.x)*factor,p1.y+(p2.y-p0.y)*factor},
                  {p2.x-(p3.x-p1.x)*factor,p2.y-(p3.y-p1.y)*factor},p2);
        }
        if(closed) closeFigure();
        return true;
    }
};

}
