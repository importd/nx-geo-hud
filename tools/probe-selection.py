# NX10 悬停/选择探针 —— 判定 SelectionManager 里到底装的是什么
#
# 用法：在 NX10 里 工具(Tools) -> 播放日志(Play Journal) -> 选本文件
# 跑三次，把每次输出记下来：
#   A. 鼠标停在图形区空白处（不点）
#   B. 鼠标悬停在一个面上（不点，等出现 NX 自己的预选高亮）
#   C. 左键点一下那个面（点亮，变成已选中）
#
# 判定：
#   A=B=0，C=1  -> SelectionManager 只反映"已选中"，hover 拿不到（预期）
#   B=1         -> hover 会写进 selection list，可直接用它做悬浮（意外之喜）

import traceback
import NXOpen

def run():
    ui = NXOpen.UI.GetUI()
    sel = ui.SelectionManager
    n = sel.GetNumSelectedObjects()
    print('NUM=%d' % n)
    for i in range(n):
        try:
            obj = sel.GetSelectedTaggedObject(i)   # NX7.5.4+，NX8 后的推荐用法
        except Exception:
            obj = sel.GetSelectedObject(i)         # NX8 起弃用，兜底
        try:
            kind = type(obj).__name__
            tag = obj.Tag
        except Exception:
            kind = '<unreadable>'
            tag = -1
        print('  [%d] tag=%d class=%s' % (i, tag, kind))

try:
    run()
except Exception:
    traceback.print_exc()
