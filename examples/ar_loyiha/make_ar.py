# AsiaCAD — sinov uchun arxitektura (АР) loyihasi: bitta faylda qavat rejalari va kesim (sintetik, mijoz fayli emas).
#  * mm da, o'q pufakchalari "1".."4" / "А".."В", sarlavhalar: "План 1-го этажа ...", "План типового этажа (2-5) ...",
#    "План кровли", "Разрез 1-1" (belgilar ±0.000 … +16.500, parapet +17.400)
#  * qatlamlar: "Колонны", "Кладка_наружная" (to'siq devor), "Перегородки", "ЛЕСТНИЦА", "ОСИ", "Надписи"
#  * deraza / eshik bloklari "ОК-1", "ДВ-1"
# Ishga tushirish: python3 make_ar.py  ->  ar_loyiha.dxf
import ezdxf, os
doc = ezdxf.new('R2000', setup=True)
doc.header['$INSUNITS'] = 4
ds = doc.dimstyles.new('M100')
ds.dxf.dimlfac = 1.0
ds.dxf.dimtxt = 250
for n in ['ОСИ', 'Колонны', 'Кладка_наружная', 'Перегородки', 'ЛЕСТНИЦА', 'Надписи', 'Размеры', 'Окна']:
    doc.layers.add(n)
msp = doc.modelspace()
AX = [('1', 0), ('2', 6000), ('3', 12000), ('4', 18000)]
AY = [('А', 0), ('Б', 6000), ('В', 12000)]
ok = doc.blocks.new('ОК-1')
ok.add_lwpolyline([(-750, -150), (750, -150), (750, 150), (-750, 150)], close=True)
dv = doc.blocks.new('ДВ-1')
dv.add_arc((0, 0), 900, 0, 90)

def bubble(x, y, n):
    msp.add_circle((x, y), 400, dxfattribs={'layer': 'ОСИ'})
    msp.add_text(n, height=450, dxfattribs={'layer': 'Надписи'}).set_placement((x, y), align=ezdxf.enums.TextEntityAlignment.MIDDLE_CENTER)

def axes(ox, oy, rows=True):
    for n, x in AX:
        bubble(ox + x, oy - 2500, n)
        if rows: bubble(ox + x, oy + 14500, n)
    if rows:
        for n, y in AY:
            bubble(ox - 2500, oy + y, n)
            bubble(ox + 20500, oy + y, n)

def rect(x0, y0, x1, y1, layer):
    msp.add_lwpolyline([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], close=True, dxfattribs={'layer': layer})

def plan(ox, oy, title, windows):
    axes(ox, oy)
    msp.add_text(title, height=600, dxfattribs={'layer': 'Надписи'}).set_placement((ox, oy + 16000))
    t = 300
    rect(ox - 150, oy - 150, ox + 18150, oy + 150, 'Кладка_наружная')
    rect(ox - 150, oy + 11850, ox + 18150, oy + 12150, 'Кладка_наружная')
    rect(ox - 150, oy + 150, ox + 150, oy + 11850, 'Кладка_наружная')
    rect(ox + 17850, oy + 150, ox + 18150, oy + 11850, 'Кладка_наружная')
    for _, x in AX:
        for _, y in AY:
            rect(ox + x - 200, oy + y - 200, ox + x + 200, oy + y + 200, 'Колонны')
    rect(ox + 6000, oy + 150, ox + 6120, oy + 11850, 'Перегородки')
    rect(ox + 12000, oy + 6000, ox + 17850, oy + 6120, 'Перегородки')
    rect(ox + 6500, oy + 6500, ox + 9500, oy + 11500, 'ЛЕСТНИЦА')
    for i in range(windows):
        x = 1500 + i * 3000
        if x > 17000: break
        msp.add_blockref('ОК-1', (ox + x, oy), dxfattribs={'layer': 'Окна'})
        msp.add_blockref('ОК-1', (ox + x, oy + 12000), dxfattribs={'layer': 'Окна'})
    d = msp.add_linear_dim(base=(ox, oy - 1200), p1=(ox, oy), p2=(ox + 6000, oy), dimstyle='M100', dxfattribs={'layer': 'Размеры'})
    d.render()

S = 40000
plan(0, 0, 'План 1-го этажа на отм. ±0.000', 6)
msp.add_blockref('ДВ-1', (9000, 0), dxfattribs={'layer': 'Окна'})
plan(S, 0, 'План типового этажа (2-5) на отм. +3.300...+13.200', 6)
plan(2 * S, 0, 'План кровли', 0)

# kesim: pastda o'qlar, chap tomonda belgilar
ox = 3 * S
for n, x in AX:
    bubble(ox + x, -2500, n)
msp.add_text('Разрез 1-1', height=600, dxfattribs={'layer': 'Надписи'}).set_placement((ox, 19500))
for lv in [0, 3300, 6600, 9900, 13200, 16500]:
    msp.add_line((ox - 2000, lv), (ox + 18000, lv), dxfattribs={'layer': 'Размеры'})
    msp.add_text(('±0.000' if lv == 0 else '+%.3f' % (lv / 1000)), height=300, dxfattribs={'layer': 'Надписи'}).set_placement((ox - 3500, lv + 100))
msp.add_text('+17.400', height=300, dxfattribs={'layer': 'Надписи'}).set_placement((ox - 3500, 17500))
msp.add_text('-0.450', height=300, dxfattribs={'layer': 'Надписи'}).set_placement((ox - 3500, -350))

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ar_loyiha.dxf')
doc.saveas(out)
print(out)
