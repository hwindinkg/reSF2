#include "../engine/format/xml_doc.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
using namespace resf2::format;
int main(int argc,char**argv){
    std::ifstream f(argv[1],std::ios::binary); std::ostringstream ss; ss<<f.rdbuf();
    auto s=ss.str();
    XmlDocument d;
    bool ok=d.parse(s);
    std::printf("parse=%d err='%s'\n",(int)ok,d.error().c_str());
    if(auto*r=d.root()){
        std::printf("root='%s' children=%zu\n",r->name.c_str(),r->children.size());
        int n=0;
        for(auto&c:r->children){ std::printf("  child '%s' attrs=%zu kids=%zu\n",c.name.c_str(),c.attributes.size(),c.children.size()); if(++n>=6)break; }
    } else std::printf("no root\n");
}
