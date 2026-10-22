#include "catalog.h"
#include <cassert>
#include <fstream>
#include <unistd.h>

int main() {
    char path[] = "/tmp/moonlight-catalog-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    auto write = [&](const std::string &text) { std::ofstream(path) << text; };
    auto rejects = [&]() {
        bool rejected = false;
        try { (void)read_catalog(path); } catch (const std::exception &) { rejected = true; }
        assert(rejected);
    };
    write("apps = ({ name = \"Titulo & \\\"Beta\\\"\"; cover = \"/tmp/cover.png\"; });");
    auto apps = read_catalog(path);
    assert(apps.size() == 1 && apps[0].name == "Titulo & \"Beta\"" && apps[0].cover == "/tmp/cover.png");
    write("apps = ();");
    assert(read_catalog(path).empty());
    write("apps = 3;"); rejects();
    write("apps = ({ name = \"\"; cover = \"\"; });"); rejects();
    write("apps = ({ name = \"One\"; });"); rejects();
    write("apps = ({ name = \"Bad\\nName\"; cover = \"\"; });"); rejects();
    write("apps = ({ name = 12; cover = \"\"; });"); rejects();
    write("apps = ({ name = \"" + std::string(513, 'a') + "\"; cover = \"\"; });"); rejects();
    {
        std::ofstream large(path);
        large.seekp(2 * 1024 * 1024);
        large.put('x');
    }
    rejects();
    std::filesystem::remove(path);
}
