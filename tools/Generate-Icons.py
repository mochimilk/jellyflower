"""Optional icon regeneration: requires Pillow and ReportLab; not needed to build."""
from pathlib import Path
from math import cos, sin, tau
import xml.etree.ElementTree as ET
from PIL import Image, ImageDraw, ImageChops
from reportlab.graphics.svgpath import SvgPath

assets = Path(__file__).resolve().parents[1] / 'assets'
sizes = (16, 20, 24, 28, 32, 40, 48, 64)


def polylines(path):
    parsed = SvgPath(path)
    points, offset, current = [], 0, (0, 0)
    for operation in parsed.operators:
        if operation in (0, 1):
            current = tuple(parsed.points[offset:offset + 2])
            offset += 2
            if operation == 0 and points:
                yield points
                points = []
            points.append(current)
        elif operation == 2:
            a = current
            b, c, d = [tuple(parsed.points[i:i + 2]) for i in range(offset, offset + 6, 2)]
            offset += 6
            for step in range(1, 65):
                t, u = step / 64, 1 - step / 64
                points.append(tuple(u**3 * a[i] + 3*u*u*t * b[i] + 3*u*t*t * c[i] + t**3 * d[i] for i in (0, 1)))
            current = d
        elif operation == 3:
            points.append(points[0])
        else:
            raise ValueError(f'Unexpected SVG operation {operation}')
    if points:
        yield points


def shapes(svg):
    root = svg.getroot()
    for element in root:
        kind = element.tag.rsplit('}', 1)[-1]
        if kind == 'path':
            paths = list(polylines(element.attrib['d']))
        elif kind == 'polyline':
            numbers = [float(value) for value in element.attrib['points'].replace(',', ' ').split()]
            paths = [list(zip(numbers[::2], numbers[1::2]))]
        elif kind == 'circle':
            cx, cy, radius = (float(element.attrib[key]) for key in ('cx', 'cy', 'r'))
            paths = [[(cx + radius * cos(tau * i / 256), cy + radius * sin(tau * i / 256)) for i in range(257)]]
        elif kind == 'rect':
            x, y, w, h, radius = (float(element.attrib.get(key, 0)) for key in ('x', 'y', 'width', 'height', 'rx'))
            points = []
            for cx, cy, start in [(x+w-radius,y+radius,-.25),(x+w-radius,y+h-radius,0),(x+radius,y+h-radius,.25),(x+radius,y+radius,.5)]:
                points += [(cx + radius * cos(tau * (start+i/256)), cy + radius * sin(tau * (start+i/256))) for i in range(65)]
            paths = [points + [points[0]]]
        else:
            raise ValueError(f'Unsupported SVG shape {kind}')
        fill = element.attrib.get('fill', root.attrib.get('fill', 'black')) != 'none'
        opacity = float(element.attrib.get('fill-opacity', root.attrib.get('fill-opacity', 1)))
        stroke = element.attrib.get('stroke', root.attrib.get('stroke', 'none')) != 'none'
        evenodd = element.attrib.get('fill-rule', root.attrib.get('fill-rule')) == 'evenodd'
        yield paths, fill, opacity, stroke, evenodd


def render(elements, size, color):
    scale = size * 8 / 24
    image = Image.new('RGBA', (size * 8, size * 8))
    radius = scale
    for paths, fill, opacity, stroke, evenodd in elements:
        layer = Image.new('RGBA', image.size)
        draw = ImageDraw.Draw(layer)
        if fill and evenodd:
            # Compound paths retain their transparent holes (the flower's eye).
            mask = Image.new('L', image.size)
            for path in paths:
                contour = Image.new('L', image.size)
                ImageDraw.Draw(contour).polygon([(x * scale, y * scale) for x, y in path], fill=255)
                mask = ImageChops.difference(mask, contour)
            layer.paste((*color[:3], round(color[3] * opacity)), (0, 0, *image.size), mask)
        for path in paths:
            points = [(x * scale, y * scale) for x, y in path]
            if fill and not evenodd:
                draw.polygon(points, fill=(*color[:3], round(color[3] * opacity)))
            if stroke:
                draw.line(points, fill=color, width=round(2 * scale), joint='curve')
                for x, y in points:
                    draw.ellipse((x-radius, y-radius, x+radius, y+radius), fill=color)
        image = Image.alpha_composite(image, layer)
    # Area coverage keeps a small compound-path hole fully transparent without
    # Lanczos ringing from the surrounding solid fill.
    sampling = Image.Resampling.BOX if any(item[4] for item in elements) else Image.Resampling.LANCZOS
    return image.resize((size, size), sampling)


def inverted_tray_icon(svg, size, light):
    """Render the supplied 28x28 icon's luminance mask and outside stroke.

    Its mask is white minus a compound black flower, not an alpha-only mask.
    Keep the 5-unit outside stroke clipped to the supplied 4,4,20,20 region.
    """
    root = svg.getroot()
    path = root.findall('{*}path')[0]
    paths = list(polylines(path.attrib['d']))
    scale = size * 8 / 28
    canvas = (size * 8, size * 8)
    paper, ink = ((0, 0, 0, 255), (255, 255, 255, 255)) if light else ((255, 255, 255, 255), (0, 0, 0, 255))
    image = Image.new('RGBA', canvas)
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((0, 0, canvas[0]-1, canvas[1]-1), radius=8*scale, fill=paper)
    flower, stroke = Image.new('L', canvas), Image.new('L', canvas)
    for contour in paths:
        points = [(x*scale, y*scale) for x,y in contour]
        part = Image.new('L', canvas)
        ImageDraw.Draw(part).polygon(points, fill=255)
        flower = ImageChops.difference(flower, part)
        stroke_draw = ImageDraw.Draw(stroke)
        stroke_draw.line(points, fill=255, width=round(5*scale), joint='curve')
        # Pillow's wide curve joins leave wedges on tightly sampled cubics.
        # Round coverage at each sample makes their continuous SVG stroke.
        radius = 2.5*scale
        for x,y in points:
            stroke_draw.ellipse((x-radius,y-radius,x+radius,y+radius), fill=255)
    mask = ImageChops.multiply(stroke, ImageChops.invert(flower))
    clip = Image.new('L', canvas)
    ImageDraw.Draw(clip).rectangle((4*scale,4*scale,24*scale-1,24*scale-1), fill=255)
    mask = ImageChops.multiply(mask, clip)
    image.paste(ink, (0,0,*canvas), mask)
    return image.resize((size,size), Image.Resampling.BOX)


def app_icon(svg, size, light):
    """Keep the supplied rounded tile, filled flower, and compound-path hole."""
    root = svg.getroot()
    scale = size * 8 / float(root.attrib['viewBox'].split()[2])
    canvas = (size * 8, size * 8)
    paper, ink = ((0,0,0,255),(255,255,255,255)) if light else ((255,255,255,255),(0,0,0,255))
    image = Image.new('RGBA', canvas)
    radius = float(root.find('{*}rect').attrib['rx'])
    ImageDraw.Draw(image).rounded_rectangle((0,0,canvas[0]-1,canvas[1]-1), radius=radius*scale, fill=paper)
    mask = Image.new('L', canvas)
    for contour in polylines(root.find('{*}path').attrib['d']):
        part = Image.new('L', canvas)
        ImageDraw.Draw(part).polygon([(x*scale,y*scale) for x,y in contour], fill=255)
        mask = ImageChops.difference(mask, part)
    image.paste(ink, (0,0,*canvas), mask)
    return image.resize((size,size), Image.Resampling.BOX)


if __name__ == '__main__':
    for name in ('acrylic-system', 'acrylic-pure', 'acrylic-off', 'sun', 'moon', 'clock', 'flower', 'update', 'package', 'package-open'):
        svg = ET.parse(assets / f'{name}.svg')
        elements = list(shapes(svg))
        if name == 'flower':
            # Preserve 24x23 aspect ratio and square-canvas vertical centering.
            elements = [([[(x, y+.5) for x,y in path] for path in item[0]], *item[1:4], True) for item in elements]
        for theme, color in (('light', (0, 0, 0, 255)), ('dark', (255, 255, 255, 255))):
            frames = [render(elements, size, color) for size in sizes]
            destination = assets / f'{name}-{theme}.ico'
            frames[-1].save(destination, sizes=[(size, size) for size in sizes], append_images=frames[:-1])
            with Image.open(destination) as check:
                assert check.ico.sizes() == {(size, size) for size in sizes}
            print(f'Generated {destination.name}: {len(sizes)} sizes')
    svg = ET.parse(assets / 'app.svg')
    for theme in ('dark','light'):
        app_sizes = (*sizes, 128, 256)
        frames = [app_icon(svg, size, theme == 'light') for size in app_sizes]
        destination = assets / f'app-{theme}.ico'
        frames[-1].save(destination, sizes=[(size,size) for size in app_sizes], append_images=frames[:-1])
        print(f'Generated {destination.name}: {len(app_sizes)} sizes')
    svg = ET.parse(assets / 'tray-inverted.svg')
    for theme in ('dark','light'):
        frames = [inverted_tray_icon(svg, size, theme == 'light') for size in sizes]
        destination = assets / f'tray-inverted-{theme}.ico'
        frames[-1].save(destination, sizes=[(size,size) for size in sizes], append_images=frames[:-1])
        print(f'Generated {destination.name}: {len(sizes)} sizes')
