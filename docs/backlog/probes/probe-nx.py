# NX10 真机探针：验证 Measure / 选择 / 单位，都走 NXOpen Python（与 C++ 插件同一套 API）
#
# 用法：NX10 → 工具(Tools) → 播放日志(Play Journal) → 选本文件
# 先不选任何对象跑一次，再点选一个面跑一次，对比输出。

import traceback
import NXOpen

def run():
    session = NXOpen.Session.GetSession()
    part = session.Parts.Work
    print('work part:', part is not None)
    if part is None:
        return

    print('measure manager:', hasattr(part, 'MeasureManager'))
    try:
        print('millimeter unit:', part.UnitCollection.FindObject('Millimeter') is not None)
        print('square mm unit:', part.UnitCollection.FindObject('SquareMillimeter') is not None)
    except Exception:
        traceback.print_exc()

    sel = NXOpen.UI.GetUI().SelectionManager
    n = sel.GetNumSelectedObjects()
    print('selected count:', n)
    for i in range(n):
        try:
            obj = sel.GetSelectedTaggedObject(i)
        except Exception:
            obj = sel.GetSelectedObject(i)
        print('  [%d] tag=%d class=%s' % (i, obj.Tag, type(obj).__name__))

try:
    run()
except Exception:
    traceback.print_exc()
