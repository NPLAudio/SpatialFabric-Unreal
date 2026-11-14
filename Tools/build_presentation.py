"""Build the editable sound-designer deck and timed presenter guide.
Install Presentations/requirements.txt, then run python Tools/build_presentation.py.
All text, diagrams and speaker notes remain editable; no Office automation is required.
"""
import json
from pathlib import Path
from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_AUTO_SHAPE_TYPE, MSO_CONNECTOR
from pptx.util import Inches, Pt

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "Presentations"
BG = "0B1426"
PANEL = "14243C"
WHITE = "F0F5FF"
MUTED = "B5C9DF"
CYAN = "54D8F5"
PURPLE = "C09AFF"
AMBER = "FFBF69"


def rgb(color): return RGBColor.from_string(color)


def text(slide, value, x, y, w, h, size=24, color=WHITE, bold=False):
    shape = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    frame = shape.text_frame
    frame.word_wrap = True
    frame.margin_left = frame.margin_right = Inches(0.02)
    frame.margin_top = frame.margin_bottom = 0
    for i, line in enumerate(value.split("\n")):
        p = frame.paragraphs[0] if i == 0 else frame.add_paragraph()
        p.text = line
        p.font.name = "Aptos"
        p.font.size = Pt(size)
        p.font.bold = bold
        p.font.color.rgb = rgb(color)
        p.space_after = Pt(9)
    return shape


def box(slide, label, x, y, w, h, accent=CYAN, size=23):
    shape = slide.shapes.add_shape(MSO_AUTO_SHAPE_TYPE.ROUNDED_RECTANGLE, Inches(x), Inches(y), Inches(w), Inches(h))
    shape.fill.solid(); shape.fill.fore_color.rgb = rgb(PANEL)
    shape.line.color.rgb = rgb(accent); shape.line.width = Pt(1.5)
    shape.adjustments[0] = 0.12
    text(slide, label, x + 0.18, y + 0.17, w - 0.36, h - 0.25, size=size)
    return shape


def line(slide, x1, y1, x2, y2, color=CYAN, width=2):
    shape = slide.shapes.add_connector(MSO_CONNECTOR.STRAIGHT, Inches(x1), Inches(y1), Inches(x2), Inches(y2))
    shape.line.color.rgb = rgb(color); shape.line.width = Pt(width)


def cards(slide, bullets):
    count = len(bullets)
    if count <= 3:
        width = 11.95 / count
        for i, item in enumerate(bullets):
            x = 0.7 + i * (width + 0.08)
            box(slide, "", x, 2.3, width - 0.08, 3.8, [CYAN, PURPLE, AMBER][i % 3])
            text(slide, f"0{i + 1}", x + 0.22, 2.6, width - 0.5, 0.5, 30, [CYAN, PURPLE, AMBER][i % 3], True)
            text(slide, item, x + 0.22, 3.35, width - 0.52, 2.5, 25)
    else:
        for i, item in enumerate(bullets):
            x, y = 0.7 + (i % 2) * 6.1, 2.25 + (i // 2) * 2.0
            box(slide, item, x, y, 5.9, 1.75, CYAN if i % 2 == 0 else PURPLE, 23)


def chain(slide, labels, y, color):
    width = (11.9 - (len(labels) - 1) * 0.45) / len(labels)
    for i, label in enumerate(labels):
        x = 0.7 + i * (width + 0.45)
        box(slide, label, x, y, width, 1.15, color, 22)
        if i < len(labels) - 1:
            line(slide, x + width + 0.05, y + 0.58, x + width + 0.38, y + 0.58, color)


def body(slide, data):
    kind, bullets = data["kind"], data["bullets"]
    if kind == "title":
        slide.shapes.add_picture(str(ROOT / "assets/spatialfabric-unified.png"), Inches(0.75), Inches(2.2), width=Inches(8.4))
        text(slide, "CONTROL\n+\nAUDIO", 10, 2.5, 2.4, 2.5, 33, CYAN, True)
        text(slide, "\n".join(bullets), 0.75, 6.05, 11.8, 0.95, 22, MUTED)
    elif kind == "lanes":
        text(slide, "CONTROL / POSITION", 0.7, 2.2, 11, 0.4, 18, CYAN, True)
        chain(slide, ["Actor + stage", "OSC position data", "External renderer"], 2.75, CYAN)
        text(slide, "AUDIO / SAMPLES", 0.7, 4.25, 11, 0.4, 18, PURPLE, True)
        chain(slide, ["SoundWave cue", "Direct cue player", "OS device / channels"], 4.8, PURPLE)
        text(slide, bullets[2], 0.7, 6.35, 11.8, 0.55, 23, AMBER)
    elif kind == "space":
        box(slide, "", 0.95, 2.4, 5.2, 3.5)
        line(slide, 3.55, 5.3, 3.55, 2.9)
        line(slide, 1.45, 4.15, 5.65, 4.15)
        text(slide, "FRONT", 2.9, 2.65, 1.7, 0.4, 18, CYAN, True)
        text(slide, "RIGHT", 4.65, 4.25, 1.4, 0.4, 18, CYAN, True)
        dot = slide.shapes.add_shape(MSO_AUTO_SHAPE_TYPE.OVAL, Inches(3.42), Inches(4.02), Inches(.26), Inches(.26))
        dot.fill.solid(); dot.fill.fore_color.rgb = rgb(AMBER); dot.line.fill.background()
        text(slide, "CENTER", 2.8, 4.5, 1.9, 0.4, 18, AMBER)
        text(slide, "\n\n".join(bullets), 6.8, 2.5, 5.7, 3.9, 24)
    elif kind == "audio":
        chain(slide, ["SoundWave\nfile content", "Prepared PCM\nready samples", "Device worker\noutput buffer"], 2.5, PURPLE)
        text(slide, "\n".join(bullets[1:]), .75, 4.25, 11.8, 2.25, 25)
    elif kind == "channels":
        text(slide, "CORRESPONDING MAPPING (0)", .75, 2.25, 6, .4, 19, CYAN, True)
        chain(slide, ["Source 1 / 2", "Output 1 / 2"], 2.85, CYAN)
        text(slide, "SINGLE SELECTED OUTPUT (1…N)", .75, 4.35, 8, .4, 19, PURPLE, True)
        chain(slide, ["First source channel", "One chosen output"], 4.95, PURPLE)
        text(slide, bullets[2], .75, 6.35, 11.8, .5, 21, AMBER)
    elif kind == "transport":
        chain(slide, ["Prepare", "Play request", "Worker starts", "Finish / Stop"], 2.55, PURPLE)
        text(slide, "\n".join(bullets), .75, 4.05, 11.8, 2.5, 24)
    elif kind == "demo":
        for i, item in enumerate(bullets):
            text(slide, f"{i+1:02}", .75, 2.15 + i * .82, .65, .5, 24, CYAN, True)
            text(slide, item, 1.55, 2.15 + i * .82, 10.8, .7, 24)
    elif kind == "credits":
        text(slide, "\n".join(bullets), .75, 2.3, 11.8, 3.9, 25)
    else:
        cards(slide, bullets)


def build():
    data = json.loads((OUT / "slides.json").read_text(encoding="utf-8"))
    slides = data["slides"]
    assert sum(s["seconds"] for s in slides if s["section"] == "talk") == 900
    assert sum(s["seconds"] for s in slides if s["section"] == "demo") == 600
    deck = Presentation()
    deck.slide_width = Inches(13.3333); deck.slide_height = Inches(7.5)
    deck.core_properties.title = data["title"]
    deck.core_properties.subject = "15-minute sound-designer talk + 10-minute demonstration + Q&A"
    deck.core_properties.author = "SpatialFabric Contributors"
    deck.core_properties.last_modified_by = "SpatialFabric Contributors"
    deck.core_properties.keywords = "sound design, OSC, spatial control, direct playback, demonstration"
    elapsed = 0
    guide = ["# Presenter guide — From Cue to Space", "", "15-minute talk + 10-minute demo + open Q&A. See DEMO_RUNSHEET.md for the live procedure.", ""]
    for index, item in enumerate(slides):
        slide = deck.slides.add_slide(deck.slide_layouts[6])
        slide.background.fill.solid(); slide.background.fill.fore_color.rgb = rgb(BG)
        section = item["section"].upper()
        text(slide, "SPATIALFABRIC  /  " + section, .75, .27, 10.8, .3, 12, CYAN, True)
        text(slide, item["title"], .75, .78, 11.85, .85, 34, WHITE, True)
        text(slide, item["subtitle"], .75, 1.62, 11.85, .48, 20, MUTED)
        slide.shapes.add_picture(str(ROOT / "assets/spatialfabric-mark.png"), Inches(12.05), Inches(.22), height=Inches(.45))
        body(slide, item)
        label = "15 MIN TALK + 10 MIN DEMO + Q&A"
        if item["section"] == "talk":
            label = f"TALK  {elapsed//60:02}:{elapsed%60:02}–{(elapsed+item['seconds'])//60:02}:{(elapsed+item['seconds'])%60:02}"
            elapsed += item["seconds"]
        text(slide, label, .75, 7.06, 10.8, .25, 11, MUTED)
        text(slide, f"{index+1:02} / {len(slides):02}", 11.7, 7.06, 1.0, .25, 11, MUTED)
        slide.notes_slide.notes_text_frame.text = item["notes"]
        guide.extend([f"## {index+1:02}. {item['title']}", "", f"Section: {item['section']} · planned duration: {item['seconds']} seconds", "", item["notes"], ""])
    destination = OUT / "SpatialFabric_From_Cue_to_Space.pptx"
    deck.save(destination)
    (OUT / "PRESENTER_GUIDE.md").write_text("\n".join(guide), encoding="utf-8")
    print("Wrote", destination, "—", len(slides), "slides; talk 15:00; demo 10:00; Q&A open")


if __name__ == "__main__": build()
