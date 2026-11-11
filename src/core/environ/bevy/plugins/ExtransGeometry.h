// Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors.
// Geometry adapted from extrans rotatebase.cpp and rotatetrans.cpp.
// Distributed under the Kirikiri license; see LICENSE-THIRD-PARTY.md.
#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cmath>
namespace BevyExtraTrans {
struct tPoint { int x, y; };
struct tRotateDrawLine { int start=0, sx=0, sy=0, stepx=0, stepy=0; };
struct tRotateDrawRegionData { int left=0, right=0, type=0; };
struct tRotateDrawData {
    int count=0;
    tRotateDrawLine src1, src2;
    tRotateDrawRegionData region[5];
};
class RotateGeometry {
    int Width, Height;
    tjs_uint64 Time, CurTime;
    double Factor, TargetFactor, Accel, Twist, TwistAccel;
    float SwapTwist;
    int CenterX, CenterY;
    bool FixSrc1;
    std::vector<tRotateDrawData> rows;
    tRotateDrawData *DrawData;
    static bool Clip(int &l, int &r, int a, int b) { l=std::max(l,a); r=std::min(r,b); return l<r; }
void AddLine(tjs_int line,
	tjs_int left, tjs_int right, tjs_int type)
{
	if(Clip(left, right, 0, Width))
	{
		tRotateDrawData *data = DrawData + line;
		tjs_int i;
		for(i = 0; i < data->count; i++)
		{
			if(data->region[i].left == data->region[i].right) continue;
			if(data->region[i].left >= left && data->region[i].right <= right)
			{
				data->region[i].right = data->region[i].left;
				continue;
			}
			if(data->region[i].left < left && data->region[i].right > right)
			{
				data->region[data->count].left = right;
				data->region[data->count].right = data->region[i].right;
				data->region[data->count].type = data->region[i].type;
				data->count++;
				data->region[i].right = left;
				break;
			}
			if(data->region[i].left < left && data->region[i].right > left)
			{
				data->region[i].right = left;
				continue;
			}
			if(data->region[i].left < right && data->region[i].right > right)
			{
				data->region[i].left = right;
				continue;
			}
		}
		for(i = 0; i < data->count; i++)
		{
			if(data->region[i].left == data->region[i].right) break;
		}
		data->region[i].left = left;
		data->region[i].right = right;
		data->region[i].type = type;
		if(i == data->count) data->count++;
	}
}
void AddSource(const tPoint *points, tjs_int type)
{
	tjs_int points_x[4];
	tjs_int points_y[4];
	tjs_int scanlinestart, scanlineend;
	tjs_int leftlimit, rightlimit;
	tjs_int toppoint, bottompoint;
	points_x[0] = points[0].x;
	points_y[0] = points[0].y;
	leftlimit = points_x[0];
	rightlimit = points_x[0];
	scanlinestart = points_y[0], toppoint = 0;
	scanlineend = points_y[0], bottompoint = 0;
	points_x[1] = points[1].x;
	points_y[1] = points[1].y;
	if(leftlimit > points_x[1]) leftlimit = points_x[1];
	if(rightlimit < points_x[1]) rightlimit = points_x[1];
	if(scanlinestart > points_y[1]) scanlinestart = points_y[1], toppoint = 1;
	if(scanlineend < points_y[1]) scanlineend = points_y[1], bottompoint = 1;
	points_x[2] = points[1].x - points[0].x + points[2].x;
	points_y[2] = points[1].y - points[0].y + points[2].y;
	if(leftlimit > points_x[2]) leftlimit = points_x[2];
	if(rightlimit < points_x[2]) rightlimit = points_x[2];
	if(scanlinestart > points_y[2]) scanlinestart = points_y[2], toppoint = 2;
	if(scanlineend < points_y[2]) scanlineend = points_y[2], bottompoint = 2;
	points_x[3] = points[2].x;
	points_y[3] = points[2].y;
	if(leftlimit > points_x[3]) leftlimit = points_x[3];
	if(rightlimit < points_x[3]) rightlimit = points_x[3];
	if(scanlinestart > points_y[3]) scanlinestart = points_y[3], toppoint = 3;
	if(scanlineend < points_y[3]) scanlineend = points_y[3], bottompoint = 3;
	if(leftlimit >= Width) return;
	if(rightlimit < 0) return;
	if(scanlinestart >= Height) return;
	if(scanlineend < 0) return;
	tjs_int pd, pa, pdnext, panext;
	tjs_int pdstepx, pastepx;
	tjs_int pdx, pax;
	tjs_int sdstep, sastep;
	tjs_int sd, sa;
	tjs_int y = 0 < scanlinestart ? scanlinestart : 0;
	tjs_int ylim = (Height-1) < scanlineend ? (Height-1) : scanlineend;
	pd = pdnext = toppoint;
	pdnext --;
	pdnext &= 3;
	while(pdnext != bottompoint && points_y[pdnext] < y)
	{
		pdnext--;
		pdnext &= 3;
		pd--;
		pd &= 3;
	}
	while(pdnext != bottompoint && points_y[pdnext] == y)
	{
		pd--;
		pdnext--;
		pd &= 3;
		pdnext &= 3;
	}
	pa = panext = toppoint;
	panext ++;
	panext &= 3;
	while(panext != bottompoint && points_y[panext] < y)
	{
		panext++;
		panext &= 3;
		pa++;
		pa &= 3;
	}
	while(panext != bottompoint && points_y[panext] == y)
	{
		pa++;
		panext++;
		pa &= 3;
		panext &= 3;
	}
	if(points_y[pdnext] - points_y[pd] + 1)
		pdstepx = 65536 * (points_x[pdnext] - points_x[pd]) /
			(points_y[pdnext] - points_y[pd] + 1);
	else
		pdstepx = 65536;
	if(points_y[panext] - points_y[pa] + 1)
		pastepx = 65536 * (points_x[panext] - points_x[pa]) /
			(points_y[panext] - points_y[pa] + 1);
	else
		pastepx = 65536;
	if(points_y[pdnext] - points_y[pd] + 1)
	{
		switch(pd)
		{
		case 0: sdstep = 65536 * (Height) /
				(points_y[pdnext] - points_y[pd] + 1); break;
		case 1: sdstep = 65536 * (-Width) /
				(points_y[pdnext] - points_y[pd] + 1); break;
		case 2: sdstep = 65536 * (-Height) /
				(points_y[pdnext] - points_y[pd] + 1); break;
		case 3: sdstep = 65536 * (Width) /
				(points_y[pdnext] - points_y[pd] + 1); break;
		}
	}
	else
	{
		sdstep = 65536;
	}
	if(points_y[panext] - points_y[pa] + 1)
	{
		switch(pa)
		{
		case 0: sastep = 65536 * (Width) /
				(points_y[panext] - points_y[pa] + 1); break;
		case 1: sastep = 65536 * (Height) /
				(points_y[panext] - points_y[pa] + 1); break;
		case 2: sastep = 65536 * (-Width) /
				(points_y[panext] - points_y[pa] + 1); break;
		case 3: sastep = 65536 * (-Height) /
				(points_y[panext] - points_y[pa] + 1); break;
		}
	}
	else
	{
		sastep = 65536;
	}
	pdx = points_x[pd] * 65536;
	if(points_y[pd] < y) pdx += pdstepx * (y - points_y[pd]);
	pax = points_x[pa] * 65536;
	if(points_y[pa] < y) pax += pastepx * (y - points_y[pa]);
	switch(pd)
	{
	case 0: sd = 0; break;
	case 1: sd = Width * 65536 - 1; break;
	case 2: sd = Height * 65536 - 1; break;
	case 3: sd = 0; break;
	}
	if(points_y[pd] < y) sd += sdstep * (y - points_y[pd]);
	switch(pa)
	{
	case 0: sa = 0; break;
	case 1: sa = 0; break;
	case 2: sa = Width * 65536 - 1; break;
	case 3: sa = Height * 65536 - 1; break;
	}
	if(points_y[pa] < y) sa += sastep * (y - points_y[pa]);
	for(; y <= ylim; y++)
	{
		tjs_int ddx = pdx >> 16;
		tjs_int adx = pax >> 16;
		tjs_int dx, dy, ax, ay;
		switch(pd)
		{
		case 0: dx = 0; dy = sd; break;
		case 1: dx = sd; dy = 0; break;
		case 2: dx = Width * 65536 - 1; dy = sd; break;
		case 3: dx = sd; dy = Height * 65536 - 1; break;
		}
		switch(pa)
		{
		case 0: ax = sa; ay = 0; break;
		case 1: ax = Width * 65536 - 1; ay = sa; break;
		case 2: ax = sa; ay = Height * 65536 - 1; break;
		case 3: ax = 0; ay = sa; break;
		}
		if(adx < ddx) std::swap(dx, ax), std::swap(dy, ay), std::swap(adx, ddx);
		tjs_int sxstep, systep;
		if(adx != ddx)
		{
			sxstep =  (ax - dx + 1) / (adx - ddx + 1);
			systep =  (ay - dy + 1) / (adx - ddx + 1);
		}
		else
		{
			sxstep = systep = 65536;
		}
		AddLine(y, ddx, adx + 1, type);
		tRotateDrawLine & drawline
			= (type == 1) ? DrawData[y].src1 : DrawData[y].src2;
		drawline.start = ddx;
		drawline.sx = dx;
		drawline.sy = dy;
		drawline.stepx = sxstep;
		drawline.stepy = systep;
		if(points_y[pdnext] == y)
		{
			do
			{
				pd--;
				pdnext--;
				pd &= 3;
				pdnext &= 3;
			} while(pdnext != bottompoint && points_y[pdnext] == y);
			if(points_y[pdnext] - points_y[pd] + 1)
				pdstepx = 65536 * (points_x[pdnext] - points_x[pd]) /
					(points_y[pdnext] - points_y[pd] + 1);
			else
				pdstepx = 65536;
			if(points_y[pdnext] - points_y[pd] + 1)
			{
				switch(pd)
				{
				case 0: sdstep = 65536 * (Height) /
						(points_y[pdnext] - points_y[pd] + 1); break;
				case 1: sdstep = 65536 * (-Width) /
						(points_y[pdnext] - points_y[pd] + 1); break;
				case 2: sdstep = 65536 * (-Height) /
						(points_y[pdnext] - points_y[pd] + 1); break;
				case 3: sdstep = 65536 * (Width) /
						(points_y[pdnext] - points_y[pd] + 1); break;
				}
			}
			else
			{
				sdstep = 65536;
			}
			switch(pd)
			{
			case 0: sd = 0; break;
			case 1: sd = Width * 65536 - 1; break;
			case 2: sd = Height * 65536 - 1; break;
			case 3: sd = 0; break;
			}
			pdx = points_x[pd] * 65536;
		}
		if(points_y[panext] == y)
		{
			do
			{
				pa++;
				panext++;
				pa &= 3;
				panext &= 3;
			} while(panext != bottompoint && points_y[panext] == y);
			if(points_y[panext] - points_y[pa] + 1)
				pastepx = 65536 * (points_x[panext] - points_x[pa]) /
					(points_y[panext] - points_y[pa] + 1);
			else
				pastepx = 65536;
			if(points_y[panext] - points_y[pa] + 1)
			{
				switch(pa)
				{
				case 0: sastep = 65536 * (Width) /
						(points_y[panext] - points_y[pa] + 1); break;
				case 1: sastep = 65536 * (Height) /
						(points_y[panext] - points_y[pa] + 1); break;
				case 2: sastep = 65536 * (-Width) /
						(points_y[panext] - points_y[pa] + 1); break;
				case 3: sastep = 65536 * (-Height) /
						(points_y[panext] - points_y[pa] + 1); break;
				}
			}
			else
			{
				sastep = 65536;
			}
			switch(pa)
			{
			case 0: sa = 0; break;
			case 1: sa = 0; break;
			case 2: sa = Width * 65536 - 1; break;
			case 3: sa = Height * 65536 - 1; break;
			}
			pax = points_x[pa] * 65536;
		}
		pdx += pdstepx;
		pax += pastepx;
		sd += sdstep;
		sa += sastep;
	}
}
void CalcZoom()
	{
		tPoint points[3];
		points[0].x = 0; points[0].y = 0;
		points[1].x = Width - 1; points[1].y = 0;
		points[2].x = 0; points[2].y = Height - 1;
		AddSource(points, FixSrc1 ? 1 : 2);
		float zm = (float)(tjs_int)CurTime / (float)(tjs_int)Time;
		float tm = zm;
		if(Accel < 0)
		{
			zm = 1.0 - zm;
			zm = pow(zm, -Accel);
			zm = 1.0 - zm;
		}
		else if(Accel > 0)
		{
			zm = pow(zm, Accel);
		}
		tjs_int scx = Width/2;
		tjs_int scy = Height/2;
		tjs_int cx = (scx - CenterX) * zm + CenterX;
		tjs_int cy = (scy - CenterY) * zm + CenterY;
		if(TwistAccel < 0)
		{
			tm = 1.0 - tm;
			tm = pow(tm, -TwistAccel);
			tm = 1.0 - tm;
		}
		else if(TwistAccel > 0)
		{
			tm = pow(tm, TwistAccel);
		}
		float rad = CurTime == Time ? 0 : 2 * 3.14159265368979 * Twist * tm;
		zm = (TargetFactor - Factor) * zm + Factor;
		float s = sin(rad) * zm;
		float c = cos(rad) * zm;
		points[0].x = -cx *  c + -cy * s + scx;
		points[0].y = -cx * -s + -cy * c + scy;
		points[1].x = (Width-1-cx) *  c + -cy * s + scx;
		points[1].y = (Width-1-cx) * -s + -cy * c + scy;
		points[2].x = -cx *  c + (Height-1-cy) * s + scx;
		points[2].y = -cx * -s + (Height-1-cy) * c + scy;
		AddSource(points, FixSrc1 ? 2 : 1);
	}
void CalcSwap()
	{
		tPoint points[3];
		float rad;
		tjs_int cx, cy;
		tjs_int scx = Width/2;
		tjs_int scy = Height/2;
		float zm = (float)(tjs_int)CurTime / (float)(tjs_int)Time;
		float tm, s, c;
		tjs_int cnt = 2;
		switch(CurTime >= Time / 2 ? 0:1)
		{
			while(true)
			{
			case 0:
				tm = zm * zm;
				cx = ( - scx ) * tm + scx + sin(tm * 3.14159265368979)*scx*1.5;
				cy = ( - scy ) * tm + scy;
				rad = tm * SwapTwist;
				tm = 1.0 - tm;
				s = sin(rad) * tm;
				c = cos(rad) * tm;
				points[0].x = -scx *  c + -scy * s + cx;
				points[0].y = (-scx * -s + -scy * c) * tm + cy;
				points[1].x = (Width-1-scx) *  c + -scy * s + cx;
				points[1].y = ((Width-1-scx) * -s + -scy * c) * tm + cy;
				points[2].x = -scx *  c + (Height-1-scy) * s + cx;
				points[2].y = (-scx * -s + (Height-1-scy) * c) * tm + cy;
				AddSource(points, 1);
				cnt--;
				if(cnt == 0) break;
			case 1:
				tm = 1.0 - (1.0 - zm) * (1.0 - zm);
				cx = (scx - (Width  - 1)) * tm + (Width  - 1) - sin(tm * 3.14159265368979) * scx*1.5;
				cy = (scy - (Height - 1)) * tm + (Height - 1);
				rad = (-1.0 + tm) * SwapTwist;
				s = sin(rad) * tm;
				c = cos(rad) * tm;
				points[0].x = -scx *  c + -scy * s + cx;
				points[0].y = (-scx * -s + -scy * c) * tm + cy;
				points[1].x = (Width-1-scx) *  c + -scy * s + cx;
				points[1].y = ((Width-1-scx) * -s + -scy * c) * tm + cy;
				points[2].x = -scx *  c + (Height-1-scy) * s + cx;
				points[2].y = (-scx * -s + (Height-1-scy) * c) * tm + cy;
				AddSource(points, 2);
				cnt--;
				if(cnt == 0) break;
			}
		}
	}
public:
    RotateGeometry(int w, int h):Width(w),Height(h),rows(h),DrawData(rows.data()) {}
    std::vector<std::array<int32_t,4>> Build(int effect, tjs_uint64 time, tjs_uint64 elapsed, const double *v) {
        Time=time; CurTime=elapsed;
        for(auto &row:rows) { row.count=1; row.region[0]={0,Width,0}; }
        if(effect==5) { SwapTwist=float(v[1]*3.14159265368979*2); CalcSwap(); }
        else {
            Factor=effect==3?v[0]:1; TargetFactor=effect==3?1:0;
            Accel=v[1]; Twist=v[2]; TwistAccel=v[3]; CenterX=int(v[4]); CenterY=int(v[5]); FixSrc1=effect==3;
            CalcZoom();
        }
        std::vector<std::array<int32_t,4>> packed;
        packed.reserve(Height*10);
        for(const auto &row:rows) {
            packed.push_back({row.count,0,0,0});
            for(const auto *line:{&row.src1,&row.src2}) {
                packed.push_back({line->start,line->sx,line->sy,line->stepx});
                packed.push_back({line->stepy,0,0,0});
            }
            for(const auto &r:row.region) packed.push_back({r.left,r.right,r.type,0});
        }
        return packed;
    }
};
}
