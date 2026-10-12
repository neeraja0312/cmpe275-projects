#!/usr/bin/env python3
"""Convert report/report.md to report/report.docx (opens in Word and Google Docs).

Usage (from Minis/mini1):  python3 scripts/make_report_docx.py
Needs pandoc on PATH and python-docx (pip install python-docx).
Pandoc does the conversion and embeds the images; this script then fixes the
styling pandoc leaves plain: fonts, table borders and header rows, image width,
captions and page numbers.
"""
import re
import subprocess
import sys
from pathlib import Path

from docx import Document
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Inches, Pt, RGBColor

ROOT = Path(__file__).resolve().parent.parent
REPORT = ROOT / "report"
SRC = REPORT / "report.md"
OUT = REPORT / "report.docx"
BODY_FONT = "Times New Roman"
MONO_FONT = "Menlo"
MAX_IMAGE_WIDTH = Inches(6.5)


def run_pandoc():
    # implicit_figures off: images stay inline and our own italic caption lines are the captions
    subprocess.run(
        ["pandoc", SRC.name, "-f", "markdown-implicit_figures", "-t", "docx", "-o", OUT.name, "--resource-path", "."],
        cwd=REPORT, check=True)


def set_font(style, name, size=None, bold=None, color=None):
    style.font.name = name
    rpr = style.element.get_or_add_rPr()
    rfonts = rpr.find(qn("w:rFonts"))
    if rfonts is None:
        rfonts = OxmlElement("w:rFonts")
        rpr.append(rfonts)
    for attr in ("w:ascii", "w:hAnsi", "w:cs", "w:eastAsia"):
        rfonts.set(qn(attr), name)
    for attr in ("w:asciiTheme", "w:hAnsiTheme", "w:cstheme", "w:eastAsiaTheme"):
        if rfonts.get(qn(attr)) is not None:
            del rfonts.attrib[qn(attr)]
    if size:
        style.font.size = Pt(size)
    if bold is not None:
        style.font.bold = bold
    if color is not None:
        style.font.color.rgb = color


def style_document(doc):
    for sec in doc.sections:
        sec.page_width, sec.page_height = Inches(8.5), Inches(11)
        sec.left_margin = sec.right_margin = Inches(1)
        sec.top_margin = sec.bottom_margin = Inches(1)
    styles = doc.styles
    black = RGBColor(0, 0, 0)
    for name in ("Normal", "Body Text", "First Paragraph", "Compact"):
        if name in [s.name for s in styles]:
            set_font(styles[name], BODY_FONT, 11)
    for name in ("Body Text", "First Paragraph"):
        if name in [s.name for s in styles]:
            pf = styles[name].paragraph_format
            pf.space_before, pf.space_after, pf.line_spacing = Pt(0), Pt(8), 1.1
    for name, size in (("Heading 1", 16), ("Heading 2", 13), ("Heading 3", 12)):
        if name in [s.name for s in styles]:
            set_font(styles[name], BODY_FONT, size, bold=True, color=black)
            pf = styles[name].paragraph_format
            pf.space_before, pf.space_after, pf.keep_with_next = Pt(14 if size > 12 else 10), Pt(6), True
    if "Title" in [s.name for s in styles]:
        set_font(styles["Title"], BODY_FONT, 20, bold=True, color=black)
    if "Verbatim Char" in [s.name for s in styles]:
        set_font(styles["Verbatim Char"], MONO_FONT, 9.5)
    if "Hyperlink" in [s.name for s in styles]:
        styles["Hyperlink"].font.color.rgb = RGBColor(0x1F, 0x4E, 0x9A)


def cell_borders(table):
    tbl = table._tbl
    tblPr = tbl.tblPr
    borders = OxmlElement("w:tblBorders")
    for edge in ("top", "left", "bottom", "right", "insideH", "insideV"):
        el = OxmlElement(f"w:{edge}")
        el.set(qn("w:val"), "single")
        el.set(qn("w:sz"), "4")
        el.set(qn("w:space"), "0")
        el.set(qn("w:color"), "808080")
        borders.append(el)
    old = tblPr.find(qn("w:tblBorders"))
    if old is not None:
        tblPr.remove(old)
    tblPr.append(borders)
    # full page width, fixed layout off so Word fits columns to content
    tblW = tblPr.find(qn("w:tblW"))
    if tblW is None:
        tblW = OxmlElement("w:tblW")
        tblPr.append(tblW)
    tblW.set(qn("w:type"), "pct")
    tblW.set(qn("w:w"), "5000")
    # cell padding
    mar = OxmlElement("w:tblCellMar")
    for edge, w in (("top", 40), ("left", 80), ("bottom", 40), ("right", 80)):
        el = OxmlElement(f"w:{edge}")
        el.set(qn("w:w"), str(w))
        el.set(qn("w:type"), "dxa")
        mar.append(el)
    tblPr.append(mar)


def shade(cell, fill):
    tcPr = cell._tc.get_or_add_tcPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), fill)
    tcPr.append(shd)


def style_tables(doc):
    for table in doc.tables:
        cell_borders(table)
        table.alignment = WD_TABLE_ALIGNMENT.CENTER
        for r_i, row in enumerate(table.rows):
            if r_i == 0:
                trPr = row._tr.get_or_add_trPr()
                hdr = OxmlElement("w:tblHeader")
                trPr.append(hdr)
            cant = OxmlElement("w:cantSplit")
            row._tr.get_or_add_trPr().append(cant)
            for cell in row.cells:
                if r_i == 0:
                    shade(cell, "E7E6E6")
                for para in cell.paragraphs:
                    if r_i < len(table.rows) - 1:
                        para.paragraph_format.keep_with_next = True  # keep the table on one page
                    para.paragraph_format.space_after = Pt(0)
                    para.paragraph_format.space_before = Pt(0)
                    para.paragraph_format.line_spacing = 1.0
                    for run in para.runs:
                        run.font.size = Pt(9)
                        run.font.name = BODY_FONT
                        if r_i == 0:
                            run.font.bold = True


CAPTION_RE = re.compile(r"^(Figure|Table) \d+\.")


def style_paragraphs(doc):
    paras = doc.paragraphs
    for i, p in enumerate(paras):
        text = p.text.strip()
        has_image = bool(p._p.xpath(".//pic:pic"))
        if has_image:
            p.alignment = WD_ALIGN_PARAGRAPH.CENTER
            p.paragraph_format.keep_with_next = True
            p.paragraph_format.space_before = Pt(6)
            p.paragraph_format.space_after = Pt(2)
        elif CAPTION_RE.match(text):
            p.alignment = WD_ALIGN_PARAGRAPH.LEFT
            p.paragraph_format.space_after = Pt(12)
            for run in p.runs:
                run.font.size = Pt(9.5)
                run.font.italic = True


def fit_images(doc):
    """Scale every figure to the full text width (aspect ratio kept)."""
    count = 0
    for shape in doc.inline_shapes:
        count += 1
        ratio = MAX_IMAGE_WIDTH / shape.width
        shape.width = int(shape.width * ratio)
        shape.height = int(shape.height * ratio)
    return count


def add_page_numbers(doc):
    for sec in doc.sections:
        p = sec.footer.paragraphs[0] if sec.footer.paragraphs else sec.footer.add_paragraph()
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        run = p.add_run()
        for kind, txt in (("begin", None), (None, "PAGE"), ("end", None)):
            if kind:
                el = OxmlElement("w:fldChar")
                el.set(qn("w:fldCharType"), kind)
            else:
                el = OxmlElement("w:instrText")
                el.set(qn("xml:space"), "preserve")
                el.text = txt
            run._r.append(el)
        run.font.size = Pt(9)


def main():
    if not SRC.exists():
        sys.exit(f"missing {SRC}")
    run_pandoc()
    doc = Document(str(OUT))
    style_document(doc)
    n_img = fit_images(doc)
    style_paragraphs(doc)
    style_tables(doc)
    add_page_numbers(doc)
    # first heading becomes the title
    for p in doc.paragraphs:
        if p.style.name == "Heading 1":
            p.style = doc.styles["Title"] if "Title" in [s.name for s in doc.styles] else p.style
            p.alignment = WD_ALIGN_PARAGRAPH.CENTER
            break
    doc.save(str(OUT))
    print(f"wrote {OUT}: {len(doc.tables)} tables, {n_img} images, {len(doc.paragraphs)} paragraphs")


if __name__ == "__main__":
    main()
