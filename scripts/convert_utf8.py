with open('E:/reSF2/assets/models/skeleton.xml', 'r', encoding='cp1251') as f:
    content = f.read()
content2 = content.replace('encoding="Windows-1251"', 'encoding="UTF-8"')
with open('E:/reSF2/assets/models/skeleton_utf8.xml', 'w', encoding='utf-8') as f:
    f.write(content2)
print('OK:', len(content2))
