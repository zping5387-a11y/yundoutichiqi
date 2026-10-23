# -*- coding: utf-8 -*-
"""生成最终发布包（便携版）。

产物：
    output/YunDouTeleprompter-<版本>-win64/     可直接运行的目录
    output/YunDouTeleprompter-<版本>-win64.zip  同内容压缩包
    output/SHA256SUMS.txt                       校验值

发布内容 = exe + 3 个私有 DLL + 说明文件（约 24MB）。
**默认不带离线语音模型**：程序第一次要用跟读时自己下载（约 740MB），
所以安装包很小。加 --with-model 可以打一个模型齐全的离线包（约 780MB）。
exe 为静态 MFC/CRT，其余依赖均为系统 DLL，因此整个目录拷贝到别的机器即可运行。

用法：
    python tools/make_release.py                # 默认 1.0.0，不带模型
    python tools/make_release.py 1.0.1          # 指定版本号
    python tools/make_release.py 1.0.0 --with-model   # 带上模型（离线安装）
"""
import hashlib
import os
import shutil
import sys
import time
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
OUT = os.path.join(ROOT, "output")

EXE = "TeacherPrompter.exe"
DLLS = [
    "sherpa-onnx-c-api.dll",
    "onnxruntime.dll",
    "onnxruntime_providers_shared.dll",
]
MODEL_DIR = "sherpa-onnx-streaming-zipformer-zh-xlarge-int8-2025-06-30"
MODEL_FILES = [
    "encoder.int8.onnx",
    "decoder.onnx",
    "joiner.int8.onnx",
    "tokens.txt",
    "bpe.model",
    "README.md",
]
MODEL_OPTIONAL_DIRS = ["test_wavs"]

QUICKSTART = """芸豆提词器 {ver} · 便携版
========================================

一、运行
  解压到任意目录（路径别太深、别放在只读位置），双击 {exe}。
  程序是绿色便携版：不写注册表、不装服务，配置保存在 exe 同级的
  TeacherPrompter.ini（写不进去时自动改用 %APPDATA%\\TeacherPrompter\\settings.ini）。

  系统要求：Windows 10 2004 及以上 / Windows 11，64 位。

  首次运行若弹出「Windows 已保护你的电脑」（SmartScreen），点「更多信息 → 仍要运行」。
  程序没有数字签名，部分安全软件可能询问，选择允许即可。

二、三步上手
  1. 「稿件」页 → 添加稿件：选 txt / md / srt / docx / pptx，
     或「从剪贴板」粘贴，也可以直接把文件拖进窗口。
  2. 「语音」页 → 选麦克风 → 点「开始跟读」。
     第一次会提示下载语音模型（约 740 MB，见第四节），按提示下载即可，
     装好后会自动继续开始跟读。
  3. 浮层（芸豆提词）跟着你讲的位置自动滚动；浮层本身对录屏/共享屏幕是隐形的，
     投影和会议观众都看不到。

三、常用快捷键（任何程序在前台时都可用）
  Ctrl+Alt+H      显示 / 隐藏提词浮层
  Ctrl+Alt+M      显示 / 隐藏控制面板（隐藏后可从托盘图标找回）
  Ctrl+Alt+V      开始 / 停止跟读
  Ctrl+Alt+C      打开 / 关闭语音命令（「下一页 / 上一页 / 回到开头」）
  Ctrl+Alt+L      锁定 / 调整浮层位置与大小
  Ctrl+Alt+B      当前行高亮开关
  Ctrl+Alt+Home   回到稿件开头
  Ctrl+Alt+Q      退出程序

四、语音模型：第一次用跟读时下载（安装包不含模型）
  程序按需下载识别模型（约 740 MB），装好一次以后一直用。
  · 入口：点「开始跟读」，或在「语音」页点状态行的「点这里下载」。
  · 下载期间可以继续用自动滚动提词；状态行显示百分比、速度、剩余时间。
  · 点状态行可暂停，下次点它从断点续传，已下载的部分不会重来。
  · 按文件下载（tokens / bpe / joiner / decoder / encoder），某个文件坏了只重下那一个，
    已经下好的文件会跳过，不用整包重来。
  · 安装位置（两处都认；模型目录名不要改）：
        <程序目录>\\models\\{model}\\                   ← 想随身带 U 盘就用这里
        %APPDATA%\\TeacherPrompter\\models\\{model}\\   ← 程序目录不可写时自动用它
  · 下载慢 / 没有外网：
      - 设置环境变量 TP_MODEL_MIRROR 指向自建镜像目录（程序在后面自动拼文件名，
        例如 https://内网文件服务器/models）；
      - 或从已装好的机器把 models 整个目录拷过来（最省事）；
      - 或手动打开 https://hf-mirror.com/csukuangfj/{model}
        逐个下载 encoder.int8.onnx、decoder.onnx、joiner.int8.onnx、tokens.txt、bpe.model，
        放进 models\\{model}\\ 下。
  · 暂时不装模型也能上课：Ctrl+Alt+P 开自动滚动；
    或「语音」页把引擎切到「系统识别」兜底（需装系统中文语音包，准确率较低）。

五、常见问题
  · 跟读不启动、电平条不动：检查「语音」页里选的麦克风是不是会议软件用的那只；
    程序用 WASAPI 采集，不跟随系统「默认通信设备」。
  · 浮层被 PPT 全屏放映盖住：改用窗口化放映 / 演示者视图，或把提词器放副屏。
  · 截屏或录屏里看得到浮层：Win10 2004 以下不支持防录屏，会自动降级为普通置顶。
  · 需要临时截取浮层做排查：设置环境变量 TP_CAPTURE_ALLOWED=1 再启动。

六、卸载
  直接删除整个目录即可。若配置写到了别处，删掉
  %APPDATA%\\TeacherPrompter 整个文件夹。

更详细的功能说明见同目录 README.md。
"""


def human(n):
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return "%.1f %s" % (n, unit) if unit != "B" else "%d B" % n
        n /= 1024.0


def sha256(path, chunk=1 << 20):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(chunk)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def collect(ver, with_model=False):
    """(源绝对路径, 包内相对路径) 列表"""
    items = []
    for name in [EXE] + DLLS:
        items.append((os.path.join(BUILD, name), name))

    if with_model:
        mdir = os.path.join(ROOT, "models", MODEL_DIR)
        for name in MODEL_FILES:
            src = os.path.join(mdir, name)
            if os.path.exists(src):
                items.append((src, "models/%s/%s" % (MODEL_DIR, name)))
        for d in MODEL_OPTIONAL_DIRS:
            src_dir = os.path.join(mdir, d)
            if os.path.isdir(src_dir):
                for f in sorted(os.listdir(src_dir)):
                    p = os.path.join(src_dir, f)
                    if os.path.isfile(p):
                        items.append((p, "models/%s/%s/%s" % (MODEL_DIR, d, f)))

    items.append((os.path.join(ROOT, "README.md"), "README.md"))
    return items


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    with_model = "--with-model" in sys.argv
    ver = args[0] if args else "1.0.0"
    pkg = "YunDouTeleprompter-%s-win64%s" % (ver, "-full" if with_model else "")
    pkg_dir = os.path.join(OUT, pkg)
    zip_path = os.path.join(OUT, pkg + ".zip")

    # 1) 前置检查
    missing = []
    for name in [EXE] + DLLS:
        if not os.path.exists(os.path.join(BUILD, name)):
            missing.append(name)
    if with_model:
        for name in MODEL_FILES[:4]:
            if not os.path.exists(os.path.join(ROOT, "models", MODEL_DIR, name)):
                missing.append(name)
    if missing:
        print("FAIL: build 目录缺少：" + ", ".join(missing))
        return 1

    exe_path = os.path.join(BUILD, EXE)
    exe_time = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(os.path.getmtime(exe_path)))
    # 源码比 exe 新 → 提醒（不阻断，exe 可能是刚构建的）
    newest_src, newest_t = 0, 0
    for dirpath, _dirs, files in os.walk(os.path.join(ROOT, "src")):
        for f in files:
            t = os.path.getmtime(os.path.join(dirpath, f))
            if t > newest_t:
                newest_t, newest_src = t, f
    stale = newest_t > os.path.getmtime(exe_path) + 1

    # 2) 重建目录
    os.makedirs(OUT, exist_ok=True)
    if os.path.isdir(pkg_dir):
        shutil.rmtree(pkg_dir)
    os.makedirs(pkg_dir)

    items = collect(ver, with_model)
    total = 0
    for src, rel in items:
        dst = os.path.join(pkg_dir, rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
        total += os.path.getsize(dst)

    # 3) 说明文件（UTF-8 BOM + CRLF，记事本直接可读）
    quick = QUICKSTART.format(ver=ver, exe=EXE, model=MODEL_DIR)
    with open(os.path.join(pkg_dir, "QUICKSTART.txt"), "w",
              encoding="utf-8-sig", newline="\r\n") as f:
        f.write(quick)

    # 4) 文件清单 + 校验
    lines = ["芸豆提词器 %s · win64 便携版" % ver,
             "语音模型: %s" % ("已内置（离线安装）" if with_model else "不内置，首次使用时下载（约 740 MB）"),
             "构建时间（exe）: %s" % exe_time,
             "打包时间: %s" % time.strftime("%Y-%m-%d %H:%M:%S"),
             "",
             "文件清单："]
    for src, rel in items:
        dst = os.path.join(pkg_dir, rel.replace("/", os.sep))
        lines.append("  %12s  %s  %s" % (human(os.path.getsize(dst)), sha256(dst)[:16], rel))
    lines.append("  %12s  %s" % (human(total), "合计（未压缩）"))
    with open(os.path.join(pkg_dir, "MANIFEST.txt"), "w",
              encoding="utf-8-sig", newline="\r\n") as f:
        f.write("\n".join(lines) + "\n")

    print("发布目录: %s" % pkg_dir)
    print("文件数: %d | 未压缩大小: %s" % (len(items) + 2, human(total)))
    if stale:
        print("WARN: src 下有文件比 exe 新（%s），发布前建议重新构建" % newest_src)

    # 5) 打包 zip（大文件用低压缩级别，模型本身已 int8 量化，压不动）
    if os.path.exists(zip_path):
        os.remove(zip_path)
    t0 = time.time()
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=1) as z:
        for dirpath, _dirs, files in os.walk(pkg_dir):
            for f in files:
                p = os.path.join(dirpath, f)
                arc = os.path.relpath(p, pkg_dir).replace(os.sep, "/")
                z.write(p, arc)
    zsize = os.path.getsize(zip_path)
    print("压缩包: %s (%s, 耗时 %.1fs)" % (zip_path, human(zsize), time.time() - t0))

    # 6) 校验清单
    sums = [("%s  %s" % (sha256(zip_path), os.path.basename(zip_path)))]
    for name in [EXE] + DLLS:
        sums.append("%s  %s" % (sha256(os.path.join(pkg_dir, name)), "%s/%s" % (pkg, name)))
    with open(os.path.join(OUT, "SHA256SUMS.txt"), "w",
              encoding="utf-8-sig", newline="\r\n") as f:
        f.write("\n".join(sums) + "\n")
    print("校验清单: %s" % os.path.join(OUT, "SHA256SUMS.txt"))
    for s in sums:
        print("  " + s)
    return 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    raise SystemExit(main())
