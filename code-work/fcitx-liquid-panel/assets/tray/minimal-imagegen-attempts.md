# Minimal tray extraction attempts (2026-10-10)

Built-in imagegen edits were attempted, but none of these outputs was installed: generated gray fill and noisy edges did not match the supplied design. Final assets are manually authored SVGs rendered with Qt; see sources.json and vector/.

## chinese

Use case: background-extraction.
Asset type: one transparent monochrome input-method tray icon, square PNG.
Image 1 is the edit target. Faithfully extract ONLY the TOP-LEFT icon: the exact white Chinese character 中 centered inside the thin white rounded-square border. Do not redesign or embellish.
Remove the entire green photographic/presentation background, all lower captions and the other three icons. Preserve the simple source design: plain white symbol, smooth thin white rounded-square outline, flat front view, original symbol proportions and placement. The only possible fill inside the square is a very faint neutral translucent gray matching the source's understated frosted interior. Outside the rounded-square outline must be actual alpha transparency.
Tight, consistent square framing with the rounded-square border occupying 90% of the canvas, equal margins on all sides, all corners fully transparent. White symbol and white outline should stay sharp and legible at 24–32 pixels.
Absolutely NO colored gradients, neon, iridescence, glow, bloom, drop shadow, bevel, 3D extrusion, highlights, badges except the specified small Caps arrow, extra border, captions, watermark or fake checkerboard. This must look like the user's new calm, flat monochrome design, NOT the previous colorful glass set.

## latin

Use case: background-extraction.
Asset type: one transparent monochrome input-method tray icon, square PNG.
Image 1 is the edit target. Faithfully extract ONLY the TOP-RIGHT icon: the exact white Chinese character 英 centered inside the thin white rounded-square border. Do not redesign or embellish.
Remove the entire green photographic/presentation background, all lower captions and the other three icons. Preserve the simple source design: plain white symbol, smooth thin white rounded-square outline, flat front view, original symbol proportions and placement. The only possible fill inside the square is a very faint neutral translucent gray matching the source's understated frosted interior. Outside the rounded-square outline must be actual alpha transparency.
Tight, consistent square framing with the rounded-square border occupying 90% of the canvas, equal margins on all sides, all corners fully transparent. White symbol and white outline should stay sharp and legible at 24–32 pixels.
Absolutely NO colored gradients, neon, iridescence, glow, bloom, drop shadow, bevel, 3D extrusion, highlights, badges except the specified small Caps arrow, extra border, captions, watermark or fake checkerboard. This must look like the user's new calm, flat monochrome design, NOT the previous colorful glass set.

## uppercase

Use case: background-extraction.
Asset type: one transparent monochrome input-method tray icon, square PNG.
Image 1 is the edit target. Faithfully extract ONLY the BOTTOM-LEFT icon: the plain white capital A centered in the thin white rounded-square border, INCLUDING the small white outline upward arrow at the bottom-right INSIDE the frame. Do not redesign or embellish.
Remove the entire green photographic/presentation background, all lower captions and the other three icons. Preserve the simple source design: plain white symbol, smooth thin white rounded-square outline, flat front view, original symbol proportions and placement. The only possible fill inside the square is a very faint neutral translucent gray matching the source's understated frosted interior. Outside the rounded-square outline must be actual alpha transparency.
Tight, consistent square framing with the rounded-square border occupying 90% of the canvas, equal margins on all sides, all corners fully transparent. White symbol and white outline should stay sharp and legible at 24–32 pixels.
Absolutely NO colored gradients, neon, iridescence, glow, bloom, drop shadow, bevel, 3D extrusion, highlights, badges except the specified small Caps arrow, extra border, captions, watermark or fake checkerboard. This must look like the user's new calm, flat monochrome design, NOT the previous colorful glass set.

## direct

Use case: background-extraction.
Asset type: one transparent monochrome input-method tray icon, square PNG.
Image 1 is the edit target. Faithfully extract ONLY the BOTTOM-RIGHT icon: the plain front-facing white line-art keyboard, complete with small white key squares and spacebar, centered in the thin white rounded-square border. Do not redesign or embellish.
Remove the entire green photographic/presentation background, all lower captions and the other three icons. Preserve the simple source design: plain white symbol, smooth thin white rounded-square outline, flat front view, original symbol proportions and placement. The only possible fill inside the square is a very faint neutral translucent gray matching the source's understated frosted interior. Outside the rounded-square outline must be actual alpha transparency.
Tight, consistent square framing with the rounded-square border occupying 90% of the canvas, equal margins on all sides, all corners fully transparent. White symbol and white outline should stay sharp and legible at 24–32 pixels.
Absolutely NO colored gradients, neon, iridescence, glow, bloom, drop shadow, bevel, 3D extrusion, highlights, badges except the specified small Caps arrow, extra border, captions, watermark or fake checkerboard. This must look like the user's new calm, flat monochrome design, NOT the previous colorful glass set.

## Transparent interior refinement (latin / uppercase / direct)

Use case: background-extraction. Image 1 is the edit target.
Make one precise change: remove ALL gray fill and ALL soft illumination from this icon. Keep only the existing white rounded-square contour and the existing white symbol/keyboard/Caps arrow, with their exact shape, position and proportions.
The entire interior of the square must be completely transparent alpha=0 except where the white glyph and white outlines actually exist. The holes inside the glyph/keys must also be alpha=0. The entire exterior must be alpha=0, remove every stray speck and every halo. No frosted/gray/white panel fill at all. No glow, shadows, bevel or any background.
Sharp flat opaque white shapes with only narrow anti-aliased edge pixels. Transparent foreground stencil, like a minimal monochrome system icon. Retain the current square canvas, subject position and rounded-square border. Do not change the symbol, do not add text.

