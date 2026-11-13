#!/usr/bin/env python3
"""Rebuild the four-page report (ReportLab, Pillow, WenQuanYi font)."""
from pathlib import Path
from xml.sax.saxutils import escape
import re
from reportlab.pdfgen import canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, Image, PageBreak
from PIL import Image as PILImage

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'experiments/ocr_detect_2026-11-13/run01'
OUT=ROOT/'output/pdf/ESP32-P4_OCR检测实验_精简图表版.pdf'
OUT.parent.mkdir(parents=True, exist_ok=True)
raw=(SRC/'report.md').read_text()
blocks=re.findall(r'(?m)^\|.*(?:\n\|.*)*',raw)
tables=[]
for block in blocks:
 rows=[[x.strip().replace('–','-') for x in line.strip('|').split('|')] for line in block.splitlines()]
 tables.append([rows[0]]+rows[2:])
pdfmetrics.registerFont(TTFont('CJK','/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc',subfontIndex=0))
BLUE=colors.HexColor('#163C5A'); TEAL=colors.HexColor('#137C88'); GRAY=colors.HexColor('#526575'); LIGHT=colors.HexColor('#EDF4F7')
styles={
 'body':ParagraphStyle('body',fontName='CJK',fontSize=10,leading=16,textColor=BLUE,spaceAfter=9,wordWrap='CJK'),
 'small':ParagraphStyle('small',fontName='CJK',fontSize=8.4,leading=12,textColor=GRAY,spaceAfter=7,wordWrap='CJK'),
 'h1':ParagraphStyle('h1',fontName='CJK',fontSize=20,leading=28,textColor=BLUE,spaceAfter=14,wordWrap='CJK'),
 'h2':ParagraphStyle('h2',fontName='CJK',fontSize=12,leading=18,textColor=TEAL,spaceBefore=10,spaceAfter=8,wordWrap='CJK'),
 'cell':ParagraphStyle('cell',fontName='CJK',fontSize=8.4,leading=12,textColor=BLUE,wordWrap='CJK'),
 'head':ParagraphStyle('head',fontName='CJK',fontSize=8.4,leading=12,textColor=colors.white,wordWrap='CJK'),
}
story=[]
W=A4[0]-84

def p(s,kind='body'):
 story.append(Paragraph(escape(s),styles[kind]))
def title(n,s):
 p(f'{n:02d}  /  {s}','h1')
def table(rows,widths=None):
 data=[[Paragraph(escape(str(c)),styles['head' if i==0 else 'cell']) for c in r] for i,r in enumerate(rows)]
 t=Table(data,colWidths=widths,repeatRows=1,hAlign='LEFT')
 t.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),BLUE),('ROWBACKGROUNDS',(0,1),(-1,-1),[colors.white,LIGHT]),('VALIGN',(0,0),(-1,-1),'MIDDLE'),('LEFTPADDING',(0,0),(-1,-1),7),('RIGHTPADDING',(0,0),(-1,-1),7),('TOPPADDING',(0,0),(-1,-1),5),('BOTTOMPADDING',(0,0),(-1,-1),5),('LINEBELOW',(0,-1),(-1,-1),.6,colors.HexColor('#C6D6DF'))]))
 story.append(t);story.append(Spacer(1,9))
def img(name,width=W):
 path=SRC/name
 with PILImage.open(path) as im: w,h=im.size
 story.append(Image(str(path),width=width,height=width*h/w))
 story.append(Spacer(1,5))
def page(): story.append(PageBreak())

p('ESP32-P4  /  板上实测  /  2026.11.13','small')
p('OCR 检测尺寸对比实验报告','h1')
p('736×736 与 320×320 · 完整面单及单行文字','h2')
p('核心结论','h2')
p('检测输入缩小至 320×320 后，本组样本的平均检测总耗时由约 3.35-3.46 秒降至 0.60-0.61 秒，加速约 5.6 倍。减少单行字数（15 字变为 2-3 字）对检测速度的影响小于 0.5%。')
p('320 使用原有 INT8 检测模型与量化参数，通过构造参数覆盖输入尺寸，无需为本次实验重新导出或量化。面单的小字存在漏检、合框及识别结果变化，应结合业务画面选择尺寸。')
table([['样本','736×736 / ms','320×320 / ms','加速','耗时下降']]+[[r[0],r[5],tables[0][i+1][5],tables[1][(i+1)//2][1],tables[1][(i+1)//2][2]] for i,r in enumerate(tables[0]) if i>0 and i%2==1],[101,112,112,85,W-410])
img('latency_comparison.png')
p('图 1  检测前处理、模型推理与后处理的平均耗时。每个条件正式测量 20 次。','small')
p('计时仅覆盖 Detect；不包含模型首次加载、JPEG 解码、文字识别（Rec）和界面显示。这是当前配置下的实测性能，不代表完整 OCR 耗时或硬件优化极限。','small')
p('测试条件','h2')
p('ESP32-P4；CPU 360 MHz；PSRAM 200 MHz；ESP-IDF v5.5.4；Debug 优化；Core 1 单核。4 张样本 × 2 种尺寸，每组 20 次正式检测、6 次预热，分两批交换尺寸顺序执行。','small')
p('完整面单原图 750×1170；单行样本为 736×736 白底黑字、40 px 字号。三组单行样本均完整识别；面单检测框由 27 个变为 24 个，辅助识别保留项由 27 项变为 23 项。框数不能直接当作准确率。','small')
p('单行大字可优先评估 320；面单小字密集时建议保留 736。结论仅适用于本组样本，仍需用实际业务图片验证。','small')
page()
title(2,'完整耗时统计')
p('各阶段均值（毫秒）','h2')
table([['样本','尺寸','前处理','模型推理','后处理','检测总计']]+[[r[j] for j in [0,1,2,3,4,5]] for r in tables[0][1:]],[91,49,90,99,84,W-413])
p('分布与检测框数','h2')
table([['样本','尺寸','中位数 / ms','P95 / ms','框数范围']]+[[r[j] for j in [0,1,6,7,8]] for r in tables[0][1:]],[113,61,119,119,W-412])
p('尺寸变化的加速效果','h2')
table(tables[1],[W*.28,W*.24,W*.24,W*.24])
p('所有时间均来自板上实测；未使用面积比例或官方基准推算。总耗时包含函数内外少量开销，因此可能与三个阶段之和存在微小差异。','small')
p('P95 为每组 20 次正式测量排序后的第 19 项；框数范围取自正式检测。','small')
page()
title(3,'面单辅助识别 · 736×736')
p('检测框 27 个；置信度达到 0.5 的结果 27 项。以下为原始识别输出，保留识别中的错字与字符差异。','small')
table([['框号','识别文本','置信度','保留']]+[[r[j] for j in [1,2,3,4]] for r in tables[3][1:] if r[0]=='736'],[38,W-147,65,44])
p('识别在检测计时之外执行；文本与置信度仅用于辅助检查检测区域，不计入第 2 页的检测耗时。','small')
page()
title(4,'面单辅助识别 · 320×320')
p('检测框 24 个；置信度达到 0.5 的结果 23 项。编号 17 因置信度不足被过滤。','small')
table([['框号','识别文本','置信度','保留']]+[[r[j] for j in [1,2,3,4]] for r in tables[3][1:] if r[0]=='320'],[38,W-147,65,44])
p('识别说明与来源','h2')
p('辅助识别使用 48×320 INT16 Rec，从原图裁剪；置信度 ≥ 0.5 才保留。两种尺寸的框号不保证对应同一区域。数据来自 ocr_detect_2026-11-13/run01/report.md；识别输出保留原样。','small')
class NumberedCanvas(canvas.Canvas):
 def __init__(self,*a,**kw): super().__init__(*a,**kw);self._states=[]
 def showPage(self): self._states.append(dict(self.__dict__));self._startPage()
 def save(self):
  count=len(self._states)
  for state in self._states:
   self.__dict__.update(state);self.footer(count);super().showPage()
  super().save()
 def footer(self,total):
  self.setStrokeColor(colors.HexColor('#D5E2E9'));self.setLineWidth(.5)
  self.line(42,43,A4[0]-42,43)
  self.setFont('CJK',8);self.setFillColor(GRAY)
  self.drawString(42,29,'ESP32-P4 OCR  |  检测尺寸实验  |  2026-11-13')
  self.drawRightString(A4[0]-42,29,f'{self._pageNumber} / {total}')

doc=SimpleDocTemplate(str(OUT),pagesize=A4,rightMargin=42,leftMargin=42,topMargin=36,bottomMargin=57,title='ESP32-P4 OCR 检测实验（精简图表版）',author='ESP32 OCR 项目')
doc.build(story,canvasmaker=NumberedCanvas)
print(OUT)
