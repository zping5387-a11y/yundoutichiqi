# -*- coding: utf-8 -*-
"""生成测试用的 .docx / .pptx 样例（纯 zipfile 拼包，不需要装 Office）

  build/samples/demo.docx  Word 样例：多 run 同段、&amp; 实体、换行/制表、
                           表格单元格、修订删除文本（应被忽略）、
                           mc:AlternateContent(Fallback 副本应被忽略)
  build/samples/demo.pptx  PPT 样例：12 页（验证 slide10 排在 slide9 后面）、
                           备注页文本（不应出现）
  build/samples/fake.docx  不是 zip 的假文件（验证错误提示）
"""
import os
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "samples")

WORD_NS = ('xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" '
           'xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006" '
           'xmlns:wps="http://schemas.microsoft.com/office/word/2010/wordprocessingShape"')


def docx(path):
    doc = f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document {WORD_NS}><w:body>
  <w:p><w:pPr><w:jc w:val="center"/></w:pPr><w:r><w:t>同学们上课</w:t></w:r><w:r><w:t xml:space="preserve"> 今天我们讲</w:t></w:r><w:r><w:t>阿基米德原理</w:t></w:r></w:p>
  <w:p/>
  <w:p><w:r><w:t>A &amp; B &lt;test&gt; &#20013;文</w:t></w:r></w:p>
  <w:p><w:r><w:t>第一行</w:t><w:br/><w:t>第二行</w:t><w:tab/><w:t>制表后</w:t></w:r></w:p>
  <w:p><w:r><w:t>删除前的</w:t><w:del><w:r><w:delText>这段是删除的</w:delText></w:r></w:del><w:r><w:t>保留的</w:t></w:r></w:p>
  <w:tbl><w:tr>
    <w:tc><w:p><w:r><w:t>单元格A</w:t></w:r></w:p></w:tc>
    <w:tc><w:p><w:r><w:t>单元格B</w:t></w:r></w:p></w:tc>
  </w:tr></w:tbl>
  <mc:AlternateContent><mc:Choice Requires="wps"><w:p><w:r><w:t>只出现一次</w:t></w:r></w:p></mc:Choice>
    <mc:Fallback><w:p><w:r><w:t>只出现一次</w:t></w:r></w:p></mc:Fallback></mc:AlternateContent>
</w:body></w:document>"""
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml",
                   '<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
                   '<Default Extension="xml" ContentType="application/xml"/></Types>')
        z.writestr("word/document.xml", doc)


SLIDE = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<p:sld xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"
       xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main">
 <p:cSld><p:spTree>
  <p:sp><p:txBody>
    <a:p><a:r><a:t>第{n}页标题</a:t></a:r></a:p>
    <a:p><a:r><a:t>第{n}页正文第一段</a:t></a:r></a:p>
    <a:p><a:r><a:t>第{n}页正文第二段</a:t></a:r></a:p>
  </p:txBody></p:sp>
 </p:spTree></p:cSld>
</p:sld>"""

NOTE = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<p:notes xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"
         xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main">
 <p:cSld><p:spTree><p:sp><p:txBody><a:p><a:r><a:t>备注不该出现</a:t></a:r></a:p></p:txBody></p:sp></p:spTree></p:cSld>
</p:notes>"""


def pptx(path, pages=12):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml",
                   '<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
                   '<Default Extension="xml" ContentType="application/xml"/></Types>')
        # 故意打乱写入顺序，验证按页码排序而不是按 zip 条目顺序
        for n in [10, 2, 1, 11, 3, 12, 4, 5, 6, 7, 8, 9][:pages]:
            z.writestr("ppt/slides/slide%d.xml" % n, SLIDE.format(n=n))
        z.writestr("ppt/notesSlides/notesSlide1.xml", NOTE)


def main():
    os.makedirs(OUT, exist_ok=True)
    docx(os.path.join(OUT, "demo.docx"))
    pptx(os.path.join(OUT, "demo.pptx"))
    with open(os.path.join(OUT, "fake.docx"), "wb") as f:
        f.write(b"this is not a zip package at all\n" * 10)
    for name in ("demo.docx", "demo.pptx", "fake.docx"):
        p = os.path.join(OUT, name)
        print("%s  %d bytes" % (p, os.path.getsize(p)))


if __name__ == "__main__":
    main()
