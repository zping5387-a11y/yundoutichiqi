# -*- coding: utf-8 -*-
"""Office 文稿抽取测试：docx / pptx 文字、顺序、去重与错误提示

流程：gen_office_samples.py 生成样例 → build/offdoc_test.exe 抽取 → 断言
  docx：多 run 合并、&amp; 实体、<w:br/> 换行、<w:tab/> 制表、
        表格单元格成行、修订删除文本不出现、AlternateContent 只出现一次
  pptx：12 页按页码顺序（slide10 在 slide9 后）、备注页文本不出现
  fake：不是 zip → 失败并给出中文原因（退出码 1）
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "offdoc_test.exe")
SAMPLES = os.path.join(ROOT, "build", "samples")
PY = sys.executable
ok = True


def run(args):
    p = subprocess.run([EXE] + args, capture_output=True, text=True, encoding="utf-8")
    return p.returncode, p.stdout


def block(out, name):
    """取出某个文件的抽取结果段落（首行是完整路径，按文件名匹配）"""
    parts = re.split(r"(?m)^=== ", out)
    for b in parts:
        lines = b.splitlines()
        if lines and name in lines[0]:
            return "\n".join(lines[1:])
    return ""


# 1) 生成样例
subprocess.run([PY, os.path.join(ROOT, "tools", "gen_office_samples.py")], check=True,
               stdout=subprocess.DEVNULL)

docx = os.path.join(SAMPLES, "demo.docx")
pptx = os.path.join(SAMPLES, "demo.pptx")
fake = os.path.join(SAMPLES, "fake.docx")

rc, out = run([docx, pptx])
if rc != 0:
    print("FAIL: 抽取成功文件时返回非 0（rc=%d）" % rc)
    print(out)
    ok = False

d = block(out, "demo.docx")
p = block(out, "demo.pptx")

print("---- docx 抽取结果 ----")
print(d.strip())

checks_docx = [
    ("多 run 合并成一段", "同学们上课 今天我们讲阿基米德原理" in d),
    ("实体解码 & < > 与数字实体", "A & B <test> 中文" in d),
    ("<w:br/> 变换行", "第一行\n第二行" in d),
    ("<w:tab/> 变制表符", "第二行\t制表后" in d),
    ("修订删除文本被忽略", "这段是删除的" not in d and "删除前的保留的" in d),
    ("表格单元格成行", "单元格A" in d and "单元格B" in d),
    ("AlternateContent 只出现一次", d.count("只出现一次") == 1),
    ("无残留 XML 标签", "<w:" not in d),
]
for name, cond in checks_docx:
    print(("  PASS  " if cond else "  FAIL  ") + name)
    ok = ok and cond

pages = re.findall(r"第(\d+)页标题", p)
expected = [str(i) for i in range(1, 13)]
print("---- pptx 页序 ----", pages)
if pages != expected:
    print("FAIL: PPT 页序不对，期望 %s" % expected)
    ok = False
if "备注不该出现" in p:
    print("FAIL: PPT 备注页文字被抽进来了")
    ok = False
if "第1页正文第一段" not in p or "第12页正文第二段" not in p:
    print("FAIL: PPT 正文段落缺失")
    ok = False

# 2) 假文件要给出中文错误
rc2, out2 = run([fake])
f = block(out2, "fake.docx").strip()
print("---- 假文件 ----")
print(f)
if rc2 == 0 or "!!!" not in f:
    print("FAIL: 非 zip 的 .docx 没有被拒绝")
    ok = False

print("RESULT:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
