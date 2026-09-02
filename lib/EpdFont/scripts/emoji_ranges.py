"""Curated monochrome emoji subset shared by the built-in and SD font pipelines.

Single source of truth for the emoji codepoints covered by:

- the built-in `emoji_10_regular` font (via fontconvert.py --emoji-subset), and
- the downloadable SD-card `Emoji` family (via the `emoji` interval preset in
  fontconvert_sdcard.py).

Both are generated from the monochrome Noto Emoji TTF
(builtinFonts/source/NotoEmoji/NotoEmoji-Regular.ttf).  Full Noto Emoji
coverage would cost megabytes of glyph bitmaps, so this list covers the
high-density emoji blocks (smileys, gestures, hearts, animals, food, sports,
travel, objects, symbols) plus the codepoints common sequences are made of:

- U+FE0F (VS16) and U+200D (ZWJ): control codepoints with no bitmap; the
  renderer treats them as zero-width, so sequences decode without extra boxes.
- U+1F3FB..U+1F3FF skin-tone modifiers.
- U+1F1E6..U+1F1FF regional indicator symbols (country flags).

Rendering model: each codepoint is rendered independently (monochrome, no
ZWJ sequence merging), matching how the MeshCore chat renderer draws text.
"""

# (first, last) inclusive codepoint ranges, ascending and non-overlapping.
EMOJI_INTERVALS = [
    (0x200D, 0x200D),    # ZWJ
    (0x2600, 0x2601),    # sun, cloud
    (0x2603, 0x2604),    # snowman, comet
    (0x2614, 0x2615),    # umbrella, hot beverage
    (0x2639, 0x263A),    # frowning face, smiling face
    (0x267B, 0x267B),    # recycling
    (0x2694, 0x2694),    # crossed swords
    (0x2699, 0x2699),    # gear
    (0x26A0, 0x26A1),    # warning, high voltage
    (0x26BD, 0x26BE),    # soccer ball, baseball
    (0x26C4, 0x26C5),    # snowman without snow, sun behind cloud
    (0x26D4, 0x26D4),    # no entry
    (0x26F0, 0x26F0),    # mountain
    (0x26F5, 0x26F5),    # sailboat
    (0x26F7, 0x26F7),    # skier
    (0x2702, 0x2702),    # scissors
    (0x2705, 0x2705),    # white heavy check mark
    (0x2708, 0x2709),    # airplane, envelope
    (0x270A, 0x270D),    # fist, hand, victory, writing hand
    (0x2714, 0x2714),    # heavy check mark
    (0x2716, 0x2716),    # heavy multiplication x
    (0x2728, 0x2728),    # sparkles
    (0x2744, 0x2744),    # snowflake
    (0x274C, 0x274C),    # cross mark
    (0x274E, 0x274E),    # negative squared cross mark
    (0x2753, 0x2755),    # question marks, exclamation
    (0x2757, 0x2757),    # white exclamation
    (0x2763, 0x2764),    # heavy heart exclamation, red heart
    (0x2795, 0x2797),    # plus, minus, divide
    (0x27A1, 0x27A1),    # right arrow
    (0x27B0, 0x27B0),    # curly loop
    (0x27BF, 0x27BF),    # double curly loop
    (0x2B05, 0x2B07),    # left/up/down arrows
    (0x2B50, 0x2B50),    # white medium star
    (0x2B55, 0x2B55),    # hollow red circle
    (0xFE0F, 0xFE0F),    # VS16
    (0x1F004, 0x1F004),  # mahjong red dragon
    (0x1F0CF, 0x1F0CF),  # joker
    (0x1F18E, 0x1F18E),  # AB button
    (0x1F191, 0x1F19A),  # CL..VS buttons
    (0x1F1E6, 0x1F1FF),  # regional indicators (flags)
    (0x1F21A, 0x1F21A),  # squared CJK unified ideograph-7121
    (0x1F22F, 0x1F22F),  # squared CJK unified ideograph-6307
    (0x1F232, 0x1F23A),  # squared CJK ideographs block
    (0x1F250, 0x1F251),  # circled ideographs advantage/accept
    (0x1F300, 0x1F30A),  # cyclone .. ocean wave
    (0x1F30B, 0x1F30F),  # volcano .. globe
    (0x1F311, 0x1F31F),  # moon phases .. glowing star
    (0x1F32D, 0x1F344),  # hot dog .. mushroom
    (0x1F345, 0x1F35F),  # tomato .. french fries
    (0x1F360, 0x1F37C),  # roasted sweet potato .. baby bottle
    (0x1F380, 0x1F39F),  # ribbon .. admission tickets
    (0x1F3A0, 0x1F3BB),  # carousel .. violin
    (0x1F3C0, 0x1F3CB),  # basketball .. weight lifter
    (0x1F3CF, 0x1F3D4),  # cricket .. snow-capped mountain
    (0x1F3D5, 0x1F3FF),  # camping .. skin tone dark
    (0x1F400, 0x1F43F),  # rat .. rabbit face
    (0x1F440, 0x1F450),  # eyes .. open hands
    (0x1F451, 0x1F45F),  # crown .. athletic shoe
    (0x1F466, 0x1F469),  # boy .. woman (ZWJ sequence components)
    (0x1F476, 0x1F476),  # baby
    (0x1F47B, 0x1F480),  # ghost .. skull
    (0x1F483, 0x1F485),  # dancer .. nail polish
    (0x1F48B, 0x1F48F),  # kiss mark .. kiss
    (0x1F490, 0x1F493),  # bouquet .. beating heart
    (0x1F494, 0x1F49F),  # broken heart .. heart decoration
    (0x1F4A1, 0x1F4AF),  # light bulb .. hundred points
    (0x1F4B0, 0x1F4B9),  # money bag .. yen chart
    (0x1F4BB, 0x1F4C2),  # laptop .. open file folder
    (0x1F4C3, 0x1F4CF),  # page with curl .. straight ruler
    (0x1F4D0, 0x1F4DF),  # triangular ruler .. pager
    (0x1F4E0, 0x1F4EA),  # fax .. closed mailbox lowered flag
    (0x1F4EB, 0x1F4F0),  # mailbox raised flag .. newspaper
    (0x1F4F1, 0x1F4F7),  # mobile phone .. camera
    (0x1F4F8, 0x1F4FD),  # camera with flash .. film projector
    (0x1F500, 0x1F53D),  # shuffle .. down-pointing triangle
    (0x1F550, 0x1F567),  # clock faces
    (0x1F5A4, 0x1F5A4),  # black heart
    (0x1F5FA, 0x1F5FF),  # world map .. moai
    (0x1F600, 0x1F64F),  # grinning face .. folded hands
    (0x1F680, 0x1F68F),  # rocket .. bus stop
    (0x1F690, 0x1F6A2),  # minibus .. ship
    (0x1F6A3, 0x1F6AC),  # rowboat .. cigarette
    (0x1F6B0, 0x1F6B5),  # potable water .. mountain bicyclist
    (0x1F6B6, 0x1F6C0),  # pedestrian .. bath
    (0x1F6C1, 0x1F6C5),  # bathtub .. left luggage
    (0x1F6D0, 0x1F6D2),  # place of worship .. shopping cart
    (0x1F6E0, 0x1F6E2),  # hammer and wrench .. oil drum
    (0x1F6EB, 0x1F6EC),  # airplane departure/arrival
    (0x1F90D, 0x1F90E),  # white heart, brown heart
    (0x1F910, 0x1F919),  # zipper-mouth .. call me hand
    (0x1F91D, 0x1F91F),  # handshake .. love-you gesture
    (0x1F920, 0x1F92F),  # cowboy hat face .. exploding head
    (0x1F930, 0x1F93A),  # pregnant woman .. person fencing
    (0x1F940, 0x1F945),  # wilted flower .. goal net
    (0x1F947, 0x1F949),  # first/second/third place medals
    (0x1F950, 0x1F95F),  # croissant .. dumpling
    (0x1F960, 0x1F96F),  # fortune cookie .. bagel
    (0x1F970, 0x1F976),  # smiling face with hearts .. cold face
    (0x1F97A, 0x1F97C),  # pleading face .. lab coat
    (0x1F980, 0x1F99F),  # crab .. mosquito
    (0x1F9A0, 0x1F9A2),  # microbe .. swan
    (0x1F9C0, 0x1F9C2),  # cheese wedge .. salt
    (0x1F9D0, 0x1F9D0),  # face with monocle
    (0x1F9E0, 0x1F9E6),  # brain .. socks
    (0x1F9F0, 0x1F9F3),  # toolbox .. luggage
    (0x1FA70, 0x1FA74),  # ballet shoes .. thong sandal
    (0x1FA75, 0x1FA77),  # light blue/gray/pink hearts
    (0x1FA78, 0x1FA7A),  # drop of blood .. stethoscope
    (0x1FA80, 0x1FA86),  # yo-yo .. nesting dolls
    (0x1FA90, 0x1FA95),  # ringed planet .. banjo
]

EMOJI_CODEPOINT_COUNT = sum(end - start + 1 for start, end in EMOJI_INTERVALS)


def intervals_as_args():
    """`--additional-intervals` arguments for fontconvert.py."""
    return [f"0x{start:X},0x{end:X}" for start, end in EMOJI_INTERVALS]


if __name__ == "__main__":
    for i, (start, end) in enumerate(EMOJI_INTERVALS):
        assert start <= end, (start, end)
        if i:
            assert EMOJI_INTERVALS[i - 1][1] < start, "overlap or wrong order"
    print(f"{len(EMOJI_INTERVALS)} intervals, {EMOJI_CODEPOINT_COUNT} codepoints")
