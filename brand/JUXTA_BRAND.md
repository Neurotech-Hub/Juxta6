# JUXTA Brand Implementation Guide

This file is the source of truth for implementing the JUXTA visual identity in digital products.

The goal is not to recreate a brand-board image literally. The goal is to translate the identity into a clean, reusable, accessible design system.

---

## 1. Brand Principles

JUXTA should feel:

- Directional
- Modern
- Precise
- Bold
- Clear
- Forward-looking

Design should communicate momentum without becoming visually noisy.

Prefer:
- strong hierarchy
- generous spacing
- simple geometry
- controlled gradients
- crisp typography
- restrained use of glow and motion

Avoid:
- excessive decoration
- overly dense layouts
- novelty UI
- heavy 3D effects
- excessive shadows
- rainbow gradients
- busy photographic backgrounds behind the logo

---

## 2. Brand Architecture

### Primary wordmark

The primary brand name is:

`JUXTA`

Do not use `J6` as the brand name or logotype.

If the wordmark is rendered as text rather than an approved asset:
- use uppercase
- use a bold geometric sans-serif
- use generous but not exaggerated letter spacing
- do not stylize individual letters

### Primary symbol

The primary symbol is the rounded white interlocking mark containing the directional arrow.

Use the primary symbol for:
- app icons
- favicons when legible
- avatars
- compact navigation
- product surfaces where space is limited

### Secondary symbol

The compass-arrow mark may be used independently as a supporting graphic.

Use it for:
- decorative background patterns
- small UI accents
- motion cues
- section dividers
- branded illustration elements

Do not substitute the compass arrow for the main JUXTA identity when the brand name should be visible.

---

## 3. Core Color Tokens

Use these exact values unless the product already has a token system that requires semantic aliases.

```css
:root {
  --juxta-white: #FFFFFF;
  --juxta-navy: #0B0F3B;
  --juxta-blue: #2563EB;
  --juxta-violet: #8B5CF6;
  --juxta-magenta: #EC0DD9;
}
```

### Primary background

Default dark background:

`#0B0F3B`

Dark surfaces may vary slightly for elevation, but should remain visually within the same navy family.

Recommended surface tokens:

```css
--juxta-bg: #0B0F3B;
--juxta-surface-1: #111746;
--juxta-surface-2: #171E55;
--juxta-border: rgba(255,255,255,0.14);
```

### Light background

Default light background:

`#FFFFFF`

Use dark navy text on light backgrounds.

---

## 4. Brand Gradient

The gradient is a primary brand device.

Recommended gradient:

```css
--juxta-gradient: linear-gradient(
  90deg,
  #2563EB 0%,
  #8B5CF6 55%,
  #EC0DD9 100%
);
```

For large hero backgrounds, a darker gradient may begin with navy:

```css
--juxta-gradient-dark: linear-gradient(
  135deg,
  #0B0F3B 0%,
  #2563EB 45%,
  #8B5CF6 72%,
  #EC0DD9 100%
);
```

Use gradients for:
- primary CTA emphasis
- selected states
- hero accents
- branded illustration
- the compass-arrow mark
- large graphic moments

Do not use gradients for:
- body text
- form labels
- dense tables
- every card
- every button
- every border

The gradient should feel intentional, not ambient.

---

## 5. Typography

### Display / headings

Preferred:

`Space Grotesk`

Weights:
- 700 Bold
- 600 SemiBold

Fallback:

```css
font-family: "Space Grotesk", "Inter", "Helvetica Neue", Arial, sans-serif;
```

### UI / body

Preferred:

`Plus Jakarta Sans`

Weights:
- 400 Regular
- 500 Medium
- 600 SemiBold
- 700 Bold

Fallback:

```css
font-family: "Plus Jakarta Sans", "Inter", "Helvetica Neue", Arial, sans-serif;
```

### Recommended type scale

```text
H1: 72 / 80, Bold
H2: 48 / 56, SemiBold
H3: 32 / 40, SemiBold
H4: 24 / 32, SemiBold
Body Large: 18 / 28, Regular
Body: 16 / 24, Regular
Small: 14 / 20, Regular
Caption: 12 / 16, Medium
```

Responsive implementations should scale down proportionally.

Do not force desktop headline sizes onto mobile.

---

## 6. Layout and Spacing

Use a simple 8 px spacing system.

```text
4
8
16
24
32
48
64
96
128
```

Default principles:
- generous whitespace
- clear sections
- limited visual competition
- one dominant action per region
- strong alignment

Content width:

```css
--juxta-content-max: 1200px;
```

Typical page padding:

```text
Desktop: 48–64 px
Tablet: 32 px
Mobile: 20–24 px
```

---

## 7. Corners

JUXTA uses rounded geometry, but should not feel overly soft.

Recommended radii:

```css
--radius-sm: 8px;
--radius-md: 12px;
--radius-lg: 20px;
--radius-xl: 28px;
--radius-pill: 999px;
```

Use:
- `12–20 px` for cards
- pill radius for buttons
- larger radii sparingly for major hero containers

Avoid using large rounded corners on every element.

---

## 8. Shadows and Glow

Prefer subtle depth.

Recommended standard shadow:

```css
box-shadow: 0 8px 30px rgba(0,0,0,0.20);
```

Recommended branded glow:

```css
box-shadow:
  0 0 24px rgba(139,92,246,0.20),
  0 0 48px rgba(236,13,217,0.12);
```

Glow should be:
- localized
- soft
- secondary to hierarchy

Do not place glow around every component.

---

## 9. Buttons

### Primary CTA

Preferred treatment:
- gradient fill
- white text
- pill shape
- high contrast

```css
background: var(--juxta-gradient);
color: #FFFFFF;
border-radius: 999px;
```

### Secondary CTA

Preferred treatment:
- transparent or navy surface
- subtle white border
- white text

### Light theme primary CTA

Use:
- navy or brand gradient
- white text

### Interaction

Buttons should have:
- clear hover
- focus-visible ring
- pressed state
- disabled state

Do not rely on color alone to indicate state.

---

## 10. Cards

Default card style:
- dark navy surface on dark layouts
- thin low-contrast border
- moderate radius
- minimal shadow
- strong content hierarchy

Avoid:
- excessive glassmorphism
- bright gradients on every card
- nested card stacks
- heavy borders

Use gradient accents only for important cards or selected states.

---

## 11. Navigation

Navigation should be simple and high contrast.

Preferred desktop header:
- logo or symbol + `JUXTA`
- 3–5 primary links
- one emphasized CTA
- minimal visual decoration

Preferred mobile header:
- symbol or wordmark
- compact menu control
- no unnecessary secondary controls

Do not use the compass-arrow pattern behind navigation text.

---

## 12. Forms and Inputs

Inputs should prioritize clarity over visual branding.

Recommended:
- dark or white surface depending on theme
- 1 px subtle border
- 10–12 px radius
- strong focus state using blue/violet
- clearly separated labels and helper text

Do not use gradient borders for standard inputs.

---

## 13. Iconography

Use clean geometric icons with consistent stroke weight.

Preferred:
- simple line icons
- rounded joins
- minimal detail

Use the compass-arrow brand mark only as a branded element, not as a replacement for unrelated functional icons.

---

## 14. Pattern System

The compass-arrow mark may be repeated as a background pattern.

Approved pattern modes:

### Sparse
Best for:
- page backgrounds
- cards
- UI surfaces

Rules:
- low density
- low opacity
- preserve large empty areas

### Dense
Best for:
- hero backgrounds
- event graphics
- campaign moments

Use sparingly.

### Oversized
Best for:
- hero sections
- large section transitions
- editorial layouts

Crop the arrow boldly rather than repeating it many times.

### Tone-on-tone
Best for:
- subtle dark backgrounds
- dashboards
- application chrome

Use navy/blue shapes with low contrast.

### Diagonal
Best for:
- motion
- promotional surfaces
- high-energy sections

Do not combine multiple pattern modes in the same section.

---

## 15. Graphic Elements

Approved supporting devices:

- navy-to-blue-to-magenta gradients
- soft radial glows
- diagonal cuts
- subtle grids
- thin dividers
- oversized compass-arrow crops

Use at most 1–2 supporting devices per section.

The interface should remain functional before decorative layers are added.

---

## 16. Web Application Guidance

For web and product UI, prioritize usability over brand spectacle.

### Recommended hierarchy

1. Functional content
2. Typography and spacing
3. Color hierarchy
4. Logo / brand marks
5. Decorative graphics

### Hero sections

Recommended structure:
- JUXTA wordmark or lockup
- one strong headline
- short supporting text
- one primary CTA
- optional secondary CTA
- one dominant graphic or arrow treatment

### Dashboards

Use:
- dark navy base
- neutral card surfaces
- white text
- blue/violet for active state
- magenta only for high-value emphasis
- gradients sparingly

### Marketing pages

Use:
- larger typography
- stronger gradient treatments
- oversized arrow graphics
- more generous whitespace

### Documentation / utility pages

Reduce decorative treatments.
Use brand primarily through:
- typography
- color tokens
- logo placement
- buttons
- navigation

---

## 17. Accessibility

Accessibility overrides decorative brand preferences.

Required:
- WCAG AA contrast for body text
- visible keyboard focus
- semantic HTML
- reduced-motion support
- meaningful alt text
- no text embedded in generated imagery when HTML text can be used instead

Do not place white text directly over the brightest violet/magenta gradient areas without verifying contrast.

---

## 18. Motion

Motion should reinforce direction.

Preferred:
- subtle upward/rightward movement
- soft gradient shifts
- arrow translation
- short fades
- modest scale transitions

Typical duration:

```text
120–180 ms: controls
200–300 ms: cards / panels
300–500 ms: larger branded transitions
```

Use ease-out for entrances.

Respect:

```css
@media (prefers-reduced-motion: reduce)
```

Avoid:
- constant pulsing
- bouncing
- large parallax
- continuous background animation

---

## 19. Logo Usage Rules

Always:
- preserve aspect ratio
- use approved colors
- maintain clear space
- keep the logo legible
- use the official asset when available

Never:
- stretch
- rotate
- recolor the white primary symbol
- add arbitrary outlines
- add drop shadows to the wordmark
- place over visually noisy imagery
- recreate the mark from a font
- use `J6` as the brand name

Clear space should be at least the diameter of the circular interior element of the primary symbol.

---

## 20. Asset Priority

When implementing the brand, use assets in this order:

1. Official supplied logo asset
2. Official supplied compass-arrow asset
3. CSS implementation of approved colors/gradients
4. CSS pattern or decorative geometry
5. Generated imagery only when necessary

Never regenerate the logo with AI when an official asset exists.

---

## 21. Component Implementation Rules

When applying this brand to an existing codebase:

1. Audit the current design system before editing.
2. Identify global tokens, theme files, and reusable components.
3. Implement JUXTA tokens centrally.
4. Update typography globally.
5. Update buttons, inputs, cards, navigation, and layout primitives.
6. Update representative screens first.
7. Visually verify in-browser.
8. Only then propagate to remaining screens.
9. Avoid page-specific CSS when a reusable token or component solves the problem.
10. Preserve existing product functionality.

Do not perform a broad visual rewrite before understanding the current component architecture.

---

## 22. Recommended Design Tokens

```css
:root {
  --juxta-white: #FFFFFF;
  --juxta-navy: #0B0F3B;
  --juxta-blue: #2563EB;
  --juxta-violet: #8B5CF6;
  --juxta-magenta: #EC0DD9;

  --juxta-bg: #0B0F3B;
  --juxta-surface-1: #111746;
  --juxta-surface-2: #171E55;

  --juxta-text: #FFFFFF;
  --juxta-text-muted: rgba(255,255,255,0.72);
  --juxta-border: rgba(255,255,255,0.14);

  --juxta-gradient:
    linear-gradient(90deg, #2563EB 0%, #8B5CF6 55%, #EC0DD9 100%);

  --juxta-gradient-dark:
    linear-gradient(135deg, #0B0F3B 0%, #2563EB 45%, #8B5CF6 72%, #EC0DD9 100%);

  --radius-sm: 8px;
  --radius-md: 12px;
  --radius-lg: 20px;
  --radius-xl: 28px;
  --radius-pill: 999px;

  --space-1: 4px;
  --space-2: 8px;
  --space-3: 16px;
  --space-4: 24px;
  --space-5: 32px;
  --space-6: 48px;
  --space-7: 64px;
  --space-8: 96px;
  --space-9: 128px;

  --juxta-content-max: 1200px;
}
```

---

## 23. Final Decision Rule

When uncertain, choose the simpler option.

A JUXTA interface should feel branded because of:
- typography
- spacing
- hierarchy
- color
- precise use of the symbol and compass arrow

It should not feel branded because every surface contains decoration.

Clarity first.
Direction second.
Decoration last.
