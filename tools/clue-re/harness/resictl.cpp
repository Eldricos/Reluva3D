#include "engines/clue/formats/resi.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
using namespace reluva::clue;
int main(int, char** argv) {
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
    Resi r; if (parseResi(b, r)) return 1;
    std::map<std::string,int> h; std::map<std::string,std::string> ex;
    for (auto& d : r.dialogs) for (auto& k : d.controls) {
        char key[96]; std::snprintf(key, sizeof key, "type %d f12 %d f13 %d f20 %d", k.type, k.raw[12] > 100000 ? -999 : k.raw[12], k.raw[13] > 100000 ? -999 : k.raw[13], k.raw[20]);
        if (!h[key]++) ex[key] = "dlg " + std::to_string(d.id) + " id " + std::to_string(k.id) + " '" + k.text.substr(0, 20) + "' " + k.animation;
    }
    for (auto& [k, n] : h) std::printf("%-40s x%-4d %s\n", k.c_str(), n, ex[k].c_str());
}
