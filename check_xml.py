import xml.etree.ElementTree as ET
tree = ET.parse('sf2_pc/www/res/locations/dojo/dojo_params.b78df4b4.xml')
root = tree.getroot()
for layer in root.findall('Layer'):
    t = layer.get('Type')
    f = layer.get('Factor')
    a = layer.get('Atlas')
    print('Type=%s Factor=%s Atlas=%s' % (t, f, a))
    for img in layer.findall('Image'):
        print('  Image: %s' % img.get('ClassName'))
    for mv in layer.findall('ModelsViewer'):
        px = mv.get('PlayerPositionX')
        py = mv.get('PlayerPositionY')
        ex = mv.get('EnemyPositionX')
        ey = mv.get('EnemyPositionY')
        print('  ModelsViewer: Player=(%s,%s) Enemy=(%s,%s)' % (px, py, ex, ey))