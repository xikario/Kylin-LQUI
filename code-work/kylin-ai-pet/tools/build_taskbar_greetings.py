"""Build small, pre-rendered hover strips from the installed character art."""
import json
import subprocess
import tempfile
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
PETS = ROOT / 'assets' / 'pets'
SIZE = 96
LANCZOS = Image.Resampling.LANCZOS


def portrait(role):
    return Image.open(PETS / role / 'avatar.png').convert('RGBA').resize((SIZE, SIZE), LANCZOS)


def video_portraits(path, fps, count, crop):
    # A fixed camera keeps the face in the original icon's position.
    with tempfile.TemporaryDirectory() as directory:
        subprocess.run(['ffmpeg', '-nostdin', '-loglevel', 'error', '-c:v', 'libvpx-vp9',
                        '-i', str(path), '-vf', f'fps={fps}', '-frames:v', str(count),
                        str(Path(directory) / '%03d.png')], check=True)
        frames = {}
        for f in Path(directory).glob('*.png'):
            with Image.open(f) as image:
                frames[int(f.stem)] = image.convert('RGBA').crop(crop).resize((SIZE, SIZE), LANCZOS)
        return frames


def soften(a, b, fraction):
    return Image.blend(a.convert('RGBa'), b.convert('RGBa'), fraction).convert('RGBA')


def smoothstep(low, high, value):
    u = min(1., max(0., (value - low) / (high - low)))
    return u * u * (3 - 2 * u)


def cat_portrait(art, base, left, right):
    # Coordinates in the same 96px portrait camera as avatar.png. The warp
    # peaks at each tip and dies out at its root, above the eyes and star clip.
    bounds = art.getchannel('A').point(lambda a: 255 if a > 85 else 0).getbbox()
    l, t, r, b = bounds
    side = round((b - t) * .65)
    origin_x = l + round((r - l) * .50) - side // 2
    origin_y = t + round((b - t) * .025)

    def source_point(px, py):
        tx, ty = px * SIZE / side, py * SIZE / side
        x, y = tx, ty
        for _ in range(8):
            wl = (1 - smoothstep(7, 30, x)) * (1 - smoothstep(7, 30, y)) * left
            wr = smoothstep(56, 74, x) * (1 - smoothstep(7, 25, y)) * right
            x = tx - (10 * wl - 8 * wr)
            y = ty - (8 * wl + 6 * wr)
        return origin_x + x * side / SIZE, origin_y + y * side / SIZE

    grid = list(range(0, side, 18)) + [side]
    vertices = {(x, y): source_point(x, y) for x in grid for y in grid}
    mesh = []
    for y, bottom in zip(grid, grid[1:]):
        for x, far in zip(grid, grid[1:]):
            corners = [(x, y), (x, bottom), (far, bottom), (far, y)]
            mesh.append(((x, y, far, bottom), tuple(v for p in corners for v in vertices[p])))
    warped = art.transform((side, side), Image.Transform.MESH, mesh,
                           Image.Resampling.BICUBIC).resize(base.size, LANCZOS)
    result = base.copy()
    for box in [(0, 0, 31, 32), (54, 0, 96, 27)]:
        result.paste(warped.crop(box), box[:2])
    return result


def strip(role, frames, holds, manifest):
    assert len(frames) == len(holds)
    sheet = Image.new('RGBA', (SIZE * len(frames), SIZE))
    for i, frame in enumerate(frames):
        sheet.paste(frame, (SIZE * i, 0))
    sheet.save(PETS / role / 'greeting.png', optimize=True)
    manifest[role] = {'file': 'greeting.png', 'holdMs': holds}


def main():
    manifest = {}
    base = portrait('claude')
    wave = video_portraits(ROOT / 'clips' / 'B04.webm', 12, 44, (304, 0, 656, 352))
    frames = [soften(base, wave[14], .48), wave[14], wave[15], wave[16], wave[18], wave[21],
              wave[24], wave[28], wave[39], wave[40], wave[41], wave[42], soften(base, wave[42], .43)]
    strip('claude', frames, [46, 54, 48, 48, 62, 66, 85, 78, 68, 50, 52, 48, 42], manifest)

    base = portrait('fish')
    side = video_portraits(ROOT / 'tools/sources/fish-side.webm',
                           24, 38, (232, 71, 402, 241))
    frames = [soften(base, side[24], .42), side[24], side[28], side[30], side[32], side[34],
              side[36], side[34], side[32], side[30], side[28], side[24], soften(base, side[24], .45)]
    strip('fish', frames, [48, 92, 50, 48, 50, 52, 155, 58, 56, 56, 54, 48, 46], manifest)

    base = portrait('gemini')
    art = Image.open(PETS / 'gemini' / 'idle.webp').convert('RGBA')
    beats = [(.32, 0), (.76, 0), (1., .07), (.57, .36), (.16, .82), (0, 1.), (0, .52), (0, .14)]
    strip('gemini', [cat_portrait(art, base, *beat) for beat in beats],
          [45, 47, 68, 48, 52, 65, 54, 43], manifest)
    # The white dragon already has a registered closed-eye portrait.
    manifest['gpt'] = {'file': 'blink.png', 'holdMs': [145]}
    (PETS / 'greetings.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    main()
