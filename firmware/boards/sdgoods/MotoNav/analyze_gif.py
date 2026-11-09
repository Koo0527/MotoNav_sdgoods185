import sys
from PIL import Image

def analyze(path):
    im = Image.open(path)
    print(f"=== {path} ===")
    print("size:", im.size, "mode:", im.mode)
    n = getattr(im, 'n_frames', 1)
    print("n_frames:", n)
    transparent = 'transparency' in im.info
    bg = im.info.get('background')
    print("transparent flag:", transparent, "bg:", bg)
    durations = []
    sizes = set()
    for i in range(n):
        im.seek(i)
        fr = im.convert('RGBA')
        w, h = fr.size
        sizes.add((w, h))
        dur = im.info.get('duration', 0)
        durations.append(dur)
        # count non-transparent pixels
        alpha = fr.split()[-1]
        opaque = sum(1 for p in alpha.getdata())  # can't sum easily; use point
        # better: count pixels with alpha>0
        import itertools
        a_data = list(alpha.getdata())
        cnt = sum(1 for v in a_data if v > 0)
        if i < 3 or i == n-1:
            print(f"  frame {i}: {w}x{h} dur={dur}ms opaque_px={cnt} mode={im.mode}")
    print("unique sizes:", sizes)
    print("durations:", durations)
    # total duration
    total = sum(durations)
    print("total duration ms:", total)

analyze("thumbs_up.gif")
analyze("motorcycle_running.gif")
