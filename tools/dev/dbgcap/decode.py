import struct, sys, collections
NAMES = {1:'pix',2:'lo_color',3:'hi_depthcov',4:'scene_depth_159',5:'tex13.x',6:'tex13.y',7:'tex13.z',8:'tex13.w',9:'hair_depth_154',10:'cov2_150',
 11:'ndc_x_188',12:'ndc_y_190',13:'invw_236',14:'pos_x_237',15:'pos_y_238',16:'pos_z_239',17:'clip_w_278',18:'uv_x_284',19:'uv_y_285',20:'dist_309',
 21:'fog324.x',22:'fog324.y',23:'fog324.z',24:'fog324.w',25:'_347',26:'fog392.x',27:'fog392.y',28:'fog392.z',29:'fog392.w',30:'_400',31:'_1155',32:'_1156',
 33:'_802',34:'_809',35:'_1141',36:'_1142',37:'_1143',38:'_1144',39:'_1136',40:'_1137',41:'_1138',42:'_1139',43:'_931',44:'_932',45:'_933',46:'_934',
 47:'_1125',48:'_1126',49:'_1127',50:'_1128',51:'out_r_1121',52:'out_g_1122',53:'out_b_1123',54:'out_a_1124',57:'_1100',58:'_1099',59:'_894',60:'_904',61:'_1140',62:'_919',63:'blown'}
data = open(sys.argv[1], 'rb').read(); off = 0; recs = []
while off + 12 <= len(data):
    assert data[off:off+8] == b'MVKDBG01'; n = struct.unpack_from('<I', data, off+8)[0]; off += 12
    for i in range(n): recs.append(struct.unpack_from('<64I', data, off + i*256))
    off += n*256
recs = [r for r in recs if r[0] == 0xD1A60001]
print(len(recs), 'records;', sum(1 for r in recs if r[63] == 1), 'blown')
f = lambda u: struct.unpack('<f', struct.pack('<I', u))[0]
def show(group, title):
    print('==', title, len(group))
    for i, name in sorted(NAMES.items()):
        vals = [r[i] for r in group if r[i] != 0xFFFFFFFF]
        if not vals: print(f'  {name:16s} (not reached)'); continue
        if i in (1, 2, 3, 63): print(f'  {name:16s} e.g. ' + ' '.join(f'{v:08x}' for v in vals[:4])); continue
        fv = sorted(f(v) for v in vals)
        print(f'  {name:16s} n={len(fv):5d} min={fv[0]:.4g} med={fv[len(fv)//2]:.4g} max={fv[-1]:.4g}')
show([r for r in recs if r[63] == 1], 'BLOWN pixels')
show([r for r in recs if r[63] == 0], 'NORMAL hair pixels (reference)')
