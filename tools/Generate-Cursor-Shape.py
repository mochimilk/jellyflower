"""Regenerate the native cubic path from the supplied SVG; optional ReportLab dependency."""
from pathlib import Path
import xml.etree.ElementTree as ET
from reportlab.graphics.svgpath import SvgPath

root = Path(__file__).resolve().parents[1]
svg = ET.parse(root / 'assets/cursor-menu.svg').getroot()
path = next(iter(svg))
parsed = SvgPath(path.attrib['d'])
points, types, offset = [], [], 0
for operation in parsed.operators:
    count, native = {0:(2,6), 1:(2,2), 2:(6,4), 3:(0,0)}[operation]
    if operation == 3:
        types[-1] |= 1  # PT_CLOSEFIGURE
        continue
    coordinates = parsed.points[offset:offset+count]; offset += count
    points.extend(zip(coordinates[::2], coordinates[1::2]))
    types.extend([native] * (count//2))

# Exact cubic extrema establish the artwork center, without changing its scale.
extent, offset, current = [], 0, (0,0)
for operation in parsed.operators:
    if operation in (0,1):
        current = tuple(parsed.points[offset:offset+2]); offset += 2; extent.append(current)
    elif operation == 2:
        a = current; b,c,d = [tuple(parsed.points[i:i+2]) for i in range(offset,offset+6,2)]; offset += 6
        values = [0.0,1.0]
        for axis in (0,1):
            qa=-a[axis]+3*b[axis]-3*c[axis]+d[axis]; qb=2*(a[axis]-2*b[axis]+c[axis]); qc=b[axis]-a[axis]
            if abs(qa)<1e-12:
                if abs(qb)>1e-12: values.append(-qc/qb)
            elif qb*qb-4*qa*qc>=0:
                delta=(qb*qb-4*qa*qc)**.5; values.extend([(-qb+delta)/(2*qa),(-qb-delta)/(2*qa)])
        for t in values:
            if 0<=t<=1:
                u=1-t; extent.append(tuple(u**3*a[j]+3*u*u*t*b[j]+3*u*t*t*c[j]+t**3*d[j] for j in (0,1)))
        current=d
cx=(min(p[0] for p in extent)+max(p[0] for p in extent))/2
cy=(min(p[1] for p in extent)+max(p[1] for p in extent))/2
lines = ['// Generated from assets/cursor-menu.svg by tools/Generate-Cursor-Shape.py.',
    '#pragma once', '#include <array>', 'namespace acrylic::cursor_shape {',
    'struct Point { double x, y; };',
    f'inline constexpr double CenterX = {cx:.8f}, CenterY = {cy:.8f};',
    f'inline constexpr double StrokeWidth = {float(path.attrib["stroke-width"]):.8f};',
    f'inline constexpr std::array<Point, {len(points)}> Points{{{{']
lines += [f'    {{{x:.8f}, {y:.8f}}},' for x,y in points]
lines += ['}};', f'inline constexpr std::array<unsigned char, {len(types)}> Types{{{{',
    '    ' + ', '.join(map(str,types)), '}};', '} // namespace acrylic::cursor_shape', '']
destination = root / 'src/cursor_shape.hpp'
destination.write_text('\n'.join(lines),encoding='utf-8')
print(f'Generated exact SVG cubic geometry: {len(points)} points; center {cx:.4f},{cy:.4f}; 2-unit stroke.')
