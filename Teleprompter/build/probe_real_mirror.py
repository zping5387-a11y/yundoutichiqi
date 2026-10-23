# -*- coding: utf-8 -*-
"""真实镜像完整安装验证：不设 TP_MODEL_MIRROR，直连 hf-mirror.com 下完整模型。

约 736MB，本机实测 ~1.3MB/s，跑 10 分钟以上。只做一次终验，不进回归。
"""
import os
import shutil
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools"))
import model_mirror_e2e as M          # 复用启动/点击/等待的辅助函数


def main():
    tmp = tempfile.mkdtemp(prefix="ydtp_real_")
    app = M.prepare_app(tmp)
    model_dir = os.path.join(app, "models", M.MODEL_NAME)
    dialogs = [("下载语音模型", M.IDYES), ("语音模型安装完成", M.IDOK), ("还没有稿件", M.IDOK)]
    print("临时目录:", tmp)

    p, dlg = M.launch(app, "")        # 空 mirror -> 走内置 hf-mirror.com
    if not dlg:
        print("FAIL 主窗口没起来")
        return 1
    M.click(dlg, M.IDC_TAB_VOICE)
    time.sleep(1.0)
    print("初始状态:", M.wtext(M.u32.GetDlgItem(dlg, M.IDC_ST_SPEECH)))

    M.click(dlg, M.IDC_ST_SPEECH)
    t0 = time.time()
    last = ""
    while time.time() - t0 < 1800:
        M.pump(dialogs)
        st = M.wtext(M.u32.GetDlgItem(dlg, M.IDC_ST_SPEECH))
        if st != last:
            print("[%5.0fs] %s" % (time.time() - t0, st))
            last = st
        if "已安装" in st or "失败" in st:
            break
        time.sleep(1.0)

    files = sorted(os.listdir(model_dir)) if os.path.isdir(model_dir) else []
    total = sum(os.path.getsize(os.path.join(model_dir, f)) for f in files
                if os.path.isfile(os.path.join(model_dir, f)))
    print("结束状态:", last)
    print("落地文件:", files)
    print("合计 %.1f MB" % (total / 1048576.0))
    M.stop(p)

    ok = "已安装" in last and len(files) >= 5 and total > 700 * 1048576
    print("RESULT:", "PASS" if ok else "FAIL")
    if ok:
        shutil.rmtree(tmp, ignore_errors=True)
    else:
        print("保留临时目录以便排查:", tmp)
    return 0 if ok else 1


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    raise SystemExit(main())
