with open('E:/reSF2/engine/game/asset_manager.cpp', 'r', encoding='utf-8', errors='replace') as f:
    lines = f.readlines()

# Find the start of load_body_model (original XML-parsing version)
start_line = None
for i, line in enumerate(lines):
    if 'void AssetManager::load_body_model(const std::string& asset_root' in line:
        start_line = i
        break

if start_line is None:
    print('NOT FOUND')
    exit(1)

# Find the end of load_sounds (the last function we want to replace)
# Look for the closing brace of load_sounds which is the last xml-loading function
end_line = None
for i in range(len(lines)-1, 0, -1):
    if 'void AssetManager::load_sounds' in lines[i]:
        # Find matching closing brace
        depth = 0
        j = i
        while j < len(lines):
            depth += lines[j].count('{') - lines[j].count('}')
            if depth == 0 and j > i:
                end_line = j
                break
            j += 1
        break

if end_line is None:
    # Fallback: find the end of load_moves
    for i, line in enumerate(lines):
        if 'void AssetManager::load_moves' in line:
            end_line = None
            depth = 0
            j = i
            while j < len(lines):
                depth += lines[j].count('{') - lines[j].count('}')
                if depth == 0 and j > i:
                    end_line = j
                    break
                j += 1
            break

if end_line is None:
    print('Could not find end')
    exit(1)

print(f'Replacing lines {start_line+1} to {end_line+1}')

stubs = '''// ── STUB: load_body_model ──
void AssetManager::load_body_model(const std::string&, const std::string&, bool is_bag) {
    auto& bm = is_bag ? punching_bag_model_ : body_model_;
    bm = std::make_unique<BodyModel>();
    struct {const char* name; float x,y,z;} bnodes[] = {
        {"NPivot",0,169.48f,0},{"NTop",0,283.48f,0},
        {"NHip_1",-4.95f,171.1f,0},{"NHip_2",-26.71f,167.87f,0},
        {"NKnee_1",0,100,0},{"NKnee_2",0,100,0},
        {"NAnkle_1",0,50,0},{"NAnkle_2",0,50,0},
        {"NToe_1",0,20,0},{"NToe_2",0,20,0},
        {"NShoulder_1",0,250,0},{"NShoulder_2",0,250,0},
        {"NElbow_1",0,210,0},{"NElbow_2",0,210,0},
        {"NWrist_1",0,170,0},{"NWrist_2",0,170,0},
    };
    for(auto&n:bnodes){BodyNode bn;bn.name=n.name;bn.x=n.x;bn.y=n.y;bn.z=n.z;bm->nodes[n.name]=bn;}
    struct{const char*n,*e1,*e2;} edges[]={
        {"BE1","NTop","NPivot"},{"BE2","NPivot","NHip_1"},{"BE3","NHip_1","NKnee_1"},
        {"BE4","NKnee_1","NAnkle_1"},{"BE5","NAnkle_1","NToe_1"},
        {"BE6","NPivot","NHip_2"},{"BE7","NHip_2","NKnee_2"},{"BE8","NKnee_2","NAnkle_2"},
        {"BE9","NAnkle_2","NToe_2"},{"BE10","NPivot","NShoulder_1"},
        {"BE11","NShoulder_1","NElbow_1"},{"BE12","NElbow_1","NWrist_1"},
        {"BE13","NPivot","NShoulder_2"},{"BE14","NShoulder_2","NElbow_2"},{"BE15","NElbow_2","NWrist_2"},
    };
    for(auto&e:edges){BodyEdge be;be.name=e.n;be.end1=e.e1;be.end2=e.e2;bm->edges.push_back(be);}
    struct{const char*n;float r1,r2;} caps[]={
        {"BE1",5,5},{"BE2",4,4},{"BE3",3,3},{"BE4",2,2},{"BE5",2,2},
        {"BE6",4,4},{"BE7",3,3},{"BE8",2,2},{"BE9",2,2},
        {"BE10",4,4},{"BE11",3,3},{"BE12",2,2},
        {"BE13",4,4},{"BE14",3,3},{"BE15",2,2},
    };
    for(auto&c:caps){BodyCapsule bc;bc.edge_name=c.n;bc.radius1=c.r1;bc.radius2=c.r2;bm->capsules.push_back(bc);}
    std::printf("[body] hardcoded %zu nodes %zu edges %zu caps\\n",bm->nodes.size(),bm->edges.size(),bm->capsules.size());
}

// ── STUB: load_punching_bag_model ──
void AssetManager::load_punching_bag_model(const std::string&) {
    load_body_model("","",true);
    std::printf("[bag] hardcoded\\n");
}

// ── STUB: load_animations ──
void AssetManager::load_animations(const std::string&,const std::string&) {
    std::printf("[anims] skip\\n");
}

// ── STUB: load_moves ──
void AssetManager::load_moves(const std::string&) {
    std::printf("[moves] skip\\n");
}

// ── STUB: load_hud_textures ──
void AssetManager::load_hud_textures(const std::string&) {
    std::printf("[hud] skip\\n");
}

// ── STUB: load_menu_textures ──
void AssetManager::load_menu_textures(const std::string&) {
    std::printf("[menu] skip\\n");
}

// ── STUB: load_hud_font ──
void AssetManager::load_hud_font(const std::string&) {
    std::printf("[font] skip\\n");
}

// ── STUB: load_sounds ──
void AssetManager::load_sounds(const std::string&) {
    std::printf("[sounds] skip\\n");
}

'''

new_lines = lines[:start_line] + [stubs] + lines[end_line+1:]
with open('E:/reSF2/engine/game/asset_manager.cpp', 'w', encoding='utf-8') as f:
    f.writelines(new_lines)
print('OK')
