raw = open(r'E:\project\test\keil_keygen\KeilKeygen_Asm\keil.fc14', 'rb').read()
lines = []
for i in range(0, len(raw), 24):
    lines.append('    "' + ''.join('\\x%02x' % b for b in raw[i:i + 24]) + '"')
out = ('/* Keil uVision keygen theme - FC14 module, %d bytes */\n'
       '#define MOD_SIZE %d\n'
       'static const char g_mod[] =\n%s;\n' % (len(raw), len(raw), '\n'.join(lines)))
open('mod.h', 'w', encoding='utf-8').write(out)
print('mod.h', len(raw))
